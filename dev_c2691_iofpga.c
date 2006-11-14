/*
 * Cisco 2691 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <termios.h>
#include <fcntl.h>
#include <pthread.h>

#include "ptask.h"
#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_vtty.h"
#include "nmc93c46.h"
#include "dev_c2691.h"

/* Debugging flags */
#define DEBUG_UNKNOWN   1
#define DEBUG_ACCESS    0

/* Definitions for Mainboard EEPROM */
#define EEPROM_MB_DOUT  3
#define EEPROM_MB_DIN   2
#define EEPROM_MB_CLK   1
#define EEPROM_MB_CS    0

/* Definitions for Network Modules EEPROM */
#define EEPROM_NM_DOUT  7
#define EEPROM_NM_DIN   6
#define EEPROM_NM_CLK   2
#define EEPROM_NM_CS    4

#define C2691_NET_IRQ_CLEARING_DELAY  16

/* IO FPGA structure */
struct iofpga_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   c2691_t *router;
   
   /* 
    * Used to introduce a "delay" before clearing the network interrupt
    * on 3620/3640 platforms. Added due to a packet loss when using an 
    * Ethernet NM on these platforms.
    *
    * Anyway, we should rely on the device information with appropriate IRQ
    * routing.
    */
   int net_irq_clearing_count;

   /* Interrupt mask*/
   m_uint16_t intr_mask;
};

/* Mainboard EEPROM definition */
static const struct nmc93c46_eeprom_def eeprom_mb_def = {
   EEPROM_MB_CLK, EEPROM_MB_CS,
   EEPROM_MB_DIN, EEPROM_MB_DOUT,
};

/* Mainboard EEPROM */
static const struct nmc93c46_group eeprom_mb_group = {
   1, 0, "Mainboard EEPROM", 0, { &eeprom_mb_def },
};

/* NM EEPROM definition */
static const struct nmc93c46_eeprom_def eeprom_nm_def = {
   EEPROM_NM_CLK, EEPROM_NM_CS,
   EEPROM_NM_DIN, EEPROM_NM_DOUT,
};

/* NM EEPROM */
static const struct nmc93c46_group eeprom_nm_group = {
   1, 0, "NM EEPROM", 0, { &eeprom_nm_def },
};

/*
 * dev_c2691_iofpga_access()
 */
static void *
dev_c2691_iofpga_access(cpu_mips_t *cpu,struct vdevice *dev,
                        m_uint32_t offset,u_int op_size,u_int op_type,
                        m_uint64_t *data)
{
   struct iofpga_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0x0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"IO_FPGA","reading reg 0x%x at pc=0x%llx (size=%u)\n",
              offset,cpu->pc,op_size);
   } else {
      cpu_log(cpu,"IO_FPGA",
              "writing reg 0x%x at pc=0x%llx, data=0x%llx (size=%u)\n",
              offset,cpu->pc,*data,op_size);
   }
