/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Galileo GT64010/GT64120A/GT96100A system controller.
 *
 * The DMA stuff is not complete, only "normal" transfers are working
 * (source and destination addresses incrementing).
 *
 * Also, these transfers are "instantaneous" from a CPU point-of-view: when
 * a channel is enabled, the transfer is immediately done. So, this is not 
 * very realistic.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "net.h"
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_gt.h"

/* Debugging flags */
#define DEBUG_UNKNOWN   0
#define DEBUG_DMA       0
#define DEBUG_MII       0
#define DEBUG_ETH_TX    0
#define DEBUG_ETH_RX    0
#define DEBUG_ETH_HASH  0

/* PCI identification */
#define PCI_VENDOR_GALILEO           0x11ab  /* Galileo Technology */
#define PCI_PRODUCT_GALILEO_GT64010  0x0146  /* GT-64010 */
#define PCI_PRODUCT_GALILEO_GT64011  0x4146  /* GT-64011 */
#define PCI_PRODUCT_GALILEO_GT64120  0x4620  /* GT-64120 */
#define PCI_PRODUCT_GALILEO_GT96100  0x9653  /* GT-96100 */

/* === Global definitions === */

/* Interrupt High Cause Register */
#define GT_IHCR_ETH0_SUM     0x00000001
#define GT_IHCR_ETH1_SUM     0x00000002
#define GT_IHCR_SDMA_SUM     0x00000010

/* Serial Cause Register */
#define GT_SCR_ETH0_SUM      0x00000001
#define GT_SCR_ETH1_SUM      0x00000002
#define GT_SCR_SDMA_SUM      0x00000010

/* === DMA definitions === */
#define GT_DMA_CHANNELS   4

#define GT_DMA_FLYBY_ENABLE  0x00000001  /* FlyBy Enable */  
#define GT_DMA_FLYBY_RDWR    0x00000002  /* SDRAM Read/Write (FlyBy) */
#define GT_DMA_SRC_DIR       0x0000000c  /* Source Direction */
#define GT_DMA_DST_DIR       0x00000030  /* Destination Direction */
#define GT_DMA_DATA_LIMIT    0x000001c0  /* Data Transfer Limit */
#define GT_DMA_CHAIN_MODE    0x00000200  /* Chained Mode */
#define GT_DMA_INT_MODE      0x00000400  /* Interrupt Mode */
#define GT_DMA_TRANS_MODE    0x00000800  /* Transfer Mode */
#define GT_DMA_CHAN_ENABLE   0x00001000  /* Channel Enable */
#define GT_DMA_FETCH_NEXT    0x00002000  /* Fetch Next Record */
#define GT_DMA_ACT_STATUS    0x00004000  /* DMA Activity Status */
#define GT_DMA_SDA           0x00008000  /* Source/Destination Alignment */
#define GT_DMA_MDREQ         0x00010000  /* Mask DMA Requests */
#define GT_DMA_CDE           0x00020000  /* Close Descriptor Enable */
#define GT_DMA_EOTE          0x00040000  /* End-of-Transfer (EOT) Enable */
#define GT_DMA_EOTIE         0x00080000  /* EOT Interrupt Enable */
#define GT_DMA_ABORT         0x00100000  /* Abort DMA Transfer */
#define GT_DMA_SLP           0x00600000  /* Override Source Address */
#define GT_DMA_DLP           0x01800000  /* Override Dest Address */
#define GT_DMA_RLP           0x06000000  /* Override Record Address */
#define GT_DMA_REQ_SRC       0x10000000  /* DMA Request Source */

/* Galileo DMA channel */
struct dma_channel {
   m_uint32_t byte_count;
   m_uint32_t src_addr;
   m_uint32_t dst_addr;
   m_uint32_t cdptr;
   m_uint32_t nrptr;
   m_uint32_t ctrl;
};

/* === Ethernet definitions === */
#define GT_ETH_PORTS     2
#define GT_MAX_PKT_SIZE  2048

/* SMI register */
#define GT_SMIR_DATA_MASK      0x0000FFFF
#define GT_SMIR_PHYAD_MASK     0x001F0000    /* PHY Device Address */
#define GT_SMIR_PHYAD_SHIFT    16
#define GT_SMIR_REGAD_MASK     0x03e00000    /* PHY Device Register Address */
#define GT_SMIR_REGAD_SHIFT    21
#define GT_SMIR_OPCODE_MASK    0x04000000    /* Opcode (0: write, 1: read) */
#define GT_SMIR_OPCODE_READ    0x04000000
#define GT_SMIR_RVALID_FLAG    0x08000000    /* Read Valid */
#define GT_SMIR_BUSY_FLAG      0x10000000    /* Busy: 1=op in progress */

/* PCR: Port Configuration Register */
#define GT_PCR_PM              0x00000001    /* Promiscuous mode */
#define GT_PCR_RBM             0x00000002    /* Reject broadcast mode */
#define GT_PCR_PBF             0x00000004    /* Pass bad frames */
#define GT_PCR_EN              0x00000080    /* Port Enabled/Disabled */
#define GT_PCR_LPBK            0x00000300    /* Loopback mode */
#define GT_PCR_FC              0x00000400    /* Force collision */
#define GT_PCR_HS              0x00001000    /* Hash size */
#define GT_PCR_HM              0x00002000    /* Hash mode */
#define GT_PCR_HDM             0x00004000    /* Hash default mode */
#define GT_PCR_HD              0x00008000    /* Duplex Mode */
#define GT_PCR_ISL             0x70000000    /* ISL enabled (0x06) */
#define GT_PCR_ACCS            0x80000000    /* Accelerate Slot Time */

/* PCXR: Port Configuration Extend Register */
#define GT_PCXR_IGMP           0x00000001    /* IGMP packet capture */
#define GT_PCXR_SPAN           0x00000002    /* BPDU packet capture */
#define GT_PCXR_PAR            0x00000004    /* Partition Enable */
#define GT_PCXR_PRIOTX         0x00000038    /* Priority weight for TX */
#define GT_PCXR_PRIORX         0x000000C0    /* Priority weight for RX */
#define GT_PCXR_PRIORX_OV      0x00000100    /* Prio RX override */
#define GT_PCXR_DPLX_EN        0x00000200    /* Autoneg for Duplex */
#define GT_PCXR_FCTL_EN        0x00000400    /* Autoneg for 802.3x */
#define GT_PCXR_FLP            0x00000800    /* Force Link Pass */
#define GT_PCXR_FCTL           0x00001000    /* Flow Control Mode */
#define GT_PCXR_MFL            0x0000C000    /* Maximum Frame Length */
#define GT_PCXR_MIB_CLR_MODE   0x00010000    /* MIB counters clear mode */
#define GT_PCXR_SPEED          0x00040000    /* Port Speed */
#define GT_PCXR_SPEED_EN       0x00080000    /* Autoneg for Speed */
#define GT_PCXR_RMII_EN        0x00100000    /* RMII Enable */
#define GT_PCXR_DSCP_EN        0x00200000    /* DSCP decoding enable */

/* PCMR: Port Command Register */
#define GT_PCMR_FJ             0x00008000    /* Force Jam / Flow Control */

/* PSR: Port Status Register */
#define GT_PSR_SPEED           0x00000001    /* Speed: 10/100 Mb/s (100=>1)*/
#define GT_PSR_DUPLEX          0x00000002    /* Duplex (1: full) */
#define GT_PSR_FCTL            0x00000004    /* Flow Control Mode */
#define GT_PSR_LINK            0x00000008    /* Link Up/Down */
#define GT_PSR_PAUSE           0x00000010    /* Flow-control disabled state */
#define GT_PSR_TXLOW           0x00000020    /* TX Low priority status */
#define GT_PSR_TXHIGH          0x00000040    /* TX High priority status */
#define GT_PSR_TXINP           0x00000080    /* TX in Progress */

