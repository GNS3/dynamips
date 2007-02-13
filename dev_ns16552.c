/*
 * Cisco 3600 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * NS16552 DUART.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <termios.h>
#include <fcntl.h>
#include <pthread.h>

#include "ptask.h"
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_vtty.h"

/* Debugging flags */
#define DEBUG_UNKNOWN  1
#define DEBUG_ACCESS   0

/* Interrupt Enable Register (IER) */
#define	IER_ERXRDY    0x1
#define	IER_ETXRDY    0x2

/* Interrupt Identification Register */
#define IIR_NPENDING  0x01   /* 0: irq pending, 1: no irq pending */
#define	IIR_TXRDY     0x02
#define	IIR_RXRDY     0x04

/* Line Status Register (LSR) */
#define	LSR_RXRDY     0x01
#define	LSR_TXRDY     0x20
#define	LSR_TXEMPTY   0x40

/* UART channel */
struct ns16552_channel {
   u_int ier,output;
   vtty_t *vtty;
};

/* NS16552 structure */
struct ns16552_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   vm_instance_t *vm;
   u_int irq;
   
   /* Register offset divisor */
   u_int reg_div;

   /* Periodic task to trigger DUART IRQ */
   ptask_id_t tid;

   struct ns16552_channel channel[2];
   u_int duart_irq_seq;
};

/* Console port input */
static void tty_con_input(vtty_t *vtty)
{
   struct ns16552_data *d = vtty->priv_data;

   if (d->channel[0].ier & IER_ERXRDY)
      vm_set_irq(d->vm,d->irq);
}

/* AUX port input */
static void tty_aux_input(vtty_t *vtty)
{
   struct ns16552_data *d = vtty->priv_data;

   if (d->channel[1].ier & IER_ERXRDY)
      vm_set_irq(d->vm,d->irq);
}

/* IRQ trickery for Console and AUX ports */
static int tty_trigger_dummy_irq(struct ns16552_data *d,void *arg)
{
   d->duart_irq_seq++;
   
   if (d->duart_irq_seq == 2) {
      if (d->channel[0].ier & IER_ETXRDY) {
         d->channel[0].output = TRUE;
         vm_set_irq(d->vm,d->irq);
      }

#if 0
      if (d->channel[1].ier & IER_ETXRDY) {
         d->channel[1].output = TRUE;
         vm_set_irq(d->vm,d->irq);
      }
#endif

      d->duart_irq_seq = 0;
   }

   return(0);
}

/*
 * dev_ns16552_access()
 */
void *dev_ns16552_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct ns16552_data *d = dev->priv_data;
   int channel = 0;
   u_char odata;

   if (op_type == MTS_READ)
      *data = 0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"NS16552","read from 0x%x, pc=0x%llx\n",
              offset,cpu_get_pc(cpu));
   } else {
      cpu_log(cpu,"NS16552","write to 0x%x, value=0x%llx, pc=0x%llx\n",
              offset,*data,cpu_get_pc(cpu));
   }
#endif

   offset >>= d->reg_div;

   if (offset >= 0x08)
      channel = 1;

   switch(offset) {
      /* Receiver Buffer Reg. (RBR) / Transmitting Holding Reg. (THR) */
      case 0x00:
      case 0x08:
         if (op_type == MTS_WRITE) {
            vtty_put_char(d->channel[channel].vtty,(char)*data);

            if (d->channel[channel].ier & IER_ETXRDY)
               vm_set_irq(d->vm,d->irq);

            d->channel[channel].output = TRUE;
         } else {
            *data = vtty_get_char(d->channel[channel].vtty);
         }
         break;

      /* Interrupt Enable Register (IER) */
      case 0x01:
      case 0x09:
         if (op_type == MTS_READ) {
            *data = d->channel[channel].ier;
         } else {
            d->channel[channel].ier = *data & 0xFF;

            if ((*data & 0x02) == 0) {   /* transmit holding register */
               d->channel[channel].vtty->managed_flush = TRUE;
               vtty_flush(d->channel[channel].vtty);               
            }
         }
         break;

      /* Interrupt Ident Register (IIR) */
      case 0x02:
         vm_clear_irq(d->vm,d->irq);
      case 0x0A:
         if (op_type == MTS_READ) {
            odata = IIR_NPENDING;

            if (vtty_is_char_avail(d->channel[channel].vtty)) {
               odata = IIR_RXRDY;
            } else {
               if (d->channel[channel].output) {
                  odata = IIR_TXRDY;
                  d->channel[channel].output = 0;
               }
            }

            *data = odata;
         }
         break;

      /* Line Status Register (LSR) */
      case 0x05:
      case 0x0D:
         if (op_type == MTS_READ) {
            odata = 0;

            if (vtty_is_char_avail(d->channel[channel].vtty))
               odata |= LSR_RXRDY;

            odata |= LSR_TXRDY|LSR_TXEMPTY;
            *data = odata;
         }
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"NS16552","read from addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,
                    "NS16552","write to addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   return NULL;
}

/* Shutdown a NS16552 device */
void dev_ns16552_shutdown(vm_instance_t *vm,struct ns16552_data *d)
{
   if (d != NULL) {
      d->channel[0].vtty->read_notifier = NULL;
      d->channel[1].vtty->read_notifier = NULL;

      /* Remove the periodic task */
      ptask_remove(d->tid);

      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/* Create a NS16552 device */
int dev_ns16552_init(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t len,
                     u_int reg_div,u_int irq,vtty_t *vtty_A,vtty_t *vtty_B)
{  
   struct ns16552_data *d;

   /* Allocate private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"NS16552: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->vm  = vm;
   d->irq = irq;
   d->reg_div = reg_div;
   d->channel[0].vtty = vtty_A;
   d->channel[1].vtty = vtty_B;

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = "ns16552";
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_ns16552_shutdown;

   /* Set device properties */
   dev_init(&d->dev);
   d->dev.name      = "ns16552";
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_ns16552_access;
   d->dev.priv_data = d;

   vtty_A->priv_data = d;
   vtty_B->priv_data = d;
   vtty_A->read_notifier = tty_con_input;
   vtty_B->read_notifier = tty_aux_input;

   /* Trigger periodically a dummy IRQ to flush buffers */
   d->tid = ptask_add((ptask_callback)tty_trigger_dummy_irq,d,NULL);

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);   
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
