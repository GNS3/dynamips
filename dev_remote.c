/*
 * Cisco C7200 (Predator) Remote Control Module.
 * Copyright (C) 2006 Christophe Fillot.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>

#include "utils.h"
#include "mips64.h"
#include "cp0.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net.h"
#include "net_io.h"
#include "registry.h"
#include "ptask.h"
#include "vm.h"
#include "dev_c7200.h"
#include "dev_c3600.h"

#define DEBUG_ACCESS 0

/* Remote control private data */
struct remote_data {
   vm_obj_t vm_obj;
   struct vdevice dev;

   char buffer[512];
   u_int buf_pos;
};

/*
 * dev_remote_control_access()
 */
void *dev_remote_control_access(cpu_mips_t *cpu,struct vdevice *dev,
                                m_uint32_t offset,u_int op_size,u_int op_type,
                                m_uint64_t *data)
{
   struct remote_data *d = dev->priv_data;
   struct vdevice *nvram_dev;
   size_t len;

   if (op_type == MTS_READ)
      *data = 0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"REMOTE","reading reg 0x%x at pc=0x%llx\n",offset,cpu->pc);
   } else {
      cpu_log(cpu,"REMOTE","writing reg 0x%x at pc=0x%llx, data=0x%llx\n",
              offset,cpu->pc,*data);
   }
#endif

   switch(offset) {
      /* ROM Identification tag */
      case 0x000: 
         if (op_type == MTS_READ)
            *data = ROM_ID;
         break;

      /* CPU ID */
      case 0x004: 
         if (op_type == MTS_READ)
            *data = cpu->id;
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

      /* Reserved/Unused */
      case 0x010:
         break;

      /* RAM size */
      case 0x014: 
         if (op_type == MTS_READ)
            *data = cpu->vm->ram_size;
         break;

      /* ROM size */
      case 0x018: 
         if (op_type == MTS_READ)
            *data = cpu->vm->rom_size;
         break;

      /* NVRAM size */
      case 0x01c: 
         if (op_type == MTS_READ)
            *data = cpu->vm->nvram_size;
         break;             

      /* IOMEM size */
      case 0x020:
        if (op_type == MTS_READ)
            *data = cpu->vm->iomem_size;
         break;

      /* Config Register */
      case 0x024:
         if (op_type == MTS_READ)
            *data = cpu->vm->conf_reg;
         break;

      /* ELF entry point */
      case 0x028: 
         if (op_type == MTS_READ)
            *data = cpu->vm->ios_entry_point;
         break;      

      /* ELF machine id */
      case 0x02c:
         if (op_type == MTS_READ)
            *data = cpu->vm->elf_machine_id;
         break;

      /* Restart IOS Image */
      case 0x030:
         /* not implemented */
         break;

      /* Stop the virtual machine */
      case 0x034:
         cpu->vm->status = VM_STATUS_SHUTDOWN;
         break;

      /* Debugging/Log message: /!\ physical address */
      case 0x038:
         if (op_type == MTS_WRITE) {
            len = physmem_strlen(cpu->vm,*data);
            if (len < sizeof(d->buffer)) {
               physmem_copy_from_vm(cpu->vm,d->buffer,*data,len+1);
               vm_log(cpu->vm,"ROM",d->buffer);
            }
         }
         break;

      /* Buffering */
      case 0x03c:
         if (d->buf_pos < (sizeof(d->buffer)-1)) {
            d->buffer[d->buf_pos++] = *data & 0xFF;
            d->buffer[d->buf_pos] = 0;

            if (d->buffer[d->buf_pos-1] == '\n') {
               vm_log(cpu->vm,"ROM","%s",d->buffer);
               d->buf_pos = 0;
            }
         } else
            d->buf_pos = 0;
         break;

      /* Console output */
      case 0x040:
         if (op_type == MTS_WRITE)
            vtty_put_char(cpu->vm->vtty_con,(char)*data);
         break;

      /* NVRAM address */
      case 0x044:
         if (op_type == MTS_READ) {
            if ((nvram_dev = dev_get_by_name(cpu->vm,"nvram")))
               *data = nvram_dev->phys_addr;
         }
         break;

      /* IO memory size for Smart-Init (C3600, others ?) */
      case 0x048:
         if (op_type == MTS_READ) {
            switch(cpu->vm->type) {
               case VM_TYPE_C3600:
                  *data = VM_C3600(cpu->vm)->nm_iomem_size;
                  break;
               default:
                  *data = 0;
            }
         }
         break;
         
   }

   return NULL;
}

/* Shutdown a remote control device */
void dev_remote_control_shutdown(vm_instance_t *vm,struct remote_data *d)
{
   if (d != NULL) {
      dev_remove(vm,&d->dev);
      free(d);
   }
}

/* remote control device */
int dev_remote_control_init(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t len)
{
   struct remote_data *d;

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"Remote Control: unable to create device.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = "remote_ctrl";
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_remote_control_shutdown;

   dev_init(&d->dev);
   d->dev.name      = "remote_ctrl";
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_remote_control_access;
   d->dev.priv_data = d;

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