/* SDCR: SDMA Configuration Register */
#define GT_SDCR_RC             0x0000003c    /* Retransmit count */
#define GT_SDCR_BLMR           0x00000040    /* Big/Little Endian RX mode */
#define GT_SDCR_BLMT           0x00000080    /* Big/Litlle Endian TX mode */
#define GT_SDCR_POVR           0x00000100    /* PCI override */
#define GT_SDCR_RIFB           0x00000200    /* RX IRQ on frame boundary */
#define GT_SDCR_BSZ            0x00003000    /* Burst size */

/* SDCMR: SDMA Command Register */
#define GT_SDCMR_ERD           0x00000080    /* Enable RX DMA */
#define GT_SDCMR_AR            0x00008000    /* Abort Receive */
#define GT_SDCMR_STDH          0x00010000    /* Stop TX High */
#define GT_SDCMR_STDL          0x00020000    /* Stop TX Low */
#define GT_SDCMR_TXDH          0x00800000    /* Start TX High */
#define GT_SDCMR_TXDL          0x01000000    /* Start TX Low */
#define GT_SDCMR_AT            0x80000000    /* Abort Transmit */

/* ICR: Interrupt Cause Register */
#define GT_ICR_RXBUF           0x00000001    /* RX Buffer returned to host */
#define GT_ICR_TXBUFH          0x00000004    /* TX Buffer High */
#define GT_ICR_TXBUFL          0x00000008    /* TX Buffer Low */
#define GT_ICR_TXENDH          0x00000040    /* TX End High */
#define GT_ICR_TXENDL          0x00000080    /* TX End Low */
#define GT_ICR_RXERR           0x00000100    /* RX Error */
#define GT_ICR_TXERRH          0x00000400    /* TX Error High */
#define GT_ICR_TXERRL          0x00000800    /* TX Error Low */
#define GT_ICR_RXOVR           0x00001000    /* RX Overrun */
#define GT_ICR_TXUDR           0x00002000    /* TX Underrun */
#define GT_ICR_RXBUFQ0         0x00010000    /* RX Buffer in Prio Queue 0 */
#define GT_ICR_RXBUFQ1         0x00020000    /* RX Buffer in Prio Queue 1 */
#define GT_ICR_RXBUFQ2         0x00040000    /* RX Buffer in Prio Queue 2 */
#define GT_ICR_RXBUFQ3         0x00080000    /* RX Buffer in Prio Queue 3 */
#define GT_ICR_RXERRQ0         0x00010000    /* RX Error in Prio Queue 0 */
#define GT_ICR_RXERRQ1         0x00020000    /* RX Error in Prio Queue 1 */
#define GT_ICR_RXERRQ2         0x00040000    /* RX Error in Prio Queue 2 */
#define GT_ICR_RXERRQ3         0x00080000    /* RX Error in Prio Queue 3 */
#define GT_ICR_MII_STC         0x10000000    /* MII PHY Status Change */
#define GT_ICR_SMI_DONE        0x20000000    /* SMI Command Done */
#define GT_ICR_INT_SUM         0x80000000    /* Ethernet Interrupt Summary */
#define GT_ICR_MASK            0x7FFFFFFF

/* Ethernet hash entry */
#define GT_HTE_VALID           0x00000001    /* Valid entry */
#define GT_HTE_SKIP            0x00000002    /* Skip entry in a chain */
#define GT_HTE_RD              0x00000004    /* 0: Discard, 1: Receive */
#define GT_HTE_ADDR_MASK       0x7fffffffffff8ULL

#define GT_HTE_HOPNUM          12            /* Hash Table Hop Number */

enum {
   GT_HTLOOKUP_MISS,
   GT_HTLOOKUP_MATCH,
   GT_HTLOOKUP_HOP_EXCEEDED,
};

/* TX Descriptor */
#define GT_TXDESC_OWN          0x80000000    /* Ownership */
#define GT_TXDESC_AM           0x40000000    /* Auto-mode */
#define GT_TXDESC_EI           0x00800000    /* Enable Interrupt */
#define GT_TXDESC_GC           0x00400000    /* Generate CRC */
#define GT_TXDESC_P            0x00040000    /* Padding */
#define GT_TXDESC_F            0x00020000    /* First buffer of packet */
#define GT_TXDESC_L            0x00010000    /* Last buffer of packet */
#define GT_TXDESC_ES           0x00008000    /* Error Summary */
#define GT_TXDESC_RC           0x00003c00    /* Retransmit Count */
#define GT_TXDESC_COL          0x00000200    /* Collision */
#define GT_TXDESC_RL           0x00000100    /* Retransmit Limit Error */
#define GT_TXDESC_UR           0x00000040    /* Underrun Error */
#define GT_TXDESC_LC           0x00000020    /* Late Collision Error */

#define GT_TXDESC_BC_MASK      0xFFFF0000    /* Number of bytes to transmit */
#define GT_TXDESC_BC_SHIFT     16

/* RX Descriptor */
#define GT_RXDESC_OWN          0x80000000    /* Ownership */
#define GT_RXDESC_AM           0x40000000    /* Auto-mode */
#define GT_RXDESC_EI           0x00800000    /* Enable Interrupt */
#define GT_RXDESC_F            0x00020000    /* First buffer of packet */
#define GT_RXDESC_L            0x00010000    /* Last buffer of packet */
#define GT_RXDESC_ES           0x00008000    /* Error Summary */
#define GT_RXDESC_IGMP         0x00004000    /* IGMP packet detected */
#define GT_RXDESC_HE           0x00002000    /* Hash Table Expired */
#define GT_RXDESC_M            0x00001000    /* Missed Frame */
#define GT_RXDESC_FT           0x00000800    /* Frame Type (802.3/Ethernet) */
#define GT_RXDESC_SF           0x00000100    /* Short Frame Error */
#define GT_RXDESC_MFL          0x00000080    /* Maximum Frame Length Error */
#define GT_RXDESC_OR           0x00000040    /* Overrun Error */
#define GT_RXDESC_COL          0x00000010    /* Collision */
#define GT_RXDESC_CE           0x00000001    /* CRC Error */

#define GT_RXDESC_BC_MASK      0x0000FFFF    /* Byte count */
#define GT_RXDESC_BS_MASK      0xFFFF0000    /* Buffer size */
#define GT_RXDESC_BS_SHIFT     16

/* RX/TX descriptor */
struct eth_desc {
   m_uint32_t buf_size;
   m_uint32_t cmd_stat;
   m_uint32_t next_ptr;
   m_uint32_t buf_ptr;
};

/* Galileo Ethernet port */
struct eth_port {
   netio_desc_t *nio;

   /* First and Current RX descriptors (4 queues) */
   m_uint32_t rx_start[4],rx_current[4];

   /* Current TX descriptors (2 queues) */
   m_uint32_t tx_current[2];

   /* Port registers */
   m_uint32_t pcr,pcxr,pcmr,psr;
   
   /* SDMA registers */
   m_uint32_t sdcr,sdcmr;

   /* Interrupt register */
   m_uint32_t icr,imr;

   /* Hash Table pointer */
   m_uint32_t ht_addr;

   /* Ethernet MIB counters */
   m_uint32_t rx_bytes,tx_bytes,rx_frames,tx_frames;
};

/* Galileo GT64xxx/GT96xxx system controller */
struct gt_data {
   char *name;
   vm_obj_t vm_obj;
   struct vdevice dev;
   struct pci_device *pci_dev;
   vm_instance_t *vm;

   struct pci_bus *bus[2];
   struct dma_channel dma[GT_DMA_CHANNELS];
   m_uint32_t int_cause_reg;
   m_uint32_t int_mask_reg;

   /* Ethernet ports (GT-96100) */
   u_int eth_irq;
   ptask_id_t eth_tx_tid;
   struct eth_port eth_ports[GT_ETH_PORTS];
   m_uint32_t smi_reg;
   m_uint16_t mii_regs[32][32];
};

/* Log a GT message */
#define GT_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* Update the interrupt status */
static void gt_update_irq_status(struct gt_data *gt_data)
{
   if (gt_data->pci_dev) {
      if (gt_data->int_cause_reg & gt_data->int_mask_reg)
         pci_dev_trigger_irq(gt_data->vm,gt_data->pci_dev);
      else
         pci_dev_clear_irq(gt_data->vm,gt_data->pci_dev);
   }
}

