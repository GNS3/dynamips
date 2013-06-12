/*  
 * Cisco router simulation platform.
 * Copyright (C) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Serial Interfaces (Mueslix).
 *
 * Note: "debug serial mueslix" gives more technical info.
 *
 * Chip mode: Cisco models 36xx and 72xx don't seem to use the same microcode,
 * so there are code variants to make things work properly.
 *
 *  Chip mode 0 => 3600
 *  Chip mode 1 => 7200
 *
 * 2 points noticed until now: 
 *    - RX/TX ring wrapping checks are done differently,
 *    - TX packet sizes are not specified in the same way.
 *
 * Test methodology:
 *    - Connect two virtual routers together ;
 *    - Do pings by sending 10 packets by 10 packets. If this stops working,
 *      count the number of transmitted packets and check with RX/TX rings
 *      sizes. This is problably a ring wrapping problem.
 *    - Do multiple pings with various sizes (padding checks);
 *    - Check if CDP is working, with various hostname sizes. Since CDP
 *      contains a checksum, it is a good way to determine if packets are
 *      sent/received correctly.
 *    - Do a Telnet from both virtual router to the other one, and do a
 *      "sh run".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_mueslix.h"

/* Debugging flags */
#define DEBUG_ACCESS     0
#define DEBUG_UNKNOWN    0
#define DEBUG_PCI_REGS   0
#define DEBUG_TRANSMIT   0
#define DEBUG_RECEIVE    0

/* Mueslix PCI vendor/product codes */
#define MUESLIX_PCI_VENDOR_ID   0x1137
#define MUESLIX_PCI_PRODUCT_ID  0x0001

/* Number of channels (4 interfaces) */
#define MUESLIX_NR_CHANNELS  4
#define MUESLIX_CHANNEL_LEN  0x100

/* RX/TX status for a channel */
#define MUESLIX_CHANNEL_STATUS_RX  0x01
#define MUESLIX_CHANNEL_STATUS_TX  0x02

/* RX/TX enable masks (XXX check if bit position is correct) */
#define MUESLIX_TX_ENABLE   0x01
#define MUESLIX_RX_ENABLE   0x02

/* RX/TX IRQ masks */
#define MUESLIX_TX_IRQ   0x01
#define MUESLIX_RX_IRQ   0x10

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
#define MUESLIX_MAX_PKT_SIZE  18000

/* Send up to 16 packets in a TX ring scan pass */
#define MUESLIX_TXRING_PASS_COUNT  16

/* RX descriptors */
#define MUESLIX_RXDESC_OWN        0x80000000  /* Ownership */
#define MUESLIX_RXDESC_FS         0x40000000  /* First Segment */
#define MUESLIX_RXDESC_LS         0x20000000  /* Last Segment */
#define MUESLIX_RXDESC_OVERRUN    0x10000000  /* Overrun */
#define MUESLIX_RXDESC_IGNORED    0x08000000  /* Ignored */
#define MUESLIX_RXDESC_ABORT      0x04000000  /* Abort */
#define MUESLIX_RXDESC_CRC        0x02000000  /* CRC error */
#define MUESLIX_RXDESC_LEN_MASK   0xffff

/* TX descriptors */
#define MUESLIX_TXDESC_OWN        0x80000000  /* Ownership */
#define MUESLIX_TXDESC_FS         0x40000000  /* First Segment */
#define MUESLIX_TXDESC_LS         0x20000000  /* Last Segment */
#define MUESLIX_TXDESC_SUB        0x00100000  /* Length substractor ? */
#define MUESLIX_TXDESC_SUB_LEN    0x03000000  /* Length substrator ? */
#define MUESLIX_TXDESC_SUB_SHIFT  24
#define MUESLIX_TXDESC_PAD        0x00c00000  /* Sort of padding info ? */
#define MUESLIX_TXDESC_PAD_SHIFT  22

