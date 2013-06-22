/*
 * Cisco 3745 simulation platform.
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
#include "dev_c3745.h"

/* Debugging flags */
#define DEBUG_UNKNOWN   1
#define DEBUG_ACCESS    0
#define DEBUG_NET_IRQ   0

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

/* Network IRQ distribution */
struct net_irq_distrib  {
   u_int reg;
   u_int offset;
};

static struct net_irq_distrib net_irq_dist[C3745_MAX_NM_BAYS] = {
   { 0,  0  },  /* Slot 0: reg 0x20, 0x00XX */
   { 1,  0  },  /* Slot 1: reg 0x22, 0x000X */
   { 1,  4  },  /* Slot 2: reg 0x22, 0x00X0 */
   { 1,  8  },  /* Slot 3: reg 0x22, 0x0X00 */
   { 1,  12 },  /* Slot 4: reg 0x22, 0xX000 */
};

/* IO FPGA structure */
struct c3745_iofpga_data {
   vm_obj_t vm_obj;
   struct vdevice dev;
   c3745_t *router;
   
   /* Network IRQ status */
   m_uint16_t net_irq_status[2];

   /* Interrupt mask */
   m_uint16_t intr_mask,io_mask2;

   /* EEPROM select */
   u_int eeprom_select;

   /* WIC select */
   u_int wic_select;
   u_int wic_cmd_pos;
   u_int wic_cmd_valid;
   m_uint16_t wic_cmd[2];
};

/* Motherboard EEPROM definition */
static const struct nmc93cX6_eeprom_def eeprom_mb_def = {
   EEPROM_MB_CLK, EEPROM_MB_CS,
   EEPROM_MB_DIN, EEPROM_MB_DOUT,
};

/* I/O board EEPROM definition */
static const struct nmc93cX6_eeprom_def eeprom_io_def = {
   EEPROM_IO_CLK, EEPROM_IO_CS,
   EEPROM_IO_DIN, EEPROM_IO_DOUT,
};

/* Midplane EEPROM definition */
static const struct nmc93cX6_eeprom_def eeprom_mp_def = {
   EEPROM_MP_CLK, EEPROM_MP_CS,
   EEPROM_MP_DIN, EEPROM_MP_DOUT,
};

