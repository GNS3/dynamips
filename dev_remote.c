/*
 * Cisco router simulation platform.
 * Copyright (C) 2006 Christophe Fillot.  All rights reserved.
 *
 * Remote control module.
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
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net.h"
#include "net_io.h"
#include "registry.h"
#include "ptask.h"
#include "dev_c7200.h"
#include "dev_c3600.h"
#include "dev_c2691.h"
#include "dev_c3725.h"
#include "dev_c3745.h"
#include "dev_c2600.h"

#define DEBUG_ACCESS 0

/* Remote control private data */
struct remote_data {
   vm_obj_t vm_obj;
   struct vdevice dev;

   char buffer[512];
   u_int buf_pos;

   u_int cookie_pos;
};

/*
 * dev_remote_control_access()
 */
void *dev_remote_control_access(cpu_gen_t *cpu,struct vdevice *dev,
                                m_uint32_t offset,u_int op_size,u_int op_type,
                                m_uint64_t *data)
{
   vm_instance_t *vm = cpu->vm;
   struct remote_data *d = dev->priv_data;
   struct vdevice *storage_dev;
   size_t len;

   if (op_type == MTS_READ)
      *data = 0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"REMOTE","reading reg 0x%x at pc=0x%llx\n",
              offset,cpu_get_pc(cpu));
   } else {
      cpu_log(cpu,"REMOTE","writing reg 0x%x at pc=0x%llx, data=0x%llx\n",
              offset,cpu_get_pc(cpu),*data);
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
            cpu->reg_dump(cpu);
         break;

      /* Display CPU memory info */
      case 0x00c:
         if (op_type == MTS_WRITE)
            cpu->mmu_dump(cpu);
         break;

      /* Reserved/Unused */
      case 0x010:
         break;

      /* RAM size */
      case 0x014: 
         if (op_type == MTS_READ)
            *data = vm->ram_size;
         break;

      /* ROM size */
      case 0x018: 
         if (op_type == MTS_READ)
            *data = vm->rom_size;
         break;

      /* NVRAM size */
      case 0x01c: 
         if (op_type == MTS_READ)
            *data = vm->nvram_size;
         break;             

      /* IOMEM size */
      case 0x020:
        if (op_type == MTS_READ)
            *data = vm->iomem_size;
         break;

      /* Config Register */
      case 0x024:
         if (op_type == MTS_READ)
            *data = vm->conf_reg;
         break;

      /* ELF entry point */
      case 0x028: 
         if (op_type == MTS_READ)
            *data = vm->ios_entry_point;
         break;      

      /* ELF machine id */
      case 0x02c:
         if (op_type == MTS_READ)
            *data = vm->elf_machine_id;
         break;

      /* Restart IOS Image */
      case 0x030:
         /* not implemented */
         break;

      /* Stop the virtual machine */
      case 0x034:
         vm->status = VM_STATUS_SHUTDOWN;
         break;

      /* Debugging/Log message: /!\ physical address */
      case 0x038:
         if (op_type == MTS_WRITE) {
            len = physmem_strlen(vm,*data);
            if (len < sizeof(d->buffer)) {
               physmem_copy_from_vm(vm,d->buffer,*data,len+1);
               vm_log(vm,"ROM",d->buffer);
            }
         }
         break;

      /* Buffering */
      case 0x03c:
         if (d->buf_pos < (sizeof(d->buffer)-1)) {
            d->buffer[d->buf_pos++] = *data & 0xFF;
            d->buffer[d->buf_pos] = 0;

            if (d->buffer[d->buf_pos-1] == '\n') {
               vm_log(vm,"ROM","%s",d->buffer);
               d->buf_pos = 0;
            }
         } else
            d->buf_pos = 0;
         break;

      /* Console output */
      case 0x040:
         if (op_type == MTS_WRITE)
            vtty_put_char(vm->vtty_con,(char)*data);
         break;

      /* NVRAM address */
      case 0x044:
         if (op_type == MTS_READ) {
            if ((storage_dev = dev_get_by_name(vm,"nvram")))
               *data = storage_dev->phys_addr;

            if ((storage_dev = dev_get_by_name(vm,"ssa")))
               *data = storage_dev->phys_addr;

            if (cpu->type == CPU_TYPE_MIPS64)
               *data += MIPS_KSEG1_BASE;
         }
         break;

      /* IO memory size for Smart-Init (C3600, others ?) */
      case 0x048:
         if (op_type == MTS_READ) {
            switch(vm->type) {
               case VM_TYPE_C3600:
                  *data = VM_C3600(vm)->nm_iomem_size;
                  break;
               case VM_TYPE_C2691:
                  *data = VM_C2691(vm)->nm_iomem_size;
                  break;
               case VM_TYPE_C3725:
                  *data = VM_C3725(vm)->nm_iomem_size;
                  break;
               case VM_TYPE_C3745:
                  *data = VM_C3745(vm)->nm_iomem_size;
                  break;
               case VM_TYPE_C2600:
                  *data = VM_C2600(vm)->nm_iomem_size;
                  break;
               default:
                  *data = 0;
            }
         }
         break;

      /* Cookie position selector */
      case 0x04c:
         if (op_type == MTS_READ)
            *data = d->cookie_pos;
         else
            d->cookie_pos = *data;
         break;
         
      /* Cookie data */
      case 0x050:
         if ((op_type == MTS_READ) && (d->cookie_pos < 64))
            *data = vm->chassis_cookie[d->cookie_pos];
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