#define MUESLIX_TXDESC_LEN_MASK   0xffff

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
   /* Channel ID */
   u_int id;

   /* Channel status (0=disabled) */
   u_int status;

   /* Clock parameters */
   u_int clk_shift,clk_div;
   u_int clk_rate;

   /* CRC control register */
   u_int crc_ctrl_reg;

   /* CRC size */
   u_int crc_size;

   /* NetIO descriptor */
   netio_desc_t *nio;

   /* TX ring scanners task id */
   ptask_id_t tx_tid;

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

   /* Lock */
   pthread_mutex_t lock;

   /* IRQ status and mask */
   m_uint32_t irq_status,irq_mask;
   u_int irq_clearing_count;

   /* TPU options */
   m_uint32_t tpu_options;

   /* Virtual machine */
   vm_instance_t *vm;

   /* Virtual device */
   struct vdevice *dev;

   /* PCI device information */
   struct pci_device *pci_dev;

   /* Chip mode: 
    *
    * 0=increment ring pointers before check + direct TX size,
    * 1=increment ring pointers after check  + "complex" TX size.
    */
   int chip_mode;

   /* Channels */
   struct mueslix_channel channel[MUESLIX_NR_CHANNELS];
   m_uint32_t channel_enable_mask;

   /* TPU microcode */
   u_char ucode[MUESLIX_UCODE_LEN];

   /* TPU Xmem and Ymem */
   u_char xmem[MUESLIX_XYMEM_LEN];
   u_char ymem[MUESLIX_XYMEM_LEN];
};

/* Offsets of the 4 channels */
static m_uint32_t channel_offset[MUESLIX_NR_CHANNELS] = {
   MUESLIX_CHANNEL0_OFFSET, MUESLIX_CHANNEL1_OFFSET,
   MUESLIX_CHANNEL2_OFFSET, MUESLIX_CHANNEL3_OFFSET,
};

/* Lock/Unlock primitives */
#define MUESLIX_LOCK(d)    pthread_mutex_lock(&(d)->lock)
#define MUESLIX_UNLOCK(d)  pthread_mutex_unlock(&(d)->lock)

/* Log a Mueslix message */
#define MUESLIX_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* Returns TRUE if RX/TX is enabled for a channel */
static inline int dev_mueslix_is_rx_tx_enabled(struct mueslix_data *d,u_int id)
{
   /* 2 bits for RX/TX, 4 channels max */
   return((d->channel_enable_mask >> (id << 1)) & 0x03);
}

/* Update IRQ status */
static inline void dev_mueslix_update_irq_status(struct mueslix_data *d)
{
   if (d->irq_status & d->irq_mask)
      pci_dev_trigger_irq(d->vm,d->pci_dev);
   else {
      if (++d->irq_clearing_count == 3) {
         pci_dev_clear_irq(d->vm,d->pci_dev);
         d->irq_clearing_count = 0;
      }
   }
}

/* Compute clock rate */
static void dev_mueslix_update_clk_rate(struct mueslix_channel *channel)
{
   u_int clk_shift = channel->clk_shift;
   
   if (clk_shift == 8)
      clk_shift = 0;
      
   channel->clk_rate = (8064000 >> clk_shift) / (channel->clk_div + 1);
   MUESLIX_LOG(channel->parent,"channel %u: clock rate set to %u\n",
               channel->id,channel->clk_rate);

   /* Apply the bandwidth constraint to the NIO */
   if (channel->nio != NULL)
      netio_set_bandwidth(channel->nio,(channel->clk_rate+1000)/1000);
}

/*
 * Access to channel registers.
 */
void dev_mueslix_chan_access(cpu_gen_t *cpu,struct mueslix_channel *channel,
                             m_uint32_t offset,u_int op_size,u_int op_type,
                             m_uint64_t *data)
{
   switch(offset) {
      case 0x00: /* CRC control register ? */
         if (op_type == MTS_READ) {
            *data = channel->crc_ctrl_reg;
         } else {
            channel->crc_ctrl_reg = *data;
            
            switch(channel->crc_ctrl_reg) {
               case 0x08:
               case 0x0a:
                  channel->crc_size = channel->crc_ctrl_reg - 0x06;
                  break;

               default:
                  MUESLIX_LOG(channel->parent,"channel %u: unknown value "
                              "for CRC ctrl reg 0x%4.4x\n",
                              channel->id,channel->crc_ctrl_reg);

                  channel->crc_size = 2;
            }
            MUESLIX_LOG(channel->parent,
                        "channel %u: CRC size set to 0x%4.4x\n",
                        channel->id,channel->crc_size);
         }
         break;

      case 0x40:
         if (op_type == MTS_READ)
            *data = channel->clk_shift;
         else
            channel->clk_shift = *data;
            
         /* Recompute clock rate */
         dev_mueslix_update_clk_rate(channel);
         break;

      case 0x44:
         if (op_type == MTS_READ)
            *data = channel->clk_div;
         else
            channel->clk_div = *data;
         break;

      case 0x60: /* signals ? */
         if ((op_type == MTS_READ) && (channel->nio != NULL))
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

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_WRITE) {
            MUESLIX_LOG(channel->parent,"channel %u: "
                        "write to unknown addr 0x%4.4x, value=0x%llx\n",
                        channel->id,offset,*data);
         }
#endif
   }
}

