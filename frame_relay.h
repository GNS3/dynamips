/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Frame-Relay definitions.
 */

#ifndef __FRAME_RELAY_H__
#define __FRAME_RELAY_H__

#include <pthread.h>

#include "utils.h"
#include "mempool.h"
#include "net_io.h"

/* DLCIs used for LMI */
#define FR_DLCI_LMI_ANSI   0       /* ANSI LMI */
#define FR_DLCI_LMI_CISCO  1023    /* Cisco LMI */

#define FR_LMI_ANSI_STATUS_OFFSET   5
#define FR_LMI_ANSI_STATUS_ENQUIRY  0x75   /* sent by user */
#define FR_LMI_ANSI_STATUS          0x7d   /* sent by network */

/* Maximum packet size */
#define FR_MAX_PKT_SIZE  2048

/* Frame-Relay switch table */
typedef struct frsw_conn frsw_conn_t;
struct frsw_conn {
   frsw_conn_t *hash_next,*next,**pprev;
   netio_desc_t *input,*output;
   u_int dlci_in,dlci_out;
   m_uint64_t count;
};

/* Virtual Frame-Relay switch table */
#define FRSW_HASH_SIZE  256

typedef struct frsw_table frsw_table_t;
struct frsw_table {
   char *name;
   pthread_mutex_t lock;
   mempool_t mp;
   m_uint64_t drop;
   frsw_conn_t *dlci_table[FRSW_HASH_SIZE];
};

#define FRSW_LOCK(t)   pthread_mutex_lock(&(t)->lock)
#define FRSW_UNLOCK(t) pthread_mutex_unlock(&(t)->lock)

/* Acquire a reference to a Frame-Relay switch (increment reference count) */
frsw_table_t *frsw_acquire(char *name);

/* Release a Frame-Relay switch (decrement reference count) */
int frsw_release(char *name);

/* Create a virtual switch table */
frsw_table_t *frsw_create_table(char *name);

/* Delete a Frame-Relay switch */
int frsw_delete(char *name);

/* Delete all Frame-Relay switches */
int frsw_delete_all(void);

/* Create a switch connection */
int frsw_create_vc(frsw_table_t *t,char *nio_input,u_int dlci_in,
                   char *nio_output,u_int dlci_out);

/* Remove a switch connection */
int frsw_delete_vc(frsw_table_t *t,char *nio_input,u_int dlci_in,
                   char *nio_output,u_int dlci_out);

/* Save the configuration of a Frame-Relay switch */
void frsw_save_config(frsw_table_t *t,FILE *fd);

/* Save configurations of all Frame-Relay switches */
void frsw_save_config_all(FILE *fd);

/* Start a virtual Frame-Relay switch */
int frsw_start(char *filename);

#endif
