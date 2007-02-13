/*
 * Cisco router simulation platform.
 * Copyright (C) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Zeroed memory zone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

/* Zero zone private data */
struct zero_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
};

/*
 * dev_zero_access()
 */
void *dev_zero_access(cpu_gen_t *cpu,struct vdevice *dev,
                      m_uint32_t offset,u_int op_size,u_int op_type,
                      m_uint64_t *data)
{
   if (op_type == MTS_READ)
      *data = 0;

   return NULL;
}

/* Shutdown a zeroed memory zone */
void dev_zero_shutdown(vm_instance_t *vm,struct zero_data *d)
{
   if (d != NULL) {
      dev_remove(vm,&d->dev);
      free(d);
   }
}

/* Initialized a zeroed memory zone */
int dev_zero_init(vm_instance_t *vm,char *name,m_uint64_t paddr,m_uint32_t len)
{
   struct zero_data *d;

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"ZERO: unable to create device.\n");
      return(-1);
   }

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_zero_shutdown;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_zero_access;
   
   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
