/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 * 
 * RAM emulation.
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

/* RAM private data */
struct ram_data {
   vm_obj_t vm_obj;
   struct vdevice *dev;
   char *filename;
   int delete_file;
};

/* Shutdown a RAM device */
void dev_ram_shutdown(vm_instance_t *vm,struct ram_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,d->dev);
      free(d->dev);

      /* Remove filename used to virtualize RAM */
      if (d->filename && d->delete_file) {
         unlink(d->filename);
         free(d->filename);
      }

      /* Free the structure itself */
      free(d);
   }
}

/* Initialize a RAM zone */
int dev_ram_init(vm_instance_t *vm,char *name,int use_mmap,int delete_file,
                 char *alternate_name,int sparse,
                 m_uint64_t paddr,m_uint32_t len)
{
   struct ram_data *d;

   /* allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"RAM: unable to create device.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->delete_file = delete_file;

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_ram_shutdown;

   if (use_mmap) {
      if (!alternate_name)
         d->filename = vm_build_filename(vm,name);
      else
         d->filename = strdup(alternate_name);

      if (!d->filename) {
         fprintf(stderr,"RAM: unable to create filename.\n");
         goto err_filename;
      }
   }

   if (!(d->dev = dev_create_ram(vm,name,sparse,d->filename,paddr,len))) {
      fprintf(stderr,"RAM: unable to create device.\n");
      goto err_dev_create;
   }

   vm_object_add(vm,&d->vm_obj);
   return(0);

 err_dev_create:
   free(d->filename);
 err_filename:
   free(d);
   return(-1);
}

/* Initialize a ghosted RAM zone */
int dev_ram_ghost_init(vm_instance_t *vm,char *name,int sparse,char *filename,
                       m_uint64_t paddr,m_uint32_t len)
{
   struct ram_data *d;

   /* allocate the private data structure */
   if (!filename || !(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"RAM_ghost: unable to create device.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->delete_file = FALSE;

   if (!(d->filename = strdup(filename)))
      goto err_filename;

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_ram_shutdown;

   if (!(d->dev = dev_create_ghost_ram(vm,name,sparse,d->filename,
                                       paddr,len))) 
   {
      fprintf(stderr,"RAM_ghost: unable to create device.\n");
      goto err_dev_create;
   }

   vm_object_add(vm,&d->vm_obj);
   return(0);

 err_dev_create:
   free(d->filename);
 err_filename:
   free(d);
   return(-1);
}
