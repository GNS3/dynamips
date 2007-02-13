/*
 * Cisco router simulation platform.
 * Copyright (c) 2005 Christophe Fillot (cf@utc.fr)
 *
 * SB-1 I/O devices.
 *
 * XXX: just for tests!
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <termios.h>
#include <fcntl.h>
#include <pthread.h>

#include "utils.h"
#include "ptask.h"
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_c7200.h"

#define DEBUG_UNKNOWN   1

/* DUART Status Register */
#define DUART_SR_RX_RDY    0x01   /* Receiver ready */
#define DUART_SR_RX_FFUL   0x02   /* Receive FIFO full */
#define DUART_SR_TX_RDY    0x04   /* Transmitter ready */
#define DUART_SR_TX_EMT    0x08   /* Transmitter empty */

/* DUART Interrupt Status Register */
#define DUART_ISR_TXA      0x01   /* Channel A Transmitter Ready */
#define DUART_ISR_RXA      0x02   /* Channel A Receiver Ready */
#define DUART_ISR_TXB      0x10   /* Channel B Transmitter Ready */
#define DUART_ISR_RXB      0x20   /* Channel B Receiver Ready */

/* DUART Interrupt Mask Register */
#define DUART_IMR_TXA      0x01   /* Channel A Transmitter Ready */
#define DUART_IMR_RXA      0x02   /* Channel A Receiver Ready */
#define DUART_IMR_TXB      0x10   /* Channel B Transmitter Ready */
#define DUART_IMR_RXB      0x20   /* Channel B Receiver Ready */

/* SB-1 DUART channel */
struct sb1_duart_channel {
   m_uint8_t mode;
   m_uint8_t cmd;
};

/* SB-1 I/O private data */
struct sb1_io_data {
   vm_obj_t vm_obj;
   struct vdevice dev;

   /* Virtual machine */
   vm_instance_t *vm;

   /* DUART info */
   u_int duart_irq,duart_irq_seq;
   m_uint8_t duart_isr,duart_imr;
   struct sb1_duart_channel duart_chan[2];

   /* Periodic task to trigger dummy DUART IRQ */
   ptask_id_t duart_irq_tid;
};

/* Console port input */
static void tty_con_input(vtty_t *vtty)
{
   struct sb1_io_data *d = vtty->priv_data;

   if (d->duart_imr & DUART_IMR_RXA) {
      d->duart_isr |= DUART_ISR_RXA;
      vm_set_irq(d->vm,d->duart_irq);
   }
}

/* AUX port input */
static void tty_aux_input(vtty_t *vtty)
{
   struct sb1_io_data *d = vtty->priv_data;

   if (d->duart_imr & DUART_IMR_RXB) {
      d->duart_isr |= DUART_ISR_RXB;
      vm_set_irq(d->vm,d->duart_irq);
   }
}

/* IRQ trickery for Console and AUX ports */
static int tty_trigger_dummy_irq(struct sb1_io_data *d,void *arg)
{
   u_int mask;

   d->duart_irq_seq++;
   
   if (d->duart_irq_seq == 2) {
      mask = DUART_IMR_TXA|DUART_IMR_TXB;
      if (d->duart_imr & mask) {
         d->duart_isr |= DUART_ISR_TXA|DUART_ISR_TXB;
         vm_set_irq(d->vm,d->duart_irq);
      }

      d->duart_irq_seq = 0;
   }
   
   return(0);
}

/*
 * dev_sb1_io_access()
 */
