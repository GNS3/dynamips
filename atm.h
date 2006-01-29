/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * ATM definitions.
 */

#ifndef __ATM_H__
#define __ATM_H__

#include <pthread.h>

#include "utils.h"
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
typedef struct atm_vp_swconn atm_vp_swconn_t;
struct atm_vp_swconn {
   atm_vp_swconn_t *next;
   netio_desc_t *input,*output;
   u_int vpi_in,vpi_out;
   m_uint64_t cell_cnt;
};

/* VC-level switch table */
typedef struct atm_vc_swconn atm_vc_swconn_t;
struct atm_vc_swconn {
   atm_vc_swconn_t *next;
   netio_desc_t *input,*output;
   u_int vpi_in,vci_in;
   u_int vpi_out,vci_out;
   m_uint64_t cell_cnt;
};

/* Virtual ATM switch table */
#define ATMSW_NIO_MAX       32
#define ATMSW_VP_HASH_SIZE  256
#define ATMSW_VC_HASH_SIZE  1024

typedef struct atm_sw_table atm_sw_table_t;
struct atm_sw_table {
   pthread_t thread;
   m_uint64_t cell_drop;
   netio_desc_t *nio[ATMSW_NIO_MAX];
   atm_vp_swconn_t *vp_table[ATMSW_VP_HASH_SIZE];
   atm_vc_swconn_t *vc_table[ATMSW_VC_HASH_SIZE];
};

/* Compute HEC field for ATM header */
m_uint8_t atm_compute_hec(m_uint8_t *cell_header);

/* Insert HEC field into an ATM header */
void atm_insert_hec(m_uint8_t *cell_header);

/* Update the CRC on the data block one byte at a time */
m_uint32_t atm_update_crc(m_uint32_t crc_accum,m_uint8_t *ptr,int len);

/* Initialize ATM code (for checksums) */
void atm_init(void);

/* Start a virtual ATM switch */
int atmsw_start(char *filename);

#endif
