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

/* Map a memory zone from a file */
u_char *memzone_map_file(int fd,size_t len)
{
   return(mmap(NULL,len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(off_t)0));
}

/* Create a file to serve as a memory zone */
int memzone_create_file(char *filename,size_t len,u_char **ptr)
{
   int fd;

   if ((fd = open(filename,O_CREAT|O_RDWR,S_IRWXU)) == -1) {
      perror("memzone_create_file: open");
      return(-1);
   }

   if (ftruncate(fd,len) == -1) {
      perror("memzone_create_file: ftruncate");
      return(-1);
   }
   
   *ptr = memzone_map_file(fd,len);

   if (!*ptr) {
      close(fd);
      fd = -1;
   }

   return(fd);
}

/* Get device by ID */
struct vdevice *dev_get_by_id(cpu_mips_t *cpu,u_int dev_id)
{
   if (!cpu || (dev_id >= MIPS64_DEVICE_MAX))
      return NULL;

   return(cpu->dev_array[dev_id]);
}

/* Get device by name */
struct vdevice *dev_get_by_name(cpu_mips_t *cpu,char *name,u_int *dev_id)
{
   u_int i;

   if (!cpu)
      return NULL;
   
   for(i=0;i<MIPS64_DEVICE_MAX;i++) {
      if (!cpu->dev_array[i])
         continue;

      if (!strcmp(cpu->dev_array[i]->name,name)) {
         if (dev_id) *dev_id = i;
         return(cpu->dev_array[i]);
      }
   }

   return NULL;
}

/* Device lookup by physical address */
struct vdevice *dev_lookup(cpu_mips_t *cpu,m_uint64_t paddr,u_int *dev_id)
{
   struct vdevice *dev;
   u_int i;
   
   for(i=0;i<MIPS64_DEVICE_MAX;i++) {
      if (!(dev = cpu->dev_array[i]))
         continue;

      if ((paddr >= dev->phys_addr) && 
          (paddr < (dev->phys_addr + dev->phys_len)))
      {
         if (dev_id) *dev_id = i;
         return dev;
      }
   }

   return NULL;
}

/* Allocate a device and return its identifier */
struct vdevice *dev_create(char *name)
{
   struct vdevice *dev;

   if (!(dev = malloc(sizeof(*dev)))) {
      fprintf(stderr,"dev_create: insufficient memory to "
              "create device '%s'.\n",name);
      return NULL;
   }

   memset(dev,0,sizeof(*dev));
   dev->name = name;
   return dev;
}

/* Show properties of a device */
void dev_show(struct vdevice *dev)
{
   if (!dev)
      return;

   printf("   %-15s: 0x%12.12llx (0x%8.8x)\n",
          dev->name,dev->phys_addr,dev->phys_len);
}

/* Show the device list */
void dev_show_list(cpu_mips_t *cpu)
{
   u_int i;
   
   printf("\nCPU%u Device list:\n",cpu->id);

   for(i=0;i<MIPS64_DEVICE_MAX;i++)
      dev_show(cpu->dev_array[i]);

   printf("\n");
}

/* device access function */
void *dev_access(cpu_mips_t *cpu,u_int dev_id,m_uint32_t offset,
                 u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct vdevice *dev = cpu->dev_array[dev_id];
   return(dev->handler(cpu,dev,offset,op_size,op_type,data));
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
   dev->fd        = orig->fd;
   dev->host_addr = orig->host_addr;
   dev->handler   = orig->handler;
   return dev;
}

/* create ram */
int dev_create_ram(cpu_group_t *cpu_group,char *name,char *filename,
                   m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev;
   u_char *ram_ptr;

   if (!(dev = dev_create(name)))
      return(-1);

   dev->phys_addr = paddr;
   dev->phys_len = len;
   dev->fd = memzone_create_file(filename,dev->phys_len,&ram_ptr);
   dev->host_addr = (m_iptr_t)ram_ptr;
   
   cpu_group_bind_device(cpu_group,dev);
   return(0);
}