/* Handle TPU commands for chip mode 0 (3600) */
static void tpu_cm0_handle_cmd(struct mueslix_data *d,u_int cmd)
{   
   struct mueslix_channel *channel;
   u_int opcode,channel_id;

   opcode = (cmd >> 12) & 0xFF;
   channel_id = cmd & 0x03;
   channel = &d->channel[channel_id];

   switch(opcode) {
      case 0x10:
         MUESLIX_LOG(d,"channel %u disabled\n",channel_id);
         channel->status = 0;
         break;
      case 0x00:
         MUESLIX_LOG(d,"channel %u enabled\n",channel_id);
         channel->status = 1;
         break;
      default:
         MUESLIX_LOG(d,"unknown command 0x%5x\n",cmd);
   }
}

/* Handle TPU commands for chip mode 1 (7200) */
static void tpu_cm1_handle_cmd(struct mueslix_data *d,u_int cmd)
{   
   struct mueslix_channel *channel;
   u_int opcode,channel_id;

   opcode = (cmd >> 12) & 0xFF;
   channel_id = cmd & 0x03;
   channel = &d->channel[channel_id];

   switch(opcode) {
      case 0x50:
      case 0x30: 
         MUESLIX_LOG(d,"channel %u disabled\n",channel_id);
         channel->status = 0;
         break;
      case 0x00:
         MUESLIX_LOG(d,"channel %u enabled\n",channel_id);
         channel->status = 1;
         break;
      default:
         MUESLIX_LOG(d,"unknown command 0x%5x\n",cmd);
   }
}

/*
 * dev_mueslix_access()
 */
void *dev_mueslix_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct mueslix_data *d = dev->priv_data;
   int i;

#if DEBUG_ACCESS >= 2
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->name,"read  access to offset=0x%x, pc=0x%llx, size=%u\n",
              offset,cpu_get_pc(cpu),op_size);
   } else {
      cpu_log(cpu,d->name,"write access to offset=0x%x, pc=0x%llx, "
              "val=0x%llx, size=%u\n",offset,cpu_get_pc(cpu),*data,op_size);
   }
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
      MUESLIX_LOCK(d);
      dev_mueslix_chan_access(cpu,&d->channel[i],
                              offset - channel_offset[i],
                              op_size,op_type,data);
      MUESLIX_UNLOCK(d);
      return NULL;
   }

   MUESLIX_LOCK(d);

   /* Generic case */
   switch(offset) {
      /* this reg is accessed when an interrupt occurs */
      case 0x0:
         if (op_type == MTS_READ) {
            *data = d->irq_status;
         } else {
            d->irq_status &= ~(*data);
            dev_mueslix_update_irq_status(d);
         }
         break;

      /* Maybe interrupt mask */
      case 0x10:
         if (op_type == MTS_READ) {
            *data = d->irq_mask;
         } else {
            d->irq_mask = *data;
            dev_mueslix_update_irq_status(d);
         }
         break;

      case 0x14:
         if (op_type == MTS_READ)
            *data = d->channel_enable_mask;
         else {
#if DEBUG_ACCESS
            cpu_log(cpu,d->name,
                    "channel_enable_mask = 0x%5.5llx at pc=0x%llx\n",
                    *data,cpu_get_pc(cpu));
#endif
            d->channel_enable_mask = *data;
         }
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
         if (op_type == MTS_WRITE) {
            cpu_log(cpu,d->name,"cmd_reg = 0x%5.5llx at pc=0x%llx\n",
                    *data,cpu_get_pc(cpu));
         }
#endif
         switch(d->chip_mode) {
            case 0:  /* 3600 */
               tpu_cm0_handle_cmd(d,*data);
               break;
            case 1:  /* 7200 */
               tpu_cm1_handle_cmd(d,*data);
               break;
         }
         break;

      /* 
       * cmd_rsp reg, it seems that 0xFFFF means OK
       * (seen on a "sh contr se1/0" with "debug serial mueslix" enabled).
       */
      case MUESLIX_TPU_CMD_RSP_OFFSET:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->name,
                    "read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,d->name,
                    "write to unknown addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   MUESLIX_UNLOCK(d);
   return NULL;
}

/*
 * Get the address of the next RX descriptor.
 */
