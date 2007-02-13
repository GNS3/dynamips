/*
 * Cisco 3745 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * This is very similar to c2691.
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
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_vtty.h"
#include "nmc93c46.h"
#include "dev_c3745.h"

/* Debugging flags */
#define DEBUG_UNKNOWN   1
#define DEBUG_ACCESS    0

/* Definitions for Motherboard EEPROM (0x00) */
#define EEPROM_MB_DOUT  3
#define EEPROM_MB_DIN   2
#define EEPROM_MB_CLK   1
#define EEPROM_MB_CS    0

/* Definitions for I/O board EEPROM (0x01) */
#define EEPROM_IO_DOUT  3
#define EEPROM_IO_DIN   2
#define EEPROM_IO_CLK   1
#define EEPROM_IO_CS    8

/* Definitions for Midplane EEPROM (0x02) */
#define EEPROM_MP_DOUT  3
#define EEPROM_MP_DIN   2
#define EEPROM_MP_CLK   1
#define EEPROM_MP_CS    9

/* Definitions for Network Modules EEPROM */
#define EEPROM_NM_DOUT  7
#define EEPROM_NM_DIN   6
#define EEPROM_NM_CLK   2
#define EEPROM_NM_CS    4

#define C3745_NET_IRQ_CLEARING_DELAY  16

/* IO FPGA structure */
struct iofpga_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   c3745_t *router;
   
   /* 
    * Used to introduce a "delay" before clearing the network interrupt
    * on 3620/3640 platforms. Added due to a packet loss when using an 
    * Ethernet NM on these platforms.
    *
    * Anyway, we should rely on the device information with appropriate IRQ
    * routing.
    */
   int net_irq_clearing_count;

   /* Interrupt mask */
   m_uint16_t intr_mask,io_mask2;

   /* EEPROM select */
   u_int eeprom_select;
};

/* Motherboard EEPROM definition */
static const struct nmc93c46_eeprom_def eeprom_mb_def = {
   EEPROM_MB_CLK, EEPROM_MB_CS,
   EEPROM_MB_DIN, EEPROM_MB_DOUT,
};

/* I/O board EEPROM definition */
static const struct nmc93c46_eeprom_def eeprom_io_def = {
   EEPROM_IO_CLK, EEPROM_IO_CS,
   EEPROM_IO_DIN, EEPROM_IO_DOUT,
};

/* Midplane EEPROM definition */
static const struct nmc93c46_eeprom_def eeprom_mp_def = {
   EEPROM_MP_CLK, EEPROM_MP_CS,
   EEPROM_MP_DIN, EEPROM_MP_DOUT,
};

