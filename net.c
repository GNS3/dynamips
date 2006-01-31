/*  
 * Copyright (c) 2005,2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * Network Utility functions.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "utils.h"
#include "net.h"

/* 
 * IP mask table, which allows to find quickly a network mask 
 * with a prefix length.
 */
n_ip_addr_t ip_masks[N_IP_ADDR_BITS+1] = {
   0x0, 
   0x80000000, 0xC0000000, 0xE0000000, 0xF0000000,
   0xF8000000, 0xFC000000, 0xFE000000, 0xFF000000,
   0xFF800000, 0xFFC00000, 0xFFE00000, 0xFFF00000,
   0xFFF80000, 0xFFFC0000, 0xFFFE0000, 0xFFFF0000,
   0xFFFF8000, 0xFFFFC000, 0xFFFFE000, 0xFFFFF000,
   0xFFFFF800, 0xFFFFFC00, 0xFFFFFE00, 0xFFFFFF00,
   0xFFFFFF80, 0xFFFFFFC0, 0xFFFFFFE0, 0xFFFFFFF0,
   0xFFFFFFF8, 0xFFFFFFFC, 0xFFFFFFFE, 0xFFFFFFFF
};

/* 
 * IPv6 mask table, which allows to find quickly a network mask 
 * with a prefix length. Note this is a particularly ugly way
 * to do this, since we use statically 2 Kb.
 */
n_ipv6_addr_t ipv6_masks[N_IPV6_ADDR_BITS+1];

/* Initialize IPv6 masks */
void ipv6_init_masks(void)
{
   int i,index;

   /* Set all bits to 1 */
   memset(ipv6_masks,0xff,sizeof(ipv6_masks));

   for(i=0;i<N_IPV6_ADDR_BITS;i++) 
   {
      index = i >> 3;  /* Compute byte index (divide by 8) */

      /* rotate byte */
      ipv6_masks[i].ip6.u6_addr8[index++] <<= (8 - (i & 7));

      /* clear following bytes */
      while(index<N_IPV6_ADDR_LEN)
         ipv6_masks[i].ip6.u6_addr8[index++] = 0;
   }
}

/* Convert an IPv4 address into a string */
char *n_ip_ntoa(char *buffer,n_ip_addr_t ip_addr)
{
   u_char *p = (u_char *)&ip_addr;
   sprintf(buffer,"%u.%u.%u.%u",p[0],p[1],p[2],p[3]);
   return(buffer);
}

/* Convert in IPv6 address into a string */
char *n_ipv6_ntoa(char *buffer,n_ipv6_addr_t *ipv6_addr)
{
   return((char *)inet_ntop(AF_INET6,ipv6_addr,buffer,INET6_ADDRSTRLEN));
}

/* Convert a string containing an IP address in binary */
int n_ip_aton(n_ip_addr_t *ip_addr,char *ip_str)
{
   struct in_addr addr;

   if (inet_aton(ip_str,&addr) == 0)
      return(-1);

   *ip_addr = ntohl(addr.s_addr);
   return(0);
}

/* Convert an IPv6 address from string into binary */
int n_ipv6_aton(n_ipv6_addr_t *ipv6_addr,char *ip_str)
{
   return(inet_pton(AF_INET6,ip_str,ipv6_addr));
}

/* Parse an IPv4 CIDR prefix */
int ip_parse_cidr(char *token,n_ip_addr_t *net_addr,n_ip_addr_t *net_mask)
{
   char *sl,*tmp,*err;
   u_long mask;

   /* Find separator */
   if ((sl = strchr(token,'/')) == NULL)
      return(-1);

   /* Get mask */
   mask = strtoul(sl+1,&err,0);
   if (*err != 0)
      return(-1);

   /* Ensure that mask has a correct value */
   if (mask > N_IP_ADDR_BITS)
      return(-1);

   if ((tmp = strdup(token)) == NULL)
      return(-1);
    
   sl = strchr(tmp,'/');
   *sl = 0;

   /* Parse IP Address */
   if (n_ip_aton(net_addr,tmp) == -1) {
      free(tmp);
      return(-1);
   }

   /* Set netmask */
   *net_mask = ip_masks[mask];

   free(tmp);
   return(0);
}

