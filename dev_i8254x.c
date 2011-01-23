/*
 * Cisco router simulation platform.
 * Copyright (C) 2007 Christophe Fillot.  All rights reserved.
 *
 * Intel i8254x (Wiseman/Livengood) Ethernet chip emulation.
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
#include "dev_i8254x.h"

/* Debugging flags */
#define DEBUG_MII_REGS   0
#define DEBUG_ACCESS     0
#define DEBUG_TRANSMIT   0
#define DEBUG_RECEIVE    0
#define DEBUG_UNKNOWN    0

/* Intel i8254x PCI vendor/product codes */
#define I8254X_PCI_VENDOR_ID    0x8086
#define I8254X_PCI_PRODUCT_ID   0x1001

/* Maximum packet size */
#define I8254X_MAX_PKT_SIZE  16384

/* Register list */
#define I8254X_REG_CTRL      0x0000  /* Control Register */
#define I8254X_REG_STATUS    0x0008  /* Device Status Register */
#define I8254X_REG_CTRLEXT   0x0018  /* Extended Control Register */
#define I8254X_REG_MDIC      0x0020  /* MDI Control Register */
#define I8254X_REG_FCAL      0x0028  /* Flow Control Address Low */
#define I8254X_REG_FCAH      0x002c  /* Flow Control Address High */
#define I8254X_REG_FCT       0x0030  /* Flow Control Type */
#define I8254X_REG_VET       0x0038  /* VLAN Ether Type */
#define I8254X_REG_ICR       0x00c0  /* Interrupt Cause Read */
#define I8254X_REG_ITR       0x00c4  /* Interrupt Throttling Register */
#define I8254X_REG_ICS       0x00c8  /* Interrupt Cause Set Register */
#define I8254X_REG_IMS       0x00d0  /* Interrupt Mask Set/Read Register */
#define I8254X_REG_IMC       0x00d8  /* Interrupt Mask Clear Register */
#define I8254X_REG_RCTL      0x0100  /* Receive Control Register */
#define I8254X_REG_FCTTV     0x0170  /* Flow Control Transmit Timer Value */
#define I8254X_REG_TXCW      0x0178  /* Transmit Configuration Word */
#define I8254X_REG_RXCW      0x0180  /* Receive Configuration Word */
#define I8254X_REG_TCTL      0x0400  /* Transmit Control Register */
#define I8254X_REG_TIPG      0x0410  /* Transmit Inter Packet Gap  */

#define I8254X_REG_LEDCTL    0x0E00  /* LED Control */
#define I8254X_REG_PBA       0x1000  /* Packet Buffer Allocation */

#define I8254X_REG_RDBAL     0x2800  /* RX Descriptor Base Address Low */
#define I8254X_REG_RDBAH     0x2804  /* RX Descriptor Base Address High */
#define I8254X_REG_RDLEN     0x2808  /* RX Descriptor Length */
#define I8254X_REG_RDH       0x2810  /* RX Descriptor Head */
#define I8254X_REG_RDT       0x2818  /* RX Descriptor Tail */
#define I8254X_REG_RDTR      0x2820  /* RX Delay Timer Register */
#define I8254X_REG_RXDCTL    0x3828  /* RX Descriptor Control */
#define I8254X_REG_RADV      0x282c  /* RX Int. Absolute Delay Timer */
#define I8254X_REG_RSRPD     0x2c00  /* RX Small Packet Detect Interrupt */

#define I8254X_REG_TXDMAC    0x3000  /* TX DMA Control */
#define I8254X_REG_TDBAL     0x3800  /* TX Descriptor Base Address Low */
#define I8254X_REG_TDBAH     0x3804  /* TX Descriptor Base Address Low */
#define I8254X_REG_TDLEN     0x3808  /* TX Descriptor Length */
#define I8254X_REG_TDH       0x3810  /* TX Descriptor Head */
#define I8254X_REG_TDT       0x3818  /* TX Descriptor Tail */
#define I8254X_REG_TIDV      0x3820  /* TX Interrupt Delay Value */
#define I8254X_REG_TXDCTL    0x3828  /* TX Descriptor Control */
#define I8254X_REG_TADV      0x382c  /* TX Absolute Interrupt Delay Value */
#define I8254X_REG_TSPMT     0x3830  /* TCP Segmentation Pad & Min Threshold */

#define I8254X_REG_RXCSUM    0x5000  /* RX Checksum Control */

/* Register list for i8254x */
#define I82542_REG_RDTR      0x0108  /* RX Delay Timer Register */
#define I82542_REG_RDBAL     0x0110  /* RX Descriptor Base Address Low */
#define I82542_REG_RDBAH     0x0114  /* RX Descriptor Base Address High */
#define I82542_REG_RDLEN     0x0118  /* RX Descriptor Length */
#define I82542_REG_RDH       0x0120  /* RDH for i82542 */
#define I82542_REG_RDT       0x0128  /* RDT for i82542 */
#define I82542_REG_TDBAL     0x0420  /* TX Descriptor Base Address Low */
#define I82542_REG_TDBAH     0x0424  /* TX Descriptor Base Address Low */
#define I82542_REG_TDLEN     0x0428  /* TX Descriptor Length */
#define I82542_REG_TDH       0x0430  /* TDH for i82542 */
#define I82542_REG_TDT       0x0438  /* TDT for i82542 */