#endif

   switch(offset) {
      /*
       * Platform type ? 
       * (other values than 0 cause crashes or lot of errors).
       */
      case 0x36:
          if (op_type == MTS_READ)
             *data = 0x0000;
          break;

      /* Mainboard EEPROM */
      case 0x0e:
         if (op_type == MTS_WRITE)
            nmc93c46_write(&d->router->mb_eeprom_group,(u_int)(*data));
         else
            *data = nmc93c46_read(&d->router->mb_eeprom_group);
         break;

      case 0x12:
         /* 
          * Bit 0: 1=No WIC in slot 0 ?
          * Bit 1: 1=No WIC in slot 1 ?
          * Bit 2: 1=No WIC in slot 2 ?
          */
         if (op_type == MTS_READ)
            *data = 0x0007;
         break;

      case 0x14:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

      case 0x18:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

      /* wic/vwic related */
      case 0x40:
         if (op_type == MTS_READ)
            *data = 0x0004;
         break;

      /* WIC related: 16-bit data */
      case 0x42:
         break;

      /* NM Slot 1 EEPROM */
      case 0x44:
         if (op_type == MTS_WRITE)
            nmc93c46_write(&d->router->nm_eeprom_group,(u_int)(*data));
         else
            *data = nmc93c46_read(&d->router->nm_eeprom_group);
         break;

      /* AIM EEPROM #0 */
      case 0x48:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

      /* AIM EEPROM #1 */
      case 0x4a:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

      /* 
       * NM Presence.
       * 
       * Bit 7: 0=NM present in slot 1.
       * Other bits unknown.
       */
      case 0x20:       
         if (op_type == MTS_READ) {
            *data = 0xFFFF;

            if (c2691_nm_check_eeprom(d->router,1))
               *data &= ~0x08;
         }
         break;

      /* ??? */
      case 0x24:
         break;

      /* Intr Mask (sh platform) */
      case 0x30:
         if (op_type == MTS_READ)
            *data = d->intr_mask;
         else
            d->intr_mask = *data;
         break;

      /* 
       * Network interrupt status.
       *
       * Bit 0: 0 = GT96100 Ethernet ports.
       * Other bits unknown.
       */
      case 0x26:
         if (op_type == MTS_READ) {
            *data = 0xFFFE;
            vm_clear_irq(d->router->vm,C2691_NETIO_IRQ);
         }
         break;

      /* 
       * Network interrupt status.
       *
       * Bit 0: 0 = NM in Slot 1.
       * Other bits unknown.
       */
      case 0x28:
         if (op_type == MTS_READ) {
            *data = 0xFFFE;
            vm_clear_irq(d->router->vm,C2691_NETIO_IRQ);
         }
         break;

      case 0x2c:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

      /* OIR interrupt but not supported (IRQ 6) */
      case 0x2e:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

      /* 
       * Environmental monitor, determined with "sh env all". 
       *
       * Bit 0: 1 = Fan Error
       * Bit 1: 1 = Fan Error
       * Bit 2: 1 = Over-temperature
       * Bit 3: ???
       * Bit 4: 0 = RPS present.
       * Bit 5: 0 = Input Voltage status failure.
       * Bit 6: 1 = Thermal status failure.
       * Bit 7: 1 = DC Output Voltage status failure.
       */
      case 0x3a:
         if (op_type == MTS_READ)
            *data = 0x0020;
         break;

      /*
       * Bit 0: Slot0 Compact Flash presence.
       * Bit 1: System Compact Flash presence.
       */
      case 0x3c:
         if (op_type == MTS_READ) {
            *data = 0xFFFF;

            /* System Flash ? */
            if (cpu->vm->pcmcia_disk_size[0])
               *data &= ~0x02;

            /* Slot0 Flash ? */
            if (cpu->vm->pcmcia_disk_size[1])
               *data &= ~0x01;
         }
         break;           

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"IO_FPGA",
                    "read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu->pc,op_size);
         } else {
            cpu_log(cpu,"IO_FPGA",
                    "write to unknown addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",offset,*data,cpu->pc,op_size);
         }
#endif
   }

   return NULL;
}

/* Initialize EEPROM groups */
void c2691_init_eeprom_groups(c2691_t *router)
{
   /* Initialize Mainboard EEPROM */
   router->mb_eeprom_group = eeprom_mb_group;
   router->mb_eeprom_group.eeprom[0] = &router->mb_eeprom;
   router->mb_eeprom.data = NULL;
   router->mb_eeprom.len  = 0;

   /* EEPROM for NM slot 1 */
   router->nm_eeprom_group = eeprom_nm_group;
   router->nm_eeprom_group.eeprom[0] = &router->nm_bay[1].eeprom;
}

/* Shutdown the IO FPGA device */
void dev_c2691_iofpga_shutdown(vm_instance_t *vm,struct iofpga_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/*
 * dev_c2691_iofpga_init()
 */
int dev_c2691_iofpga_init(c2691_t *router,m_uint64_t paddr,m_uint32_t len)
{
   vm_instance_t *vm = router->vm;
   struct iofpga_data *d;

   /* Allocate private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"IO_FPGA: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->router = router;

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = "io_fpga";
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_c2691_iofpga_shutdown;

   /* Set device properties */
   dev_init(&d->dev);
   d->dev.name      = "io_fpga";
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.priv_data = d;
   d->dev.handler   = dev_c2691_iofpga_access;

   /* Map this device to the VM */
   vm_bind_device(router->vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
