/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 *
 * Intel Flash SIMM emulation.
 *
 * Intelligent ID Codes:
 *   28F008SA: 0x89A2 (1 Mb)
 *   28F016SA: 0x89A0 (2 Mb)
 *
 * Manuals:
 *    http://www.ortodoxism.ro/datasheets/Intel/mXvsysv.pdf
 *
 * TODO: A lot of commands are lacking. Doesn't work with NPE-G2.
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

/* Flash command states */
enum {
   FLASH_CMD_READ_ARRAY = 0,
   FLASH_CMD_READ_ID,
   FLASH_CMD_READ_QUERY,
   FLASH_CMD_READ_STATUS,
   FLASH_CMD_WRITE_BUF_CNT,
   FLASH_CMD_WRITE_BUF_DATA,
   FLASH_CMD_WRITE_BUF_CONFIRM,
   FLASH_CMD_WB_PROG,
   FLASH_CMD_WB_PROG_DONE,
   FLASH_CMD_BLK_ERASE,
   FLASH_CMD_BLK_ERASE_DONE,
   FLASH_CMD_CONFIG,
};

/* Flash access mode (byte or word) */
enum {
   FLASH_MODE_BYTE = 1,
   FLASH_MODE_WORD = 2,
};

#define MAX_FLASH  4
#define FLASH_BUF_SIZE  32

/* Forward declarations */
struct flash_data;
struct flashset_data;

/* Flash model */
struct flash_model {
   char *name;
   u_int total_size;
   u_int mode;
   u_int nr_flash_bits;
   u_int blk_size;
   u_int id_manufacturer;
   u_int id_device;
};

/* Flash internal data */
struct flash_data {
   u_int mode,offset_shift,state,blk_size;
   m_uint8_t id_manufacturer,id_device;
   m_uint8_t status_reg;
   
   struct flashset_data *flash_set;
   u_int flash_pos;

   /* Write buffer */
   u_int wb_offset,wb_count,wb_remain;
   u_int wbuf[FLASH_BUF_SIZE];
};

/* Flashset private data */
struct flashset_data {
   vm_instance_t *vm;
   vm_obj_t vm_obj;
   struct vdevice dev;
   char *filename;
   
   u_int mode;
   u_int nr_flash_bits;
   u_int nr_flash_count;
   struct flash_data flash[MAX_FLASH];
};

/* Log a Flash message */
#define FLASH_LOG(d,msg...) vm_log((d)->flash_set->vm, \
                                   (d)->flash_set->dev.name, \
                                   msg)

#define BPTR(d,offset) (((u_char *)(d)->dev.host_addr) + offset)

/* Some Flash models */
static struct flash_model flash_models[] = {
   /* C1700 4 Mb bootflash: 1x28F320 in word mode */
   { "c1700-bootflash-4mb",4 * 1048576,FLASH_MODE_WORD,0,0x10000,0x89,0x14 },

   /* C1700 8 Mb bootflash: 1x28F640 in word mode */
   { "c1700-bootflash-8mb",8 * 1048576,FLASH_MODE_WORD,0,0x10000,0x89,0x15 },

   /* C3600 8 Mb bootflash: 4x28F016SA in byte mode */
   { "c3600-bootflash-8mb",8 * 1048576,FLASH_MODE_BYTE,2,0x10000,0x89,0xA0 },

   /* C7200 4 Mb bootflash: 4x28F008SA in byte mode */
   { "c7200-bootflash-4mb",4 * 1048576,FLASH_MODE_BYTE,2,0x10000,0x89,0xA2 },

   /* C7200 8 Mb bootflash: 4x28F016SA in byte mode */
   { "c7200-bootflash-8mb",8 * 1048576,FLASH_MODE_BYTE,2,0x10000,0x89,0xA0 },

   /*
    * C7200 64 Mb bootflash: 4x128 Mb Intel flash in byte mode 
    * (for NPE-G2 but doesn't work now).
    */
   { "c7200-bootflash-64mb",64 * 1048576,FLASH_MODE_BYTE,2,0x10000,0x89,0x18 },

   /* C2600 8 Mb bootflash: 4x28F016SA in byte mode */
   { "c2600-bootflash-8mb",8 * 1048576,FLASH_MODE_BYTE,2,0x10000,0x89,0xA0 },

   { NULL, 0, 0, 0, 0, 0 },
};

/* Flash model lookup */
static struct flash_model *flash_model_find(char *name)
{
   struct flash_model *fm;

   for(fm=&flash_models[0];fm->name!=NULL;fm++)
      if (!strcmp(fm->name,name))
         return fm;

   return NULL;
}

