/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * NetIO Packet Filters.
 */

#ifndef __NET_IO_FILTER_H__
#define __NET_IO_FILTER_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include "utils.h"
#include "net_io.h"

/* Directions for filters */
#define NETIO_FILTER_DIR_RX  0
#define NETIO_FILTER_DIR_TX  1
#define NETIO_FILTER_DIR_BOTH 2 

/* Find a filter */
netio_pktfilter_t *netio_filter_find(char *name);

/* Add a new filter */
int netio_filter_add(netio_pktfilter_t *pf);

/* Bind a filter to a NIO */
int netio_filter_bind(netio_desc_t *nio,int direction,char *pf_name);

/* Unbind a filter from a NIO */
int netio_filter_unbind(netio_desc_t *nio,int direction);

/* Setup a filter */
int netio_filter_setup(netio_desc_t *nio,int direction,int argc,char *argv[]);

/* Load all packet filters */
void netio_filter_load_all(void);

#endif
