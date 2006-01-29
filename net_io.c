/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Network Input/Output Abstraction Layer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifdef __linux__
#include <net/if.h>
#include <linux/if_tun.h>
#endif

#include "net.h"
#include "net_io.h"

/* NETIO types (must follow the enum definition) */
static char *netio_types[NETIO_TYPE_MAX] = {
   "unix",
   "tap",
   "udp",
   "tcp_cli",
   "tcp_ser",
#ifdef LINUX_ETH
   "linux_eth",
#endif
   "null",
};

/* Get NETIO type given a description */
int netio_get_type(char *desc)
{
   int i;

   for(i=0;i<NETIO_TYPE_MAX;i++)
      if (!strcmp(desc,netio_types[i]))
         return(i);

   return(-1);
}

/* Show the NETIO types */
void netio_show_types(void)
{
   int i;

   printf("Available NETIO types:\n");

   for(i=0;i<NETIO_TYPE_MAX;i++)
      printf("  * %s\n",netio_types[i]);

   printf("\n");
}

/*
 * =========================================================================
 * UNIX sockets
 * =========================================================================
 */

/* Create an UNIX socket */
static int netio_unix_create_socket(netio_unix_desc_t *nud)
{
   struct sockaddr_un ssun;
   size_t len;

   if ((nud->fd = socket(AF_UNIX,SOCK_DGRAM,0)) == -1) {
      perror("netio_unix: socket");
      return(-1);
   }

   memset(&ssun,0,sizeof(ssun));
   ssun.sun_family = AF_UNIX;
   strcpy(ssun.sun_path,nud->local_filename);
   len = strlen(nud->local_filename) + sizeof(ssun.sun_family);

   if (bind(nud->fd,(struct sockaddr *)&ssun,len) == -1) {
      perror("netio_unix: bind");
      return(-1);
   }

   return(nud->fd);
}

/* Free a NetIO unix descriptor */
static void netio_unix_free(netio_unix_desc_t *nud)
{
   if (nud->fd != -1)
      close(nud->fd);

   free(nud->local_filename);
}

/* Allocate a new NetIO UNIX descriptor */
static int netio_unix_create(netio_unix_desc_t *nud,char *local,char *remote)
{
   size_t len;

   if ((strlen(local) >= sizeof(nud->remote_sun.sun_path)) ||
       ((len = strlen(remote)) >= sizeof(nud->remote_sun.sun_path)))
      goto nomem_error;

   memset(nud,0,sizeof(*nud));
   nud->local_filename = strdup(local);

   if (!nud->local_filename)
      goto nomem_error;

   if (netio_unix_create_socket(nud) == -1)
      goto error;

   /* prepare the remote info */
   memset(&nud->remote_sun,0,sizeof(nud->remote_sun));
   nud->remote_sun.sun_family = AF_UNIX;
   strcpy(nud->remote_sun.sun_path,remote);
   len += sizeof(nud->remote_sun.sun_family);

   nud->remote_sun_len = len;
   return(0);

 nomem_error:
   perror("netio_unix_create: malloc");
 error:
   netio_unix_free(nud);
   return(-1);
}

/* Write a packet to an UNIX socket */
static ssize_t netio_unix_send(netio_unix_desc_t *nud,void *pkt,size_t pkt_len)
{
   return(sendto(nud->fd,pkt,pkt_len,0,
                 (struct sockaddr *)&nud->remote_sun,nud->remote_sun_len));
}

/* Receive a packet from an UNIX socket */
static ssize_t netio_unix_recv(netio_unix_desc_t *nud,void *pkt,size_t max_len)
{
   return(recvfrom(nud->fd,pkt,max_len,0,NULL,NULL));
}

