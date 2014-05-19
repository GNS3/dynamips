/*  
 * Cisco router simlation platform.
 * Copyright (C) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * DEC21140 FastEthernet chip emulation.
 *
 * It allows to emulate a C7200-IO-FE card with 1 port and PA-FE-TX cards.
 *
 * Many many thanks to mtve (aka "Mtv Europe") for his great work on 
 * this stuff.
 *
 * Manuals:
 *
 * DECchip 21140 PCI fast Ethernet LAN controller Hardware reference manual
 * http://ftp.nluug.nl/NetBSD/misc/dec-docs/ec-qc0cb-te.ps.gz
 *
 * National DP83840 PHY
 * http://www.rezrov.net/docs/DP83840A.pdf
 *
 * Remark: only Big-endian mode is supported.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#include "crc.h"
#include "utils.h"
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_dec21140.h"

/* Debugging flags */
#define DEBUG_MII_REGS   0
#define DEBUG_CSR_REGS   0
#define DEBUG_PCI_REGS   0
#define DEBUG_TRANSMIT   0
#define DEBUG_RECEIVE    0

/* DEC21140 PCI vendor/product codes */
#define DEC21140_PCI_VENDOR_ID    0x1011
#define DEC21140_PCI_PRODUCT_ID   0x0009

/* DEC21140 PCI registers */
#define DEC21140_PCI_CFID_REG_OFFSET   0x00
#define DEC21140_PCI_CFCS_REG_OFFSET   0x04
#define DEC21140_PCI_CFRV_REG_OFFSET   0x08
#define DEC21140_PCI_CFLT_REG_OFFSET   0x0C
#define DEC21140_PCI_CBIO_REG_OFFSET   0x10
#define DEC21140_PCI_CBMA_REG_OFFSET   0x14
#define DEC21140_PCI_CFIT_REG_OFFSET   0x3C
#define DEC21140_PCI_CFDA_REG_OFFSET   0x40

/* Number of CSR registers */
#define DEC21140_CSR_NR  16

/* CSR5: Status Register */
#define DEC21140_CSR5_TI          0x00000001  /* TX Interrupt */
#define DEC21140_CSR5_TPS         0x00000002  /* TX Process Stopped */
#define DEC21140_CSR5_TU          0x00000004  /* TX Buffer Unavailable */
#define DEC21140_CSR5_TJT         0x00000008  /* TX Jabber Timeout */
#define DEC21140_CSR5_UNF         0x00000020  /* TX Underflow */
#define DEC21140_CSR5_RI          0x00000040  /* RX Interrupt */
#define DEC21140_CSR5_RU          0x00000080  /* RX Buffer Unavailable */
#define DEC21140_CSR5_RPS         0x00000100  /* RX Process Stopped */
#define DEC21140_CSR5_RWT         0x00000200  /* RX Watchdog Timeout */
#define DEC21140_CSR5_GTE         0x00000800  /* Gen Purpose Timer Expired */
#define DEC21140_CSR5_FBE         0x00002000  /* Fatal Bus Error */
#define DEC21140_CSR5_AIS         0x00008000  /* Abnormal Interrupt Summary */
#define DEC21140_CSR5_NIS         0x00010000  /* Normal Interrupt Summary */

#define DEC21140_NIS_BITS \
   (DEC21140_CSR5_TI|DEC21140_CSR5_RI|DEC21140_CSR5_TU)

#define DEC21140_AIS_BITS \
   (DEC21140_CSR5_TPS|DEC21140_CSR5_TJT|DEC21140_CSR5_UNF| \
    DEC21140_CSR5_RU|DEC21140_CSR5_RPS|DEC21140_CSR5_RWT| \
    DEC21140_CSR5_GTE|DEC21140_CSR5_FBE)

#define DEC21140_CSR5_RS_SHIFT    17
#define DEC21140_CSR5_TS_SHIFT    20

/* CSR6: Operating Mode Register */
#define DEC21140_CSR6_START_RX    0x00000002
#define DEC21140_CSR6_START_TX    0x00002000
#define DEC21140_CSR6_PROMISC     0x00000040

/* CSR9: Serial EEPROM and MII */
#define DEC21140_CSR9_RX_BIT      0x00080000
#define DEC21140_CSR9_MII_READ    0x00040000
#define DEC21140_CSR9_TX_BIT      0x00020000
#define DEC21140_CSR9_MDC_CLOCK   0x00010000
#define DEC21140_CSR9_READ        0x00004000
#define DEC21140_CSR9_WRITE       0x00002000

