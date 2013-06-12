/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Byte-swapping device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

struct bswap_data {
   /* VM object info */
   vm_obj_t vm_obj;
   
   /* VM instance */
   vm_instance_t *vm;

   /* Byte-swap device */
   struct vdevice dev;

   /* Physical address base for rewrite */
   m_uint64_t phys_base;
};

/*
 * Byte swapped access.
 */
static void *dev_bswap_access(cpu_gen_t *cpu,struct vdevice *dev,
                              m_uint32_t offset,u_int op_size,u_int op_type,
                              m_uint64_t *data)
{
   struct bswap_data *d = dev->priv_data;
   m_uint64_t paddr;

   paddr = d->phys_base + offset;

   switch(op_size) {
      case 1:
         if (op_type == MTS_READ)
            *data = physmem_copy_u8_from_vm(d->vm,paddr ^ 0x03);
         else
            physmem_copy_u8_to_vm(d->vm,paddr ^ 0x03,*data);
         break;

      case 2:
         if (op_type == MTS_READ)
            *data = swap16(physmem_copy_u16_from_vm(d->vm,paddr ^ 0x02));
         else
            physmem_copy_u16_to_vm(d->vm,paddr ^ 0x02,swap16(*data));
         break;

      case 4:
         if (op_type == MTS_READ)
            *data = swap32(physmem_copy_u32_from_vm(d->vm,paddr));
         else
            physmem_copy_u32_to_vm(d->vm,paddr,swap32(*data));
         break;
   }

   return NULL;
}

/* Shutdown an byte-swap device */
void dev_bswap_shutdown(vm_instance_t *vm,struct bswap_data *d)
{
   if (d != NULL) {
      /* Remove the alias, the byte-swapped and the main device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/* Initialized a byte-swap device */
int dev_bswap_init(vm_instance_t *vm,char *name,
                   m_uint64_t paddr,m_uint32_t len,
                   m_uint64_t remap_addr)
{
   struct bswap_data *d;

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"BSWAP: unable to create device.\n");
      return(-1);
   }

   vm_object_init(&d->vm_obj);
   d->vm = vm;
   d->phys_base = remap_addr;
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_bswap_shutdown;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_bswap_access;
   d->dev.priv_data = d;

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
