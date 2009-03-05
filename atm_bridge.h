/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * ATM bridge (RFC1483)
 */

#ifndef __ATM_BRIDGE_H__
#define __ATM_BRIDGE_H__

#include <pthread.h>

#include "utils.h"
#include "net_io.h"
#include "atm.h"
#include "atm_vsar.h"

typedef struct atm_bridge atm_bridge_t;
struct atm_bridge {
   char *name;
   pthread_mutex_t lock;
   netio_desc_t *eth_nio,*atm_nio;
   u_int vpi,vci;
   struct atm_reas_context arc;
};

/* Acquire a reference to an ATM bridge (increment reference count) */
atm_bridge_t *atm_bridge_acquire(char *name);

/* Release an ATM switch (decrement reference count) */
int atm_bridge_release(char *name);

/* Create a virtual ATM bridge */
atm_bridge_t *atm_bridge_create(char *name);

/* Configure an ATM bridge */
int atm_bridge_configure(atm_bridge_t *t,char *eth_nio,
                         char *atm_nio,u_int vpi,u_int vci);

/* Unconfigure an ATM bridge */
int atm_bridge_unconfigure(atm_bridge_t *t);

/* Delete an ATM bridge */
int atm_bridge_delete(char *name);

/* Delete all ATM switches */
int atm_bridge_delete_all(void);

/* Start a virtual ATM bridge */
int atm_bridge_start(char *filename);

#endif
