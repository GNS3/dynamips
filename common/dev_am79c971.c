/*  
 * Cisco router simulation platform.
 * Copyright (C) 2006 Christophe Fillot.  All rights reserved.
 *
 * AMD Am79c971 FastEthernet chip emulation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#include "utils.h"
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_am79c971.h"

/* Debugging flags */
#define DEBUG_CSR_REGS   0
#define DEBUG_BCR_REGS   0
#define DEBUG_PCI_REGS   0
#define DEBUG_ACCESS     0
#define DEBUG_TRANSMIT   0
#define DEBUG_RECEIVE    0
#define DEBUG_UNKNOWN    0

/* AMD Am79c971 PCI vendor/product codes */
#define AM79C971_PCI_VENDOR_ID    0x1022
#define AM79C971_PCI_PRODUCT_ID   0x2000

/* Maximum packet size */
#define AM79C971_MAX_PKT_SIZE  2048

/* Send up to 16 packets in a TX ring scan pass */
#define AM79C971_TXRING_PASS_COUNT  16

/* CSR0: Controller Status and Control Register */
#define AM79C971_CSR0_ERR      0x00008000    /* Error (BABL,CERR,MISS,MERR) */
#define AM79C971_CSR0_BABL     0x00004000    /* Transmitter Timeout Error */
#define AM79C971_CSR0_CERR     0x00002000    /* Collision Error */
#define AM79C971_CSR0_MISS     0x00001000    /* Missed Frame */
#define AM79C971_CSR0_MERR     0x00000800    /* Memory Error */
#define AM79C971_CSR0_RINT     0x00000400    /* Receive Interrupt */
#define AM79C971_CSR0_TINT     0x00000200    /* Transmit Interrupt */
#define AM79C971_CSR0_IDON     0x00000100    /* Initialization Done */
#define AM79C971_CSR0_INTR     0x00000080    /* Interrupt Flag */
#define AM79C971_CSR0_IENA     0x00000040    /* Interrupt Enable */
#define AM79C971_CSR0_RXON     0x00000020    /* Receive On */
#define AM79C971_CSR0_TXON     0x00000010    /* Transmit On */
#define AM79C971_CSR0_TDMD     0x00000008    /* Transmit Demand */
#define AM79C971_CSR0_STOP     0x00000004    /* Stop */
#define AM79C971_CSR0_STRT     0x00000002    /* Start */
#define AM79C971_CSR0_INIT     0x00000001    /* Initialization */

/* CSR3: Interrupt Masks and Deferral Control */
#define AM79C971_CSR3_BABLM    0x00004000    /* Transmit. Timeout Int. Mask */
#define AM79C971_CSR3_CERRM    0x00002000    /* Collision Error Int. Mask*/
#define AM79C971_CSR3_MISSM    0x00001000    /* Missed Frame Interrupt Mask */
#define AM79C971_CSR3_MERRM    0x00000800    /* Memory Error Interrupt Mask */
#define AM79C971_CSR3_RINTM    0x00000400    /* Receive Interrupt Mask */
#define AM79C971_CSR3_TINTM    0x00000200    /* Transmit Interrupt Mask */
#define AM79C971_CSR3_IDONM    0x00000100    /* Initialization Done Mask */
#define AM79C971_CSR3_BSWP     0x00000004    /* Byte Swap */
#define AM79C971_CSR3_IM_MASK  0x00007F00    /* Interrupt Masks for CSR3 */

/* CSR5: Extended Control and Interrupt 1 */
#define AM79C971_CSR5_TOKINTD  0x00008000    /* Receive Interrupt Mask */
#define AM79C971_CSR5_SPND     0x00000001    /* Suspend */

/* CSR15: Mode */
#define AM79C971_CSR15_PROM    0x00008000    /* Promiscous Mode */
#define AM79C971_CSR15_DRCVBC  0x00004000    /* Disable Receive Broadcast */
#define AM79C971_CSR15_DRCVPA  0x00002000    /* Disable Receive PHY address */
#define AM79C971_CSR15_DTX     0x00000002    /* Disable Transmit */
#define AM79C971_CSR15_DRX     0x00000001    /* Disable Receive */

/* AMD 79C971 Initialization block length */
#define AM79C971_INIT_BLOCK_LEN  0x1c

/* RX descriptors */
#define AM79C971_RMD1_OWN      0x80000000    /* OWN=1: owned by Am79c971 */
#define AM79C971_RMD1_ERR      0x40000000    /* Error */
#define AM79C971_RMD1_FRAM     0x20000000    /* Framing Error */
#define AM79C971_RMD1_OFLO     0x10000000    /* Overflow Error */
#define AM79C971_RMD1_CRC      0x08000000    /* Invalid CRC */
#define AM79C971_RMD1_BUFF     0x08000000    /* Buffer Error (chaining) */
#define AM79C971_RMD1_STP      0x02000000    /* Start of Packet */
#define AM79C971_RMD1_ENP      0x01000000    /* End of Packet */
#define AM79C971_RMD1_BPE      0x00800000    /* Bus Parity Error */
#define AM79C971_RMD1_PAM      0x00400000    /* Physical Address Match */
#define AM79C971_RMD1_LAFM     0x00200000    /* Logical Addr. Filter Match */
#define AM79C971_RMD1_BAM      0x00100000    /* Broadcast Address Match */
#define AM79C971_RMD1_LEN      0x00000FFF    /* Buffer Length */