/* System EEPROM group */
static const struct nmc93c46_group eeprom_sys_group = {
   3, 0, "System EEPROM", 0, 
   { &eeprom_mb_def, &eeprom_io_def, &eeprom_mp_def }, 
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
 * dev_c3745_iofpga_access()
 */
static void *
dev_c3745_iofpga_access(cpu_gen_t *cpu,struct vdevice *dev,
                        m_uint32_t offset,u_int op_size,u_int op_type,
                        m_uint64_t *data)
{
   struct iofpga_data *d = dev->priv_data;
   u_int slot;

   if (op_type == MTS_READ)
      *data = 0x0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"IO_FPGA","reading reg 0x%x at pc=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),op_size);
   } else {
      cpu_log(cpu,"IO_FPGA",
              "writing reg 0x%x at pc=0x%llx, data=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),*data,op_size);
   }
#endif

   switch(offset) {
      /* Unknown */
      case 0x000000:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

      /* Unknown */
      case 0x000004:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

      /*
       * CompactFlash.
       *
       * Bit 0: Slot0 Compact Flash presence.
       * Bit 1: System Compact Flash presence.
       */
      case 0x000012:
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

      /* Suppress the "****TDM FPGA download failed.." message */
      case 0x000014:
         if (op_type == MTS_READ)
            *data = 0x00FF;
         break;

      /* Power supply status */
      case 0x00000a:
         if (op_type == MTS_READ)
            *data = 0x0000;
         break;

      /* Fan status */
      case 0x00000c:
         if (op_type == MTS_READ)
            *data = 0x0000;
         break;

      /* System EEPROMs */
      case 0x00000e:
         if (op_type == MTS_WRITE)
            nmc93c46_write(&d->router->sys_eeprom_group,(u_int)(*data));
         else
            *data = nmc93c46_read(&d->router->sys_eeprom_group);
         break;

      /* 
       * Network interrupt status.
       * 
       * Bit 0: 0 = GT96100 Ethernet ports.
       * Bit 8: 0 = AIM slot 0.
       * Bit 9: 0 = AIM slot 1.
       */
      case 0x000020:
         if (op_type == MTS_READ)
            *data = 0xFFFE;
         break;

      /* 
       * Network interrupt status.
       *
       * Bit  0: 0 = Interrupt for slot 1
       * Bit  4: 0 = Interrupt for slot 2
       * Bit  8: 0 = Interrupt for slot 3
       * Bit 12: 0 = Interrupt for slot 4
       */
      case 0x000022:
         if (op_type == MTS_READ)
            *data = 0x0000;
         vm_clear_irq(d->router->vm,C3745_NETIO_IRQ);
         break;

      /* 
       * Per Slot Intr Mask (seen with "sh platform").
       * IO Mask 1 is the lower 8-bits.
       */
      case 0x00002a:
         if (op_type == MTS_READ)
            *data = d->intr_mask;
         else
            d->intr_mask = *data;
         break;

      /* IO Mask 2 (seen with "sh platform") */
      case 0x00002c:
         if (op_type == MTS_READ)
            *data = d->io_mask2;
         else
            d->io_mask2 = *data;
         break;

      /* EEPROM in slots 1-4 */
      case 0x000040:
      case 0x000042:
      case 0x000044:
      case 0x000046:
         slot = (offset - 0x000040) >> 1;

         if (op_type == MTS_WRITE)
            nmc93c46_write(&d->router->nm_eeprom_group[slot],(u_int)(*data));
         else
            *data = nmc93c46_read(&d->router->nm_eeprom_group[slot]);
         break;

      /* AIM slot 0 EEPROM */
      case 0x000048:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;
   
      /* AIM slot 1 EEPROM */
      case 0x00004A:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

      /* 
       * NM presence.
       *
       * Bit  0: 0 = NM present in slot 2 (0x42)
       * Bit  4: 0 = NM present in slot 4 (0x46)
       * Bit  8: 0 = NM present in slot 1 (0x40)
       * Bit 12: 0 = NM present in slot 3 (0x44)
       */
      case 0x00004e:
         if (op_type == MTS_READ) {
            *data = 0xFFFF;
            
            if (c3745_nm_check_eeprom(d->router,1))
               *data &= ~0x0100;

            if (c3745_nm_check_eeprom(d->router,2))
               *data &= ~0x0001;

            if (c3745_nm_check_eeprom(d->router,3))
               *data &= ~0x1000;

            if (c3745_nm_check_eeprom(d->router,4))
               *data &= ~0x0010;
         }
         break;

      /* VWIC/WIC related */
      case 0x100004:
      case 0x100006:
      case 0x100008:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"IO_FPGA",
                    "read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,"IO_FPGA",
                    "write to unknown addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   return NULL;
}

/* Initialize EEPROM groups */
void c3745_init_eeprom_groups(c3745_t *router)
{
   int i;

   /* Initialize Mainboard EEPROM */
   router->sys_eeprom_group = eeprom_sys_group;

   for(i=0;i<3;i++) {
      router->sys_eeprom_group.eeprom[i] = &router->sys_eeprom[i];
      router->sys_eeprom[i].data = NULL;
      router->sys_eeprom[i].len  = 0;
   }      

   /* EEPROMs for Network Modules */
   for(i=1;i<=4;i++) {
      router->nm_eeprom_group[i-1] = eeprom_nm_group;
      router->nm_eeprom_group[i-1].eeprom[0] = &router->nm_bay[i].eeprom;
   }
}

/* Shutdown the IO FPGA device */
void dev_c3745_iofpga_shutdown(vm_instance_t *vm,struct iofpga_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/*
 * dev_c3745_iofpga_init()
 */
int dev_c3745_iofpga_init(c3745_t *router,m_uint64_t paddr,m_uint32_t len)
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
   d->vm_obj.shutdown = (vm_shutdown_t)dev_c3745_iofpga_shutdown;

   /* Set device properties */
   dev_init(&d->dev);
   d->dev.name      = "io_fpga";
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.priv_data = d;
   d->dev.handler   = dev_c3745_iofpga_access;

   /* Map this device to the VM */
   vm_bind_device(router->vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
