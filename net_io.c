/*
 * Cisco router) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Network Input/Output Abstraction Layer.
 */

/* By default, Cygwin supports only 64 FDs with select()! */
#ifdef __CYGWIN__
#define FD_SETSIZE 1024
#endif

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
#include <pthread.h>

#ifdef __linux__
#include <net/if.h>
#include <linux/if_tun.h>
#endif

#include "registry.h"
#include "net.h"
#include "net_io.h"
#include "net_io_filter.h"

/* Free a NetIO descriptor */
static int netio_free(void *data,void *arg);

/* NIO RX listener */
static pthread_mutex_t netio_rxl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t netio_rxq_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct netio_rx_listener *netio_rxl_list = NULL;
static struct netio_rx_listener *netio_rxl_add_list = NULL;
static netio_desc_t *netio_rxl_remove_list = NULL;
static pthread_t netio_rxl_thread;
static pthread_cond_t netio_rxl_cond;

#define NETIO_RXL_LOCK()   pthread_mutex_lock(&netio_rxl_mutex);
#define NETIO_RXL_UNLOCK() pthread_mutex_unlock(&netio_rxl_mutex);

#define NETIO_RXQ_LOCK()   pthread_mutex_lock(&netio_rxq_mutex);
#define NETIO_RXQ_UNLOCK() pthread_mutex_unlock(&netio_rxq_mutex);

/* NetIO type */
typedef struct {
   char *name;
   char *desc;
}netio_type_t;

/* NETIO types (must follow the enum definition) */
static netio_type_t netio_types[NETIO_TYPE_MAX] = {
   { "unix"      , "UNIX local sockets" },
   { "vde"       , "Virtual Distributed Ethernet / UML switch" },
   { "tap"       , "Linux/FreeBSD TAP device" },
   { "udp"       , "UDP sockets" },
   { "tcp_cli"   , "TCP client" },
   { "tcp_ser"   , "TCP server" },
#ifdef LINUX_ETH
   { "linux_eth" , "Linux Ethernet device" },
#endif
#ifdef GEN_ETH
   { "gen_eth"   , "Generic Ethernet device (PCAP)" },
#endif
   { "fifo"      , "FIFO (intra-hypervisor)" },
   { "null"      , "Null device" },
};

/* Get NETIO type given a description */
int netio_get_type(char *type)
{
   int i;

   for(i=0;i<NETIO_TYPE_MAX;i++)
      if (!strcmp(type,netio_types[i].name))
         return(i);

   return(-1);
}

/* Show the NETIO types */
void netio_show_types(void)
{
   int i;

   printf("Available NETIO types:\n");

   for(i=0;i<NETIO_TYPE_MAX;i++)
      printf("  * %-10s : %s\n",netio_types[i].name,netio_types[i].desc);

   printf("\n");
}

/*
 * =========================================================================
 * Generic functions (abstraction layer)
 * =========================================================================
 */

/* Acquire a reference to NIO from registry (increment reference count) */
netio_desc_t *netio_acquire(char *name)
{
   return(registry_find(name,OBJ_TYPE_NIO));
}

/* Release an NIO (decrement reference count) */
int netio_release(char *name)
{
   return(registry_unref(name,OBJ_TYPE_NIO));
}

/* Record an NIO in registry */
static int netio_record(netio_desc_t *nio)
{
   return(registry_add(nio->name,OBJ_TYPE_NIO,nio));
}

/* Create a new NetIO descriptor */
static netio_desc_t *netio_create(char *name)
{
   netio_desc_t *nio;

   if (!(nio = malloc(sizeof(*nio))))
      return NULL;

   /* setup as a NULL descriptor */
   memset(nio,0,sizeof(*nio));
   nio->type = NETIO_TYPE_NULL;

   /* save name for registry */
   if (!(nio->name = strdup(name))) {
      free(nio);
      return NULL;
   }

   return nio;
}

/* Delete a NetIO descriptor */
int netio_delete(char *name)
{
   return(registry_delete_if_unused(name,OBJ_TYPE_NIO,netio_free,NULL));
}

/* Delete all NetIO descriptors */
int netio_delete_all(void)
{
   return(registry_delete_type(OBJ_TYPE_NIO,netio_free,NULL));
}

/* Save the configuration of a NetIO descriptor */
void netio_save_config(netio_desc_t *nio,FILE *fd)
{
   if (nio->save_cfg)
      nio->save_cfg(nio,fd);
}

/* Save configurations of all NetIO descriptors */
static void netio_reg_save_config(registry_entry_t *entry,void *opt,int *err)
{
   netio_save_config((netio_desc_t *)entry->data,(FILE *)opt);
}

void netio_save_config_all(FILE *fd)
{
   registry_foreach_type(OBJ_TYPE_NIO,netio_reg_save_config,fd,NULL);
   fprintf(fd,"\n");
}

