/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * NetIO bridge.
 */

#ifndef __NET_IO_BRIDGE_H__
#define __NET_IO_BRIDGE_H__

#include "utils.h"
#include "net_io.h"

#define NETIO_BRIDGE_MAX_NIO 32

typedef struct netio_bridge netio_bridge_t;
struct netio_bridge {
   pthread_t thread;
   netio_desc_t *nio[NETIO_BRIDGE_MAX_NIO];
};

/* Start a virtual bridge */
int netio_bridge_start(char *filename);

#endif
