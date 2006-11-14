/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "mips64.h"
#include "cpu.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "cp0.h"

#define DEBUG_DEV_ACCESS  0

/* Get device by ID */
struct vdevice *dev_get_by_id(vm_instance_t *vm,u_int dev_id)
{
   if (!vm || (dev_id >= MIPS64_DEVICE_MAX))
      return NULL;

   return(vm->dev_array[dev_id]);
}

/* Get device by name */
struct vdevice *dev_get_by_name(vm_instance_t *vm,char *name)
{
   struct vdevice *dev;

   if (!vm)
      return NULL;

   for(dev=vm->dev_list;dev;dev=dev->next)
      if (!strcmp(dev->name,name))
         return dev;

   return NULL;
}

/* Device lookup by physical address */
struct vdevice *dev_lookup(vm_instance_t *vm,m_uint64_t phys_addr,int cached)
{
   struct vdevice *dev;
   
   if (!vm)
      return NULL;

   for(dev=vm->dev_list;dev;dev=dev->next) {
      if (cached && !(dev->flags & VDEVICE_FLAG_CACHING))
         continue;

      if ((phys_addr >= dev->phys_addr) && 
          ((phys_addr - dev->phys_addr) < dev->phys_len))
         return dev;
   }

   return NULL;
}

/* Find the next device after the specified address */
struct vdevice *dev_lookup_next(vm_instance_t *vm,m_uint64_t phys_addr,
                                struct vdevice *dev_start,int cached)
{
   struct vdevice *dev;
   
   if (!vm)
      return NULL;

   dev = (dev_start != NULL) ? dev_start : vm->dev_list;
   for(;dev;dev=dev->next) {
      if (cached && !(dev->flags & VDEVICE_FLAG_CACHING))
         continue;

      if (dev->phys_addr > phys_addr)
         return dev;
   }

   return NULL;
}

/* Initialize a device */
void dev_init(struct vdevice *dev)
{
   memset(dev,0,sizeof(*dev));
   dev->fd = -1;
}

/* Allocate a device */
struct vdevice *dev_create(char *name)
{
   struct vdevice *dev;

   if (!(dev = malloc(sizeof(*dev)))) {
      fprintf(stderr,"dev_create: insufficient memory to "
              "create device '%s'.\n",name);
      return NULL;
   }
   
   dev_init(dev);
   dev->name = name;
   return dev;
}

/* Remove a device */
void dev_remove(vm_instance_t *vm,struct vdevice *dev)
{
   if (dev != NULL) {
      vm_unbind_device(vm,dev);
      
      vm_log(vm,"DEVICE",
             "Removal of device %s, fd=%d, host_addr=0x%llx, flags=%d\n",
             dev->name,dev->fd,(m_uint64_t)dev->host_addr,dev->flags);

      if (dev->fd != -1) {
         /* Unmap memory mapped file */
         if (dev->host_addr && !(dev->flags & VDEVICE_FLAG_REMAP)) {
            if (dev->flags & VDEVICE_FLAG_SYNC) {
               msync((void *)dev->host_addr,dev->phys_len,
                     MS_SYNC|MS_INVALIDATE);
            }

            vm_log(vm,"MMAP","unmapping of device '%s', "
                   "fd=%d, host_addr=0x%llx, len=0x%x\n",
                   dev->name,dev->fd,(m_uint64_t)dev->host_addr,dev->phys_len);
            munmap((void *)dev->host_addr,dev->phys_len);
         }

         if (dev->flags & VDEVICE_FLAG_SYNC)
            fsync(dev->fd);

         close(dev->fd);
      } else {
         /* Use of malloc'ed host memory: free it */
         if (dev->host_addr && !(dev->flags & VDEVICE_FLAG_REMAP))
            free((void *)dev->host_addr);
      }

      /* reinitialize the device to a clean state */
      dev_init(dev);
   }
}

/* Show properties of a device */
void dev_show(struct vdevice *dev)
{
   if (!dev)
      return;

   printf("   %-18s: 0x%12.12llx (0x%8.8x)\n",
          dev->name,dev->phys_addr,dev->phys_len);
}

/* Show the device list */
void dev_show_list(vm_instance_t *vm)
{
   struct vdevice *dev;
   
   printf("\nVM \"%s\" (%u) Device list:\n",vm->name,vm->instance_id);

   for(dev=vm->dev_list;dev;dev=dev->next)
      dev_show(dev);

   printf("\n");
}