/* Send a packet through a NetIO descriptor */
ssize_t netio_send(netio_desc_t *nio,void *pkt,size_t len)
{
   int res;

   if (!nio)
      return(-1);

   if (nio->debug) {
      printf("NIO %s: sending a packet of %lu bytes:\n",nio->name,(u_long)len);
      mem_dump(stdout,pkt,len);
   }

   /* Apply the TX filter */
   if (nio->tx_filter != NULL) {
      res = nio->tx_filter->pkt_handler(nio,pkt,len,nio->tx_filter_data);

      if (res <= 0)
         return(-1);
   }

   /* Apply the bidirectional filter */
   if (nio->both_filter != NULL) {
      res = nio->both_filter->pkt_handler(nio,pkt,len,nio->both_filter_data);

      if (res == NETIO_FILTER_ACTION_DROP)
         return(-1);
   }

   return(nio->send(nio->dptr,pkt,len));
}

/* Receive a packet through a NetIO descriptor */
ssize_t netio_recv(netio_desc_t *nio,void *pkt,size_t max_len)
{
   ssize_t len;
   int res;

   if (!nio)
      return(-1);

   /* Receive the packet */
   if ((len = nio->recv(nio->dptr,pkt,max_len)) <= 0)
      return(-1);

   if (nio->debug) {
      printf("NIO %s: receiving a packet of %ld bytes:\n",nio->name,(long)len);
      mem_dump(stdout,pkt,len);
   }

   /* Apply the RX filter */
   if (nio->rx_filter != NULL) {
      res = nio->rx_filter->pkt_handler(nio,pkt,len,nio->rx_filter_data);

      if (res == NETIO_FILTER_ACTION_DROP)
         return(-1);
   }

   /* Apply the bidirectional filter */
   if (nio->both_filter != NULL) {
      res = nio->both_filter->pkt_handler(nio,pkt,len,nio->both_filter_data);

      if (res == NETIO_FILTER_ACTION_DROP)
         return(-1);
   }

   return(len);
}

/* Get a NetIO FD */
int netio_get_fd(netio_desc_t *nio)
{
   int fd = -1;

   switch(nio->type) {
      case NETIO_TYPE_UNIX:
         fd = nio->u.nud.fd;
         break;
      case NETIO_TYPE_VDE:
         fd = nio->u.nvd.data_fd;
         break;
      case NETIO_TYPE_TAP:
         fd = nio->u.ntd.fd;
         break;
      case NETIO_TYPE_TCP_CLI:
      case NETIO_TYPE_TCP_SER:
      case NETIO_TYPE_UDP:
         fd = nio->u.nid.fd;
         break;
#ifdef LINUX_ETH
      case NETIO_TYPE_LINUX_ETH:
         fd = nio->u.nled.fd;
         break;
#endif
   }
   
   return(fd);
}

/*
 * =========================================================================
 * UNIX sockets
 * =========================================================================
 */