/* Maximum packet size */
#define DEC21140_MAX_PKT_SIZE     2048

/* Send up to 32 packets in a TX ring scan pass */
#define DEC21140_TXRING_PASS_COUNT  32

/* Setup frame size */
#define DEC21140_SETUP_FRAME_SIZE 192

/* RX descriptors */
#define DEC21140_RXDESC_OWN       0x80000000  /* Ownership */
#define DEC21140_RXDESC_LS        0x00000100  /* Last Segment */
#define DEC21140_RXDESC_FS        0x00000200  /* First Segment */
#define DEC21140_RXDESC_MF        0x00000400  /* Multicast Frame */
#define DEC21140_RXDESC_DE        0x00004000  /* Descriptor Error */
#define DEC21140_RXDESC_RCH       0x01000000  /* Sec. Addr. Chained */
#define DEC21140_RXDESC_RER       0x02000000  /* Receive End of Ring */
#define DEC21140_RXDESC_FL_SHIFT  16
#define DEC21140_RXDESC_LEN_MASK  0x7ff

/* TX descriptors */
#define DEC21140_TXDESC_OWN       0x80000000  /* Ownership */
#define DEC21140_TXDESC_TCH       0x01000000  /* Sec. Addr. Chained */
#define DEC21140_TXDESC_TER       0x02000000  /* Transmit End of Ring */
#define DEC21140_TXDESC_SET       0x08000000  /* Setup frame */
#define DEC21140_TXDESC_FS        0x20000000  /* First Segment */
#define DEC21140_TXDESC_LS        0x40000000  /* Last Segment */
#define DEC21140_TXDESC_IC        0x80000000  /* IRQ on completion */

#define DEC21140_TXDESC_LEN_MASK  0x7ff

/* RX Descriptor */
struct rx_desc {
   m_uint32_t rdes[4];
};

/* TX Descriptor */
struct tx_desc {
   m_uint32_t tdes[4];
};

/* DEC21140 Data */
struct dec21140_data {
   char *name;
   
   /* Physical addresses of current RX and TX descriptors */
   m_uint32_t rx_current;
   m_uint32_t tx_current;

   /* CSR registers */
   m_uint32_t csr[DEC21140_CSR_NR];

   /* MII registers */
   m_uint32_t mii_state;
   m_uint32_t mii_phy;
   m_uint32_t mii_reg;
   m_uint32_t mii_data;
   m_uint32_t mii_outbits;
   m_uint16_t mii_regs[32][32];

