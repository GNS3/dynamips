/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Cirrus Logic PD6729 PCI-to-PCMCIA host adapter.
 *
 * TODO: finish the code! (especially extended registers) 
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
#include "pci_dev.h"
#include "pci_io.h"

#define DEBUG_ACCESS  0

/* Cirrus Logic PD6729 PCI vendor/product codes */
#define CLPD6729_PCI_VENDOR_ID    0x1013
#define CLPD6729_PCI_PRODUCT_ID   0x1100

#define CLPD6729_REG_CHIP_REV     0x00    /* Chip Revision */
#define CLPD6729_REG_INT_STATUS   0x01    /* Interface Status */
#define CLPD6729_REG_POWER_CTRL   0x02    /* Power Control */
#define CLPD6729_REG_INTGEN_CTRL  0x03    /* Interrupt & General Control */
#define CLPD6729_REG_CARD_STATUS  0x04    /* Card Status Change */
#define CLPD6729_REG_FIFO_CTRL    0x17    /* FIFO Control */
#define CLPD6729_REG_EXT_INDEX    0x2E    /* Extended Index */

/* CLPD6729 private data */
struct clpd6729_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   struct pci_device *pci_dev;
   struct pci_io_device *pci_io_dev;

   /* VM objects present in slots (typically, PCMCIA disks...) */
   vm_obj_t *slot_obj[2];

   /* Base registers */
   m_uint8_t base_index;
   m_uint8_t base_regs[256];
};

/* Handle access to a base register */
static void clpd6729_base_reg_access(cpu_gen_t *cpu,struct clpd6729_data *d,
                                     u_int op_type,m_uint64_t *data)
{
   u_int slot_id,reg;
   
#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"CLPD6729","reading reg 0x%2.2x at pc=0x%llx\n",
              d->base_index,cpu_get_pc(cpu));
   } else {
      cpu_log(cpu,"CLPD6729","writing reg 0x%2.2x, data=0x%llx at pc=0x%llx\n",
              d->base_index,*data,cpu_get_pc(cpu));
   }
#endif

   if (op_type == MTS_READ)
      *data = 0;

   /* Reserved registers */
   if (d->base_index >= 0x80)
      return;

   /* 
    * Socket A regs: 0x00 to 0x3f
    * Socket B regs: 0x40 to 0x7f
    */
   if (d->base_index >= 0x40) {
      slot_id = 1;
      reg = d->base_index - 0x40;
   } else {
      slot_id = 0;
      reg = d->base_index;
   }

   switch(reg) {
      case CLPD6729_REG_CHIP_REV:
         if (op_type == MTS_READ)
            *data = 0x48;
         break;

      case CLPD6729_REG_INT_STATUS:
         if (op_type == MTS_READ) {
            if (d->slot_obj[slot_id])
               *data = 0xEF;
            else
               *data = 0x80;
         }
         break;

      case CLPD6729_REG_INTGEN_CTRL:
         if (op_type == MTS_READ)
            *data = 0x40;
         break;

      case CLPD6729_REG_EXT_INDEX:
         if (op_type == MTS_WRITE) {
            cpu_log(cpu,"CLPD6729","ext reg index 0x%2.2llx at pc=0x%llx\n",
                    *data,cpu_get_pc(cpu));
         }
         break;

      case CLPD6729_REG_FIFO_CTRL:
         if (op_type == MTS_READ)
            *data = 0x80;  /* FIFO is empty */
         break;

      default:
         if (op_type == MTS_READ)
            *data = d->base_regs[d->base_index];
         else
            d->base_regs[d->base_index] = (m_uint8_t)(*data);
   }
}

/*
 * dev_clpd6729_io_access()
 */
static void *dev_clpd6729_io_access(cpu_gen_t *cpu,struct vdevice *dev,
                                    m_uint32_t offset,u_int op_size,
                                    u_int op_type,m_uint64_t *data)
{
   struct clpd6729_data *d = dev->priv_data;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,dev->name,"reading at offset 0x%x, pc=0x%llx\n",
              offset,cpu_get_pc(cpu));
   } else {
      cpu_log(cpu,dev->name,"writing at offset 0x%x, pc=0x%llx, data=0x%llx\n",
              offset,cpu_get_pc(cpu),*data);
   }
#endif

   switch(offset) {
      case 0:
         /* Data register */
         clpd6729_base_reg_access(cpu,d,op_type,data);
         break;

      case 1:
         /* Index register */
         if (op_type == MTS_READ)
            *data = d->base_index;
         else
            d->base_index = *data;
         break;
   }

   return NULL;
}

/* Shutdown a CLPD6729 device */
void dev_clpd6729_shutdown(vm_instance_t *vm,struct clpd6729_data *d)
{
   if (d != NULL) {
      /* Remove the PCI device */
      pci_dev_remove(d->pci_dev);

      /* Remove the PCI I/O device */
      pci_io_remove(d->pci_io_dev);

      /* Free the structure itself */
      free(d);
   }
}

/*
 * dev_clpd6729_init()
 */
int dev_clpd6729_init(vm_instance_t *vm,
                      struct pci_bus *pci_bus,int pci_device,
                      struct pci_io_data *pci_io_data,
                      m_uint32_t io_start,m_uint32_t io_end)
{
   struct clpd6729_data *d;

   /* Allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"CLPD6729: unable to create device.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   vm_object_init(&d->vm_obj);
   d->vm_obj.name = "clpd6729";
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_clpd6729_shutdown;

   dev_init(&d->dev);
   d->dev.name = "clpd6729";
   d->dev.priv_data = d;

   d->pci_io_dev = pci_io_add(pci_io_data,io_start,io_end,&d->dev,
                              dev_clpd6729_io_access);

   d->pci_dev = pci_dev_add(pci_bus,"clpd6729",
                            CLPD6729_PCI_VENDOR_ID,CLPD6729_PCI_PRODUCT_ID,
                            pci_device,0,-1,&d->dev,NULL,NULL,NULL);

   if (!d->pci_io_dev || !d->pci_dev) {
      fprintf(stderr,"CLPD6729: unable to create PCI devices.\n");
      dev_clpd6729_shutdown(vm,d);
      return(-1);
   }

   vm_object_add(vm,&d->vm_obj);

#if 1
   /* PCMCIA disk test */
   if (vm->pcmcia_disk_size[0])
      d->slot_obj[0] = dev_pcmcia_disk_init(vm,"disk0",0x40000000ULL,0x200000,
                                            vm->pcmcia_disk_size[0],0);

   if (vm->pcmcia_disk_size[1])
      d->slot_obj[1] = dev_pcmcia_disk_init(vm,"disk1",0x44000000ULL,0x200000,
                                            vm->pcmcia_disk_size[1],0);
#endif

#if 0
   /* PCMCIA disk test */
   if (vm->pcmcia_disk_size[0])
      d->slot_obj[0] = dev_pcmcia_disk_init(vm,"disk0",0xd8000000ULL,0x200000,
                                            vm->pcmcia_disk_size[0],0);

   if (vm->pcmcia_disk_size[1])
      d->slot_obj[1] = dev_pcmcia_disk_init(vm,"disk1",0xdc000000ULL,0x200000,
                                            vm->pcmcia_disk_size[1],0);
#endif

   return(0);
}
