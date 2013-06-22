/* 
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * C6k-Sup1a Midplane FPGA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "nmc93cX6.h"
#include "dev_c6sup1.h"

#define DEBUG_UNKNOWN  1
#define DEBUG_ACCESS   1
#define DEBUG_NET_IRQ  1

/* 
 * Function 0xX000:
 *   bit 0: 0:present, 1:absent.
 *   bit 1: power ok (?)
 */
#define SLOT_NOT_PRESENT   0x01
#define SLOT_POWER_OK      0x02

/* 
 * Function 0xX200: requires bit 3 to be set to avoid error about power 
 * convertor failure.
 */
#define SLOT_POWER_CONVERTOR   0x08

/* Midplane FPGA private data */
struct c6sup1_mpfpga_data {
   vm_obj_t vm_obj;
   struct vdevice dev;

   c6sup1_t *router;
   m_uint32_t irq_status;
   m_uint32_t intr_enable;

   /* Slot/function selector */
   u_int slot_sel;

   /* Slot status (up/down) */
   u_int slot_status[C6SUP1_MAX_SLOTS];
};

/* === Definitions for "Backplane" EEPROM (Chassis Clock, VTT, ...) ======= */
#define EEPROM_BP_DOUT  0      /* reg 0x3c */
#define EEPROM_BP_DIN   0      /* reg 0x20 */
#define EEPROM_BP_CLK   1

/* Chip select (CS) bits */
#define EEPROM_BP_CS_CHASSIS   3   /* Chassis (6509,...) */
#define EEPROM_BP_CS_CHASSIS2  4   /* Chassis redundant EEPROM ? */
#define EEPROM_BP_CS_PS1       5   /* Power Supply #1 */
#define EEPROM_BP_CS_PS2       6   /* Power Supply #2 */
#define EEPROM_BP_CS_CLK1      7   /* Clock card #1 */
#define EEPROM_BP_CS_CLK2      8   /* Clock card #2 */
#define EEPROM_BP_CS_VTT1      9   /* VTT #1 */
#define EEPROM_BP_CS_VTT2      10  /* VTT #2 */
#define EEPROM_BP_CS_VTT3      11  /* VTT #3 */

static const struct nmc93cX6_eeprom_def eeprom_bp_def_chassis = {
   EEPROM_BP_CLK, EEPROM_BP_CS_CHASSIS, EEPROM_BP_DIN, EEPROM_BP_DOUT,
};

static const struct nmc93cX6_eeprom_def eeprom_bp_def_chassis2 = {
   EEPROM_BP_CLK, EEPROM_BP_CS_CHASSIS2, EEPROM_BP_DIN, EEPROM_BP_DOUT,
};

static const struct nmc93cX6_eeprom_def eeprom_bp_def_ps1 = {
   EEPROM_BP_CLK, EEPROM_BP_CS_PS1, EEPROM_BP_DIN, EEPROM_BP_DOUT,
};

static const struct nmc93cX6_eeprom_def eeprom_bp_def_ps2 = {
   EEPROM_BP_CLK, EEPROM_BP_CS_PS2, EEPROM_BP_DIN, EEPROM_BP_DOUT,
};

static const struct nmc93cX6_eeprom_def eeprom_bp_def_clk1 = {
   EEPROM_BP_CLK, EEPROM_BP_CS_CLK1, EEPROM_BP_DIN, EEPROM_BP_DOUT,
};

static const struct nmc93cX6_eeprom_def eeprom_bp_def_clk2 = {
   EEPROM_BP_CLK, EEPROM_BP_CS_CLK2, EEPROM_BP_DIN, EEPROM_BP_DOUT,
};

static const struct nmc93cX6_eeprom_def eeprom_bp_def_vtt1 = {
   EEPROM_BP_CLK, EEPROM_BP_CS_VTT1, EEPROM_BP_DIN, EEPROM_BP_DOUT,
};

static const struct nmc93cX6_eeprom_def eeprom_bp_def_vtt2 = {
   EEPROM_BP_CLK, EEPROM_BP_CS_VTT2, EEPROM_BP_DIN, EEPROM_BP_DOUT,
};

