/*
 * Cisco C7200 (Predator) Bootflash.
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

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#define DEBUG_ACCESS  0
#define DEBUG_WRITE   0

/* bootflash private data */
struct bootflash_data {
   u_char *host_addr;
   m_uint32_t cui_cmd,blk_cmd;
   m_uint32_t status;
};

/*
 * dev_bootflash_access()
 */
void *dev_bootflash_access(cpu_mips_t *cpu,struct vdevice *dev,
                           m_uint32_t offset,u_int op_size,u_int op_type,
                           m_uint64_t *data)
{
   struct bootflash_data *d = dev->priv_data;

#if DEBUG_ACCESS
   if (op_type == MTS_READ)
      m_log(dev->name,"read  access to offset = 0x%x, pc = 0x%llx "
            "(stat=%u,cui_cmd=0x%x)\n",
            offset,cpu->pc,d->status,d->cui_cmd);
   else
      m_log(dev->name,"write access to vaddr = 0x%x, pc = 0x%llx, "
            "val = 0x%llx\n",offset,cpu->pc,*data);
#endif

   if (op_type == MTS_READ) {
      /* Read Array mode */
      if (d->status == 0)
         return(d->host_addr + offset);

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
                  m_log(dev->name,
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
      m_log(dev->name,"Writing 0x%llx at offset=0x%x\n",*data,offset);
#endif
      d->blk_cmd = 0;
      d->cui_cmd = 0;
      d->status = 1;
      return(d->host_addr + offset);
   }

   switch(*data) {
      /* Erase Setup */
      case 0x20202020:
         d->blk_cmd = *data;
         break;
         
      /* Erase Confirm */
      case 0xd0d0d0d0:
         if ((d->blk_cmd == 0x20202020) && !(offset & 0x3FFFF)) {
            memset(d->host_addr+offset,0xFF,0x40000);
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
         m_log(dev->name,"bootflash: default write case, val=0x%llx\n",*data);
   }

   return NULL;
}

/* dev_bootflash_init() */
int dev_bootflash_init(cpu_group_t *cpu_group,char *filename,
                       m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev;
   struct bootflash_data *d;

   if (!(dev = dev_create("bootflash"))) {
      fprintf(stderr,"bootflash: unable to create device.\n");
      return(-1);
   }

   /* allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"bootflash: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));

   dev->phys_addr = paddr;
   dev->phys_len  = 8 * 1048576;
   dev->handler   = dev_bootflash_access;
   dev->fd        = memzone_create_file(filename,dev->phys_len,&d->host_addr);
   dev->priv_data = d;

   if (dev->fd == -1) {
      fprintf(stderr,"bootflash: unable to map file '%s'\n",filename);
      return(-1);
   }

   /* Map this device to all CPU */
   cpu_group_bind_device(cpu_group,dev);
   return(0);
}