/* CTRL - Control Register (0x0000) */
#define I8254X_CTRL_FD               0x00000001  /* Full Duplex */
#define I8254X_CTRL_LRST             0x00000008  /* Link Reset */
#define I8254X_CTRL_ASDE             0x00000020  /* Auto-speed detection */
#define I8254X_CTRL_SLU              0x00000040  /* Set Link Up */
#define I8254X_CTRL_ILOS             0x00000080  /* Invert Loss of Signal */
#define I8254X_CTRL_SPEED_MASK       0x00000300  /* Speed selection */
#define I8254X_CTRL_SPEED_SHIFT      8
#define I8254X_CTRL_FRCSPD           0x00000800  /* Force Speed */
#define I8254X_CTRL_FRCDPLX          0x00001000  /* Force Duplex */
#define I8254X_CTRL_SDP0_DATA        0x00040000  /* SDP0 data */
#define I8254X_CTRL_SDP1_DATA        0x00080000  /* SDP1 data */
#define I8254X_CTRL_SDP0_IODIR       0x00400000  /* SDP0 direction */
#define I8254X_CTRL_SDP1_IODIR       0x00800000  /* SDP1 direction */
#define I8254X_CTRL_RST              0x04000000  /* Device Reset */
#define I8254X_CTRL_RFCE             0x08000000  /* RX Flow Ctrl Enable */
#define I8254X_CTRL_TFCE             0x10000000  /* TX Flow Ctrl Enable */
#define I8254X_CTRL_VME              0x40000000  /* VLAN Mode Enable */
#define I8254X_CTRL_PHY_RST          0x80000000  /* PHY reset */

/* STATUS - Device Status Register (0x0008) */
#define I8254X_STATUS_FD             0x00000001  /* Full Duplex */
#define I8254X_STATUS_LU             0x00000002  /* Link Up */
#define I8254X_STATUS_TXOFF          0x00000010  /* Transmit paused */
#define I8254X_STATUS_TBIMODE        0x00000020  /* TBI Mode */
#define I8254X_STATUS_SPEED_MASK     0x000000C0  /* Link Speed setting */
#define I8254X_STATUS_SPEED_SHIFT    6
#define I8254X_STATUS_ASDV_MASK      0x00000300  /* Auto Speed Detection */
#define I8254X_STATUS_ASDV_SHIFT     8
#define I8254X_STATUS_PCI66          0x00000800  /* PCI bus speed */
#define I8254X_STATUS_BUS64          0x00001000  /* PCI bus width */
#define I8254X_STATUS_PCIX_MODE      0x00002000  /* PCI-X mode */
#define I8254X_STATUS_PCIXSPD_MASK   0x0000C000  /* PCI-X speed */
#define I8254X_STATUS_PCIXSPD_SHIFT  14

/* CTRL_EXT - Extended Device Control Register (0x0018) */
#define I8254X_CTRLEXT_PHY_INT       0x00000020  /* PHY interrupt */
#define I8254X_CTRLEXT_SDP6_DATA     0x00000040  /* SDP6 data */
#define I8254X_CTRLEXT_SDP7_DATA     0x00000080  /* SDP7 data */
#define I8254X_CTRLEXT_SDP6_IODIR    0x00000400  /* SDP6 direction */
#define I8254X_CTRLEXT_SDP7_IODIR    0x00000800  /* SDP7 direction */
#define I8254X_CTRLEXT_ASDCHK        0x00001000  /* Auto-Speed Detect Chk */
#define I8254X_CTRLEXT_EE_RST        0x00002000  /* EEPROM reset */
#define I8254X_CTRLEXT_SPD_BYPS      0x00008000  /* Speed Select Bypass */
#define I8254X_CTRLEXT_RO_DIS        0x00020000  /* Relaxed Ordering Dis. */
#define I8254X_CTRLEXT_LNKMOD_MASK   0x00C00000  /* Link Mode */
#define I8254X_CTRLEXT_LNKMOD_SHIFT  22

/* MDIC - MDI Control Register (0x0020) */
#define I8254X_MDIC_DATA_MASK        0x0000FFFF  /* Data */
#define I8254X_MDIC_REG_MASK         0x001F0000  /* PHY Register */
#define I8254X_MDIC_REG_SHIFT        16
#define I8254X_MDIC_PHY_MASK         0x03E00000  /* PHY Address */
#define I8254X_MDIC_PHY_SHIFT        21
#define I8254X_MDIC_OP_MASK          0x0C000000  /* Opcode */
#define I8254X_MDIC_OP_SHIFT         26
#define I8254X_MDIC_R                0x10000000  /* Ready */
#define I8254X_MDIC_I                0x20000000  /* Interrupt Enable */
#define I8254X_MDIC_E                0x40000000  /* Error */