/* Create an UNIX socket */
static int netio_unix_create_socket(netio_unix_desc_t *nud)
{
   struct sockaddr_un local_sock;

   if ((nud->fd = socket(AF_UNIX,SOCK_DGRAM,0)) == -1) {
      perror("netio_unix: socket");
      return(-1);
   }

   memset(&local_sock,0,sizeof(local_sock));
   local_sock.sun_family = AF_UNIX;
   strcpy(local_sock.sun_path,nud->local_filename);

   if (bind(nud->fd,(struct sockaddr *)&local_sock,sizeof(local_sock)) == -1) {
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

   if (nud->local_filename) {
      unlink(nud->local_filename);
      free(nud->local_filename);
   }
}

/* Allocate a new NetIO UNIX descriptor */
static int netio_unix_create(netio_unix_desc_t *nud,char *local,char *remote)
{
   memset(nud,0,sizeof(*nud));
   nud->fd = -1;

   /* check lengths */
   if ((strlen(local) >= sizeof(nud->remote_sock.sun_path)) ||
       (strlen(remote) >= sizeof(nud->remote_sock.sun_path)))
      goto nomem_error;

   if (!(nud->local_filename = strdup(local)))
      goto nomem_error;

   if (netio_unix_create_socket(nud) == -1)
      return(-1);
      
   /* prepare the remote info */
   nud->remote_sock.sun_family = AF_UNIX;
   strcpy(nud->remote_sock.sun_path,remote);
   return(0);

 nomem_error:
   fprintf(stderr,"netio_unix_create: "
           "invalid file size or insufficient memory\n");
   return(-1);
}

/* Write a packet to an UNIX socket */
static ssize_t netio_unix_send(netio_unix_desc_t *nud,void *pkt,size_t pkt_len)
{
   return(sendto(nud->fd,pkt,pkt_len,0,
                 (struct sockaddr *)&nud->remote_sock,
                 sizeof(&nud->remote_sock)));
}

/* Receive a packet from an UNIX socket */
static ssize_t netio_unix_recv(netio_unix_desc_t *nud,void *pkt,size_t max_len)
{
   return(recvfrom(nud->fd,pkt,max_len,0,NULL,NULL));
}

/* Save the NIO configuration */
static void netio_unix_save_cfg(netio_desc_t *nio,FILE *fd)
{
   netio_unix_desc_t *nud = nio->dptr;
   fprintf(fd,"nio create_unix %s %s %s\n",
           nio->name,nud->local_filename,nud->remote_sock.sun_path);
}

/* Create a new NetIO descriptor with UNIX method */
netio_desc_t *netio_desc_create_unix(char *nio_name,char *local,char *remote)
{
   netio_desc_t *nio;
   
   if (!(nio = netio_create(nio_name)))
      return NULL;

   if (netio_unix_create(&nio->u.nud,local,remote) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   nio->type     = NETIO_TYPE_UNIX;
   nio->send     = (void *)netio_unix_send;
   nio->recv     = (void *)netio_unix_recv;
   nio->save_cfg = netio_unix_save_cfg;
   nio->dptr     = &nio->u.nud;

   if (netio_record(nio) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   return nio;
}

/*
 * =========================================================================
 * VDE (Virtual Distributed Ethernet) interface
 * =========================================================================
 */

/* Free a NetIO VDE descriptor */
static void netio_vde_free(netio_vde_desc_t *nvd)
{
   if (nvd->data_fd != -1) 
      close(nvd->data_fd);

   if (nvd->ctrl_fd != -1) 
      close(nvd->ctrl_fd);

   if (nvd->local_filename) {
      unlink(nvd->local_filename);
      free(nvd->local_filename);
   }
}

/* Create a new NetIO VDE descriptor */
static int netio_vde_create(netio_vde_desc_t *nvd,char *control,char *local)
{
   struct sockaddr_un ctrl_sock,tst;
   struct vde_request_v3 req;
   ssize_t len;
   int res;

   memset(nvd,0,sizeof(*nvd));
   nvd->ctrl_fd = nvd->data_fd = -1;

   if ((strlen(control) >= sizeof(ctrl_sock.sun_path)) ||
       (strlen(local) >= sizeof(nvd->remote_sock.sun_path))) {
      fprintf(stderr,"netio_vde_create: bad filenames specified\n");
      return(-1);
   }

   /* Copy the local filename */
   if (!(nvd->local_filename = strdup(local))) {
      fprintf(stderr,"netio_vde_create: insufficient memory\n");
      return(-1);
   }

   /* Connect to the VDE switch controller */
   nvd->ctrl_fd = socket(AF_UNIX,SOCK_STREAM,0);
   if (nvd->ctrl_fd < 0) {
      perror("netio_vde_create: socket(control)");
      return(-1);
   }
      
   memset(&ctrl_sock,0,sizeof(ctrl_sock));
   ctrl_sock.sun_family = AF_UNIX;
   strcpy(ctrl_sock.sun_path,control);

   res = connect(nvd->ctrl_fd,(struct sockaddr *)&ctrl_sock,
                 sizeof(ctrl_sock));

   if (res < 0) {
      perror("netio_vde_create: connect(control)");
      return(-1);
   }

   tst.sun_family = AF_UNIX;
   strcpy(tst.sun_path,local);

   /* Create the data connection */
   nvd->data_fd = socket(AF_UNIX,SOCK_DGRAM,0);
   if (nvd->data_fd < 0) {
      perror("netio_vde_create: socket(data)");
      return(-1);
   }

   if (bind(nvd->data_fd,(struct sockaddr *)&tst,sizeof(tst))<0) {
      perror("netio_vde_create: bind(data)");
      return(-1);
   }

   /* Now, process to registration */
   memset(&req,0,sizeof(req));
   req.sock.sun_family = AF_UNIX;
   strcpy(req.sock.sun_path,local);
   req.magic   = VDE_SWITCH_MAGIC;
   req.version = VDE_SWITCH_VERSION;
   req.type    = VDE_REQ_NEW_CONTROL;
   
   len = write(nvd->ctrl_fd,&req,sizeof(req));
   if (len != sizeof(req)) {
      perror("netio_vde_create: write(req)");
      return(-1);
   }

   /* Read the remote socket descriptor */
   len = read(nvd->ctrl_fd,&nvd->remote_sock,sizeof(nvd->remote_sock));
   if (len != sizeof(nvd->remote_sock)) {
      perror("netio_vde_create: read(req)");
      return(-1);
   }

   return(0);
}

/* Write a packet to a VDE data socket */
static ssize_t netio_vde_send(netio_vde_desc_t *nvd,void *pkt,size_t pkt_len)
{
   return(sendto(nvd->data_fd,pkt,pkt_len,0,
                 (struct sockaddr *)&nvd->remote_sock,
                 sizeof(nvd->remote_sock)));
}

/* Receive a packet from a VDE socket */
static ssize_t netio_vde_recv(netio_vde_desc_t *nvd,void *pkt,size_t max_len)
{
   return(recvfrom(nvd->data_fd,pkt,max_len,0,NULL,NULL));
}

/* Save the NIO configuration */
static void netio_vde_save_cfg(netio_desc_t *nio,FILE *fd)
{
   netio_vde_desc_t *nvd = nio->dptr;
   fprintf(fd,"nio create_vde %s %s %s\n",
           nio->name,nvd->remote_sock.sun_path,nvd->local_filename);
}

/* Create a new NetIO descriptor with VDE method */
netio_desc_t *netio_desc_create_vde(char *nio_name,char *control,char *local)
{
   netio_vde_desc_t *nvd;
   netio_desc_t *nio;
   
   if (!(nio = netio_create(nio_name)))
      return NULL;

   nvd = &nio->u.nvd;

   if (netio_vde_create(nvd,control,local) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   nio->type     = NETIO_TYPE_VDE;
   nio->send     = (void *)netio_vde_send;
   nio->recv     = (void *)netio_vde_recv;
   nio->save_cfg = netio_vde_save_cfg;
   nio->dptr     = &nio->u.nvd;

   if (netio_record(nio) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   return nio;
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

/* Save the NIO configuration */
static void netio_tap_save_cfg(netio_desc_t *nio,FILE *fd)
{
   netio_tap_desc_t *ntd = nio->dptr;
   fprintf(fd,"nio create_tap %s %s\n",nio->name,ntd->filename);
}

/* Create a new NetIO descriptor with TAP method */
netio_desc_t *netio_desc_create_tap(char *nio_name,char *tap_name)
{
   netio_tap_desc_t *ntd;
   netio_desc_t *nio;
   
   if (!(nio = netio_create(nio_name)))
      return NULL;

   ntd = &nio->u.ntd;

   if (netio_tap_create(ntd,tap_name) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   nio->type     = NETIO_TYPE_TAP;
   nio->send     = (void *)netio_tap_send;
   nio->recv     = (void *)netio_tap_recv;
   nio->save_cfg = netio_tap_save_cfg;
   nio->dptr     = &nio->u.ntd;

   if (netio_record(nio) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   return nio;
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
netio_desc_t *netio_desc_create_tcp_cli(char *nio_name,char *host,char *port)
{
   netio_desc_t *nio;
   
   if (!(nio = netio_create(nio_name)))
      return NULL;

   if (netio_tcp_cli_create(&nio->u.nid,host,port) < 0) {
      netio_free(nio,NULL);
      return NULL;
   }

   nio->type = NETIO_TYPE_TCP_CLI;
   nio->send = (void *)netio_tcp_send;
   nio->recv = (void *)netio_tcp_recv;
   nio->dptr = &nio->u.nid;

  if (netio_record(nio) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   return nio;
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
netio_desc_t *netio_desc_create_tcp_ser(char *nio_name,char *port)
{
   netio_desc_t *nio;
   
   if (!(nio = netio_create(nio_name)))
      return NULL;

   if (netio_tcp_ser_create(&nio->u.nid,port) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   nio->type = NETIO_TYPE_TCP_SER;
   nio->send = (void *)netio_tcp_send;
   nio->recv = (void *)netio_tcp_recv;
   nio->dptr = &nio->u.nid;

   if (netio_record(nio) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   return nio;
}

/*
 * =========================================================================
 * UDP sockets
 * =========================================================================
 */

/* Free a NetIO UDP descriptor */
static void netio_udp_free(netio_inet_desc_t *nid)
{
   if (nid->remote_host) {
      free(nid->remote_host);
      nid->remote_host = NULL;
   }

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

/* Save the NIO configuration */
static void netio_udp_save_cfg(netio_desc_t *nio,FILE *fd)
{
   netio_inet_desc_t *nid = nio->dptr;
   fprintf(fd,"nio create_udp %s %d %s %d\n",
           nio->name,nid->local_port,nid->remote_host,nid->remote_port);
}

/* Create a new NetIO descriptor with UDP method */
netio_desc_t *netio_desc_create_udp(char *nio_name,int local_port,
                                    char *remote_host,int remote_port)
{
   netio_inet_desc_t *nid;
   netio_desc_t *nio;
   
   if (!(nio = netio_create(nio_name)))
      return NULL;

   nid = &nio->u.nid;
   nid->local_port  = local_port;
   nid->remote_port = remote_port;

   if (!(nid->remote_host = strdup(remote_host))) {
      fprintf(stderr,"netio_desc_create_udp: insufficient memory\n");
      goto error;
   }

   if ((nid->fd = udp_connect(local_port,remote_host,remote_port)) < 0) {
      fprintf(stderr,"netio_desc_create_udp: unable to connect to %s:%d\n",
              remote_host,remote_port);
      goto error;
   }

   nio->type     = NETIO_TYPE_UDP;
   nio->send     = (void *)netio_udp_send;
   nio->recv     = (void *)netio_udp_recv;
   nio->save_cfg = netio_udp_save_cfg;
   nio->dptr     = &nio->u.nid;

   if (netio_record(nio) == -1)
      goto error;

   return nio;

 error:
   netio_free(nio,NULL);
   return NULL;
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
   return(lnx_eth_send(nled->fd,nled->dev_id,pkt,pkt_len));
}

/* Receive a packet from an raw Ethernet socket */
static ssize_t netio_lnxeth_recv(netio_lnxeth_desc_t *nled,
                                 void *pkt,size_t max_len)
{
   return(lnx_eth_recv(nled->fd,pkt,max_len));
}

/* Save the NIO configuration */
static void netio_lnxeth_save_cfg(netio_desc_t *nio,FILE *fd)
{
   netio_lnxeth_desc_t *nled = nio->dptr;
   fprintf(fd,"nio create_linux_eth %s %s\n",nio->name,nled->dev_name);
}

/* Create a new NetIO descriptor with raw Ethernet method */
netio_desc_t *netio_desc_create_lnxeth(char *nio_name,char *dev_name)
{
   netio_lnxeth_desc_t *nled;
   netio_desc_t *nio;
   
   if (!(nio = netio_create(nio_name)))
      return NULL;

   nled = &nio->u.nled;

   if (strlen(dev_name) >= NETIO_DEV_MAXLEN) {
      fprintf(stderr,"netio_desc_create_lnxeth: bad Ethernet device string "
              "specified.\n");
      netio_free(nio,NULL);
      return NULL;
   }

   strcpy(nled->dev_name,dev_name);

   nled->fd = lnx_eth_init_socket(dev_name);
   nled->dev_id = lnx_eth_get_dev_index(dev_name);

   if (nled->fd < 0) {
      netio_free(nio,NULL);
      return NULL;
   }

   nio->type     = NETIO_TYPE_LINUX_ETH;
   nio->send     = (void *)netio_lnxeth_send;
   nio->recv     = (void *)netio_lnxeth_recv;
   nio->save_cfg = netio_lnxeth_save_cfg;
   nio->dptr     = &nio->u.nled;

   if (netio_record(nio) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   return nio;
}
#endif /* LINUX_ETH */

/*
 * =========================================================================
 * Generic RAW Ethernet driver
 * =========================================================================
 */
#ifdef GEN_ETH
/* Free a NetIO raw ethernet descriptor */
static void netio_geneth_free(netio_geneth_desc_t *nged)
{
   gen_eth_close(nged->pcap_dev);
}

/* Write a packet to an Ethernet device */
static ssize_t netio_geneth_send(netio_geneth_desc_t *nged,
                                 void *pkt,size_t pkt_len)
{
   return(gen_eth_send(nged->pcap_dev,pkt,pkt_len));
}

/* Receive a packet from an Ethernet device */
static ssize_t netio_geneth_recv(netio_geneth_desc_t *nged,
                                 void *pkt,size_t max_len)
{
   return(gen_eth_recv(nged->pcap_dev,pkt,max_len));
}

/* Save the NIO configuration */
static void netio_geneth_save_cfg(netio_desc_t *nio,FILE *fd)
{
   netio_geneth_desc_t *nged = nio->dptr;
   fprintf(fd,"nio create_gen_eth %s %s\n",nio->name,nged->dev_name);
}

/* Create a new NetIO descriptor with generic raw Ethernet method */
netio_desc_t *netio_desc_create_geneth(char *nio_name,char *dev_name)
{
   netio_geneth_desc_t *nged;
   netio_desc_t *nio;
      
   if (!(nio = netio_create(nio_name)))
      return NULL;

   nged = &nio->u.nged;

   if (strlen(dev_name) >= NETIO_DEV_MAXLEN) {
      fprintf(stderr,"netio_desc_create_geneth: bad Ethernet device string "
              "specified.\n");
      netio_free(nio,NULL);
      return NULL;
   }

   strcpy(nged->dev_name,dev_name);

   if (!(nged->pcap_dev = gen_eth_init(dev_name))) {
      netio_free(nio,NULL);
      return NULL;
   }

   nio->type     = NETIO_TYPE_GEN_ETH;
   nio->send     = (void *)netio_geneth_send;
   nio->recv     = (void *)netio_geneth_recv;
   nio->save_cfg = netio_geneth_save_cfg;
   nio->dptr     = &nio->u.nged;

   if (netio_record(nio) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   return nio;
}
#endif /* GEN_ETH */

/*
 * =========================================================================
 * FIFO Driver (intra-hypervisor communications)
 * =========================================================================
 */

/* Extract the first packet of the FIFO */
static netio_fifo_pkt_t *netio_fifo_extract_pkt(netio_fifo_desc_t *nfd)
{ 
   netio_fifo_pkt_t *p;
      
   if (!(p = nfd->head))
      return NULL;

   nfd->pkt_count--;
   nfd->head = p->next;

   if (!nfd->head)
      nfd->last = NULL;

   return p;
}

/* Insert a packet into the FIFO (in tail) */
static void netio_fifo_insert_pkt(netio_fifo_desc_t *nfd,netio_fifo_pkt_t *p)
{   
   pthread_mutex_lock(&nfd->lock);

   nfd->pkt_count++;
   p->next = NULL;

   if (nfd->last) {
      nfd->last->next = p;
   } else {
      nfd->head = p;
   }

   nfd->last = p;
   pthread_mutex_unlock(&nfd->lock);
}

/* Free the packet list */
static void netio_fifo_free_pkt_list(netio_fifo_desc_t *nfd)
{
   netio_fifo_pkt_t *p,*next;
   
   for(p=nfd->head;p;p=next) {
      next = p->next;
      free(p);
   }

   nfd->head = nfd->last = NULL;
   nfd->pkt_count = 0;
}

/* Establish a cross-connect between two FIFO NetIO */
int netio_fifo_crossconnect(netio_desc_t *a,netio_desc_t *b)
{
   netio_fifo_desc_t *pa,*pb;

   if ((a->type != NETIO_TYPE_FIFO) || (b->type != NETIO_TYPE_FIFO))
      return(-1);

   pa = &a->u.nfd;
   pb = &b->u.nfd;

   /* A => B */
   pthread_mutex_lock(&pa->endpoint_lock);
   pthread_mutex_lock(&pa->lock);
   pa->endpoint = pb;
   netio_fifo_free_pkt_list(pa);
   pthread_mutex_unlock(&pa->lock);
   pthread_mutex_unlock(&pa->endpoint_lock);

   /* B => A */
   pthread_mutex_lock(&pb->endpoint_lock);
   pthread_mutex_lock(&pb->lock);
   pb->endpoint = pa;
   netio_fifo_free_pkt_list(pb);
   pthread_mutex_unlock(&pb->lock);
   pthread_mutex_unlock(&pb->endpoint_lock);
   return(0);
}

/* Unbind an endpoint */
static void netio_fifo_unbind_endpoint(netio_fifo_desc_t *nfd)
{
   pthread_mutex_lock(&nfd->endpoint_lock);
   nfd->endpoint = NULL;
   pthread_mutex_unlock(&nfd->endpoint_lock);
}

/* Free a NetIO FIFO descriptor */
static void netio_fifo_free(netio_fifo_desc_t *nfd)
{
   if (nfd->endpoint)
      netio_fifo_unbind_endpoint(nfd->endpoint);

   netio_fifo_free_pkt_list(nfd);
   pthread_mutex_destroy(&nfd->lock);
   pthread_cond_destroy(&nfd->cond);
}

/* Send a packet (to the endpoint FIFO) */
static ssize_t netio_fifo_send(netio_fifo_desc_t *nfd,void *pkt,size_t pkt_len)
{
   netio_fifo_pkt_t *p;
   size_t len;

   pthread_mutex_lock(&nfd->endpoint_lock);

   /* The cross-connect must have been established before */
   if (!nfd->endpoint)
      goto error;

   /* Allocate a a new packet and insert it into the endpoint FIFO */
   len = sizeof(netio_fifo_pkt_t) + pkt_len;
   if (!(p = malloc(len)))
      goto error;

   memcpy(p->pkt,pkt,pkt_len);
   p->pkt_len = pkt_len;
   netio_fifo_insert_pkt(nfd->endpoint,p);
   pthread_cond_signal(&nfd->endpoint->cond);
   pthread_mutex_unlock(&nfd->endpoint_lock);
   return(pkt_len);

 error:
   pthread_mutex_unlock(&nfd->endpoint_lock);
   return(-1);
}

/* Read a packet from the local FIFO queue */
static ssize_t netio_fifo_recv(netio_fifo_desc_t *nfd,void *pkt,size_t max_len)
{
   struct timespec ts; 
   m_tmcnt_t expire;
   netio_fifo_pkt_t *p;
   size_t len = -1;

   /* Wait for the endpoint to signal a new arriving packet */
   expire = m_gettime_usec() + 50000;
   ts.tv_sec = expire / 1000000;
   ts.tv_nsec = (expire % 1000000) * 1000;

   pthread_mutex_lock(&nfd->lock);
   pthread_cond_timedwait(&nfd->cond,&nfd->lock,&ts);

   /* Extract a packet from the list */
   p = netio_fifo_extract_pkt(nfd);
   pthread_mutex_unlock(&nfd->lock);

   if (p) {
      len = m_min(p->pkt_len,max_len);
      memcpy(pkt,p->pkt,len);
      free(p);
   }
   
   return(len);
}

/* Create a new NetIO descriptor with FIFO method */
netio_desc_t *netio_desc_create_fifo(char *nio_name)
{
   netio_fifo_desc_t *nfd;
   netio_desc_t *nio;
   
   if (!(nio = netio_create(nio_name)))
      return NULL;

   nfd = &nio->u.nfd;
   pthread_mutex_init(&nfd->lock,NULL);
   pthread_mutex_init(&nfd->endpoint_lock,NULL);
   pthread_cond_init(&nfd->cond,NULL);

   nio->type = NETIO_TYPE_FIFO;
   nio->send = (void *)netio_fifo_send;
   nio->recv = (void *)netio_fifo_recv;
   nio->dptr = nfd;

   if (netio_record(nio) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   return nio;
}

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
   usleep(200000);
   return(-1);
}

static void netio_null_save_cfg(netio_desc_t *nio,FILE *fd)
{
   fprintf(fd,"nio create_null %s\n",nio->name);
}

/* Create a new NetIO descriptor with NULL method */
netio_desc_t *netio_desc_create_null(char *nio_name)
{
   netio_desc_t *nio;
   
   if (!(nio = netio_create(nio_name)))
      return NULL;

   nio->type     = NETIO_TYPE_NULL;
   nio->send     = (void *)netio_null_send;
   nio->recv     = (void *)netio_null_recv;
   nio->save_cfg = netio_null_save_cfg;
   nio->dptr     = NULL;

   if (netio_record(nio) == -1) {
      netio_free(nio,NULL);
      return NULL;
   }

   return nio;
}

/* Free a NetIO descriptor */
static int netio_free(void *data,void *arg)
{
   netio_desc_t *nio = data;

   if (nio) {
      netio_filter_unbind(nio,NETIO_FILTER_DIR_RX);
      netio_filter_unbind(nio,NETIO_FILTER_DIR_TX);
      netio_filter_unbind(nio,NETIO_FILTER_DIR_BOTH);

      switch(nio->type) {
         case NETIO_TYPE_UNIX:
            netio_unix_free(&nio->u.nud);
            break;
         case NETIO_TYPE_VDE:
            netio_vde_free(&nio->u.nvd);
            break;
         case NETIO_TYPE_TAP:
            netio_tap_free(&nio->u.ntd);
            break;
         case NETIO_TYPE_TCP_CLI:
         case NETIO_TYPE_TCP_SER:
            netio_tcp_free(&nio->u.nid);
            break;
         case NETIO_TYPE_UDP:
            netio_udp_free(&nio->u.nid);
            break;
#ifdef LINUX_ETH
         case NETIO_TYPE_LINUX_ETH:
            netio_lnxeth_free(&nio->u.nled);
            break;
#endif
#ifdef GEN_ETH
         case NETIO_TYPE_GEN_ETH:
            netio_geneth_free(&nio->u.nged);
            break;
#endif
         case NETIO_TYPE_FIFO:
            netio_fifo_free(&nio->u.nfd);
            break;
         case NETIO_TYPE_NULL:           
            break;
         default:
            fprintf(stderr,"NETIO: unknown descriptor type %u\n",nio->type);
      }

      free(nio->name);
      free(nio);
   }

   return(TRUE);
}

/*
 * =========================================================================
 * RX Listeners
 * =========================================================================
 */

/* Find a RX listener */
static inline struct netio_rx_listener *netio_rxl_find(netio_desc_t *nio)
{
   struct netio_rx_listener *rxl;

   for(rxl=netio_rxl_list;rxl;rxl=rxl->next)
      if (rxl->nio == nio)
         return rxl;

   return NULL;
}

/* Remove a NIO from the listener list */
static int netio_rxl_remove_internal(netio_desc_t *nio)
{
   struct netio_rx_listener *rxl;
   int res = -1;

   if ((rxl = netio_rxl_find(nio))) {
      /* we suppress this NIO only when the ref count hits 0 */
      rxl->ref_count--;

      if (!rxl->ref_count) {
         /* remove this listener from the double linked list */
         if (rxl->next)
            rxl->next->prev = rxl->prev;
      
         if (rxl->prev)
            rxl->prev->next = rxl->next;
         else
            netio_rxl_list = rxl->next;

         /* if this is non-FD NIO, wait for thread to terminate */
         if (netio_get_fd(rxl->nio) == -1) {
            rxl->running = FALSE;
            pthread_join(rxl->spec_thread,NULL);
         }
         
         free(rxl);
      }

      res = 0;
   }
   
   return(res);
}

/* Add a RXL listener to the listener list */
static void netio_rxl_add_internal(struct netio_rx_listener *rxl)
{  
   struct netio_rx_listener *tmp;
   
   if ((tmp = netio_rxl_find(rxl->nio))) {
      tmp->ref_count++;
      free(rxl);
   } else {
      rxl->prev = NULL;
      rxl->next = netio_rxl_list;
      if (rxl->next) rxl->next->prev = rxl;
      netio_rxl_list = rxl;
   }
}

/* RX Listener dedicated thread (for non-FD NIO) */
static void *netio_rxl_spec_thread(void *arg)
{
   struct netio_rx_listener *rxl = arg;
   netio_desc_t *nio = rxl->nio;
   ssize_t pkt_len;

   while(rxl->running) {
      pkt_len = netio_recv(nio,nio->rx_pkt,sizeof(nio->rx_pkt));

      if (pkt_len > 0)
         rxl->rx_handler(nio,nio->rx_pkt,pkt_len,rxl->arg1,rxl->arg2);
   }

   return NULL;
}

/* RX Listener General Thread */
void *netio_rxl_gen_thread(void *arg)
{ 
   struct netio_rx_listener *rxl;
   ssize_t pkt_len;
   netio_desc_t *nio;
   struct timeval tv;
   int fd,fd_max,res;
   fd_set rfds;

   for(;;) {
      NETIO_RXL_LOCK();

      NETIO_RXQ_LOCK();
      /* Add the new waiting NIO to the active list */
      while(netio_rxl_add_list != NULL) {
         rxl = netio_rxl_add_list;
         netio_rxl_add_list = netio_rxl_add_list->next;
         netio_rxl_add_internal(rxl);
      }

      /* Delete the NIO present in the remove list */
      while(netio_rxl_remove_list != NULL) {
         nio = netio_rxl_remove_list;
         netio_rxl_remove_list = netio_rxl_remove_list->rxl_next;
         netio_rxl_remove_internal(nio);
      }

      pthread_cond_broadcast(&netio_rxl_cond);
      NETIO_RXQ_UNLOCK();

      /* Build the FD set */
      FD_ZERO(&rfds);
      fd_max = -1;
      for(rxl=netio_rxl_list;rxl;rxl=rxl->next) {
         if ((fd = netio_get_fd(rxl->nio)) == -1)
            continue;

         if (fd > fd_max) fd_max = fd;
         FD_SET(fd,&rfds);
      }
      NETIO_RXL_UNLOCK();

      /* Wait for incoming packets */
      tv.tv_sec = 0;
      tv.tv_usec = 20 * 1000;  /* 200 ms */
      res = select(fd_max+1,&rfds,NULL,NULL,&tv);

      if (res == -1) {
         if (errno != EINTR)
            perror("netio_rxl_thread: select");
         continue;
      }

      /* Examine active FDs and call user handlers */
      NETIO_RXL_LOCK();

      for(rxl=netio_rxl_list;rxl;rxl=rxl->next) {
         nio = rxl->nio;

         if ((fd = netio_get_fd(nio)) == -1)
            continue;

         if (FD_ISSET(fd,&rfds)) {
            pkt_len = netio_recv(nio,nio->rx_pkt,sizeof(nio->rx_pkt));

            if (pkt_len > 0)
               rxl->rx_handler(nio,nio->rx_pkt,pkt_len,rxl->arg1,rxl->arg2);
         }
      }

      NETIO_RXL_UNLOCK();
   }
   
   return NULL;
}

/* Add a RX listener in the listener list */
int netio_rxl_add(netio_desc_t *nio,netio_rx_handler_t rx_handler,
                  void *arg1,void *arg2)
{
   struct netio_rx_listener *rxl;

   NETIO_RXQ_LOCK();

   if (!(rxl = malloc(sizeof(*rxl)))) {
      NETIO_RXQ_UNLOCK();
      fprintf(stderr,"netio_rxl_add: unable to create structure.\n");
      return(-1);
   }

   memset(rxl,0,sizeof(*rxl));
   rxl->nio = nio;
   rxl->ref_count = 1;
   rxl->rx_handler = rx_handler;
   rxl->arg1 = arg1;
   rxl->arg2 = arg2;
   rxl->running = TRUE;

   if ((netio_get_fd(rxl->nio) == -1) &&
       pthread_create(&rxl->spec_thread,NULL,netio_rxl_spec_thread,rxl)) 
   {
      NETIO_RXQ_UNLOCK();
      fprintf(stderr,"netio_rxl_add: unable to create specific thread.\n");
      free(rxl);
      return(-1);
   }

   rxl->next = netio_rxl_add_list;
   netio_rxl_add_list = rxl;

   pthread_cond_wait(&netio_rxl_cond,&netio_rxq_mutex);
   NETIO_RXQ_UNLOCK();
   return(0);
}

/* Remove a NIO from the listener list */
int netio_rxl_remove(netio_desc_t *nio)
{
   NETIO_RXQ_LOCK();
   nio->rxl_next = netio_rxl_remove_list;
   netio_rxl_remove_list = nio;
   pthread_cond_wait(&netio_rxl_cond,&netio_rxq_mutex);
   NETIO_RXQ_UNLOCK();
   return(0);
}

/* Initialize the RXL thread */
int netio_rxl_init(void)
{
   pthread_cond_init(&netio_rxl_cond,NULL);

   if (pthread_create(&netio_rxl_thread,NULL,netio_rxl_gen_thread,NULL)) {
      perror("netio_rxl_init: pthread_create");
      return(-1);
   }

   return(0);
}
