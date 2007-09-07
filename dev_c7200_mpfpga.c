/* 
 * Cisco router simulation platform.
 * Copyright (c) 2005-2007 Christophe Fillot (cf@utc.fr)
 *
 * Cisco c7200 Midplane FPGA.
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
#include "dev_c7200.h"

#define DEBUG_UNKNOWN  1
#define DEBUG_ACCESS   0
#define DEBUG_NET_IRQ  0
#define DEBUG_OIR      1

/*
 * Definitions for Port Adapter Status.
 */
#define PCI_BAY0_3V_OK    0x00000002      /* IO card 3V */
#define PCI_BAY0_5V_OK    0x00000004      /* IO card 5V */

#define PCI_BAY1_5V_OK    0x00000200      /* Bay 1 5V */
#define PCI_BAY1_3V_OK    0x00000400      /* Bay 1 3V */

#define PCI_BAY2_5V_OK    0x00002000      /* Bay 2 5V */
#define PCI_BAY2_3V_OK    0x00004000      /* Bay 2 3V */

#define PCI_BAY3_5V_OK    0x02000000      /* Bay 3 5V */
#define PCI_BAY3_3V_OK    0x04000000      /* Bay 3 3V */

#define PCI_BAY4_5V_OK    0x00020000      /* Bay 4 5V */
#define PCI_BAY4_3V_OK    0x00040000      /* Bay 4 3V */

#define PCI_BAY5_5V_OK    0x20000000      /* Bay 5 5V */
#define PCI_BAY5_3V_OK    0x40000000      /* Bay 5 3V */

#define PCI_BAY6_5V_OK    0x00200000      /* Bay 6 5V */
#define PCI_BAY6_3V_OK    0x00400000      /* Bay 6 3V */

/*
 * Definitions for EEPROM access (slots 0,1,3,4) (0x60)
 */
#define BAY0_EEPROM_SELECT_BIT	 1
#define BAY0_EEPROM_CLOCK_BIT	 3
#define BAY0_EEPROM_DIN_BIT      4
#define BAY0_EEPROM_DOUT_BIT	 6

#define BAY1_EEPROM_SELECT_BIT   9
#define BAY1_EEPROM_CLOCK_BIT    11
#define BAY1_EEPROM_DIN_BIT	 12
#define BAY1_EEPROM_DOUT_BIT     14

#define BAY3_EEPROM_SELECT_BIT   25
#define BAY3_EEPROM_CLOCK_BIT    27
#define BAY3_EEPROM_DIN_BIT      28
#define BAY3_EEPROM_DOUT_BIT     30

#define BAY4_EEPROM_SELECT_BIT   17
#define BAY4_EEPROM_CLOCK_BIT    19
#define BAY4_EEPROM_DIN_BIT      20
#define BAY4_EEPROM_DOUT_BIT     22

/*
 * Definitions for EEPROM access (slots 2,5,6) (0x68)
 */
#define BAY2_EEPROM_SELECT_BIT   9
#define BAY2_EEPROM_CLOCK_BIT    11
#define BAY2_EEPROM_DIN_BIT      12
#define BAY2_EEPROM_DOUT_BIT     14

#define BAY5_EEPROM_SELECT_BIT   25
#define BAY5_EEPROM_CLOCK_BIT    27
#define BAY5_EEPROM_DIN_BIT      28
#define BAY5_EEPROM_DOUT_BIT     30

#define BAY6_EEPROM_SELECT_BIT   17
#define BAY6_EEPROM_CLOCK_BIT    19
#define BAY6_EEPROM_DIN_BIT      20
#define BAY6_EEPROM_DOUT_BIT     22