/* ICR - Interrupt Cause Read (0x00c0) */
#define I8254X_ICR_TXDW         0x00000001  /* TX Desc Written back */
#define I8254X_ICR_TXQE         0x00000002  /* TX Queue Empty */
#define I8254X_ICR_LSC          0x00000004  /* Link Status Change */
#define I8254X_ICR_RXSEQ        0x00000008  /* RX Sequence Error */
#define I8254X_ICR_RXDMT0       0x00000010  /* RX Desc min threshold reached */
#define I8254X_ICR_RXO          0x00000040  /* RX Overrun */
#define I8254X_ICR_RXT0         0x00000080  /* RX Timer Interrupt */
#define I8254X_ICR_MDAC         0x00000200  /* MDIO Access Complete */
#define I8254X_ICR_RXCFG        0x00000400
#define I8254X_ICR_PHY_INT      0x00001000  /* PHY Interrupt */
#define I8254X_ICR_GPI_SDP6     0x00002000  /* GPI on SDP6 */
#define I8254X_ICR_GPI_SDP7     0x00004000  /* GPI on SDP7 */
#define I8254X_ICR_TXD_LOW      0x00008000  /* TX Desc low threshold hit */
#define I8254X_ICR_SRPD         0x00010000  /* Small RX packet detected */

/* RCTL - Receive Control Register (0x0100) */
#define I8254X_RCTL_EN          0x00000002  /* Receiver Enable */
#define I8254X_RCTL_SBP         0x00000004  /* Store Bad Packets */
#define I8254X_RCTL_UPE         0x00000008  /* Unicast Promiscuous Enabled */
#define I8254X_RCTL_MPE         0x00000010  /* Xcast Promiscuous Enabled */
#define I8254X_RCTL_LPE         0x00000020  /* Long Packet Reception Enable */
#define I8254X_RCTL_LBM_MASK    0x000000C0  /* Loopback Mode */
#define I8254X_RCTL_LBM_SHIFT   6
#define I8254X_RCTL_RDMTS_MASK  0x00000300  /* RX Desc Min Threshold Size */
#define I8254X_RCTL_RDMTS_SHIFT 8
#define I8254X_RCTL_MO_MASK     0x00003000  /* Multicast Offset */
#define I8254X_RCTL_MO_SHIFT    12
#define I8254X_RCTL_BAM         0x00008000  /* Broadcast Accept Mode */
#define I8254X_RCTL_BSIZE_MASK  0x00030000  /* RX Buffer Size */
#define I8254X_RCTL_BSIZE_SHIFT 16
#define I8254X_RCTL_VFE         0x00040000  /* VLAN Filter Enable */
#define I8254X_RCTL_CFIEN       0x00080000  /* CFI Enable */
#define I8254X_RCTL_CFI         0x00100000  /* Canonical Form Indicator Bit */
#define I8254X_RCTL_DPF         0x00400000  /* Discard Pause Frames */
#define I8254X_RCTL_PMCF        0x00800000  /* Pass MAC Control Frames */
#define I8254X_RCTL_BSEX        0x02000000  /* Buffer Size Extension */
#define I8254X_RCTL_SECRC       0x04000000  /* Strip Ethernet CRC */

/* TCTL - Transmit Control Register (0x0400) */
#define I8254X_TCTL_EN          0x00000002  /* Transmit Enable */
#define I8254X_TCTL_PSP         0x00000008  /* Pad short packets */
#define I8254X_TCTL_SWXOFF      0x00400000  /* Software XOFF Transmission */

/* PBA - Packet Buffer Allocation (0x1000) */
#define I8254X_PBA_RXA_MASK     0x0000FFFF  /* RX Packet Buffer */
#define I8254X_PBA_RXA_SHIFT    0
#define I8254X_PBA_TXA_MASK     0xFFFF0000  /* TX Packet Buffer */
#define I8254X_PBA_TXA_SHIFT    16

/* Flow Control Type */
#define I8254X_FCT_TYPE_DEFAULT  0x8808

/* === TX Descriptor fields === */

/* TX Packet Length (word 2) */
#define I8254X_TXDESC_LEN_MASK  0x0000ffff

/* TX Descriptor CMD field (word 2) */
#define I8254X_TXDESC_IDE       0x80000000  /* Interrupt Delay Enable */
#define I8254X_TXDESC_VLE       0x40000000  /* VLAN Packet Enable */
#define I8254X_TXDESC_DEXT      0x20000000  /* Extension */
#define I8254X_TXDESC_RPS       0x10000000  /* Report Packet Sent */
#define I8254X_TXDESC_RS        0x08000000  /* Report Status */
#define I8254X_TXDESC_IC        0x04000000  /* Insert Checksum */
#define I8254X_TXDESC_IFCS      0x02000000  /* Insert FCS */
#define I8254X_TXDESC_EOP       0x01000000  /* End Of Packet */

/* TX Descriptor STA field (word 3) */
#define I8254X_TXDESC_TU        0x00000008  /* Transmit Underrun */
#define I8254X_TXDESC_LC        0x00000004  /* Late Collision */
#define I8254X_TXDESC_EC        0x00000002  /* Excess Collisions */
#define I8254X_TXDESC_DD        0x00000001  /* Descriptor Done */

/* === RX Descriptor fields === */

/* RX Packet Length (word 2) */
#define I8254X_RXDESC_LEN_MASK  0x0000ffff

/* RX Descriptor STA field (word 3) */
#define I8254X_RXDESC_PIF       0x00000080  /* Passed In-exact Filter */
#define I8254X_RXDESC_IPCS      0x00000040  /* IP cksum calculated */
#define I8254X_RXDESC_TCPCS     0x00000020  /* TCP cksum calculated */
#define I8254X_RXDESC_VP        0x00000008  /* Packet is 802.1Q */
#define I8254X_RXDESC_IXSM      0x00000004  /* Ignore cksum indication */
#define I8254X_RXDESC_EOP       0x00000002  /* End Of Packet */
#define I8254X_RXDESC_DD        0x00000001  /* Descriptor Done */