/* Fetch a DMA record (chained mode) */
static void gt_dma_fetch_rec(vm_instance_t *vm,struct dma_channel *channel)
{
   m_uint32_t ptr;
 
#if DEBUG_DMA
   vm_log(vm,"GT_DMA","fetching record at address 0x%x\n",channel->nrptr);
#endif

   /* fetch the record from RAM */
   ptr = channel->nrptr;
   channel->byte_count = swap32(physmem_copy_u32_from_vm(vm,ptr));
   channel->src_addr   = swap32(physmem_copy_u32_from_vm(vm,ptr+0x04));
   channel->dst_addr   = swap32(physmem_copy_u32_from_vm(vm,ptr+0x08));
   channel->nrptr      = swap32(physmem_copy_u32_from_vm(vm,ptr+0x0c));
   
   /* clear the "fetch next record bit" */
   channel->ctrl &= ~GT_DMA_FETCH_NEXT;
}

/* Handle control register of a DMA channel */
static void gt_dma_handle_ctrl(struct gt_data *gt_data,int chan_id)
{
   struct dma_channel *channel = &gt_data->dma[chan_id];
   vm_instance_t *vm = gt_data->vm;
   int done;

   if (channel->ctrl & GT_DMA_FETCH_NEXT) {
      if (channel->nrptr == 0) {
         vm_log(vm,"GT_DMA","trying to load a NULL DMA record...\n");
         return;
      }

      gt_dma_fetch_rec(vm,channel);
   }

   if (channel->ctrl & GT_DMA_CHAN_ENABLE) 
   {
      do {
         done = TRUE;

#if DEBUG_DMA
         vm_log(vm,"GT_DMA",
                "starting transfer from 0x%x to 0x%x (size=%u bytes)\n",
                channel->src_addr,channel->dst_addr,
                channel->byte_count & 0xFFFF);
#endif
         physmem_dma_transfer(vm,channel->src_addr,channel->dst_addr,
                              channel->byte_count & 0xFFFF);

         /* chained mode */
         if (!(channel->ctrl & GT_DMA_CHAIN_MODE)) {
            if (channel->nrptr) {
               gt_dma_fetch_rec(vm,channel);
               done = FALSE;
            }
         }
      }while(!done);

#if DEBUG_DMA
      vm_log(vm,"GT_DMA","finished transfer.\n");
#endif
      /* Trigger DMA interrupt */
      gt_data->int_cause_reg |= 1 << (4 + chan_id);
      gt_update_irq_status(gt_data);
   }
}

#define DMA_REG(ch,reg_name) \
   if (op_type == MTS_WRITE) \
      gt_data->dma[ch].reg_name = swap32(*data); \
   else \
      *data = swap32(gt_data->dma[ch].reg_name);

/* Handle a DMA channel */
static int gt_dma_access(cpu_gen_t *cpu,struct vdevice *dev,
                         m_uint32_t offset,u_int op_size,u_int op_type,
                         m_uint64_t *data)
{
   struct gt_data *gt_data = dev->priv_data;

   switch(offset) {
      /* DMA Source Address */
      case 0x810: DMA_REG(0,src_addr); return(1);
      case 0x814: DMA_REG(1,src_addr); return(1);
      case 0x818: DMA_REG(2,src_addr); return(1);
      case 0x81c: DMA_REG(3,src_addr); return(1);

      /* DMA Destination Address */
      case 0x820: DMA_REG(0,dst_addr); return(1);
      case 0x824: DMA_REG(1,dst_addr); return(1);
      case 0x828: DMA_REG(2,dst_addr); return(1);
      case 0x82c: DMA_REG(3,dst_addr); return(1);

      /* DMA Next Record Pointer */
      case 0x830:
         gt_data->dma[0].cdptr = *data;
         DMA_REG(0,nrptr);
         return(1);

      case 0x834:
         gt_data->dma[1].cdptr = *data;
         DMA_REG(1,nrptr);
         return(1);

      case 0x838:
         gt_data->dma[2].cdptr = *data;
         DMA_REG(2,nrptr);
         return(1);
   
      case 0x83c: 
         gt_data->dma[3].cdptr = *data;
         DMA_REG(3,nrptr);
         return(1);

      /* DMA Channel Control */
      case 0x840:
         DMA_REG(0,ctrl);
         if (op_type == MTS_WRITE) 
            gt_dma_handle_ctrl(gt_data,0);
         return(1);

      case 0x844:
         DMA_REG(1,ctrl);
         if (op_type == MTS_WRITE) 
            gt_dma_handle_ctrl(gt_data,1);
         return(1);

      case 0x848:
         DMA_REG(2,ctrl);
         if (op_type == MTS_WRITE) 
            gt_dma_handle_ctrl(gt_data,2);
         return(1);

      case 0x84c:
         DMA_REG(3,ctrl);
         if (op_type == MTS_WRITE) 
            gt_dma_handle_ctrl(gt_data,3);
         return(1);
   }

   return(0);
}

/*
 * dev_gt64010_access()
 */
void *dev_gt64010_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct gt_data *gt_data = dev->priv_data;
	
   if (op_type == MTS_READ)
      *data = 0;

   if (gt_dma_access(cpu,dev,offset,op_size,op_type,data) != 0)
      return NULL;

   switch(offset) {
      /* ===== DRAM Settings (completely faked, 128 Mb) ===== */
      case 0x008:    /* ras10_low */
         if (op_type == MTS_READ)
            *data = swap32(0x000);
         break;
      case 0x010:    /* ras10_high */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x018:    /* ras32_low */
         if (op_type == MTS_READ)
            *data = swap32(0x080);
         break;
      case 0x020:    /* ras32_high */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x400:    /* ras0_low */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0x404:    /* ras0_high */
         if (op_type == MTS_READ)
            *data = swap32(0xFF);
         break;
      case 0x408:    /* ras1_low */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x40c:    /* ras1_high */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0x410:    /* ras2_low */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0x414:    /* ras2_high */
         if (op_type == MTS_READ)
            *data = swap32(0xFF);
         break;
      case 0x418:    /* ras3_low */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x41c:    /* ras3_high */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0xc08:    /* pci0_cs10 */
         if (op_type == MTS_READ)
            *data = swap32(0xFFF);
         break;
      case 0xc0c:    /* pci0_cs32 */
         if (op_type == MTS_READ)
            *data = swap32(0xFFF);
         break;

      case 0xc00:    /* pci_cmd */
         if (op_type == MTS_READ)
            *data = swap32(0x00008001);
         break;

      /* ===== Interrupt Cause Register ===== */
      case 0xc18:
         if (op_type == MTS_READ) {
            *data = swap32(gt_data->int_cause_reg);
         } else {
            gt_data->int_cause_reg &= swap32(*data);
            gt_update_irq_status(gt_data);
         }
         break;

      /* ===== Interrupt Mask Register ===== */
      case 0xc1c:
         if (op_type == MTS_READ)
            *data = swap32(gt_data->int_mask_reg);
         else {
            gt_data->int_mask_reg = swap32(*data);
            gt_update_irq_status(gt_data);
         }
         break;

      /* ===== PCI Configuration ===== */
      case PCI_BUS_ADDR:    /* pci configuration address (0xcf8) */
         pci_dev_addr_handler(cpu,gt_data->bus[0],op_type,TRUE,data);
         break;

      case PCI_BUS_DATA:    /* pci data address (0xcfc) */
         pci_dev_data_handler(cpu,gt_data->bus[0],op_type,TRUE,data);
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"GT64010","read from addr 0x%x, pc=0x%llx\n",
                    offset,cpu_get_pc(cpu));
         } else {
            cpu_log(cpu,"GT64010","write to addr 0x%x, value=0x%llx, "
                    "pc=0x%llx\n",offset,*data,cpu_get_pc(cpu));
         }
#endif
   }

   return NULL;
}

/*
 * dev_gt64120_access()
 */