#define AM79C971_RMD2_LEN      0x00000FFF    /* Received byte count */

/* TX descriptors */
#define AM79C971_TMD1_OWN      0x80000000    /* OWN=1: owned by Am79c971 */
#define AM79C971_TMD1_ERR      0x40000000    /* Error */
#define AM79C971_TMD1_ADD_FCS  0x20000000    /* FCS generation */
#define AM79C971_TMD1_STP      0x02000000    /* Start of Packet */
#define AM79C971_TMD1_ENP      0x01000000    /* End of Packet */
#define AM79C971_TMD1_LEN      0x00000FFF    /* Buffer Length */

/* RX Descriptor */
struct rx_desc {
   m_uint32_t rmd[4];
};

/* TX Descriptor */
struct tx_desc {
   m_uint32_t tmd[4];
};

/* AMD 79C971 Data */
struct am79c971_data {
   char *name;

   /* Lock */
   pthread_mutex_t lock;
   
   /* Interface type (10baseT or 100baseTX) */
   int type;

   /* RX/TX clearing count */
   int rx_tx_clear_count;

   /* Current RAP (Register Address Pointer) value */
   m_uint8_t rap;

   /* CSR and BCR registers */
   m_uint32_t csr[256],bcr[256];
   
   /* RX/TX rings start addresses */
   m_uint32_t rx_start,tx_start;

   /* RX/TX number of descriptors (log2) */
   m_uint32_t rx_l2len,tx_l2len;

   /* RX/TX number of descriptors */
   m_uint32_t rx_len,tx_len;

   /* RX/TX ring positions */
   m_uint32_t rx_pos,tx_pos;
   
   /* MII registers */
   m_uint16_t mii_regs[32][32];

   /* Physical (MAC) address */
   n_eth_addr_t mac_addr;

   /* Device information */
   struct vdevice *dev;

   /* PCI device information */
   struct pci_device *pci_dev;

   /* Virtual machine */
   vm_instance_t *vm;

   /* NetIO descriptor */
   netio_desc_t *nio;

   /* TX ring scanner task id */
   ptask_id_t tx_tid;
};

/* Log an am79c971 message */
#define AM79C971_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* Lock/Unlock primitives */
#define AM79C971_LOCK(d)    pthread_mutex_lock(&(d)->lock)
#define AM79C971_UNLOCK(d)  pthread_mutex_unlock(&(d)->lock)

static m_uint16_t mii_reg_values[32] = {
   0x1000, 0x782D, 0x0013, 0x78E2, 0x01E1, 0xC9E1, 0x000F, 0x2001,
   0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0x0104, 0x4780, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x00C8, 0x0000, 0xFFFF, 0x0000, 0x0000, 0x0000,

#if 0
   0x1000, 0x782D, 0x0013, 0x78e2, 0x01E1, 0xC9E1, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0x8060,
   0x8023, 0x0820, 0x0000, 0x3800, 0xA3B9, 0x0000, 0x0000, 0x0000,
#endif
};

/* Read a MII register */
static m_uint16_t mii_reg_read(struct am79c971_data *d,u_int phy,u_int reg)
{
   if ((phy >= 32) || (reg >= 32))
      return(0);

   return(d->mii_regs[phy][reg]);
}

/* Write a MII register */
__maybe_unused static void mii_reg_write(struct am79c971_data *d,u_int phy,u_int reg,
                          m_uint16_t value)
{
   if ((phy < 32) && (reg < 32))
      d->mii_regs[phy][reg] = value;
}

/* Check if a packet must be delivered to the emulated chip */
static inline int am79c971_handle_mac_addr(struct am79c971_data *d,
                                           m_uint8_t *pkt)
{
   n_eth_hdr_t *hdr = (n_eth_hdr_t *)pkt;

   /* Accept systematically frames if we are running in promiscuous mode */
   if (d->csr[15] & AM79C971_CSR15_PROM)
      return(TRUE);

   /* Accept systematically all multicast frames */
   if (eth_addr_is_mcast(&hdr->daddr))
      return(TRUE);

   /* Accept frames directly for us, discard others */
   if (!memcmp(&d->mac_addr,&hdr->daddr,N_ETH_ALEN))
      return(TRUE);
      
   return(FALSE);
}