/* Intel i8254x private data */
struct i8254x_data {
   char *name;

   /* Lock test */
   pthread_mutex_t lock;

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

   /* Interrupt registers */
   m_uint32_t icr,imr;

   /* Device Control Register */
   m_uint32_t ctrl;

   /* Extended Control Register */
   m_uint32_t ctrl_ext;

   /* Flow Control registers */
   m_uint32_t fcal,fcah,fct;

   /* RX Delay Timer */
   m_uint32_t rdtr;

   /* RX/TX Control Registers */
   m_uint32_t rctl,tctl;

   /* RX buffer size (computed from RX control register */
   m_uint32_t rx_buf_size;

   /* RX/TX ring base addresses */
   m_uint64_t rx_addr,tx_addr;

   /* RX/TX descriptor length */
   m_uint32_t rdlen,tdlen;

   /* RX/TX descriptor head and tail */
   m_uint32_t rdh,rdt,tdh,tdt;

   /* TX packet buffer */
   m_uint8_t tx_buffer[I8254X_MAX_PKT_SIZE];

   /* RX IRQ count */
   m_uint32_t rx_irq_cnt;

   /* MII/PHY handling */
   u_int mii_state;
   u_int mii_bit;
   u_int mii_opcode;
   u_int mii_phy;
   u_int mii_reg;
   u_int mii_data_pos;
   u_int mii_data;
   u_int mii_regs[32][32];
};

/* TX descriptor */
struct tx_desc {
   m_uint32_t tdes[4];
};

/* RX descriptor */
struct rx_desc {
   m_uint32_t rdes[4];
};

#define LVG_LOCK(d)   pthread_mutex_lock(&(d)->lock)
#define LVG_UNLOCK(d) pthread_mutex_unlock(&(d)->lock)

/* Log an message */
#define LVG_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* Read a MII register */
static m_uint16_t mii_reg_read(struct i8254x_data *d)
{
#if DEBUG_MII_REGS
   LVG_LOG(d,"MII PHY read %d reg %d\n",d->mii_phy,d->mii_reg);
#endif

   switch(d->mii_reg) {
      case 0x00:
         return((d->mii_regs[d->mii_phy][d->mii_reg] & ~0x8200) | 0x2000);
      case 0x01:
         return(0x782c);
      case 0x02:
         return(0x0013);
      case 0x03:
         return(0x61d4);
      case 0x05:
         return(0x41e1);
      case 0x06:
         return(0x0001);
      case 0x11:
         return(0x4700);
      default:
         return(d->mii_regs[d->mii_phy][d->mii_reg]);
   }
}

/* Write a MII register */
static void mii_reg_write(struct i8254x_data *d)
{
#if DEBUG_MII_REGS
   LVG_LOG(d,"MII PHY write %d reg %d value %04x\n",
             d->mii_phy,d->mii_reg,d->mii_data);
#endif
   assert(d->mii_phy < 32);
   assert(d->mii_reg < 32);
   d->mii_regs[d->mii_phy][d->mii_reg] = d->mii_data;
}

enum {
   MII_OPCODE_READ = 1,
   MII_OPCODE_WRITE,
};

/* MII Finite State Machine */
static void mii_access(struct i8254x_data *d)
{
   switch(d->mii_state) {
      case 0:   /* reset */
         d->mii_phy = 0;
         d->mii_reg = 0;
         d->mii_data_pos = 15;
         d->mii_data = 0;

      case 1:   /* idle */
         if (!d->mii_bit)
            d->mii_state = 2;
         else
            d->mii_state = 1;
         break;

      case 2:   /* start */
         d->mii_state = d->mii_bit ? 3 : 0;
         break;

      case 3:   /* opcode */
         d->mii_state = d->mii_bit ? 4 : 5;
         break;

      case 4:   /* read: opcode "10" */
         if (!d->mii_bit) {
            d->mii_opcode = MII_OPCODE_READ;
            d->mii_state = 6;
         } else {
            d->mii_state = 0;
         }
         break;

      case 5:   /* write: opcode "01" */
         if (d->mii_bit) {
            d->mii_opcode = MII_OPCODE_WRITE;
            d->mii_state = 6;
         } else {
            d->mii_state = 0;
         }
         break;

      case 6 ... 10:   /* phy */
         d->mii_phy <<= 1;
         d->mii_phy |= d->mii_bit;
         d->mii_state++;
         break;

      case 11 ... 15:  /* reg */
         d->mii_reg <<= 1;
         d->mii_reg |= d->mii_bit;
         d->mii_state++;
         break;

      case 16 ... 17:  /* ta */
         if (d->mii_opcode == MII_OPCODE_READ)
            d->mii_state = 18;
         else
            d->mii_state++;
         break;

      case 18:
         if (d->mii_opcode == MII_OPCODE_READ) {
            d->mii_data = mii_reg_read(d);
            d->mii_state++;
         }

      case 19 ... 35:
         if (d->mii_opcode == MII_OPCODE_READ) {
            d->mii_bit = (d->mii_data >> d->mii_data_pos) & 0x1;
         } else {
            d->mii_data |= d->mii_bit << d->mii_data_pos;
         }

         if (!d->mii_data_pos) {
            if (d->mii_opcode == MII_OPCODE_WRITE)
               mii_reg_write(d);
            d->mii_state = 0;
         } else {
            d->mii_state++;
         }        

         d->mii_data_pos--;
         break;

      default:
         printf("MII: impossible state %u!\n",d->mii_state);
   }
}