/* Initialize a flashset */
static int flashset_init(struct flashset_data *d,
                         u_int mode,u_int nr_flash_bits,u_int blk_size,
                         m_uint8_t id_manufacturer,m_uint8_t id_device)
{
   struct flash_data *flash;
   u_int i,offset_shift;
   
   d->mode = mode;
   d->nr_flash_bits  = nr_flash_bits;
   d->nr_flash_count = 1 << d->nr_flash_bits;

   switch(mode) {
      case FLASH_MODE_BYTE:
         offset_shift = 0;
         break;
      case FLASH_MODE_WORD:
         offset_shift = 1;
         break;
      default:
         return(-1);
   }

   for(i=0;i<d->nr_flash_count;i++) {
      flash = &d->flash[i];

      flash->mode = mode;
      flash->offset_shift = offset_shift;
      flash->state = FLASH_CMD_READ_ARRAY;

      flash->id_manufacturer = id_manufacturer;
      flash->id_device = id_device;

      flash->flash_set = d;
      flash->flash_pos = i;

      flash->blk_size = blk_size;
   }

   return(0);
}

/* Read a byte from a Flash */
static int flash_read(struct flash_data *d,u_int offset,u_int *data)
{
   u_int real_offset;

   real_offset = (offset << (d->flash_set->nr_flash_bits)) + d->flash_pos;

   if (d->mode == FLASH_MODE_BYTE) {
      *data = *BPTR(d->flash_set,real_offset);
   } else {
      *data  = *BPTR(d->flash_set,(real_offset << 1)) << 8;
      *data |= *BPTR(d->flash_set,(real_offset << 1)+1);
   }
   return(0);
}

/* Write a byte to a Flash */
static int flash_write(struct flash_data *d,u_int offset,u_int data)
{
   u_int real_offset;

   real_offset = (offset << (d->flash_set->nr_flash_bits)) + d->flash_pos;

   if (d->mode == FLASH_MODE_BYTE) {
      *BPTR(d->flash_set,real_offset) = data;
   } else {
      *BPTR(d->flash_set,(real_offset << 1))   = data >> 8;
      *BPTR(d->flash_set,(real_offset << 1)+1) = data & 0xFF;
   }
   return(0);
}

/* Set machine state given a command */
static void flash_cmd(struct flash_data *d,u_int offset,u_int cmd)
{
   cmd = cmd & 0xFF;
   
   switch(cmd) {
      case 0x40:
      case 0x10:
         d->state = FLASH_CMD_WB_PROG;
         break;
      case 0xe8:
         d->state = FLASH_CMD_WRITE_BUF_CNT;
         d->wb_offset = offset;
         d->wb_count = d->wb_remain = 0;
         break;
      case 0x70:
         d->state = FLASH_CMD_READ_STATUS;
         break;
      case 0x50:
         d->status_reg = 0;
         d->state = FLASH_CMD_READ_ARRAY;
         break;
      case 0x90:
         d->state = FLASH_CMD_READ_ID;
         break;
      case 0x20:
         d->state = FLASH_CMD_BLK_ERASE;
         break;
      case 0xff:
         d->state = FLASH_CMD_READ_ARRAY;
         break;
      default:
         FLASH_LOG(d,"flash_cmd(%u): command 0x%2.2x not implemented\n",
                   d->flash_pos,(u_int)cmd);
   }
}

/* Generic Flash access */
static void flash_access(struct flash_data *d,m_uint32_t offset,u_int op_type,
                         u_int *data)
{
   u_int i;

   if (op_type == MTS_READ)
      *data = 0x00;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      FLASH_LOG(d,"flash_access(%u): read  access to offset 0x%8.8x "
                "(state=%u)\n",d->flash_pos,offset,d->state);
   } else {
      FLASH_LOG(d,"flash_access(%u): write access to offset 0x%8.8x, "
                "data=0x%4.4x (state=%u)\n",
                d->flash_pos,offset,*data,d->state);
   }