static m_uint32_t rxdesc_get_next(struct mueslix_channel *channel,
                                  m_uint32_t rxd_addr)
{
   m_uint32_t nrxd_addr;

   switch(channel->parent->chip_mode) {
      case 0:
         nrxd_addr = rxd_addr + sizeof(struct rx_desc);
         if (nrxd_addr == channel->rx_end)
            nrxd_addr = channel->rx_start;
         break;

      case 1:
      default:
         if (rxd_addr == channel->rx_end)
            nrxd_addr = channel->rx_start;
         else
            nrxd_addr = rxd_addr + sizeof(struct rx_desc);
         break;         
   }

   return(nrxd_addr);
}

/* Read an RX descriptor */
static void rxdesc_read(struct mueslix_data *d,m_uint32_t rxd_addr,
                        struct rx_desc *rxd)
{
#if DEBUG_RECEIVE
   MUESLIX_LOG(d,"reading RX descriptor at address 0x%x\n",rxd_addr);
#endif

   /* get the next descriptor from VM physical RAM */
   physmem_copy_from_vm(d->vm,rxd,rxd_addr,sizeof(struct rx_desc));

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
   MUESLIX_LOG(d,"copying %d bytes at 0x%x\n",cp_len,rxd->rdes[1]);
#endif
      
   /* copy packet data to the VM physical RAM */
   physmem_copy_to_vm(d->vm,*pkt,rxd->rdes[1],cp_len);
      
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

   if ((channel->rx_start == 0) || (channel->status == 0) ||
       (channel->nio == NULL))
      return;

   /* Don't make anything if RX is not enabled for this channel */
   if (!(dev_mueslix_is_rx_tx_enabled(d,channel->id) & MUESLIX_RX_ENABLE))
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
         rxdc->rdes[0] |= cp_len + channel->crc_size + 1;

         if (i != 0)
            physmem_copy_u32_to_vm(d->vm,channel->rx_current,rxdc->rdes[0]);

         channel->rx_current = rxdn_addr;
         break;
      }

#if DEBUG_RECEIVE
      MUESLIX_LOG(d,"trying to acquire new descriptor at 0x%x\n",rxdn_addr);
#endif

      /* Get status of the next descriptor to see if we can acquire it */
      rxdn_rdes0 = physmem_copy_u32_from_vm(d->vm,rxdn_addr);

      if (!rxdesc_acquire(rxdn_rdes0))
         rxdc->rdes[0] = MUESLIX_RXDESC_LS | MUESLIX_RXDESC_OVERRUN;
      else
         rxdc->rdes[0] = 0x00000000;  /* ok, no special flag */

      rxdc->rdes[0] |= cp_len;

      /* Update the new status (only if we are not on the first desc) */
      if (i != 0)
         physmem_copy_u32_to_vm(d->vm,channel->rx_current,rxdc->rdes[0]);

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
   physmem_copy_u32_to_vm(d->vm,rx_start,rxd0.rdes[0]);

   /* Indicate that we have a frame ready (XXX something to do ?) */

   /* Generate IRQ on CPU */
   d->irq_status |= MUESLIX_RX_IRQ << channel->id;
   dev_mueslix_update_irq_status(d);
}

/* Handle the Mueslix RX ring of the specified channel */
static int dev_mueslix_handle_rxring(netio_desc_t *nio,
                                     u_char *pkt,ssize_t pkt_len,
                                     struct mueslix_channel *channel)
{  
   struct mueslix_data *d = channel->parent;

#if DEBUG_RECEIVE
   MUESLIX_LOG(d,"channel %u: receiving a packet of %d bytes\n",
               channel->id,pkt_len);
   mem_dump(log_file,pkt,pkt_len);
#endif

   MUESLIX_LOCK(d);
   if (dev_mueslix_is_rx_tx_enabled(d,channel->id) & MUESLIX_RX_ENABLE)
      dev_mueslix_receive_pkt(channel,pkt,pkt_len);
   MUESLIX_UNLOCK(d);
   return(TRUE);
}

/* Read a TX descriptor */
static void txdesc_read(struct mueslix_data *d,m_uint32_t txd_addr,
                        struct tx_desc *txd)
{
   /* get the next descriptor from VM physical RAM */
   physmem_copy_from_vm(d->vm,txd,txd_addr,sizeof(struct tx_desc));

   /* byte-swapping */
   txd->tdes[0] = vmtoh32(txd->tdes[0]);
   txd->tdes[1] = vmtoh32(txd->tdes[1]);
}