/* Update the interrupt status */
static inline void dev_i8254x_update_irq_status(struct i8254x_data *d)
{
   if (d->icr & d->imr)
      pci_dev_trigger_irq(d->vm,d->pci_dev);
   else 
      pci_dev_clear_irq(d->vm,d->pci_dev);
}

/* Compute RX buffer size */
static inline void dev_i8254x_set_rx_buf_size(struct i8254x_data *d)
{
   m_uint32_t bsize;

   bsize = (d->rctl & I8254X_RCTL_BSIZE_MASK) >> I8254X_RCTL_BSIZE_SHIFT;

   if (!(d->rctl & I8254X_RCTL_BSEX)) {
      /* Standard buffer sizes */
      switch(bsize) {
         case 0:
            d->rx_buf_size = 2048;
            break;
         case 1:
            d->rx_buf_size = 1024;
            break;
         case 2:
            d->rx_buf_size = 512;
            break;
         case 3:
            d->rx_buf_size = 256;
            break;
      }
   } else {
      /* Extended buffer sizes */
      switch(bsize) {
         case 0:
            d->rx_buf_size = 0;  /* invalid */
            break;
         case 1:
            d->rx_buf_size = 16384;
            break;
         case 2:
            d->rx_buf_size = 8192;
            break;
         case 3:
            d->rx_buf_size = 4096;
            break;
      }
   }
}

/*
 * dev_i8254x_access()
 */
void *dev_i8254x_access(cpu_gen_t *cpu,struct vdevice *dev,
                        m_uint32_t offset,u_int op_size,u_int op_type,
                        m_uint64_t *data)
{
   struct i8254x_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0x0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->name,"read  access to offset=0x%x, pc=0x%llx, size=%u\n",
              offset,cpu_get_pc(cpu),op_size);
   } else {
      cpu_log(cpu,d->name,"write access to offset=0x%x, pc=0x%llx, "
              "val=0x%llx, size=%u\n",offset,cpu_get_pc(cpu),*data,op_size);
   }
