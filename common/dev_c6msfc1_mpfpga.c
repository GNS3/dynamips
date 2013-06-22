/* 
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * MSFC1 Midplane FPGA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "nmc93cX6.h"
#include "dev_c6msfc1.h"

#define DEBUG_UNKNOWN  1
#define DEBUG_ACCESS   1
#define DEBUG_NET_IRQ  1

/* Midplane FPGA private data */
struct c6msfc1_mpfpga_data {
   vm_obj_t vm_obj;
   struct vdevice dev;

   c6msfc1_t *router;
   m_uint32_t irq_status;
   m_uint32_t intr_enable;
};

/* Update network interrupt status */
static inline 
void dev_c6msfc1_mpfpga_net_update_irq(struct c6msfc1_mpfpga_data *d)
{
   if (d->irq_status) {
      vm_set_irq(d->router->vm,C6MSFC1_NETIO_IRQ);
   } else {
      vm_clear_irq(d->router->vm,C6MSFC1_NETIO_IRQ);
   }
}

/* Trigger a Network IRQ for the specified slot/port */
void dev_c6msfc1_mpfpga_net_set_irq(struct c6msfc1_mpfpga_data *d,
                                    u_int slot,u_int port)
{
#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"MP_FPGA","setting NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   d->irq_status |= 1 << slot;
   dev_c6msfc1_mpfpga_net_update_irq(d);
}

/* Clear a Network IRQ for the specified slot/port */
void dev_c6msfc1_mpfpga_net_clear_irq(struct c6msfc1_mpfpga_data *d,
                                      u_int slot,u_int port)
{
#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"MP_FPGA","clearing NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   d->irq_status &= ~(1 << slot);
   dev_c6msfc1_mpfpga_net_update_irq(d);
}

/*
 * dev_c6msfc1_access()
 */
void *dev_c6msfc1_mpfpga_access(cpu_gen_t *cpu,struct vdevice *dev,
                                m_uint32_t offset,u_int op_size,u_int op_type,
                                m_uint64_t *data)
{
   struct c6msfc1_mpfpga_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0x0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"MP_FPGA","reading reg 0x%x at pc=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),op_size);
   } else {
      cpu_log(cpu,"MP_FPGA",
              "writing reg 0x%x at pc=0x%llx, data=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),*data,op_size);
   }
#endif

   switch(offset) {
      /* 
       * Revision + Slot: just tell we're in slot 1 (and chip rev 2)
       * Other bits are unknown.
       */
      case 0x00:
         if (op_type == MTS_READ)
            *data = 0x12;
         break;

      /* Interrupt Control ("sh msfc") - unknown */
      case 0x08:
         if (op_type == MTS_READ)
            *data = 0x1c;
         break;

      /* Interrupt Enable ("sh msfc") */
      case 0x10:
         if (op_type == MTS_READ)
            *data = d->intr_enable;
         else
            d->intr_enable = *data;
         break;
         
      /* 
       * Read when a Network Interrupt is triggered.
       *   Bit 0: EOBC
       *   Bit 1: IBC
       */
      case 0x18:
      case 0x1b:
         if (op_type == MTS_READ)
            *data = d->irq_status;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"MP_FPGA","read from unknown addr 0x%x, pc=0x%llx\n",
                    offset,cpu_get_pc(cpu));
         } else {
            cpu_log(cpu,"MP_FPGA","write to unknown addr 0x%x, value=0x%llx, "
                    "pc=0x%llx\n",offset,*data,cpu_get_pc(cpu));
         }
#endif
   }
	
   return NULL;
}

/* Shutdown the MP FPGA device */
static void 
dev_c6msfc1_mpfpga_shutdown(vm_instance_t *vm,struct c6msfc1_mpfpga_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);
      
      /* Free the structure itself */
      free(d);
   }
}

/* 
 * dev_c6msfc1_mpfpga_init()
 */
int dev_c6msfc1_mpfpga_init(c6msfc1_t *router,m_uint64_t paddr,m_uint32_t len)
{   
   struct c6msfc1_mpfpga_data *d;

   /* Allocate private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"MP_FPGA: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->router = router;
   
   vm_object_init(&d->vm_obj);
   d->vm_obj.name = "mp_fpga";
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_c6msfc1_mpfpga_shutdown;

   /* Set device properties */
   dev_init(&d->dev);
   d->dev.name      = "mp_fpga";
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_c6msfc1_mpfpga_access;
   d->dev.priv_data = d;

   /* Map this device to the VM */
   vm_bind_device(router->vm,&d->dev);
   vm_object_add(router->vm,&d->vm_obj);
   return(0);
}
