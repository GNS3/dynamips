/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * ATM Virtual Segmentation & Reassembly Engine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "utils.h"
#include "registry.h"
#include "crc.h"
#include "atm.h"
#include "net_io.h"
#include "atm_vsar.h"

/* Segmentation Context */
struct atm_seg_context {
   netio_desc_t *nio;

   m_uint8_t txfifo_cell[ATM_CELL_SIZE];
   size_t txfifo_pos,txfifo_avail;
   size_t aal5_len;
   m_uint32_t aal5_crc;
   m_uint32_t atm_hdr;

   char *buffer;
   size_t buf_len;
};

/* Send the ATM cell in FIFO */
static void atm_send_cell(struct atm_seg_context *asc)
{
   m_hton32(asc->txfifo_cell,asc->atm_hdr);
   atm_insert_hec(asc->txfifo_cell);
   netio_send(asc->nio,asc->txfifo_cell,ATM_CELL_SIZE);
}

/* Clear the TX fifo */
static void atm_clear_tx_fifo(struct atm_seg_context *asc)
{
   asc->txfifo_avail = ATM_PAYLOAD_SIZE;
   asc->txfifo_pos   = ATM_HDR_SIZE;
   memset(asc->txfifo_cell,0,ATM_CELL_SIZE);
}

/* Add padding to the FIFO */
static void atm_add_tx_padding(struct atm_seg_context *asc,size_t len)
{
   if (len > asc->txfifo_avail)
      len = asc->txfifo_avail;

   memset(&asc->txfifo_cell[asc->txfifo_pos],0,len);
   asc->txfifo_pos += len;
   asc->txfifo_avail -= len;
}

/* Send the TX fifo if it is empty */
static void atm_send_fifo(struct atm_seg_context *asc)
{
   if (!asc->txfifo_avail) {
      asc->aal5_crc = crc32_compute(asc->aal5_crc,
                                    &asc->txfifo_cell[ATM_HDR_SIZE],
                                    ATM_PAYLOAD_SIZE);
      atm_send_cell(asc);
      atm_clear_tx_fifo(asc);
   }
}

/* Store a packet in the TX FIFO */
static int atm_store_fifo(struct atm_seg_context *asc)
{
   size_t len;

   len = m_min(asc->buf_len,asc->txfifo_avail);

   memcpy(&asc->txfifo_cell[asc->txfifo_pos],asc->buffer,len);
   asc->buffer += len;
   asc->buf_len -= len;
   asc->txfifo_pos += len;
   asc->txfifo_avail -= len;

   if (!asc->txfifo_avail) {
      atm_send_fifo(asc);
      return(TRUE);
   }

   return(FALSE);
}

/* Add the AAL5 trailer to the TX FIFO */
static void atm_aal5_add_trailer(struct atm_seg_context *asc)
{
   m_uint8_t *trailer;

   trailer = &asc->txfifo_cell[ATM_AAL5_TRAILER_POS];

   /* Control field + Length */
   m_hton32(trailer,asc->aal5_len);

   /* Final CRC-32 computation */
   asc->aal5_crc = crc32_compute(asc->aal5_crc,
                                 &asc->txfifo_cell[ATM_HDR_SIZE],
                                 ATM_PAYLOAD_SIZE - 4);

   m_hton32(trailer+4,~asc->aal5_crc);

   /* Consider the FIFO as full */
   asc->txfifo_avail = 0;
}

/* Send an AAL5 packet through an NIO (segmentation) */
int atm_aal5_send(netio_desc_t *nio,u_int vpi,u_int vci,
                  struct iovec *iov,int iovcnt)
{
   struct atm_seg_context asc;
   int i;
   
   asc.nio = nio;
   asc.aal5_len = 0;
   asc.aal5_crc = 0xFFFFFFFF;
   atm_clear_tx_fifo(&asc);

   /* prepare the atm header */
   asc.atm_hdr  = vpi << ATM_HDR_VPI_SHIFT;
   asc.atm_hdr |= vci << ATM_HDR_VCI_SHIFT;

   for(i=0;i<iovcnt;i++) {
      asc.buffer  = iov[i].iov_base;
      asc.buf_len = iov[i].iov_len;
      asc.aal5_len += iov[i].iov_len;

      while(asc.buf_len > 0)
         atm_store_fifo(&asc);
   }

   /* 
    * Add the PDU trailer. If we have enough room, add it in the last cell,
    * otherwise create a new one.
    */
   if (asc.txfifo_avail < ATM_AAL5_TRAILER_SIZE) {
      atm_add_tx_padding(&asc,asc.txfifo_avail);
      atm_send_fifo(&asc);
   }
   
   /* Set AAL5 end of packet in ATM header (PTI field) */
   asc.atm_hdr |= ATM_PTI_EOP;
   
   atm_add_tx_padding(&asc,asc.txfifo_avail - ATM_AAL5_TRAILER_SIZE);
   atm_aal5_add_trailer(&asc);
   atm_send_cell(&asc);
   return(0);
}

/* Reset a receive context */
void atm_aal5_recv_reset(struct atm_reas_context *arc)
{
   arc->buf_pos = 0;
   arc->len = 0;
}

/* Receive an ATM cell and process reassembly */
int atm_aal5_recv(struct atm_reas_context *arc,m_uint8_t *cell)
{
   m_uint32_t atm_hdr;

   /* Check buffer boundary */
   if ((arc->buf_pos + ATM_PAYLOAD_SIZE) > ATM_REAS_MAX_SIZE) {
      atm_aal5_recv_reset(arc);
      return(-1);
   }

   /* Get the PTI field: we cannot handle "network" traffic */
   atm_hdr = m_ntoh32(cell);

   if (atm_hdr & ATM_PTI_NETWORK)
      return(2);
   
   /* Copy the payload */
   memcpy(&arc->buffer[arc->buf_pos],&cell[ATM_HDR_SIZE],ATM_PAYLOAD_SIZE);
   arc->buf_pos += ATM_PAYLOAD_SIZE;

   /* 
    * If this is the last cell of the packet, get the real length (the
    * trailer is at the end).
    */
   if (atm_hdr & ATM_PTI_EOP) {
      arc->len = m_ntoh16(&cell[ATM_AAL5_TRAILER_POS+2]);
      return((arc->len <= arc->buf_pos) ? 1 : -2);
   }

   return(0);
}

/* Send a packet through a rfc1483 bridge encap */
int atm_aal5_send_rfc1483b(netio_desc_t *nio,u_int vpi,u_int vci,
                           void *pkt,size_t len)
{
   struct iovec vec[2];

   vec[0].iov_base = (void *)atm_rfc1483b_header;
   vec[0].iov_len  = ATM_RFC1483B_HLEN;
   vec[1].iov_base = pkt;
   vec[1].iov_len  = len;

   return(atm_aal5_send(nio,vpi,vci,vec,2));
}