/* Set the address of the next TX descriptor */
static void txdesc_set_next(struct mueslix_channel *channel)
{
   switch(channel->parent->chip_mode) {
      case 0:
         channel->tx_current += sizeof(struct tx_desc);

         if (channel->tx_current == channel->tx_end)
            channel->tx_current = channel->tx_start;
         break;

      case 1:
      default:
         if (channel->tx_current == channel->tx_end)
            channel->tx_current = channel->tx_start;
         else
            channel->tx_current += sizeof(struct tx_desc);
   }
}

/* Handle the TX ring of a specific channel (single packet) */
static int dev_mueslix_handle_txring_single(struct mueslix_channel *channel)
{
   struct mueslix_data *d = channel->parent;
   u_char pkt[MUESLIX_MAX_PKT_SIZE],*pkt_ptr;
   m_uint32_t tx_start,clen,sub_len,tot_len,pad;
   struct tx_desc txd0,ctxd,*ptxd;
   int done = FALSE;

   if ((channel->tx_start == 0) || (channel->status == 0))
      return(FALSE);

   /* Check if the NIO can transmit */
   if (!netio_can_transmit(channel->nio))
      return(FALSE);

   /* Copy the current txring descriptor */
   tx_start = channel->tx_current;   
   ptxd = &txd0;
   txdesc_read(d,channel->tx_current,ptxd);

   /* If we don't own the descriptor, we cannot transmit */
   if (!(txd0.tdes[0] & MUESLIX_TXDESC_OWN))
      return(FALSE);

#if DEBUG_TRANSMIT
   MUESLIX_LOG(d,"mueslix_handle_txring: 1st desc: "
               "tdes[0]=0x%x, tdes[1]=0x%x\n",
               ptxd->tdes[0],ptxd->tdes[1]);
#endif

   pkt_ptr = pkt;
   tot_len = 0;

   do {
#if DEBUG_TRANSMIT
      MUESLIX_LOG(d,"mueslix_handle_txring: loop: "
                  "tdes[0]=0x%x, tdes[1]=0x%x\n",
                  ptxd->tdes[0],ptxd->tdes[1]);
#endif

      if (!(ptxd->tdes[0] & MUESLIX_TXDESC_OWN)) {
         MUESLIX_LOG(d,"mueslix_handle_txring: descriptor not owned!\n");
         return(FALSE);
      }

      switch(channel->parent->chip_mode) {
         case 0:
            clen = ptxd->tdes[0] & MUESLIX_TXDESC_LEN_MASK;
            break;

         case 1:
         default:
            clen = (ptxd->tdes[0] & MUESLIX_TXDESC_LEN_MASK) << 2;

            if (ptxd->tdes[0] & MUESLIX_TXDESC_SUB) {
               sub_len = ptxd->tdes[0] & MUESLIX_TXDESC_SUB_LEN;
               sub_len >>= MUESLIX_TXDESC_SUB_SHIFT;
               clen -= sub_len;
            }
      }
      
      /* Be sure that we have length not null */
      if (clen != 0) {
         //printf("pkt_ptr = %p, ptxd->tdes[1] = 0x%x, clen = %d\n",
         //       pkt_ptr, ptxd->tdes[1], clen);
         physmem_copy_from_vm(d->vm,pkt_ptr,ptxd->tdes[1],clen);
      }

      pkt_ptr += clen;
      tot_len += clen;

      /* Clear the OWN bit if this is not the first descriptor */
      if (!(ptxd->tdes[0] & MUESLIX_TXDESC_FS))
         physmem_copy_u32_to_vm(d->vm,channel->tx_current,0);

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
      MUESLIX_LOG(d,"sending packet of %u bytes (flags=0x%4.4x)\n",
                  tot_len,txd0.tdes[0]);
      mem_dump(log_file,pkt,tot_len);
#endif

      pad = ptxd->tdes[0] & MUESLIX_TXDESC_PAD;
      pad >>= MUESLIX_TXDESC_PAD_SHIFT;
      tot_len -= (4 - pad) & 0x03;

      /* send it on wire */
      netio_send(channel->nio,pkt,tot_len);
   }

   /* Clear the OWN flag of the first descriptor */
   physmem_copy_u32_to_vm(d->vm,tx_start,0);

   /* Interrupt on completion ? */
   d->irq_status |= MUESLIX_TX_IRQ << channel->id;
   dev_mueslix_update_irq_status(d);
   return(TRUE);
}