/* Create a new NetIO descriptor with UNIX method */
netio_desc_t *netio_desc_create_unix(char *local,char *remote)
{
   netio_desc_t *desc;
   
   if (!(desc = malloc(sizeof(*desc))))
      return NULL;

   if (netio_unix_create(&desc->u.nud,local,remote) == -1)
      return NULL;

   desc->type = NETIO_TYPE_UNIX;
   desc->send = (void *)netio_unix_send;
   desc->recv = (void *)netio_unix_recv;
   desc->dptr = &desc->u.nud;
   return desc;
}

/*
 * =========================================================================
 * TAP devices
 * =========================================================================
 */

/* Free a NetIO TAP descriptor */
static void netio_tap_free(netio_tap_desc_t *ntd)
{
   if (ntd->fd != -1) 
      close(ntd->fd);
}

/* Open a TAP device */
static int netio_tap_open(char *tap_devname)
{     
#ifdef __linux__
   struct ifreq ifr;
   int fd,err;

   if ((fd = open("/dev/net/tun",O_RDWR)) < 0)
      return(-1);

   memset(&ifr,0,sizeof(ifr));
   
   /* Flags: IFF_TUN   - TUN device (no Ethernet headers)
    *        IFF_TAP   - TAP device
    *
    *        IFF_NO_PI - Do not provide packet information
    */
   ifr.ifr_flags = IFF_TAP|IFF_NO_PI;
   if (*tap_devname)
      strncpy(ifr.ifr_name,tap_devname,IFNAMSIZ);

   if ((err = ioctl(fd,TUNSETIFF,(void *)&ifr)) < 0) {
      close(fd);
      return err;
   }
   
   strcpy(tap_devname,ifr.ifr_name);
   return(fd);
#else
   int i,fd = -1;
   
   if (*tap_devname) {
      fd = open(tap_devname,O_RDWR);
   } else {
      for(i=0;i<16;i++) {
         snprintf(tap_devname,NETIO_DEV_MAXLEN,"/dev/tap%d",i);
      
         if ((fd = open(tap_devname,O_RDWR)) >= 0)
            break;
      }
   }

   return(fd);
#endif   
}

/* Allocate a new NetIO TAP descriptor */
static int netio_tap_create(netio_tap_desc_t *ntd,char *tap_name)
{
   if (strlen(tap_name) >= NETIO_DEV_MAXLEN) {
      fprintf(stderr,"netio_tap_create: bad TAP device string specified.\n");
      return(-1);
   }

   memset(ntd,0,sizeof(*ntd));
   strcpy(ntd->filename,tap_name);
   ntd->fd = netio_tap_open(ntd->filename);

   if (ntd->fd == -1) {
      fprintf(stderr,"netio_tap_create: unable to open TAP device %s (%s)\n",
              tap_name,strerror(errno));
      return(-1);
   }

   return(0);
}

/* Write a packet to a TAP device */
static ssize_t netio_tap_send(netio_tap_desc_t *ntd,void *pkt,size_t pkt_len)
{
   return(write(ntd->fd,pkt,pkt_len));
}

/* Receive a packet through a TAP device */
static ssize_t netio_tap_recv(netio_tap_desc_t *ntd,void *pkt,size_t max_len)
{
   return(read(ntd->fd,pkt,max_len));
}

/* Create a new NetIO descriptor with TAP method */
netio_desc_t *netio_desc_create_tap(char *tap_name)
{
   netio_tap_desc_t *ntd;
   netio_desc_t *desc;
   
   if (!(desc = malloc(sizeof(*desc))))
      return NULL;

   ntd = &desc->u.ntd;

   if (netio_tap_create(ntd,tap_name) == -1) {
      free(desc);
      return NULL;
   }

   m_log("TAP","allocated device %s\n",ntd->filename);
   
   desc->type = NETIO_TYPE_TAP;
   desc->send = (void *)netio_tap_send;
   desc->recv = (void *)netio_tap_recv;
   desc->dptr = &desc->u.ntd;
   return desc;
}

/*
 * =========================================================================
 * TCP sockets
 * =========================================================================
 */

