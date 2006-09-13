/*
 * Cisco C7200 (Predator) Simulation Platform.
 * Copyright (C) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * PLX PCI9060 - PCI bus master interface chip.
 *
 * This is very basic, it has been designed to allow the C7200 PA-POS-OC3
 * to work.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "pci_dev.h"

#define DEBUG_ACCESS  1

/* PLX9060 vendor/product codes */
#define PLX9060_PCI_VENDOR_ID    0x10b5
#define PLX9060_PCI_PRODUCT_ID   0x9060

#define PLX9060_S0_RANGE_DEFAULT      0xFFF00000
#define PLX9060_S0_RANGE_DECODE_MASK  0xFFFFFFF0

/* PLX9060 data */
struct plx9060_data {
   /* Device name */
   char *name;

   /* Virtual machine and object info */
   vm_instance_t *vm;
   vm_obj_t vm_obj;

   /* Virtual PLX device */
   struct vdevice plx_dev;
   struct pci_device *pci_plx_dev;

   /* Managed device */
   struct vdevice *dev;

   /* Space 0 properties */
   m_uint32_t space0_lbaddr;
   m_uint32_t space0_range;
};

/* Log a PLX message */
#define PLX_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* Map space 0 */
static void plx9060_map_space0(struct plx9060_data *d)
{
   d->dev->phys_len = 1+(~(d->space0_range & PLX9060_S0_RANGE_DECODE_MASK));
   vm_map_device(d->vm,d->dev,d->space0_lbaddr);

   PLX_LOG(d,"device mapped at 0x%llx, size=0x%x\n",
           d->dev->phys_addr,d->dev->phys_len);
}

/* PLX9060 access */
void *dev_plx9060_access(cpu_mips_t *cpu,struct vdevice *dev,
                         m_uint32_t offset,u_int op_size,u_int op_type,
                         m_uint64_t *data)
{
   struct plx9060_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

   switch(offset) {
      case 0x00:
         if (op_type == MTS_WRITE) {
            d->space0_range = *data;
            plx9060_map_space0(d);
         } else {
            *data = d->space0_range;
         }
         break;

      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->name,
                    "read from unhandled addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu->pc,op_size);
         } else {
            cpu_log(cpu,d->name,
                    "write to handled addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",offset,*data,cpu->pc,op_size);
         }
   }

   return NULL;
}

/*
 * pci_plx9060_read()
 */
static m_uint32_t pci_plx9060_read(cpu_mips_t *cpu,struct pci_device *dev,
                                   int reg)
{   
   struct plx9060_data *d = dev->priv_data;

#if DEBUG_ACCESS
   PLX_LOG(d,"read PLX PCI register 0x%x\n",reg);
#endif
   switch(reg) {
      case PCI_REG_BAR0:
         return(d->plx_dev.phys_addr);
      case PCI_REG_BAR2:
         return(d->space0_lbaddr);
      default:
         return(0);
   }
}

/*
 * pci_plx9060_cs_write()
 */
static void pci_plx9060_write(cpu_mips_t *cpu,struct pci_device *dev,
                             int reg,m_uint32_t value)
{
   struct plx9060_data *d = dev->priv_data;

#if DEBUG_ACCESS
   PLX_LOG(d,"write 0x%x to PLX PCI register 0x%x\n",value,reg);
#endif

   switch(reg) {
      case PCI_REG_BAR0:
         vm_map_device(cpu->vm,&d->plx_dev,(m_uint64_t)value);
         PLX_LOG(d,"PLX registers are mapped at 0x%x\n",value);
         break;
      case PCI_REG_BAR2:
         d->space0_lbaddr = value;
         plx9060_map_space0(d);
         break;
   }
}

/* Shutdown a PLX9060 device */
void dev_plx9060_shutdown(vm_instance_t *vm,struct plx9060_data *d)
{
   if (d != NULL) {
      /* Unbind the managed device */
      vm_unbind_device(vm,d->dev);

      /* Remove the PLX device */
      dev_remove(vm,&d->plx_dev);

      /* Remove the PCI PLX device */
      pci_dev_remove(d->pci_plx_dev);

      /* Free the structure itself */
      free(d);
   }
}

/* Create a PLX9060 device */
vm_obj_t *dev_plx9060_init(vm_instance_t *vm,char *name,
                           struct pci_bus *pci_bus,int pci_device,
                           struct vdevice *dev)
{
   struct plx9060_data *d;

   if (!dev)
      return NULL;

   /* Allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"PLX9060: unable to create device.\n");
      return NULL;
   }

   memset(d,0,sizeof(*d));
   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_plx9060_shutdown;

   d->vm   = vm;
   d->dev  = dev;
   d->name = name;
   d->space0_range = PLX9060_S0_RANGE_DEFAULT;

   dev_init(&d->plx_dev);
   d->plx_dev.name      = name;
   d->plx_dev.priv_data = d;
   d->plx_dev.phys_addr = 0;
   d->plx_dev.phys_len  = 0x1000;
   d->plx_dev.handler   = dev_plx9060_access;

   /* Add PLX as a PCI device */
   d->pci_plx_dev = pci_dev_add(pci_bus,name,
                                PLX9060_PCI_VENDOR_ID,PLX9060_PCI_PRODUCT_ID,
                                pci_device,0,-1,d,
                                NULL,pci_plx9060_read,pci_plx9060_write);

   if (!d->pci_plx_dev) {
      fprintf(stderr,"%s (PLX9060): unable to create PCI device.\n",name);
      goto err_pci_dev;
   }

   vm_object_add(vm,&d->vm_obj);
   return(&d->vm_obj);

 err_pci_dev:
   free(d);
   return NULL;
}