/* PA Bay EEPROM definitions */
static const struct nmc93cX6_eeprom_def eeprom_bay_def[C7200_MAX_PA_BAYS] = {
   /* Bay 0 */
   { BAY0_EEPROM_CLOCK_BIT , BAY0_EEPROM_SELECT_BIT,
     BAY0_EEPROM_DIN_BIT   , BAY0_EEPROM_DOUT_BIT,
   },

   /* Bay 1 */
   { BAY1_EEPROM_CLOCK_BIT , BAY1_EEPROM_SELECT_BIT,
     BAY1_EEPROM_DIN_BIT   , BAY1_EEPROM_DOUT_BIT,
   },

   /* Bay 2 */
   { BAY2_EEPROM_CLOCK_BIT , BAY2_EEPROM_SELECT_BIT,
     BAY2_EEPROM_DIN_BIT   , BAY2_EEPROM_DOUT_BIT,
   },

   /* Bay 3 */
   { BAY3_EEPROM_CLOCK_BIT , BAY3_EEPROM_SELECT_BIT,
     BAY3_EEPROM_DIN_BIT   , BAY3_EEPROM_DOUT_BIT,
   },

   /* Bay 4 */
   { BAY4_EEPROM_CLOCK_BIT , BAY4_EEPROM_SELECT_BIT,
     BAY4_EEPROM_DIN_BIT   , BAY4_EEPROM_DOUT_BIT,
   },

   /* Bay 5 */
   { BAY5_EEPROM_CLOCK_BIT , BAY5_EEPROM_SELECT_BIT,
     BAY5_EEPROM_DIN_BIT   , BAY5_EEPROM_DOUT_BIT,
   },

   /* Bay 6 */
   { BAY6_EEPROM_CLOCK_BIT , BAY6_EEPROM_SELECT_BIT,
     BAY6_EEPROM_DIN_BIT   , BAY6_EEPROM_DOUT_BIT,
   },
};

/* EEPROM group #1 (Bays 0, 1, 3, 4) */
static const struct nmc93cX6_group eeprom_bays_g1 = {
   EEPROM_TYPE_NMC93C46, 4, 0, 
   EEPROM_DORD_NORMAL,
   EEPROM_DOUT_HIGH,
   EEPROM_DEBUG_DISABLED,
   "PA Bays (Group #1) EEPROM", 
   { &eeprom_bay_def[0], &eeprom_bay_def[1], 
     &eeprom_bay_def[3], &eeprom_bay_def[4],
   },
};

/* EEPROM group #2 (Bays 2, 5, 6) */
static const struct nmc93cX6_group eeprom_bays_g2 = {
   EEPROM_TYPE_NMC93C46, 3, 0, 
   EEPROM_DORD_NORMAL,
   EEPROM_DOUT_HIGH,
   EEPROM_DEBUG_DISABLED,
   "PA Bays (Group #2) EEPROM",
   { &eeprom_bay_def[2], &eeprom_bay_def[5], &eeprom_bay_def[6] },
};

/* Network IRQ distribution */
struct net_irq_distrib  {
   u_int reg;
   u_int offset;
};

static struct net_irq_distrib net_irq_dist[C7200_MAX_PA_BAYS] = {
   { 0,  0 },  /* Slot 0: reg 0x10, 0x000000XX */
   { 0,  8 },  /* Slot 1: reg 0x10, 0x0000XX00 */
   { 1,  8 },  /* Slot 2: reg 0x18, 0x0000XX00 */
   { 0, 24 },  /* Slot 3: reg 0x10, 0xXX000000 */
   { 0, 16 },  /* Slot 4: reg 0x10, 0x00XX0000 */
   { 1, 24 },  /* Slot 5: reg 0x18, 0xXX000000 */
   { 1, 16 },  /* Slot 6: reg 0x18, 0x00XX0000 */
};

/* Midplane FPGA private data */
struct c7200_mpfpga_data {
   vm_obj_t vm_obj;
   struct vdevice dev;

   c7200_t *router;
   m_uint32_t pa_status_reg;
   m_uint32_t pa_ctrl_reg;

   m_uint32_t net_irq_status[2];
   m_uint32_t net_irq_mask[2];
};

/* Update network interrupt status */
static inline void dev_c7200_mpfpga_net_update_irq(struct c7200_mpfpga_data *d)
{
   int status;

   status = (d->net_irq_status[0] & d->net_irq_mask[0]) ||
      (d->net_irq_status[1] & d->net_irq_mask[1]);
   
   if (status) {
      vm_set_irq(d->router->vm,C7200_NETIO_IRQ);
   } else {
      vm_clear_irq(d->router->vm,C7200_NETIO_IRQ);
   }
}

/* Trigger a Network IRQ for the specified slot/port */
void dev_c7200_mpfpga_net_set_irq(struct c7200_mpfpga_data *d,
                                  u_int slot,u_int port)
{
   struct net_irq_distrib *irq_dist;

#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"MP_FPGA","setting NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   irq_dist = &net_irq_dist[slot];
   d->net_irq_status[irq_dist->reg] |= 1 << (irq_dist->offset + port);
   dev_c7200_mpfpga_net_update_irq(d);
}

