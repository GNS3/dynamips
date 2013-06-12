/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * NetIO bridge.
 */

#ifndef __NET_IO_BRIDGE_H__
#define __NET_IO_BRIDGE_H__

#include <pthread.h>

#include "utils.h"
#include "net_io.h"

#define NETIO_BRIDGE_MAX_NIO 32

typedef struct netio_bridge netio_bridge_t;
struct netio_bridge {
   char *name;
   pthread_mutex_t lock;
   netio_desc_t *nio[NETIO_BRIDGE_MAX_NIO];
};

#define NETIO_BRIDGE_LOCK(t)   pthread_mutex_lock(&(t)->lock)
#define NETIO_BRIDGE_UNLOCK(t) pthread_mutex_unlock(&(t)->lock)

/* Acquire a reference to NetIO bridge from the registry (inc ref count) */
netio_desc_t *netio_bridge_acquire(char *name);

/* Release a NetIO bridge (decrement reference count) */
int netio_bridge_release(char *name);

/* Create a virtual bridge */
netio_bridge_t *netio_bridge_create(char *name);

/* Add a NetIO descriptor to a virtual bridge */
int netio_bridge_add_netio(netio_bridge_t *t,char *nio_name);

/* Remove a NetIO descriptor from a virtual bridge */
int netio_bridge_remove_netio(netio_bridge_t *t,char *nio_name);

/* Save the configuration of a brdige */
void netio_bridge_save_config(netio_bridge_t *t,FILE *fd);

/* Save configurations of all NIO bridges */
void netio_bridge_save_config_all(FILE *fd);

/* Delete a virtual bridge */
int netio_bridge_delete(char *name);

/* Delete all virtual bridges */
int netio_bridge_delete_all(void);

/* Start a virtual bridge */
int netio_bridge_start(char *filename);

#endif