/* Update the Interrupt Flag bit of csr0 */
static void am79c971_update_irq_status(struct am79c971_data *d)
{
   m_uint32_t mask;

   /* Bits set in CR3 disable the specified interrupts */
   mask = AM79C971_CSR3_IM_MASK & ~(d->csr[3] & AM79C971_CSR3_IM_MASK);
   
   if (d->csr[0] & mask)
      d->csr[0] |= AM79C971_CSR0_INTR;
   else
      d->csr[0] &= ~AM79C971_CSR0_INTR;

   if ((d->csr[0] & (AM79C971_CSR0_INTR|AM79C971_CSR0_IENA)) ==
       (AM79C971_CSR0_INTR|AM79C971_CSR0_IENA)) 
   {
      pci_dev_trigger_irq(d->vm,d->pci_dev);
   } else {
      pci_dev_clear_irq(d->vm,d->pci_dev);
   }
}

/* Update RX/TX ON bits of csr0 */
static void am79c971_update_rx_tx_on_bits(struct am79c971_data *d)
{
   /* 
    * Set RX ON if DRX in csr15 is cleared, and set TX on if DTX
    * in csr15 is cleared. The START bit must be set.
    */
   d->csr[0] &= ~(AM79C971_CSR0_RXON|AM79C971_CSR0_TXON);

   if (d->csr[0] & AM79C971_CSR0_STRT) {
      if (!(d->csr[15] & AM79C971_CSR15_DRX))
         d->csr[0] |= AM79C971_CSR0_RXON;
      
      if (!(d->csr[15] & AM79C971_CSR15_DTX))
         d->csr[0] |= AM79C971_CSR0_TXON;
   }
}

/* Update RX/TX descriptor lengths */
static void am79c971_update_rx_tx_len(struct am79c971_data *d)
{
   d->rx_len = 1 << d->rx_l2len;
   d->tx_len = 1 << d->tx_l2len;

   /* Normalize ring sizes */
   if (d->rx_len > 512) d->rx_len = 512;
   if (d->tx_len > 512) d->tx_len = 512;
}

/* Fetch the initialization block from memory */
static int am79c971_fetch_init_block(struct am79c971_data *d)
{
   m_uint32_t ib[AM79C971_INIT_BLOCK_LEN];
   m_uint32_t ib_addr,ib_tmp;

   /* The init block address is contained in csr1 (low) and csr2 (high) */
   ib_addr = (d->csr[2] << 16) | d->csr[1];

   if (!ib_addr) {
      AM79C971_LOG(d,"trying to fetch init block at address 0...\n");
      return(-1);
   }

   AM79C971_LOG(d,"fetching init block at address 0x%8.8x\n",ib_addr);
   physmem_copy_from_vm(d->vm,ib,ib_addr,sizeof(ib));
   
   /* Extract RX/TX ring addresses */
   d->rx_start = vmtoh32(ib[5]);
   d->tx_start = vmtoh32(ib[6]);

   /* Set csr15 from mode field */
   ib_tmp = vmtoh32(ib[0]);
   d->csr[15] = ib_tmp & 0xffff;

   /* Extract RX/TX ring sizes */
   d->rx_l2len = (ib_tmp >> 20) & 0x0F;
   d->tx_l2len = (ib_tmp >> 28) & 0x0F;
   am79c971_update_rx_tx_len(d);

   AM79C971_LOG(d,"rx_ring = 0x%8.8x (%u), tx_ring = 0x%8.8x (%u)\n",
                d->rx_start,d->rx_len,d->tx_start,d->tx_len);

   /* Get the physical MAC address */
   ib_tmp = vmtoh32(ib[1]);
   d->csr[12] = ib_tmp & 0xFFFF;
   d->csr[13] = ib_tmp >> 16;

   d->mac_addr.eth_addr_byte[3] = (ib_tmp >> 24) & 0xFF;
   d->mac_addr.eth_addr_byte[2] = (ib_tmp >> 16) & 0xFF;
   d->mac_addr.eth_addr_byte[1] = (ib_tmp >>  8) & 0xFF;
   d->mac_addr.eth_addr_byte[0] = ib_tmp & 0xFF;
   
   ib_tmp = vmtoh32(ib[2]);
   d->csr[14] = ib_tmp & 0xFFFF;
   d->mac_addr.eth_addr_byte[5] = (ib_tmp >> 8) & 0xFF;
   d->mac_addr.eth_addr_byte[4] = ib_tmp & 0xFF;

   /* 
    * Mark the initialization as done is csr0. 
    */
   d->csr[0] |= AM79C971_CSR0_IDON;

   /* Update RX/TX ON bits of csr0 since csr15 has been modified */
   am79c971_update_rx_tx_on_bits(d);
   AM79C971_LOG(d,"CSR0 = 0x%4.4x\n",d->csr[0]);
   return(0);
}

/* RDP (Register Data Port) access */
static void am79c971_rdp_access(cpu_gen_t *cpu,struct am79c971_data *d,
                                u_int op_type,m_uint64_t *data)
{
   m_uint32_t mask;

#if DEBUG_CSR_REGS
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->name,"read access to CSR %d\n",d->rap);
   } else {
      cpu_log(cpu,d->name,"write access to CSR %d, value=0x%x\n",d->rap,*data);
   }
