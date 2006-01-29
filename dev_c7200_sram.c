/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Packet SRAM. This is a fast memory zone for packets on NPE150/NPE200.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#define PCI_VENDOR_SRAM   0x1137
#define PCI_PRODUCT_SRAM  0x0005

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

/* dev_c7200_sram_init() */
int dev_c7200_sram_init(cpu_group_t *cpu_group,char *name,char *filename,
                        m_uint64_t paddr,m_uint32_t len,
                        struct pci_data *pci_data,int pci_bus)
{
   struct vdevice *sram_dev,*bs_dev;
   m_uint64_t alias_paddr;
   char *dname;

   /* add as a pci device */
   pci_dev_add_basic(pci_data,name,
                     PCI_VENDOR_SRAM,PCI_PRODUCT_SRAM,
                     pci_bus,0,0);               

   alias_paddr = 0x100000000ULL + paddr;
   
   /* create the standard RAM zone */
   if (dev_create_ram(cpu_group,name,filename,paddr,len) == -1) {
      fprintf(stderr,"dev_c7200_sram_init: unable to create '%s' file.\n",
              filename);
      return(-1);
   }

   /* create the RAM Alias */
   if (!(dname = dyn_sprintf("%s_alias",name))) {
      fprintf(stderr,"dev_c7200_sram_init: unable to create alias name.\n");
      return(-1);
   }

   if (dev_create_ram_alias(cpu_group,dname,name,alias_paddr,len) == -1) {
      fprintf(stderr,"dev_c7200_sram_init: unable to create alias device.\n");
      return(-1);
   }

   /* create the byte-swapped zone (used with Galileo DMA) */
   if (!(dname = dyn_sprintf("%s_bswap",name))) {
      fprintf(stderr,"dev_c7200_sram_init: unable to create BS name.\n");
      return(-1);
   }

   if (!(bs_dev = dev_create(dname))) {
      fprintf(stderr,"dev_c7200_sram_init: unable to create BS device.\n");
      return(-1);
   }
   
   sram_dev = dev_get_by_name(cpu_group_find_id(cpu_group,0),name,NULL);
   assert(sram_dev);

   bs_dev->phys_addr = paddr + 0x800000;
   bs_dev->phys_len  = len;
   bs_dev->handler   = dev_sram_bs_access;
   bs_dev->fd        = sram_dev->fd;
   bs_dev->host_addr = (m_iptr_t)sram_dev->host_addr;
   bs_dev->flags     = VDEVICE_FLAG_NO_MTS_MMAP;
   cpu_group_bind_device(cpu_group,bs_dev);

   return(0);
}    
