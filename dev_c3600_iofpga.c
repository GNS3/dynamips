/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * TODO: Online Insertion/Removal (OIR).
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
#include "dev_c3600.h"

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

/* Network IRQ distribution */
struct net_irq_distrib  {
   u_int c3620_c3640_offset;
   u_int c3660_reg;
   u_int c3660_offset;
};

/* 
 * Network IRQ distribution for c3620/c3640
 *
 *  Slot 0 | 3620/3640: reg 0x20001 | 3660: reg 0x20010, offset 0
 *  Slot 1 | 3620/3640: reg 0x20000 | 3660: reg 0x10010, offset 24
 *  Slot 2 | 3640     : reg 0x20003 | 3660: reg 0x10010, offset 16
 *  Slot 3 | 3640     : reg 0x20002 | 3660: reg 0x10010, offset 28
 *  Slot 4 | 3620/3640: N/A         | 3660: reg 0x10010, offset 20
 *  Slot 5 | 3620/3640: N/A         | 3660: reg 0x10010, offset 8
 *  Slot 6 | 3620/3640: N/A         | 3660: reg 0x10010, offset 0
 */
static struct net_irq_distrib net_irq_dist[C3600_MAX_NM_BAYS] = {
   { 16, 1, 0  },
   { 24, 0, 24 },
   {  0, 0, 16 },
   {  8, 0, 28 },
   { 32, 0, 20 },
   { 32, 0, 8  },
   { 32, 0, 0  },
};

/* IO FPGA structure */
struct c3600_iofpga_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   c3600_t *router;
   
   /* Network IRQ status */
   m_uint32_t net_irq_status[2];

   /* Slot select for EEPROM access */
   u_int eeprom_slot;

   /* IO Mask. Don't know the meaning */
   m_uint8_t io_mask;

   m_uint16_t sel;
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

/* C3660 NM presence masks */
static const m_uint16_t c3660_nm_masks[6] = {
   0xF0FF,   /* slot 1 */
   0xFFF0,   /* slot 2 */
   0x0FFF,   /* slot 3 */
   0xFF0F,   /* slot 4 */
   0xF0FF,   /* slot 5 */
   0xFFF0,   /* slot 6 */
};

/* Select the current NM EEPROM */
static void nm_eeprom_select(struct c3600_iofpga_data *d,u_int slot)
{
   struct cisco_eeprom *eeprom = NULL;
   struct cisco_card *card;

   card = vm_slot_get_card_ptr(d->router->vm,slot);

   if (card != NULL)
      eeprom = &card->eeprom;

   d->router->nm_eeprom_group.eeprom[0] = eeprom;
}

/* Return the NM status register given the detected EEPROM (3620/3640) */
static u_int nm_get_status_1(struct c3600_iofpga_data *d)
{
   u_int res = 0xFFFF;
   int i;

   for(i=0;i<4;i++) {
      if (vm_slot_get_card_ptr(d->router->vm,i))
         res &= ~(0x1111 << i);
   }
   
   return(res);
}

/* Return the NM status register given the detected EEPROM (3660) */
static u_int nm_get_status_2(struct c3600_iofpga_data *d,u_int pos)
{
   u_int res = 0xFFFF;
   u_int start,end;
   int i;

   switch(pos) {
      case 0:  /* word 0: slot 1 - 4 */
         start = 1;
         end   = 4;
         break;
      case 1:  /* word 1: slot 5 - 6 */
         start = 5;
         end = 6;
         break;
      default:
         return(res);
   }

   for(i=start;i<=end;i++) {
      if (vm_slot_get_card_ptr(d->router->vm,i))
         res &= c3660_nm_masks[i-1];
   }
   
   return(res);
}

/* Update network interrupt status */
static inline void 
dev_c3620_c3640_iofpga_net_update_irq(struct c3600_iofpga_data *d)
{
   if (d->net_irq_status[0]) {
      vm_set_irq(d->router->vm,C3600_NETIO_IRQ);
   } else {
      vm_clear_irq(d->router->vm,C3600_NETIO_IRQ);
   }
}