   /* Ethernet unicast addresses */
   n_eth_addr_t mac_addr[16];
   u_int mac_addr_count;

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

/* Log a dec21140 message */
#define DEC21140_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* Check if a packet must be delivered to the emulated chip */
static inline int dec21140_handle_mac_addr(struct dec21140_data *d,
                                           m_uint8_t *pkt)
{
   n_eth_hdr_t *hdr = (n_eth_hdr_t *)pkt;
   int i;

   /* Accept systematically frames if we are running is promiscuous mode */
   if (d->csr[6] & DEC21140_CSR6_PROMISC)
      return(TRUE);

   /* Accept systematically all multicast frames */
   if (eth_addr_is_mcast(&hdr->daddr))
      return(TRUE);

   /* Accept frames directly for us, discard others */
   for(i=0;i<d->mac_addr_count;i++)
      if (!memcmp(&d->mac_addr[i],&hdr->daddr,N_ETH_ALEN))
         return(TRUE);
      
   return(FALSE);
}

/* Update MAC addresses */
static void dec21140_update_mac_addr(struct dec21140_data *d,
                                     u_char *setup_frame)
{
   n_eth_addr_t addr;
   int i,nb_addr,addr_size;

   d->mac_addr_count = 0;

   addr_size = N_ETH_ALEN * 2;
   nb_addr = DEC21140_SETUP_FRAME_SIZE / addr_size;

   for(i=0;i<nb_addr;i++) {
      addr.eth_addr_byte[0] = setup_frame[(i * addr_size) + 0];
      addr.eth_addr_byte[1] = setup_frame[(i * addr_size) + 1];
      addr.eth_addr_byte[2] = setup_frame[(i * addr_size) + 4];
      addr.eth_addr_byte[3] = setup_frame[(i * addr_size) + 5];
      addr.eth_addr_byte[4] = setup_frame[(i * addr_size) + 8];
      addr.eth_addr_byte[5] = setup_frame[(i * addr_size) + 9];

      if (!eth_addr_is_mcast(&addr)) {
         memcpy(&d->mac_addr[d->mac_addr_count],&addr,N_ETH_ALEN);
         DEC21140_LOG(d,"unicast MAC address: "
                      "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
                      addr.eth_addr_byte[0],addr.eth_addr_byte[1],
                      addr.eth_addr_byte[2],addr.eth_addr_byte[3],
                      addr.eth_addr_byte[4],addr.eth_addr_byte[5]);
         d->mac_addr_count++;
      }
   }
}

/* Get a PCI register name */
__maybe_unused static char *pci_cfgreg_name(int reg)
{
   static char *name[] = {
      "FID", "FCS", "FRV", "FLT", "BIO", "BMA", "?", "?",
      "?", "?", "?", "?", "?", "?", "?", "FIT", "FDA"
   };

   return((reg>=0) && (reg<=DEC21140_CSR_NR*4) && ((reg&3)==0) ?
          name[reg>>2] : "?");
}

/*
 * read from register of DP83840A PHY
 */ 
static m_uint16_t mii_reg_read(struct dec21140_data *d)
{
#if DEBUG_MII_REGS
   DEC21140_LOG(d,"MII PHY read %d reg %d\n",d->mii_phy,d->mii_reg);
#endif

   /*
    * if it's BASIC MODE STATUS REGISTER (BMSR) at address 0x1
    * then tell them that "Link Status" is up and no troubles.
    */
   if (d->mii_reg == 1) {
      if (d->nio != NULL)
         return(0x04);
      else
         return(0x00);
   }

   return(d->mii_regs[d->mii_phy][d->mii_reg]);
}

/*
 * write to register of DP83840A PHY
 */ 
static void mii_reg_write(struct dec21140_data *d)
{
#if DEBUG_MII_REGS
   DEC21140_LOG(d,"MII PHY write %d reg %d value %04x\n",
                d->mii_phy,d->mii_reg,d->mii_data);
#endif
   assert(d->mii_phy < 32);
   assert(d->mii_reg < 32);
   d->mii_regs[d->mii_phy][d->mii_reg] = d->mii_data;
}

/*
 * process new bit sent by IOS to PHY.
 */
static void mii_newbit(struct dec21140_data *d,int newbit)
{
#if DEBUG_MII_REGS
   DEC21140_LOG(d,"MII state was %d\n",d->mii_state);
#endif

   switch (d->mii_state) {
      case 0: /* init */
         d->mii_state = newbit ? 0 : 1;
         d->mii_phy = 0;
         d->mii_reg = 0;
         d->mii_data = 0;
         break;

      case 1: /* already got 0 */
         d->mii_state = newbit ? 2 : 0;
         break;

      case 2: /* already got attention */
         d->mii_state = newbit ? 3 : 4;
         break;

      case 3: /* probably it's read */
         d->mii_state = newbit ? 0 : 10;
         break;

      case 4: /* probably it's write */
         d->mii_state = newbit ? 20 : 0;
         break;

      case 10: case 11: case 12: case 13: case 14:
      case 20: case 21: case 22: case 23: case 24:
         /* read or write state, read 5 bits of phy */
         d->mii_phy <<= 1;
         d->mii_phy |= newbit;
         d->mii_state++;
         break;

      case 15: case 16: case 17: case 18: case 19:
      case 25: case 26: case 27: case 28: case 29:
         /* read or write state, read 5 bits of reg */
         d->mii_reg <<= 1;
         d->mii_reg |= newbit;
         d->mii_state++;

         if (d->mii_state == 20) {
            /* read state, got everything */
            d->mii_outbits = mii_reg_read (d) << 15; /* first bit will
                                                      * be thrown away!
                                                      */
            d->mii_state = 0;
         }

         break;

      case 30: /* write state, read first waiting bit */
         d->mii_state = newbit ? 31 : 0;
         break;

      case 31: /* write state, read second waiting bit */
         d->mii_state = newbit ? 0 : 32;
         break;

      case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39:
      case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
         /* write state, read 16 bits of data */
         d->mii_data <<= 1;
         d->mii_data |= newbit;
         d->mii_state++;

         if (d->mii_state == 48) {
            /* write state, got everything */
            mii_reg_write (d);
            d->mii_state = 0;
         }

         break;
      default:
         DEC21140_LOG(d,"MII impossible state\n");
   }

#if DEBUG_MII_REGS
   DEC21140_LOG(d,"MII state now %d\n",d->mii_state);
#endif
}

/* Update the interrupt status */
static inline void dev_dec21140_update_irq_status(struct dec21140_data *d)
{
   int trigger = FALSE;
   m_uint32_t csr5;

   /* Work on a temporary copy of csr5 */
   csr5 = d->csr[5];
   
   /* Compute Interrupt Summary */
   csr5 &= ~(DEC21140_CSR5_AIS|DEC21140_CSR5_NIS);

   if (csr5 & DEC21140_NIS_BITS) {
      csr5 |= DEC21140_CSR5_NIS;
      trigger = TRUE;
   }

   if (csr5 & DEC21140_AIS_BITS) {
      csr5 |= DEC21140_CSR5_AIS;
      trigger = TRUE;
   }

   d->csr[5] = csr5;
   
   if (trigger)
      pci_dev_trigger_irq(d->vm,d->pci_dev);
   else {
      pci_dev_clear_irq(d->vm,d->pci_dev);
   }
}

/*
 * dev_dec21140_access()
 */
void *dev_dec21140_access(cpu_gen_t *cpu,struct vdevice *dev,
                          m_uint32_t offset,u_int op_size,u_int op_type,
                          m_uint64_t *data)
{
   struct dec21140_data *d = dev->priv_data;
   u_int reg;
   
   /* which CSR register ? */
   reg = offset / 8;

   if ((reg >= DEC21140_CSR_NR) || (offset % 8) != 0) {
      cpu_log(cpu,d->name,"invalid access to offset 0x%x\n",offset);
      return NULL;
   }

   if (op_type == MTS_READ) {
#if DEBUG_CSR_REGS
      cpu_log(cpu,d->name,"read CSR%u value 0x%x\n",reg,d->csr[reg]);
#endif
      switch(reg) {
         case 5:
            /* Dynamically construct CSR5 */
            *data = 0;

            if (d->csr[6] & DEC21140_CSR6_START_RX)
               *data |= 0x03 << DEC21140_CSR5_RS_SHIFT;
               
            if (d->csr[6] & DEC21140_CSR6_START_TX)
               *data |= 0x03 << DEC21140_CSR5_TS_SHIFT;
         
            *data |= d->csr[5];
            break;

         case 8:
            /* CSR8 is cleared when read (missed frame counter) */
            d->csr[reg] = 0;
            *data = 0;
            break;
            
         default:
            *data = d->csr[reg];
      }
   } else {
#if DEBUG_CSR_REGS
      cpu_log(cpu,d->name,"write CSR%u value 0x%x\n",reg,(m_uint32_t)*data);
#endif
      switch(reg) {
         case 3:
            d->csr[reg] = *data;
            d->rx_current = d->csr[reg];
            break;
         case 4:
            d->csr[reg] = *data;
            d->tx_current = d->csr[reg];
            break;
         case 5:
            d->csr[reg] &= ~(*data);
            dev_dec21140_update_irq_status(d);
            break;
         case 9:
            /*
             * CSR9, probably they want to mess with MII PHY
             * The protocol to PHY is like serial over one bit.
             * We will ignore clock 0 of read or write.
             *
             * This whole code is needed only to tell IOS that "Link Status"
             * bit in BMSR register of DP83840A PHY is set.
             *
             * Also it makes "sh contr f0/0" happy.
             */
            d->csr[reg] = *data;

            if ((*data & ~DEC21140_CSR9_TX_BIT) == (DEC21140_CSR9_MII_READ|
                 DEC21140_CSR9_READ|DEC21140_CSR9_MDC_CLOCK)) {
               /*
                * read, pop one bit from mii_outbits
                */
               if (d->mii_outbits & (1<<31))
                  d->csr[9] |= DEC21140_CSR9_RX_BIT;
               else
                  d->csr[9] &= ~DEC21140_CSR9_RX_BIT;
               d->mii_outbits <<= 1;
            } else if((*data&~DEC21140_CSR9_TX_BIT) == 
                      (DEC21140_CSR9_WRITE|DEC21140_CSR9_MDC_CLOCK)) {
               /*
                * write, we've got input, do state machine
                */
               mii_newbit(d,(*data&DEC21140_CSR9_TX_BIT) ? 1 : 0);            
            }
            break;

         default:
            d->csr[reg] = *data;
      }
   }

   return NULL;
}

/*
 * Get the address of the next RX descriptor.
 */
static m_uint32_t rxdesc_get_next(struct dec21140_data *d,m_uint32_t rxd_addr,
                                  struct rx_desc *rxd)
{
   m_uint32_t nrxd_addr;

