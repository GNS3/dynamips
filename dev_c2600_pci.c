/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Cisco 2600 PCI controller.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "net.h"
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net_io.h"

/* Debugging flags */
#define DEBUG_ACCESS    0
#define DEBUG_UNKNOWN   1

/* Galileo GT64xxx/GT96xxx system controller */
struct c2600_pci_data {
   char *name;
   vm_obj_t vm_obj;
   struct vdevice dev;
   struct pci_device *pci_dev;
   vm_instance_t *vm;

   struct pci_bus *bus;
};

/*
 * dev_c2600_pci_access()
 */
void *dev_c2600_pci_access(cpu_gen_t *cpu,struct vdevice *dev,
                           m_uint32_t offset,u_int op_size,u_int op_type,
                           m_uint64_t *data)
{
   struct c2600_pci_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0x0;

   switch(offset) {
      case 0x500:
         pci_dev_addr_handler(cpu,d->bus,op_type,FALSE,data);
         break;

      case 0x504:
         pci_dev_data_handler(cpu,d->bus,op_type,FALSE,data);
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->name,"read from addr 0x%x, pc=0x%llx\n",
                    offset,cpu_get_pc(cpu));
         } else {
            cpu_log(cpu,d->name,"write to addr 0x%x, value=0x%llx, "
                    "pc=0x%llx\n",offset,*data,cpu_get_pc(cpu));
         }
#endif
   }

   return NULL;
}

/* Shutdown the c2600 PCI controller device */
void dev_c2600_pci_shutdown(vm_instance_t *vm,struct c2600_pci_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/* Create the c2600 PCI controller device */
int dev_c2600_pci_init(vm_instance_t *vm,char *name,
                       m_uint64_t paddr,m_uint32_t len,
                       struct pci_bus *bus)
{
   struct c2600_pci_data *d;

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"c2600_pci: unable to create device data.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->name = name;
   d->vm   = vm;
   d->bus  = bus;

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_c2600_pci_shutdown;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_c2600_pci_access;

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}