/* Clear a Network IRQ for the specified slot/port */
void dev_c7200_mpfpga_net_clear_irq(struct c7200_mpfpga_data *d,
                                    u_int slot,u_int port)
{
   struct net_irq_distrib *irq_dist;

#if DEBUG_NET_IRQ
   vm_log(d->router->vm,"MP_FPGA","clearing NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   irq_dist = &net_irq_dist[slot];
   d->net_irq_status[irq_dist->reg] &= ~(1 << (irq_dist->offset + port));
   dev_c7200_mpfpga_net_update_irq(d);
}

/* Update Port Adapter Status */
static void pa_update_status_reg(struct c7200_mpfpga_data *d)
{
   m_uint32_t res = 0;

   /* PA Power. Bay 0 is always powered */
   res |= PCI_BAY0_5V_OK | PCI_BAY0_3V_OK;

   /* We fake power on bays defined by the final user */
   if (vm_slot_check_eeprom(d->router->vm,1,0))
      res |= PCI_BAY1_5V_OK | PCI_BAY1_3V_OK;
   
   if (vm_slot_check_eeprom(d->router->vm,2,0))
      res |= PCI_BAY2_5V_OK | PCI_BAY2_3V_OK;

   if (vm_slot_check_eeprom(d->router->vm,3,0))
      res |= PCI_BAY3_5V_OK | PCI_BAY3_3V_OK;
   
   if (vm_slot_check_eeprom(d->router->vm,4,0))
      res |= PCI_BAY4_5V_OK | PCI_BAY4_3V_OK;

   if (vm_slot_check_eeprom(d->router->vm,5,0))
      res |= PCI_BAY5_5V_OK | PCI_BAY5_3V_OK;
      
   if (vm_slot_check_eeprom(d->router->vm,6,0))
      res |= PCI_BAY6_5V_OK | PCI_BAY6_3V_OK;

   d->pa_status_reg = res;
}

/*
 * dev_mpfpga_access()
 */
void *dev_c7200_mpfpga_access(cpu_gen_t *cpu,struct vdevice *dev,
                              m_uint32_t offset,u_int op_size,u_int op_type,
                              m_uint64_t *data)
{
   struct c7200_mpfpga_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0x0;

   /* Optimization: this is written regularly */
   if (offset == 0x7b)
      return NULL;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"MP_FPGA","reading reg 0x%x at pc=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),op_size);
   } else {
      cpu_log(cpu,"MP_FPGA",
              "writing reg 0x%x at pc=0x%llx, data=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),*data,op_size);
   }
#endif

   switch(offset) {
      /* Interrupt status for slots 0, 1, 3, 4 */
      case 0x10:
      case 0x11:
      case 0x12:
      case 0x13:
         if (op_type == MTS_READ)
            *data = d->net_irq_status[0];
         break;

      /* Interrupt status for slots 2, 5, 6 */
      case 0x18:
      case 0x19:
      case 0x1a:
      case 0x1b:
         if (op_type == MTS_READ)
            *data = d->net_irq_status[1];
         break;

      /* Interrupt mask for slots 0, 1, 3, 4 */
      case 0x20:
         if (op_type == MTS_READ) {
            *data = d->net_irq_mask[0];
         } else {
            d->net_irq_mask[0] = *data;
            dev_c7200_mpfpga_net_update_irq(d);
         }
         break;

      /* Interrupt mask for slots 2, 5, 6 */
      case 0x28:
         if (op_type == MTS_READ) {
            *data = d->net_irq_mask[1];
         } else {
            d->net_irq_mask[1] = *data;
            dev_c7200_mpfpga_net_update_irq(d);
         }
         break;

      /* 
       * - PCI errors (seen with IRQ 6)
       * - Used when PA Mgmt IRQ is triggered.
       * 
       * If the PA Mgmt IRQ is triggered for an undefined slot, a crash
       * occurs with "Error: Unexpected NM Interrupt received from slot: 6"
       * So, we use the PA status reg as mask to return something safe 
       * (slot order is identical).
       */
      case 0x40:
         if (op_type == MTS_READ)
            *data = 0x66666600 & d->pa_status_reg;

         vm_clear_irq(d->router->vm,C7200_PA_MGMT_IRQ);
         break;

      case 0x48:  /* ??? (test) */
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

      /* 
       * This corresponds to err_stat in error message when IRQ 6 is 
       * triggered.
       *
       * Bit 7 => SRAM error.
       * Bits 1-6 => OIR on slot 1-6
       */
      case 0x70:
         if (op_type == MTS_READ) {
#if DEBUG_OIR
            cpu_log(cpu,"MP_FPGA","reading reg 0x%x at pc=0x%llx, val=0x%x\n",
                    offset,cpu_get_pc(cpu),d->router->oir_status);
#endif
            *data = d->router->oir_status;
            vm_clear_irq(d->router->vm,C7200_OIR_IRQ);
         } else {
#if DEBUG_OIR
            cpu_log(cpu,"MP_FPGA","writing reg 0x%x at pc=0x%llx "
                    "(data=0x%llx)\n",offset,cpu_get_pc(cpu),*data);
#endif
            d->router->oir_status &= ~(*data);
            vm_clear_irq(d->router->vm,C7200_OIR_IRQ);                    
         }
         break;

      /* 
       * This corresponds to err_enable in error message when IRQ 6 is 
       * triggered. No idea of what it really means.
       */
      case 0x78:
         if (op_type == MTS_READ) {
#if DEBUG_OIR
            cpu_log(cpu,"MP_FPGA","reading 0x78 at pc=0x%llx\n",
                    cpu_get_pc(cpu));
#endif
            *data = 0x00;
         } else {
#if DEBUG_OIR
            cpu_log(cpu,"MP_FPGA","writing reg 0x78 at pc=0x%llx "
                  "(data=0x%llx)\n",cpu_get_pc(cpu),*data);
#endif
         }
         break;

      case 0x38:   /* TDM status */
         break;

      case 0x50:   /* Port Adapter Status */
         if (op_type == MTS_READ) {
            pa_update_status_reg(d);
            *data = d->pa_status_reg;
         }
         break;
 
      case 0x58:   /* Port Adapter Control */
         if (op_type == MTS_WRITE)
            d->pa_ctrl_reg = *data;
         else
            *data = d->pa_ctrl_reg;
         break;

      case 0x60:   /* EEPROM for PA in slots 0,1,3,4 */
         if (op_type == MTS_WRITE)
            nmc93cX6_write(&d->router->pa_eeprom_g1,*data);
         else
            *data = nmc93cX6_read(&d->router->pa_eeprom_g1);
         break;

      case 0x68:   /* EEPROM for PA in slots 2,5,6 */
         if (op_type == MTS_WRITE)
            nmc93cX6_write(&d->router->pa_eeprom_g2,*data);
         else
            *data = nmc93cX6_read(&d->router->pa_eeprom_g2);
         break;

      case 0x7b:  /* ??? */
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"MP_FPGA","read from addr 0x%x, pc=0x%llx\n",
                    offset,cpu_get_pc(cpu));
         } else {
            cpu_log(cpu,"MP_FPGA","write to addr 0x%x, value=0x%llx, "
                    "pc=0x%llx\n",offset,*data,cpu_get_pc(cpu));
         }
#endif
   }
	
   return NULL;
}

