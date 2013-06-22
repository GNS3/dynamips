/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Dallas DS1216 chip emulation:
 *   - NVRAM
 *   - Calendar
 *
 * Manuals:
 *    http://pdfserv.maxim-ic.com/en/ds/DS1216-DS1216H.pdf
 *
 * Calendar stuff written by Mtve.
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

/* SmartWatch pattern (p.5 of documentation) */
#define PATTERN 0x5ca33ac55ca33ac5ULL

/* NVRAM private data */
struct nvram_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   char *filename;
   u_int cal_state;
   m_uint64_t cal_read,cal_write;
};

/* Convert an 8-bit number to a BCD form */
static m_uint8_t u8_to_bcd(m_uint8_t val)
{
   return(((val / 10) << 4) + (val % 10));
}

/* Get the current time (p.8) */
static m_uint64_t get_current_time(cpu_gen_t *cpu)
{
   m_uint64_t res;
   struct tm *tmx;
   time_t ct;
   
   time(&ct);
   tmx = localtime(&ct);
   
   res =  u8_to_bcd(tmx->tm_sec)  << 8;
   res += u8_to_bcd(tmx->tm_min)  << 16;
   res += u8_to_bcd(tmx->tm_hour) << 24;
   res += ((m_uint64_t)(u8_to_bcd(tmx->tm_wday))) << 32;
   res += ((m_uint64_t)(u8_to_bcd(tmx->tm_mday))) << 40;
   res += ((m_uint64_t)(u8_to_bcd(tmx->tm_mon+1))) << 48;
   res += ((m_uint64_t)(u8_to_bcd(tmx->tm_year))) << 56;

   return(res);
}

/*
 * dev_nvram_access()
 */
void *dev_nvram_access(cpu_gen_t *cpu,struct vdevice *dev,
                       m_uint32_t offset,u_int op_size,u_int op_type,
                       m_uint64_t *data)
{
   struct nvram_data *d = dev->priv_data;

#if DEBUG_ACCESS
   if (op_type == MTS_READ)
      cpu_log(cpu,dev->name,"read  access to offset=0x%x, pc=0x%llx\n",
              offset,cpu_get_pc(cpu));
   else
      cpu_log(cpu,dev->name,
              "write access to vaddr=0x%x, pc=0x%llx, val=0x%llx\n",
              offset,cpu_get_pc(cpu),*data);
#endif

   switch(offset) {
      case 0x03:
         if (op_type == MTS_READ) {            
            *data = d->cal_read & 1;
            d->cal_read >>= 1;
         } else {
            d->cal_write >>= 1;
            d->cal_write |= *data << 63;

            if (d->cal_write == PATTERN) {
               d->cal_state = 1;
               vm_log(cpu->vm,"Calendar","reset\n");
               d->cal_read = get_current_time(cpu);
            } else if(d->cal_state > 0 && d->cal_state++ == 64) {
               /* untested */
               vm_log(cpu->vm,"Calendar","set 0x%016llx\n",d->cal_write);
               d->cal_state = 0;
            }
         }
         return NULL;
   }

   return((void *)(dev->host_addr + offset));
}

/* Set appropriately the config register if the NVRAM is empty */
static void set_config_register(struct vdevice *dev,u_int *conf_reg)
{
   m_uint32_t *ptr;
   int i;

   ptr = (m_uint32_t *)dev->host_addr;
   for(i=0;i<(dev->phys_len/4);i++,ptr++)
      if (*ptr) return;

   /* 
    * nvram is empty: tells IOS to ignore its contents.
    * http://www.cisco.com/en/US/products/hw/routers/ps274/products_installation_guide_chapter09186a008007de4c.html
    */
   *conf_reg |= 0x0040;
   printf("NVRAM is empty, setting config register to 0x%x\n",*conf_reg);
}

/* Shutdown the NVRAM device */
void dev_nvram_shutdown(vm_instance_t *vm,struct nvram_data *d)
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

/* Create the NVRAM device */
int dev_nvram_init(vm_instance_t *vm,char *name,
                   m_uint64_t paddr,m_uint32_t len,
                   u_int *conf_reg)
{
   struct nvram_data *d;
   u_char *ptr;

   /* allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"NVRAM: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_nvram_shutdown;

   if (!(d->filename = vm_build_filename(vm,name))) {
      fprintf(stderr,"NVRAM: unable to create filename.\n");
      return(-1);
   }

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_nvram_access;
   d->dev.fd        = memzone_create_file(d->filename,d->dev.phys_len,&ptr);
   d->dev.host_addr = (m_iptr_t)ptr;
   d->dev.flags     = VDEVICE_FLAG_NO_MTS_MMAP|VDEVICE_FLAG_SYNC;
   d->dev.priv_data = d;

   if (d->dev.fd == -1) {
      fprintf(stderr,"NVRAM: unable to map file '%s'\n",d->filename);
      return(-1);
   }

   /* Modify the configuration register if NVRAM is empty */
   set_config_register(&d->dev,conf_reg);

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}

/* Compute NVRAM checksum */
m_uint16_t nvram_cksum_old(vm_instance_t *vm,m_uint64_t addr,size_t count) 
{
   m_uint32_t sum = 0;

   while(count > 1) {
      sum = sum + physmem_copy_u16_from_vm(vm,addr);
      addr += sizeof(m_uint16_t);
      count -= sizeof(m_uint16_t);
   }

   if (count > 0) 
      sum = sum + ((physmem_copy_u16_from_vm(vm,addr) & 0xFF) << 8); 

   while(sum>>16)
      sum = (sum & 0xffff) + (sum >> 16);

   return(~sum);
}