#endif

   switch(d->rap) {
      case 0:  /* CSR0: Controller Status and Control Register */
         if (op_type == MTS_READ) {
            //AM79C971_LOG(d,"reading CSR0 (val=0x%4.4x)\n",d->csr[0]);
            *data = d->csr[0];
         } else {
            /* 
             * The STOP bit clears other bits.
             * It has precedence over INIT and START bits.
             */
            if (*data & AM79C971_CSR0_STOP) {
               //AM79C971_LOG(d,"stopping interface!\n");
               d->csr[0] = AM79C971_CSR0_STOP;
               d->tx_pos = d->rx_pos = 0;
               am79c971_update_irq_status(d);
               break;
            }
            
            /* These bits are cleared when set to 1 */
            mask  = AM79C971_CSR0_BABL | AM79C971_CSR0_CERR;
            mask |= AM79C971_CSR0_MISS | AM79C971_CSR0_MERR;
            mask |= AM79C971_CSR0_IDON;

            if (++d->rx_tx_clear_count == 3) {
               mask |= AM79C971_CSR0_RINT | AM79C971_CSR0_TINT;
               d->rx_tx_clear_count = 0;
            }

            d->csr[0] &= ~(*data & mask);

            /* Save the Interrupt Enable bit */
            d->csr[0] |= *data & AM79C971_CSR0_IENA;

            /* If INIT bit is set, fetch the initialization block */
            if (*data & AM79C971_CSR0_INIT) {
               d->csr[0] |= AM79C971_CSR0_INIT;
               d->csr[0] &= ~AM79C971_CSR0_STOP;
               am79c971_fetch_init_block(d);
            }

            /* If STRT bit is set, clear the stop bit */
            if (*data & AM79C971_CSR0_STRT) {
               //AM79C971_LOG(d,"enabling interface!\n");
               d->csr[0] |= AM79C971_CSR0_STRT;
               d->csr[0] &= ~AM79C971_CSR0_STOP;
               am79c971_update_rx_tx_on_bits(d);
            }

            /* Update IRQ status */
            am79c971_update_irq_status(d);
         }
         break;

      case 6:   /* CSR6: RX/TX Descriptor Table Length */
         if (op_type == MTS_WRITE) {
            d->rx_l2len = (*data >> 8) & 0x0F;
            d->tx_l2len = (*data >> 12) & 0x0F;
            am79c971_update_rx_tx_len(d);
         } else {
            *data = (d->tx_l2len << 12) | (d->rx_l2len << 8);
         }
         break;            

      case 15:  /* CSR15: Mode */
         if (op_type == MTS_WRITE) {
            d->csr[15] = *data;
            am79c971_update_rx_tx_on_bits(d);
         } else {
            *data = d->csr[15];
         }
         break;

      case 88:
         if (op_type == MTS_READ) {
            switch(d->type) {
               case AM79C971_TYPE_100BASE_TX:
                  *data = 0x2623003;
                  break;
               default:
                  *data = 0;
                  break;
            }
         }
         break;

      default:
         if (op_type == MTS_READ) {
            *data = d->csr[d->rap];
         } else {
            d->csr[d->rap] = *data;
         }

#if DEBUG_UNKNOWN
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->name,"read access to unknown CSR %d\n",d->rap);
         } else {
            cpu_log(cpu,d->name,"write access to unknown CSR %d, value=0x%x\n",
                    d->rap,*data);
         }
#endif
   }
}

/* BDP (BCR Data Port) access */
static void am79c971_bdp_access(cpu_gen_t *cpu,struct am79c971_data *d,
                                u_int op_type,m_uint64_t *data)
{
   u_int mii_phy,mii_reg;

#if DEBUG_BCR_REGS
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->name,"read access to BCR %d\n",d->rap);
   } else {
      cpu_log(cpu,d->name,"write access to BCR %d, value=0x%x\n",d->rap,*data);
   }
#endif

   switch(d->rap) {
      case 9:
         if (op_type == MTS_READ)
            *data = 1;
         break;

      case 34:  /* BCR34: MII Management Data Register */
         mii_phy = (d->bcr[33] >> 5) & 0x1F;
         mii_reg = (d->bcr[33] >> 0) & 0x1F;

         if (op_type == MTS_READ)
            *data = mii_reg_read(d,mii_phy,mii_reg);
         //else
         //mii_reg_write(d,mii_phy,mii_reg,*data);
         break;

      default:
         if (op_type == MTS_READ) {
            *data = d->bcr[d->rap];
         } else {
            d->bcr[d->rap] = *data;
         }

#if DEBUG_UNKNOWN
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->name,"read access to unknown BCR %d\n",d->rap);
         } else {
            cpu_log(cpu,d->name,
                    "write access to unknown BCR %d, value=0x%x\n",
                    d->rap,*data);
         }
