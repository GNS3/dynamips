/*
 * Cisco router simulation platform.
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
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_vtty.h"
#include "nmc93c46.h"
#include "dev_c2600.h"

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
   c2600_t *router;
   
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
 * dev_c2600_iofpga_access()
 */
static void *
dev_c2600_iofpga_access(cpu_gen_t *cpu,struct vdevice *dev,
                        m_uint32_t offset,u_int op_size,u_int op_type,
                        m_uint64_t *data)
{
   struct iofpga_data *d = dev->priv_data;

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
      case 0x04:
         if (op_type == MTS_READ)
            *data = 0x00;
         //vm_clear_irq(cpu->vm,C2600_NETIO_IRQ);
         break;

      /* 
       * Network Interrupt.
       *
       * Bit 0: slot 1.
       * Bit 4: slot 0 (MB), port 0
       * Bit 5: slot 0 (MB), port 1
       * Other: AIM ? (error messages displayed)
       */
      case 0x08:
         if (op_type == MTS_READ)
            *data = 0x31;
         vm_clear_irq(cpu->vm,C2600_NETIO_IRQ);
         break;

      case 0x10:
      case 0x14:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

      /* Flash Related: 0x1y */
#if 1
      case 0x0c:
         if (op_type == MTS_READ)
            *data = 0x10;
         break;
#endif

      /* NM EEPROM */
      case 0x1c:
         if (op_type == MTS_WRITE)
            nmc93c46_write(&d->router->nm_eeprom_group,(u_int)(*data));
         else
            *data = nmc93c46_read(&d->router->nm_eeprom_group);
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
void c2600_init_eeprom_groups(c2600_t *router)
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
void dev_c2600_iofpga_shutdown(vm_instance_t *vm,struct iofpga_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/*
 * dev_c2600_iofpga_init()
 */
int dev_c2600_iofpga_init(c2600_t *router,m_uint64_t paddr,m_uint32_t len)
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
   d->vm_obj.shutdown = (vm_shutdown_t)dev_c2600_iofpga_shutdown;

   /* Set device properties */
   dev_init(&d->dev);
   d->dev.name      = "io_fpga";
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.priv_data = d;
   d->dev.handler   = dev_c2600_iofpga_access;

   /* Map this device to the VM */
   vm_bind_device(router->vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