   /* go to the next descriptor */
   if (rxd->rdes[1] & DEC21140_RXDESC_RER)
      nrxd_addr = d->csr[3];
   else {
      if (rxd->rdes[1] & DEC21140_RXDESC_RCH)
         nrxd_addr = rxd->rdes[3];
      else
         nrxd_addr = rxd_addr + sizeof(struct rx_desc);
   }

   return(nrxd_addr);
}

/* Read a RX descriptor */
static void rxdesc_read(struct dec21140_data *d,m_uint32_t rxd_addr,
                        struct rx_desc *rxd)
{
   /* get the next descriptor from VM physical RAM */
   physmem_copy_from_vm(d->vm,rxd,rxd_addr,sizeof(struct rx_desc));

   /* byte-swapping */
   rxd->rdes[0] = vmtoh32(rxd->rdes[0]);
   rxd->rdes[1] = vmtoh32(rxd->rdes[1]);
   rxd->rdes[2] = vmtoh32(rxd->rdes[2]);
   rxd->rdes[3] = vmtoh32(rxd->rdes[3]);
}

/* 
 * Try to acquire the specified RX descriptor. Returns TRUE if we have it.
 * It assumes that the byte-swapping is done.
 */
static inline int rxdesc_acquire(m_uint32_t rdes0)
{
   return(rdes0 & DEC21140_RXDESC_OWN);
}

/* Put a packet in buffer(s) of a descriptor */
static void rxdesc_put_pkt(struct dec21140_data *d,struct rx_desc *rxd,
                           u_char **pkt,ssize_t *pkt_len)
{
   ssize_t len1,len2,cp_len;

   /* get rbs1 and rbs2 */
   len1 = rxd->rdes[1] & DEC21140_RXDESC_LEN_MASK;
   len2 = (rxd->rdes[1] >> 10) & DEC21140_RXDESC_LEN_MASK;
   
   /* try with buffer #1 */
   if (len1 != 0)
   {
      /* compute the data length to copy */
      cp_len = m_min(len1,*pkt_len);
      
      /* copy packet data to the VM physical RAM */
      physmem_copy_to_vm(d->vm,*pkt,rxd->rdes[2],cp_len);
      
      *pkt += cp_len;
      *pkt_len -= cp_len;
   }

   /* try with buffer #2 */
   if ((len2 != 0) && !(rxd->rdes[1] & DEC21140_RXDESC_RCH))
   {
      /* compute the data length to copy */
      cp_len = m_min(len2,*pkt_len);
      
      /* copy packet data to the VM physical RAM */
      physmem_copy_to_vm(d->vm,*pkt,rxd->rdes[3],cp_len);
      
      *pkt += cp_len;
      *pkt_len -= cp_len;
   }
}

/*
 * Put a packet in the RX ring of the DEC21140.
 */
static int dev_dec21140_receive_pkt(struct dec21140_data *d,
                                    u_char *pkt,ssize_t pkt_len)
{
   m_uint32_t rx_start,rxdn_addr,rxdn_rdes0;
   struct rx_desc rxd0,rxdn,*rxdc;
   ssize_t tot_len = pkt_len;
   u_char *pkt_ptr = pkt;
   n_eth_hdr_t *hdr;
   int i;

   /* Truncate the packet if it is too big */
   pkt_len = m_min(pkt_len,DEC21140_MAX_PKT_SIZE);

   /* Copy the current rxring descriptor */
   rxdesc_read(d,d->rx_current,&rxd0);

   /* We must have the first descriptor... */
   if (!rxdesc_acquire(rxd0.rdes[0]))
      return(FALSE);

   /* Remember the first RX descriptor address */
   rx_start = d->rx_current;

   for(i=0,rxdc=&rxd0;tot_len>0;i++)
   {
      /* Put data into the descriptor buffers */
      rxdesc_put_pkt(d,rxdc,&pkt_ptr,&tot_len);

      /* Get address of the next descriptor */
      rxdn_addr = rxdesc_get_next(d,d->rx_current,rxdc);

      /* We have finished if the complete packet has been stored */
      if (tot_len == 0) {
         rxdc->rdes[0] = DEC21140_RXDESC_LS;
         rxdc->rdes[0] |= (pkt_len + 4) << DEC21140_RXDESC_FL_SHIFT;

         /* if this is a multicast frame, set the appropriate bit */
         hdr = (n_eth_hdr_t *)pkt;
         if (eth_addr_is_mcast(&hdr->daddr))
            rxdc->rdes[0] |= DEC21140_RXDESC_MF;

         if (i != 0)
            physmem_copy_u32_to_vm(d->vm,d->rx_current,rxdc->rdes[0]);

         d->rx_current = rxdn_addr;
         break;
      }

      /* Get status of the next descriptor to see if we can acquire it */
      rxdn_rdes0 = physmem_copy_u32_from_vm(d->vm,rxdn_addr);

      if (!rxdesc_acquire(rxdn_rdes0))
         rxdc->rdes[0] = DEC21140_RXDESC_LS | DEC21140_RXDESC_DE;
      else
         rxdc->rdes[0] = 0;  /* ok, no special flag */

      /* Update the new status (only if we are not on the first desc) */
      if (i != 0)
         physmem_copy_u32_to_vm(d->vm,d->rx_current,rxdc->rdes[0]);

      /* Update the RX pointer */
      d->rx_current = rxdn_addr;

      if (rxdc->rdes[0] != 0)
         break;

      /* Read the next descriptor from VM physical RAM */
      rxdesc_read(d,rxdn_addr,&rxdn);
      rxdc = &rxdn;
   }

   /* Update the first RX descriptor */
   rxd0.rdes[0] |= DEC21140_RXDESC_FS;
   physmem_copy_u32_to_vm(d->vm,rx_start,rxd0.rdes[0]);

   /* Indicate that we have a frame ready */
   d->csr[5] |= DEC21140_CSR5_RI;
   dev_dec21140_update_irq_status(d);
   return(TRUE);
}

/* Handle the DEC21140 RX ring */
static int dev_dec21140_handle_rxring(netio_desc_t *nio,
                                      u_char *pkt,ssize_t pkt_len,
                                      struct dec21140_data *d)
{
   /* 
    * Don't start receive if the RX ring address has not been set
    * and if the SR bit in CSR6 is not set yet.
    */
   if ((d->csr[3] == 0) || !(d->csr[6] & DEC21140_CSR6_START_RX))
      return(FALSE);

#if DEBUG_RECEIVE
   DEC21140_LOG(d,"receiving a packet of %d bytes\n",pkt_len);
   mem_dump(log_file,pkt,pkt_len);
#endif

