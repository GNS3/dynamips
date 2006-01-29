/* 
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Cisco C7200 (Predator) Midplane FPGA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "nmc93c46.h"
#include "dev_c7200_bay.h"

#define DEBUG_UNKNOWN  0

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


/* PA Bay Empty EEPROM */
static unsigned short eeprom_pa_empty[64] = {
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* PA Bay EEPROM definitions */
static struct nmc93c46_group_def eeprom_bay_def[MAX_PA_BAYS] = {
   /* Bay 0 */
   { BAY0_EEPROM_CLOCK_BIT , BAY0_EEPROM_SELECT_BIT,
     BAY0_EEPROM_DIN_BIT   , BAY0_EEPROM_DOUT_BIT,
     eeprom_pa_empty },

   /* Bay 1 */
   { BAY1_EEPROM_CLOCK_BIT , BAY1_EEPROM_SELECT_BIT,
     BAY1_EEPROM_DIN_BIT   , BAY1_EEPROM_DOUT_BIT,
     eeprom_pa_empty },

   /* Bay 2 */
   { BAY2_EEPROM_CLOCK_BIT , BAY2_EEPROM_SELECT_BIT,
     BAY2_EEPROM_DIN_BIT   , BAY2_EEPROM_DOUT_BIT,
     eeprom_pa_empty },

   /* Bay 3 */
   { BAY3_EEPROM_CLOCK_BIT , BAY3_EEPROM_SELECT_BIT,
     BAY3_EEPROM_DIN_BIT   , BAY3_EEPROM_DOUT_BIT,
     eeprom_pa_empty },

   /* Bay 4 */
   { BAY4_EEPROM_CLOCK_BIT , BAY4_EEPROM_SELECT_BIT,
     BAY4_EEPROM_DIN_BIT   , BAY4_EEPROM_DOUT_BIT,
     eeprom_pa_empty },

   /* Bay 5 */
   { BAY5_EEPROM_CLOCK_BIT , BAY5_EEPROM_SELECT_BIT,
     BAY5_EEPROM_DIN_BIT   , BAY5_EEPROM_DOUT_BIT,
     eeprom_pa_empty },

   /* Bay 6 */
   { BAY6_EEPROM_CLOCK_BIT , BAY6_EEPROM_SELECT_BIT,
     BAY6_EEPROM_DIN_BIT   , BAY6_EEPROM_DOUT_BIT,
     eeprom_pa_empty },
};

/* EEPROM group #1 (Bays 0, 1, 3, 4) */
static struct nmc93c46_eeprom eeprom_bays_g1 = {
   4, 0, "PA Bays (Group #1) EEPROM", 0,

   { /* EEPROM group definitions */
      &eeprom_bay_def[0], &eeprom_bay_def[1], &eeprom_bay_def[3], 
      &eeprom_bay_def[4],
   },

   { { 0, 0, 0, 0, 0}, { 0, 0, 0, 0, 0}, { 0, 0, 0, 0, 0}, 
     { 0, 0, 0, 0, 0} },
};

/* EEPROM group #2 (Bays 2, 5, 6) */
static struct nmc93c46_eeprom eeprom_bays_g2 = {
   3, 0, "PA Bays (Group #2) EEPROM", 0, 

   { /* EEPROM group definitions */
      &eeprom_bay_def[2], &eeprom_bay_def[5], &eeprom_bay_def[6], 
   },

   { { 0, 0, 0, 0, 0}, { 0, 0, 0, 0, 0}, { 0, 0, 0, 0, 0} },
};

/* Midplane FPGA */
struct mpfpga_data {
   unsigned int pa_status_reg;
   unsigned int pa_ctrl_reg;
};

/* Set PA EEPROM definition */
int c7200_pa_set_eeprom(u_int pa_bay,struct c7200_eeprom *eeprom)
{
   if (pa_bay >= MAX_PA_BAYS) {
      fprintf(stderr,"c7200_pa_set_eeprom: invalid PA Bay %u.\n",pa_bay);
      return(-1);
   }
   
   eeprom_bay_def[pa_bay].data = eeprom->data;
   eeprom_bay_def[pa_bay].data_len = eeprom->len;
   return(0);
}

/* Check if a bay has a port adapter */
static int c7200_check_bay(u_int pa_bay)
{
   struct nmc93c46_group_def *def;

   if (!pa_bay || (pa_bay >= MAX_PA_BAYS))
      return(0);

   def = &eeprom_bay_def[pa_bay];
   
   if (def->data == eeprom_pa_empty)
      return(0);

   return(1);
}

/* Port Adapter Status */
static void pa_status(struct mpfpga_data *d,u_int op_type,m_uint64_t *data)
{
   u_int res = 0;

   if (op_type == MTS_WRITE) {
      /* uh, write in status register ??? */
      m_log("MP_FPGA","write attempt in PA status register\n");
      return;
   }

   /* PA Power. Bay 0 is always powered */
   res |= PCI_BAY0_5V_OK | PCI_BAY0_3V_OK;
   
   /* We fake power on bays defined by the final user */
   if (c7200_check_bay(1))
      res |= PCI_BAY1_5V_OK | PCI_BAY1_3V_OK;
   
   if (c7200_check_bay(2))
      res |= PCI_BAY2_5V_OK | PCI_BAY2_3V_OK;

   if (c7200_check_bay(3))
      res |= PCI_BAY3_5V_OK | PCI_BAY3_3V_OK;
   
   if (c7200_check_bay(4))
      res |= PCI_BAY4_5V_OK | PCI_BAY4_3V_OK;

   if (c7200_check_bay(5))
      res |= PCI_BAY5_5V_OK | PCI_BAY5_3V_OK;
      
   if (c7200_check_bay(6))
      res |= PCI_BAY6_5V_OK | PCI_BAY6_3V_OK;

   *data = res;
}

/*
 * dev_mpfpga_access()
 */
void *dev_mpfpga_access(cpu_mips_t *cpu,struct vdevice *dev,m_uint32_t offset,
                        u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct mpfpga_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

   switch(offset) {
      case 0x10:  /* interrupt mask, should be done more efficiently */
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

      case 0x18:  /* interrupt mask, should be done more efficiently */
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

      case 0x40:  /* probably the interrupt enable mask */
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

      case 0x48:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

#if 0
      case 0x70:  /* ??? test */
         if (op_type == MTS_READ)
            *data = 0x1234578;
         break;

      /* 
       * this corresponds to err_enable in error message when IRQ 6 is 
       * triggered.
       */
      case 0x78:  
         if (op_type == MTS_READ)
            *data = 0;
         break;
#endif

      case 0x38:   /* TDM status */
         break;

      case 0x50:   /* Port Adapter Status */
         pa_status(d,op_type,data);
         break;
 
      case 0x58:   /* Port Adapter Control */
         if (op_type == MTS_WRITE)
            d->pa_ctrl_reg = *data;
         else
            *data = d->pa_ctrl_reg;
         break;

      case 0x60:   /* EEPROM for PA in slots 0,1,3,4 */
         if (op_type == MTS_WRITE)
            nmc93c46_write(&eeprom_bays_g1,*data);
         else
            *data = nmc93c46_read(&eeprom_bays_g1);
         break;

      case 0x68:   /* EEPROM for PA in slots 2,5,6 */
         if (op_type == MTS_WRITE)
            nmc93c46_write(&eeprom_bays_g2,*data);
         else
            *data = nmc93c46_read(&eeprom_bays_g2);
         break;

      case 0x7b:  /* ??? */
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ)
            m_log("MP_FPGA","read from addr 0x%x\n",offset);
         else
            m_log("MP_FPGA","write to addr 0x%x, value=0x%llx\n",offset,*data);
#endif
   }
	
   return NULL;
}

/* 
 * dev_mpfpga_init()
 */
int dev_mpfpga_init(cpu_group_t *cpu_group,m_uint64_t paddr,m_uint32_t len)
{   
   struct mpfpga_data *d;
   struct vdevice *dev;

   /* Allocate private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"MP_FPGA: out of memory\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));

   /* Create the device itself */
   if (!(dev = dev_create("mp_fpga"))) {
      fprintf(stderr,"MP_FPGA: unable to create device.\n");
      return(-1);
   }

   dev->phys_addr = paddr;
   dev->phys_len  = len;
   dev->handler   = dev_mpfpga_access;
   dev->priv_data = d;

   /* Map this device to all CPU */
   cpu_group_bind_device(cpu_group,dev);
   return(0);
}