/* Free a NetIO TCP descriptor */
static void netio_tcp_free(netio_inet_desc_t *nid)
{
   if (nid->fd != -1) 
      close(nid->fd);
}

/*
 * very simple protocol to send packets over tcp
 * 32 bits in network format - size of packet, then packet itself and so on.
 */
static ssize_t netio_tcp_send(netio_inet_desc_t *nid,void *pkt,size_t pkt_len)
{
   u_long l = htonl(pkt_len);

   if (write(nid->fd,&l,sizeof(l)) == -1)
      return(-1);

   return(write(nid->fd,pkt,pkt_len));
}

static ssize_t netio_tcp_recv(netio_inet_desc_t *nid,void *pkt,size_t max_len)
{
   u_long l;

   if (read(nid->fd,&l,sizeof(l)) != sizeof(l))
      return(-1);

   if (ntohl(l) > max_len)
      return(-1);

   return(read(nid->fd,pkt,ntohl(l)));
}

static int netio_tcp_cli_create(netio_inet_desc_t *nid,char *host,char *port)
{
   struct sockaddr_in serv;
   struct servent *sp;
   struct hostent *hp;

   if ((nid->fd = socket(PF_INET,SOCK_STREAM,0)) < 0) {
      perror("netio_tcp_cli_create: socket");
      return(-1);
   }

   memset(&serv,0,sizeof(serv));
   serv.sin_family = AF_INET;

   if (atoi(port) == 0) {
      if (!(sp = getservbyname(port,"tcp"))) {
         fprintf(stderr,"netio_tcp_cli_create: port %s is neither "
                 "number not service %s\n",port,strerror(errno));
         close(nid->fd);
         return(-1);
      }
      serv.sin_port = sp->s_port;
   } else
      serv.sin_port = htons(atoi(port));

   if (inet_addr(host) == INADDR_NONE) {
      if (!(hp = gethostbyname(host))) {
         fprintf(stderr,"netio_tcp_cli_create: no host %s\n",host);
         close(nid->fd);
         return(-1);
      }
      serv.sin_addr.s_addr = *hp->h_addr;
   } else
      serv.sin_addr.s_addr = inet_addr(host);

   if (connect(nid->fd,(struct sockaddr *)&serv,sizeof(serv)) < 0) {
      fprintf(stderr,"netio_tcp_cli_create: connect to %s:%s failed %s\n",
              host,port,strerror(errno));
      close(nid->fd);
      return(-1);
   }
   return(0);
}

/* Create a new NetIO descriptor with TCP_CLI method */
netio_desc_t *netio_desc_create_tcp_cli(char *host,char *port)
{
   netio_desc_t *desc;
   
   if (!(desc = malloc(sizeof(*desc))))
      return NULL;

   if (netio_tcp_cli_create(&desc->u.nid,host,port) < 0) {
      free(desc);
      return NULL;
   }

   desc->type = NETIO_TYPE_TCP_CLI;
   desc->send = (void *)netio_tcp_send;
   desc->recv = (void *)netio_tcp_recv;
   desc->dptr = &desc->u.nid;
   return desc;
}