#endif
   }
}

/*
 * dev_am79c971_access()
 */
void *dev_am79c971_access(cpu_gen_t *cpu,struct vdevice *dev,
                          m_uint32_t offset,u_int op_size,u_int op_type,
                          m_uint64_t *data)
{
   struct am79c971_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->name,"read  access to offset=0x%x, pc=0x%llx, size=%u\n",
              offset,cpu_get_pc(cpu),op_size);
   } else {
      cpu_log(cpu,d->name,"write access to offset=0x%x, pc=0x%llx, "
              "val=0x%llx, size=%u\n",offset,cpu_get_pc(cpu),*data,op_size);
   }
#endif

   AM79C971_LOCK(d);

   switch(offset) {
      case 0x14:  /* RAP (Register Address Pointer) */
         if (op_type == MTS_WRITE) {
            d->rap = *data & 0xFF;
         } else {
            *data = d->rap;
         }
         break;

      case 0x10:  /* RDP (Register Data Port) */
         am79c971_rdp_access(cpu,d,op_type,data);
         break;

      case 0x1c:  /* BDP (BCR Data Port) */
         am79c971_bdp_access(cpu,d,op_type,data);
         break;
   }

   AM79C971_UNLOCK(d);
   return NULL;
}

/* Read a RX descriptor */
static int rxdesc_read(struct am79c971_data *d,m_uint32_t rxd_addr,
                       struct rx_desc *rxd)
{
   m_uint32_t buf[4];
   m_uint8_t sw_style;

   /* Get the software style */
   sw_style = d->bcr[20];

   /* Read the descriptor from VM physical RAM */
   physmem_copy_from_vm(d->vm,&buf,rxd_addr,sizeof(struct rx_desc));

   switch(sw_style) {
      case 2:
         rxd->rmd[0] = vmtoh32(buf[0]);  /* rb addr */
         rxd->rmd[1] = vmtoh32(buf[1]);  /* own flag, ... */
         rxd->rmd[2] = vmtoh32(buf[2]);  /* rfrtag, mcnt, ... */
         rxd->rmd[3] = vmtoh32(buf[3]);  /* user */
         break;

      case 3:
         rxd->rmd[0] = vmtoh32(buf[2]);  /* rb addr */
         rxd->rmd[1] = vmtoh32(buf[1]);  /* own flag, ... */
         rxd->rmd[2] = vmtoh32(buf[0]);  /* rfrtag, mcnt, ... */
         rxd->rmd[3] = vmtoh32(buf[3]);  /* user */
         break;

      default:
         AM79C971_LOG(d,"invalid software style %u!\n",sw_style);
         return(-1);     
   }

   return(0);
}

/* Set the address of the next RX descriptor */
static inline void rxdesc_set_next(struct am79c971_data *d)
{
   d->rx_pos++;

   if (d->rx_pos == d->rx_len)
      d->rx_pos = 0;
}

/* Compute the address of the current RX descriptor */
static inline m_uint32_t rxdesc_get_current(struct am79c971_data *d)
{
   return(d->rx_start + (d->rx_pos * sizeof(struct rx_desc)));
}

/* Put a packet in buffer of a descriptor */
static void rxdesc_put_pkt(struct am79c971_data *d,struct rx_desc *rxd,
                           u_char **pkt,ssize_t *pkt_len)
{
   ssize_t len,cp_len;

   /* Compute the data length to copy */
   len = ~((rxd->rmd[1] & AM79C971_RMD1_LEN) - 1);
   len &= AM79C971_RMD1_LEN;
   cp_len = m_min(len,*pkt_len);
      
   /* Copy packet data to the VM physical RAM */
#if DEBUG_RECEIVE
   AM79C971_LOG(d,"am79c971_handle_rxring: storing %u bytes at 0x%8.8x\n",
                cp_len, rxd->rmd[0]);
#endif
   physmem_copy_to_vm(d->vm,*pkt,rxd->rmd[0],cp_len);

   *pkt += cp_len;
   *pkt_len -= cp_len;
}

/*
 * Put a packet in the RX ring.
 */
