/*
 * Cisco router simulation platform.
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
#include <assert.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#define DEBUG_DEV_ACCESS  0

/* Get device by ID */
struct vdevice *dev_get_by_id(vm_instance_t *vm,u_int dev_id)
{
   if (!vm || (dev_id >= VM_DEVICE_MAX))
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
   if (dev == NULL)
      return;

   vm_unbind_device(vm,dev);
      
   vm_log(vm,"DEVICE",
          "Removal of device %s, fd=%d, host_addr=0x%llx, flags=%d\n",
          dev->name,dev->fd,(m_uint64_t)dev->host_addr,dev->flags);

   if (dev->flags & VDEVICE_FLAG_REMAP) {
      dev_init(dev);
      return;
   }

   if (dev->flags & VDEVICE_FLAG_SPARSE) {
      dev_sparse_shutdown(dev);

      if (dev->flags & VDEVICE_FLAG_GHOST) {
         vm_ghost_image_release(dev->fd);
         dev_init(dev);
         return;
      }
   }

   if (dev->fd != -1) {
      /* Unmap memory mapped file */
      if (dev->host_addr) {
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
      if (dev->host_addr)
         free((void *)dev->host_addr);
   }

   /* reinitialize the device to a clean state */
   dev_init(dev);
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
void *dev_access(cpu_gen_t *cpu,u_int dev_id,m_uint32_t offset,
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

   dev->phys_addr  = paddr;
   dev->phys_len   = len;
   dev->flags      = orig->flags | VDEVICE_FLAG_REMAP;
   dev->fd         = orig->fd;
   dev->host_addr  = orig->host_addr;
   dev->handler    = orig->handler;
   dev->sparse_map = orig->sparse_map;
   return dev;
}

/* Create a RAM device */
struct vdevice *dev_create_ram(vm_instance_t *vm,char *name,
                               int sparse,char *filename,
                               m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev;
   u_char *ram_ptr;

   if (!(dev = dev_create(name)))
      return NULL;

   dev->phys_addr = paddr;
   dev->phys_len = len;
   dev->flags = VDEVICE_FLAG_CACHING;

   if (!sparse) {
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
   } else {
      dev_sparse_init(dev);
   }

   vm_bind_device(vm,dev);
   return dev;
}

/* Create a ghosted RAM device */
struct vdevice *
dev_create_ghost_ram(vm_instance_t *vm,char *name,int sparse,char *filename,
                     m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev;
   u_char *ram_ptr;

   if (!(dev = dev_create(name)))
      return NULL;

   dev->phys_addr = paddr;
   dev->phys_len = len;
   dev->flags = VDEVICE_FLAG_CACHING|VDEVICE_FLAG_GHOST;

   if (!sparse) {
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
   } else {
      if (vm_ghost_image_get(filename,&ram_ptr,&dev->fd) == -1) {
         free(dev);
         return NULL;
      }

      dev->host_addr = (m_iptr_t)ram_ptr;
      dev_sparse_init(dev);
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

   if (!(dev = dev_remap(name,orig_dev,paddr,len))) {
      fprintf(stderr,"VM%u: dev_create_ram_alias: unable to create "
              "new device %s.\n",vm->instance_id,name);
      return NULL;
   }

   vm_bind_device(vm,dev);
   return dev;
}

/* Initialize a sparse device */
int dev_sparse_init(struct vdevice *dev)
{
   u_int i,nr_pages;
   size_t len;

   /* create the sparse mapping */
   nr_pages = normalize_size(dev->phys_len,VM_PAGE_SIZE,VM_PAGE_SHIFT);
   len = nr_pages * sizeof(m_iptr_t);

   if (!(dev->sparse_map = malloc(len)))
      return(-1);

   if (!dev->host_addr) {
      memset(dev->sparse_map,0,len);
   } else {
      for(i=0;i<nr_pages;i++)
         dev->sparse_map[i] = dev->host_addr + (i << VM_PAGE_SHIFT);
   }

   dev->flags |= VDEVICE_FLAG_SPARSE;
   return(0);
}

/* Shutdown sparse device structures */
int dev_sparse_shutdown(struct vdevice *dev)
{
   if (!(dev->flags & VDEVICE_FLAG_SPARSE))
      return(-1);

   free(dev->sparse_map);
   dev->sparse_map = NULL;
   return(0);
}

/* Show info about a sparse device */
int dev_sparse_show_info(struct vdevice *dev)
{
   u_int i,nr_pages,dirty_pages;

   printf("Sparse information for device '%s':\n",dev->name);

   if (!(dev->flags & VDEVICE_FLAG_SPARSE)) {
      printf("This is not a sparse device.\n");
      return(-1);
   }

   if (!dev->sparse_map) {
      printf("No sparse map.\n");
      return(-1);
   }

   nr_pages = normalize_size(dev->phys_len,VM_PAGE_SIZE,VM_PAGE_SHIFT);
   dirty_pages = 0;
  
   for(i=0;i<nr_pages;i++)
      if (dev->sparse_map[i] & VDEVICE_PTE_DIRTY)
         dirty_pages++;

   printf("%u dirty pages on a total of %u pages.\n",dirty_pages,nr_pages);
   return(0);
}

/* Get an host address for a sparse device */
m_iptr_t dev_sparse_get_host_addr(vm_instance_t *vm,struct vdevice *dev,
                                  m_uint64_t paddr,u_int op_type,int *cow)
{
   m_iptr_t ptr,ptr_new;
   u_int offset;

   offset = (paddr - dev->phys_addr) >> VM_PAGE_SHIFT;
   ptr = dev->sparse_map[offset];
   *cow = 0;

   /* 
    * If the device is not in COW mode, allocate a host page if the physical
    * page is requested for the first time.
    */
   if (!dev->host_addr) {
      if (!(ptr & VDEVICE_PTE_DIRTY)) {
         ptr = (m_iptr_t)vm_alloc_host_page(vm);
         assert(ptr);

         dev->sparse_map[offset] = ptr | VDEVICE_PTE_DIRTY;
         return(ptr);
      } 

      return(ptr & VM_PAGE_MASK);
   }

   /* 
    * We have a "ghost" base. We apply the copy-on-write (COW) mechanism 
    * ourselves. 
    */
   if (ptr & VDEVICE_PTE_DIRTY)
      return(ptr & VM_PAGE_MASK);

   if (op_type == MTS_READ) {
      *cow = 1;
      return(ptr & VM_PAGE_MASK);
   }

   /* Write attempt on a "ghost" page. Duplicate it */
   ptr_new = (m_iptr_t)vm_alloc_host_page(vm);
   assert(ptr_new);

   memcpy((void *)ptr_new,(void *)(ptr & VM_PAGE_MASK),VM_PAGE_SIZE);
   dev->sparse_map[offset] = ptr_new | VDEVICE_PTE_DIRTY;
   return(ptr_new);
}

/* dummy console handler */
static void *dummy_console_handler(cpu_gen_t *cpu,struct vdevice *dev,
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