static int netio_tcp_ser_create(netio_inet_desc_t *nid,char *port)
{
   struct sockaddr_in serv;
   struct servent *sp;
   int sock_fd;

   if ((sock_fd = socket(PF_INET,SOCK_STREAM,0)) < 0) {
      perror("netio_tcp_cli_create: socket\n");
      return(-1);
   }

   memset(&serv,0,sizeof(serv));
   serv.sin_family = AF_INET;
   serv.sin_addr.s_addr = htonl(INADDR_ANY);

   if (atoi(port) == 0) {
      if (!(sp = getservbyname(port,"tcp"))) {
         fprintf(stderr,"netio_tcp_ser_create: port %s is neither "
                 "number not service %s\n",port,strerror(errno));
         close(sock_fd);
         return(-1);
      }
      serv.sin_port = sp->s_port;
   } else
      serv.sin_port = htons(atoi(port));

   if (bind(sock_fd,(struct sockaddr *)&serv,sizeof(serv)) < 0) {
      fprintf(stderr,"netio_tcp_ser_create: bind %s failed %s\n",
              port,strerror(errno));
      close(sock_fd);
      return(-1);
   }

   if (listen(sock_fd,1) < 0) {
      fprintf(stderr,"netio_tcp_ser_create: listen %s failed %s\n",
              port,strerror(errno));
      close(sock_fd);
      return(-1);
   }

   fprintf(stderr,"Waiting connection on port %s...\n",port);

   if ((nid->fd = accept(sock_fd,NULL,NULL)) < 0) {
      fprintf(stderr,"netio_tcp_ser_create: accept %s failed %s\n",
              port,strerror(errno));
      close(sock_fd);
      return(-1);
   }

   fprintf(stderr,"Connected\n");

   close(sock_fd);
   return(0);
}

/* Create a new NetIO descriptor with TCP_SER method */
netio_desc_t *netio_desc_create_tcp_ser(char *port)
{
   netio_desc_t *desc;
   
   if (!(desc = malloc(sizeof(*desc))))
      return NULL;

   if (netio_tcp_ser_create(&desc->u.nid,port) == -1) {
      free(desc);
      return NULL;
   }

   desc->type = NETIO_TYPE_TCP_SER;
   desc->send = (void *)netio_tcp_send;
   desc->recv = (void *)netio_tcp_recv;
   desc->dptr = &desc->u.nid;
   return desc;
}

/*
 * =========================================================================
 * UDP sockets
 * =========================================================================
 */

/* Free a NetIO UDP descriptor */
static void netio_udp_free(netio_inet_desc_t *nid)
{
   if (nid->fd != -1) 
      close(nid->fd);
}

/* Write a packet to an UDP socket */
static ssize_t netio_udp_send(netio_inet_desc_t *nid,void *pkt,size_t pkt_len)
{
   return(send(nid->fd,pkt,pkt_len,0));
}

/* Receive a packet from an UDP socket */
static ssize_t netio_udp_recv(netio_inet_desc_t *nid,void *pkt,size_t max_len)
{
   return(recvfrom(nid->fd,pkt,max_len,0,NULL,NULL));
}

/* Create a new NetIO descriptor with UDP method */
netio_desc_t *netio_desc_create_udp(int local_port,
                                    char *remote_host,
                                    int remote_port)
{
   netio_inet_desc_t *nid;
   netio_desc_t *desc;
   
   if (!(desc = malloc(sizeof(*desc))))
      return NULL;

   nid = &desc->u.nid;
   nid->fd = udp_connect(local_port,remote_host,remote_port);

   if (nid->fd < 0) {
      free(desc);
      return NULL;
   }

   desc->type = NETIO_TYPE_UDP;
   desc->send = (void *)netio_udp_send;
   desc->recv = (void *)netio_udp_recv;
   desc->dptr = &desc->u.nid;
   return desc;
}

/*
 * =========================================================================
 * Linux RAW Ethernet driver
 * =========================================================================
 */
#ifdef LINUX_ETH
/* Free a NetIO raw ethernet descriptor */
static void netio_lnxeth_free(netio_lnxeth_desc_t *nled)
{
   if (nled->fd != -1) 
      close(nled->fd);
}

/* Write a packet to a raw Ethernet socket */
static ssize_t netio_lnxeth_send(netio_lnxeth_desc_t *nled,
                                 void *pkt,size_t pkt_len)
{
   return(eth_send(nled->fd,nled->dev_id,pkt,pkt_len));
}

/* Receive a packet from an raw Ethernet socket */
static ssize_t netio_lnxeth_recv(netio_lnxeth_desc_t *nled,
                                 void *pkt,size_t max_len)
{
   return(eth_recv(nled->fd,pkt,max_len));
}