static const struct nmc93cX6_eeprom_def eeprom_bp_def_vtt3 = {
   EEPROM_BP_CLK, EEPROM_BP_CS_VTT3, EEPROM_BP_DIN, EEPROM_BP_DOUT,
};

/* Backplane EEPROMs */
static const struct nmc93cX6_group eeprom_bp_group = {
   EEPROM_TYPE_NMC93C56, 9, 0, 
   EEPROM_DORD_REVERSED,
   EEPROM_DOUT_KEEP,
   EEPROM_DEBUG_DISABLED,
   "Backplane EEPROMs",
   {
      &eeprom_bp_def_chassis,
      &eeprom_bp_def_chassis2, 
      &eeprom_bp_def_ps1,
      &eeprom_bp_def_ps2,
      &eeprom_bp_def_clk1,
      &eeprom_bp_def_clk2,
      &eeprom_bp_def_vtt1,
      &eeprom_bp_def_vtt2,
      &eeprom_bp_def_vtt3,
   },
};

/* === Definitions for "Supervisor" EEPROMs (Sup1A,PFC/EARL) ============== */
#define EEPROM_SUP_DOUT    0         /* XXX */
#define EEPROM_SUP_DIN     2
#define EEPROM_SUP_CLK     1
#define EEPROM_SUP_CS      3

#define EEPROM_EARL_DOUT   2         /* XXX */
#define EEPROM_EARL_DIN    9
#define EEPROM_EARL_CLK    10
#define EEPROM_EARL_CS     8

static const struct nmc93cX6_eeprom_def eeprom_sup_def = {
   EEPROM_SUP_CLK, EEPROM_SUP_CS, EEPROM_SUP_DIN, EEPROM_SUP_DOUT,
};

static const struct nmc93cX6_eeprom_def eeprom_earl_def = {
   EEPROM_EARL_CLK, EEPROM_EARL_CS, EEPROM_EARL_DIN, EEPROM_EARL_DOUT,
};

/* Supervisor EEPROMs */
static const struct nmc93cX6_group eeprom_sup_group = {
   EEPROM_TYPE_NMC93C56, 2, 0, 
   EEPROM_DORD_REVERSED,
   EEPROM_DOUT_KEEP,
   EEPROM_DEBUG_DISABLED,
   "Supervisor EEPROMs",
   { &eeprom_sup_def, &eeprom_earl_def },
};

/* === Definitions for "Slot" EEPROM ====================================== */
#define EEPROM_SLOT_DOUT   0   /* reg 0x4c */
#define EEPROM_SLOT_DIN    0   /* reg 0x48 */
#define EEPROM_SLOT_CLK    1
#define EEPROM_SLOT_CS     3

static const struct nmc93cX6_eeprom_def eeprom_slot_def = {
   EEPROM_SLOT_CLK, EEPROM_SLOT_CS, EEPROM_SLOT_DIN, EEPROM_SLOT_DOUT,
};

static const struct nmc93cX6_group eeprom_slot_group = {
   EEPROM_TYPE_NMC93C56, 1, 0, 
   EEPROM_DORD_REVERSED,
   EEPROM_DOUT_KEEP,
   EEPROM_DEBUG_DISABLED,
   "Slot EEPROMs",
   { &eeprom_slot_def },
};

/* ------------------------------------------------------------------------ */

/* Update network interrupt status */
static inline 
void dev_c6sup1_mpfpga_net_update_irq(struct c6sup1_mpfpga_data *d)
{
   if (d->irq_status) {
      vm_set_irq(d->router->vm,C6SUP1_NETIO_IRQ);
   } else {
      vm_clear_irq(d->router->vm,C6SUP1_NETIO_IRQ);
   }
}

/* Trigger a Network IRQ for the specified slot/port */
void dev_c6sup1_mpfpga_net_set_irq(struct c6sup1_mpfpga_data *d,
                                   u_int slot,u_int port)
{
#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"MP_FPGA","setting NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   d->irq_status |= 1 << slot;
   dev_c6sup1_mpfpga_net_update_irq(d);
}

/* Clear a Network IRQ for the specified slot/port */
void dev_c6sup1_mpfpga_net_clear_irq(struct c6sup1_mpfpga_data *d,
                                     u_int slot,u_int port)
{
#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"MP_FPGA","clearing NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   d->irq_status &= ~(1 << slot);
   dev_c6sup1_mpfpga_net_update_irq(d);
}

