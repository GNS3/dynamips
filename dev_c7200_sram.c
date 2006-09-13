/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Packet SRAM. This is a fast memory zone for packets on NPE150/NPE200.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#define PCI_VENDOR_SRAM   0x1137
#define PCI_PRODUCT_SRAM  0x0005

/* SRAM structure */
struct sram_data {
   /* VM object info */
   vm_obj_t vm_obj;
   
   /* SRAM main device */
   struct vdevice *dev;

   /* Aliased device */
   char *alias_dev_name;
   struct vdevice *alias_dev;

   /* Byte-swapped device */
   char *bs_dev_name;
   struct vdevice *bs_dev;

   /* PCI device */
   struct pci_device *pci_dev;

   /* Filename used to virtualize SRAM */
   char *filename;
};

/*
 * SRAM byte swapped access.
 */
static void *dev_sram_bs_access(cpu_mips_t *cpu,struct vdevice *dev,
                                m_uint32_t offset,u_int op_size,u_int op_type,
                                m_uint64_t *data)
{
   void *ptr = (u_char *)dev->host_addr + offset;

   switch(op_size) {
      case 1:
         return(ptr);

      case 2:
         if (op_type == MTS_READ)
            *data = swap16(htovm16(*(m_uint16_t *)ptr));
         else
            *(m_uint16_t *)ptr = vmtoh16(swap16(*data));
         break;

      case 4:
         if (op_type == MTS_READ)
            *data = swap32(htovm32(*(m_uint32_t *)ptr));
         else
            *(m_uint32_t *)ptr = vmtoh32(swap32(*data));
         break;

      case 8:
         if (op_type == MTS_READ)
            *data = swap64(htovm64(*(m_uint64_t *)ptr));
         else
            *(m_uint64_t *)ptr = vmtoh64(swap64(*data));
         break;
   }

   return NULL;
}

/* Shutdown an SRAM device */
void dev_c7200_sram_shutdown(vm_instance_t *vm,struct sram_data *d)
{
   if (d != NULL) {
      /* Remove the PCI device */
      pci_dev_remove(d->pci_dev);

      /* Remove the alias, the byte-swapped and the main device */
      dev_remove(vm,d->alias_dev);
      dev_remove(vm,d->bs_dev);
      dev_remove(vm,d->dev);

      /* Free devices */
      free(d->alias_dev);
      free(d->bs_dev);
      free(d->dev);
      
      /* Free device names */
      free(d->alias_dev_name);
      free(d->bs_dev_name);

      /* Remove filename used to virtualize SRAM */
      if (d->filename) {
         unlink(d->filename);
         free(d->filename);
      }

      /* Free the structure itself */
      free(d);
   }
}

/* Initialize an SRAM device */
int dev_c7200_sram_init(vm_instance_t *vm,char *name,
                        m_uint64_t paddr,m_uint32_t len,
                        struct pci_bus *pci_bus,int pci_device)
{
   m_uint64_t alias_paddr;
   struct sram_data *d;

   /* Allocate the private data structure for SRAM */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"dev_c7200_sram_init (%s): out of memory\n",name);
      return(-1);
   }

   memset(d,0,sizeof(*d));

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_c7200_sram_shutdown;

   if (!(d->filename = vm_build_filename(vm,name)))
      return(-1);

   /* add as a pci device */
   d->pci_dev = pci_dev_add_basic(pci_bus,name,
                                  PCI_VENDOR_SRAM,PCI_PRODUCT_SRAM,
                                  pci_device,0);

   alias_paddr = 0x100000000ULL + paddr;
   
   /* create the standard RAM zone */
   if (!(d->dev = dev_create_ram(vm,name,d->filename,paddr,len))) {
      fprintf(stderr,"dev_c7200_sram_init: unable to create '%s' file.\n",
              d->filename);
      return(-1);
   }

   /* create the RAM alias */
   if (!(d->alias_dev_name = dyn_sprintf("%s_alias",name))) {
      fprintf(stderr,"dev_c7200_sram_init: unable to create alias name.\n");
      return(-1);
   }

   d->alias_dev = dev_create_ram_alias(vm,d->alias_dev_name,name,
                                       alias_paddr,len);

   if (!d->alias_dev) {
      fprintf(stderr,"dev_c7200_sram_init: unable to create alias device.\n");
      return(-1);
   }

   /* create the byte-swapped zone (used with Galileo DMA) */
   if (!(d->bs_dev_name = dyn_sprintf("%s_bswap",name))) {
      fprintf(stderr,"dev_c7200_sram_init: unable to create BS name.\n");
      return(-1);
   }

   if (!(d->bs_dev = dev_create(d->bs_dev_name))) {
      fprintf(stderr,"dev_c7200_sram_init: unable to create BS device.\n");
      return(-1);
   }

   d->bs_dev->phys_addr = paddr + 0x800000;
   d->bs_dev->phys_len  = len;
   d->bs_dev->handler   = dev_sram_bs_access;
   d->bs_dev->host_addr = (m_iptr_t)d->dev->host_addr;
   d->bs_dev->flags     = VDEVICE_FLAG_NO_MTS_MMAP|VDEVICE_FLAG_CACHING;
   d->bs_dev->flags    |= VDEVICE_FLAG_REMAP;
   vm_bind_device(vm,d->bs_dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}    
