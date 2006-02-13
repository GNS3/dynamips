/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Frame-Relay definitions.
 */

#ifndef __FRAME_RELAY_H__
#define __FRAME_RELAY_H__

#include <pthread.h>

#include "utils.h"
#include "net_io.h"
#include "frame_relay.h"

/* DLCIs used for LMI */
#define FR_DLCI_LMI_ANSI   0       /* ANSI LMI */
#define FR_DLCI_LMI_CISCO  1023    /* Cisco LMI */

#define FR_LMI_ANSI_STATUS_OFFSET   5
#define FR_LMI_ANSI_STATUS_ENQUIRY  0x75   /* sent by user */
#define FR_LMI_ANSI_STATUS          0x7d   /* sent by network */

/* Maximum packet size */
#define FR_MAX_PKT_SIZE  2048

/* Frame-Relay switch table */
typedef struct fr_swconn fr_swconn_t;
struct fr_swconn {
   fr_swconn_t *next;
   netio_desc_t *input,*output;
   u_int dlci_in,dlci_out;
   m_uint64_t count;
   int dlci_lmi_announced;
};

/* Virtual Frame-Relay switch table */
#define FRSW_NIO_MAX    32
#define FRSW_HASH_SIZE  256

typedef struct fr_sw_table fr_sw_table_t;
struct fr_sw_table {
   pthread_t thread;
   m_uint64_t drop;
   netio_desc_t *nio[FRSW_NIO_MAX];
   fr_swconn_t *dlci_table[FRSW_HASH_SIZE];
};

/* Start a virtual Frame-Relay switch */
int frsw_start(char *filename);

#endif
