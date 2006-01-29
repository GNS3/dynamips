/*
 * Cisco C7200 (Predator) NVRAM with Calendar Module.
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

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_c7200.h"

#define DEBUG_ACCESS  0

/* SmartWatch pattern (p.5 of documentation) */
#define PATTERN 0x5ca33ac55ca33ac5ULL

/* NVRAM private data */
struct nvram_data {
   u_int cal_state;
   m_uint64_t cal_read,cal_write;
};

/* Convert an 8-bit number to a BCD form */
static m_uint8_t u8_to_bcd(m_uint8_t val)
{
   return(((val / 10) << 4) + (val % 10));
}

/* Get the current time (p.8) */
static m_uint64_t get_current_time(void)
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
void *dev_nvram_access(cpu_mips_t *cpu,struct vdevice *dev,
                       m_uint32_t offset,u_int op_size,u_int op_type,
                       m_uint64_t *data)
{
   struct nvram_data *d = dev->priv_data;

#if DEBUG_ACCESS
   if (op_type == MTS_READ)
      m_log(dev->name,"read  access to offset = 0x%x, pc = 0x%llx\n",
            offset,cpu->pc);
   else
      m_log(dev->name,"write access to vaddr = 0x%x, pc = 0x%llx, "
            "val = 0x%llx\n",offset,cpu->pc,*data);
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
               m_log("calendar","reset\n");
               d->cal_read = get_current_time();
            } else if(d->cal_state > 0 && d->cal_state++ == 64) {
               /* untested */
               m_log("calendar","set %016llx\n",d->cal_write);
               d->cal_state = 0;
            }
         }
         return NULL;
   }

   return((void *)(dev->host_addr + offset));
}

/* Set appropriately the config register if the NVRAM is empty */
static int set_config_register(struct vdevice *dev)
{
   m_uint32_t *ptr;
   int i;

   ptr = (m_uint32_t *)dev->host_addr;
   for(i=0;i<(dev->phys_len/4);i++,ptr++)
      if (*ptr) return(0);

   /* 
    * nvram is empty: tells IOS to ignore its contents.
    * http://www.cisco.com/en/US/products/hw/routers/ps274/products_installation_guide_chapter09186a008007de4c.html
    */
   conf_reg |= 0x0040;
   printf("NVRAM is empty, setting config register to 0x%x\n",conf_reg);
   return(0);
}

/* Export configuration from NVRAM */
int dev_nvram_export_config(char *nvram_filename,char *cfg_filename)
{
   m_uint32_t tag,start,end,len,clen,nvlen;
   FILE *nvram_fd,*cfg_fd;
   char buffer[512];
   int res = -1;

   if (!(nvram_fd = fopen(nvram_filename,"r"))) {
      fprintf(stderr,"Unable to open NVRAM file '%s'!\n",nvram_filename);
      return(-1);
   }

   if (!(cfg_fd = fopen(cfg_filename,"w"))) {
      fprintf(stderr,"Unable to create config file '%s'!\n",cfg_filename);
      return(-1);
   }
 
   fseek(nvram_fd,C7200_NVRAM_ROM_RES_SIZE+6,SEEK_SET);
   fread(&tag,sizeof(tag),1,nvram_fd);
   if (ntohl(tag) != 0xF0A5ABCD) {
      fprintf(stderr,"NVRAM: Unable to find IOS tag (tag=0x%8.8x)!\n",
              ntohl(tag));
      goto done;
   }

   fseek(nvram_fd,0x06,SEEK_CUR);
   fread(&start,sizeof(start),1,nvram_fd);
   fread(&end,sizeof(end),1,nvram_fd);
   fread(&nvlen,sizeof(nvlen),1,nvram_fd);
   start = htonl(start) + 1;
   end   = htonl(end);
   nvlen = htonl(nvlen);

   if ((start <= C7200_NVRAM_ADDR) || (end <= C7200_NVRAM_ADDR) || 
       (end <= start)) 
   {
      fprintf(stderr,"NVRAM: invalid configuration markers "
              "(start=0x%x,end=0x%x).\n",start,end);
      goto done;
   }
   
   clen = len = end - start;
   if ((clen + 1) != nvlen) {
      fprintf(stderr,"NVRAM: invalid configuration size (0x%x)\n",nvlen);
      goto done;
   }

   start -= C7200_NVRAM_ADDR;
   fseek(nvram_fd,start,SEEK_SET);

   while(len > 0) {
      if (len > sizeof(buffer))
         clen = sizeof(buffer);
      else
         clen = len;

      fread(buffer,clen,1,nvram_fd);
      fwrite(buffer,clen,1,cfg_fd);
      len -= clen;
   }

   res = 0;
 done:    
   fclose(nvram_fd);
   fclose(cfg_fd);
   return(res);
}