static int am79c971_receive_pkt(struct am79c971_data *d,
                                u_char *pkt,ssize_t pkt_len)
{ 
   m_uint32_t rx_start,rx_current,rx_next,rxdn_rmd1;
   struct rx_desc rxd0,rxdn,*rxdc;
   ssize_t tot_len = pkt_len;
   u_char *pkt_ptr = pkt;
   m_uint8_t sw_style;
   int i;

   /* Truncate the packet if it is too big */
   pkt_len = m_min(pkt_len,AM79C971_MAX_PKT_SIZE);

   /* Copy the current rxring descriptor */
   rx_start = rx_current = rxdesc_get_current(d);
   rxdesc_read(d,rx_start,&rxd0);
   
   /* We must have the first descriptor... */
   if (!(rxd0.rmd[1] & AM79C971_RMD1_OWN))
      return(FALSE);

   for(i=0,rxdc=&rxd0;;i++)
   {
#if DEBUG_RECEIVE
      AM79C971_LOG(d,"am79c971_handle_rxring: i=%d, addr=0x%8.8x: "
                   "rmd[0]=0x%x, rmd[1]=0x%x, rmd[2]=0x%x, rmd[3]=0x%x\n",
                   i,rx_current,
                   rxdc->rmd[0],rxdc->rmd[1],rxdc->rmd[2],rxdc->rmd[3]);
#endif
      /* Put data into the descriptor buffer */
      rxdesc_put_pkt(d,rxdc,&pkt_ptr,&tot_len);

      /* Go to the next descriptor */
      rxdesc_set_next(d);

      /* If this is not the first descriptor, clear the OWN bit */
      if (i != 0)
         rxdc->rmd[1] &= ~AM79C971_RMD1_OWN;

      /* If we have finished, mark the descriptor as end of packet */
      if (tot_len == 0) {
         rxdc->rmd[1] |= AM79C971_RMD1_ENP;
         physmem_copy_u32_to_vm(d->vm,rx_current+4,rxdc->rmd[1]);

         /* Get the software style */
         sw_style = d->bcr[20];

         /* Update the message byte count field */
         rxdc->rmd[2] &= ~AM79C971_RMD2_LEN;
         rxdc->rmd[2] |= pkt_len + 4;

         switch(sw_style) {
            case 2:
               physmem_copy_u32_to_vm(d->vm,rx_current+8,rxdc->rmd[2]);
               break;
            case 3:
               physmem_copy_u32_to_vm(d->vm,rx_current,rxdc->rmd[2]);
               break;
            default:
               AM79C971_LOG(d,"invalid software style %u!\n",sw_style);
         }

         break;
      }

      /* Try to acquire the next descriptor */
      rx_next = rxdesc_get_current(d);
      rxdn_rmd1 = physmem_copy_u32_from_vm(d->vm,rx_next+4);

      if (!(rxdn_rmd1 & AM79C971_RMD1_OWN)) {
         rxdc->rmd[1] |= AM79C971_RMD1_ERR | AM79C971_RMD1_BUFF;
         rxdc->rmd[1] |= AM79C971_RMD1_ENP;
         physmem_copy_u32_to_vm(d->vm,rx_current+4,rxdc->rmd[1]);
         break;
      }

      /* Update rmd1 to store change of OWN bit */
      physmem_copy_u32_to_vm(d->vm,rx_current+4,rxdc->rmd[1]);

      /* Read the next descriptor from VM physical RAM */
      rxdesc_read(d,rx_next,&rxdn);
      rxdc = &rxdn;
      rx_current = rx_next;
   }

   /* Update the first RX descriptor */
   rxd0.rmd[1] &= ~AM79C971_RMD1_OWN;
   rxd0.rmd[1] |= AM79C971_RMD1_STP;
   physmem_copy_u32_to_vm(d->vm,rx_start+4,rxd0.rmd[1]);

   d->csr[0] |= AM79C971_CSR0_RINT;
   am79c971_update_irq_status(d);
   return(TRUE);
}

/* Handle the RX ring */
static int am79c971_handle_rxring(netio_desc_t *nio,
                                  u_char *pkt,ssize_t pkt_len,
                                  struct am79c971_data *d)
{
   /* 
    * Don't start receive if the RX ring address has not been set
    * and if RX ON is not set.
    */
   if ((d->rx_start == 0) || !(d->csr[0] & AM79C971_CSR0_RXON))
      return(FALSE);

#if DEBUG_RECEIVE
   AM79C971_LOG(d,"receiving a packet of %d bytes\n",pkt_len);
   mem_dump(log_file,pkt,pkt_len);
#endif

   AM79C971_LOCK(d);

   /* 
    * Receive only multicast/broadcast trafic + unicast traffic 
    * for this virtual machine.
    */
   if (am79c971_handle_mac_addr(d,pkt))
      am79c971_receive_pkt(d,pkt,pkt_len);

   AM79C971_UNLOCK(d);
   return(TRUE);
}

/* Read a TX descriptor */
static int txdesc_read(struct am79c971_data *d,m_uint32_t txd_addr,
                       struct tx_desc *txd)
{
   m_uint32_t buf[4];
   m_uint8_t sw_style;

   /* Get the software style */
   sw_style = d->bcr[20];

   /* Read the descriptor from VM physical RAM */
   physmem_copy_from_vm(d->vm,&buf,txd_addr,sizeof(struct tx_desc));