static inline void 
dev_c3660_iofpga_net_update_irq(struct c3600_iofpga_data *d)
{
   if (d->net_irq_status[0] || d->net_irq_status[1]) {
      vm_set_irq(d->router->vm,C3600_NETIO_IRQ);
   } else {
      vm_clear_irq(d->router->vm,C3600_NETIO_IRQ);
   }
}

/* Trigger a Network IRQ for the specified slot/port */
void dev_c3600_iofpga_net_set_irq(struct c3600_iofpga_data *d,
                                  u_int slot,u_int port)
{
   struct net_irq_distrib *irq_dist;

#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"IO_FPGA","setting NetIRQ for slot %u port %u\n",
          slot,port);
#endif

   irq_dist = &net_irq_dist[slot];

   switch(c3600_chassis_get_id(d->router)) {
      case 3620:
      case 3640:
         d->net_irq_status[0] |= (1 << (irq_dist->c3620_c3640_offset + port));
         dev_c3620_c3640_iofpga_net_update_irq(d);
         break;
      case 3660:
         d->net_irq_status[irq_dist->c3660_reg] |=
            (1 << (irq_dist->c3660_offset + port));
         dev_c3660_iofpga_net_update_irq(d);
         break;
   }
}

/* Clear a Network IRQ for the specified slot/port */
void dev_c3600_iofpga_net_clear_irq(struct c3600_iofpga_data *d,
                                    u_int slot,u_int port)
{
   struct net_irq_distrib *irq_dist;

#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"IO_FPGA","clearing NetIRQ for slot %u port %u\n",
          slot,port);
#endif

   irq_dist = &net_irq_dist[slot];

   switch(c3600_chassis_get_id(d->router)) {
      case 3620:
      case 3640:
         d->net_irq_status[0] &= ~(1 << (irq_dist->c3620_c3640_offset + port));
         dev_c3620_c3640_iofpga_net_update_irq(d);
         break;
      case 3660:
         d->net_irq_status[irq_dist->c3660_reg] &=
            ~(1 << (irq_dist->c3660_offset + port));
         dev_c3660_iofpga_net_update_irq(d);
         break;
   }
}

/*
 * dev_c3620_c3640_iofpga_access()
 */