/* Create a new NetIO descriptor with raw Ethernet method */
netio_desc_t *netio_desc_create_lnxeth(char *dev_name)
{
   netio_lnxeth_desc_t *nled;
   netio_desc_t *desc;
   
   if (!(desc = malloc(sizeof(*desc))))
      return NULL;

   nled = &desc->u.nled;

   if (strlen(dev_name) >= NETIO_DEV_MAXLEN) {
      fprintf(stderr,"netio_desc_create_lnxeth: bad Ethernet device string "
              "speecified.\n");
      free(desc);
      return NULL;
   }

   strcpy(nled->dev_name,dev_name);

   nled->fd = eth_init_socket(dev_name);
   nled->dev_id = eth_get_dev_index(dev_name);

   if (nled->fd < 0) {
      free(desc);
      return NULL;
   }

   desc->type = NETIO_TYPE_LINUX_ETH;
   desc->send = (void *)netio_lnxeth_send;
   desc->recv = (void *)netio_lnxeth_recv;
   desc->dptr = &desc->u.nled;
   return desc;
}
#endif /* LINUX_ETH */

/*
 * =========================================================================
 * NULL Driver (does nothing, used for debugging)
 * =========================================================================
 */
static ssize_t netio_null_send(void *null_ptr,void *pkt,size_t pkt_len)
{
   return(pkt_len);
}

static ssize_t netio_null_recv(void *null_ptr,void *pkt,size_t max_len)
{
   pause();
   return(-1);
}

/* Create a new NetIO descriptor with NULL method */
netio_desc_t *netio_desc_create_null(void)
{
   netio_desc_t *desc;
   
   if (!(desc = malloc(sizeof(*desc))))
      return NULL;

   desc->type = NETIO_TYPE_NULL;
   desc->send = (void *)netio_null_send;
   desc->recv = (void *)netio_null_recv;
   desc->dptr = NULL;
   return desc;
}
/*
 * =========================================================================
 * Generic functions (abstraction layer)
 * =========================================================================
 */

/* Send a packet through a NetIO descriptor */
ssize_t netio_send(netio_desc_t *desc,void *pkt,size_t max_len)
{
   return((desc != NULL) ? desc->send(desc->dptr,pkt,max_len) : -1);
}

/* Receive a packet through a NetIO descriptor */
ssize_t netio_recv(netio_desc_t *desc,void *pkt,size_t max_len)
{
   return((desc != NULL) ? desc->recv(desc->dptr,pkt,max_len) : -1);
}

/* Get a NetIO FD */
int netio_get_fd(netio_desc_t *desc)
{
   int fd = -1;

   switch(desc->type) {
      case NETIO_TYPE_UNIX:
         fd = desc->u.nud.fd;
         break;
      case NETIO_TYPE_TAP:
         fd = desc->u.ntd.fd;
         break;
      case NETIO_TYPE_TCP_CLI:
      case NETIO_TYPE_TCP_SER:
      case NETIO_TYPE_UDP:
         fd = desc->u.nid.fd;
         break;
   }
   
   return(fd);
}

/* Free a NetIO descriptor */
void netio_free(netio_desc_t *desc)
{
   if (desc) {
      switch(desc->type) {
         case NETIO_TYPE_UNIX:
            netio_unix_free(&desc->u.nud);
            break;
         case NETIO_TYPE_TAP:
            netio_tap_free(&desc->u.ntd);
            break;
         case NETIO_TYPE_TCP_CLI:
         case NETIO_TYPE_TCP_SER:
            netio_tcp_free(&desc->u.nid);
            break;
         case NETIO_TYPE_UDP:
            netio_udp_free(&desc->u.nid);
            break;
#ifdef LINUX_ETH
         case NETIO_TYPE_LINUX_ETH:
            netio_lnxeth_free(&desc->u.nled);
            break;
#endif
         default:
            fprintf(stderr,"NETIO: unknown descriptor type %u\n",desc->type);
      }
   }
}