/* System EEPROM group */
static const struct nmc93cX6_group eeprom_sys_group = {
   EEPROM_TYPE_NMC93C46, 3, 0, 
   EEPROM_DORD_NORMAL,
   EEPROM_DOUT_HIGH,
   EEPROM_DEBUG_DISABLED,
   "System EEPROM", 
   { &eeprom_mb_def, &eeprom_io_def, &eeprom_mp_def }, 
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
static inline void dev_c3745_iofpga_net_update_irq(struct c3745_iofpga_data *d)
{
   if ((d->net_irq_status[0] != 0xFFFF) || (d->net_irq_status[1] != 0xFFFF)) {
      vm_set_irq(d->router->vm,C3745_NETIO_IRQ);
   } else {
      vm_clear_irq(d->router->vm,C3745_NETIO_IRQ);
   }
}

/* Trigger a Network IRQ for the specified slot/port */
void dev_c3745_iofpga_net_set_irq(struct c3745_iofpga_data *d,
                                  u_int slot,u_int port)
{
   struct net_irq_distrib *irq_dist;

#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"IO_FPGA","setting NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   irq_dist = &net_irq_dist[slot];
   d->net_irq_status[irq_dist->reg] &= ~(1 << (irq_dist->offset + port));
   dev_c3745_iofpga_net_update_irq(d);
}

/* Clear a Network IRQ for the specified slot/port */
void dev_c3745_iofpga_net_clear_irq(struct c3745_iofpga_data *d,
                                    u_int slot,u_int port)
{
   struct net_irq_distrib *irq_dist;

#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"IO_FPGA","clearing NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   irq_dist = &net_irq_dist[slot];
   d->net_irq_status[irq_dist->reg] |= (1 << (irq_dist->offset + port));
   dev_c3745_iofpga_net_update_irq(d);
}

/* Read a WIC EEPROM */
static m_uint16_t dev_c3745_read_wic_eeprom(struct c3745_iofpga_data *d)
{   
   struct cisco_eeprom *eeprom;
   u_int wic_port;
   u_int eeprom_offset;
   m_uint8_t val[2];

   switch(d->wic_select) {
      case 0x1700:
         wic_port = 0x10;
         break;
      case 0x1D00:
         wic_port = 0x20;
         break;
      case 0x3500:
         wic_port = 0x30;
         break;
      default:
         wic_port = 0;
   }

   /* No WIC in slot or no EEPROM: fake an empty EEPROM */
   if (!wic_port || !(eeprom = vm_slot_get_eeprom(d->router->vm,0,wic_port)))
      return(0xFFFF);

   /* EEPROM offset is in the lowest 6 bits */
   eeprom_offset = d->wic_cmd[0] & 0x3F;

   cisco_eeprom_get_byte(eeprom,eeprom_offset,&val[0]);
   cisco_eeprom_get_byte(eeprom,eeprom_offset+1,&val[1]);

   return(((m_uint16_t)val[0] << 8) | val[1]);
}

/*
 * dev_c3745_iofpga_access()
 */
static void *
dev_c3745_iofpga_access(cpu_gen_t *cpu,struct vdevice *dev,
                        m_uint32_t offset,u_int op_size,u_int op_type,
                        m_uint64_t *data)
{
   struct c3745_iofpga_data *d = dev->priv_data;
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
            nmc93cX6_write(&d->router->sys_eeprom_group,(u_int)(*data));
         else
            *data = nmc93cX6_read(&d->router->sys_eeprom_group);
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
            *data = d->net_irq_status[0];
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
            *data = d->net_irq_status[1];
         break;
      
      /* 
       * Read when PA Mgmt IRQ (4) is received.
       * Message: "Error: Unexpected NM Interrupt received from slot: X"
       *
       * Bits 0-1: 0 = Interrupt for slot 1
       * Bits 2-3: 0 = Interrupt for slot 2
       * Bits 4-5: 0 = Interrupt for slot 3
       * Bits 6-7: 0 = Interrupt for slot 4
       */
      case 0x000026:
         if (op_type == MTS_READ)
            *data = 0x00FF;
         break;

      /* 
       * Read when OIR IRQ (6) is received.
       * Bits 0-3: 1 = OIR for slots 1-4
       * Bits 4-8: 1 = OIR "Watchdog" (???) for slots 1-4
       */
      case 0x000028:
         if (op_type == MTS_READ) {
            *data = d->router->oir_status;
         } else {
            d->router->oir_status &= ~(*data);
            vm_clear_irq(d->router->vm,C3745_EXT_IRQ);
         }
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
            nmc93cX6_write(&d->router->nm_eeprom_group[slot],(u_int)(*data));
         else
            *data = nmc93cX6_read(&d->router->nm_eeprom_group[slot]);
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
            
            if (vm_slot_check_eeprom(d->router->vm,1,0))
               *data &= ~0x0100;

            if (vm_slot_check_eeprom(d->router->vm,2,0))
               *data &= ~0x0001;

            if (vm_slot_check_eeprom(d->router->vm,3,0))
               *data &= ~0x1000;

            if (vm_slot_check_eeprom(d->router->vm,4,0))
               *data &= ~0x0010;
         }
         break;

      /* 
       * VWIC/WIC related 
       * Bits 0-2: WIC presence
       */
      case 0x100004:
         if (op_type == MTS_READ) {
           *data = 0xFFFF;

           /* check WIC 0 */
            if (vm_slot_check_eeprom(d->router->vm,0,0x10))
               *data &= ~0x01;

            /* check WIC 1 */
            if (vm_slot_check_eeprom(d->router->vm,0,0x20))
               *data &= ~0x02;

            /* check WIC 2 */
            if (vm_slot_check_eeprom(d->router->vm,0,0x30))
               *data &= ~0x04;
         } else {
            d->wic_select = *data;
         }
         break;

      case 0x100006:
         if (op_type == MTS_READ)
            *data = 0x0004;
         break;

      case 0x100008:
         if (op_type == MTS_READ) {
            if (d->wic_cmd_valid) {
               *data = dev_c3745_read_wic_eeprom(d);
               d->wic_cmd_valid = FALSE;
            } else {
               *data = 0xFFFF;
            }
         } else {
            /* 
             * Store the EEPROM command (in 2 words).
             *
             * For a read, we have:
             *    Word 0: 0x180 (nmc93c46 READ) + offset (6-bits).
             *    Word 1: 0 (no data).
             */
            d->wic_cmd[d->wic_cmd_pos++] = *data;
            
            if (d->wic_cmd_pos == 2) {
               d->wic_cmd_pos = 0;
               d->wic_cmd_valid = TRUE;
            }
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
      router->nm_eeprom_group[i-1].eeprom[0] = NULL;
   }
}

/* Shutdown the IO FPGA device */
static void 
dev_c3745_iofpga_shutdown(vm_instance_t *vm,struct c3745_iofpga_data *d)
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
   struct c3745_iofpga_data *d;

   /* Allocate private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"IO_FPGA: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->router = router;
   d->net_irq_status[0] = 0xFFFF;
   d->net_irq_status[1] = 0xFFFF;

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