   /* 
    * Receive only multicast/broadcast trafic + unicast traffic 
    * for this virtual machine.
    */
   if (dec21140_handle_mac_addr(d,pkt))
      return(dev_dec21140_receive_pkt(d,pkt,pkt_len));

   return(FALSE);
}

/* Read a TX descriptor */
static void txdesc_read(struct dec21140_data *d,m_uint32_t txd_addr,
                        struct tx_desc *txd)
{
   /* get the descriptor from VM physical RAM */
   physmem_copy_from_vm(d->vm,txd,txd_addr,sizeof(struct tx_desc));

   /* byte-swapping */
   txd->tdes[0] = vmtoh32(txd->tdes[0]);
   txd->tdes[1] = vmtoh32(txd->tdes[1]);
   txd->tdes[2] = vmtoh32(txd->tdes[2]);
   txd->tdes[3] = vmtoh32(txd->tdes[3]);
}

/* Set the address of the next TX descriptor */
static void txdesc_set_next(struct dec21140_data *d,struct tx_desc *txd)
{
   if (txd->tdes[1] & DEC21140_TXDESC_TER)
      d->tx_current = d->csr[4];
   else {
      if (txd->tdes[1] & DEC21140_TXDESC_TCH)
         d->tx_current = txd->tdes[3];
      else
         d->tx_current += sizeof(struct tx_desc);
   }
}

/* Handle the TX ring (single packet) */
static int dev_dec21140_handle_txring_single(struct dec21140_data *d)
{   
   u_char pkt[DEC21140_MAX_PKT_SIZE],*pkt_ptr;
   u_char setup_frame[DEC21140_SETUP_FRAME_SIZE];
   m_uint32_t tx_start,len1,len2,clen,tot_len;
   struct tx_desc txd0,ctxd,*ptxd;
   int done = FALSE;

   /* 
    * Don't start transmit if the txring address has not been set
    * and if the ST bit in CSR6 is not set yet.
    */
   if ((d->csr[4] == 0) || (!(d->csr[6] & DEC21140_CSR6_START_TX)))
      return(FALSE);

   /* Check if the NIO can transmit */
   if (!netio_can_transmit(d->nio))
      return(FALSE);

   /* Copy the current txring descriptor */
   tx_start = d->tx_current;   
   ptxd = &txd0;
   txdesc_read(d,tx_start,ptxd);

   /* If we don't own the first descriptor, we cannot transmit */
   if (!(txd0.tdes[0] & DEC21140_TXDESC_OWN))
      return(FALSE);

   /* 
    * Ignore setup frames (clear the own bit and skip).
    * We extract unicast MAC addresses to allow only appropriate traffic
    * to pass.
    */
   if (!(txd0.tdes[1] & (DEC21140_TXDESC_FS|DEC21140_TXDESC_LS))) 
   {
      len1 = ptxd->tdes[1] & DEC21140_TXDESC_LEN_MASK;
      len2 = (ptxd->tdes[1] >> 11) & DEC21140_TXDESC_LEN_MASK;

      if (txd0.tdes[1] & DEC21140_TXDESC_SET) {
         physmem_copy_from_vm(d->vm,setup_frame,ptxd->tdes[2],
                              sizeof(setup_frame));
         dec21140_update_mac_addr(d,setup_frame);
      }

      txdesc_set_next(d,ptxd);
      goto clear_txd0_own_bit;
   }

#if DEBUG_TRANSMIT
   DEC21140_LOG(d,"dec21140_handle_txring: 1st desc: "
                "tdes[0]=0x%x, tdes[1]=0x%x, tdes[2]=0x%x, tdes[3]=0x%x\n",
                ptxd->tdes[0],ptxd->tdes[1],ptxd->tdes[2],ptxd->tdes[3]);
#endif

   /* Empty packet for now */
   pkt_ptr = pkt;
   tot_len = 0;

   do {
#if DEBUG_TRANSMIT
      DEC21140_LOG(d,"dec21140_handle_txring: loop: "
                   "tdes[0]=0x%x, tdes[1]=0x%x, tdes[2]=0x%x, tdes[3]=0x%x\n",
                   ptxd->tdes[0],ptxd->tdes[1],ptxd->tdes[2],ptxd->tdes[3]);
#endif

      if (!(ptxd->tdes[0] & DEC21140_TXDESC_OWN)) {
         DEC21140_LOG(d,"dec21140_handle_txring: descriptor not owned!\n");
         return(FALSE);
      }

      len1 = ptxd->tdes[1] & DEC21140_TXDESC_LEN_MASK;
      len2 = (ptxd->tdes[1] >> 11) & DEC21140_TXDESC_LEN_MASK;
      clen = len1 + len2;

      /* Be sure that we have either len1 or len2 not null */
      if (clen != 0)
      {
         if (len1 != 0)
            physmem_copy_from_vm(d->vm,pkt_ptr,ptxd->tdes[2],len1);
         
         if ((len2 != 0) && !(ptxd->tdes[1] & DEC21140_TXDESC_TCH))
            physmem_copy_from_vm(d->vm,pkt_ptr+len1,ptxd->tdes[3],len2);
      }

      pkt_ptr += clen;
      tot_len += clen;

      /* Clear the OWN bit if this is not the first descriptor */
      if (!(ptxd->tdes[1] & DEC21140_TXDESC_FS))
         physmem_copy_u32_to_vm(d->vm,d->tx_current,0);

      /* Go to the next descriptor */
      txdesc_set_next(d,ptxd);

      /* 
       * Copy the next txring descriptor (ignore setup frames that 
       * have both FS and LS bit cleared).
       */
      if (!(ptxd->tdes[1] & (DEC21140_TXDESC_LS|DEC21140_TXDESC_SET))) {
         txdesc_read(d,d->tx_current,&ctxd);
         ptxd = &ctxd;
      } else
         done = TRUE;
   }while(!done);

   if (tot_len != 0) {
#if DEBUG_TRANSMIT
      DEC21140_LOG(d,"sending packet of %u bytes\n",tot_len);
      mem_dump(log_file,pkt,tot_len);
#endif
      /* rewrite ISL header if required */
      cisco_isl_rewrite(pkt,tot_len);

      /* send it on wire */
      netio_send(d->nio,pkt,tot_len);
   }

 clear_txd0_own_bit:
   /* Clear the OWN flag of the first descriptor */
   physmem_copy_u32_to_vm(d->vm,tx_start,0);
 
   /* Interrupt on completion ? */
   if (txd0.tdes[1] & DEC21140_TXDESC_IC) {      
      d->csr[5] |= DEC21140_CSR5_TI;
      dev_dec21140_update_irq_status(d);
   }
   
   return(TRUE);
}

/* Handle the TX ring */
static int dev_dec21140_handle_txring(struct dec21140_data *d)
{  
   int i;

   for(i=0;i<DEC21140_TXRING_PASS_COUNT;i++)
      if (!dev_dec21140_handle_txring_single(d))
         break;

   netio_clear_bw_stat(d->nio);
   return(TRUE);
}

/*
 * pci_dec21140_read()
 *
 * Read a PCI register.
 */
static m_uint32_t pci_dec21140_read(cpu_gen_t *cpu,struct pci_device *dev,
                                    int reg)
{   
   struct dec21140_data *d = dev->priv_data;

#if DEBUG_PCI_REGS
   DEC21140_LOG(d,"read C%s(%u)\n",pci_cfgreg_name(reg),reg);
#endif

   switch (reg) {
      case DEC21140_PCI_CFID_REG_OFFSET:
         return(0x00091011);
      case DEC21140_PCI_CFRV_REG_OFFSET:
         return(0x02000011);
      case DEC21140_PCI_CBMA_REG_OFFSET:
         return(d->dev->phys_addr);
      default:
         return(0);
   }
}

/*
 * pci_dec21140_write()
 *
 * Write a PCI register.
 */
static void pci_dec21140_write(cpu_gen_t *cpu,struct pci_device *dev,
                               int reg,m_uint32_t value)
{
   struct dec21140_data *d = dev->priv_data;

#if DEBUG_PCI_REGS
   DEC21140_LOG(d,"write C%s(%u) value 0x%x\n",pci_cfgreg_name(reg),reg,value);
#endif

   switch(reg) {
      case DEC21140_PCI_CBMA_REG_OFFSET:
         vm_map_device(cpu->vm,d->dev,(m_uint64_t)value);
         DEC21140_LOG(d,"registers are mapped at 0x%x\n",value);
         break;
   }
}

/* 
 * dev_dec21140_init()
 *
 * Generic DEC21140 initialization code.
 */
struct dec21140_data *dev_dec21140_init(vm_instance_t *vm,char *name,
                                        struct pci_bus *pci_bus,int pci_device,
                                        int irq)
{
   struct dec21140_data *d;
   struct pci_device *pci_dev;
   struct vdevice *dev;

   /* Allocate the private data structure for DEC21140 */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"%s (DEC21140): out of memory\n",name);
      return NULL;
   }

   memset(d,0,sizeof(*d));

   /* Add as PCI device */
   pci_dev = pci_dev_add(pci_bus,name,
                         DEC21140_PCI_VENDOR_ID,DEC21140_PCI_PRODUCT_ID,
                         pci_device,0,irq,
                         d,NULL,pci_dec21140_read,pci_dec21140_write);

   if (!pci_dev) {
      fprintf(stderr,"%s (DEC21140): unable to create PCI device.\n",name);
      goto err_pci_dev;
   }

   /* Create the device itself */
   if (!(dev = dev_create(name))) {
      fprintf(stderr,"%s (DEC21140): unable to create device.\n",name);
      goto err_dev;
   }

   d->name     = name;
   d->vm       = vm;
   d->pci_dev  = pci_dev;
   d->dev      = dev;

   /* Basic register setup */
   d->csr[0]   = 0xfff80000;
   d->csr[5]   = 0xfc000000;
   d->csr[8]   = 0xfffe0000;

   dev->phys_addr = 0;
   dev->phys_len  = 0x20000;
   dev->handler   = dev_dec21140_access;
   dev->priv_data = d;
   return(d);

 err_dev:
   pci_dev_remove(pci_dev);
 err_pci_dev:
   free(d);
   return NULL;
}

/* Remove a DEC21140 device */
void dev_dec21140_remove(struct dec21140_data *d)
{
   if (d != NULL) {
      pci_dev_remove(d->pci_dev);
      vm_unbind_device(d->vm,d->dev);
      cpu_group_rebuild_mts(d->vm->cpu_group);
      free(d->dev);
      free(d);
   }
}

/* Bind a NIO to DEC21140 device */
int dev_dec21140_set_nio(struct dec21140_data *d,netio_desc_t *nio)
{   
   /* check that a NIO is not already bound */
   if (d->nio != NULL)
      return(-1);

   d->nio = nio;
   d->tx_tid = ptask_add((ptask_callback)dev_dec21140_handle_txring,d,NULL);
   netio_rxl_add(nio,(netio_rx_handler_t)dev_dec21140_handle_rxring,d,NULL);
   return(0);
}

/* Unbind a NIO from a DEC21140 device */
void dev_dec21140_unset_nio(struct dec21140_data *d)
{
   if (d->nio != NULL) {
      ptask_remove(d->tx_tid);
      netio_rxl_remove(d->nio);
      d->nio = NULL;
   }
}
