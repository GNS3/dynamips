/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 *
 * PCI configuration space for SB-1 processor.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#define DEBUG_ACCESS  0

/* Sibyte PCI ID */
#define SB1_PCI_VENDOR_ID  0x166D

/* SB-1 PCI private data */
struct sb1_pci_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   struct pci_bus *pci_bus;

   /* PCI configuration (Bus 0, Device 0) */
   struct pci_device *pci_cfg_dev;
   
   /* HyperTransport configuration (Bus 0, Device 1) */
   struct pci_device *ht_cfg_dev;
};

/*
 * sb1_pci_cfg_read()
 *
 * PCI Configuration (Bus 0, Device 0).
 */
static m_uint32_t sb1_pci_cfg_read(cpu_gen_t *cpu,struct pci_device *dev,
                                   int reg)
{
   switch(reg) {
      case 0x08:
         return(0x06000002);
      default:
         return(0);
   }
}

/*
 * sb1_ht_cfg_read()
 *
 * HyperTransport Configuration (Bus 0, Device 1).
 */
static m_uint32_t sb1_ht_cfg_read(cpu_gen_t *cpu,struct pci_device *dev,
                                  int reg)
{
   switch(reg) {
      case 0x08:
         return(0x06000002);
      case 0x44:
         return(1<<5);         /* HyperTransport OK */
      default:
         return(0);
   }
}

/*
 * dev_sb1_pci_access()
 */
void *dev_sb1_pci_access(cpu_gen_t *cpu,struct vdevice *dev,
                         m_uint32_t offset,u_int op_size,u_int op_type,
                         m_uint64_t *data)
{
   struct sb1_pci_data *d = dev->priv_data;

#if DEBUG_ACCESS
   if (op_type == MTS_READ)
      cpu_log(cpu,dev->name,"read  access to offset = 0x%x, pc = 0x%llx\n",
              offset,cpu_get_pc(cpu));
   else
      cpu_log(cpu,dev->name,"write access to vaddr = 0x%x, pc = 0x%llx, "
              "val = 0x%llx\n",offset,cpu_get_pc(cpu),*data);
#endif

   if (op_type == MTS_READ)
      *data = 0;

   d->pci_bus->pci_addr = offset;
   pci_dev_data_handler(cpu,d->pci_bus,op_type,FALSE,data);
   return NULL;
}

/* Shutdown the PCI bus configuration zone */
void dev_sb1_pci_shutdown(vm_instance_t *vm,struct sb1_pci_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/* Create the SB-1 PCI bus configuration zone */
int dev_sb1_pci_init(vm_instance_t *vm,char *name,m_uint64_t paddr)
{  
   struct sb1_pci_data *d;

   /* allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"SB1_PCI: unable to create device.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->pci_bus = vm->pci_bus[0];

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_sb1_pci_shutdown;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = 1 << 24;
   d->dev.handler   = dev_sb1_pci_access;

   /* PCI configuration header on Bus 0, Device 0 */
   d->pci_cfg_dev = pci_dev_add(d->pci_bus,"sb1_pci_cfg",
                                SB1_PCI_VENDOR_ID,0x0001,0,0,-1,NULL,
                                NULL,sb1_pci_cfg_read,NULL);

   /* Create the HyperTransport bus #1 */
   vm->pci_bus_pool[28] = pci_bus_create("HT bus #1",-1);

   /* HyperTransport configuration header on Bus 0, Device 1 */
   d->ht_cfg_dev = pci_bridge_create_dev(d->pci_bus,"sb1_ht_cfg",
                                         SB1_PCI_VENDOR_ID,0x0002,
                                         1,0,vm->pci_bus_pool[28],
                                         sb1_ht_cfg_read,NULL);

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