/* device access function */
void *dev_access(cpu_mips_t *cpu,u_int dev_id,m_uint32_t offset,
                 u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct vdevice *dev = cpu->vm->dev_array[dev_id];

#if DEBUG_DEV_ACCESS
   cpu_log(cpu,"DEV_ACCESS","%s: dev_id=%u, offset=0x%8.8x, op_size=%u, "
         "op_type=%u, data=%p\n",dev->name,dev_id,offset,op_size,op_type,data);
#endif

   return(dev->handler(cpu,dev,offset,op_size,op_type,data));
}

/* Synchronize memory for a memory-mapped (mmap) device */
int dev_sync(struct vdevice *dev)
{
   if (!dev || !dev->host_addr)
      return(-1);

   return(msync((void *)dev->host_addr,dev->phys_len,MS_SYNC));
}

/* Remap a device at specified physical address */
struct vdevice *dev_remap(char *name,struct vdevice *orig,
                          m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev;

   if (!(dev = dev_create(name)))
      return NULL;

   dev->phys_addr = paddr;
   dev->phys_len  = len;
   dev->flags     = orig->flags | VDEVICE_FLAG_REMAP;
   dev->fd        = orig->fd;
   dev->host_addr = orig->host_addr;
   dev->handler   = orig->handler;
   return dev;
}

/* Create a RAM device */
struct vdevice *dev_create_ram(vm_instance_t *vm,char *name,char *filename,
                               m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev;
   u_char *ram_ptr;

   if (!(dev = dev_create(name)))
      return NULL;

   dev->phys_addr = paddr;
   dev->phys_len = len;
   dev->flags = VDEVICE_FLAG_CACHING;

   if (filename) {
      dev->fd = memzone_create_file(filename,dev->phys_len,&ram_ptr);

      if (dev->fd == -1) {
         perror("dev_create_ram: mmap");
         free(dev);
         return NULL;
      }
      
      dev->host_addr = (m_iptr_t)ram_ptr;
   } else {
      dev->host_addr = (m_iptr_t)m_memalign(4096,dev->phys_len);
   }
   
   if (!dev->host_addr) {
      free(dev);
      return NULL;
   }

   vm_bind_device(vm,dev);
   return dev;
}

/* Create a ghosted RAM device */
struct vdevice *
dev_create_ghost_ram(vm_instance_t *vm,char *name,char *filename,
                     m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev;
   u_char *ram_ptr;

   if (!(dev = dev_create(name)))
      return NULL;

   dev->phys_addr = paddr;
   dev->phys_len = len;
   dev->flags = VDEVICE_FLAG_CACHING;

   dev->fd = memzone_open_cow_file(filename,dev->phys_len,&ram_ptr);
   if (dev->fd == -1) {
      perror("dev_create_ghost_ram: mmap");
      free(dev);
      return NULL;
   }

   if (!(dev->host_addr = (m_iptr_t)ram_ptr)) {
      free(dev);
      return NULL;
   }

   vm_bind_device(vm,dev);
   return dev;
}

/* Create a memory alias */
struct vdevice *dev_create_ram_alias(vm_instance_t *vm,char *name,char *orig,
                                     m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev,*orig_dev;

   /* try to locate the device */
   if (!(orig_dev = dev_get_by_name(vm,orig))) {
      fprintf(stderr,"VM%u: dev_create_ram_alias: unknown device '%s'.\n",
              vm->instance_id,orig);
      return NULL;
   }

   if (orig_dev->fd == -1) {
      fprintf(stderr,"VM%u: dev_create_ram_alias: device %s has no FD.\n",
              vm->instance_id,orig_dev->name);
      return NULL;
   }

   if (!(dev = dev_remap(name,orig_dev,paddr,len))) {
      fprintf(stderr,"VM%u: dev_create_ram_alias: unable to create "
              "new device %s.\n",vm->instance_id,name);
      return NULL;
   }

   vm_bind_device(vm,dev);
   return dev;
}

/* dummy console handler */
static void *dummy_console_handler(cpu_mips_t *cpu,struct vdevice *dev,
                                   m_uint32_t offset,u_int op_size,
                                   u_int op_type,m_uint64_t *data)
{
   switch(offset) {
      case 0x40c:
         if (op_type == MTS_READ)
            *data = 0x04;  /* tx ready */
         break;

      case 0x41c:
         if (op_type == MTS_WRITE) {
            printf("%c",(u_char)(*data & 0xff));
            fflush(stdout);
         }
         break;
   }

   return NULL;
}

/* Create a dummy console */
int dev_create_dummy_console(vm_instance_t *vm)
{
   struct vdevice *dev;

   if (!(dev = dev_create("dummy_console")))
      return(-1);

   dev->phys_addr = 0x1e840000; /* 0x1f000000; */
   dev->phys_len  = 4096;
   dev->handler = dummy_console_handler;

   vm_bind_device(vm,dev);
   return(0);
}