#endif

   LVG_LOCK(d);

   switch(offset) {
#if 0 /* TODO */
      case 0x180:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF; //0xDC004020; //1 << 31;
         break;
#endif

      /* Link is Up and Full Duplex */
      case I8254X_REG_STATUS:
         if (op_type == MTS_READ)
            *data = I8254X_STATUS_LU | I8254X_STATUS_FD;
         break;

      /* Device Control Register */
      case I8254X_REG_CTRL:
         if (op_type == MTS_WRITE)
            d->ctrl = *data;
         else
            *data = d->ctrl;
         break;

      /* Extended Device Control Register */
      case I8254X_REG_CTRLEXT:
         if (op_type == MTS_WRITE) {
            /* MDIO clock set ? */
            if (!(d->ctrl_ext & I8254X_CTRLEXT_SDP6_DATA) && 
                (*data & I8254X_CTRLEXT_SDP6_DATA)) 
             {
               if (*data & I8254X_CTRLEXT_SDP7_IODIR)
                  d->mii_bit = (*data & I8254X_CTRLEXT_SDP7_DATA) ? 1 : 0;

               mii_access(d);
            }

            d->ctrl_ext = *data;
         } else {
            *data = d->ctrl_ext;

            if (!(d->ctrl_ext & I8254X_CTRLEXT_SDP7_IODIR)) {
               if (d->mii_bit)
                  *data |= I8254X_CTRLEXT_SDP7_DATA;
               else
                  *data &= ~I8254X_CTRLEXT_SDP7_DATA;
            }           
         }
         break;

      /* XXX */
      case I8254X_REG_MDIC:
         if (op_type == MTS_READ)
            *data = 1 << 28;
         break;

      /* 
       * Interrupt Cause Read Register.
       *
       * Notice: a read clears all interrupt bits.
       */
      case I8254X_REG_ICR:
         if (op_type == MTS_READ) {
            *data = d->icr;
            d->icr = 0;

            if (d->rx_irq_cnt > 0) {
               d->icr |= I8254X_ICR_RXT0;
               d->rx_irq_cnt--;
            }

            dev_i8254x_update_irq_status(d);
         }
         break;

      /* Interrupt Cause Set Register */
      case I8254X_REG_ICS:
         if (op_type == MTS_WRITE) {
            d->icr |= *data;
            dev_i8254x_update_irq_status(d);
         }
         break;

      /* Interrupt Mask Set/Read Register */
      case I8254X_REG_IMS:
         if (op_type == MTS_WRITE) {
            d->imr |= *data;
            dev_i8254x_update_irq_status(d);
         } else {
            *data = d->imr;
         }
         break;

      /* Interrupt Mask Clear Register */
      case I8254X_REG_IMC:
         if (op_type == MTS_WRITE) {
            d->imr &= ~(*data);
            dev_i8254x_update_irq_status(d);
         }
         break;

      /* Receive Control Register */
      case I8254X_REG_RCTL:
         if (op_type == MTS_READ) {
            *data = d->rctl;
         } else {
            d->rctl = *data;
            dev_i8254x_set_rx_buf_size(d);
         }
         break;

      /* Transmit Control Register */
      case I8254X_REG_TCTL:
         if (op_type == MTS_READ)
            *data = d->tctl;
         else
            d->tctl = *data;
         break;

      /* RX Descriptor Base Address Low */
      case I8254X_REG_RDBAL:
      case I82542_REG_RDBAL:
         if (op_type == MTS_WRITE) {
            d->rx_addr &= 0xFFFFFFFF00000000ULL;
            d->rx_addr |= (m_uint32_t)(*data);
         } else {
            *data = (m_uint32_t)d->rx_addr;
         }
         break;

      /* RX Descriptor Base Address High */
      case I8254X_REG_RDBAH:
      case I82542_REG_RDBAH:
         if (op_type == MTS_WRITE) {
            d->rx_addr &= 0x00000000FFFFFFFFULL;
            d->rx_addr |= *data << 32;
         } else {
            *data = d->rx_addr >> 32;
         }
         break;

      /* TX Descriptor Base Address Low */
      case I8254X_REG_TDBAL:
      case I82542_REG_TDBAL:
         if (op_type == MTS_WRITE) {
            d->tx_addr &= 0xFFFFFFFF00000000ULL;
            d->tx_addr |= (m_uint32_t)(*data);
         } else {
            *data = (m_uint32_t)d->tx_addr;
         }
         break;

      /* TX Descriptor Base Address High */
      case I8254X_REG_TDBAH:
      case I82542_REG_TDBAH:
         if (op_type == MTS_WRITE) {
            d->tx_addr &= 0x00000000FFFFFFFFULL;
            d->tx_addr |= *data << 32;
         } else {
            *data = d->tx_addr >> 32;
         }
         break;
      
      /* RX Descriptor Length */
      case I8254X_REG_RDLEN:
      case I82542_REG_RDLEN:
         if (op_type == MTS_WRITE)
            d->rdlen = *data & 0xFFF80;
         else
            *data = d->rdlen;
         break;

      /* TX Descriptor Length */
      case I8254X_REG_TDLEN:
      case I82542_REG_TDLEN:
         if (op_type == MTS_WRITE)
            d->tdlen = *data & 0xFFF80;
         else
            *data = d->tdlen;
         break;
         
      /* RX Descriptor Head */
      case I82542_REG_RDH:
      case I8254X_REG_RDH:
         if (op_type == MTS_WRITE)
            d->rdh = *data & 0xFFFF;
         else
            *data = d->rdh;
         break;

      /* RX Descriptor Tail */
      case I8254X_REG_RDT:
      case I82542_REG_RDT:
         if (op_type == MTS_WRITE)
            d->rdt = *data & 0xFFFF;
         else
            *data = d->rdt;
         break;

      /* TX Descriptor Head */
      case I82542_REG_TDH:
      case I8254X_REG_TDH:
         if (op_type == MTS_WRITE)
            d->tdh = *data & 0xFFFF;
         else
            *data = d->tdh;
         break;

      /* TX Descriptor Tail */
      case I82542_REG_TDT:
      case I8254X_REG_TDT:
         if (op_type == MTS_WRITE)
            d->tdt = *data & 0xFFFF;
         else
            *data = d->tdt;
         break;

      /* Flow Control Address Low */
      case I8254X_REG_FCAL:
         if (op_type == MTS_WRITE)
            d->fcal = *data;
         else
            *data = d->fcal;
         break;

      /* Flow Control Address High */
      case I8254X_REG_FCAH:
         if (op_type == MTS_WRITE)
            d->fcah = *data & 0xFFFF;
         else
            *data = d->fcah;
         break;

      /* Flow Control Type */
      case I8254X_REG_FCT:
         if (op_type == MTS_WRITE)
            d->fct = *data & 0xFFFF;
         else
            *data = d->fct;
         break;

      /* RX Delay Timer */
      case I8254X_REG_RDTR:
      case I82542_REG_RDTR:
         if (op_type == MTS_WRITE)
            d->rdtr = *data & 0xFFFF;
         else
            *data = d->rdtr;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->name,
                    "read access to unknown offset=0x%x, "
                    "pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,d->name,
                    "write access to unknown offset=0x%x, pc=0x%llx, "
                    "val=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),*data,op_size);
         }
#endif
   }

   LVG_UNLOCK(d);
   return NULL;
}

/* Read a TX descriptor */
static void txdesc_read(struct i8254x_data *d,m_uint64_t txd_addr,
                        struct tx_desc *txd)
{
   /* Get the descriptor from VM physical RAM */
   physmem_copy_from_vm(d->vm,txd,txd_addr,sizeof(struct tx_desc));

   /* byte-swapping */
   txd->tdes[0] = vmtoh32(txd->tdes[0]);
   txd->tdes[1] = vmtoh32(txd->tdes[1]);
   txd->tdes[2] = vmtoh32(txd->tdes[2]);
   txd->tdes[3] = vmtoh32(txd->tdes[3]);
}