/*
 * dev_c6sup1_access()
 */
void *dev_c6sup1_mpfpga_access(cpu_gen_t *cpu,struct vdevice *dev,
                               m_uint32_t offset,u_int op_size,u_int op_type,
                               m_uint64_t *data)
{
   struct c6sup1_mpfpga_data *d = dev->priv_data;
   struct nmc93cX6_group *grp;
   u_int i,slot,func;

   if (op_type == MTS_READ)
      *data = 0xFFFFFFFF;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"MP_FPGA",
              "reading reg 0x%x at pc=0x%llx, ra=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),CPU_MIPS64(cpu)->gpr[MIPS_GPR_RA],
              op_size);
   } else {
      cpu_log(cpu,"MP_FPGA",
              "writing reg 0x%x at pc=0x%llx, ra=0x%llx "
              "data=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),CPU_MIPS64(cpu)->gpr[MIPS_GPR_RA],
              *data,op_size);
   }
#endif

   switch(offset) {
      case 0x0c:
      case 0x14:
      case 0x1c:
         if (op_type == MTS_READ)
            *data = 0;
         break;

      case 0x18:
         if (op_type == MTS_READ)
            *data = 0x8000;
         break;

      /* 0x3E80 is written regularly here (watchdog ?) */
      case 0x20004:
         break;

      /* Backplane EEPROMs */
      case 0x000020:
         if (op_type == MTS_WRITE) {
            //m_log("EEPROM","write access(BP): data=0x%4.4llx\n",*data);
            nmc93cX6_write(&d->router->bp_eeprom_group,(u_int)(*data));
         }
         break;

      /* Supervisor EEPROMs */
      case 0x000024:
         if (op_type == MTS_WRITE) {
            //m_log("EEPROM","write access(SUP): data=0x%4.4llx\n",*data);
            nmc93cX6_write(&d->router->sup_eeprom_group,(u_int)(*data));
         }
         break;

      /* Backplane/Supervisor EEPROMs read access */
      case 0x00003C:
         if (op_type == MTS_READ) {
            *data = 0x0000;

            /* Backplane EEPROMs */
            grp = &d->router->bp_eeprom_group;

            for(i=0;i<grp->nr_eeprom;i++) {
               if (nmc93cX6_is_active(grp,i))
                  *data |= nmc93cX6_get_dout(grp,i);
            }

            /* Supervisor EEPROMs */
            grp = &d->router->sup_eeprom_group;

            for(i=0;i<grp->nr_eeprom;i++) {
               if (nmc93cX6_is_active(grp,i))
                  if (nmc93cX6_get_dout(grp,i))
                     *data |= 0xFFFF; //nmc93cX6_get_dout(grp,i);
            }
         }
         break;

      /* Slot selection */
      case 0x000044:
         if (op_type == MTS_WRITE) {
            d->slot_sel = *data;
            slot = (d->slot_sel & 0xF000) >> 12;
            func = (d->slot_sel & 0x0F00) >> 8;

            if (slot <= C6SUP1_MAX_SLOTS) {
               grp = &d->router->slot_eeprom_group;
               grp->eeprom[0] = &d->router->slot_eeprom[slot-1];

               /* mark the slot as powered on */
               if (func == 0x02) {
                  //printf("Marking slot %u as powered ON\n",slot);
                  d->slot_status[slot-1] = TRUE;
               }
            }              
         }
         break;
         
      /* Slot EEPROM write */
      case 0x000048:
         if (op_type == MTS_WRITE)
            nmc93cX6_write(&d->router->slot_eeprom_group,(u_int)(*data));
         break;

      /* Slot EEPROM read */ 
      case 0x00004c:
         if (op_type == MTS_READ) {
            grp = &d->router->slot_eeprom_group;
            slot = (d->slot_sel & 0xF000) >> 12;
            func = (d->slot_sel & 0x0F00) >> 8;
            *data = 0;
            
            switch(func) {
               /* Presence + power ? */
               case 0x00:
                  *data = SLOT_NOT_PRESENT;

                  if (grp->eeprom[0] && grp->eeprom[0]->data) {
                     *data = 0;

                     /* The SUP slot is always powered */
                     if (d->slot_status[slot-1] || 
                         (slot == d->router->sup_slot))
                        *data |= SLOT_POWER_OK;
                  }
                  break;

               case 0x01:
                  *data = 0x0001;

                  if (grp->eeprom[0] && grp->eeprom[0]->data) {
                     *data = 0x0000;
                  }
                  break;

               /* Power-related */
               case 0x02:
                  *data = SLOT_POWER_CONVERTOR;
                  break;

               /* EEPROM reading */
               case 0x05:
                  if (nmc93cX6_is_active(grp,0))
                     *data |= nmc93cX6_get_dout(grp,0);
                  break;

               default:
                  cpu_log(cpu,"MP_FPGA","slot control: unknown func 0x%2.2x\n",
                          func);
            }
         }
         break;

      /* Slot Identification */
      case 0x000004:
         if (op_type == MTS_READ)
            *data = (d->router->sup_slot << 8) | 0x80;
         break;

      /* Unknown: EARL interrupt ? */
      /* 00:00:27: %CPU_MONITOR-3-PEER_EXCEPTION:
         CPU_MONITOR peer has failed due to exception , resetting [0/1] */
      case 0x000050:
         if (op_type == MTS_READ)
            *data = 0; //0xFFFF;
         break;

      case 0x000074:
         if (op_type == MTS_READ)
            *data = 0x0000; //0x3FFF;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"MP_FPGA",
                    "read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,"MP_FPGA",
                    "write to unknown addr 0x%x, value=0x%llx, pc=0x%llx "
                    "(op_size=%u)\n",offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }
	
   return NULL;
}

