/*
 * Cisco router simulation platform.
 * Copyright (C) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * PLX PCI9060/PCI9054 - PCI bus master interface chip.
 *
 * This is very basic, it has been designed to allow the C7200 PA-POS-OC3
 * to work, and for the upcoming PA-MC-8TE1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "pci_dev.h"
#include "dev_plx.h"

#define DEBUG_ACCESS  1

/* PLX vendor/product codes */
#define PLX_PCI_VENDOR_ID        0x10b5
#define PLX9060_PCI_PRODUCT_ID   0x9060
#define PLX9054_PCI_PRODUCT_ID   0x9054

/* Number of Local Spaces (1 on 9060, 2 on 9054) */
#define PLX_LOCSPC_MAX  2

/* Local Space ranges */
#define PLX_LOCSPC_RANGE_DEFAULT      0xFFF00000
#define PLX_LOCSPC_RANGE_DECODE_MASK  0xFFFFFFF0

/* Local space definition */
struct plx_locspace {
   m_uint32_t lbaddr;
   m_uint32_t range;
   struct vdevice *dev;
};

/* PLX data */
struct plx_data {
   /* Device name */
   char *name;

   /* Variant (9060, 9054) */
   u_int variant;

   /* Virtual machine and object info */
   vm_instance_t *vm;
   vm_obj_t vm_obj;

   /* Virtual PLX device */
   struct vdevice plx_dev;
   struct pci_device *pci_plx_dev;

   /* Local spaces */
   struct plx_locspace lspc[PLX_LOCSPC_MAX];

   /* Doorbell registers */
   m_uint32_t pci2loc_doorbell_reg,loc2pci_doorbell_reg;
   dev_plx_doorbell_cbk pci2loc_doorbell_cbk;
   void *pci2loc_doorbell_cbk_arg;
};

/* Log a PLX message */
#define PLX_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* Map a local space device */
static void plx_map_space(struct plx_data *d,u_int id)
{
   struct plx_locspace *lspc;

   lspc = &d->lspc[id];

   if (!lspc->dev)
      return;

   lspc->dev->phys_len = 1+(~(lspc->range & PLX_LOCSPC_RANGE_DECODE_MASK));
   vm_map_device(d->vm,lspc->dev,lspc->lbaddr);

   PLX_LOG(d,"device %u mapped at 0x%llx, size=0x%x\n",
           id,lspc->dev->phys_addr,lspc->dev->phys_len);
}

/* PLX device common access routine */
void *dev_plx_access(cpu_gen_t *cpu,struct vdevice *dev,
                     m_uint32_t offset,u_int op_size,u_int op_type,
                     m_uint64_t *data)
{
   struct plx_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

   switch(offset) {
      /* Local Address Space 0 Range Register */
      case 0x00:
         if (op_type == MTS_WRITE) {
            d->lspc[0].range = *data;
            plx_map_space(d,0);
         } else {
            *data = d->lspc[0].range;
         }
         break;

      /* PCI-to-Local Doorbell Register */
      case 0x60:
         if (op_type == MTS_WRITE) {
            d->pci2loc_doorbell_reg = *data;

            if (d->pci2loc_doorbell_cbk != NULL) {
               d->pci2loc_doorbell_cbk(d,d->pci2loc_doorbell_cbk_arg,
                                       d->pci2loc_doorbell_reg);
            }
         }
         break;

      /* Local-to-PCI Doorbell Register */
      case 0x64:
         if (op_type == MTS_READ)
            *data = d->loc2pci_doorbell_reg;
         else
            d->loc2pci_doorbell_reg &= ~(*data);
         break;

      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->name,
                    "read from unhandled addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,d->name,
                    "write to handled addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
   }

   return NULL;
}

/* PLX9054 access routine */
void *dev_plx9054_access(cpu_gen_t *cpu,struct vdevice *dev,
                         m_uint32_t offset,u_int op_size,u_int op_type,
                         m_uint64_t *data)
{
   struct plx_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

   switch(offset) {
      /* Local Address Space 1 Range Register */
      case 0xF0:
         if (op_type == MTS_WRITE) {
            d->lspc[1].range = *data;
            plx_map_space(d,1);
         } else {
            *data = d->lspc[1].range;
         }
         break;

      default:
         return(dev_plx_access(cpu,dev,offset,op_size,op_type,data));
   }

   return NULL;
}

/*
 * pci_plx_read() - Common PCI read.
 */
static m_uint32_t pci_plx_read(cpu_gen_t *cpu,struct pci_device *dev,int reg)
{   
   struct plx_data *d = dev->priv_data;

#if DEBUG_ACCESS
   PLX_LOG(d,"read PLX PCI register 0x%x\n",reg);
#endif
   switch(reg) {
      /* PLX registers */
      case PCI_REG_BAR0:
         return(d->plx_dev.phys_addr);

      /* Local space 0 */
      case PCI_REG_BAR2:
         return(d->lspc[0].lbaddr);

      default:
         return(0);
   }
}

/*
 * pci_plx_write() - Common PCI write.
 */
static void pci_plx_write(cpu_gen_t *cpu,struct pci_device *dev,
                          int reg,m_uint32_t value)
{
   struct plx_data *d = dev->priv_data;

#if DEBUG_ACCESS
   PLX_LOG(d,"write 0x%x to PLX PCI register 0x%x\n",value,reg);
#endif

   switch(reg) {
      /* PLX registers */
      case PCI_REG_BAR0:
         vm_map_device(cpu->vm,&d->plx_dev,(m_uint64_t)value);
         PLX_LOG(d,"PLX registers are mapped at 0x%x\n",value);
         break;

      /* Local space 0 */
      case PCI_REG_BAR2:
         d->lspc[0].lbaddr = value;
         plx_map_space(d,0);
         break;
   }
}