/* Directly extract the configuration from the NVRAM device */
int dev_nvram_extract_config(cpu_group_t *cpu_group,char *cfg_filename)
{   
   m_uint32_t tag,start,end,len,clen,nvlen;
   m_uint64_t addr;
   cpu_mips_t *cpu0;
   char buffer[512];
   FILE *fd;

   if (!(cpu0 = cpu_group_find_id(cpu_group,0)))
      return(-1);

   addr = C7200_NVRAM_ADDR + C7200_NVRAM_ROM_RES_SIZE;

   if ((tag = physmem_copy_u32_from_vm(cpu0,addr+0x06)) != 0xF0A5ABCD) {
      fprintf(stderr,"NVRAM: unable to find IOS tag (read 0x%8.8x)!\n",tag);
      return(-1);
   }

   start = physmem_copy_u32_from_vm(cpu0,addr+0x10) + 1;
   end   = physmem_copy_u32_from_vm(cpu0,addr+0x14);
   nvlen = physmem_copy_u32_from_vm(cpu0,addr+0x18);
   clen  = len = end - start;

   if ((clen + 1) != nvlen) {
      fprintf(stderr,"NVRAM: invalid configuration size (0x%x)\n",nvlen);
      return(-1);
   }

   if ((start <= C7200_NVRAM_ADDR) || (end <= C7200_NVRAM_ADDR) || 
       (end <= start)) 
   {
      fprintf(stderr,"NVRAM: invalid configuration markers "
              "(start=0x%x,end=0x%x)\n",start,end);
      return(-1);
   }

   if (!(fd = fopen(cfg_filename,"w"))) {
      fprintf(stderr,"NVRAM: Unable to create config file '%s'!\n",
              cfg_filename);
      return(-1);
   }
    
   addr = start;
   while(len > 0) {
      if (len > sizeof(buffer))
         clen = sizeof(buffer);
      else
         clen = len;

      physmem_copy_from_vm(cpu0,buffer,addr,clen);
      fwrite(buffer,clen,1,fd);
      len -= clen;
      addr += clen;
   }
   
   fclose(fd);
   return(0);
}

/* Directly push the IOS configuration to the NVRAM device */
int dev_nvram_push_config(cpu_group_t *cpu_group,char *cfg_filename)
{   
   m_uint64_t addr,cfg_addr,cfg_start_addr;
   cpu_mips_t *cpu0;
   char buffer[512];
   size_t len;
   FILE *fd;

   if (!(cpu0 = cpu_group_find_id(cpu_group,0)))
      return(-1);

   /* Open IOS user configuration file */
   if (!(fd = fopen(cfg_filename,"r"))) {
      fprintf(stderr,"NVRAM: Unable to open config file '%s'!\n",
              cfg_filename);
      return(-1);
   }

   addr = C7200_NVRAM_ADDR + C7200_NVRAM_ROM_RES_SIZE;
   cfg_start_addr = cfg_addr = addr + 0x40;

   /* Write IOS tag, uncompressed config... */
   physmem_copy_u32_to_vm(cpu0,addr+0x06,0xF0A5ABCD);
   physmem_copy_u32_to_vm(cpu0,addr+0x0a,0x00010000);

   /* Store file contents to NVRAM */
   while(!feof(fd)) {
      len = fread(buffer,1,sizeof(buffer),fd);
      if (len == 0)
         break;

      physmem_copy_to_vm(cpu0,buffer,cfg_addr,len);
      cfg_addr += len;
   }

   fclose(fd);

   /* Write config addresses + size */
   physmem_copy_u32_to_vm(cpu0,addr+0x10,cfg_start_addr);
   physmem_copy_u32_to_vm(cpu0,addr+0x14,cfg_addr);
   physmem_copy_u32_to_vm(cpu0,addr+0x18,cfg_addr - cfg_start_addr);
   return(0);
}

/* dev_nvram_init() */
int dev_nvram_init(cpu_group_t *cpu_group,m_uint64_t paddr,char *filename)
{
   struct vdevice *dev;
   struct nvram_data *d;
   u_char *ptr;

   if (!(dev = dev_create("nvram"))) {
      fprintf(stderr,"NVRAM: unable to create device.\n");
      return(-1);
   }

   /* allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"NVRAM: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));

   dev->phys_addr = paddr;
   dev->phys_len  = nvram_size * 1024;
   dev->handler   = dev_nvram_access;
   dev->fd        = memzone_create_file(filename,dev->phys_len,&ptr);
   dev->host_addr = (m_iptr_t)ptr;
   dev->flags     = VDEVICE_FLAG_NO_MTS_MMAP;
   dev->priv_data = d;

   if (dev->fd == -1) {
      fprintf(stderr,"NVRAM: unable to map file '%s'\n",filename);
      return(-1);
   }

   /* Modify the configuration register if NVRAM is empty */
   set_config_register(dev);

   /* Map this device to all CPU */
   cpu_group_bind_device(cpu_group,dev);
   return(0);
}
