/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * ATM definitions.
 */

#ifndef __ATM_H__
#define __ATM_H__

#include <pthread.h>

#include "utils.h"
#include "mempool.h"
#include "net_io.h"

/* ATM payload size */
#define ATM_HDR_SIZE           5
#define ATM_PAYLOAD_SIZE       48
#define ATM_CELL_SIZE          (ATM_HDR_SIZE + ATM_PAYLOAD_SIZE)
#define ATM_AAL5_TRAILER_SIZE  8
#define ATM_AAL5_TRAILER_POS   (ATM_CELL_SIZE - ATM_AAL5_TRAILER_SIZE)

/* ATM header structure */
#define ATM_HDR_VPI_MASK       0xFFF00000
#define ATM_HDR_VPI_SHIFT      20
#define ATM_HDR_VCI_MASK       0x000FFFF0
#define ATM_HDR_VCI_SHIFT      4
#define ATM_HDR_PTI_MASK       0x0000000E
#define ATM_HDR_PTI_SHIFT      1

/* PTI bits */
#define ATM_PTI_EOP            0x00000002  /* End of packet */
#define ATM_PTI_CONGESTION     0x00000004  /* Congestion detected */
#define ATM_PTI_NETWORK        0x00000008  /* Network traffic */

/* VP-level switch table */
typedef struct atmsw_vp_conn atmsw_vp_conn_t;
struct atmsw_vp_conn {
   atmsw_vp_conn_t *next;
   netio_desc_t *input,*output;
   u_int vpi_in,vpi_out;
   m_uint64_t cell_cnt;
};

/* VC-level switch table */
typedef struct atmsw_vc_conn atmsw_vc_conn_t;
struct atmsw_vc_conn {
   atmsw_vc_conn_t *next;
   netio_desc_t *input,*output;
   u_int vpi_in,vci_in;
   u_int vpi_out,vci_out;
   m_uint64_t cell_cnt;
};

/* Virtual ATM switch table */
#define ATMSW_NIO_MAX       32
#define ATMSW_VP_HASH_SIZE  256
#define ATMSW_VC_HASH_SIZE  1024

typedef struct atmsw_table atmsw_table_t;
struct atmsw_table {
   char *name;
   pthread_mutex_t lock;
   mempool_t mp;
   m_uint64_t cell_drop;
   atmsw_vp_conn_t *vp_table[ATMSW_VP_HASH_SIZE];
   atmsw_vc_conn_t *vc_table[ATMSW_VC_HASH_SIZE];
};

#define ATMSW_LOCK(t)   pthread_mutex_lock(&(t)->lock)
#define ATMSW_UNLOCK(t) pthread_mutex_unlock(&(t)->lock)

/* RFC1483 bridged mode header */
#define ATM_RFC1483B_HLEN  10
extern m_uint8_t atm_rfc1483b_header[ATM_RFC1483B_HLEN];

/* Compute HEC field for ATM header */
m_uint8_t atm_compute_hec(m_uint8_t *cell_header);

/* Insert HEC field into an ATM header */
void atm_insert_hec(m_uint8_t *cell_header);

/* Update the CRC on the data block one byte at a time */
m_uint32_t atm_update_crc(m_uint32_t crc_accum,m_uint8_t *ptr,int len);

/* Initialize ATM code (for HEC checksums) */
void atm_init(void);

/* Acquire a reference to an ATM switch (increment reference count) */
atmsw_table_t *atmsw_acquire(char *name);

/* Release an ATM switch (decrement reference count) */
int atmsw_release(char *name);

/* Create a virtual switch table */
atmsw_table_t *atmsw_create_table(char *name);

/* Create a VP switch connection */
int atmsw_create_vpc(atmsw_table_t *t,char *nio_input,u_int vpi_in,
                     char *nio_output,u_int vpi_out);

/* Delete a VP switch connection */
int atmsw_delete_vpc(atmsw_table_t *t,char *nio_input,u_int vpi_in,
                     char *nio_output,u_int vpi_out);

/* Create a VC switch connection */
int atmsw_create_vcc(atmsw_table_t *t,
                     char *input,u_int vpi_in,u_int vci_in,
                     char *output,u_int vpi_out,u_int vci_out);

/* Delete a VC switch connection */
int atmsw_delete_vcc(atmsw_table_t *t,
                     char *nio_input,u_int vpi_in,u_int vci_in,
                     char *nio_output,u_int vpi_out,u_int vci_out);

/* Save the configuration of an ATM switch */
void atmsw_save_config(atmsw_table_t *t,FILE *fd);

/* Save configurations of all ATM switches */
void atmsw_save_config_all(FILE *fd);

/* Delete an ATM switch */
int atmsw_delete(char *name);

/* Delete all ATM switches */
int atmsw_delete_all(void);

/* Start a virtual ATM switch */
int atmsw_start(char *filename);

#endif
