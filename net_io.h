/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Network interaction.
 */

#ifndef __NET_IO_H__
#define __NET_IO_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "utils.h"

#ifdef LINUX_ETH
#include "linux_eth.h"
#endif
#ifdef GEN_ETH
#include "gen_eth.h"
#endif

#define NETIO_DEV_MAXLEN  64

enum {
   NETIO_TYPE_UNIX = 0,
   NETIO_TYPE_TAP,
   NETIO_TYPE_UDP,
   NETIO_TYPE_TCP_CLI,
   NETIO_TYPE_TCP_SER,
#ifdef LINUX_ETH
   NETIO_TYPE_LINUX_ETH,
#endif
#ifdef GEN_ETH
   NETIO_TYPE_GEN_ETH,
#endif
   NETIO_TYPE_NULL,
   NETIO_TYPE_MAX,
};

/* netio unix descriptor */
typedef struct netio_unix_desc netio_unix_desc_t;
struct netio_unix_desc {
   char *local_filename;
   struct sockaddr_un remote_sun;
   size_t remote_sun_len;
   int fd;
};

/* netio tap descriptor */
typedef struct netio_tap_desc netio_tap_desc_t;
struct netio_tap_desc {
   char filename[NETIO_DEV_MAXLEN];
   int fd;
};

/* netio udp/tcp descriptor */
typedef struct netio_inet_desc netio_inet_desc_t;
struct netio_inet_desc {
   int fd;
};

#ifdef LINUX_ETH
/* netio linux raw ethernet descriptor */
typedef struct netio_lnxeth_desc netio_lnxeth_desc_t;
struct netio_lnxeth_desc {
   char dev_name[NETIO_DEV_MAXLEN];
   int dev_id,fd;
};
#endif

#ifdef GEN_ETH
/* netio generic raw ethernet descriptor */
typedef struct netio_geneth_desc netio_geneth_desc_t;
struct netio_geneth_desc {
   char dev_name[NETIO_DEV_MAXLEN];
   pcap_t *pcap_dev;
};
#endif

/* generic netio descriptor */
typedef struct netio_desc netio_desc_t;
struct netio_desc {
   u_int type;
   void *dptr;
   char *name;

   union {
      netio_unix_desc_t nud;
      netio_tap_desc_t ntd;
      netio_inet_desc_t nid;
#ifdef LINUX_ETH
      netio_lnxeth_desc_t nled;
#endif
#ifdef GEN_ETH
      netio_geneth_desc_t nged;
#endif
   } u;

   /* send and receive prototypes */
   ssize_t (*send)(void *desc,void *pkt,size_t len);
   ssize_t (*recv)(void *desc,void *pkt,size_t len);
};

/* Get NETIO type given a description */
int netio_get_type(char *desc);

/* Show the NETIO types */
void netio_show_types(void);

/* Create a new NetIO descriptor */
netio_desc_t *netio_desc_create_unix(char *local,char *remote);

/* Create a new NetIO descriptor with TAP method */
netio_desc_t *netio_desc_create_tap(char *tap_name);

/* Create a new NetIO descriptor with TCP_CLI method */
netio_desc_t *netio_desc_create_tcp_cli(char *addr,char *port);

/* Create a new NetIO descriptor with TCP_SER method */
netio_desc_t *netio_desc_create_tcp_ser(char *port);

/* Create a new NetIO descriptor with UDP method */
netio_desc_t *netio_desc_create_udp(int local_port,
                                    char *remote_host,
                                    int remote_port);

#ifdef LINUX_ETH
/* Create a new NetIO descriptor with raw Ethernet method */
netio_desc_t *netio_desc_create_lnxeth(char *dev_name);
#endif

#ifdef GEN_ETH
/* Create a new NetIO descriptor with generic raw Ethernet method */
netio_desc_t *netio_desc_create_geneth(char *dev_name);
#endif

/* Create a new NetIO descriptor with NULL method */
netio_desc_t *netio_desc_create_null(void);

/* Send a packet through a NetIO descriptor */
ssize_t netio_send(netio_desc_t *desc,void *pkt,size_t max_len);

/* Receive a packet through a NetIO descriptor */
ssize_t netio_recv(netio_desc_t *desc,void *pkt,size_t max_len);

/* Get a NetIO FD */
int netio_get_fd(netio_desc_t *desc);

#endif
