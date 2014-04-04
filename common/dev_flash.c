/*
 * Cisco Simulation Platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 *
 * 23-Oct-2006: only basic code at this time.
 *
 * Considering the access pattern, this might be emulating SST39VF1681/SST39VF1682.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#define DEBUG_ACCESS  0
#define DEBUG_WRITE   0

/* Flash private data */
struct flash_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   m_uint32_t state;
   u_int sector_size;
   char *filename;
};

#define BPTR(d,offset) (((char *)d->dev.host_addr) + offset)

/*
 * dev_bootflash_access()
 */
void *dev_flash_access(cpu_gen_t *cpu,struct vdevice *dev,
                       m_uint32_t offset,u_int op_size,u_int op_type,
                       m_uint64_t *data)
{
   struct flash_data *d = dev->priv_data;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,dev->name,
              "read  access to offset=0x%x, pc=0x%llx (state=%u)\n",
              offset,cpu_get_pc(cpu),d->state);
   } else {
      cpu_log(cpu,dev->name,
              "write access to vaddr=0x%x, pc=0x%llx, val=0x%llx (state=%d)\n",
              offset,cpu_get_pc(cpu),*data,d->state);
   }
#endif

   if (op_type == MTS_READ) {
      switch(d->state) {
         case 0:
            return(BPTR(d,offset));
         default:
            cpu_log(cpu,dev->name,"read: unhandled state %d\n",d->state);
      }

      return NULL;
   }

   /* Write mode */
#if DEBUG_WRITE
   cpu_log(cpu,dev->name,"write to offset 0x%x, data=0x%llx\n",offset,*data);
#endif

   switch(d->state) {
      /* Initial Cycle */
      case 0:
         switch(offset) {
            case 0xAAA:
               if (*data == 0xAA)
                  d->state = 1;
               break;
            default:
               switch(*data) {
                  case 0xB0:
                     /* Erase/Program Suspend */
                     d->state = 0;
                     break;                    
                  case 0x30:
                     /* Erase/Program Resume */
                     d->state = 0;
                     break;
                  case 0xF0:
                     /* Product ID Exit */
                     break;
               }
         }
         break;

      /* Cycle 1 was: 0xAAA, 0xAA */
      case 1:
         if ((offset != 0x555) && (*data != 0x55))
            d->state = 0;
         else
            d->state = 2;
         break;

      /* Cycle 1 was: 0xAAA, 0xAA, Cycle 2 was: 0x555, 0x55 */
      case 2:
         d->state = 0;

         if (offset == 0xAAA) {
            switch(*data) {
               case 0x80:
                  d->state = 3;
                  break;
               case 0xA0: 
                  /* Byte/Word program */
                  d->state = 4;
                  break;
               case 0xF0:
                  /* Product ID Exit */
                  break;
               case 0xC0:
                  /* Program Protection Register / Lock Protection Register */
                  d->state = 5;
                  break;
               case 0x90:
                  /* Product ID Entry / Status of Block B protection */
                  d->state = 6;
                  break;
               case 0xD0:
                  /* Set configuration register */
                  d->state = 7;
                  break;
            }
         }
         break;

     /*
      * Cycle 1 was 0xAAA, 0xAA
      * Cycle 2 was 0x555, 0x55
      * Cycle 3 was 0xAAA, 0x80
      */
      case 3:
         if ((offset != 0xAAA) && (*data != 0xAA))
            d->state = 0;
         else
            d->state = 8;
         break;

     /*
      * Cycle 1 was 0xAAA, 0xAA
      * Cycle 2 was 0x555, 0x55
      * Cycle 3 was 0xAAA, 0x80
      * Cycle 4 was 0xAAA, 0xAA
      */
      case 8:
         if ((offset != 0x555) && (*data != 0x55))
            d->state = 0;
         else
            d->state = 9;
         break;

     /*
      * Cycle 1 was 0xAAA, 0xAA
      * Cycle 2 was 0x555, 0x55
      * Cycle 3 was 0xAAA, 0x80
      * Cycle 4 was 0xAAA, 0xAA
      * Cycle 5 was 0x555, 0x55
      */
      case 9:
         d->state = 0;

         switch(*data) {
            case 0x10:
               /* Chip Erase */
               memset(BPTR(d,offset),0,d->dev.phys_len);
               break;

            case 0x30:
               /* Sector Erase */
               memset(BPTR(d,offset),0,d->sector_size);
               break;

            case 0xA0:
               /* Enter Single Pulse Program Mode */
               break;

            case 0x60:
               /* Sector Lockdown */
               break;
         }
         break;

      /* Byte/Word Program */
      case 4:
         d->state = 0;
         *(m_uint8_t *)(BPTR(d,offset)) = *data;
         break;

      default:
         cpu_log(cpu,dev->name,"write: unhandled state %d\n",d->state);
   }

   return NULL;
}

/* Copy data directly to a flash device */
int dev_flash_copy_data(vm_obj_t *obj,m_uint32_t offset,
                        u_char *ptr,ssize_t len)
{
   struct flash_data *d = obj->data;
   u_char *p;

   if (!d || !d->dev.host_addr)
      return(-1);

   p = (u_char *)d->dev.host_addr + offset;
   memcpy(p,ptr,len);
   return(0);
}

/* Shutdown a flash device */
void dev_flash_shutdown(vm_instance_t *vm,struct flash_data *d)
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

/* Create a Flash device */
vm_obj_t *dev_flash_init(vm_instance_t *vm,char *name,
                         m_uint64_t paddr,m_uint32_t len)
{  
   struct flash_data *d;
   u_char *ptr;

   /* Allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"Flash: unable to create device.\n");
      return NULL;
   }

   memset(d,0,sizeof(*d));
   d->sector_size = 0x4000;

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_flash_shutdown;

   if (!(d->filename = vm_build_filename(vm,name))) {
      fprintf(stderr,"Flash: unable to create filename.\n");
      goto err_filename;
   }

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_flash_access;
   d->dev.fd        = memzone_create_file(d->filename,d->dev.phys_len,&ptr);
   d->dev.host_addr = (m_iptr_t)ptr;
   d->dev.flags     = VDEVICE_FLAG_NO_MTS_MMAP;

   if (d->dev.fd == -1) {
      fprintf(stderr,"Flash: unable to map file '%s'\n",d->filename);
      goto err_fd_create;
   }

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(&d->vm_obj);

 err_fd_create:
   free(d->filename);
 err_filename:
   free(d);
   return NULL;
}