/* create a memory alias */
int dev_create_ram_alias(cpu_group_t *cpu_group,char *name,char *orig_name,
                         m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev,*orig;

   /* try to locate the device on the first CPU of this group */
   if (!(orig = dev_get_by_name(cpu_group->cpu_list,orig_name,NULL))) {
      fprintf(stderr,"CPU Group %s: dev_create_ram_alias: "
              "unknown device '%s'.\n",cpu_group->name,orig_name);
      return(-1);
   }

   if (orig->fd == -1) {
      fprintf(stderr,"CPU Group %s: dev_create_ram_alias: "
              "device %s has no FD.\n",cpu_group->name,orig->name);
      return(-1);
   }

   if (!(dev = dev_remap(name,orig,paddr,len))) {
      fprintf(stderr,"CPU Group %s: dev_create_ram_alias: "
              "unable to create new device %s.\n",cpu_group->name,name);
      return(-1);
   }

   cpu_group_bind_device(cpu_group,dev);
   return(0);
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

/* create a dummy console */
int dev_create_dummy_console(cpu_group_t *cpu_group)
{
   struct vdevice *dev;

   if (!(dev = dev_create("dummy_console")))
      return(-1);

   dev->phys_addr = 0x1e840000; /* 0x1f000000; */
   dev->phys_len  = 4096;
   dev->handler = dummy_console_handler;

   cpu_group_bind_device(cpu_group,dev);
   return(0);
}

/*
 * dev_remote_control_access()
 */
void *dev_remote_control_access(cpu_mips_t *cpu,struct vdevice *dev,
                                m_uint32_t offset,u_int op_size,u_int op_type,
                                m_uint64_t *data)
{
   static char buffer[512];
   static u_int buf_pos = 0;
   size_t len;

   if (op_type == MTS_READ)
      *data = 0;

   switch(offset) {
      /* IOSEMU ID */
      case 0x000: 
         if (op_type == MTS_READ)
            *data = IOSEMU_ID;
         break;

      /* CPU ID */
      case 0x004: 
         if (op_type == MTS_READ)
            *data = 0;   /* XXX */
         break;

      /* Display CPU registers */
      case 0x008:
         if (op_type == MTS_WRITE)
            mips64_dump_regs(cpu);
         break;

      /* Display CPU TLB */
      case 0x00c:
         if (op_type == MTS_WRITE)
            tlb_dump(cpu);
         break;

      /* Instruction trace */
      case 0x010:
         if (op_type == MTS_WRITE)
            insn_itrace = *data;
         else
            *data = insn_itrace;
         break;

      /* RAM size */
      case 0x014: 
         if (op_type == MTS_READ)
            *data = ram_size;
         break;

      /* ROM size */
      case 0x018: 
         if (op_type == MTS_READ)
            *data = rom_size;
         break;

      /* NVRAM size */
      case 0x01c: 
         if (op_type == MTS_READ)
            *data = nvram_size;
         break;             

      /* ELF entry point */
      case 0x020: 
         if (op_type == MTS_READ)
            *data = ios_entry_point;
         break;      

      /* Config Register */
      case 0x024:
         if (op_type == MTS_READ)
            *data = conf_reg;
         break;

      /* Debugging/Log message */
      case 0x028:
         if (op_type == MTS_WRITE) {
            len = physmem_strlen(cpu,*data);
            if (len < sizeof(buffer)) {
               physmem_copy_from_vm(cpu,buffer,*data,len+1);
               m_log("ROM",buffer);
            }
         }
         break;

      /* Buffering */
      case 0x02c:
         if (buf_pos < (sizeof(buffer)-1)) {
            buffer[buf_pos++] = *data & 0xFF;
            buffer[buf_pos] = 0;

            if (buffer[buf_pos-1] == '\n') {
               m_log("ROM",buffer);
               buf_pos = 0;
            }
         } else
            buf_pos = 0;
         break;
   }

   return NULL;
}

/* remote control device */
int dev_create_remote_control(cpu_group_t *cpu_group,
                              m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev;

   if (!(dev = dev_create("remote_ctrl"))) {
      fprintf(stderr,"remote_ctrl: unable to create device.\n");
      return(-1);
   }

   dev->phys_addr = paddr;
   dev->phys_len  = len;
   dev->handler   = dev_remote_control_access;
   
   cpu_group_bind_device(cpu_group,dev);
   return(0);
}