/* Handle the TX ring */
static int dev_i8254x_handle_txring(struct i8254x_data *d)
{
   m_uint64_t txd_addr,buf_addr;
   m_uint32_t buf_len,tot_len;
   m_uint32_t norm_len,icr;
   struct tx_desc txd;
   m_uint8_t *pkt_ptr;

   /* Transmit Enabled ? */
   if (!(d->tctl & I8254X_TCTL_EN))
      return(FALSE);

   /* If Head is at same position than Tail, the ring is empty */
   if (d->tdh == d->tdt)
      return(FALSE);

   LVG_LOCK(d);
   
   /* Empty packet for now */
   pkt_ptr = d->tx_buffer;
   tot_len = 0;
   icr = 0;

   while(d->tdh != d->tdt) {
      txd_addr = d->tx_addr + (d->tdh * sizeof(struct tx_desc));
      txdesc_read(d,txd_addr,&txd);

      /* Copy the packet buffer */
      buf_addr = ((m_uint64_t)txd.tdes[1] << 32) | txd.tdes[0];
      buf_len  = txd.tdes[2] & I8254X_TXDESC_LEN_MASK;

#if DEBUG_TRANSMIT
      LVG_LOG(d,"copying data from 0x%8.8llx (buf_len=%u)\n",buf_addr,buf_len);
#endif
      norm_len = normalize_size(buf_len,4,0);
      physmem_copy_from_vm(d->vm,pkt_ptr,buf_addr,norm_len);
      mem_bswap32(pkt_ptr,norm_len);

      pkt_ptr += buf_len;
      tot_len += buf_len;

      /* Write the descriptor done bit if required */
      if (txd.tdes[2] & I8254X_TXDESC_RS) {
         txd.tdes[3] |= I8254X_TXDESC_DD;
         icr |= I8254X_ICR_TXDW;
         physmem_copy_u32_to_vm(d->vm,txd_addr+0x0c,txd.tdes[3]);
      }

      /* Go to the next descriptor. Wrap ring if we are at end */
      if (++d->tdh == (d->tdlen / sizeof(struct tx_desc)))
         d->tdh = 0;

      /* End of packet ? */
      if (txd.tdes[2] & I8254X_TXDESC_EOP) {
#if DEBUG_TRANSMIT
         LVG_LOG(d,"sending packet of %u bytes\n",tot_len);
         mem_dump(log_file,d->tx_buffer,tot_len);
#endif
         netio_send(d->nio,d->tx_buffer,tot_len);
         break;
      }
   }

   if (d->tdh == d->tdt)
      icr |= I8254X_ICR_TXQE;

   /* Update the interrupt cause register and trigger IRQ if needed */
   d->icr |= icr;
   dev_i8254x_update_irq_status(d);
   LVG_UNLOCK(d);
   return(TRUE);
}

/* Read a RX descriptor */
static void rxdesc_read(struct i8254x_data *d,m_uint64_t rxd_addr,
                        struct rx_desc *rxd)
{
   /* Get the descriptor from VM physical RAM */
   physmem_copy_from_vm(d->vm,rxd,rxd_addr,sizeof(struct rx_desc));

   /* byte-swapping */
   rxd->rdes[0] = vmtoh32(rxd->rdes[0]);
   rxd->rdes[1] = vmtoh32(rxd->rdes[1]);
   rxd->rdes[2] = vmtoh32(rxd->rdes[2]);
   rxd->rdes[3] = vmtoh32(rxd->rdes[3]);
}

/*
 * Put a packet in the RX ring.
 */
static int dev_i8254x_receive_pkt(struct i8254x_data *d,
                                  u_char *pkt,ssize_t pkt_len)
{
   m_uint64_t rxd_addr,buf_addr;
   m_uint32_t cur_len,norm_len,tot_len;
   struct rx_desc rxd;
   m_uint32_t icr;
   u_char *pkt_ptr;

   if (!d->rx_buf_size)
      return(FALSE);

   LVG_LOCK(d);
   pkt_ptr = pkt;
   tot_len = pkt_len;
   icr = 0;

   while(tot_len > 0) {
      /* No descriptor available: RX overrun condition */
      if (d->rdh == d->rdt) {
         icr |= I8254X_ICR_RXO;
         break;
      }

      rxd_addr = d->rx_addr + (d->rdh * sizeof(struct rx_desc));
      rxdesc_read(d,rxd_addr,&rxd);

      cur_len = (tot_len > d->rx_buf_size) ? d->rx_buf_size : tot_len;

      /* Copy the packet data into the RX buffer */
      buf_addr = ((m_uint64_t)rxd.rdes[1] << 32) | rxd.rdes[0];

      norm_len = normalize_size(cur_len,4,0);
      mem_bswap32(pkt_ptr,norm_len);
      physmem_copy_to_vm(d->vm,pkt_ptr,buf_addr,norm_len);
      tot_len -= cur_len;
      pkt_ptr += cur_len;

      /* Set length field */
      rxd.rdes[2] = cur_len;

      /* Set the status */
      rxd.rdes[3] = I8254X_RXDESC_IXSM|I8254X_RXDESC_DD;
    
      if (!tot_len) {
         rxd.rdes[3] |= I8254X_RXDESC_EOP;
         icr |= I8254X_ICR_RXT0;
         d->rx_irq_cnt++;
         rxd.rdes[2] += 4;   /* FCS */
      }

      /* Write back updated descriptor */
      physmem_copy_u32_to_vm(d->vm,rxd_addr+0x08,rxd.rdes[2]);
      physmem_copy_u32_to_vm(d->vm,rxd_addr+0x0c,rxd.rdes[3]);

      /* Goto to the next descriptor, and wrap if necessary */
      if (++d->rdh == (d->rdlen / sizeof(struct rx_desc)))
         d->rdh = 0;
   }

   /* Update the interrupt cause register and trigger IRQ if needed */
   d->icr |= icr;
   dev_i8254x_update_irq_status(d);
   LVG_UNLOCK(d);
   return(TRUE);
}

