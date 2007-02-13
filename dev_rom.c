/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 *
 * ROM Emulation.
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

/* Embedded MIPS64 ROM */
m_uint8_t mips64_microcode[] = {
#include "mips64_microcode_dump.inc"
};

ssize_t mips64_microcode_len = sizeof(mips64_microcode);

/* Embedded PPC32 ROM */
m_uint8_t ppc32_microcode[] = {
#include "ppc32_microcode_dump.inc"
};

ssize_t ppc32_microcode_len = sizeof(ppc32_microcode);

/* ROM private data */
struct rom_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   m_uint8_t *rom_ptr;
   m_uint32_t rom_size;
};

/*
 * dev_rom_access()
 */
void *dev_rom_access(cpu_gen_t *cpu,struct vdevice *dev,
                     m_uint32_t offset,u_int op_size,u_int op_type,
                     m_uint64_t *data)
{
   struct rom_data *d = dev->priv_data;

   if (op_type == MTS_WRITE) {
      cpu_log(cpu,"ROM","write attempt at address 0x%llx (data=0x%llx)\n",
              dev->phys_addr+offset,*data);
      return NULL;
   }

   if (offset >= d->rom_size) {
      *data = 0;
      return NULL;
   }

   return((void *)(d->rom_ptr + offset));
}

/* Shutdown a ROM device */
void dev_rom_shutdown(vm_instance_t *vm,struct rom_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/* Initialize a ROM zone */
int dev_rom_init(vm_instance_t *vm,char *name,m_uint64_t paddr,m_uint32_t len,
                 m_uint8_t *rom_data,ssize_t rom_data_size)
{
   struct rom_data *d;

   /* allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"ROM: unable to create device.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->rom_ptr  = rom_data;
   d->rom_size = rom_data_size;

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_rom_shutdown;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.flags     = VDEVICE_FLAG_CACHING;
   d->dev.handler   = dev_rom_access;

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