void *dev_sb1_io_access(cpu_gen_t *cpu,struct vdevice *dev,
                        m_uint32_t offset,u_int op_size,u_int op_type,
                        m_uint64_t *data)
{
   struct sb1_io_data *d = dev->priv_data;
   u_char odata;

   if (op_type == MTS_READ)
      *data = 0;

   switch(offset) {
      case 0x390:  /* DUART Interrupt Status Register */
         if (op_type == MTS_READ)
            *data = d->duart_isr;
         break;

      case 0x320:  /* DUART Channel A Only Interrupt Status Register */
         if (op_type == MTS_READ)
            *data = d->duart_isr & 0x0F;
         break;

      case 0x340:  /* DUART Channel B Only Interrupt Status Register */
         if (op_type == MTS_READ)
            *data = (d->duart_isr >> 4) & 0x0F;
         break;

      case 0x3a0:  /* DUART Interrupt Mask Register */
         if (op_type == MTS_READ)
            *data = d->duart_imr;
         else
            d->duart_imr = *data;
         break;

      case 0x330:  /* DUART Channel A Only Interrupt Mask Register */
         if (op_type == MTS_READ) {
            *data = d->duart_imr & 0x0F;
         } else {
            d->duart_imr &= ~0x0F;
            d->duart_imr |= *data & 0x0F;
         }
         break;

      case 0x350:  /* DUART Channel B Only Interrupt Mask Register */
         if (op_type == MTS_READ) {
            *data = (d->duart_imr >> 4) & 0x0F;
         } else {
            d->duart_imr &= ~0xF0;
            d->duart_imr |= (*data & 0x0F) << 4;
         }
         break;

      case 0x100:  /* DUART Mode (Channel A) */
         if (op_type == MTS_READ)
            d->duart_chan[0].mode = *data;
         else
            *data = d->duart_chan[0].mode;
         break;

      case 0x200:  /* DUART Mode (Channel B) */
         if (op_type == MTS_READ)
            d->duart_chan[1].mode = *data;
         else
            *data = d->duart_chan[1].mode;
         break;

      case 0x150:  /* DUART Command Register (Channel A) */
         if (op_type == MTS_READ)
            d->duart_chan[0].cmd = *data;
         else
            *data = d->duart_chan[0].cmd;
         break;

      case 0x250:  /* DUART Command Register (Channel B) */
          if (op_type == MTS_READ)
            d->duart_chan[1].cmd = *data;
         else
            *data = d->duart_chan[1].cmd;
         break;

      case 0x120:  /* DUART Status Register (Channel A) */
         if (op_type == MTS_READ) {
            odata = 0;

            if (vtty_is_char_avail(d->vm->vtty_con))
               odata |= DUART_SR_RX_RDY;

            odata |= DUART_SR_TX_RDY;
         
            vm_clear_irq(d->vm,d->duart_irq);
            *data = odata;
         }
         break;

      case 0x220:  /* DUART Status Register (Channel B) */
         if (op_type == MTS_READ) {
            odata = 0;

            if (vtty_is_char_avail(d->vm->vtty_aux))
               odata |= DUART_SR_RX_RDY;

            odata |= DUART_SR_TX_RDY;
         
            //vm_clear_irq(d->vm,d->duart_irq);
            *data = odata;
         }
         break;

      case 0x160:  /* DUART Received Data Register (Channel A) */
         if (op_type == MTS_READ) {
            *data = vtty_get_char(d->vm->vtty_con);
            d->duart_isr &= ~DUART_ISR_RXA;
         }
         break;

      case 0x260:  /* DUART Received Data Register (Channel B) */
         if (op_type == MTS_READ) {
            *data = vtty_get_char(d->vm->vtty_aux);
            d->duart_isr &= ~DUART_ISR_RXB;
         }
         break;

      case 0x170:  /* DUART Transmit Data Register (Channel A) */
         if (op_type == MTS_WRITE) {
            vtty_put_char(d->vm->vtty_con,(char)*data);
            d->duart_isr &= ~DUART_ISR_TXA;
         }
         break;

      case 0x270:  /* DUART Transmit Data Register (Channel B) */
         if (op_type == MTS_WRITE) {
            vtty_put_char(d->vm->vtty_aux,(char)*data);
            d->duart_isr &= ~DUART_ISR_TXB;
         }
         break;

      case 0x1a76:   /* pcmcia status */
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"SB1_IO","read from addr 0x%x, pc=0x%llx\n",
                    offset,cpu_get_pc(cpu));
         } else {
            cpu_log(cpu,"SB1_IO","write to addr 0x%x, value=0x%llx, "
                    "pc=0x%llx\n",offset,*data,cpu_get_pc(cpu));
         }
#endif
   }

   return NULL;
}

/* Shutdown the SB-1 I/O devices */
void dev_sb1_io_shutdown(vm_instance_t *vm,struct sb1_io_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}


/* Create SB-1 I/O devices */
int dev_sb1_io_init(vm_instance_t *vm,u_int duart_irq)
{   
   struct sb1_io_data *d;

   /* allocate private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"SB1_IO: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->vm        = vm;
   d->duart_irq = duart_irq;

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = "sb1_io";
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_sb1_io_shutdown;

   /* Set device properties */
   dev_init(&d->dev);
   d->dev.name      = "sb1_io";
   d->dev.priv_data = d;
   d->dev.phys_addr = 0x10060000ULL;
   d->dev.phys_len  = 0x10000;
   d->dev.handler   = dev_sb1_io_access;

   /* Set console and AUX port notifying functions */
   vm->vtty_con->priv_data = d;
   vm->vtty_aux->priv_data = d;
   vm->vtty_con->read_notifier = tty_con_input;
   vm->vtty_aux->read_notifier = tty_aux_input;

   /* Trigger periodically a dummy IRQ to flush buffers */
   d->duart_irq_tid = ptask_add((ptask_callback)tty_trigger_dummy_irq,d,NULL);

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);  
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
