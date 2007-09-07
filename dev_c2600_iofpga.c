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
#include "nmc93cX6.h"
#include "dev_mpc860.h"
#include "dev_c2600.h"

/* Debugging flags */
#define DEBUG_UNKNOWN   1
#define DEBUG_ACCESS    0
#define DEBUG_NET_IRQ   0

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

/* Network IRQ distribution */
static u_int net_irq_dist[C2600_MAX_NM_BAYS] = {
   4,  /* reg 0x08, bits 4-5 */
   0,  /* reg 0x08, bits 0-3 */
};

/* IO FPGA structure */
struct c2600_iofpga_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   c2600_t *router;
   
   /* Network Interrupt status */
   m_uint8_t net_irq_status;

   /* Interrupt mask */
   m_uint16_t intr_mask;

   /* WIC SPI selection */
   m_uint8_t wic_select;
};

/* Mainboard EEPROM definition */
static const struct nmc93cX6_eeprom_def eeprom_mb_def = {
   EEPROM_MB_CLK, EEPROM_MB_CS,
   EEPROM_MB_DIN, EEPROM_MB_DOUT,
};

/* Mainboard EEPROM */
static const struct nmc93cX6_group eeprom_mb_group = {
   EEPROM_TYPE_NMC93C46, 1, 0, 
   EEPROM_DORD_NORMAL,
   EEPROM_DOUT_HIGH,
   EEPROM_DEBUG_DISABLED,
   "Mainboard EEPROM",
   { &eeprom_mb_def },
};

/* NM EEPROM definition */
static const struct nmc93cX6_eeprom_def eeprom_nm_def = {
   EEPROM_NM_CLK, EEPROM_NM_CS,
   EEPROM_NM_DIN, EEPROM_NM_DOUT,
};

/* NM EEPROM */
static const struct nmc93cX6_group eeprom_nm_group = {
   EEPROM_TYPE_NMC93C46, 1, 0, 
   EEPROM_DORD_NORMAL,
   EEPROM_DOUT_HIGH,
   EEPROM_DEBUG_DISABLED,
   "NM EEPROM", 
   { &eeprom_nm_def },
};

/* Update network interrupt status */
static inline void dev_c2600_iofpga_net_update_irq(struct c2600_iofpga_data *d)
{
   if (d->net_irq_status) {
      vm_set_irq(d->router->vm,C2600_NETIO_IRQ);
   } else {
      vm_clear_irq(d->router->vm,C2600_NETIO_IRQ);
   }
}

/* Trigger a Network IRQ for the specified slot/port */
void dev_c2600_iofpga_net_set_irq(struct c2600_iofpga_data *d,
                                  u_int slot,u_int port)
{
#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"IO_FPGA","setting NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   d->net_irq_status |= 1 << (net_irq_dist[slot] + port);
   dev_c2600_iofpga_net_update_irq(d);
}

/* Clear a Network IRQ for the specified slot/port */
void dev_c2600_iofpga_net_clear_irq(struct c2600_iofpga_data *d,
                                    u_int slot,u_int port)
{
#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"IO_FPGA","clearing NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   d->net_irq_status &= ~(1 << (net_irq_dist[slot] + port));
   dev_c2600_iofpga_net_update_irq(d);
}

/* Callback for MPC860 SPI Transmit */
static void dev_c2600_mpc860_spi_tx_callback(struct mpc860_data *mpc_data,
                                             u_char *buffer,u_int len,
                                             void *user_arg)
{
   struct c2600_iofpga_data *d = user_arg;
   struct cisco_eeprom *eeprom;
   u_char reply_buf[4];
   u_int wic_port;
   u_int eeprom_offset;

   if (d->wic_select & 0x20)
      wic_port = 0x10;
   else if (d->wic_select & 0x08)
      wic_port = 0x20;
   else {
      vm_error(d->router->vm,"unknown value for wic_select (0x%8.8x)\n",
               d->wic_select);
      wic_port = 0;
   }

   /* No WIC in slot or no EEPROM: fake an empty EEPROM */
   if (!wic_port || !(eeprom = vm_slot_get_eeprom(d->router->vm,0,wic_port))) {
      memset(reply_buf,0xFF,sizeof(reply_buf));
      mpc860_spi_receive(mpc_data,reply_buf,sizeof(reply_buf));
      return;
   }

   /* Read request: 0x03 offset 0x00 0x00 */
   eeprom_offset = buffer[1];

   reply_buf[0] = 0;
   reply_buf[1] = 0;
   cisco_eeprom_get_byte(eeprom,eeprom_offset,&reply_buf[2]);
   cisco_eeprom_get_byte(eeprom,eeprom_offset+1,&reply_buf[3]);

   mpc860_spi_receive(mpc_data,reply_buf,sizeof(reply_buf));
}
     
/*
 * dev_c2600_iofpga_access()
 */
static void *
dev_c2600_iofpga_access(cpu_gen_t *cpu,struct vdevice *dev,
                        m_uint32_t offset,u_int op_size,u_int op_type,
                        m_uint64_t *data)
{
   struct c2600_iofpga_data *d = dev->priv_data;

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
         break;

      /* 
       * Network Interrupt.
       *
       * Bit 0-3: slot 1.
       * Bit 4: slot 0 (MB), port 0
       * Bit 5: slot 0 (MB), port 1
       * Other: AIM ? (error messages displayed)
       */
      case 0x08:
         if (op_type == MTS_READ)
            *data = d->net_irq_status;
         break;

      case 0x10:
         if (op_type == MTS_READ)
            *data = d->wic_select;
         else {
            d->wic_select = *data;
         }
         break;

      case 0x14:
         if (op_type == MTS_READ)
            *data = 0; //0xFFFFFFFF;
         break;

      /* 
       * Flash Related: 0x1y
       * 
       * Bit 1: card present in slot 0 / WIC 0.
       * Bit 2: card present in slot 0 / WIC 1.
       *
       * Other bits unknown.
       */
      case 0x0c:
         if (op_type == MTS_READ) {
            *data = 0x10;

            /* check WIC 0 */
            if (vm_slot_check_eeprom(d->router->vm,0,0x10))
               *data |= 0x02;

            /* check WIC 1 */
            if (vm_slot_check_eeprom(d->router->vm,0,0x20))
               *data |= 0x04;
         }
         break;

      /* NM EEPROM */
      case 0x1c:
         if (op_type == MTS_WRITE)
            nmc93cX6_write(&d->router->nm_eeprom_group,(u_int)(*data));
         else
            *data = nmc93cX6_read(&d->router->nm_eeprom_group);
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

/* Shutdown the IO FPGA device */
static void 
dev_c2600_iofpga_shutdown(vm_instance_t *vm,struct c2600_iofpga_data *d)
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
   struct c2600_iofpga_data *d;

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

   /* Initialize the MPC860 SPI TX callback to read mainboard WIC EEPROMs */
   mpc860_spi_set_tx_callback(router->mpc_data,
                              dev_c2600_mpc860_spi_tx_callback,d);
                              
   /* Map this device to the VM */
   vm_bind_device(router->vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
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
}