/*
 * pci_plx9054_read()
 */
static m_uint32_t pci_plx9054_read(cpu_gen_t *cpu,struct pci_device *dev,
                                   int reg)
{   
   struct plx_data *d = dev->priv_data;

#if DEBUG_ACCESS
   PLX_LOG(d,"read PLX PCI register 0x%x\n",reg);
#endif
   switch(reg) {
      /* Local space 1 */
      case PCI_REG_BAR3:
         return(d->lspc[1].lbaddr);

      default:
         return(pci_plx_read(cpu,dev,reg));
   }
}

/*
 * pci_plx9054_write()
 */
static void pci_plx9054_write(cpu_gen_t *cpu,struct pci_device *dev,
                              int reg,m_uint32_t value)
{
   struct plx_data *d = dev->priv_data;

#if DEBUG_ACCESS
   PLX_LOG(d,"write 0x%x to PLX PCI register 0x%x\n",value,reg);
#endif

   switch(reg) {
      /* Local space 1 */
      case PCI_REG_BAR3:
         d->lspc[1].lbaddr = value;
         plx_map_space(d,1);
         break;

      default:
         pci_plx_write(cpu,dev,reg,value);
   }
}

/* Shutdown a PLX device */
void dev_plx_shutdown(vm_instance_t *vm,struct plx_data *d)
{
   int i;

   if (d != NULL) {
      /* Unbind the managed devices */
      for(i=0;i<PLX_LOCSPC_MAX;i++) {
         if (d->lspc[i].dev)
            vm_unbind_device(vm,d->lspc[i].dev);
      }

      /* Remove the PLX device */
      dev_remove(vm,&d->plx_dev);

      /* Remove the PCI PLX device */
      pci_dev_remove(d->pci_plx_dev);

      /* Free the structure itself */
      free(d);
   }
}

/* Create a generic PLX device */
struct plx_data *dev_plx_init(vm_instance_t *vm,char *name)
{
   struct plx_data *d;
   int i;

   /* Allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"PLX: unable to create device.\n");
      return NULL;
   }

   memset(d,0,sizeof(*d));
   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_plx_shutdown;

   d->vm   = vm;
   d->name = name;

   for(i=0;i<PLX_LOCSPC_MAX;i++)
      d->lspc[i].range = PLX_LOCSPC_RANGE_DEFAULT;

   dev_init(&d->plx_dev);
   d->plx_dev.name      = name;
   d->plx_dev.priv_data = d;
   d->plx_dev.phys_addr = 0;
   d->plx_dev.phys_len  = 0x1000;
   d->plx_dev.handler   = dev_plx_access;
   return(d);
}

/* Create a PLX9060 device */
vm_obj_t *dev_plx9060_init(vm_instance_t *vm,char *name,
                           struct pci_bus *pci_bus,int pci_device,
                           struct vdevice *dev0)
{
   struct plx_data *d;

   /* Create the PLX data */
   if (!(d = dev_plx_init(vm,name))) {
      fprintf(stderr,"PLX: unable to create device.\n");
      return NULL;
   }

   /* Set the PLX variant */
   d->variant = 9060;

   /* Set device for Local Space 0 */
   d->lspc[0].dev = dev0;

   /* Add PLX as a PCI device */
   d->pci_plx_dev = pci_dev_add(pci_bus,name,
                                PLX_PCI_VENDOR_ID,PLX9060_PCI_PRODUCT_ID,
                                pci_device,0,-1,d,
                                NULL,pci_plx_read,pci_plx_write);

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

/* Create a PLX9054 device */
vm_obj_t *dev_plx9054_init(vm_instance_t *vm,char *name,
                           struct pci_bus *pci_bus,int pci_device,
                           struct vdevice *dev0,struct vdevice *dev1)
{
   struct plx_data *d;

   /* Create the PLX data */
   if (!(d = dev_plx_init(vm,name))) {
      fprintf(stderr,"PLX: unable to create device.\n");
      return NULL;
   }

   /* Set the PLX variant */
   d->variant = 9054;
   d->plx_dev.handler = dev_plx9054_access;

   /* Set device for Local Space 0 and 1 */
   d->lspc[0].dev = dev0;
   d->lspc[1].dev = dev1;

   /* Add PLX as a PCI device */
   d->pci_plx_dev = pci_dev_add(pci_bus,name,
                                PLX_PCI_VENDOR_ID,PLX9054_PCI_PRODUCT_ID,
                                pci_device,0,-1,d,
                                NULL,pci_plx9054_read,pci_plx9054_write);

   if (!d->pci_plx_dev) {
      fprintf(stderr,"%s (PLX9054): unable to create PCI device.\n",name);
      goto err_pci_dev;
   }

   vm_object_add(vm,&d->vm_obj);
   return(&d->vm_obj);

 err_pci_dev:
   free(d);
   return NULL;
}

/* Set callback function for PCI-to-Local doorbell register */
void dev_plx_set_pci2loc_doorbell_cbk(struct plx_data *d,
                                      dev_plx_doorbell_cbk cbk,
                                      void *arg)
{
   if (d != NULL) {
      d->pci2loc_doorbell_cbk = cbk;
      d->pci2loc_doorbell_cbk_arg = arg;
   }
}

/* Set the Local-to-PCI doorbell register (for Local device use) */
void dev_plx_set_loc2pci_doorbell_reg(struct plx_data *d,m_uint32_t value)
{
   if (d != NULL)
      d->loc2pci_doorbell_reg = value;
}