void *dev_gt64120_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct gt_data *gt_data = dev->priv_data;
   
   if (op_type == MTS_READ)
      *data = 0;

   if (gt_dma_access(cpu,dev,offset,op_size,op_type,data) != 0)
      return NULL;

   switch(offset) {
      case 0x008:    /* ras10_low */
         if (op_type == MTS_READ)
            *data = swap32(0x000);
         break;
      case 0x010:    /* ras10_high */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x018:    /* ras32_low */
         if (op_type == MTS_READ)
            *data = swap32(0x100);
         break;
      case 0x020:    /* ras32_high */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x400:    /* ras0_low */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0x404:    /* ras0_high */
         if (op_type == MTS_READ)
            *data = swap32(0xFF);
         break;
      case 0x408:    /* ras1_low */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x40c:    /* ras1_high */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0x410:    /* ras2_low */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0x414:    /* ras2_high */
         if (op_type == MTS_READ)
            *data = swap32(0xFF);
         break;
      case 0x418:    /* ras3_low */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x41c:    /* ras3_high */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0xc08:    /* pci0_cs10 */
         if (op_type == MTS_READ)
            *data = swap32(0xFFF);
         break;
      case 0xc0c:    /* pci0_cs32 */
         if (op_type == MTS_READ)
            *data = swap32(0xFFF);
         break;

      case 0xc00:    /* pci_cmd */
         if (op_type == MTS_READ)
            *data = swap32(0x00008001);
         break;

      /* ===== Interrupt Cause Register ===== */
      case 0xc18:
         if (op_type == MTS_READ)
            *data = swap32(gt_data->int_cause_reg);
         else {
            gt_data->int_cause_reg &= swap32(*data);
            gt_update_irq_status(gt_data);
         }
         break;

      /* ===== Interrupt Mask Register ===== */
      case 0xc1c:
         if (op_type == MTS_READ) {
            *data = swap32(gt_data->int_mask_reg);
         } else {
            gt_data->int_mask_reg = swap32(*data);
            gt_update_irq_status(gt_data);
         }
         break;

      /* ===== PCI Bus 1 ===== */
      case 0xcf0:
         pci_dev_addr_handler(cpu,gt_data->bus[1],op_type,TRUE,data);
         break;

      case 0xcf4:
         pci_dev_data_handler(cpu,gt_data->bus[1],op_type,TRUE,data);
         break;
         
      /* ===== PCI Bus 0 ===== */
      case PCI_BUS_ADDR:    /* pci configuration address (0xcf8) */
         pci_dev_addr_handler(cpu,gt_data->bus[0],op_type,TRUE,data);
         break;

      case PCI_BUS_DATA:    /* pci data address (0xcfc) */
         pci_dev_data_handler(cpu,gt_data->bus[0],op_type,TRUE,data);
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"GT64120","read from addr 0x%x, pc=0x%llx\n",
                    offset,cpu_get_pc(cpu));
         } else {
            cpu_log(cpu,"GT64120","write to addr 0x%x, value=0x%llx, "
                    "pc=0x%llx\n",offset,*data,cpu_get_pc(cpu));
         }
#endif
   }

   return NULL;
}

/* Trigger/clear Ethernet interrupt if one or both port have pending events */
static void gt_eth_set_int_status(struct gt_data *d)
{
   if ((d->eth_ports[0].icr & GT_ICR_INT_SUM) ||
       (d->eth_ports[1].icr & GT_ICR_INT_SUM))
      vm_set_irq(d->vm,d->eth_irq);
   else
      vm_clear_irq(d->vm,d->eth_irq);
}

/* Update the Ethernet port interrupt status */
static void gt_eth_update_int_status(struct gt_data *d,struct eth_port *port)
{
   if (port->icr & port->imr & GT_ICR_MASK) {
      port->icr |= GT_ICR_INT_SUM;
   } else {
      port->icr &= ~GT_ICR_INT_SUM;
   }

   gt_eth_set_int_status(d);
}

/* Read a MII register */
static m_uint32_t gt_mii_read(struct gt_data *d)
{
   m_uint8_t port,reg;
   m_uint32_t res = 0;

   port = (d->smi_reg & GT_SMIR_PHYAD_MASK) >> GT_SMIR_PHYAD_SHIFT;
   reg  = (d->smi_reg & GT_SMIR_REGAD_MASK) >> GT_SMIR_REGAD_SHIFT;

#if DEBUG_MII
   GT_LOG(d,"MII: port 0x%4.4x, reg 0x%2.2x: reading.\n",port,reg);
#endif

   if ((port < GT_ETH_PORTS) && (reg < 32)) {
      res = d->mii_regs[port][reg];

      switch(reg) {
         case 0x00:
            res &= ~0x8200; /* clear reset bit and autoneg restart */
            break;
         case 0x01:
#if 0
            if (d->ports[port].nio && bcm5600_mii_port_status(d,port))
               d->mii_output = 0x782C;
            else
               d->mii_output = 0;
#endif
            res = 0x782c;
            break;
         case 0x02:
            res = 0x40;
            break;
         case 0x03:
            res = 0x61d4;
            break;
         case 0x04:
            res = 0x1E1;
            break;
         case 0x05:
            res = 0x41E1;
            break;
         default:
            res = 0;
      }
   }

   /* Mark the data as ready */
   res |= GT_SMIR_RVALID_FLAG;

   return(res);
}

/* Write a MII register */
static void gt_mii_write(struct gt_data *d)
{
   m_uint8_t port,reg;
   m_uint16_t isolation;

   port = (d->smi_reg & GT_SMIR_PHYAD_MASK) >> GT_SMIR_PHYAD_SHIFT;
   reg  = (d->smi_reg & GT_SMIR_REGAD_MASK) >> GT_SMIR_REGAD_SHIFT;

   if ((port < GT_ETH_PORTS) && (reg < 32))
   {
#if DEBUG_MII
      GT_LOG(d,"MII: port 0x%4.4x, reg 0x%2.2x: writing 0x%4.4x\n",
             port,reg,d->smi_reg & GT_SMIR_DATA_MASK);
#endif

      /* Check if PHY isolation status is changing */
      if (reg == 0) {
         isolation = (d->smi_reg ^ d->mii_regs[port][reg]) & 0x400;

         if (isolation) {
#if DEBUG_MII
            GT_LOG(d,"MII: port 0x%4.4x: generating IRQ\n",port);
#endif
            d->eth_ports[port].icr |= GT_ICR_MII_STC;
            gt_eth_update_int_status(d,&d->eth_ports[port]);
         }
      }

      d->mii_regs[port][reg] = d->smi_reg & GT_SMIR_DATA_MASK;
   }
}