static void *
dev_c3620_c3640_iofpga_access(cpu_gen_t *cpu,struct vdevice *dev,
                              m_uint32_t offset,u_int op_size,u_int op_type,
                              m_uint64_t *data)
{
   struct c3600_iofpga_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0x0;

#if DEBUG_ACCESS
   if (offset != 0x0c) {
      if (op_type == MTS_READ) {
         cpu_log(cpu,"IO_FPGA","reading reg 0x%x at pc=0x%llx (size=%u)\n",
                 offset,cpu_get_pc(cpu),op_size);
      } else {
         cpu_log(cpu,"IO_FPGA",
                 "writing reg 0x%x at pc=0x%llx, data=0x%llx (size=%u)\n",
                 offset,cpu_get_pc(cpu),*data,op_size);
      }
   }
#endif

   switch(offset) {
      /* Probably flash protection (if 0, no write access allowed) */
      case 0x00008:
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

      /* Bootflash of 8 Mb */
      case 0x0000a:
         if (op_type == MTS_READ)
            *data = 0x1000;
         break;

      /* 
       * 0x7d00 is written here regularly.
       * Some kind of hardware watchdog ?
       */     
      case 0x0000c:
         break;

      /* Mainboard EEPROM */
      case 0x0000e:
         if (op_type == MTS_WRITE)
            nmc93cX6_write(&d->router->mb_eeprom_group,(u_int)(*data));
         else
            *data = nmc93cX6_read(&d->router->mb_eeprom_group);
         break;

      case 0x10004:  /* ??? OIR control ??? */
         if (op_type == MTS_READ) {
            *data = 0x0000;
         }
         break;
         
      /* 
       * Network modules presence.
       *
       * Bit 0: 0 = NM in slot 0 is valid
       * Bit 1: 0 = NM in slot 1 is valid
       * Bit 2: 0 = NM in slot 2 is valid
       * Bit 3: 0 = NM in slot 3 is valid
       *
       * Very well explained on Cisco website:
       * http://www.cisco.com/en/US/customer/products/hw/routers/ps274/products_tech_note09186a0080109510.shtml
       */
      case 0x10006:
         if (op_type == MTS_READ)
            *data = nm_get_status_1(d);
         break;
        
      /*
       * NM EEPROMs.
       */
      case 0x10008:
         if (op_type == MTS_WRITE) {
            d->eeprom_slot = *data & 0x03;
            nm_eeprom_select(d,d->eeprom_slot);
            nmc93cX6_write(&d->router->nm_eeprom_group,*data);
         } else {
            *data = nmc93cX6_read(&d->router->nm_eeprom_group);
         }
         break;

      /* Network interrupt status */
      case 0x20000:   /* slot 1 */
         if (op_type == MTS_READ)
            *data = d->net_irq_status[0] >> 24;
         break;
      case 0x20001:   /* slot 0 */
         if (op_type == MTS_READ)
            *data = d->net_irq_status[0] >> 16;
         break;
      case 0x20002:   /* slot 3 */
         if (op_type == MTS_READ)
            *data = d->net_irq_status[0] >> 8;
         break;
      case 0x20003:   /* slot 2 */
         if (op_type == MTS_READ)
            *data = d->net_irq_status[0];
         break;

      /* 
       * Read when a PA Management interrupt is triggered.
       *
       * If not 0, we get:
       *   "Error: Unexpected NM Interrupt received from slot: x"
       */
      case 0x20004:
         if (op_type == MTS_READ)
            *data = 0x00;
         vm_clear_irq(d->router->vm,C3600_NM_MGMT_IRQ);
         break;

      /* 
       * Read when an external interrupt is triggered.
       *
       * Bit 4: 1 = %UNKNOWN-1-GT64010: Unknown fatal interrupt(s)
       * Bit 6: 1 = %OIRINT: OIR Event has occurred oir_ctrl 1000 oir_stat FFFF
       *
       * oir_ctrl = register 0x10004
       * oir_stat = register 0x10006
       */
      case 0x20006:
         if (op_type == MTS_READ)
            *data = 0x00;
         vm_clear_irq(d->router->vm,C3600_EXT_IRQ);
         break;

      /* IO Mask (displayed by "show c3600") */
      case 0x20008:
         if (op_type == MTS_READ)
            *data = d->io_mask;
         else
            d->io_mask = *data;
         break;

      /* 
       * Platform type ?
       * 0: 3640, 4 << 5: 3620, 3 << 5: 3660 
       */
      case 0x30000:
         if (op_type == MTS_READ) {
            switch(c3600_chassis_get_id(d->router)) {
               case 3620:
                  *data = 4 << 5;
                  break;
               case 3640:
                  *data = 0 << 5;
                  break;
               case 3660:
                  *data = 3 << 5;
                  break;
               default:
                  *data = 0;
            }
         }
         break;

      /* ??? */
      case 0x30002:
         if (op_type == MTS_WRITE) {
            d->sel = *data;
         } else {
            //*data = d->sel;
         }
         break;

      /* 
       * Environmental parameters, determined with "sh env all". 
       *
       * Bit 0: 0 = overtemperature condition.
       * Bit 4: 0 = RPS present.
       * Bit 5: 0 = Input Voltage status failure.
       * Bit 6: 1 = Thermal status failure.
       * Bit 7: 1 = DC Output Voltage status failure.
       */
      case 0x30004:
         if (op_type == MTS_READ) {
            *data = 32 + 1;
         }
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

/*
 * dev_c3660_iofpga_access()
 */
static void *
dev_c3660_iofpga_access(cpu_gen_t *cpu,struct vdevice *dev,
                        m_uint32_t offset,u_int op_size,u_int op_type,
                        m_uint64_t *data)
{
   struct c3600_iofpga_data *d = dev->priv_data;
   u_int slot;

   if (op_type == MTS_READ)
      *data = 0x0;

#if DEBUG_ACCESS
   if (offset != 0x0c) {
      if (op_type == MTS_READ) {
         cpu_log(cpu,"IO_FPGA","reading reg 0x%x at pc=0x%llx (size=%u)\n",
                 offset,cpu_get_pc(cpu),op_size);
      } else {
         cpu_log(cpu,"IO_FPGA",
                 "writing reg 0x%x at pc=0x%llx, data=0x%llx (size=%u)\n",
                 offset,cpu_get_pc(cpu),*data,op_size);
      }
   }
#endif

   switch(offset) {
      /* 
       * 0x7d00 is written here regularly.
       * Some kind of hardware watchdog ?
       */     
      case 0x0000c:
         break;

      /* Probably flash protection (if 0, no write access allowed) */
      case 0x00008:
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

      /* Bootflash of 8 Mb */
      case 0x0000a:
         if (op_type == MTS_READ)
            *data = 0x1000;
         break;

      /* NM presence - slots 1 to 4 */
      case 0x10006:
         if (op_type == MTS_READ)
            *data = nm_get_status_2(d,0);
         break;

      /* NM presence - slot 5 to 6 */
      case 0x10008:
         if (op_type == MTS_READ)
            *data = nm_get_status_2(d,1);
         break;
      
      /* Fan status, PS presence */
      case 0x10018:
         if (op_type == MTS_READ)
            *data = 0x0000;
         break;

      /* unknown, read by env monitor */
      case 0x1001a: 
         if (op_type == MTS_READ)
            *data = 0x0000;
         break;

      /* board temperature */
      case 0x30004:
         if (op_type == MTS_READ) {
            *data = 32 + 1;
         }
         break;

      /* sh c3600: Per Slot Intr Mask */
      case 0x10016:
         if (op_type == MTS_READ)
            *data = 0x12;
         break;

      /* sh c3600: OIR fsm state slot's (12) */
      case 0x10020:
         if (op_type == MTS_READ)
            *data = 0x00;
         break;

      /* sh c3600: OIR fsm state slot's (34) */
      case 0x10022:
         if (op_type == MTS_READ)
            *data = 0x00;
         break;

      /* sh c3600: OIR fsm state slot's (56) */
      case 0x10024:
         if (op_type == MTS_READ)
            *data = 0x00;
         break;

      /* 
       * Backplane EEPROM.
       *
       * Bit 7: 0=Telco chassis, 1=Enterprise chassis.
       */
      case 0x10000:
         if (op_type == MTS_WRITE)
            nmc93cX6_write(&d->router->mb_eeprom_group,(u_int)(*data));
         else
            *data = nmc93cX6_read(&d->router->mb_eeprom_group) | 0x80;
         break;

      /* NM EEPROMs - slots 1 to 6 */
      case 0x1000a:
      case 0x1000b:
      case 0x1000c:
      case 0x1000d:
      case 0x1000e:
      case 0x1000f:
         slot = (offset - 0x1000a) + 1;

         if (op_type == MTS_WRITE) {
            nmc93cX6_write(&d->router->c3660_nm_eeprom_group[slot],
                           (u_int)(*data));
         } else {
            *data = nmc93cX6_read(&d->router->c3660_nm_eeprom_group[slot]);
         }
         break;

      /* NM EEPROM - slot 0 */
      case 0x20006:
         if (op_type == MTS_WRITE) {
            nmc93cX6_write(&d->router->c3660_nm_eeprom_group[0],
                           (u_int)(*data));
         } else {
            *data = nmc93cX6_read(&d->router->c3660_nm_eeprom_group[0]);
         }
         break;

      /* Unknown EEPROMs ? */
      case 0x20000:
      case 0x20002:
      case 0x20004:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

      /* IO Mask (displayed by "show c3600") */
      case 0x20008:
         if (op_type == MTS_READ)
            *data = d->io_mask;
         else
            d->io_mask = *data;
         break;

      /* 0: 3640, 4 << 5: 3620, 3 << 5: 3660 */
      case 0x30000:
         if (op_type == MTS_READ)            
            *data = 3 << 5;
         break;

      /* ??? */   
      case 0x30008:
        if (op_type == MTS_READ)
           *data = 0xFF;
        break;

      /* 
       * Read at net interrupt (size 4).
       * It seems that there are 4 lines per slot.
       *
       *   Bit 24-27: slot 1
       *   Bit 16-19: slot 2
       *   Bit 28-31: slot 3
       *   Bit 20-23: slot 4
       *   Bit 08-11: slot 5
       *   Bit 00-03: slot 6
       *
       * Other bits are unknown.
       */
      case 0x10010:
         if (op_type == MTS_READ)
            *data = d->net_irq_status[0];
         break;

      /* 
       * Read at net interrupt (size 1) 
       *
       *   Bit 7-6: we get "Unexpected AIM interrupt on AIM slot 1".
       *   Bit 5-4: we get "Unexpected AIM interrupt on AIM slot 0".
       *   Bit 0-3: net interrupt for slot 0.
       */        
      case 0x20010:
         if (op_type == MTS_READ)
            *data = d->net_irq_status[1];
         break;

      /* 
       * Read when a PA Management interrupt is triggered.
       *
       * If not 0, we get:
       *   "Error: Unexpected NM Interrupt received from slot: x"
       */
      case 0x10014:
         if (op_type == MTS_READ)
            *data = 0x00;
         vm_clear_irq(d->router->vm,C3600_NM_MGMT_IRQ);
         break;

      /* 
       * Read when an external interrupt is triggered.
       *
       * Bit 4: 1 = %UNKNOWN-1-GT64010: Unknown fatal interrupt(s)
       * Bit 6: 1 = %OIRINT: OIR Event has occurred oir_ctrl 1000 oir_stat FFFF
       *
       * oir_ctrl = register 0x10004
       * oir_stat = register 0x10006
       */
      case 0x2000a:
         if (op_type == MTS_READ)
            *data = 0x54;
         vm_clear_irq(d->router->vm,C3600_EXT_IRQ);
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
void c3600_init_eeprom_groups(c3600_t *router)
{
   int i;

   /* Initialize Mainboard EEPROM */
   router->mb_eeprom_group = eeprom_mb_group;
   router->mb_eeprom_group.eeprom[0] = &router->mb_eeprom;
   router->mb_eeprom.data = NULL;
   router->mb_eeprom.len  = 0;

   /* Initialize NM EEPROM for 3620/3640 */
   router->nm_eeprom_group = eeprom_nm_group;
   router->nm_eeprom_group.eeprom[0] = NULL;

   /* Initialize NM EEPROM for 3660 */
   for(i=0;i<C3600_MAX_NM_BAYS;i++) {
      router->c3660_nm_eeprom_group[i] = eeprom_nm_group;
      router->c3660_nm_eeprom_group[i].eeprom[0] = NULL;
   }
}

/* Shutdown the IO FPGA device */
static void 
dev_c3600_iofpga_shutdown(vm_instance_t *vm,struct c3600_iofpga_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/*
 * dev_c3600_iofpga_init()
 */
int dev_c3600_iofpga_init(c3600_t *router,m_uint64_t paddr,m_uint32_t len)
{
   vm_instance_t *vm = router->vm;
   struct c3600_iofpga_data *d;

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
   d->vm_obj.shutdown = (vm_shutdown_t)dev_c3600_iofpga_shutdown;

   /* Set device properties */
   dev_init(&d->dev);
   d->dev.name      = "io_fpga";
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.priv_data = d;

   switch(router->chassis_driver->chassis_id) {
      case 3620:
      case 3640:
         d->dev.handler = dev_c3620_c3640_iofpga_access;
         break;
      case 3660:
         d->dev.handler = dev_c3660_iofpga_access;
         break;
      default:
         fprintf(stderr,"C3600 '%s': invalid chassis ID %d\n",
                 router->vm->name,router->chassis_driver->chassis_id);
         free(d);
         return(-1);
   }

   /* Map this device to the VM */
   vm_bind_device(router->vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