/* Parse an IPv6 CIDR prefix */
int ipv6_parse_cidr(char *token,n_ipv6_addr_t *net_addr,u_int *net_mask)
{
   char *sl,*tmp,*err;
   u_long mask;

   /* Find separator */
   if ((sl = strchr(token,'/')) == NULL)
      return(-1);

   /* Get mask */
   mask = strtoul(sl+1,&err,0);
   if (*err != 0)
      return(-1);

   /* Ensure that mask has a correct value */
   if (mask > N_IPV6_ADDR_BITS)
      return(-1);

   if ((tmp = strdup(token)) == NULL)
      return(-1);
    
   sl = strchr(tmp,'/');
   *sl = 0;

   /* Parse IP Address */
   if (n_ipv6_aton(net_addr,tmp) <= 0) {
      free(tmp);
      return(-1);
   }

   /* Set netmask */
   *net_mask = (u_int)mask;

   free(tmp);
   return(0);
}

#if HAS_RFC2553
/* Create a new socket to connect to specified host */
int udp_connect(int local_port,char *remote_host,int remote_port)
{
   struct addrinfo hints,*res,*res0;
   struct sockaddr_storage st;
   int error, sck = -1;
   char port_str[20];

   memset(&hints,0,sizeof(hints));
   hints.ai_family = PF_UNSPEC;
   hints.ai_socktype = SOCK_DGRAM;

   snprintf(port_str,sizeof(port_str),"%d",remote_port);

   if ((error = getaddrinfo(remote_host,port_str,&hints,&res0)) != 0) {
      fprintf(stderr,"%s\n",gai_strerror(error));
      return(-1);
   }

   for(res=res0;res;res=res->ai_next)
   {
      /* We want only IPv4 or IPv6 */
      if ((res->ai_family != PF_INET) && (res->ai_family != PF_INET6))
         continue;

      /* create new socket */
      if (!(sck = socket(res->ai_family,SOCK_DGRAM,res->ai_protocol))) {
         perror("udp_connect: socket");
         continue;
      }

      /* bind to the local port */
      memset(&st,0,sizeof(st));
      
      switch(res->ai_family) {
         case PF_INET: {
            struct sockaddr_in *sin = (struct sockaddr_in *)&st;
            sin->sin_family = PF_INET;
            sin->sin_port = htons(local_port);
            break;
         }

         case PF_INET6: {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&st;
#ifdef SIN6_LEN
            sin6->sin6_len = res->ai_addrlen;
#endif
            sin6->sin6_family = PF_INET6;
            sin6->sin6_port = htons(local_port);
            break;
         }

         default:
            /* cannot happen */
            continue;
      }

      if (bind(sck,(struct sockaddr *)&st,res->ai_addrlen) == -1)
         goto next;

      /* try to connect to remote host */
      if (!connect(sck,res->ai_addr,res->ai_addrlen))
         return(sck);

   next:
      close(sck);
      sck = -1;
   }

   freeaddrinfo(res0);
   return(sck);
}
#else
/* 
 * Create a new socket to connect to specified host.
 * Version for old systems that do not support RFC 2553 (getaddrinfo())
 *
 * See http://www.faqs.org/rfcs/rfc2553.html for more info.
 */
int udp_connect(int local_port,char *remote_host,int remote_port)
{ 
   struct sockaddr_in sin;
   struct hostent *hp;
   int sck;

   if (!(hp = gethostbyname(remote_host))) {
      fprintf(stderr,"udp_connect: unable to resolve '%s'\n",remote_host);
      return(-1);
   }

   if ((sck = socket(AF_INET,SOCK_DGRAM,0)) < 0) {
      perror("udp_connect: socket");
      return(-1);
   }

   /* bind local port */
   memset(&sin,0,sizeof(sin));
   sin.sin_family = PF_INET;
   sin.sin_port = htons(local_port);
   
   if (bind(sck,(struct sockaddr *)&sin,sizeof(sin)) < 0) {
      perror("udp_connect: bind");
      close(sck);
   }

   /* try to connect to remote host */
   memset(&sin,0,sizeof(sin));
   memcpy(&sin.sin_addr,hp->h_addr_list[0],sizeof(struct in_addr));
   sin.sin_family = PF_INET;
   sin.sin_port = htons(remote_port);

   if (connect(sck,(struct sockaddr *)&sin,sizeof(sin)) < 0) {
      perror("udp_connect: connect");
      close(sck);
   }

   return(sck);
}
#endif /* HAS_RFC2553 */
