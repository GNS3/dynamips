/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 *
 * Intel Flash SIMM emulation (28F008SA/28F016SA)
 *
 * Intelligent ID Codes:
 *   28F008SA: 0x89A2 (1 Mb)
 *   28F016SA: 0x89A0 (2 Mb)
 *
 * Manuals:
 *    http://www.ortodoxism.ro/datasheets/Intel/mXvsysv.pdf
 *
 * This code is working but is far from perfect. The four assembled circuits
 * should be managed independently.
 *
 * Here, we emulate a group of four 28F016SA, for a total size of 8 Mb.
 * If you need to change this, the Flash SIMM register must also be changed.
 * (TODO: a CLI option + generic code).
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
#define DEBUG_WRITE   0

/* Bootflash private data */
struct bootflash_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   m_uint32_t cui_cmd,blk_cmd;
   m_uint32_t status;
   char *filename;
};

#define BPTR(d,offset) (((char *)d->dev.host_addr) + offset)

/*
 * dev_bootflash_access()
 */
void *dev_bootflash_access(cpu_gen_t *cpu,struct vdevice *dev,
                           m_uint32_t offset,u_int op_size,u_int op_type,
                           m_uint64_t *data)
{
   struct bootflash_data *d = dev->priv_data;

#if DEBUG_ACCESS
   if (op_type == MTS_READ)
      cpu_log(cpu,dev->name,"read  access to offset = 0x%x, pc = 0x%llx "
              "(stat=%u,cui_cmd=0x%x)\n",
              offset,cpu_get_pc(cpu),d->status,d->cui_cmd);
   else
      cpu_log(cpu,dev->name,"write access to vaddr = 0x%x, pc = 0x%llx, "
              "val = 0x%llx\n",offset,cpu_get_pc(cpu),*data);
#endif

   if (op_type == MTS_READ) {
      *data = 0;

      /* Read Array mode */
      if (d->status == 0)
         return(BPTR(d,offset));

      switch(d->cui_cmd) {
         /* Intelligent identifier */
         case 0x90909090:
            switch(offset) {
               case 0x00:
                  *data = 0x89898989;   /* manufacturer code */
                  return NULL;
               case 0x04:
                  *data = 0xA0A0A0A0;   /* device code */
                  return NULL;
               default:
                  cpu_log(cpu,dev->name,
                          "Reading Intelligent ID Code at offset = 0x%x ?\n",
                          offset);
                  *data = 0x00000000;
                  return NULL;
            }
            break;
            
         /* Read Status Register */
         case 0x70707070:
            *data = 0x80808080;
            return NULL;
      }
      
      /* Default: status register */
      *data = 0x80808080;
      return NULL;
   }

   /* write mode */
   if (d->blk_cmd == 0x40404040) {
#if DEBUG_WRITE
      cpu_log(cpu,dev->name,"Writing 0x%llx at offset=0x%x\n",*data,offset);
#endif
      d->blk_cmd = 0;
      d->cui_cmd = 0;
      d->status = 1;
      return(BPTR(d,offset));
   }

   switch(*data) {
      /* Erase Setup */
      case 0x20202020:
         d->blk_cmd = *data;
         break;
         
      /* Erase Confirm */
      case 0xd0d0d0d0:
         if ((d->blk_cmd == 0x20202020) && !(offset & 0x3FFFF)) {
            memset(BPTR(d,offset),0xFF,0x40000);
            d->blk_cmd = 0;
            d->cui_cmd = 0;
            d->status = 1;
         }
         break;

      /* Byte Write Setup (XXX ugly hack) */
      case 0x40404040:
      case 0x40ffffff:
      case 0x4040ffff:
      case 0x404040ff:
      case 0xff404040:
      case 0xffff4040:
      case 0xffffff40:
         d->blk_cmd = 0x40404040;
         break;

      /* Reset */
      case 0xffffffff:
         d->status = 0;
         break;

      /* Intelligent Identifier and Read Status register */
      case 0x90909090:
      case 0x70707070:
         d->status = 1;
         d->cui_cmd = *data;
         break;

      default:
         cpu_log(cpu,dev->name,
                 "default write case at offset=0x%7.7x, val=0x%llx\n",
                 offset,*data);
   }

   return NULL;
}

/* Shutdown a bootflash device */
void dev_bootflash_shutdown(vm_instance_t *vm,struct bootflash_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* We don't remove the file, since it used as permanent storage */
      if (d->filename)
         free(d->filename);

      /* Free the structure itself */
      free(d);
   }
}

/* Create a 8 Mb bootflash */
int dev_bootflash_init(vm_instance_t *vm,char *name,
                       m_uint64_t paddr,m_uint32_t len)
{  
   struct bootflash_data *d;
   u_char *ptr;

   /* Allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"Bootflash: unable to create device.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_bootflash_shutdown;

   if (!(d->filename = vm_build_filename(vm,name))) {
      fprintf(stderr,"Bootflash: unable to create filename.\n");
      goto err_filename;
   }

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_bootflash_access;
   d->dev.fd        = memzone_create_file(d->filename,d->dev.phys_len,&ptr);
   d->dev.host_addr = (m_iptr_t)ptr;
   d->dev.flags     = VDEVICE_FLAG_NO_MTS_MMAP;

   if (d->dev.fd == -1) {
      fprintf(stderr,"Bootflash: unable to map file '%s'\n",d->filename);
      goto err_fd_create;
   }

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);

 err_fd_create:
   free(d->filename);
 err_filename:
   free(d);
   return(-1);
}
