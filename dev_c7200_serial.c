/*  
 * Cisco C7200 (Predator) Simulation Platform.
 * Copyright (C) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Cisco C7200 (Predator) Serial Interfaces (Mueslix).
 *
 * EEPROM types:
 *   - 0x0C: PA-4T+
 *   - 0x0D: PA-8T-V35
 *   - 0x0E: PA-8T-X21
 *   - 0x0F: PA-8T-232
 *   - 0x10: PA-2H (HSSI)
 *   - 0x40: PA-4E1G/120
 * 
 * It seems that the PA-8T is a combination of two PA-4T+.
 *
 * Note: "debug serial mueslix" gives more technical info.
 *
 * TODO: - study what "crc 16", "crc 32", "loopback", ... commands produce
 *         to guess register functions.
 *       - check if encapsulation [frame-relay|hdlc] has an influence.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_c7200_bay.h"

/* Debugging flags */
#define DEBUG_ACCESS     1
#define DEBUG_MII_REGS   0
#define DEBUG_CSR_REGS   0
#define DEBUG_PCI_REGS   0
#define DEBUG_TRANSMIT   0
#define DEBUG_RECEIVE    0

/* PA-8T PCI vendor/product codes */
#define PA8T_PCI_VENDOR_ID   0x1137
#define PA8T_PCI_PRODUCT_ID  0x0001

/* TPU Registers */
#define MUESLIX_TPU_CMD_OFFSET     0x2c24
#define MUESLIX_TPU_CMD_RSP_OFFSET 0x2c2c

/* TPU microcode */
#define MUESLIX_UCODE_OFFSET 0x2000
#define MUESLIX_UCODE_LEN    0x800

/* TPU Xmem and YMem */
#define MUESLIX_XMEM_OFFSET  0x2a00
#define MUESLIX_YMEM_OFFSET  0x2b00
#define MUESLIX_XYMEM_LEN    0x100

/* Mueslix Data */
struct mueslix_data {
   char *name;
   m_uint32_t bay_addr;
   m_uint32_t cbma_addr;
   
   /* PCI device information */
   struct pci_device *pci_dev;

   /* NetIO descriptor */
   netio_desc_t *nio;

   /* Threads used to walk through rxrings and txrings */
   pthread_t rx_thread,tx_thread;

   /* TPU microcode */
   u_long ucode[MUESLIX_UCODE_LEN/4];

   /* TPU Xmem and Ymem */
   u_long xmem[MUESLIX_XYMEM_LEN/4];
   u_long ymem[MUESLIX_XYMEM_LEN/4];
};

