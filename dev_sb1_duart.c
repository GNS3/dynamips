/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005 Christophe Fillot (cf@utc.fr)
 *
 * Cisco C7200 NPE-G1/SB-1 DUART.
 *
 * Not working, just for tests.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <termios.h>
#include <fcntl.h>
#include <pthread.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#define DUART_IRQ  5

/* DUART/Console status */
#define DUART_RX_READY  0x01
#define DUART_TX_READY  0x04

/* SB1 DUART structure */
struct sb1_duart_data {
   /* DUART & Console Management */
   u_int duart_interrupt;
   u_int duart_state;
   u_char duart_dataout;
   u_char duart_datain;
   u_int duart_tx_pending;
   u_int duart_rx_pending;

   /* Managing CPU */
   cpu_mips_t *mgr_cpu;
};

/*
 * dev_sb1_duart_access()
 */
void *dev_sb1_duart_access(cpu_mips_t *cpu,struct vdevice *dev,
                           m_uint32_t offset,u_int op_size,u_int op_type,
                           m_uint64_t *data)
{
   struct sb1_duart_data *d = dev->priv_data;
   u_char odata;

   if (op_type == MTS_READ)
      *data = 0;

   switch(offset) {
      case 0x127:   /* state */
         odata = 0;

         if (d->duart_rx_pending)
            odata |= DUART_RX_READY;

         if (!d->duart_tx_pending)
            odata |= DUART_TX_READY;
         
         mips64_clear_irq(d->mgr_cpu,DUART_IRQ);

         *data = odata;
         break;

#if 0
      case 0x414:   /* command */
         break;

      case 0x41c:   /* data */
         if (op_type == MTS_WRITE) {
            printf("%c",(char)(*data));
            fflush(stdout);

#if 0
            if (d->duart_interrupt & DUART_TXRDYA)
               mips64_set_irq(d->mgr_cpu,DUART_IRQ);
#endif

            d->duart_dataout = *data;
            //d->duart_tx_pending = 1;
         } else {
            //mips64_clear_irq(d->mgr_cpu,DUART_IRQ);
            *data = d->duart_datain;
            d->duart_rx_pending = 0;
         }
         break;

      case 0x42c:   /* DUART interrupt */
         if (op_type == MTS_WRITE) {
            d->duart_interrupt = *data;
         } else
            *data = d->duart_interrupt;
         break;         
#endif

#if 0 //DEBUG_UNKNOWN
      default:
         if (op_type == MTS_WRITE)
            printf("[ SB1_DUART: read from addr 0x%x ]\n",offset);
         else
            printf("[ SB1_DUART: write to addr 0x%x ]\n",offset);
#endif
   }

   return NULL;
}

/*
 *  dev_sb1_duart_init()
 */
int dev_sb1_duart_init(cpu_group_t *cpu_group,m_uint64_t paddr,m_uint32_t len)
{   
   struct sb1_duart_data *d;
   struct vdevice *dev;
   cpu_mips_t *cpu0;

   /* Device is managed by CPU0 */
   cpu0 = cpu_group_find_id(cpu_group,0);

   /* allocate private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"SB1_DUART: out of memory\n");
      return(-1);
   }

   if (!(dev = dev_create("sb1_duart"))) {
      fprintf(stderr,"SB1_DUART: unable to create device.\n");
      return(-1);
   }

   dev->phys_addr = paddr;
   dev->phys_len  = len;
   dev->handler   = dev_sb1_duart_access;
   dev->priv_data = d;
   d->mgr_cpu = cpu0;

   /* Map this device to all CPU */
   cpu_group_bind_device(cpu_group,dev);
   return(0);
}