/* Handle registers of Ethernet ports */
static int gt_eth_access(cpu_gen_t *cpu,struct vdevice *dev,
                         m_uint32_t offset,u_int op_size,u_int op_type,
                         m_uint64_t *data)
{
   struct gt_data *d = dev->priv_data;
   struct eth_port *port;
   u_int port_id = 0;
   u_int queue;

   if ((offset < 0x80000) || (offset >= 0x90000))
      return(FALSE);

   if (op_type == MTS_WRITE)
      *data = swap32(*data);

   /* Detemine the Ethernet port */
   if ((offset >= 0x84800) && (offset < 0x88800))
      port_id = 0;

   if ((offset >= 0x88800) && (offset < 0x8c800))
      port_id = 1;

   port = &d->eth_ports[port_id];   

   switch(offset) {
      /* SMI register */
      case 0x80810:
         if (op_type == MTS_WRITE) {
            d->smi_reg = *data;

            if (!(d->smi_reg & GT_SMIR_OPCODE_READ))
               gt_mii_write(d);
         } else {
            *data = 0;

            if (d->smi_reg & GT_SMIR_OPCODE_READ)
               *data = gt_mii_read(d);
         }
         break;

      /* ICR: Interrupt Cause Register */
      case 0x84850:
      case 0x88850:
         if (op_type == MTS_READ) {
            *data = port->icr;
         } else {
            port->icr &= *data;
            gt_eth_update_int_status(d,port);
         }
         break;

      /* IMR: Interrupt Mask Register */
      case 0x84858:
      case 0x88858:
         if (op_type == MTS_READ) {
            *data = port->imr;
         } else {
            port->imr = *data;
            gt_eth_update_int_status(d,port);
         }
         break;

      /* PCR: Port Configuration Register */
      case 0x84800:
      case 0x88800:
         if (op_type == MTS_READ)
            *data = port->pcr;
         else
            port->pcr = *data;
         break;

      /* PCXR: Port Configuration Extend Register */
      case 0x84808:
      case 0x88808:
         if (op_type == MTS_READ) {
            *data = port->pcxr;
            *data |= GT_PCXR_SPEED;
         } else
            port->pcxr = *data;
         break;
         
      /* PCMR: Port Command Register */
      case 0x84810:
      case 0x88810:
         if (op_type == MTS_READ)
            *data = port->pcmr;
         else
            port->pcmr = *data;
         break;

      /* Port Status Register */
      case 0x84818:
      case 0x88818:
         if (op_type == MTS_READ)
            *data = 0x0F;
         break;

      /* First RX descriptor */
      case 0x84880:
      case 0x88880:
      case 0x84884:
      case 0x88884:
      case 0x84888:
      case 0x88888:
      case 0x8488C:
      case 0x8888C:
         queue = (offset >> 2) & 0x03;
         if (op_type == MTS_READ)
            *data = port->rx_start[queue];
         else
            port->rx_start[queue] = *data;
         break;

      /* Current RX descriptor */
      case 0x848A0:
      case 0x888A0:
      case 0x848A4:
      case 0x888A4:
      case 0x848A8:
      case 0x888A8:
      case 0x848AC:
      case 0x888AC:
         queue = (offset >> 2) & 0x03;
         if (op_type == MTS_READ)
            *data = port->rx_current[queue];
         else
            port->rx_current[queue] = *data;
         break;

      /* Current TX descriptor */
      case 0x848E0:
      case 0x888E0:
      case 0x848E4:
      case 0x888E4:
         queue = (offset >> 2) & 0x01;
         if (op_type == MTS_READ)
            *data = port->tx_current[queue];
         else
            port->tx_current[queue] = *data;
         break;

      /* Hash Table Pointer */
      case 0x84828:
      case 0x88828:
         if (op_type == MTS_READ)
            *data = port->ht_addr;
         else
            port->ht_addr = *data;
         break;

      /* SDCR: SDMA Configuration Register */
      case 0x84840:
      case 0x88840:
         if (op_type == MTS_READ)
            *data = port->sdcr;
         else
            port->sdcr = *data;
         break;

      /* SDCMR: SDMA Command Register */
      case 0x84848:
      case 0x88848:
         if (op_type == MTS_WRITE) {
            /* Start RX DMA */
            if (*data & GT_SDCMR_ERD) {
               port->sdcmr |= GT_SDCMR_ERD;
               port->sdcmr &= ~GT_SDCMR_AR;
            }

            /* Abort RX DMA */
            if (*data & GT_SDCMR_AR)
               port->sdcmr &= ~GT_SDCMR_ERD;

            /* Start TX High */
            if (*data & GT_SDCMR_TXDH) {
               port->sdcmr |= GT_SDCMR_TXDH;
               port->sdcmr &= ~GT_SDCMR_STDH;
            }

            /* Start TX Low */
            if (*data & GT_SDCMR_TXDL) {
               port->sdcmr |= GT_SDCMR_TXDL;
               port->sdcmr &= ~GT_SDCMR_STDL;
            }

            /* Stop TX High */
            if (*data & GT_SDCMR_STDH) {
               port->sdcmr &= ~GT_SDCMR_TXDH;
               port->sdcmr |= GT_SDCMR_STDH;
            }

            /* Stop TX Low */
            if (*data & GT_SDCMR_STDL) {
               port->sdcmr &= ~GT_SDCMR_TXDL;
               port->sdcmr |= GT_SDCMR_STDL;
            }
         } else {
            *data = port->sdcmr;
         }
         break;

      case 0x85800:
      case 0x89800:
         if (op_type == MTS_READ) {
            *data = port->rx_bytes;
            port->rx_bytes = 0;
         }
         break;

      case 0x85804:
      case 0x89804:
         if (op_type == MTS_READ) {
            *data = port->tx_bytes;
            port->tx_bytes = 0;
         }
         break;

      case 0x85808:
      case 0x89808:
         if (op_type == MTS_READ) {
            *data = port->rx_frames;
            port->rx_frames = 0;
         }
         break;

      case 0x8580C:
      case 0x8980C:
         if (op_type == MTS_READ) {
            *data = port->tx_frames;
            port->tx_frames = 0;
         }
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"GT96100/ETH",
                    "read access to unknown register 0x%x, pc=0x%llx\n",
                    offset,cpu_get_pc(cpu));
         } else {
            cpu_log(cpu,"GT96100/ETH",
                    "write access to unknown register 0x%x, value=0x%llx, "
                    "pc=0x%llx\n",offset,*data,cpu_get_pc(cpu));
         }
#endif
   }

   if (op_type == MTS_READ)
      *data = swap32(*data);

   return(TRUE);
}

/*
 * dev_gt96100_access()
 */
void *dev_gt96100_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct gt_data *gt_data = dev->priv_data;
   
   if (op_type == MTS_READ)
      *data = 0;

   if (gt_dma_access(cpu,dev,offset,op_size,op_type,data) != 0)
      return NULL;

   if (gt_eth_access(cpu,dev,offset,op_size,op_type,data) != 0)
      return NULL;

   switch(offset) {
      /* Watchdog configuration register */
      case 0x101a80:
         break;
         
      /* Watchdog value register */
      case 0x101a84:
         break;

      case 0x008:    /* ras10_low */
         if (op_type == MTS_READ)
            *data = swap32(0x000);
         break;
      case 0x010:    /* ras10_high */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x018:    /* ras32_low */
         if (op_type == MTS_READ)
            *data = swap32(0x100);
         break;
      case 0x020:    /* ras32_high */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x400:    /* ras0_low */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0x404:    /* ras0_high */
         if (op_type == MTS_READ)
            *data = swap32(0xFF);
         break;
      case 0x408:    /* ras1_low */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x40c:    /* ras1_high */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0x410:    /* ras2_low */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0x414:    /* ras2_high */
         if (op_type == MTS_READ)
            *data = swap32(0xFF);
         break;
      case 0x418:    /* ras3_low */
         if (op_type == MTS_READ)
            *data = swap32(0x7F);
         break;
      case 0x41c:    /* ras3_high */
         if (op_type == MTS_READ)
            *data = swap32(0x00);
         break;
      case 0xc08:    /* pci0_cs10 */
         if (op_type == MTS_READ)
            *data = swap32(0xFFF);
         break;
      case 0xc0c:    /* pci0_cs32 */
         if (op_type == MTS_READ)
            *data = swap32(0xFFF);
         break;

      case 0xc00:    /* pci_cmd */
         if (op_type == MTS_READ)
            *data = swap32(0x00008001);
         break;

      /* ===== Interrupt Main Cause Register ===== */
      case 0xc18:
         if (op_type == MTS_READ) {
            *data = gt_data->int_cause_reg;
         
            /* TODO: signal Eth0/Eth1 */
            //*data |= (1 << 30) | (1 << 31) | 1;

            *data = swap32(*data);
         } else {
            gt_data->int_cause_reg &= swap32(*data);
            gt_update_irq_status(gt_data);
         }
         break;

      /* ===== Interrupt Mask Register ===== */
      case 0xc1c:
         if (op_type == MTS_READ) {
            *data = swap32(gt_data->int_mask_reg);
         } else {
            gt_data->int_mask_reg = swap32(*data);
            gt_update_irq_status(gt_data);
         }
         break;

      /* ===== Interrupt High Cause Register ===== */
      case 0xc98:
         if (op_type == MTS_READ) {
            *data = 0;

            /* interrupt on ethernet port 0 ? */
            if (gt_data->eth_ports[0].icr & GT_ICR_INT_SUM)
               *data |= GT_IHCR_ETH0_SUM;

            /* interrupt on ethernet port 1 ? */
            if (gt_data->eth_ports[1].icr & GT_ICR_INT_SUM)
               *data |= GT_IHCR_ETH1_SUM;

            *data = swap32(*data);
         }
         break;

      /* Serial Cause Register */
      case 0x103a00:
         if (op_type == MTS_READ) {
            *data = 0;

            /* interrupt on ethernet port 0 ? */
            if (gt_data->eth_ports[0].icr & GT_ICR_INT_SUM)
               *data |= GT_SCR_ETH0_SUM;

            /* interrupt on ethernet port 1 ? */
            if (gt_data->eth_ports[1].icr & GT_ICR_INT_SUM)
               *data |= GT_SCR_ETH1_SUM;

            gt_update_irq_status(gt_data);
            *data = swap32(*data);
         }
         break;

      /* ===== PCI Bus 1 ===== */
      case 0xcf0:
         pci_dev_addr_handler(cpu,gt_data->bus[1],op_type,TRUE,data);
         break;

      case 0xcf4:
         pci_dev_data_handler(cpu,gt_data->bus[1],op_type,TRUE,data);
         break;
         
      /* ===== PCI Bus 0 ===== */
      case PCI_BUS_ADDR:    /* pci configuration address (0xcf8) */
         pci_dev_addr_handler(cpu,gt_data->bus[0],op_type,TRUE,data);
         break;

      case PCI_BUS_DATA:    /* pci data address (0xcfc) */
         pci_dev_data_handler(cpu,gt_data->bus[0],op_type,TRUE,data);
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"GT96100","read from addr 0x%x, pc=0x%llx\n",
                    offset,cpu_get_pc(cpu));
         } else {
            cpu_log(cpu,"GT96100","write to addr 0x%x, value=0x%llx, "
                    "pc=0x%llx\n",offset,*data,cpu_get_pc(cpu));
         }
