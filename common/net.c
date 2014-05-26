/*  
 * Copyright (c) 2005,2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * Network Utility functions.
 */

#include "utils.h"
#include "net.h"
#include "crc.h"

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

#if HAS_RFC2553
/* Convert in IPv6 address into a string */
char *n_ipv6_ntoa(char *buffer,n_ipv6_addr_t *ipv6_addr)
{
   return((char *)inet_ntop(AF_INET6,ipv6_addr,buffer,INET6_ADDRSTRLEN));
}
#endif

/* Convert a string containing an IP address in binary */
int n_ip_aton(n_ip_addr_t *ip_addr,char *ip_str)
{
   struct in_addr addr;

   if (inet_aton(ip_str,&addr) == 0)
      return(-1);

   *ip_addr = ntohl(addr.s_addr);
   return(0);
}

#if HAS_RFC2553
/* Convert an IPv6 address from string into binary */
int n_ipv6_aton(n_ipv6_addr_t *ipv6_addr,char *ip_str)
{
   return(inet_pton(AF_INET6,ip_str,ipv6_addr));
}
#endif

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

#if HAS_RFC2553
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
#endif

/* Parse a processor board id and return the eeprom settings in a buffer */
int parse_board_id(m_uint8_t *buf,const char *id,int encode)
{
  // Encode the serial board id
  //   encode 4 maps this into 4 bytes
  //   encode 9 maps into 9 bytes
  //   encode 11 maps into 11 bytes
  //

  memset(buf,0,11);
  if (encode == 4) {
    int res;

    int v;
    res = sscanf(id,"%d",&v);
    if (res != 1) return (-1);
    buf[3] = (v & 0xFF); v>>=8;
    buf[2] = (v & 0xFF); v>>=8;
    buf[1] = (v & 0xFF); v>>=8;
    buf[0] = (v & 0xFF); v>>=8;
    //printf("%x %x %x %x \n",buf[0],buf[1],buf[2],buf[3]);
    return (0);
  } else if (encode == 9) {
    int res;
  
    res = sscanf(id,"%c%c%c%2hx%2hx%c%c%c%c",
      &buf[0],&buf[1],
      &buf[2],(unsigned short int *)&buf[3],
      (unsigned short int *)&buf[4],&buf[5],
      &buf[6],&buf[7],&buf[8]
    );
    if (res != 9) return (-1);
    //printf("%x %x %x %x %x %x %x %x .. %x\n",
    //     buf[0],buf[1],buf[2],buf[3],
    //     buf[4],buf[5],buf[6],buf[7]  ,buf[8]);
    return (0);
  } else if (encode == 11) {
    int res;
  
    res = sscanf(id,"%c%c%c%c%c%c%c%c%c%c%c",
      &buf[0],&buf[1],
      &buf[2],&buf[3],
      &buf[4],&buf[5],
      &buf[6],&buf[7],
      &buf[8],&buf[9],
      &buf[10]
    );
    if (res != 11) return (-1);
    //printf("%x %x %x %x %x %x %x %x %x %x .. %x\n",
    //  buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],
    //  buf[6],buf[7],buf[8],buf[9],       buf[10]);
    return (0);
  }
  return (-1);
}

/* Parse a MAC address */
int parse_mac_addr(n_eth_addr_t *addr,char *str)
{
   u_int v[N_ETH_ALEN];
   int i,res;

   /* First try, standard format (00:01:02:03:04:05) */
   res = sscanf(str,"%x:%x:%x:%x:%x:%x",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5]);

   if (res == 6) {
      for(i=0;i<N_ETH_ALEN;i++)
         addr->eth_addr_byte[i] = v[i];
      return(0);
   }

   /* Second try, Cisco format (0001.0002.0003) */
   res = sscanf(str,"%x.%x.%x",&v[0],&v[1],&v[2]);

   if (res == 3) {
      addr->eth_addr_byte[0] = (v[0] >> 8) & 0xFF;
      addr->eth_addr_byte[1] = v[0] & 0xFF;
      addr->eth_addr_byte[2] = (v[1] >> 8) & 0xFF;
      addr->eth_addr_byte[3] = v[1] & 0xFF;
      addr->eth_addr_byte[4] = (v[2] >> 8) & 0xFF;
      addr->eth_addr_byte[5] = v[2] & 0xFF;
      return(0);
   }

   return(-1);
}