/* Initialize EEPROM groups */
void c7200_init_mp_eeprom_groups(c7200_t *router)
{
   /* Group 1: bays 0, 1, 3, 4 */
   router->pa_eeprom_g1 = eeprom_bays_g1;
   router->pa_eeprom_g1.eeprom[0] = NULL;
   router->pa_eeprom_g1.eeprom[1] = NULL;
   router->pa_eeprom_g1.eeprom[2] = NULL;
   router->pa_eeprom_g1.eeprom[3] = NULL;

   /* Group 2: bays 2, 5, 6 */
   router->pa_eeprom_g2 = eeprom_bays_g2;
   router->pa_eeprom_g2.eeprom[0] = NULL;
   router->pa_eeprom_g2.eeprom[1] = NULL;
   router->pa_eeprom_g2.eeprom[2] = NULL;
}

/* Shutdown the MP FPGA device */
static void 
dev_c7200_mpfpga_shutdown(vm_instance_t *vm,struct c7200_mpfpga_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);
      
      /* Free the structure itself */
      free(d);
   }
}

/* Create the c7200 Midplane FPGA */
int dev_c7200_mpfpga_init(c7200_t *router,m_uint64_t paddr,m_uint32_t len)
{   
   struct c7200_mpfpga_data *d;

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
   d->vm_obj.shutdown = (vm_shutdown_t)dev_c7200_mpfpga_shutdown;

   /* Set device properties */
   dev_init(&d->dev);
   d->dev.name      = "mp_fpga";
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_c7200_mpfpga_access;
   d->dev.priv_data = d;

   /* Map this device to the VM */
   vm_bind_device(router->vm,&d->dev);
   vm_object_add(router->vm,&d->vm_obj);
   return(0);
}