/* EEPROM definition */
static unsigned short eeprom_pa_4t_data[64] = {
   0x010C, 0x010F, 0xffff, 0xffff, 0x4906, 0x2E07, 0x0000, 0x0000,
   0x5000, 0x0000, 0x0010, 0x2400, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

static struct c7200_eeprom eeprom_pa_4t = {
   "PA-4T+", eeprom_pa_4t_data, sizeof(eeprom_pa_4t_data)/2,
};

/*
 * dev_mueslix_access()
 */
void *dev_mueslix_access(cpu_mips_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct mueslix_data *d = dev->priv_data;

   /* Returns 0x00000000 if we don't know the offset */
   if (op_type == MTS_READ)
      *data = 0;

   /* Handle microcode access */
   if ((offset >= MUESLIX_UCODE_OFFSET) && 
       (offset < (MUESLIX_UCODE_OFFSET + MUESLIX_UCODE_LEN)))
   {
      if (op_type == MTS_READ)
         *data = d->ucode[offset >> 2];
      else
         d->ucode[offset >> 2] = *data;

      return NULL;
   }

   /* Handle TPU XMem access */
   if ((offset >= MUESLIX_XMEM_OFFSET) && 
       (offset < (MUESLIX_XMEM_OFFSET + MUESLIX_XYMEM_LEN)))
   {
      if (op_type == MTS_READ)
         *data = d->xmem[offset >> 2];
      else
         d->xmem[offset >> 2] = *data;

      return NULL;
   }
  
   /* Handle TPU YMem access */
   if ((offset >= MUESLIX_YMEM_OFFSET) && 
       (offset < (MUESLIX_YMEM_OFFSET + MUESLIX_XYMEM_LEN)))
   {
      if (op_type == MTS_READ)
         *data = d->ymem[offset >> 2];
      else
         d->ymem[offset >> 2] = *data;

      return NULL;
   }

#if DEBUG_ACCESS
   if (op_type == MTS_READ)
      printf("Mueslix: read  access to offset = 0x%x, pc = 0x%llx\n",
             offset,cpu->pc);
   else
      printf("Mueslix: write access to vaddr = 0x%x, pc = 0x%llx, "
             "val = 0x%llx\n",offset,cpu->pc,*data);
#endif
  
   /* Generic case */
   switch(offset) {
      /* this reg is accessed when an interrupt occurs */
      case 0x0:
        if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

      case 0x18:
        if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
        break;

      case 0x48:
        if (op_type == MTS_READ)
            *data = 0x0;
        break;

      case 0x160: /* signals ? */
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

      case 0x164: /* port status */
         if (op_type == MTS_READ)
            *data = 0x73;
         break;

      case 0x190: /* has influence on clock rate */
         if (op_type == MTS_READ)
            *data = 0x11111111;
         break;

      /* cmd reg */
      case MUESLIX_TPU_CMD_OFFSET:
#if DEBUG_ACCESS
         if (op_type == MTS_WRITE)
            printf("Mueslix: cmd_reg = 0x%llx\n",*data);
#endif
         break;

      /* 
       * cmd_rsp reg, it seems that 0xFFFF means OK
       * (seen on a "sh contr se1/0" with "debug serial mueslix" enabled).
       */
      case MUESLIX_TPU_CMD_RSP_OFFSET:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;
   }

   return NULL;
}

/* RX thread */
static void *dev_mueslix_rxthread(void *arg)
{
   struct mueslix_data *d = arg;

   while(1) {
      //dev_dec21140_handle_rxring(d);
      pause();
   }

   return NULL;
}

/* TX thread */
static void *dev_mueslix_txthread(void *arg)
{
   struct mueslix_data *d = arg;

   sleep(60);

   while(1) {
      //dev_dec21140_handle_txring(d);
      //usleep(10000);
      //pause();

      /* interrupt trigger test */
      sleep(1);
#if DEBUG_ACCESS
      printf("Mueslix: trigger interrupt...\n");
#endif
      //pci_dev_trigger_irq(&cpu_mips,d->pci_dev);

   }

   return NULL;
}

/*
 * pci_mueslix_read()
 *
 * For now, only returns 0. Is it a device similar to DEC21140 or something
 * like that ? (CBMA,...)
 */
static m_uint32_t pci_mueslix_read(struct pci_device *dev,int reg)
{
   switch(reg) {
      case 0x08:  /* Rev ID */
         return(0x2800001);

      default:
         return(0);
   }
}

/*
 * dev_c7200_pa_4t_init()
 *
 * Add a PA-4T port adapter into specified slot.
 */
int dev_c7200_pa_4t_init(c7200_t *router,char *name,u_int pa_bay,
                         netio_desc_t *nio)
{
   struct pa_bay_info *bay_info;
   struct pci_device *pci_dev;
   struct mueslix_data *d;
   struct vdevice *dev;

   /* Allocate the private data structure for Mueslix chip */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"%s (Mueslix): out of memory\n",name);
      return(-1);
   }

   memset(d,0,sizeof(*d));

   /* Set the EEPROM */
   c7200_pa_set_eeprom(pa_bay,&eeprom_pa_4t);

   /* Get PCI bus info about this bay */
   if (!(bay_info = c7200_get_pa_bay_info(pa_bay))) {
      fprintf(stderr,"%s: unable to get info for PA bay %u\n",name,pa_bay);
      return(-1);
   }

   /* Add as PCI device */
   pci_dev = pci_dev_add(router->pa_pci_map[pa_bay],name,
                         PA8T_PCI_VENDOR_ID,PA8T_PCI_PRODUCT_ID,
                         bay_info->pci_secondary_bus,0,0,C7200_NETIO_IRQ,NULL,
                         NULL,pci_mueslix_read,NULL);

   if (!pci_dev) {
      fprintf(stderr,"%s (Mueslix): unable to create PCI device.\n",name);
      return(-1);
   }

   /* Create the device itself */
   if (!(dev = dev_create(name))) {
      fprintf(stderr,"%s (Mueslix): unable to create device.\n",name);
      return(-1);
   }

   d->name        = name;
   d->bay_addr    = bay_info->phys_addr;
   d->nio         = nio;
   d->pci_dev     = pci_dev;

   dev->phys_addr = bay_info->phys_addr;
   dev->phys_len  = 0x4000;
   dev->handler   = dev_mueslix_access;
   dev->priv_data = d;

   /* Map this device to all CPU */
   cpu_group_bind_device(router->cpu_group,dev);

   /* create the receive/transmit threads */
   pthread_create(&d->rx_thread,NULL,dev_mueslix_rxthread,d);
   pthread_create(&d->tx_thread,NULL,dev_mueslix_txthread,d);
   return(0);
}
