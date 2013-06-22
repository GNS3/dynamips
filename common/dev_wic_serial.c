/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * WIC-1T & WIC-2T devices.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#include "crc.h"
#include "utils.h"
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_wic_serial.h"

/* Debugging flags */
#define DEBUG_UNKNOWN  1

struct wic_serial_data {
   char *name;
   struct vdevice dev;
   vm_instance_t *vm;
};

/*
 * dev_wic1t_access()
 */
static void *dev_wic1t_access(cpu_gen_t *cpu,struct vdevice *dev,
                              m_uint32_t offset,u_int op_size,u_int op_type,
                              m_uint64_t *data)
{
   struct wic_serial_data *d = dev->priv_data;

   switch(offset) {
      case 0x04: 
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

      case 0x08:
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->name,
                    "read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,d->name,
                    "write to unknown addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   return NULL;
}

/*
 * dev_wic2t_access()
 */
static void *dev_wic2t_access(cpu_gen_t *cpu,struct vdevice *dev,
                              m_uint32_t offset,u_int op_size,u_int op_type,
                              m_uint64_t *data)
{
   struct wic_serial_data *d = dev->priv_data;

   switch(offset) {
      /* Port 0 */
      case 0x04: 
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

      case 0x08:
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

      /* Port 1 */
      case 0x14: 
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

      case 0x18:
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->name,
                    "read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,d->name,
                    "write to unknown addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   return NULL;
}

/* 
 * dev_wic_serial_init()
 *
 * Generic WIC Serial initialization code.
 */
struct wic_serial_data *
dev_wic_serial_init(vm_instance_t *vm,char *name,u_int model,
                    m_uint64_t paddr,m_uint32_t len)
{
   struct wic_serial_data *d;
   struct vdevice *dev;

   /* Allocate the private data structure for WIC */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"%s (WIC-SERIAL): out of memory\n",name);
      return NULL;
   }

   memset(d,0,sizeof(*d));

   /* Create the device itself */
   if (!(dev = dev_create(name))) {
      fprintf(stderr,"%s (WIC): unable to create device.\n",name);
      free(d);
      return NULL;
   }

   d->name = name;
   d->vm   = vm;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;

   switch(model) {
      case WIC_SERIAL_MODEL_1T:
         d->dev.handler = dev_wic1t_access;
         break;
      case WIC_SERIAL_MODEL_2T:
         d->dev.handler = dev_wic2t_access;
         break;
      default:
         fprintf(stderr,"%s (WIC-SERIAL): unknown model %u\n",name,model);
         free(d);
         return NULL;
   }

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   return(d);
}

/* Remove a WIC serial device */
void dev_wic_serial_remove(struct wic_serial_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(d->vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}
