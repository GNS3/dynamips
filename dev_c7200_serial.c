/*  
 * Cisco C7200 (Predator) Simulation Platform.
 * Copyright (C) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Serial Interfaces (Mueslix).
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_c7200.h"
#include "dev_c7200_bay.h"

/* Debugging flags */
#define DEBUG_ACCESS     0
#define DEBUG_PCI_REGS   0
#define DEBUG_TRANSMIT   0
#define DEBUG_RECEIVE    0

/* Mueslix PCI vendor/product codes */
#define MUESLIX_PCI_VENDOR_ID   0x1137
#define MUESLIX_PCI_PRODUCT_ID  0x0001

/* Number of channels (4 interfaces) */
#define MUESLIX_NR_CHANNELS  4
#define MUESLIX_CHANNEL_LEN  0x100

/* Addresses of ports */
#define MUESLIX_CHANNEL0_OFFSET  0x100
#define MUESLIX_CHANNEL1_OFFSET  0x200
#define MUESLIX_CHANNEL2_OFFSET  0x300
#define MUESLIX_CHANNEL3_OFFSET  0x400

/* TPU Registers */
#define MUESLIX_TPU_CMD_OFFSET     0x2c24
#define MUESLIX_TPU_CMD_RSP_OFFSET 0x2c2c

/* General and channels registers */
#define MUESLIX_GEN_CHAN_LEN  0x500

/* TPU microcode */
#define MUESLIX_UCODE_OFFSET  0x2000
#define MUESLIX_UCODE_LEN     0x800

/* TPU Xmem and YMem */
#define MUESLIX_XMEM_OFFSET   0x2a00
#define MUESLIX_YMEM_OFFSET   0x2b00
#define MUESLIX_XYMEM_LEN     0x100

/* Maximum packet size */
#define MUESLIX_MAX_PKT_SIZE  2048

/* RX descriptors */
#define MUESLIX_RXDESC_OWN        0x80000000  /* Ownership */
#define MUESLIX_RXDESC_FS         0x40000000  /* First Segment */
#define MUESLIX_RXDESC_LS         0x20000000  /* Last Segment */
#define MUESLIX_RXDESC_OVERRUN    0x10000000  /* Overrun */
#define MUESLIX_RXDESC_IGNORED    0x08000000  /* Ignored */
#define MUESLIX_RXDESC_ABORT      0x04000000  /* Abort */
#define MUESLIX_RXDESC_CRC        0x02000000  /* CRC error */
#define MUESLIX_RXDESC_LEN_MASK   0xfff

/* TX descriptors */
#define MUESLIX_TXDESC_OWN        0x80000000  /* Ownership */
#define MUESLIX_TXDESC_FS         0x40000000  /* First Segment */
#define MUESLIX_TXDESC_LS         0x20000000  /* Last Segment */
#define MUESLIX_TXDESC_SUB        0x00100000  /* Length substractor ? */
#define MUESLIX_TXDESC_SUB_LEN    0x03000000  /* Length substrator ? */
#define MUESLIX_TXDESC_SUB_SHIFT  24
#define MUESLIX_TXDESC_PAD        0x00c00000  /* Sort of padding info ? */
#define MUESLIX_TXDESC_PAD_SHIFT  22

#define MUESLIX_TXDESC_LEN_MASK   0xfff

/* RX Descriptor */
struct rx_desc {
   m_uint32_t rdes[2];
};

/* TX Descriptor */
struct tx_desc {
   m_uint32_t tdes[2];
};

/* Forward declaration of Mueslix data */
typedef struct mueslix_data mueslix_data_t;

/* Mueslix channel */
struct mueslix_channel {
   /* NetIO descriptor */
   netio_desc_t *nio;

   /* Thread used to walk through RX ring */
   pthread_t rx_thread;

   /* physical addresses for start and end of RX/TX rings */
   m_uint32_t rx_start,rx_end,tx_start,tx_end;
  
   /* physical addresses of current RX and TX descriptors */
   m_uint32_t rx_current,tx_current;

   /* Parent mueslix structure */
   mueslix_data_t *parent;
};