#endif
   }

   return NULL;
}

/* Read an Ethernet descriptor */
static void gt_eth_desc_read(struct gt_data *d,m_uint32_t addr,
                             struct eth_desc *desc)
{
   physmem_copy_from_vm(d->vm,desc,addr,sizeof(struct eth_desc));
   
   /* byte-swapping */
   desc->cmd_stat = vmtoh32(desc->cmd_stat);
   desc->buf_size = vmtoh32(desc->buf_size);
   desc->next_ptr = vmtoh32(desc->next_ptr);
   desc->buf_ptr  = vmtoh32(desc->buf_ptr);
}

/* Write an Ethernet descriptor */
static void gt_eth_desc_write(struct gt_data *d,m_uint32_t addr,
                              struct eth_desc *desc)
{
   struct eth_desc tmp;
   
   /* byte-swapping */
   tmp.cmd_stat = vmtoh32(desc->cmd_stat);
   tmp.buf_size = vmtoh32(desc->buf_size);
   tmp.next_ptr = vmtoh32(desc->next_ptr);
   tmp.buf_ptr  = vmtoh32(desc->buf_ptr);

   physmem_copy_to_vm(d->vm,&tmp,addr,sizeof(struct eth_desc));
}

/* Handle a TX queue (single packet) */
static int gt_eth_handle_txqueue(struct gt_data *d,struct eth_port *port,
                                 int queue)
{   
   u_char pkt[GT_MAX_PKT_SIZE],*pkt_ptr;
   struct eth_desc txd0,ctxd,*ptxd;
   m_uint32_t tx_start,tx_current;
   m_uint32_t len,tot_len;
   int abort = FALSE;

   /* Check if this TX queue is active */
   if ((queue == 0) && (port->sdcmr & GT_SDCMR_STDL))
      return(FALSE);

   if ((queue == 1) && (port->sdcmr & GT_SDCMR_STDH))
      return(FALSE);

   /* Copy the current txring descriptor */
   tx_start = tx_current = port->tx_current[queue];

   if (!tx_start)
      return(FALSE);

   ptxd = &txd0;
   gt_eth_desc_read(d,tx_start,ptxd);

   /* If we don't own the first descriptor, we cannot transmit */
   if (!(txd0.cmd_stat & GT_TXDESC_OWN))
      return(FALSE);

   /* Empty packet for now */
   pkt_ptr = pkt;
   tot_len = 0;

   for(;;) {
#if DEBUG_ETH_TX
      GT_LOG(d,"gt_eth_handle_txqueue: loop: "
             "cmd_stat=0x%x, buf_size=0x%x, next_ptr=0x%x, buf_ptr=0x%x\n",
             ptxd->cmd_stat,ptxd->buf_size,ptxd->next_ptr,ptxd->buf_ptr);
#endif

      if (!(ptxd->cmd_stat & GT_TXDESC_OWN)) {
         GT_LOG(d,"gt_eth_handle_txqueue: descriptor not owned!\n");
         abort = TRUE;
         break;
      }

      /* Copy packet data to the buffer */
      len = (ptxd->buf_size & GT_TXDESC_BC_MASK) >> GT_TXDESC_BC_SHIFT;

      physmem_copy_from_vm(d->vm,pkt_ptr,ptxd->buf_ptr,len);
      pkt_ptr += len;
      tot_len += len;

      /* Clear the OWN bit if this is not the first descriptor */
      if (!(ptxd->cmd_stat & GT_TXDESC_F)) {
         ptxd->cmd_stat &= ~GT_TXDESC_OWN;
         physmem_copy_u32_to_vm(d->vm,tx_current,ptxd->cmd_stat);
      }

      tx_current = ptxd->next_ptr;

      /* Last descriptor or no more desc available ? */
      if (ptxd->cmd_stat & GT_TXDESC_L)
         break;

      if (!tx_current) {
         abort = TRUE;
         break;
      }

      /* Fetch the next descriptor */
      gt_eth_desc_read(d,tx_current,&ctxd);
      ptxd = &ctxd;
   }

   if ((tot_len != 0) && !abort) {
#if DEBUG_ETH_TX
      GT_LOG(d,"Ethernet: sending packet of %u bytes\n",tot_len);
      mem_dump(log_file,pkt,tot_len);
#endif
      /* send it on wire */
      netio_send(port->nio,pkt,tot_len);

      /* Update MIB counters */
      port->tx_bytes += tot_len;
      port->tx_frames++;
   }

   /* Clear the OWN flag of the first descriptor */
   txd0.cmd_stat &= ~GT_TXDESC_OWN;
   physmem_copy_u32_to_vm(d->vm,tx_start+4,txd0.cmd_stat);

   port->tx_current[queue] = tx_current;
   
   /* Notify host about transmitted packet */
   if (queue == 0)
      port->icr |= GT_ICR_TXBUFL;
   else
      port->icr |= GT_ICR_TXBUFH;

   if (abort) {
      /* TX underrun */
      port->icr |= GT_ICR_TXUDR;

      if (queue == 0)
         port->icr |= GT_ICR_TXERRL;
      else
         port->icr |= GT_ICR_TXERRH;
   } else {
      /* End of queue has been reached */
      if (!tx_current) {
         if (queue == 0)
            port->icr |= GT_ICR_TXENDL;
         else
            port->icr |= GT_ICR_TXENDH;
      }
   }

   /* Update the interrupt status */
   gt_eth_update_int_status(d,port); 
   return(TRUE);
}

/* Handle TX ring of the specified port */
static void gt_eth_handle_port_txqueues(struct gt_data *d,u_int port)
{
   gt_eth_handle_txqueue(d,&d->eth_ports[port],0);  /* TX Low */
   gt_eth_handle_txqueue(d,&d->eth_ports[port],1);  /* TX High */
}

/* Handle all TX rings of all Ethernet ports */
static int gt_eth_handle_txqueues(struct gt_data *d)
{
   int i;

   for(i=0;i<GT_ETH_PORTS;i++)
      gt_eth_handle_port_txqueues(d,i);

   return(TRUE);
}

/* Inverse a nibble */
static const int inv_nibble[16] = { 
   0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE, 
   0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF 
};

