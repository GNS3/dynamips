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
#define DEBUG_PCI       1

#define C2600_PCI_BRIDGE_VENDOR_ID   0x10ee
#define C2600_PCI_BRIDGE_PRODUCT_ID  0x4013

/* C2600 PCI controller */
struct c2600_pci_data {
   char *name;
   vm_obj_t vm_obj;
   struct vdevice dev;
   struct pci_device *pci_dev;
   vm_instance_t *vm;

   struct pci_bus *bus;
   m_uint32_t bridge_bar0,bridge_bar1;
};

/*
 * dev_c2600_pci_access()
 */
void *dev_c2600_pci_access(cpu_gen_t *cpu,struct vdevice *dev,
                           m_uint32_t offset,u_int op_size,u_int op_type,
                           m_uint64_t *data)
{
   struct c2600_pci_data *d = dev->priv_data;
   struct pci_device *pci_dev;
   u_int bus,device,function,reg;

   if (op_type == MTS_READ)
      *data = 0x0;

   bus      = 0;
   device   = (offset >> 12) & 0x0F;
   function = (offset >> 8)  & 0x07;
   reg      = offset & 0xFF;

   /* Find the corresponding PCI device */
   pci_dev = pci_dev_lookup(d->bus,bus,device,function);

#if DEBUG_PCI
   if (op_type == MTS_READ) {
      cpu_log(cpu,"PCI","read request at pc=0x%llx: "
              "bus=%d,device=%d,function=%d,reg=0x%2.2x\n",
              cpu_get_pc(cpu), bus, device, function, reg);
   } else {
      cpu_log(cpu,"PCI","write request (data=0x%8.8llx) at pc=0x%llx: "
              "bus=%d,device=%d,function=%d,reg=0x%2.2x\n",
              *data, cpu_get_pc(cpu), bus, device, function, reg);
   }
#endif

   if (!pci_dev) {
      if (op_type == MTS_READ) {
         cpu_log(cpu,"PCI","read request for unknown device at pc=0x%llx "
                 "(bus=%d,device=%d,function=%d,reg=0x%2.2x).\n",
                 cpu_get_pc(cpu), bus, device, function, reg);
      } else {
         cpu_log(cpu,"PCI","write request (data=0x%8.8llx) for unknown device "
                 "at pc=0x%llx (bus=%d,device=%d,function=%d,reg=0x%2.2x).\n",
                 *data, cpu_get_pc(cpu), bus, device, function, reg);
      }

      /* Returns an invalid device ID */
      if ((op_type == MTS_READ) && (reg == PCI_REG_ID))
         *data = 0xffffffff;
   } else {
      if (op_type == MTS_WRITE) {
         if (pci_dev->write_register != NULL)
            pci_dev->write_register(cpu,pci_dev,reg,*data);
      } else {
         if (reg == PCI_REG_ID)
            *data = (pci_dev->product_id << 16) | pci_dev->vendor_id;
         else {
            if (pci_dev->read_register != NULL)
               *data = pci_dev->read_register(cpu,pci_dev,reg);
         }
      }
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

/* PCI bridge read access */
static m_uint32_t dev_c2600_pci_bridge_read(cpu_gen_t *cpu,
                                            struct pci_device *dev,
                                            int reg)
{
   struct c2600_pci_data *d = dev->priv_data;

   switch(reg) {
      case 0x10:
         return(d->bridge_bar0);
      case 0x14:
         return(d->bridge_bar1);
      default:
         return(0);
   }
}

/* PCI bridge read access */
static void dev_c2600_pci_bridge_write(cpu_gen_t *cpu,struct pci_device *dev,
                                       int reg,m_uint32_t value)
{
   struct c2600_pci_data *d = dev->priv_data;

   switch(reg) {
      case 0x10:
         /* BAR0 must be at 0x00000000 for correct RAM access */
         if (value != 0x00000000) {
            vm_error(d->vm,"C2600_PCI",
                     "Trying to set bridge BAR0 at 0x%8.8x!\n",
                     value);                     
         }
         d->bridge_bar0 = value;
         break;
      case 0x14:
         /* BAR1 = byte swapped zone */
         if (!d->bridge_bar1) {
            d->bridge_bar1 = value;
            
            /* XXX */
            dev_bswap_init(d->vm,"pci_bswap",d->bridge_bar1,0x10000000,
                           0x00000000);
         }
         break;
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

   pci_dev_add(d->bus,"pci_bridge",
               C2600_PCI_BRIDGE_VENDOR_ID,C2600_PCI_BRIDGE_PRODUCT_ID,
               15,0,-1,d,
               NULL,
               dev_c2600_pci_bridge_read,
               dev_c2600_pci_bridge_write);

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}