/* Mueslix Data */
struct mueslix_data {
   char *name;
   m_uint32_t bay_addr;
   m_uint32_t cbma_addr;
   
   /* TPU options */
   m_uint32_t tpu_options;

   /* "Managing" CPU */
   cpu_mips_t *mgr_cpu;

   /* Virtual device */
   struct vdevice *dev;

   /* PCI device information */
   struct pci_device *pci_dev;

   /* Channels */
   struct mueslix_channel channel[MUESLIX_NR_CHANNELS];

   /* TPU microcode */
   u_char ucode[MUESLIX_UCODE_LEN];

   /* TPU Xmem and Ymem */
   u_char xmem[MUESLIX_XYMEM_LEN];
   u_char ymem[MUESLIX_XYMEM_LEN];
};

/* PA-8T data */
struct pa8t_data {
   struct mueslix_data *mueslix[2];
};

/* Offsets of the 4 channels */
static m_uint32_t channel_offset[MUESLIX_NR_CHANNELS] = {
   MUESLIX_CHANNEL0_OFFSET, MUESLIX_CHANNEL1_OFFSET,
   MUESLIX_CHANNEL2_OFFSET, MUESLIX_CHANNEL3_OFFSET,
};

/* EEPROM definitions */
static unsigned short eeprom_pa_4t_data[64] = {
   0x010C, 0x010F, 0xffff, 0xffff, 0x4906, 0x2E07, 0x0000, 0x0000,
   0x5000, 0x0000, 0x0010, 0x2400, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

static struct c7200_eeprom eeprom_pa_4t = {
   "PA-4T+", eeprom_pa_4t_data, sizeof(eeprom_pa_4t_data)/2,
};

/* EEPROM definition */
static unsigned short eeprom_pa_8t_data[64] = {
   0x010E, 0x010F, 0xffff, 0xffff, 0x4906, 0x2E07, 0x0000, 0x0000,
   0x5000, 0x0000, 0x0010, 0x2400, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

static struct c7200_eeprom eeprom_pa_8t = {
   "PA-8T", eeprom_pa_8t_data, sizeof(eeprom_pa_8t_data)/2,
};

/*
 * Access to channel registers.
 */
void dev_mueslix_chan_access(cpu_mips_t *cpu,struct mueslix_channel *channel,
                             m_uint32_t offset,u_int op_size,u_int op_type,
                             m_uint64_t *data)
{
   switch(offset) {
      case 0x60: /* signals ? */
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

      case 0x64: /* port status - cable type and probably other things */
         if (op_type == MTS_READ)
            *data = 0x7B;
         break;

      case 0x90: /* has influence on clock rate */
         if (op_type == MTS_READ)
            *data = 0x11111111;
         break;

      case 0x80: /* TX start */
         if (op_type == MTS_WRITE)
            channel->tx_start = channel->tx_current = *data;
         else
            *data = channel->tx_start;
         break;

      case 0x84: /* TX end */
         if (op_type == MTS_WRITE)
            channel->tx_end = *data;
         else
            *data = channel->tx_end;
         break;

      case 0x88: /* RX start */
         if (op_type == MTS_WRITE)
            channel->rx_start = channel->rx_current = *data;
         else
            *data = channel->rx_start;
         break;

      case 0x8c: /* RX end */
         if (op_type == MTS_WRITE)
            channel->rx_end = *data;
         else
            *data = channel->rx_end;
         break;
   }
}

/*
 * dev_mueslix_access()
 */
void *dev_mueslix_access(cpu_mips_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct mueslix_data *d = dev->priv_data;
   int i;

#if DEBUG_ACCESS
   if (op_type == MTS_READ)
      m_log("Mueslix","read  access to offset=0x%x, pc=0x%llx, size=%u\n",
            offset,cpu->pc,op_size);
   else
      m_log("Mueslix","write access to offset=0x%x, pc=0x%llx, "
            "val=0x%llx, size=%u\n",offset,cpu->pc,*data,op_size);
#endif

   /* Returns 0 if we don't know the offset */
   if (op_type == MTS_READ)
      *data = 0x00000000;

   /* Handle microcode access */
   if ((offset >= MUESLIX_UCODE_OFFSET) && 
       (offset < (MUESLIX_UCODE_OFFSET + MUESLIX_UCODE_LEN)))
      return(d->ucode + offset - MUESLIX_UCODE_OFFSET);

   /* Handle TPU XMem access */
   if ((offset >= MUESLIX_XMEM_OFFSET) && 
       (offset < (MUESLIX_XMEM_OFFSET + MUESLIX_XYMEM_LEN)))
      return(d->xmem + offset - MUESLIX_XMEM_OFFSET);
  
   /* Handle TPU YMem access */
   if ((offset >= MUESLIX_YMEM_OFFSET) && 
       (offset < (MUESLIX_YMEM_OFFSET + MUESLIX_XYMEM_LEN)))
      return(d->ymem + offset - MUESLIX_YMEM_OFFSET);
  
   /* Handle channel access */
   for(i=0;i<MUESLIX_NR_CHANNELS;i++)
      if ((offset >= channel_offset[i]) && 
          (offset < (channel_offset[i] + MUESLIX_CHANNEL_LEN)))
   {
      dev_mueslix_chan_access(cpu,&d->channel[i],
                              offset - channel_offset[i],
                              op_size,op_type,data);
      return NULL;
   }

   /* Generic case */
   switch(offset) {
      /* this reg is accessed when an interrupt occurs */
      case 0x0:
         if (op_type == MTS_READ) {
           //*data = 0x00000200;            
           *data = 0x000000ff;
         }
         break;

      /* maybe interrupt mask */
      case 0x10:
         if (op_type == MTS_READ)
            *data = 0x2FF;
         break;

      case 0x14:
         if (op_type == MTS_READ)
            *data = 0x9FF;
         break;

      case 0x18:
         if (op_type == MTS_READ)
            *data = 0x7F7F7F7F;
         break;

      case 0x48:
         if (op_type == MTS_READ)
            *data = 0x00000000;
         break;

      case 0x7c:
         if (op_type == MTS_READ)
            *data = 0x492;
         break;

      case 0x2c00:
         if (op_type == MTS_READ)
            *data = d->tpu_options;
         else
            d->tpu_options = *data;
         break;

      /* cmd reg */
      case MUESLIX_TPU_CMD_OFFSET:
#if DEBUG_ACCESS
         if (op_type == MTS_WRITE)
            m_log("Mueslix","cmd_reg = 0x%llx\n",*data);
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

/*
 * Get the address of the next RX descriptor.
 */
static m_uint32_t rxdesc_get_next(struct mueslix_channel *channel,
                                  m_uint32_t rxd_addr)
{
   m_uint32_t nrxd_addr;

   if (rxd_addr == channel->rx_end)
      nrxd_addr = channel->rx_start;
   else
      nrxd_addr = rxd_addr + sizeof(struct rx_desc);

   return(nrxd_addr);
}

/* Read an RX descriptor */
static void rxdesc_read(struct mueslix_data *d,m_uint32_t rxd_addr,
                        struct rx_desc *rxd)
{
#if DEBUG_RECEIVE
   m_log(d->name,"reading RX descriptor at address 0x%x\n",rxd_addr);
#endif

   /* get the next descriptor from VM physical RAM */
   physmem_copy_from_vm(d->mgr_cpu,rxd,rxd_addr,sizeof(struct rx_desc));

   /* byte-swapping */
   rxd->rdes[0] = vmtoh32(rxd->rdes[0]);
   rxd->rdes[1] = vmtoh32(rxd->rdes[1]);
}

/* 
 * Try to acquire the specified RX descriptor. Returns TRUE if we have it.
 * It assumes that the byte-swapping is done.
 */
static inline int rxdesc_acquire(m_uint32_t rdes0)
{
   return(rdes0 & MUESLIX_RXDESC_OWN);
}

/* Put a packet in buffer of a descriptor */
static ssize_t rxdesc_put_pkt(struct mueslix_data *d,struct rx_desc *rxd,
                              u_char **pkt,ssize_t *pkt_len)
{
   ssize_t len,cp_len;

   len = rxd->rdes[0] & MUESLIX_RXDESC_LEN_MASK;

   /* compute the data length to copy */
   cp_len = m_min(len,*pkt_len);

#if DEBUG_RECEIVE
   m_log(d->name,"copying %d bytes at 0x%x\n",cp_len,rxd->rdes[1]);
#endif
      
   /* copy packet data to the VM physical RAM */
   physmem_copy_to_vm(d->mgr_cpu,*pkt,rxd->rdes[1],cp_len);
      
   *pkt += cp_len;
   *pkt_len -= cp_len;
   return(cp_len);
}

/*
 * Put a packet in the RX ring of the Mueslix specified channel.
 */
static void dev_mueslix_receive_pkt(struct mueslix_channel *channel,
                                    u_char *pkt,ssize_t pkt_len)
{
   struct mueslix_data *d = channel->parent;
   m_uint32_t rx_start,rxdn_addr,rxdn_rdes0;
   struct rx_desc rxd0,rxdn,*rxdc;
   ssize_t cp_len,tot_len = pkt_len;
   u_char *pkt_ptr = pkt;
   int i;

   if (channel->rx_start == 0)
      return;

   /* Truncate the packet if it is too big */
   pkt_len = m_min(pkt_len,MUESLIX_MAX_PKT_SIZE);

   /* Copy the current rxring descriptor */
   rxdesc_read(d,channel->rx_current,&rxd0);
   
   /* We must have the first descriptor... */
   if (!rxdesc_acquire(rxd0.rdes[0]))
      return;

   /* Remember the first RX descriptor address */
   rx_start = channel->rx_current;

   for(i=0,rxdc=&rxd0;tot_len>0;i++)
   {
      /* Put data into the descriptor buffers */
      cp_len = rxdesc_put_pkt(d,rxdc,&pkt_ptr,&tot_len);

      /* Get address of the next descriptor */
      rxdn_addr = rxdesc_get_next(channel,channel->rx_current);

      /* We have finished if the complete packet has been stored */
      if (tot_len == 0) {
         rxdc->rdes[0] = MUESLIX_RXDESC_LS;
         rxdc->rdes[0] |= cp_len;

         if (i != 0)
            physmem_copy_u32_to_vm(d->mgr_cpu,channel->rx_current,
                                   rxdc->rdes[0]);

         channel->rx_current = rxdn_addr;
         break;
      }

#if DEBUG_RECEIVE
      m_log(d->name,"trying to acquire new descriptor at 0x%x\n",rxdn_addr);
#endif

      /* Get status of the next descriptor to see if we can acquire it */
      rxdn_rdes0 = physmem_copy_u32_from_vm(d->mgr_cpu,rxdn_addr);

      if (!rxdesc_acquire(rxdn_rdes0))
         rxdc->rdes[0] = MUESLIX_RXDESC_LS | MUESLIX_RXDESC_OVERRUN;
      else
         rxdc->rdes[0] = 0x00000000;  /* ok, no special flag */

      rxdc->rdes[0] |= cp_len;

      /* Update the new status (only if we are not on the first desc) */
      if (i != 0)
         physmem_copy_u32_to_vm(d->mgr_cpu,channel->rx_current,rxdc->rdes[0]);

      /* Update the RX pointer */
      channel->rx_current = rxdn_addr;

      if (rxdc->rdes[0] & MUESLIX_RXDESC_LS)
         break;

      /* Read the next descriptor from VM physical RAM */
      rxdesc_read(d,rxdn_addr,&rxdn);
      rxdc = &rxdn;
   }

   /* Update the first RX descriptor */
   rxd0.rdes[0] |= MUESLIX_RXDESC_FS;
   physmem_copy_u32_to_vm(d->mgr_cpu,rx_start,rxd0.rdes[0]);

   /* Indicate that we have a frame ready (XXX something to do ?) */

   /* Generate IRQ on CPU */
   pci_dev_trigger_irq(d->mgr_cpu,d->pci_dev);
}

/* Handle the Mueslix RX ring of the specified channel */
static void dev_mueslix_handle_chan_rxring(struct mueslix_channel *channel)
{      
   u_char pkt[MUESLIX_MAX_PKT_SIZE];
   ssize_t pkt_len;

   pkt_len = netio_recv(channel->nio,pkt,MUESLIX_MAX_PKT_SIZE);

   if (pkt_len < 0) {
      m_log(channel->parent->name,"net_io RX failed %s\n",strerror(errno));
      return;
   }

#if DEBUG_RECEIVE
   m_log(channel->parent->name,"receiving a packet of %d bytes\n",pkt_len);
   mem_dump(log_file,pkt,pkt_len);
#endif

   dev_mueslix_receive_pkt(channel,pkt,pkt_len);
}

/* Read a TX descriptor */
static void txdesc_read(struct mueslix_data *d,m_uint32_t txd_addr,
                        struct tx_desc *txd)
{
   /* get the next descriptor from VM physical RAM */
   physmem_copy_from_vm(d->mgr_cpu,txd,txd_addr,sizeof(struct tx_desc));

   /* byte-swapping */
   txd->tdes[0] = vmtoh32(txd->tdes[0]);
   txd->tdes[1] = vmtoh32(txd->tdes[1]);
}

/* Set the address of the next TX descriptor */
static void txdesc_set_next(struct mueslix_channel *channel)
{
   if (channel->tx_current == channel->tx_end)
      channel->tx_current = channel->tx_start;
   else
      channel->tx_current += sizeof(struct tx_desc);
}

/* Handle the TX ring of a specific channel */
static int dev_mueslix_handle_chan_txring(struct mueslix_channel *channel)
{
   struct mueslix_data *d = channel->parent;
   u_char pkt[MUESLIX_MAX_PKT_SIZE],*pkt_ptr;
   m_uint32_t tx_start,clen,sub_len,tot_len,pad;
   struct tx_desc txd0,ctxd,*ptxd;
   int done = FALSE;

   if ((channel->tx_start == 0) || (channel->nio == NULL))
      return(FALSE);

   /* Copy the current txring descriptor */
   tx_start = channel->tx_current;   
   ptxd = &txd0;
   txdesc_read(d,channel->tx_current,ptxd);

   /* If the we don't own the descriptor, we cannot transmit */
   if (!(txd0.tdes[0] & MUESLIX_TXDESC_OWN))
      return(FALSE);

#if DEBUG_TRANSMIT
   m_log(d->name,"mueslix_handle_txring: 1st desc: "
         "tdes[0]=0x%x, tdes[1]=0x%x\n",ptxd->tdes[0],ptxd->tdes[1]);
#endif

   pkt_ptr = pkt;
   tot_len = 0;

   do {
#if DEBUG_TRANSMIT
      m_log(d->name,"mueslix_handle_txring: loop: "
            "tdes[0]=0x%x, tdes[1]=0x%x\n",ptxd->tdes[0],ptxd->tdes[1]);
#endif

      if (!(ptxd->tdes[0] & MUESLIX_TXDESC_OWN)) {
         m_log(d->name,"mueslix_handle_txring: descriptor not owned!\n");
         return(FALSE);
      }

      clen = (ptxd->tdes[0] & MUESLIX_TXDESC_LEN_MASK) << 2;

      if (ptxd->tdes[0] & MUESLIX_TXDESC_SUB) {
         sub_len = ptxd->tdes[0] & MUESLIX_TXDESC_SUB_LEN;
         sub_len >>= MUESLIX_TXDESC_SUB_SHIFT;
         clen -= sub_len;
      }
      
      /* Be sure that we have length not null */
      if (clen != 0)
         physmem_copy_from_vm(d->mgr_cpu,pkt_ptr,ptxd->tdes[1],clen);

      pkt_ptr += clen;
      tot_len += clen;

      /* Clear the OWN bit if this is not the first descriptor */
      if (!(ptxd->tdes[0] & MUESLIX_TXDESC_FS))
         physmem_copy_u32_to_vm(d->mgr_cpu,channel->tx_current,0);

      /* Go to the next descriptor */
      txdesc_set_next(channel);

      /* Copy the next txring descriptor */
      if (!(ptxd->tdes[0] & MUESLIX_TXDESC_LS)) {
         txdesc_read(d,channel->tx_current,&ctxd);
         ptxd = &ctxd;
      } else
         done = TRUE;
   }while(!done);

   if (tot_len != 0) {
#if DEBUG_TRANSMIT
      m_log(d->name,"sending of packet of %u bytes (flags=0x%4.4x)\n",
            tot_len,txd0.tdes[0]);
      mem_dump(log_file,pkt,tot_len);
#endif

      pad = ptxd->tdes[0] & MUESLIX_TXDESC_PAD;
      pad >>= MUESLIX_TXDESC_PAD_SHIFT;
      tot_len += (pad - 1) & 0x03;

      /* send it on wire */
      netio_send(channel->nio,pkt,tot_len);
   }

   /* Clear the OWN flag of the first descriptor */
   physmem_copy_u32_to_vm(d->mgr_cpu,tx_start,0);

   /* Interrupt on completion ? */
   pci_dev_trigger_irq(d->mgr_cpu,d->pci_dev);   
   return(TRUE);
}

/* Handle the TX ring of all channels */
static int dev_mueslix_handle_txring(struct mueslix_data *d)
{
   int i;

   for(i=0;i<MUESLIX_NR_CHANNELS;i++)
      dev_mueslix_handle_chan_txring(&d->channel[i]);
   
   return(0);
}

/* RX thread */
static void *dev_mueslix_rxthread(void *arg)
{
   struct mueslix_channel *channel = arg;

   for(;;) dev_mueslix_handle_chan_rxring(channel);
   return NULL;
}

/* pci_mueslix_read() */
static m_uint32_t pci_mueslix_read(struct pci_device *dev,int reg)
{
   switch(reg) {
      case 0x08:  /* Rev ID */
         return(0x2800001);

      default:
         return(0);
   }
}

/* Initialize a channel with the specified NIO descriptor */
static int dev_mueslix_init_channel(struct mueslix_data *d,u_int channel_id,
                                    netio_desc_t *nio)
{
   struct mueslix_channel *channel;

   assert(channel_id < MUESLIX_NR_CHANNELS);

   channel = &d->channel[channel_id];

   channel->nio = nio;
   channel->parent = d;

   /* create the RX thread */
   pthread_create(&channel->rx_thread,NULL,dev_mueslix_rxthread,channel);
   return(0);
}

/* Initialize a Mueslix chip */
struct mueslix_data *dev_mueslix_init(cpu_group_t *cpu_group,char *name,
                                      m_uint32_t phys_addr,m_uint32_t phys_len,
                                      struct pci_data *pci_data,
                                      int pci_bus,int pci_device,int irq)
{
   struct pci_device *pci_dev;
   struct mueslix_data *d;
   struct vdevice *dev;
   cpu_mips_t *cpu0;

   /* Device is managed by CPU0 */ 
   cpu0 = cpu_group_find_id(cpu_group,0);

   /* Allocate the private data structure for Mueslix chip */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"%s (Mueslix): out of memory\n",name);
      return NULL;
   }

   memset(d,0,sizeof(*d));

   /* Add as PCI device */
   pci_dev = pci_dev_add(pci_data,name,
                         MUESLIX_PCI_VENDOR_ID,MUESLIX_PCI_PRODUCT_ID,
                         pci_bus,pci_device,0,irq,
                         d,NULL,pci_mueslix_read,NULL);

   if (!pci_dev) {
      fprintf(stderr,"%s (Mueslix): unable to create PCI device.\n",name);
      return NULL;
   }

   /* Create the device itself */
   if (!(dev = dev_create(name))) {
      fprintf(stderr,"%s (Mueslix): unable to create device.\n",name);
      return NULL;
   }

   d->name        = name;
   d->bay_addr    = phys_addr;
   d->pci_dev     = pci_dev;
   d->mgr_cpu     = cpu0;

   dev->phys_addr = phys_addr;
   dev->phys_len  = phys_len;
   dev->handler   = dev_mueslix_access;
   dev->priv_data = d;

   /* Store device info */
   dev->priv_data = d;
   d->dev = dev;

   /* Map this device to all CPU */
   cpu_group_bind_device(cpu_group,dev);

   /* Start the TX ring scanner */
   ptask_add((ptask_callback)dev_mueslix_handle_txring,d,NULL);
   return(d);
}

/*
 * dev_c7200_pa_4t_init()
 *
 * Add a PA-4T port adapter into specified slot.
 */
int dev_c7200_pa_4t_init(c7200_t *router,char *name,u_int pa_bay)
{
   struct pa_bay_info *bay_info;
   struct mueslix_data *data;

   /* Set the EEPROM */
   c7200_pa_set_eeprom(pa_bay,&eeprom_pa_4t);

   /* Get PCI bus info about this bay */
   if (!(bay_info = c7200_get_pa_bay_info(pa_bay))) {
      fprintf(stderr,"%s: unable to get info for PA bay %u\n",name,pa_bay);
      return(-1);
   }

   /* Create the Mueslix chip */
   data = dev_mueslix_init(router->cpu_group,name,
                           bay_info->phys_addr,0x4000,
                           router->pa_bay[pa_bay].pci_map,
                           bay_info->pci_secondary_bus,0,
                           C7200_NETIO_IRQ);
   if (!data) return(-1);

   /* Store device info into the router structure */
   return(c7200_set_slot_drvinfo(router,pa_bay,data));
}

/* Bind a Network IO descriptor to a specific port */
int dev_c7200_pa_4t_set_nio(c7200_t *router,u_int pa_bay,u_int port_id,
                            netio_desc_t *nio)
{
   struct mueslix_data *data;

   if ((port_id >= MUESLIX_NR_CHANNELS) || 
       !(data = c7200_get_slot_drvinfo(router,pa_bay)))
      return(-1);

   dev_mueslix_init_channel(data,port_id,nio);
   return(0);
}

/*
 * dev_c7200_pa_8t_init()
 *
 * Add a PA-8T port adapter into specified slot.
 */
int dev_c7200_pa_8t_init(c7200_t *router,char *name,u_int pa_bay)
{
   struct pa_bay_info *bay_info;
   struct pa8t_data *data;

   /* Allocate the private data structure for the PA-8T */
   if (!(data = malloc(sizeof(*data)))) {
      fprintf(stderr,"%s (PA-8T): out of memory\n",name);
      return(-1);
   }

   /* Set the EEPROM */
   c7200_pa_set_eeprom(pa_bay,&eeprom_pa_8t);

   /* Get PCI bus info about this bay */
   if (!(bay_info = c7200_get_pa_bay_info(pa_bay))) {
      fprintf(stderr,"%s: unable to get info for PA bay %u\n",name,pa_bay);
      return(-1);
   }

   /* Create the 1st Mueslix chip */
   data->mueslix[0] = dev_mueslix_init(router->cpu_group,name,
                                       bay_info->phys_addr,0x4000,
                                       router->pa_bay[pa_bay].pci_map,
                                       bay_info->pci_secondary_bus,0,
                                       C7200_NETIO_IRQ);
   if (!data->mueslix[0]) return(-1);

   /* Create the 2nd Mueslix chip */
   data->mueslix[1] = dev_mueslix_init(router->cpu_group,name,
                                       bay_info->phys_addr+0x20000,0x4000,
                                       router->pa_bay[pa_bay].pci_map,
                                       bay_info->pci_secondary_bus,1,
                                       C7200_NETIO_IRQ);
   if (!data->mueslix[1]) return(-1);

   /* Store device info into the router structure */
   return(c7200_set_slot_drvinfo(router,pa_bay,data));
}

/* Bind a Network IO descriptor to a specific port */
int dev_c7200_pa_8t_set_nio(c7200_t *router,u_int pa_bay,u_int port_id,
                            netio_desc_t *nio)
{
   struct pa8t_data *data;

   if ((port_id >= (MUESLIX_NR_CHANNELS*2)) || 
       !(data = c7200_get_slot_drvinfo(router,pa_bay)))
      return(-1);

   dev_mueslix_init_channel(data->mueslix[port_id>>2],port_id&0x03,nio);
   return(0);
}
