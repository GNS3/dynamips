/*
 * Cisco 3600 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 * Patched by Jeremy Grossmann for the GNS3 project (www.gns3.net)
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

/* Line Control Register */
#define LCR_WRL0       0x01
#define LCR_WRL1       0x02
#define LCR_NUMSTOP    0x04
#define LCR_PARITYON   0x08
#define LCR_PARITYEV   0x10
#define LCR_STICKP     0x20
#define LCR_SETBREAK   0x40
#define LCR_DIVLATCH   0x80

/* Modem Control Register */
#define MCR_DTR        0x01
#define MCR_RTS        0x02
#define MCR_OUT1       0x04
#define MCR_OUT2       0x08
#define MCR_LOOP       0x10

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
   
   u_int line_control_reg;
   u_int div_latch;
   u_int baud_divisor;
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

      if (d->channel[1].ier & IER_ETXRDY) {
         d->channel[1].output = TRUE;
         vm_set_irq(d->vm,d->irq);
      }

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

// From the NS16552V datasheet, the following is known about the registers
// Bit 4 is channel
// Value 0 Receive or transmit buffer
// Value 1 Interrupt enable
// Value 2 Interrupt identification (READ), FIFO Config (Write)
// Value 3 Line Control (Appears in IOS)
//    0x1 - Word Length Selector bit 0
//    0x2 - Word Length Selector bit 1
//    0x4 - Num stop bits
//    0x8 - Parity Enable
//    0x16 - Parity even
//    0x32 - Stick Parity
//    0x64 - Set Break
//    0x128 - Division Latch
// Value 4 Modem Control (Appears in IOS)
// Value 5 Line status
// Value 6 Modem Status
// Value 7 Scratch
	  
   switch(offset) {
      /* Receiver Buffer Reg. (RBR) / Transmitting Holding Reg. (THR) */
      case 0x00:
      case 0x08:
         if (d->div_latch == 0) {
           if (op_type == MTS_WRITE) {
              vtty_put_char(d->channel[channel].vtty,(char)*data);
  
              if (d->channel[channel].ier & IER_ETXRDY)
                 vm_set_irq(d->vm,d->irq);
  
              d->channel[channel].output = TRUE;
           } else
              *data = vtty_get_char(d->channel[channel].vtty);
         } else {
           if (op_type == MTS_WRITE)
             d->baud_divisor = ((*data) & 0x00ff) | (d->baud_divisor & 0xff00);
         }
         break;

      /* Interrupt Enable Register (IER) */
      case 0x01:
      case 0x09:
         if (d->div_latch == 0) {
           if (op_type == MTS_READ) {
              *data = d->channel[channel].ier;
           } else {
              d->channel[channel].ier = *data & 0xFF;

              if ((*data & 0x02) == 0) {   /* transmit holding register */
                 d->channel[channel].vtty->managed_flush = TRUE;
                 vtty_flush(d->channel[channel].vtty);               
              }
           }
         } else {
           if (op_type == MTS_WRITE)
              d->baud_divisor = (((*data) & 0xff)<<8)|(d->baud_divisor & 0xff);
         }
         break;

      /* Interrupt Ident Register (IIR) */
      case 0x02:
      case 0x0A:
         if (d->div_latch == 0) {
           vm_clear_irq(d->vm,d->irq);
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
         }
         break;
      /* Line Control Register (LCR) */
      case 0x03:
      case 0x0B:
         if (op_type == MTS_READ) {
           *data = d->line_control_reg;
         } else {

           d->line_control_reg = (uint)*data;
           uint bits = 5;
           _maybe_used char *stop = "1";
           _maybe_used char *parity = "no ";
           _maybe_used char *parityeven = "odd";
           if (*data & LCR_WRL0) bits+=1;
           if (*data & LCR_WRL1) bits+=2;
         
           if (*data & LCR_NUMSTOP) {
             if ( bits >= 6) {
               stop = "2";
             } else {
               stop = "1.5";
             }
           }

           if (*data & LCR_PARITYON)
             parity=""; //Parity on
           if (*data & LCR_PARITYEV)
             parityeven="even";

           // DIV LATCH changes the behavior of 0x0,0x1,and 0x2
           if (*data & LCR_DIVLATCH) {
             d->div_latch = 1;
           } else {
             _maybe_used uint baud;
             d->div_latch = 0;
             //  1200 divisor was 192
             //  9600 divisor was  24
             // 19200 divisor was  12
             // Suggests a crystal of 3686400 hz
             if (d->baud_divisor > 0) {
               baud = 3686400 / (d->baud_divisor * 16);
             } else {
               baud = 0;
             }
           }
         }
         break;
      /* MODEM Control Register (MCR) */
      case 0x04:
      case 0x0C:
         if (op_type != MTS_READ) {
           _maybe_used char *f1 = "";
           _maybe_used char *f2 = "";
           _maybe_used char *f3 = "";
           _maybe_used char *f4 = "";
           _maybe_used char *f5 = "";
           if (*data & MCR_DTR) f1 = "DTR ";
           if (*data & MCR_RTS) f2 = "RTS ";
           if (*data & MCR_OUT1) f3 = "OUT1 ";
           if (*data & MCR_OUT2) f4 = "OUT2 ";
           if (*data & MCR_LOOP) f5 = "LOOP ";
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

      /* MODEM Status Register (MSR)?
      case 0x06:
      case 0x0E:
      */

      /* Scratch Register (SCR)?
      case 0x07:
      case 0x0F:
      */

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
   d->line_control_reg = 0;
   d->div_latch = 0;
   d->baud_divisor=0;
   
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