   switch(sw_style) {
      case 2:
         txd->tmd[0] = vmtoh32(buf[0]);  /* tb addr */
         txd->tmd[1] = vmtoh32(buf[1]);  /* own flag, ... */
         txd->tmd[2] = vmtoh32(buf[2]);  /* buff, uflo, ... */
         txd->tmd[3] = vmtoh32(buf[3]);  /* user */
         break;

      case 3:
         txd->tmd[0] = vmtoh32(buf[2]);  /* tb addr */
         txd->tmd[1] = vmtoh32(buf[1]);  /* own flag, ... */
         txd->tmd[2] = vmtoh32(buf[0]);  /* buff, uflo, ... */
         txd->tmd[3] = vmtoh32(buf[3]);  /* user */
         break;

      default:
         AM79C971_LOG(d,"invalid software style %u!\n",sw_style);
         return(-1);     
   }

   return(0);
}

/* Set the address of the next TX descriptor */
static inline void txdesc_set_next(struct am79c971_data *d)
{
   d->tx_pos++;

   if (d->tx_pos == d->tx_len)
      d->tx_pos = 0;
}

/* Compute the address of the current TX descriptor */
static inline m_uint32_t txdesc_get_current(struct am79c971_data *d)
{
   return(d->tx_start + (d->tx_pos * sizeof(struct tx_desc)));
}

/* Handle the TX ring (single packet) */
static int am79c971_handle_txring_single(struct am79c971_data *d)
{
   u_char pkt[AM79C971_MAX_PKT_SIZE],*pkt_ptr;
   struct tx_desc txd0,ctxd,ntxd,*ptxd;
   m_uint32_t tx_start,tx_current;
   m_uint32_t clen,tot_len;
   
   if ((d->tx_start == 0) || !(d->csr[0] & AM79C971_CSR0_TXON))
      return(FALSE);
   
   /* Check if the NIO can transmit */
   if (!netio_can_transmit(d->nio))
      return(FALSE);
   
   /* Copy the current txring descriptor */
   tx_start = tx_current = txdesc_get_current(d);
   ptxd = &txd0;
   txdesc_read(d,tx_start,ptxd);
   
   /* If we don't own the first descriptor, we cannot transmit */
   if (!(ptxd->tmd[1] & AM79C971_TMD1_OWN))
      return(FALSE);
    
#if DEBUG_TRANSMIT
   AM79C971_LOG(d,"am79c971_handle_txring: 1st desc: "
                "tmd[0]=0x%x, tmd[1]=0x%x, tmd[2]=0x%x, tmd[3]=0x%x\n",
                ptxd->tmd[0],ptxd->tmd[1],ptxd->tmd[2],ptxd->tmd[3]);
#endif

   /* Empty packet for now */
   pkt_ptr = pkt;
   tot_len = 0;

   for(;;) {
#if DEBUG_TRANSMIT
      AM79C971_LOG(d,"am79c971_handle_txring: loop: "
                   "tmd[0]=0x%x, tmd[1]=0x%x, tmd[2]=0x%x, tmd[3]=0x%x\n",
                   ptxd->tmd[0],ptxd->tmd[1],ptxd->tmd[2],ptxd->tmd[3]);
#endif
      /* Copy packet data */
      clen = ~((ptxd->tmd[1] & AM79C971_TMD1_LEN) - 1);
      clen &= AM79C971_TMD1_LEN;

      physmem_copy_from_vm(d->vm,pkt_ptr,ptxd->tmd[0],clen);

      pkt_ptr += clen;
      tot_len += clen;

      /* Clear the OWN bit if this is not the first descriptor */
      if (!(ptxd->tmd[1] & AM79C971_TMD1_STP)) {
         ptxd->tmd[1] &= ~AM79C971_TMD1_OWN;
         physmem_copy_u32_to_vm(d->vm,tx_current+4,ptxd->tmd[1]);
      }

      /* Set the next descriptor */
      txdesc_set_next(d);

      /* Stop now if end of packet has been reached */
      if (ptxd->tmd[1] & AM79C971_TMD1_ENP)
         break;

      /* Read the next descriptor and try to acquire it */
      tx_current = txdesc_get_current(d);
      txdesc_read(d,tx_current,&ntxd);

      if (!(ntxd.tmd[1] & AM79C971_TMD1_OWN)) {
         AM79C971_LOG(d,"am79c971_handle_txring: UNDERFLOW!\n");
         return(FALSE);
      }

      memcpy(&ctxd,&ntxd,sizeof(struct tx_desc));
      ptxd = &ctxd;
   }

   if (tot_len != 0) {
#if DEBUG_TRANSMIT
      AM79C971_LOG(d,"sending packet of %u bytes\n",tot_len);
      mem_dump(log_file,pkt,tot_len);
#endif
      /* rewrite ISL header if required */
      cisco_isl_rewrite(pkt,tot_len);

      /* send it on wire */
      netio_send(d->nio,pkt,tot_len);
   }

   /* Clear the OWN flag of the first descriptor */
   txd0.tmd[1] &= ~AM79C971_TMD1_OWN;
   physmem_copy_u32_to_vm(d->vm,tx_start+4,txd0.tmd[1]);

   /* Generate TX interrupt */
   d->csr[0] |= AM79C971_CSR0_TINT;
   am79c971_update_irq_status(d);
   return(TRUE);
}