/* Convert an Ethernet address into a string */
char *n_eth_ntoa(char *buffer,n_eth_addr_t *addr,int format)
{
   char *str_format;

   if (format == 0) {
      str_format = "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x";
   } else {
      str_format = "%2.2x%2.2x.%2.2x%2.2x.%2.2x%2.2x";
   }

   sprintf(buffer,str_format,
           addr->eth_addr_byte[0],addr->eth_addr_byte[1],
           addr->eth_addr_byte[2],addr->eth_addr_byte[3],
           addr->eth_addr_byte[4],addr->eth_addr_byte[5]);
   return(buffer);
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
      if ((sck = socket(res->ai_family,SOCK_DGRAM,res->ai_protocol)) < 0) {
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
            /* shouldn't happen */
            close(sck);
            sck = -1;
            continue;
      }

      /* try to connect to remote host */
      if (!bind(sck,(struct sockaddr *)&st,res->ai_addrlen) &&
          !connect(sck,res->ai_addr,res->ai_addrlen))
         break;

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
      return(-1);
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

#if HAS_RFC2553
/* Listen on the specified port */
int ip_listen(char *ip_addr,int port,int sock_type,int max_fd,int fd_array[])
{
   struct addrinfo hints,*res,*res0;
   char port_str[20],*addr;
   int nsock,error,i;
   int reuse = 1;

   for(i=0;i<max_fd;i++)
      fd_array[i] = -1;

   memset(&hints,0,sizeof(hints));
   hints.ai_family = PF_UNSPEC;
   hints.ai_socktype = sock_type;
   hints.ai_flags = AI_PASSIVE;

   snprintf(port_str,sizeof(port_str),"%d",port);
   addr = (ip_addr && strlen(ip_addr)) ? ip_addr : NULL;

   if ((error = getaddrinfo(addr,port_str,&hints,&res0)) != 0) {
      fprintf(stderr,"ip_listen: %s", gai_strerror(error));
      return(-1);
   }

   nsock = 0;
   for(res=res0;(res && (nsock < max_fd));res=res->ai_next)
   {
      if ((res->ai_family != PF_INET) && (res->ai_family != PF_INET6))
         continue;

      fd_array[nsock] = socket(res->ai_family,res->ai_socktype,
                               res->ai_protocol);

      if (fd_array[nsock] < 0)
         continue;      

      setsockopt(fd_array[nsock],SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

      if ((bind(fd_array[nsock],res->ai_addr,res->ai_addrlen) < 0) ||
          ((sock_type == SOCK_STREAM) && (listen(fd_array[nsock],5) < 0)))
      {
         close(fd_array[nsock]);
         fd_array[nsock] = -1;
         continue;
      }

      nsock++;
   }

   freeaddrinfo(res0);
   return(nsock);
}
#else
/* Listen on the specified port */
int ip_listen(char *ip_addr,int port,int sock_type,int max_fd,int fd_array[])
{
   struct sockaddr_in sin;
   int i,sck,reuse=1;

   for(i=0;i<max_fd;i++)
      fd_array[i] = -1;

   if ((sck = socket(AF_INET,sock_type,0)) < 0) {
      perror("ip_listen: socket");
      return(-1);
   }

   /* bind local port */
   memset(&sin,0,sizeof(sin));
   sin.sin_family = PF_INET;
   sin.sin_port   = htons(port);

   if (ip_addr && strlen(ip_addr))
      sin.sin_addr.s_addr = inet_addr(ip_addr);

   setsockopt(sck,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

   if (bind(sck,(struct sockaddr *)&sin,sizeof(sin)) < 0) {
      perror("ip_listen: bind");
      goto error;
   }

   if ((sock_type == SOCK_STREAM) && (listen(sck,5) < 0)) {
      perror("ip_listen: listen");
      goto error;
   }

   fd_array[0] = sck;
   return(1);

 error:
   close(sck);
   return(-1);
}
#endif

#if HAS_RFC2553
/* Get port in an address info structure */
static int ip_socket_get_port(struct sockaddr *addr)
{
   switch(addr->sa_family) {
      case AF_INET:
         return(ntohs(((struct sockaddr_in *)addr)->sin_port));
      case AF_INET6:
         return(ntohs(((struct sockaddr_in6 *)addr)->sin6_port));
      default:
         fprintf(stderr,"ip_socket_get_port: unknown address family %d\n",
                 addr->sa_family);
         return(-1);
   }
}

/* Set port in an address info structure */
static int ip_socket_set_port(struct sockaddr *addr,int port)
{
   if (!addr)
      return(-1);
   
   switch(addr->sa_family) {
      case AF_INET:
         ((struct sockaddr_in *)addr)->sin_port = htons(port);
         return(0);
                  
      case AF_INET6:
         ((struct sockaddr_in6 *)addr)->sin6_port = htons(port);
         return(0);
         
      default:
         fprintf(stderr,"ip_socket_set_port: unknown address family %d\n",
                 addr->sa_family);
         return(-1);
   }
}

/* Try to create a socket and bind to the specified address info */
static int ip_socket_bind(struct addrinfo *addr)
{
   int fd,off=0;
   
   if ((fd = socket(addr->ai_family,addr->ai_socktype,addr->ai_protocol)) < 0)
      return(-1);
      
#ifdef IPV6_V6ONLY
   setsockopt(fd,IPPROTO_IPV6,IPV6_V6ONLY,&off,sizeof(off));
#endif

   if ( (bind(fd,addr->ai_addr,addr->ai_addrlen) < 0) ||
        ((addr->ai_socktype == SOCK_STREAM) && (listen(fd,5) < 0)) )
   {
      close(fd);
      return(-1);
   }

   return(fd);
}

/* Listen on a TCP/UDP port - port is choosen in the specified range */
int ip_listen_range(char *ip_addr,int port_start,int port_end,int *port,
                    int sock_type)
{
   struct addrinfo hints,*res,*res0;
   struct sockaddr_storage st;
   socklen_t st_len;
   char port_str[20],*addr;
   int error,i,fd = -1;

   memset(&hints,0,sizeof(hints));
   hints.ai_family   = PF_UNSPEC;
   hints.ai_socktype = sock_type;
   hints.ai_flags    = AI_PASSIVE;

   snprintf(port_str,sizeof(port_str),"%d",port_start);
   addr = (ip_addr && strlen(ip_addr)) ? ip_addr : NULL;

   if ((error = getaddrinfo(addr,port_str,&hints,&res0)) != 0) {
      fprintf(stderr,"ip_listen_range: %s", gai_strerror(error));
      return(-1);
   }

   for(i=port_start;i<=port_end;i++) {
      for(res=res0;res!=NULL;res=res->ai_next) {
         ip_socket_set_port(res->ai_addr,i);
         
         if ((fd = ip_socket_bind(res)) >= 0) {
            st_len = sizeof(st);
            getsockname(fd,(struct sockaddr *)&st,&st_len);
            *port = ip_socket_get_port((struct sockaddr *)&st);
            goto done;
         }
      }
   }
   
 done:
   freeaddrinfo(res0);
   return(fd);
}

/* Connect an existing socket to connect to specified host */
int ip_connect_fd(int fd,char *remote_host,int remote_port)
{
   struct addrinfo hints,*res,*res0;
   char port_str[20];
   int error;

   memset(&hints,0,sizeof(hints));
   hints.ai_family = PF_UNSPEC;

   snprintf(port_str,sizeof(port_str),"%d",remote_port);

   if ((error = getaddrinfo(remote_host,port_str,&hints,&res0)) != 0) {
      fprintf(stderr,"%s\n",gai_strerror(error));
      return(-1);
   }

   for(res=res0;res;res=res->ai_next) {
      if ((res->ai_family != PF_INET) && (res->ai_family != PF_INET6))
         continue;

      if (!connect(fd,res->ai_addr,res->ai_addrlen))
         break;
   }

   freeaddrinfo(res0);
   return(0);
}
#else
/* Try to create a socket and bind to the specified address info */
static int ip_socket_bind(struct sockaddr_in *sin,int sock_type)
{
   int fd;
   
   if ((fd = socket(sin->sin_family,sock_type,0)) < 0)
      return(-1);
   
   if ( (bind(fd,(struct sockaddr *)sin,sizeof(*sin)) < 0) ||
        ((sock_type == SOCK_STREAM) && (listen(fd,5) < 0)) )
   {
      close(fd);
      return(-1);
   }
   
   return(fd);
}

/* Listen on a TCP/UDP port - port is choosen in the specified range */
int ip_listen_range(char *ip_addr,int port_start,int port_end,int *port,
                    int sock_type)
{
   struct hostent *hp;
   struct sockaddr_in sin;
   socklen_t len;
   int i,fd;

   memset(&sin,0,sizeof(sin));
   sin.sin_family = PF_INET;
   
   if (ip_addr && strlen(ip_addr)) {
      if (!(hp = gethostbyname(ip_addr))) {
         fprintf(stderr,"ip_listen_range: unable to resolve '%s'\n",ip_addr);
         return(-1);
      }
   
      memcpy(&sin.sin_addr,hp->h_addr_list[0],sizeof(struct in_addr));
   }
      
   for(i=port_start;i<=port_end;i++) {
      sin.sin_port = htons(i);
      
      if ((fd = ip_socket_bind(&sin,sock_type)) >= 0) {
         len = sizeof(sin);
         getsockname(fd,(struct sockaddr *)&sin,&len);
         *port = ntohs(sin.sin_port);
         return(fd);
      }
   }
   
   return(-1);
}

/* Connect an existing socket to connect to specified host */
int ip_connect_fd(int fd,char *remote_host,int remote_port)
{
   struct sockaddr_in sin;
   struct hostent *hp;
 
   if (!(hp = gethostbyname(remote_host))) {
      fprintf(stderr,"ip_connect_fd: unable to resolve '%s'\n",remote_host);
      return(-1);
   }
   
   /* try to connect to remote host */
   memset(&sin,0,sizeof(sin));
   memcpy(&sin.sin_addr,hp->h_addr_list[0],sizeof(struct in_addr));
   sin.sin_family = PF_INET;
   sin.sin_port   = htons(remote_port);

   return(connect(fd,(struct sockaddr *)&sin,sizeof(sin)));
}
#endif

/* Create a socket UDP listening in a port of specified range */
int udp_listen_range(char *ip_addr,int port_start,int port_end,int *port)
{
   return(ip_listen_range(ip_addr,port_start,port_end,port,SOCK_DGRAM));
}

#if HAS_RFC2553
#if !defined(IPV6_JOIN_GROUP) && defined(IPV6_ADD_MEMBERSHIP)
// cygwin doesn't have IPV6_JOIN_GROUP anymore (is copy of IPV6_ADD_MEMBERSHIP)
#define IPV6_JOIN_GROUP IPV6_ADD_MEMBERSHIP
#endif
/* Open a multicast socket */
int udp_mcast_socket(char *mcast_group,int mcast_port,
                     struct sockaddr *sa,int *sa_len)
{
   struct addrinfo hints,*res,*res0;
   struct sockaddr_storage st;
   int error, sck = -1, tmp = 1;
   char port_str[20];

   memset(&hints,0,sizeof(hints));
   hints.ai_family = PF_UNSPEC;
   hints.ai_socktype = SOCK_DGRAM;

   snprintf(port_str,sizeof(port_str),"%d",mcast_port);

   if ((error = getaddrinfo(mcast_group,port_str,&hints,&res0)) != 0) {
      fprintf(stderr,"%s\n",gai_strerror(error));
      return(-1);
   }

   for(res=res0;res;res=res->ai_next)
   {
      /* We want only IPv4 or IPv6 */
      if ((res->ai_family != PF_INET) && (res->ai_family != PF_INET6))
         continue;

      /* create new socket */
      if ((sck = socket(res->ai_family,SOCK_DGRAM,res->ai_protocol)) < 0) {
         perror("udp_mcast_socket: socket");
         continue;
      }

#ifdef SO_REUSEPORT
      setsockopt(sck,SOL_SOCKET,SO_REUSEPORT,&tmp,sizeof(tmp));
#endif
      setsockopt(sck,SOL_SOCKET,SO_REUSEADDR,&tmp,sizeof(tmp));

      /* bind to the mcast port */
      memset(&st,0,sizeof(st));
      
      switch(res->ai_family) {
         case PF_INET: {
            struct sockaddr_in *sin = (struct sockaddr_in *)&st;
            struct ip_mreq mreq;

            /* prepare the bind() call */
            sin->sin_family = PF_INET;
            sin->sin_port = htons(mcast_port);
            sin->sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;

            /* try to join the multicast group */
            memset(&mreq,0,sizeof(mreq));
            mreq.imr_multiaddr = sin->sin_addr;

            if (setsockopt(sck,IPPROTO_IP,IP_ADD_MEMBERSHIP,
                           &mreq,sizeof(mreq)) == -1) 
               goto next_try;

            break;
         }

         case PF_INET6: {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)&st;
            struct ipv6_mreq mreq;

            /* prepare the bind() call */
#ifdef SIN6_LEN
            sin6->sin6_len = res->ai_addrlen;
#endif
            sin6->sin6_family = PF_INET6;
            sin6->sin6_port = htons(mcast_port);
            sin6->sin6_addr = ((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;

            /* try to join the multicast group */
            memset(&mreq,0,sizeof(mreq));
            mreq.ipv6mr_multiaddr = sin6->sin6_addr;

            if (setsockopt(sck,IPPROTO_IPV6,IPV6_JOIN_GROUP,
                           &mreq,sizeof(mreq)) == -1)
               goto next_try;

            break;
         }

         default:
            /* shouldn't happen */
            goto next_try;
      }

      if (!bind(sck,(struct sockaddr *)&st,res->ai_addrlen)) {
         /* Prepare info for sendto() */
         memcpy(sa,res->ai_addr,res->ai_addrlen);
         *sa_len = res->ai_addrlen;
         break;
      }

   next_try:
      close(sck);
      sck = -1;
   }

   freeaddrinfo(res0);
   return(sck);
}
#else
int udp_mcast_socket(char *mcast_group,int mcast_port,
                     struct sockaddr *sa,int *sa_len)
{
   struct sockaddr_in sin;
   struct ip_mreq mreq;
   struct hostent *hp;
   int sck,tmp = 1;

   if (!(hp = gethostbyname(mcast_group))) {
      fprintf(stderr,"udp_mcast_connect: unable to resolve '%s'\n",
              mcast_group);
      return(-1);
   }

   if ((sck = socket(AF_INET,SOCK_DGRAM,0)) < 0) {
      perror("udp_mcast_socket: socket");
      return(-1);
   }

   /* Bind multicast port */
#ifdef SO_REUSEPORT
   setsockopt(sck,SOL_SOCKET,SO_REUSEPORT,&tmp,sizeof(tmp));
#endif
   setsockopt(sck,SOL_SOCKET,SO_REUSEADDR,&tmp,sizeof(tmp));

   memset(&sin,0,sizeof(sin));
   sin.sin_family = PF_INET;
   sin.sin_port = htons(mcast_port);
   memcpy(&sin.sin_addr,hp->h_addr_list[0],sizeof(struct in_addr));

   if (bind(sck,(struct sockaddr *)&sin,sizeof(sin)) < 0) {
      perror("udp_mcast_socket: bind");
      goto error;
   }

   /* Join the IP Multicast group */
   memset(&mreq,0,sizeof(mreq));
   mreq.imr_multiaddr = sin.sin_addr;
   
   if (setsockopt(sck,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof(mreq)) == -1) {
      perror("udp_mcast_socket: setsockopt");
      goto error;
   }

   /* Prepare info for sendto() */
   memset(&sin,0,sizeof(sin));
   sin.sin_family = PF_INET;
   sin.sin_port = htons(mcast_port);
   sin.sin_addr = mreq.imr_multiaddr;
   memcpy(sa,&sin,sizeof(sin));
   *sa_len = sizeof(sin);

   return(sck);

 error:
   close(sck);
   return(-1);
}
#endif

/* Set TTL for a multicast socket */
int udp_mcast_set_ttl(int sck,int ttl)
{
   setsockopt(sck,IPPROTO_IP,IP_MULTICAST_TTL,&ttl,sizeof(ttl));
#if HAS_RFC2553
   setsockopt(sck,IPPROTO_IPV6,IPV6_MULTICAST_HOPS,&ttl,sizeof(ttl));
#endif
   return(0);
}

/* 
 * ISL rewrite.
 *
 * See: http://www.cisco.com/en/US/tech/tk389/tk390/technologies_tech_note09186a0080094665.shtml
 */
void cisco_isl_rewrite(m_uint8_t *pkt,m_uint32_t tot_len)
{
   static m_uint8_t isl_xaddr[N_ETH_ALEN] = { 0x01,0x00,0x0c,0x00,0x10,0x00 };
   u_int real_offset,real_len;
   n_eth_hdr_t *hdr;
   m_uint32_t ifcs;

   hdr = (n_eth_hdr_t *)pkt;
   if (!memcmp(&hdr->daddr,isl_xaddr,N_ETH_ALEN)) {
      real_offset = N_ETH_HLEN + N_ISL_HDR_SIZE;
      real_len    = ntohs(hdr->type);
      real_len    -= (N_ISL_HDR_SIZE + 4);
   
      if ((real_offset+real_len) > tot_len)
         return;
   
      /* Rewrite the destination MAC address */
      hdr->daddr.eth_addr_byte[4] = 0x00;

      /* Compute the internal FCS on the encapsulated packet */
      ifcs = crc32_compute(0xFFFFFFFF,pkt+real_offset,real_len);
      pkt[tot_len-4] = ifcs & 0xff;
      pkt[tot_len-3] = (ifcs >> 8) & 0xff;
      pkt[tot_len-2] = (ifcs >> 16) & 0xff;
      pkt[tot_len-1] = ifcs >> 24;
   }
}

/* Verify checksum of an IP header */
int ip_verify_cksum(n_ip_hdr_t *hdr)
{
   m_uint8_t *p = (m_uint8_t *)hdr;
   m_uint32_t sum = 0;
   u_int len;

   len = (hdr->ihl & 0x0F) << 1;
   while(len-- > 0) {
      sum += ((m_uint16_t)p[0] << 8) | p[1];
      p += sizeof(m_uint16_t);
   }

   while(sum >> 16)
      sum = (sum & 0xFFFF) + (sum >> 16);

   return(sum == 0xFFFF);
}

/* Compute an IP checksum */
void ip_compute_cksum(n_ip_hdr_t *hdr)
{  
   m_uint8_t *p = (m_uint8_t *)hdr;
   m_uint32_t sum = 0;
   u_int len;

   hdr->cksum = 0;

   len = (hdr->ihl & 0x0F) << 1;
   while(len-- > 0) {
      sum += ((m_uint16_t)p[0] << 8) | p[1];
      p += sizeof(m_uint16_t);      
   }

   while(sum >> 16)
      sum = (sum & 0xFFFF) + (sum >> 16);

   hdr->cksum = htons(~sum);
}

/* Partial checksum (for UDP/TCP) */
static inline m_uint32_t ip_cksum_partial(m_uint8_t *buf,int len)
{
   m_uint32_t sum = 0;

   while(len > 1) {
      sum += ((m_uint16_t)buf[0] << 8) | buf[1];
      buf += sizeof(m_uint16_t);
      len -= sizeof(m_uint16_t);
   }

   if (len == 1)
      sum += (m_uint16_t)(*buf) << 8;
   
   return(sum);
}

/* Partial checksum test */
int ip_cksum_partial_test(void)
{
#define N_BUF  4
   m_uint8_t buffer[N_BUF][512];
   m_uint16_t psum[N_BUF];
   m_uint32_t tmp,sum,gsum;
   int i;

   for(i=0;i<N_BUF;i++) {
      m_randomize_block(buffer[i],sizeof(buffer[i]));
      //mem_dump(stdout,buffer[i],sizeof(buffer[i]));

      sum = ip_cksum_partial(buffer[i],sizeof(buffer[i]));

      while(sum >> 16)
         sum = (sum & 0xFFFF) + (sum >> 16);

      psum[i] = ~sum;
   }

   /* partial sums + accumulator */
   for(i=0,tmp=0;i<N_BUF;i++) {
      //printf("psum[%d] = 0x%4.4x\n",i,psum[i]);
      tmp += (m_uint16_t)(~psum[i]);
   }

   /* global sum */
   sum = ip_cksum_partial((m_uint8_t *)buffer,sizeof(buffer));

   while(sum >> 16)
      sum = (sum & 0xFFFF) + (sum >> 16);

   gsum = sum;

   /* accumulator */
   while(tmp >> 16)
      tmp = (tmp & 0xFFFF) + (tmp >> 16);

   //printf("gsum = 0x%4.4x, tmp = 0x%4.4x : %s\n",
   //       gsum,tmp,(gsum == tmp) ? "OK" : "FAILURE");

   return(tmp == gsum);
#undef N_BUF
}

/* Compute TCP/UDP checksum */
m_uint16_t pkt_ctx_tcp_cksum(n_pkt_ctx_t *ctx,int ph)
{
   m_uint32_t sum;
   m_uint16_t old_cksum = 0;
   u_int len;

   /* replace the actual checksum value with 0 to recompute it */
   if (!(ctx->flags & N_PKT_CTX_FLAG_IP_FRAG)) {
      switch(ctx->ip_l4_proto) {
         case N_IP_PROTO_TCP:
            old_cksum = ctx->tcp->cksum;
            ctx->tcp->cksum = 0;
            break;
         case N_IP_PROTO_UDP:
            old_cksum = ctx->udp->cksum;
            ctx->udp->cksum = 0;
            break;
      }
   }

   len = ntohs(ctx->ip->tot_len) - ((ctx->ip->ihl & 0x0F) << 2);
   sum = ip_cksum_partial(ctx->l4,len);
   
   /* include pseudo-header */
   if (ph) {
      sum += ip_cksum_partial((m_uint8_t *)&ctx->ip->saddr,8);
      sum += ctx->ip_l4_proto + len;
   }

   while(sum >> 16)
      sum = (sum & 0xFFFF) + (sum >> 16);

   /* restore the old value */
   if (!(ctx->flags & N_PKT_CTX_FLAG_IP_FRAG)) {
      switch(ctx->ip_l4_proto) {
         case N_IP_PROTO_TCP:
            ctx->tcp->cksum = old_cksum;
            break;
         case N_IP_PROTO_UDP:
            ctx->udp->cksum = old_cksum;
            break;
      }
   }

   return(~sum);
}

/* Analyze L4 for an IP packet */
int pkt_ctx_ip_analyze_l4(n_pkt_ctx_t *ctx)
{
   switch(ctx->ip_l4_proto) {
      case N_IP_PROTO_TCP:
         ctx->flags |= N_PKT_CTX_FLAG_L4_TCP;
         break;
      case N_IP_PROTO_UDP:
         ctx->flags |= N_PKT_CTX_FLAG_L4_UDP;
         break;
      case N_IP_PROTO_ICMP:
         ctx->flags |= N_PKT_CTX_FLAG_L4_ICMP;
         break;
   }

   return(TRUE);
}

/* Analyze a packet */
int pkt_ctx_analyze(n_pkt_ctx_t *ctx,m_uint8_t *pkt,size_t pkt_len)
{
   n_eth_dot1q_hdr_t *eth = (n_eth_dot1q_hdr_t *)pkt;
   m_uint16_t eth_type;
   m_uint8_t *p;

   ctx->pkt = pkt;
   ctx->pkt_len = pkt_len;
   ctx->flags = 0;
   ctx->vlan_id = 0;
   ctx->l3 = NULL;
   ctx->l4 = NULL;

   eth_type = ntohs(eth->type);
   p = PTR_ADJUST(m_uint8_t *,eth,N_ETH_HLEN);

   if (eth_type >= N_ETH_MTU) {
      if (eth_type == N_ETH_PROTO_DOT1Q) {
         ctx->flags |= N_PKT_CTX_FLAG_VLAN;
         ctx->vlan_id = htons(eth->vlan_id);

         /* override the ethernet type */
         eth_type = ntohs(*(m_uint16_t *)(p+2));

         /* skip 802.1Q header info */
         p += sizeof(m_uint32_t);
      }
   }

   if (eth_type < N_ETH_MTU) {
      /* LLC/SNAP: TODO */
      return(TRUE);
   } else {
      ctx->flags |= N_PKT_CTX_FLAG_ETHV2;
   }

   switch(eth_type) {
      case N_ETH_PROTO_IP: {
         n_ip_hdr_t *ip;
         u_int len,offset;

         ctx->flags |= N_PKT_CTX_FLAG_L3_IP;
         ctx->ip = ip = (n_ip_hdr_t *)p;

         /* Check header */
         if (((ip->ihl & 0xF0) != 0x40) || 
             ((len = ip->ihl & 0x0F) < N_IP_MIN_HLEN) ||
             ((len << 2) > ntohs(ip->tot_len)) || 
             !ip_verify_cksum(ctx->ip))
            return(TRUE);

         ctx->flags |= N_PKT_CTX_FLAG_IPH_OK;
         ctx->ip_l4_proto = ip->proto;
         ctx->l4 = PTR_ADJUST(void *,ip,len << 2);

         /* Check if the packet is a fragment */
         offset = ntohs(ip->frag_off);

         if (((offset & N_IP_OFFMASK) != 0) || (offset & N_IP_FLAG_MF))
            ctx->flags |= N_PKT_CTX_FLAG_IP_FRAG;
         break;
      }

      case N_ETH_PROTO_ARP:
         ctx->flags |= N_PKT_CTX_FLAG_L3_ARP;
         ctx->arp = (n_arp_hdr_t *)p;
         return(TRUE);

      default:
         /* other: unknown, stop now */
         return(TRUE);
   }

   return(TRUE);
}

/* Dump packet context */
void pkt_ctx_dump(n_pkt_ctx_t *ctx)
{
   printf("pkt=%p (len=%lu), flags=0x%8.8x, vlan_id=0x%4.4x, l3=%p, l4=%p\n",
          ctx->pkt,(u_long)ctx->pkt_len,ctx->flags,ctx->vlan_id,
          ctx->l3,ctx->l4);
}
