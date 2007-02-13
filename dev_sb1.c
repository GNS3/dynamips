/*
 * Cisco router simulation platform.
 * Copyright (c) 2005 Christophe Fillot (cf@utc.fr)
 *
 * SB-1 system control devices.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <termios.h>
#include <fcntl.h>
#include <pthread.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_c7200.h"

#define DEBUG_UNKNOWN   1

/* SB-1 private data */
struct sb1_data {
   vm_obj_t vm_obj;
   struct vdevice dev;

   /* Virtual machine */
   vm_instance_t *vm;
};

/*
 * dev_sb1_access()
 */
void *dev_sb1_access(cpu_gen_t *cpu,struct vdevice *dev,
                     m_uint32_t offset,u_int op_size,u_int op_type,
                     m_uint64_t *data)
{
   struct sb1_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

   switch(offset) {
      case 0x20000:
         if (op_type == MTS_READ)
            *data = 0x125020FF;
         break;

         /* Seen on a real NPE-G1 :) */
      case 0x20008:
         if (op_type == MTS_READ)
            *data = 0x00800000FCDB0700ULL;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"SB1","read from addr 0x%x, pc=0x%llx\n",
                    offset,cpu_get_pc(cpu));
         } else {
            cpu_log(cpu,"SB1","write to addr 0x%x, value=0x%llx, pc=0x%llx\n",
                    offset,*data,cpu_get_pc(cpu));
         }
#endif
   }

   return NULL;
}

/* Shutdown the SB-1 system control devices */
void dev_sb1_shutdown(vm_instance_t *vm,struct sb1_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}


/* Create SB-1 system control devices */
int dev_sb1_init(vm_instance_t *vm)
{   
   struct sb1_data *d;

   /* allocate private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"SB1: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = "sb1_sysctrl";
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_sb1_shutdown;

   dev_init(&d->dev);
   d->dev.name      = "sb1_sysctrl";
   d->dev.priv_data = d;
   d->dev.phys_addr = 0x10000000ULL;
   d->dev.phys_len  = 0x60000;
   d->dev.handler   = dev_sb1_access;

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);  
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
