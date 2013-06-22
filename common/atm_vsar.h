/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * ATM Virtual Segmentation & Reassembly Engine.
 */

#ifndef __ATM_VSAR_H__
#define __ATM_VSAR_H__

#include <pthread.h>

#include "utils.h"
#include "net_io.h"

#define ATM_REAS_MAX_SIZE  16384

/* Reassembly Context */
struct atm_reas_context {
   m_uint8_t buffer[ATM_REAS_MAX_SIZE];
   size_t buf_pos;
   size_t len;
};

/* Send an AAL5 packet through an NIO (segmentation) */
int atm_aal5_send(netio_desc_t *nio,u_int vpi,u_int vci,
                  struct iovec *iov,int iovcnt);

/* Reset a receive context */
void atm_aal5_recv_reset(struct atm_reas_context *arc);

/* Receive an ATM cell and process reassembly */
int atm_aal5_recv(struct atm_reas_context *arc,m_uint8_t *cell);

/* Send a packet through a rfc1483 bridge encap */
int atm_aal5_send_rfc1483b(netio_desc_t *nio,u_int vpi,u_int vci,
                           void *pkt,size_t len);

#endif