/* Handle the TX ring of a specific channel */
static int dev_mueslix_handle_txring(struct mueslix_channel *channel)
{
   struct mueslix_data *d = channel->parent;
   int res,i;

   if (!dev_mueslix_is_rx_tx_enabled(d,channel->id) & MUESLIX_TX_ENABLE)
      return(FALSE);

   for(i=0;i<MUESLIX_TXRING_PASS_COUNT;i++) {
      MUESLIX_LOCK(d);
      res = dev_mueslix_handle_txring_single(channel);
      MUESLIX_UNLOCK(d);
     
      if (!res)
         break;
   }

   netio_clear_bw_stat(channel->nio);
   return(TRUE);
}

/* pci_mueslix_read() */
static m_uint32_t pci_mueslix_read(cpu_gen_t *cpu,struct pci_device *dev,
                                   int reg)
{   
   struct mueslix_data *d = dev->priv_data;

   switch(reg) {
      case 0x08:  /* Rev ID */
         return(0x2800001);
      case PCI_REG_BAR0:
         return(d->dev->phys_addr);
      default:
         return(0);
   }
}

/* pci_mueslix_write() */
static void pci_mueslix_write(cpu_gen_t *cpu,struct pci_device *dev,
                              int reg,m_uint32_t value)
{   
   struct mueslix_data *d = dev->priv_data;

   switch(reg) {
      case PCI_REG_BAR0:
         vm_map_device(cpu->vm,d->dev,(m_uint64_t)value);
         MUESLIX_LOG(d,"registers are mapped at 0x%x\n",value);
         break;
   }
}

/* Initialize a Mueslix chip */
struct mueslix_data *
dev_mueslix_init(vm_instance_t *vm,char *name,int chip_mode,
                 struct pci_bus *pci_bus,int pci_device,int irq)
{
   struct pci_device *pci_dev;
   struct mueslix_data *d;
   struct vdevice *dev;
   int i;

   /* Allocate the private data structure for Mueslix chip */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"%s (Mueslix): out of memory\n",name);
      return NULL;
   }

   memset(d,0,sizeof(*d));
   pthread_mutex_init(&d->lock,NULL);
   d->chip_mode = chip_mode;

   for(i=0;i<MUESLIX_NR_CHANNELS;i++) {
      d->channel[i].id = i;
      d->channel[i].parent = d;
   }

   /* Add as PCI device */
   pci_dev = pci_dev_add(pci_bus,name,
                         MUESLIX_PCI_VENDOR_ID,MUESLIX_PCI_PRODUCT_ID,
                         pci_device,0,irq,
                         d,NULL,pci_mueslix_read,pci_mueslix_write);

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
   d->pci_dev     = pci_dev;
   d->vm          = vm;

   dev->phys_addr = 0;
   dev->phys_len  = 0x4000;
   dev->handler   = dev_mueslix_access;
   dev->priv_data = d;

   /* Store device info */
   dev->priv_data = d;
   d->dev = dev;
   return(d);
}

/* Remove a Mueslix device */
void dev_mueslix_remove(struct mueslix_data *d)
{
   if (d != NULL) {
      pci_dev_remove(d->pci_dev);
      vm_unbind_device(d->vm,d->dev);
      cpu_group_rebuild_mts(d->vm->cpu_group);
      free(d->dev);
      free(d);
   }
}

/* Bind a NIO to a Mueslix channel */
int dev_mueslix_set_nio(struct mueslix_data *d,u_int channel_id,
                        netio_desc_t *nio)
{
   struct mueslix_channel *channel;

   if (channel_id >= MUESLIX_NR_CHANNELS)
      return(-1);

   channel = &d->channel[channel_id];

   /* check that a NIO is not already bound */
   if (channel->nio != NULL)
      return(-1);

   /* define the new NIO */
   channel->nio = nio;
   channel->tx_tid = ptask_add((ptask_callback)dev_mueslix_handle_txring,
                               channel,NULL);
   netio_rxl_add(nio,(netio_rx_handler_t)dev_mueslix_handle_rxring,
                 channel,NULL);
   return(0);
}

/* Unbind a NIO from a Mueslix channel */
int dev_mueslix_unset_nio(struct mueslix_data *d,u_int channel_id)
{
   struct mueslix_channel *channel;

   if (channel_id >= MUESLIX_NR_CHANNELS)
      return(-1);

   channel = &d->channel[channel_id];

   if (channel->nio) {
      ptask_remove(channel->tx_tid);
      netio_rxl_remove(channel->nio);
      channel->nio = NULL;
   }
   return(0);
}