/* Inverse a 9-bit value */
static inline u_int gt_hash_inv_9bit(u_int val)
{
   u_int res;

   res  = inv_nibble[val & 0x0F] << 5;
   res |= inv_nibble[(val & 0xF0) >> 4] << 1;
   res |= (val & 0x100) >> 8;
   return(res);
}

/* 
 * Compute hash value for Ethernet address filtering.
 * Two modes are available (p.271 of the GT96100 doc).
 */
static u_int gt_eth_hash_value(n_eth_addr_t *addr,int mode)
{
   m_uint64_t tmp;
   u_int res;
   int i;

   /* Swap the nibbles */
   for(i=0,tmp=0;i<N_ETH_ALEN;i++) {
      tmp <<= 8;
      tmp |= (inv_nibble[addr->eth_addr_byte[i] & 0x0F]) << 4;
      tmp |= inv_nibble[(addr->eth_addr_byte[i] & 0xF0) >> 4];
   }

   if (mode == 0) {
      /* Fill bits 0:8 */
      res  = (tmp & 0x00000003) | ((tmp & 0x00007f00) >> 6);
      res ^= (tmp & 0x00ff8000) >> 15;
      res ^= (tmp & 0x1ff000000ULL) >> 24;

      /* Fill bits 9:14 */
      res |= (tmp & 0xfc) << 7;
   } else {
      /* Fill bits 0:8 */
      res  = gt_hash_inv_9bit((tmp & 0x00007fc0) >> 6);
      res ^= gt_hash_inv_9bit((tmp & 0x00ff8000) >> 15);
      res ^= gt_hash_inv_9bit((tmp & 0x1ff000000ULL) >> 24);

      /* Fill bits 9:14 */
      res |= (tmp & 0x3f) << 9;
   }

   return(res);
}

/*
 * Walk through the Ethernet hash table.
 */
static int gt_eth_hash_lookup(struct gt_data *d,struct eth_port *port,
                              n_eth_addr_t *addr,m_uint64_t *entry)
{
   m_uint64_t eth_val;
   m_uint32_t hte_addr;
   u_int hash_val;
   int i;

   eth_val  = (m_uint64_t)addr->eth_addr_byte[0] << 3;
   eth_val |= (m_uint64_t)addr->eth_addr_byte[1] << 11;
   eth_val |= (m_uint64_t)addr->eth_addr_byte[2] << 19;
   eth_val |= (m_uint64_t)addr->eth_addr_byte[3] << 27;
   eth_val |= (m_uint64_t)addr->eth_addr_byte[4] << 35;
   eth_val |= (m_uint64_t)addr->eth_addr_byte[5] << 43;

   /* Compute hash value for Ethernet address filtering */
   hash_val = gt_eth_hash_value(addr,port->pcr & GT_PCR_HM);
   
   if (port->pcr & GT_PCR_HS) {
      /* 1/2K address filtering */
      hte_addr = port->ht_addr + ((hash_val & 0x7ff) << 3);
   } else {
      /* 8K address filtering */
      hte_addr = port->ht_addr + (hash_val << 3);
   }

#if DEBUG_ETH_HASH
   GT_LOG(d,"Hash Lookup for Ethernet address "
          "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x: addr=0x%x\n",
          addr->eth_addr_byte[0], addr->eth_addr_byte[1],
          addr->eth_addr_byte[2], addr->eth_addr_byte[3],
          addr->eth_addr_byte[4], addr->eth_addr_byte[5],
          hte_addr);
#endif

   for(i=0;i<GT_HTE_HOPNUM;i++,hte_addr+=8) {
      *entry  = ((m_uint64_t)physmem_copy_u32_from_vm(d->vm,hte_addr)) << 32;
      *entry |= physmem_copy_u32_from_vm(d->vm,hte_addr+4);

      /* Empty entry ? */
      if (!(*entry & GT_HTE_VALID))
         return(GT_HTLOOKUP_MISS);

      /* Skip flag or different Ethernet address: jump to next entry */
      if ((*entry & GT_HTE_SKIP) || ((*entry & GT_HTE_ADDR_MASK) != eth_val))
         continue;

      /* We have the good MAC address in this entry */
      return(GT_HTLOOKUP_MATCH);
   }

   return(GT_HTLOOKUP_HOP_EXCEEDED);
}

/* 
 * Check if a packet (given its destination address) must be handled 
 * at RX path.
 *
 * Return values:
 *   - 0: Discard packet ;
 *   - 1: Receive packet ;
 *   - 2: Receive packet and set "M" bit in RX descriptor.
 *
 * The documentation is not clear about the M bit in RX descriptor.
 * It is described as "Miss" or "Match" depending on the section.
 */
static inline int gt_eth_handle_rx_daddr(struct gt_data *d,
                                         struct eth_port *port,
                                         u_int hash_res,
                                         m_uint64_t hash_entry)
{
   /* Hop Number exceeded */
   if (hash_res == GT_HTLOOKUP_HOP_EXCEEDED)
      return(1);

   /* Match and hash entry marked as "Receive" */
   if ((hash_res == GT_HTLOOKUP_MATCH) && (hash_entry & GT_HTE_RD))
      return(2);

   /* Miss but hash table default mode to forward ? */
   if ((hash_res == GT_HTLOOKUP_MISS) && (port->pcr & GT_PCR_HDM))
      return(2);

   /* Promiscous Mode */
   if (port->pcr & GT_PCR_PM)
      return(1);

   /* Drop packet for other cases */
   return(0);
}

/* Put a packet in buffer of a descriptor */
static void gt_eth_rxdesc_put_pkt(struct gt_data *d,struct eth_desc *rxd,
                                  u_char **pkt,ssize_t *pkt_len)
{
   ssize_t len,cp_len;

   len = (rxd->buf_size & GT_RXDESC_BS_MASK) >> GT_RXDESC_BS_SHIFT;
   
   /* compute the data length to copy */
   cp_len = m_min(len,*pkt_len);
   
   /* copy packet data to the VM physical RAM */
   physmem_copy_to_vm(d->vm,*pkt,rxd->buf_ptr,cp_len);
      
   /* set the byte count in descriptor */
   rxd->buf_size |= cp_len;

   *pkt += cp_len;
   *pkt_len -= cp_len;
}