#endif

   offset >>= d->offset_shift;

   /* State machine for Flash commands */
   switch(d->state) {
      case FLASH_CMD_READ_ARRAY:
         if (op_type == MTS_READ) {
            flash_read(d,offset,data);
            return;
         }

         /* Command Write */
         flash_cmd(d,offset,*data);
         break;

      /* Write byte/word */
      case FLASH_CMD_WB_PROG:
         if (op_type == MTS_WRITE) {
            flash_write(d,offset,*data);
            d->state = FLASH_CMD_WB_PROG_DONE;
         }
         break;

      /* Write byte/word (done) */
      case FLASH_CMD_WB_PROG_DONE:
         if (op_type == MTS_WRITE) {
            flash_cmd(d,offset,*data);
         } else {
            *data = 0x80;
         }
         break;

      /* Write buffer (count) */
      case FLASH_CMD_WRITE_BUF_CNT:
         if (op_type == MTS_WRITE) {
            d->wb_count = d->wb_remain = (*data & 0x1F) + 1;
            d->state = FLASH_CMD_WRITE_BUF_DATA;
         } else {
            *data = 0x80;
         }
         break;

      /* Write buffer (data) */
      case FLASH_CMD_WRITE_BUF_DATA:
         if (op_type == MTS_WRITE) {            
            if ((offset >= d->wb_offset) && 
                (offset < (d->wb_offset + d->wb_count)))
            {
               d->wbuf[offset - d->wb_offset] = *data;
               d->wb_remain--;

               if (!d->wb_remain)
                  d->state = FLASH_CMD_WRITE_BUF_CONFIRM;
            }
         } else {
            *data = 0x80;
         }
         break;

      /* Write buffer (confirm) */
      case FLASH_CMD_WRITE_BUF_CONFIRM:
         if (op_type == MTS_WRITE) {
            if ((*data & 0xFF) == 0xD0) {
               for(i=0;i<d->wb_count;i++)
                  flash_write(d,d->wb_offset+i,d->wbuf[i]);
            } else {
               /* XXX Error */
            }

            d->state = FLASH_CMD_READ_ARRAY;
         } else {
            *data = 0x80;
         }
         break;

      /* Read status register */
      case FLASH_CMD_READ_STATUS:
         if (op_type == MTS_READ)
            *data = 0x80; //d->status_reg;

         d->state = FLASH_CMD_READ_ARRAY;
         break;

      /* Read identifier codes */
      case FLASH_CMD_READ_ID:
         if (op_type == MTS_READ) {
            switch(offset) {
               case 0x00:
                  *data = d->id_manufacturer;
                  break;
               case 0x01:
                  *data = d->id_device;
                  break;
               default:
                  *data = 0x00;
                  break;
            }
         } else {
            flash_cmd(d,offset,*data);
         }
         break;

      /* Block Erase */
      case FLASH_CMD_BLK_ERASE:
         if (op_type == MTS_WRITE) {
#if DEBUG_WRITE
            FLASH_LOG(d,"flash_access(%u): erasing block at offset 0x%8.8x\n"
                      offset);
#endif
            if ((*data & 0xFF) == 0xD0) {
               for(i=0;i<d->blk_size;i++)
                  flash_write(d,offset+i,0xFFFF);

               d->state = FLASH_CMD_BLK_ERASE_DONE;
            }
         } else {
            *data = 0x80;
         }
         break;

      /* Block Erase Done */
      case FLASH_CMD_BLK_ERASE_DONE:
         if (op_type == MTS_WRITE) {
            flash_cmd(d,offset,*data);
         } else {
            *data = 0x80;
         }
         break;
   }
}

/*
 * dev_bootflash_access()
 */
void *dev_bootflash_access(cpu_gen_t *cpu,struct vdevice *dev,
                           m_uint32_t offset,u_int op_size,u_int op_type,
                           m_uint64_t *data)
{
   struct flashset_data *d = dev->priv_data;
   u_int flash_data[8];
   u_int i,fi,d_off;

#if DEBUG_ACCESS
   if (op_type == MTS_READ)
      cpu_log(cpu,dev->name,"read  access to offset = 0x%x, pc = 0x%llx\n",
              offset,cpu_get_pc(cpu));
   else
      cpu_log(cpu,dev->name,"write access to vaddr = 0x%x, pc = 0x%llx, "
              "val = 0x%llx\n",offset,cpu_get_pc(cpu),*data);
#endif

   if (op_type == MTS_READ) {
      *data = 0;

      for(i=0;i<op_size;i+=d->mode) {
         fi = (offset+i) & (d->nr_flash_count-1);

         flash_access(&d->flash[fi],((offset+i) >> d->nr_flash_bits),op_type,
                      &flash_data[i]);

         d_off = (op_size - i - d->mode) << 3;
         *data |= (m_uint64_t)flash_data[i] << d_off;
      }
   } else {
      for(i=0;i<op_size;i+=d->mode) {
         fi = (offset+i) & (d->nr_flash_count-1);

         d_off = (op_size - i - d->mode) << 3;
         flash_data[i] = *data >> d_off;

         flash_access(&d->flash[fi],((offset+i) >> d->nr_flash_bits),op_type,
                      &flash_data[i]);
      }
   }
   
   return NULL;
}

/* Shutdown a bootflash device */
void dev_bootflash_shutdown(vm_instance_t *vm,struct flashset_data *d)
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
int dev_bootflash_init(vm_instance_t *vm,char *name,char *model,
                       m_uint64_t paddr)
{  
   struct flash_model *fm;
   struct flashset_data *d;
   u_char *ptr;

   /* Find the flash model */
   if (!(fm = flash_model_find(model))) {
      vm_error(vm,"bootflash: unable to find model '%s'\n",model);
      return(-1);
   }

   /* Allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      vm_error(vm,"bootflash: unable to create device.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->vm = vm;

   /* Initialize flash based on model properties */
   flashset_init(d,fm->mode,fm->nr_flash_bits,fm->blk_size,
                 fm->id_manufacturer,fm->id_device);

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_bootflash_shutdown;

   if (!(d->filename = vm_build_filename(vm,name))) {
      vm_error(vm,"bootflash: unable to create filename.\n");
      goto err_filename;
   }

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = fm->total_size;
   d->dev.handler   = dev_bootflash_access;
   d->dev.fd        = memzone_create_file(d->filename,d->dev.phys_len,&ptr);
   d->dev.host_addr = (m_iptr_t)ptr;
   d->dev.flags     = VDEVICE_FLAG_NO_MTS_MMAP;

   if (d->dev.fd == -1) {
      vm_error(vm,"bootflash: unable to map file '%s'\n",d->filename);
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
