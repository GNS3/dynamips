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

#include "ptask.h"
#include "vm.h"
#include "dynamips.h"
#include "device.h"
#include "dev_vtty.h"
#include "rust-dynamips.h"

/* Debugging flags */
#define DEBUG_ACCESS   0

/* NS16552 structure */
struct ns16552_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   vm_instance_t *vm;
   u_int irq;
   vtty_t *vtty_A;
   vtty_t *vtty_B;
   MutexNs16552 *duart;
   
   /* Register offset divisor */
   u_int reg_div;

   /* Periodic task to trigger DUART logic */
   ptask_id_t tid;
};

/* Console/aux port input */
static void tty_con_aux_input(vtty_t *vtty)
{
   struct ns16552_data *d = vtty->priv_data;

   ns16552_tick(d->duart);
}

/* Trigger DUART logic */
static int dev_ns16552_tick(void *object,void *_arg)
{
   struct ns16552_data *d = object;

   ns16552_tick(d->duart);

   return(0);
}

/*
 * dev_ns16552_access()
 */
void *dev_ns16552_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct ns16552_data *d = dev->priv_data;

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

   if (op_type == MTS_READ) {
      *data = ns16552_read_access(d->duart, offset);
   } else if (op_type == MTS_WRITE) {
      ns16552_write_access(d->duart, offset, (uint8_t)(*data & 0xff));
      *data = 0;
   }

   return NULL;
}

/* Shutdown a NS16552 device */
void dev_ns16552_shutdown(vm_instance_t *vm,struct ns16552_data *d)
{
   if (d != NULL) {
      if (d->vtty_A != NULL) {
         d->vtty_A->read_notifier = NULL;
         d->vtty_A->priv_data = NULL;
      }
      if (d->vtty_B != NULL) {
         d->vtty_B->read_notifier = NULL;
         d->vtty_B->priv_data = NULL;
      }

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
      goto err_malloc;
   }

   memset(d,0,sizeof(*d));
   d->vm  = vm;
   d->irq = irq;
   d->reg_div = reg_div;
   d->vtty_A = vtty_A;
   d->vtty_B = vtty_B;

   d->duart = ns16552_new(vm, irq, vtty_A, vtty_B);
   if (d->duart == NULL) {
      fprintf(stderr,"NS16552: failed to allocate duart\n");
      goto err_duart;
   }
   
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

   if (vtty_A != NULL) {
      vtty_A->priv_data = d;
      vtty_A->read_notifier = tty_con_aux_input;
   }
   if (vtty_B != NULL) {
      vtty_B->priv_data = d;
      vtty_B->read_notifier = tty_con_aux_input;
   }

   /* Trigger periodically a dummy IRQ to flush buffers */
   d->tid = ptask_add(dev_ns16552_tick,d,NULL);

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);   
   vm_object_add(vm,&d->vm_obj);
   return(0);

err_duart:
   free(d);
err_malloc:
   return(-1);
}