/* Put a packet in the specified RX queue */
static int gt_eth_handle_rxqueue(struct gt_data *d,u_int port_id,u_int queue,
                                 u_char *pkt,ssize_t pkt_len)
{
   struct eth_port *port = &d->eth_ports[port_id];
   m_uint32_t rx_start,rx_current;
   struct eth_desc rxd0,rxdn,*rxdc;
   ssize_t tot_len = pkt_len;
   u_char *pkt_ptr = pkt;
   n_eth_dot1q_hdr_t *hdr;
   m_uint64_t hash_entry;
   int i,hash_res,addr_action;

   /* Truncate the packet if it is too big */
   pkt_len = m_min(pkt_len,GT_MAX_PKT_SIZE);

   /* Copy the first RX descriptor */
   if (!(rx_start = rx_current = port->rx_start[queue]))
      goto dma_error;

   /* Analyze the Ethernet header */
   hdr = (n_eth_dot1q_hdr_t *)pkt;

   /* Hash table lookup for address filtering */
   hash_res = gt_eth_hash_lookup(d,port,&hdr->daddr,&hash_entry);

#if DEBUG_ETH_HASH
   GT_LOG(d,"Hash result: %d, hash_entry=0x%llx\n",hash_res,hash_entry);
#endif

   if (!(addr_action = gt_eth_handle_rx_daddr(d,port,hash_res,hash_entry)))
      return(FALSE);

   /* Load the first RX descriptor */
   gt_eth_desc_read(d,rx_start,&rxd0);

#if DEBUG_ETH_RX
   GT_LOG(d,"port %u/queue %u: reading desc at 0x%8.8x "
          "[buf_size=0x%8.8x,cmd_stat=0x%8.8x,"
          "next_ptr=0x%8.8x,buf_ptr=0x%8.8x]\n",
          port_id,queue,rx_start,
          rxd0.buf_size,rxd0.cmd_stat,rxd0.next_ptr,rxd0.buf_ptr);
#endif

   for(i=0,rxdc=&rxd0;tot_len>0;i++)
   {
      /* We must own the descriptor */
      if (!(rxdc->cmd_stat & GT_RXDESC_OWN))
         goto dma_error;

      /* Put data into the descriptor buffer */
      gt_eth_rxdesc_put_pkt(d,rxdc,&pkt_ptr,&tot_len);

      /* Clear the OWN bit */
      rxdc->cmd_stat &= ~GT_RXDESC_OWN;

      /* We have finished if the complete packet has been stored */
      if (tot_len == 0) {
         rxdc->cmd_stat |= GT_RXDESC_L;
         rxdc->buf_size += 4;  /* Add 4 bytes for CRC */
      }

      /* Update the descriptor in host memory (but not the 1st) */
      if (i != 0)
         gt_eth_desc_write(d,rx_current,rxdc);

      /* Get address of the next descriptor */
      rx_current = rxdc->next_ptr;

      if (tot_len == 0)
         break;

      if (!rx_current)
         goto dma_error;

      /* Read the next descriptor from VM physical RAM */
      gt_eth_desc_read(d,rx_current,&rxdn);
      rxdc = &rxdn;
   }

   /* Update the RX pointers */
   port->rx_start[queue] = port->rx_current[queue] = rx_current;

   /* Update the first RX descriptor */
   rxd0.cmd_stat |= GT_RXDESC_F;

   if (hash_res == GT_HTLOOKUP_HOP_EXCEEDED)
      rxd0.cmd_stat |= GT_RXDESC_HE;

   if (addr_action == 2)
      rxd0.cmd_stat |= GT_RXDESC_M;

   if (ntohs(hdr->type) <= N_ETH_MTU)   /* 802.3 frame */
      rxd0.cmd_stat |= GT_RXDESC_FT;

   gt_eth_desc_write(d,rx_start,&rxd0);

   /* Update MIB counters */
   port->rx_bytes += pkt_len;
   port->rx_frames++;

   /* Indicate that we have a frame ready */
   port->icr |= (GT_ICR_RXBUFQ0 << queue) | GT_ICR_RXBUF;
   gt_eth_update_int_status(d,port); 
   return(TRUE);

 dma_error:
   port->icr |= (GT_ICR_RXERRQ0 << queue) | GT_ICR_RXERR;
   gt_eth_update_int_status(d,port); 
   return(FALSE);
}

/* Handle RX packet for an Ethernet port */
static int gt_eth_handle_rx_pkt(netio_desc_t *nio,
                                u_char *pkt,ssize_t pkt_len,
                                struct gt_data *d,void *arg)
{
   u_int queue,port_id = (int)arg;
   struct eth_port *port;

   port = &d->eth_ports[port_id];

  /* Check if RX DMA is active */
   if (!(port->sdcmr & GT_SDCMR_ERD))
      return(FALSE);

   queue = 0;  /* At this time, only put packet in queue 0 */
   gt_eth_handle_rxqueue(d,port_id,queue,pkt,pkt_len);
   return(TRUE);
}

/* Shutdown a GT system controller */
void dev_gt_shutdown(vm_instance_t *vm,struct gt_data *d)
{
   if (d != NULL) {
      /* Stop the TX ring scanner */
      ptask_remove(d->eth_tx_tid);

      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Remove the PCI device */
      pci_dev_remove(d->pci_dev);

      /* Free the structure itself */
      free(d);
   }
}

/* Create a new GT64010 controller */
int dev_gt64010_init(vm_instance_t *vm,char *name,
                     m_uint64_t paddr,m_uint32_t len,u_int irq)
{
   struct gt_data *d;

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"gt64010: unable to create device data.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->vm = vm;   
   d->bus[0] = vm->pci_bus[0];

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_gt_shutdown;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_gt64010_access;

   /* Add the controller as a PCI device */
   if (!pci_dev_lookup(d->bus[0],0,0,0)) {
      d->pci_dev = pci_dev_add(d->bus[0],name,
                               PCI_VENDOR_GALILEO,PCI_PRODUCT_GALILEO_GT64010,
                               0,0,irq,d,NULL,NULL,NULL);

      if (!d->pci_dev) {
         fprintf(stderr,"gt64010: unable to create PCI device.\n");
         return(-1);
      }
   }

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}

/*
 * pci_gt64120_read()
 *
 * Read a PCI register.
 */
static m_uint32_t pci_gt64120_read(cpu_gen_t *cpu,struct pci_device *dev,
                                   int reg)
{   
   switch (reg) {
      case 0x08:
         return(0x03008005);
      default:
         return(0);
   }
}

/* Create a new GT64120 controller */
int dev_gt64120_init(vm_instance_t *vm,char *name,
                     m_uint64_t paddr,m_uint32_t len,u_int irq)
{
   struct gt_data *d;

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"gt64120: unable to create device data.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->vm = vm;
   d->bus[0] = vm->pci_bus[0];
   d->bus[1] = vm->pci_bus[1];

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_gt_shutdown;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_gt64120_access;

   /* Add the controller as a PCI device */
   if (!pci_dev_lookup(d->bus[0],0,0,0)) {
      d->pci_dev = pci_dev_add(d->bus[0],name,
                               PCI_VENDOR_GALILEO,PCI_PRODUCT_GALILEO_GT64120,
                               0,0,irq,d,NULL,pci_gt64120_read,NULL);
      if (!d->pci_dev) {
         fprintf(stderr,"gt64120: unable to create PCI device.\n");
         return(-1);
      }
   }

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}

/*
 * pci_gt96100_read()
 *
 * Read a PCI register.
 */
static m_uint32_t pci_gt96100_read(cpu_gen_t *cpu,struct pci_device *dev,
                                   int reg)
{   
   switch (reg) {
      case 0x08:
         return(0x03008005);
      default:
         return(0);
   }
}

/* Create a new GT96100 controller */
int dev_gt96100_init(vm_instance_t *vm,char *name,
                     m_uint64_t paddr,m_uint32_t len,
                     u_int dma_irq,u_int eth_irq)
{
   struct gt_data *d;

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"gt96100: unable to create device data.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->name = name;
   d->vm = vm;
   d->eth_irq = eth_irq;
   d->bus[0] = vm->pci_bus[0];
   d->bus[1] = vm->pci_bus[1];

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_gt_shutdown;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_gt96100_access;

   /* Add the controller as a PCI device */
   if (!pci_dev_lookup(d->bus[0],0,0,0)) {
      d->pci_dev = pci_dev_add(d->bus[0],name,
                               PCI_VENDOR_GALILEO,PCI_PRODUCT_GALILEO_GT96100,
                               0,0,dma_irq,d,NULL,pci_gt96100_read,NULL);
      if (!d->pci_dev) {
         fprintf(stderr,"gt96100: unable to create PCI device.\n");
         return(-1);
      }
   }

   /* Start the TX ring scanner */
   d->eth_tx_tid = ptask_add((ptask_callback)gt_eth_handle_txqueues,d,NULL);

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}

/* Bind a NIO to GT96100 device */
int dev_gt96100_set_nio(struct gt_data *d,u_int port_id,netio_desc_t *nio)
{
   struct eth_port *port;

   if (port_id >= GT_ETH_PORTS)
      return(-1);

   port = &d->eth_ports[port_id];

   /* check that a NIO is not already bound */
   if (port->nio != NULL)
      return(-1);

   port->nio = nio;
   netio_rxl_add(nio,(netio_rx_handler_t)gt_eth_handle_rx_pkt,
                 d,(void *)port_id);
   return(0);
}

/* Unbind a NIO from a GT96100 device */
int dev_gt96100_unset_nio(struct gt_data *d,u_int port_id)
{
   struct eth_port *port;

   if (port_id >= GT_ETH_PORTS)
      return(-1);

   port = &d->eth_ports[port_id];

   if (port->nio != NULL) {
      netio_rxl_remove(port->nio);
      port->nio = NULL;
   }

   return(0);
}