/* Handle the TX ring */
static int am79c971_handle_txring(struct am79c971_data *d)
{
   int i;

   AM79C971_LOCK(d);

   for(i=0;i<AM79C971_TXRING_PASS_COUNT;i++)
      if (!am79c971_handle_txring_single(d))
         break;

   netio_clear_bw_stat(d->nio);
   AM79C971_UNLOCK(d);
   return(TRUE);
}

/*
 * pci_am79c971_read()
 *
 * Read a PCI register.
 */
static m_uint32_t pci_am79c971_read(cpu_gen_t *cpu,struct pci_device *dev,
                                    int reg)
{   
   struct am79c971_data *d = dev->priv_data;

#if DEBUG_PCI_REGS
   AM79C971_LOG(d,"read PCI register 0x%x\n",reg);
#endif

   switch (reg) {
      case 0x00:
         return((AM79C971_PCI_PRODUCT_ID << 16) | AM79C971_PCI_VENDOR_ID);
      case 0x08:
         return(0x02000002);
      case PCI_REG_BAR1:
         return(d->dev->phys_addr);
      default:
         return(0);
   }
}

/*
 * pci_am79c971_write()
 *
 * Write a PCI register.
 */
static void pci_am79c971_write(cpu_gen_t *cpu,struct pci_device *dev,
                               int reg,m_uint32_t value)
{
   struct am79c971_data *d = dev->priv_data;

#if DEBUG_PCI_REGS
   AM79C971_LOG(d,"write PCI register 0x%x, value 0x%x\n",reg,value);
#endif

   switch(reg) {
      case PCI_REG_BAR1:
         vm_map_device(cpu->vm,d->dev,(m_uint64_t)value);
         AM79C971_LOG(d,"registers are mapped at 0x%x\n",value);
         break;
   }
}

/* 
 * dev_am79c971_init()
 *
 * Generic AMD Am79c971 initialization code.
 */
struct am79c971_data *
dev_am79c971_init(vm_instance_t *vm,char *name,int interface_type,
                  struct pci_bus *pci_bus,int pci_device,int irq)
{
   struct am79c971_data *d;
   struct pci_device *pci_dev;
   struct vdevice *dev;

   /* Allocate the private data structure for AM79C971 */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"%s (AM79C971): out of memory\n",name);
      return NULL;
   }

   memset(d,0,sizeof(*d));
   memcpy(d->mii_regs[0],mii_reg_values,sizeof(mii_reg_values));
   pthread_mutex_init(&d->lock,NULL);

   /* Add as PCI device */
   pci_dev = pci_dev_add(pci_bus,name,
                         AM79C971_PCI_VENDOR_ID,AM79C971_PCI_PRODUCT_ID,
                         pci_device,0,irq,
                         d,NULL,pci_am79c971_read,pci_am79c971_write);

   if (!pci_dev) {
      fprintf(stderr,"%s (AM79C971): unable to create PCI device.\n",name);
      goto err_pci_dev;
   }

   /* Create the device itself */
   if (!(dev = dev_create(name))) {
      fprintf(stderr,"%s (AM79C971): unable to create device.\n",name);
      goto err_dev;
   }

   d->name     = name;
   d->vm       = vm;
   d->type     = interface_type;
   d->pci_dev  = pci_dev;
   d->dev      = dev;

   dev->phys_addr = 0;
   dev->phys_len  = 0x4000;
   dev->handler   = dev_am79c971_access;
   dev->priv_data = d;
   return(d);

 err_dev:
   pci_dev_remove(pci_dev);
 err_pci_dev:
   free(d);
   return NULL;
}

/* Remove an AMD Am79c971 device */
void dev_am79c971_remove(struct am79c971_data *d)
{
   if (d != NULL) {
      pci_dev_remove(d->pci_dev);
      vm_unbind_device(d->vm,d->dev);
      cpu_group_rebuild_mts(d->vm->cpu_group);
      free(d->dev);
      free(d);
   }
}

/* Bind a NIO to an AMD Am79c971 device */
int dev_am79c971_set_nio(struct am79c971_data *d,netio_desc_t *nio)
{   
   /* check that a NIO is not already bound */
   if (d->nio != NULL)
      return(-1);

   d->nio = nio;
   d->tx_tid = ptask_add((ptask_callback)am79c971_handle_txring,d,NULL);
   netio_rxl_add(nio,(netio_rx_handler_t)am79c971_handle_rxring,d,NULL);
   return(0);
}

/* Unbind a NIO from an AMD Am79c971 device */
void dev_am79c971_unset_nio(struct am79c971_data *d)
{
   if (d->nio != NULL) {
      ptask_remove(d->tx_tid);
      netio_rxl_remove(d->nio);
      d->nio = NULL;
   }
}