/* Shutdown the MP FPGA device */
static void 
dev_c6sup1_mpfpga_shutdown(vm_instance_t *vm,struct c6sup1_mpfpga_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);
      
      /* Free the structure itself */
      free(d);
   }
}

/* Initialize EEPROM groups */
void c6sup1_init_eeprom_groups(c6sup1_t *router)
{
   struct nmc93cX6_group *grp;

   router->bp_eeprom_group = eeprom_bp_group;
   router->sup_eeprom_group = eeprom_sup_group;
   router->slot_eeprom_group = eeprom_slot_group;

   /* XXX */
   grp = &router->bp_eeprom_group;
   grp->eeprom[0] = cisco_eeprom_find_c6k("C6K-CHASSIS-6509");
   grp->eeprom[2] = cisco_eeprom_find_c6k("C6K-POWER-1000W");
   grp->eeprom[3] = cisco_eeprom_find_c6k("C6K-POWER-1000W");
   grp->eeprom[6] = cisco_eeprom_find_c6k("C6K-VTT");
   grp->eeprom[7] = cisco_eeprom_find_c6k("C6K-VTT");
   grp->eeprom[8] = cisco_eeprom_find_c6k("C6K-VTT");

   grp = &router->sup_eeprom_group;
   grp->eeprom[0] = cisco_eeprom_find_c6k("C6K-SUP-SUP1A-2GE");
   grp->eeprom[1] = cisco_eeprom_find_c6k("C6K-EARL-PFC1");

   cisco_eeprom_copy(&router->slot_eeprom[0],
                     cisco_eeprom_find_c6k("C6K-SUP-SUP1A-2GE"));
   
   cisco_eeprom_copy(&router->slot_eeprom[8],
                     cisco_eeprom_find_c6k("C6K-LC-WS-X6248"));
}

/* 
 * dev_c6sup1_mpfpga_init()
 */
int dev_c6sup1_mpfpga_init(c6sup1_t *router,m_uint64_t paddr,m_uint32_t len)
{   
   struct c6sup1_mpfpga_data *d;

   /* Allocate private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"MP_FPGA: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->router = router;
   
   vm_object_init(&d->vm_obj);
   d->vm_obj.name = "mp_fpga";
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_c6sup1_mpfpga_shutdown;

   /* Set device properties */
   dev_init(&d->dev);
   d->dev.name      = "mp_fpga";
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_c6sup1_mpfpga_access;
   d->dev.priv_data = d;

   /* Map this device to the VM */
   vm_bind_device(router->vm,&d->dev);
   vm_object_add(router->vm,&d->vm_obj);
   return(0);
}