/* Handle the RX ring */
static int dev_i8254x_handle_rxring(netio_desc_t *nio,
                                    u_char *pkt,ssize_t pkt_len,
                                    struct i8254x_data *d)
{
   /* 
    * Don't start receive if RX has not been enabled in RCTL register.
    */
   if (!(d->rctl & I8254X_RCTL_EN))
      return(FALSE);

#if DEBUG_RECEIVE
   LVG_LOG(d,"receiving a packet of %d bytes\n",pkt_len);
   mem_dump(log_file,pkt,pkt_len);
#endif

   /* 
    * Receive only multicast/broadcast trafic + unicast traffic 
    * for this virtual machine.
    */
   //if (dec21140_handle_mac_addr(d,pkt))
   return(dev_i8254x_receive_pkt(d,pkt,pkt_len));

   return(FALSE);
}

/*
 * pci_i8254x_read()
 *
 * Read a PCI register.
 */
static m_uint32_t pci_i8254x_read(cpu_gen_t *cpu,struct pci_device *dev,
                                  int reg)
{
   struct i8254x_data *d = dev->priv_data;

#if DEBUG_PCI_REGS
   I8254X_LOG(d,"read PCI register 0x%x\n",reg);
#endif

   switch (reg) {
      case 0x00:
         return((I8254X_PCI_PRODUCT_ID << 16) | I8254X_PCI_VENDOR_ID);
      case 0x08:
         return(0x02000003);
      case PCI_REG_BAR0:
         return(d->dev->phys_addr);
      default:
         return(0);
   }
}

/*
 * pci_i8254x_write()
 *
 * Write a PCI register.
 */
static void pci_i8254x_write(cpu_gen_t *cpu,struct pci_device *dev,
                             int reg,m_uint32_t value)
{
   struct i8254x_data *d = dev->priv_data;

#if DEBUG_PCI_REGS
   LVG_LOG(d,"write PCI register 0x%x, value 0x%x\n",reg,value);
#endif

   switch(reg) {
      case PCI_REG_BAR0:
         vm_map_device(cpu->vm,d->dev,(m_uint64_t)value);
         LVG_LOG(d,"registers are mapped at 0x%x\n",value);
         break;
   }
}

/*
 * dev_i8254x_init()
 */
struct i8254x_data *
dev_i8254x_init(vm_instance_t *vm,char *name,int interface_type,
                struct pci_bus *pci_bus,int pci_device,int irq)
{
   struct i8254x_data *d;
   struct pci_device *pci_dev;
   struct vdevice *dev;

   /* Allocate the private data structure for I8254X */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"%s (i8254x): out of memory\n",name);
      return NULL;
   }

   memset(d,0,sizeof(*d));
   pthread_mutex_init(&d->lock,NULL);

   /* Add as PCI device */
   pci_dev = pci_dev_add(pci_bus,name,
                         I8254X_PCI_VENDOR_ID,I8254X_PCI_PRODUCT_ID,
                         pci_device,0,irq,
                         d,NULL,pci_i8254x_read,pci_i8254x_write);

   if (!pci_dev) {
      fprintf(stderr,"%s (i8254x): unable to create PCI device.\n",name);
      goto err_pci_dev;
   }

   /* Create the device itself */
   if (!(dev = dev_create(name))) {
      fprintf(stderr,"%s (i8254x): unable to create device.\n",name);
      goto err_dev;
   }

   d->name     = name;
   d->vm       = vm;
   d->pci_dev  = pci_dev;
   d->dev      = dev;

   dev->phys_addr = 0;
   dev->phys_len  = 0x10000;
   dev->handler   = dev_i8254x_access;
   dev->priv_data = d;
   return(d);

 err_dev:
   pci_dev_remove(pci_dev);
 err_pci_dev:
   free(d);
   return NULL;
}

/* Remove an Intel i8254x device */
void dev_i8254x_remove(struct i8254x_data *d)
{
   if (d != NULL) {
      pci_dev_remove(d->pci_dev);
      vm_unbind_device(d->vm,d->dev);
      cpu_group_rebuild_mts(d->vm->cpu_group);
      free(d->dev);
      free(d);
   }
}

/* Bind a NIO to an Intel i8254x device */
int dev_i8254x_set_nio(struct i8254x_data *d,netio_desc_t *nio)
{
   /* check that a NIO is not already bound */
   if (d->nio != NULL)
      return(-1);

   d->nio = nio;
   d->tx_tid = ptask_add((ptask_callback)dev_i8254x_handle_txring,d,NULL);
   netio_rxl_add(nio,(netio_rx_handler_t)dev_i8254x_handle_rxring,d,NULL);
   return(0);
}

/* Unbind a NIO from an Intel i8254x device */
void dev_i8254x_unset_nio(struct i8254x_data *d)
{
   if (d->nio != NULL) {
      ptask_remove(d->tx_tid);
      netio_rxl_remove(d->nio);
      d->nio = NULL;
   }
}
