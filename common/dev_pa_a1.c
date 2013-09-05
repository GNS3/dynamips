/*  
 * Cisco router simulation platform.
 * Copyright (C) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * PA-A1 ATM interface based on TI1570 and PLX 9060-ES.
 *
 * EEPROM types:
 *   - 0x17: PA-A1-OC3MM
 *   - 0x2C: PA-A1-OC3SM
 *   - 0x2D: PA-A1-OC3UTP
 *
 * IOS command: "sh controller atm2/0"
 * 
 * Manuals:
 *
 * Texas Instruments TNETA1570 ATM segmentation and reassembly device
 * with integrated 64-bit PCI-host interface
 * http://focus.ti.com/docs/prod/folders/print/tneta1570.html
 *
 * PLX 9060-ES
 * http://www.plxtech.com/products/io_accelerators/PCI9060/default.htm
 *
 * TODO: 
 *   - RX error handling and RX AAL5-related stuff
 *   - HEC and AAL5 CRC fields.
 *
 * Cell trains for faster NETIO communications ?
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "crc.h"
#include "atm.h"
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "ptask.h"
#include "dev_c7200.h"

/* Debugging flags */
#define DEBUG_ACCESS     0
#define DEBUG_UNKNOWN    0
#define DEBUG_TRANSMIT   0
#define DEBUG_RECEIVE    0
#define DEBUG_TX_DMA     0

/* PCI vendor/product codes */
#define TI1570_PCI_VENDOR_ID       0x104c
#define TI1570_PCI_PRODUCT_ID      0xa001

#define PLX_9060ES_PCI_VENDOR_ID   0x10b5
#define PLX_9060ES_PCI_PRODUCT_ID  0x906e

/* Number of buffers transmitted at each TX DMA ring scan pass */
#define TI1570_TXDMA_PASS_COUNT  16

/* TI1570 Internal Registers (p.58 of doc) */
#define TI1570_REG_CONFIG          0x0000  /* Configuration registers */
#define TI1570_REG_STATUS          0x0001  /* Status register */
#define TI1570_REG_IMASK           0x0002  /* Interrupt-mask register */
#define TI1570_REG_RGT_RAT         0x0003  /* RGT + RAT cycle-counter */
#define TI1570_REG_RX_UNKNOWN      0x0004  /* RX Unknown Register */
#define TI1570_REG_TX_CRING_SIZE   0x0005  /* TX Completion ring sizes */
#define TI1570_REG_RX_CRING_SIZE   0x0006  /* RX Completion ring sizes */
#define TI1570_REG_TX_PSR_SIZE     0x0007  /* TX Pkt-seg ring size + FIFO */
#define TI1570_REG_HEC_AAL5_DISC   0x0008  /* HEC err + AAL5 CPCS discard */
#define TI1570_REG_UNK_PROTO_CNT   0x0009  /* Unknown-protocols counter */
#define TI1570_REG_RX_ATM_COUNT    0x000A  /* ATM-cells-received counter */
#define TI1570_REG_TX_ATM_COUNT    0x000B  /* ATM-cells-tranmitted counter */
#define TI1570_REG_TX_RX_FIFO      0x000C  /* TX/RX FIFO occupancy, VCI mask */
#define TI1570_REG_SCHED_SIZE      0x000D  /* Scheduler Table size */
#define TI1570_REG_SOFT_RESET      0x000E  /* Software Reset */
#define TI1570_REG_TCR_WOI_ADDR    0x0080  /* TX Compl. Ring w/o IRQ addr. */
#define TI1570_REG_TCR_WI_ADDR     0x0081  /* TX Compl. Ring w/ IRQ addr. */
#define TI1570_REG_RCR_WOI_ADDR    0x0082  /* RX Compl. Ring w/o IRQ addr. */
#define TI1570_REG_RCR_WI_ADDR     0x0083  /* RX Compl. Ring w/ IRQ addr. */

/* TI1570 configuration register (p.59) */
#define TI1570_CFG_EN_RAT          0x00000001  /* Reassembly Aging */
#define TI1570_CFG_BP_SEL          0x00000002  /* IRQ on packet or buffer */
#define TI1570_CFG_EN_RX           0x00000010  /* RX enable */
#define TI1570_CFG_EN_TX           0x00000020  /* TX enable */
#define TI1570_CFG_SMALL_MAP       0x00000040  /* Small map */

/* TI1570 status register (p.61) */
#define TI1570_STAT_CP_TX          0x00000001  /* Transmit completion ring */
#define TI1570_STAT_RX_IRR         0x00000040  /* Receive unknown reg set */
#define TI1570_STAT_CP_RX          0x00000080  /* Receive completion ring */
#define TI1570_STAT_TX_FRZ         0x00000100  /* TX Freeze */
#define TI1570_STAT_RX_FRZ         0x00000200  /* RX Freeze */

/* Mask for RX/TX completion-ring sizes */
#define TI1570_TCR_SIZE_MASK       0x00001FFF  /* TX compl. ring size mask */
#define TI1570_RCR_SIZE_MASK       0x000003FF  /* RX compl. ring size mask */

/* TI1750 TX packet segmentation ring register */
#define TI1570_PSR_SIZE_MASK       0x000000FF  /* pkt-seg ring size */

/* Total size of the TI1570 Control Memory */
#define TI1570_CTRL_MEM_SIZE       0x100000

/* Offsets of the TI1570 structures (p.66) */
#define TI1570_TX_SCHED_OFFSET          0x0000  /* TX scheduler table */
#define TI1570_INTERNAL_REGS_OFFSET     0x3200  /* Internal Registers */
#define TI1570_FREE_BUFFERS_OFFSET      0x3800  /* Free-Buffer Pointers */
#define TI1570_RX_DMA_PTR_TABLE_OFFSET  0x4000  /* RX VPI/VCI pointer table */
#define TI1570_TX_DMA_TABLE_OFFSET      0x8000  /* TX DMA state table */
#define TI1570_RX_DMA_TABLE_OFFSET      0x10000 /* RX DMA state table */

/* TX scheduler table */
#define TI1570_TX_SCHED_ENTRY_COUNT  6200
#define TI1570_TX_SCHED_ENTRY_MASK   0x3FF   /* Entry mask */
#define TI1570_TX_SCHED_E0_SHIFT     0       /* Shift for entry 0 */
#define TI1570_TX_SCHED_E1_SHIFT     16      /* Shift for entry 0 */

/* TX DMA state table */
#define TI1570_TX_DMA_ACT            0x80000000  /* ACTive (word 0) */
#define TI1570_TX_DMA_SOP            0x40000000  /* Start of Packet (SOP) */
#define TI1570_TX_DMA_EOP            0x20000000  /* End of Packet (EOP) */
#define TI1570_TX_DMA_ABORT          0x10000000  /* Abort */
#define TI1570_TX_DMA_TCR_SELECT     0x02000000  /* TX comp. ring selection */
#define TI1570_TX_DMA_AAL_TYPE_MASK  0x0C000000  /* AAL-type mask */

#define TI1570_TX_DMA_AAL_TRWPTI     0x00000000  /* Transp. AAL w/ PTI set */
#define TI1570_TX_DMA_AAL_AAL5       0x04000000  /* AAL5 */
#define TI1570_TX_DMA_AAL_TRWOPTI    0x08000000  /* Transp. AAL w/o PTI set */

#define TI1570_TX_DMA_OFFSET_MASK    0x00FF0000
#define TI1570_TX_DMA_OFFSET_SHIFT   16
#define TI1570_TX_DMA_DCOUNT_MASK    0x0000FFFF

#define TI1570_TX_DMA_ON                 0x80000000   /* DMA state (word 3) */
#define TI1570_TX_DMA_RING_OFFSET_MASK   0x3FFFFF00
#define TI1570_TX_DMA_RING_OFFSET_SHIFT  8
#define TI1570_TX_DMA_RING_INDEX_MASK    0x000000FF

#define TI1570_TX_DMA_RING_AAL5_LEN_MASK 0x0000FFFF

typedef struct ti1570_tx_dma_entry ti1570_tx_dma_entry_t;
struct ti1570_tx_dma_entry {
   m_uint32_t ctrl_buf;      /* Ctrl, Buffer Offset, Buffer data-byte count */
   m_uint32_t cb_addr;       /* Current Buffer Address */
   m_uint32_t atm_hdr;       /* 4-byte ATM header */
   m_uint32_t dma_state;     /* DMA state + Packet segmentation ring address */
   m_uint32_t nb_addr;       /* Next Buffer address */
   m_uint32_t sb_addr;       /* Start of Buffer address */
   m_uint32_t aal5_crc;      /* Partial AAL5-transmit CRC */
   m_uint32_t aal5_ctrl;     /* AAL5-control field and length field */
};

/* TX Packet-Segmentation Rings */
#define TI1570_TX_RING_OWN       0x80000000   /* If set, packet is ready */
#define TI1570_TX_RING_PTR_MASK  0x3FFFFFFF   /* Buffer pointer */

/* TX Data Buffers */
#define TI1570_TX_BUFFER_RDY     0x80000000   /* If set, buffer is ready */
#define TI1570_TX_BUFFER_SOP     0x40000000   /* First buffer of packet */
#define TI1570_TX_BUFFER_EOP     0x20000000   /* Last buffer of packet */
#define TI1570_TX_BUFFER_ABORT   0x10000000   /* Abort */

#define TI1570_TX_BUFFER_OFFSET_MASK   0x00FF0000
#define TI1570_TX_BUFFER_OFFSET_SHIFT  16
#define TI1570_TX_BUFFER_DCOUNT_MASK   0x0000FFFF

typedef struct ti1570_tx_buffer ti1570_tx_buffer_t;
struct ti1570_tx_buffer {
   m_uint32_t ctrl_buf;      /* Ctrl, Buffer offset, Buffer data-byte count */
   m_uint32_t nb_addr;       /* Start-of-next buffer pointer */
   m_uint32_t atm_hdr;       /* 4-byte ATM header */
   m_uint32_t aal5_ctrl;     /* PCS-UU/CPI field (AAL5 control field) */
};

/* TX completion-ring */
#define TI1570_TCR_OWN    0x80000000   /* OWNner bit */
#define TI1570_TCR_ABORT  0x40000000   /* Abort */

/* RX VPI/VCI DMA pointer table */
#define TI1570_RX_VPI_ENABLE         0x80000000  /* VPI enabled ? */
#define TI1570_RX_BASE_PTR_MASK      0x7FFF0000  /* Base pointer mask */
#define TI1570_RX_BASE_PTR_SHIFT     16          /* Base pointer shift */
#define TI1570_RX_VCI_RANGE_MASK     0x0000FFFF  /* Valid VCI range */

/* RX DMA state table (p.36) */
#define TI1570_RX_DMA_ACT            0x80000000  /* ACTive (word 0) */
#define TI1570_RX_DMA_RCR_SELECT     0x20000000  /* RX comp. ring selection */
#define TI1570_RX_DMA_WAIT_EOP       0x10000000  /* Wait for EOP */
#define TI1570_RX_DMA_AAL_TYPE_MASK  0x0C000000  /* AAL-type mask */

#define TI1570_RX_DMA_AAL_PTI        0x00000000  /* PTI based tr. AAL pkt */
#define TI1570_RX_DMA_AAL_AAL5       0x04000000  /* AAL5 */
#define TI1570_RX_DMA_AAL_CNT        0x08000000  /* Cnt based tr. AAL pkt */

#define TI1570_RX_DMA_FIFO           0x02000000  /* FIFO used for free bufs */

#define TI1570_RX_DMA_TR_CNT_MASK    0xFFFF0000  /* Cnt-based Tr-AAL */
#define TI1570_RX_DMA_TR_CNT_SHIFT   16
#define TI1570_RX_DMA_CB_LEN_MASK    0x0000FFFF  /* Current buffer length */

#define TI1570_RX_DMA_ON             0x80000000  /* DMA state (word 6) */
#define TI1570_RX_DMA_FILTER         0x40000000  /* Filter */

#define TI1570_RX_DMA_FB_PTR_MASK    0x3FFFFFFF  /* Free-buffer ptr mask */
#define TI1570_RX_DMA_FB_INDEX_MASK  0x000000FF  /* Index with Free-buf ring */

typedef struct ti1570_rx_dma_entry ti1570_rx_dma_entry_t;
struct ti1570_rx_dma_entry {
   m_uint32_t ctrl;          /* Control field, EFCN cell cnt, pkt length */
   m_uint32_t cb_addr;       /* Current Buffer Address */
   m_uint32_t sb_addr;       /* Start of Buffer address */
   m_uint32_t cb_len;        /* Transp-AAL pkt counter, current buf length */
   m_uint32_t sp_ptr;        /* Start-of-packet pointer */
   m_uint32_t aal5_crc;      /* Partial AAL5-receive CRC */
   m_uint32_t fbr_entry;     /* Free-buffer ring-pointer table entry */
   m_uint32_t timeout;       /* Timeout value, current timeout count */
};

/* RX free-buffer ring pointer table entry (p.39) */
#define TI1570_RX_FBR_PTR_MASK       0xFFFFFFFC
#define TI1570_RX_FBR_BS_MASK        0xFFFF0000  /* Buffer size mask */
#define TI1570_RX_FBR_BS_SHIFT       16
#define TI1570_RX_FBR_RS_MASK        0x0000FC00  /* Ring size mask */
#define TI1570_RX_FBR_RS_SHIFT       10
#define TI1570_RX_FBR_IDX_MASK       0x000003FF  /* Current index mask */

typedef struct ti1570_rx_fbr_entry ti1570_rx_fbr_entry_t;
struct ti1570_rx_fbr_entry {
   m_uint32_t fbr_ptr;       /* RX free-buffer ring pointer */
   m_uint32_t ring_size;     /* Ring size and buffer size */
};

/* RX buffer pointer (p.41) */
#define TI1570_RX_BUFPTR_OWN    0x80000000   /* If set, buffer is ready */
#define TI1570_RX_BUFPTR_MASK   0x3FFFFFFF   /* Buffer address mask */

/* RX data buffer (p.42) */
#define TI1570_RX_BUFFER_SOP    0x80000000   /* Start-of-Packet buffer */
#define TI1570_RX_BUFFER_EOP    0x40000000   /* End-of-Packet buffer */

typedef struct ti1570_rx_buffer ti1570_rx_buffer_t;
struct ti1570_rx_buffer {
   m_uint32_t reserved;      /* Reserved, not used by the TI1570 */
   m_uint32_t ctrl;          /* Control field, Start of next buffer pointer */
   m_uint32_t atm_hdr;       /* ATM header */
   m_uint32_t user;          /* User-defined value */
};

/* Internal structure to hold free buffer info */
typedef struct ti1570_rx_buf_holder ti1570_rx_buf_holder_t;
struct ti1570_rx_buf_holder {
   m_uint32_t buf_addr;
   m_uint32_t buf_size;
   ti1570_rx_buffer_t rx_buf;
};

/* RX completion ring entry */
#define TI1570_RCR_PKT_OVFLW    0x80000000   /* Packet overflow (word 0) */
#define TI1570_RCR_CRC_ERROR    0x40000000   /* CRC error */
#define TI1570_RCR_BUF_STARV    0x20000000   /* Buffer starvation */
#define TI1570_RCR_TIMEOUT      0x10000000   /* Reassembly timeout */
#define TI1570_RCR_ABORT        0x08000000   /* Abort condition */
#define TI1570_RCR_AAL5         0x04000000   /* AAL5 indicator */

#define TI1570_RCR_VALID        0x80000000   /* Start-ptr valid (word 2) */

#define TI1570_RCR_OWN          0x80000000   /* Buffer ready (word 4) */
#define TI1570_RCR_ERROR        0x40000000   /* Error entry */

typedef struct ti1570_rcr_entry ti1570_rcr_entry_t;
struct ti1570_rcr_entry {
   m_uint32_t atm_hdr;       /* ATM header */
   m_uint32_t error;         /* Error Indicator + Congestion cell count */
   m_uint32_t sp_addr;       /* Start of packet */
   m_uint32_t aal5_trailer;  /* AAL5 trailer */
   m_uint32_t fbr_entry;     /* Free-buffer ring-pointer table entry */
   m_uint32_t res[3];        /* Reserved, not used by the TI1570 */
};

/* TI1570 Data */
struct pa_a1_data {
   char *name;

   /* IRQ clearing counter */
   u_int irq_clear_count;

   /* Control Memory pointer */
   m_uint32_t *ctrl_mem_ptr;

   /* TI1570 internal registers */
   m_uint32_t *iregs;
   
   /* TX FIFO cell */
   m_uint8_t txfifo_cell[ATM_CELL_SIZE];
   m_uint32_t txfifo_avail,txfifo_pos;

   /* TX Scheduler table */
   m_uint32_t *tx_sched_table;

   /* TX DMA state table */
   ti1570_tx_dma_entry_t *tx_dma_table;

   /* TX/RX completion ring current position */
   m_uint32_t tcr_wi_pos,tcr_woi_pos;
   m_uint32_t rcr_wi_pos,rcr_woi_pos;

   /* RX VPI/VCI DMA pointer table */
   m_uint32_t *rx_vpi_vci_dma_table;

   /* RX DMA state table */
   ti1570_rx_dma_entry_t *rx_dma_table;

   /* RX Free-buffer ring pointer table */
   ti1570_rx_fbr_entry_t *rx_fbr_table;

   /* Virtual device */
   struct vdevice *dev;

   /* PCI device information */
   struct pci_device *pci_dev_ti,*pci_dev_plx;

   /* Virtual machine */
   vm_instance_t *vm;

   /* NetIO descriptor */
   netio_desc_t *nio;

   /* TX ring scanner task id */
   ptask_id_t tx_tid;
};

/* Log a TI1570 message */
#define TI1570_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* Reset the TI1570 (forward declaration) */
static void ti1570_reset(struct pa_a1_data *d,int clear_ctrl_mem);

/* Update the interrupt status */
static inline void dev_pa_a1_update_irq_status(struct pa_a1_data *d)
{
   if (d->iregs[TI1570_REG_STATUS] & d->iregs[TI1570_REG_IMASK]) {
      pci_dev_trigger_irq(d->vm,d->pci_dev_ti);
   } else {     
      pci_dev_clear_irq(d->vm,d->pci_dev_ti);
   }
}

/*
 * dev_pa_a1_access()
 */
void *dev_pa_a1_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                       u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct pa_a1_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"TI1570","read  access to offset = 0x%x, pc = 0x%llx\n",
              offset,cpu_get_pc(cpu));
   } else {
      cpu_log(cpu,"TI1570","write access to vaddr = 0x%x, pc = 0x%llx, "
              "val = 0x%llx\n",offset,cpu_get_pc(cpu),*data);
   }
#endif   

   /* Specific cases */
   switch(offset) {
      /* Status register */
      case 0x3204:
         if (op_type == MTS_READ) {
            *data = d->iregs[TI1570_REG_STATUS];

            if (++d->irq_clear_count == 5) {
               d->iregs[TI1570_REG_STATUS] &= ~0x3FF;
               d->irq_clear_count = 0;
            }

            dev_pa_a1_update_irq_status(d);
         }
         break;

      /* Software Reset register */
      case 0x3238:
         TI1570_LOG(d,"reset issued.\n");
         ti1570_reset(d,FALSE);
         break;

      case 0x18000c:
         if (op_type == MTS_READ) {
            *data = 0xa6;
            return NULL;
         }
         break;
   }

   /* Control Memory access */
   if (offset < TI1570_CTRL_MEM_SIZE) {
      if (op_type == MTS_READ)
         *data = d->ctrl_mem_ptr[offset >> 2];
      else
         d->ctrl_mem_ptr[offset >> 2] = *data;
      return NULL;
   }

   /* Unknown offset */
#if DEBUG_UNKNOWN
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->name,"read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),op_size);
   } else {
      cpu_log(cpu,d->name,"write to unknown addr 0x%x, value=0x%llx, "
              "pc=0x%llx (size=%u)\n",offset,*data,cpu_get_pc(cpu),op_size);
   }
#endif
   return NULL;
}

/* Fetch a TX data buffer from host memory */
static void ti1570_read_tx_buffer(struct pa_a1_data *d,m_uint32_t addr,
                                  ti1570_tx_buffer_t *tx_buf)
{
   physmem_copy_from_vm(d->vm,tx_buf,addr,sizeof(ti1570_tx_buffer_t));

   /* byte-swapping */
   tx_buf->ctrl_buf  = vmtoh32(tx_buf->ctrl_buf);
   tx_buf->nb_addr   = vmtoh32(tx_buf->nb_addr);
   tx_buf->atm_hdr   = vmtoh32(tx_buf->atm_hdr);
   tx_buf->aal5_ctrl = vmtoh32(tx_buf->aal5_ctrl);
}

/* Acquire a TX buffer */
static int ti1570_acquire_tx_buffer(struct pa_a1_data *d,   
                                    ti1570_tx_dma_entry_t *tde,
                                    m_uint32_t buf_addr)
{
   ti1570_tx_buffer_t tx_buf;
   m_uint32_t buf_offset;

#if DEBUG_TRANSMIT
   TI1570_LOG(d,"ti1570_acquire_tx_buffer: acquiring buffer at address 0x%x\n",
              buf_addr);
#endif

   /* Read the TX buffer from host memory */
   ti1570_read_tx_buffer(d,buf_addr,&tx_buf);

   /* The buffer must be ready to be acquired */
   if (!(tx_buf.ctrl_buf & TI1570_TX_BUFFER_RDY))
      return(FALSE);

   /* Put the TX buffer data into the TX DMA state entry */
   tde->ctrl_buf  = tx_buf.ctrl_buf;
   tde->nb_addr   = tx_buf.nb_addr << 2;

   /* Read the ATM header only from the first buffer */
   if (tx_buf.ctrl_buf & TI1570_TX_BUFFER_SOP) {
      tde->atm_hdr   = tx_buf.atm_hdr;
      tde->aal5_ctrl = tx_buf.aal5_ctrl;
      tde->aal5_crc  = 0; /* will be inverted at first CRC update */
   }

   /* Compute the current-buffer-data address */
   buf_offset = tx_buf.ctrl_buf & TI1570_TX_BUFFER_OFFSET_MASK;
   buf_offset >>= TI1570_TX_BUFFER_OFFSET_SHIFT;
   tde->cb_addr = buf_addr + sizeof(tx_buf) + buf_offset;

   /* Remember the start address of the buffer */
   tde->sb_addr = buf_addr;
   return(TRUE);
}

/* Returns TRUE if the TX DMA entry is for an AAL5 packet */
static inline int ti1570_is_tde_aal5(ti1570_tx_dma_entry_t *tde)
{
   m_uint32_t pkt_type;

   pkt_type = tde->ctrl_buf & TI1570_TX_DMA_AAL_TYPE_MASK;
   return(pkt_type == TI1570_TX_DMA_AAL_AAL5);
}

/* Update the AAL5 partial CRC */
static void ti1570_update_aal5_crc(struct pa_a1_data *d,
                                   ti1570_tx_dma_entry_t *tde)
{
   tde->aal5_crc = crc32_compute(~tde->aal5_crc,
                                 &d->txfifo_cell[ATM_HDR_SIZE],
                                 ATM_PAYLOAD_SIZE);
}

/* 
 * Update the TX DMA entry buffer offset and count when "data_len" bytes
 * have been transmitted.
 */
static void ti1570_update_tx_dma_bufinfo(ti1570_tx_dma_entry_t *tde,
                                         m_uint32_t buf_size,
                                         m_uint32_t data_len)
{
   m_uint32_t tmp,tot_len;

   /* update the current buffer address */
   tde->cb_addr += data_len;
   
   /* set the remaining byte count */
   tmp = tde->ctrl_buf & ~TI1570_TX_BUFFER_DCOUNT_MASK;
   tde->ctrl_buf = tmp + (buf_size - data_len);

   /* update the AAL5 count */
   if (ti1570_is_tde_aal5(tde)) {
      tot_len = tde->aal5_ctrl & TI1570_TX_DMA_RING_AAL5_LEN_MASK;
      tot_len += data_len;

      tmp = (tde->aal5_ctrl & ~TI1570_TX_DMA_RING_AAL5_LEN_MASK) + tot_len;
      tde->aal5_ctrl = tmp;
   }
}

/* Clear the TX fifo */
static void ti1570_clear_tx_fifo(struct pa_a1_data *d)
{
   d->txfifo_avail = ATM_PAYLOAD_SIZE;
   d->txfifo_pos   = ATM_HDR_SIZE;
   memset(d->txfifo_cell,0,ATM_CELL_SIZE);
}

/* 
 * Transmit the TX FIFO cell through the NETIO infrastructure if 
 * it is full.
 */
static void ti1570_send_tx_fifo(struct pa_a1_data *d,
                                ti1570_tx_dma_entry_t *tde,
                                int update_aal5_crc)
{
   if (d->txfifo_avail == 0) {
#if DEBUG_TRANSMIT
      TI1570_LOG(d,"ti1570_transmit_cell: transmitting to NETIO device\n");
      mem_dump(log_file,d->txfifo_cell,ATM_CELL_SIZE);
#endif
      if (update_aal5_crc)
         ti1570_update_aal5_crc(d,tde);

      netio_send(d->nio,d->txfifo_cell,ATM_CELL_SIZE);
      ti1570_clear_tx_fifo(d);
   }
}

/* Add padding to the FIFO */
static void ti1570_add_tx_padding(struct pa_a1_data *d,m_uint32_t len)
{
   if (len > d->txfifo_avail) {
      TI1570_LOG(d,"ti1570_add_tx_padding: trying to add too large "
                 "padding (avail: 0x%x, pad: 0x%x)\n",d->txfifo_avail,len);
      len = d->txfifo_avail;
   }

   memset(&d->txfifo_cell[d->txfifo_pos],0,len);
   d->txfifo_pos += len;
   d->txfifo_avail -= len;
}

/* Initialize an ATM cell for tranmitting */
static m_uint32_t ti1570_init_tx_atm_cell(struct pa_a1_data *d,
                                          ti1570_tx_dma_entry_t *tde,
                                          int set_pti)
{
   m_uint32_t buf_size,len,atm_hdr;

   buf_size = tde->ctrl_buf & TI1570_TX_DMA_DCOUNT_MASK;
   len = m_min(buf_size,d->txfifo_avail);

#if DEBUG_TRANSMIT
   TI1570_LOG(d,"ti1570_init_tx_atm_cell: data ptr=0x%x, "
              "buf_size=%u (0x%x), len=%u (0x%x), atm_hdr=0x%x\n",
              tde->cb_addr,buf_size,buf_size,len,len,tde->atm_hdr);
#endif

   /* copy the ATM header */
   atm_hdr = tde->atm_hdr;

   if (set_pti) {
      atm_hdr &= ~ATM_PTI_NETWORK;
      atm_hdr |= ATM_PTI_EOP;
   }

   atm_hdr = htonl(atm_hdr);
   memcpy(&d->txfifo_cell[0],&atm_hdr,sizeof(atm_hdr));

   /* compute HEC field */
   atm_insert_hec(d->txfifo_cell);

   /* copy the payload and try to transmit if the FIFO is full */
   if (len > 0) {
      physmem_copy_from_vm(d->vm,&d->txfifo_cell[d->txfifo_pos],
                           tde->cb_addr,len);
      d->txfifo_pos += len;
      d->txfifo_avail -= len;
   }

   ti1570_update_tx_dma_bufinfo(tde,buf_size,len);
   return(len);
}

/* 
 * Transmit an Transparent-AAL ATM cell through the NETIO infrastructure.
 */
static int ti1570_transmit_transp_cell(struct pa_a1_data *d,
                                       ti1570_tx_dma_entry_t *tde,
                                       int atm_set_eop,int *buf_end)
{
   m_uint32_t buf_size,len;
   int pkt_end,last_cell;

   pkt_end = tde->ctrl_buf & TI1570_TX_DMA_EOP;
   buf_size = tde->ctrl_buf & TI1570_TX_DMA_DCOUNT_MASK;
   last_cell = FALSE;

   if (!pkt_end) {
      len = ti1570_init_tx_atm_cell(d,tde,FALSE);
      ti1570_send_tx_fifo(d,tde,FALSE);

      if ((buf_size - len) == 0) 
         *buf_end = TRUE;

      return(FALSE);
   }

   /* this is the end of packet and the last buffer */
   if (buf_size <= d->txfifo_avail)
      last_cell = TRUE;

   len = ti1570_init_tx_atm_cell(d,tde,last_cell & atm_set_eop);
   if (last_cell) ti1570_add_tx_padding(d,d->txfifo_avail);
   ti1570_send_tx_fifo(d,tde,FALSE);
   return(last_cell);
}

/* Add the AAL5 trailer to the TX FIFO */
static void ti1570_add_aal5_trailer(struct pa_a1_data *d,
                                    ti1570_tx_dma_entry_t *tde)
{
   m_uint8_t *trailer;

   trailer = &d->txfifo_cell[ATM_AAL5_TRAILER_POS];

   /* Control field + Length */
   *(m_uint32_t *)trailer = htonl(tde->aal5_ctrl);

   /* Final CRC-32 computation */
   tde->aal5_crc = crc32_compute(~tde->aal5_crc,
                                 &d->txfifo_cell[ATM_HDR_SIZE],
                                 ATM_PAYLOAD_SIZE - 4);

   *(m_uint32_t *)(trailer+4) = htonl(~tde->aal5_crc);

   /* Consider the FIFO as full */
   d->txfifo_avail = 0;
}

/*
 * Tranmit an AAL5 cell through the NETIO infrastructure.
 *
 * Returns TRUE if this is the real end of packet.
 */
static int ti1570_transmit_aal5_cell(struct pa_a1_data *d,
                                     ti1570_tx_dma_entry_t *tde,
                                     int *buf_end)
{
   m_uint32_t buf_size,len;
   int pkt_end;

   pkt_end = tde->ctrl_buf & TI1570_TX_DMA_EOP;
   buf_size = tde->ctrl_buf & TI1570_TX_DMA_DCOUNT_MASK;

#if DEBUG_TRANSMIT
   TI1570_LOG(d,"ti1570_transmit_aal5_cell: data ptr=0x%x, "
              "buf_size=0x%x (%u)\n",tde->cb_addr,buf_size,buf_size);
#endif

   /* If this is not the end of packet, transmit the cell normally */
   if (!pkt_end) {
      len = ti1570_init_tx_atm_cell(d,tde,FALSE);
      ti1570_send_tx_fifo(d,tde,TRUE);

      if ((buf_size - len) == 0)
         *buf_end = TRUE;

      return(FALSE);
   }

   /* 
    * This is the end of packet, check if we need to emit a special cell
    * for the AAL5 trailer.
    */
   if ((buf_size + ATM_AAL5_TRAILER_SIZE) <= d->txfifo_avail) {
      len = ti1570_init_tx_atm_cell(d,tde,TRUE);

      /* add the padding */
      ti1570_add_tx_padding(d,d->txfifo_avail - ATM_AAL5_TRAILER_SIZE);

      /* add the AAL5 trailer at offset 40 */
      ti1570_add_aal5_trailer(d,tde);

      /* we can transmit the cell */
      ti1570_send_tx_fifo(d,tde,FALSE);

      *buf_end = TRUE;
      return(TRUE);
   }

   /* Transmit the cell normally */
   len = ti1570_init_tx_atm_cell(d,tde,FALSE);
   ti1570_add_tx_padding(d,d->txfifo_avail);
   ti1570_send_tx_fifo(d,tde,TRUE);
   return(FALSE);
}

/* Update the TX completion ring */
static void ti1570_update_tx_cring(struct pa_a1_data *d,
                                   ti1570_tx_dma_entry_t *tde)
{
   m_uint32_t tcr_addr,tcr_end,val;

   if (tde->ctrl_buf & TI1570_TX_DMA_TCR_SELECT) {
      /* TX completion ring with interrupt */
      tcr_addr = d->iregs[TI1570_REG_TCR_WI_ADDR] + (d->tcr_wi_pos * 4);
   } else {
      /* TX completion ring without interrupt */
      tcr_addr = d->iregs[TI1570_REG_TCR_WOI_ADDR] + (d->tcr_woi_pos * 4);
   }

#if DEBUG_TRANSMIT
   TI1570_LOG(d,"ti1570_update_tx_cring: posting 0x%x at address 0x%x\n",
              tde->sb_addr,tcr_addr);

   physmem_dump_vm(d->vm,tde->sb_addr,sizeof(ti1570_tx_buffer_t) >> 2);
#endif

   /* we have a TX freeze if the buffer belongs to the host */
   val = physmem_copy_u32_from_vm(d->vm,tcr_addr);
   if (!(val & TI1570_TCR_OWN)) {
      d->iregs[TI1570_REG_STATUS] |= TI1570_STAT_TX_FRZ;
      return;
   }

   /* put the buffer address in the ring */
   val = tde->sb_addr >> 2;

   if (tde->ctrl_buf & TI1570_TX_DMA_ABORT)
      val |= TI1570_TCR_ABORT;

   physmem_copy_u32_to_vm(d->vm,tcr_addr,val);

   /* update the internal position pointer */
   if (tde->ctrl_buf & TI1570_TX_DMA_TCR_SELECT) {
      tcr_end = d->iregs[TI1570_REG_TX_CRING_SIZE] & TI1570_TCR_SIZE_MASK;

      if ((d->tcr_wi_pos++) == tcr_end)
         d->tcr_wi_pos = 0;
   } else  {
      tcr_end = (d->iregs[TI1570_REG_TX_CRING_SIZE] >> 16);
      tcr_end &= TI1570_TCR_SIZE_MASK;

      if ((d->tcr_woi_pos++) == tcr_end)
         d->tcr_woi_pos = 0;
   }
}

/* Analyze a TX DMA state table entry */
static int ti1570_scan_tx_dma_entry_single(struct pa_a1_data *d,
                                           m_uint32_t index)
{
   ti1570_tx_dma_entry_t *tde;
   m_uint32_t psr_base,psr_addr,psr_entry,psr_end;
   m_uint32_t buf_addr,buf_size,pkt_type,tmp;
   m_uint32_t psr_index;
   int atm_set_eop = 0;
   int pkt_end,buf_end = 0;

   tde = &d->tx_dma_table[index];

   /* The DMA channel state flag must be ON */
   if (!(tde->dma_state & TI1570_TX_DMA_ON))
      return(FALSE);

#if DEBUG_TX_DMA
   /* We have a running DMA channel */
   TI1570_LOG(d,"ti1570_scan_tx_dma_entry: TX DMA entry %u is ON "
              "(ctrl_buf = 0x%x)\n",index,tde->ctrl_buf);
#endif

   /* Is this the start of a new packet ? */
   if (!(tde->ctrl_buf & TI1570_TX_DMA_ACT))
   {
#if DEBUG_TX_DMA
      TI1570_LOG(d,"ti1570_scan_tx_dma_entry: TX DMA entry %u is not ACT\n",
                 index);
#endif

      /* No packet yet, fetch it from the packet-segmentation ring */
      psr_base = tde->dma_state & TI1570_TX_DMA_RING_OFFSET_MASK;
      psr_index = tde->dma_state & TI1570_TX_DMA_RING_INDEX_MASK;

      /* Compute address of the current packet segmentation ring entry */
      psr_addr = (psr_base + psr_index) << 2;
      psr_entry = physmem_copy_u32_from_vm(d->vm,psr_addr);

#if DEBUG_TX_DMA
      TI1570_LOG(d,"ti1570_scan_tx_dma_entry: psr_addr = 0x%x, "
                 "psr_entry = 0x%x\n",psr_addr,psr_entry);
#endif

      /* The packet-segmentation-ring entry is owned by host, quit now */
      if (!(psr_entry & TI1570_TX_RING_OWN))
         return(FALSE);

      /* Acquire the first buffer (it MUST be in the ready state) */
      buf_addr = (psr_entry & TI1570_TX_RING_PTR_MASK) << 2;

      if (!ti1570_acquire_tx_buffer(d,tde,buf_addr)) {
         TI1570_LOG(d,"ti1570_scan_tx_dma_entry: PSR entry with OWN bit set "
                    "but buffer without RDY bit set.\n");
         return(FALSE);
      }

      /* Set ACT bit for the DMA channel */
      tde->ctrl_buf |= TI1570_TX_DMA_ACT;
   }

   /* Compute the remaining size and determine the packet type */
   buf_size = tde->ctrl_buf & TI1570_TX_DMA_DCOUNT_MASK;
   pkt_type = tde->ctrl_buf & TI1570_TX_DMA_AAL_TYPE_MASK;
   pkt_end  = tde->ctrl_buf & TI1570_TX_DMA_EOP;

#if DEBUG_TRANSMIT
   TI1570_LOG(d,"ti1570_scan_tx_dma_entry: ctrl_buf=0x%8.8x, "
              "cb_addr=0x%8.8x, atm_hdr=0x%8.8x, dma_state=0x%8.8x\n",
              tde->ctrl_buf, tde->cb_addr, tde->atm_hdr, tde->dma_state);

   TI1570_LOG(d,"ti1570_scan_tx_dma_entry: nb_addr=0x%8.8x, "
              "sb_addr=0x%8.8x, aal5_crc=0x%8.8x, aal5_ctrl=0x%8.8x\n",
              tde->nb_addr, tde->sb_addr, tde->aal5_crc, tde->aal5_ctrl);
#endif

   /* 
    * If the current buffer is now empty and if this is not the last
    * buffer in the current packet, try to fetch a new buffer.
    * If the next buffer is not yet ready, we have finished.
    */
   if (!buf_size && !pkt_end && !ti1570_acquire_tx_buffer(d,tde,tde->nb_addr))
      return(FALSE);

   switch(pkt_type) {
      case TI1570_TX_DMA_AAL_TRWPTI:
         atm_set_eop = 1;

      case TI1570_TX_DMA_AAL_TRWOPTI:
         /* Transmit the ATM cell transparently */
         pkt_end = ti1570_transmit_transp_cell(d,tde,atm_set_eop,&buf_end);
         break;

      case TI1570_TX_DMA_AAL_AAL5:
         pkt_end = ti1570_transmit_aal5_cell(d,tde,&buf_end);
         break;

      default:
         TI1570_LOG(d,"ti1570_scan_tx_dma_entry: invalid AAL-type\n");
         return(FALSE);
   }

   /* Re-read the remaining buffer size */
   buf_size = tde->ctrl_buf & TI1570_TX_DMA_DCOUNT_MASK;

   /* Put the buffer address in the transmit completion ring */
   if (buf_end) ti1570_update_tx_cring(d,tde);
   
   /* 
    * If we have reached end of packet (EOP): clear the ACT bit,
    * give back the packet-segmentation ring entry to the host,
    * and increment the PSR index.
    */
   if (pkt_end) {
      tde->ctrl_buf &= ~TI1570_TX_DMA_ACT;

      /* Clear the OWN bit of the packet-segmentation ring entry */
      psr_base = tde->dma_state & TI1570_TX_DMA_RING_OFFSET_MASK;
      psr_index = (tde->dma_state & TI1570_TX_DMA_RING_INDEX_MASK);
      psr_addr = (psr_base + psr_index) << 2;

      psr_entry = physmem_copy_u32_from_vm(d->vm,psr_addr);
      psr_entry &= ~TI1570_TX_RING_OWN;
      physmem_copy_u32_to_vm(d->vm,psr_addr,psr_entry);
      
      /* Increment the packet-segmentation ring index */
      psr_index++;
      psr_end = d->iregs[TI1570_REG_TX_PSR_SIZE] >> 16;
      psr_end &= TI1570_PSR_SIZE_MASK;

      if (psr_index > psr_end) {
         psr_index = 0;
#if DEBUG_TX_DMA
         TI1570_LOG(d,"ti1570_scan_tx_dma_entry: PSR ring rotation "
                    "(psr_end = %u)\n",psr_end);
#endif
      }

      tmp = (tde->dma_state & ~TI1570_TX_DMA_RING_INDEX_MASK);
      tmp |= (psr_index & TI1570_TX_DMA_RING_INDEX_MASK);
      tde->dma_state = tmp;
   }

   /* Generate an interrupt if required */
   if (tde->ctrl_buf & TI1570_TX_DMA_TCR_SELECT) 
   {
      if (((d->iregs[TI1570_REG_CONFIG] & TI1570_CFG_BP_SEL) && buf_end) ||
          pkt_end)
      {
         d->iregs[TI1570_REG_STATUS] |= TI1570_STAT_CP_TX;
         dev_pa_a1_update_irq_status(d);
      }
   }

   return(TRUE);
}

/* Analyze a TX DMA state table entry */
static void ti1570_scan_tx_dma_entry(struct pa_a1_data *d,m_uint32_t index)
{
   int i;

   for(i=0;i<TI1570_TXDMA_PASS_COUNT;i++)
      if (!ti1570_scan_tx_dma_entry_single(d,index))
         break;
}

/* Analyze the TX schedule table */
static void ti1570_scan_tx_sched_table(struct pa_a1_data *d)
{
   m_uint32_t cw,index0,index1;
   u_int i;

   for(i=0;i<TI1570_TX_SCHED_ENTRY_COUNT>>1;i++) {
      cw = d->tx_sched_table[i];

      /* We have 2 index in TX DMA state table per word */
      index0 = (cw >> TI1570_TX_SCHED_E0_SHIFT) & TI1570_TX_SCHED_ENTRY_MASK;
      index1 = (cw >> TI1570_TX_SCHED_E1_SHIFT) & TI1570_TX_SCHED_ENTRY_MASK;

      /* Scan the two entries (null entry => nothing to do) */
      if (index0) ti1570_scan_tx_dma_entry(d,index0);
      if (index1) ti1570_scan_tx_dma_entry(d,index1);
   }
}

/*
 * Read a RX buffer from the host memory.
 */
static void ti1570_read_rx_buffer(struct pa_a1_data *d,m_uint32_t addr,
                                  ti1570_rx_buffer_t *rx_buf)
{
   physmem_copy_from_vm(d->vm,rx_buf,addr,sizeof(ti1570_rx_buffer_t));

   /* byte-swapping */
   rx_buf->reserved = vmtoh32(rx_buf->reserved);
   rx_buf->ctrl     = vmtoh32(rx_buf->ctrl);
   rx_buf->atm_hdr  = vmtoh32(rx_buf->atm_hdr);
   rx_buf->user     = vmtoh32(rx_buf->user);
}

/* Update the RX completion ring */
static void ti1570_update_rx_cring(struct pa_a1_data *d,
                                   ti1570_rx_dma_entry_t *rde,
                                   m_uint32_t atm_hdr,
                                   m_uint32_t aal5_trailer,
                                   m_uint32_t err_ind,
                                   m_uint32_t fbuf_valid)
{
   m_uint32_t rcr_addr,rcr_end,aal_type,ptr,val;
   ti1570_rcr_entry_t rcre;

   if (rde->ctrl & TI1570_RX_DMA_RCR_SELECT) {
      /* RX completion ring with interrupt */
      rcr_addr = d->iregs[TI1570_REG_RCR_WI_ADDR];
      rcr_addr += (d->rcr_wi_pos * sizeof(rcre));
   } else {
      /* RX completion ring without interrupt */
      rcr_addr = d->iregs[TI1570_REG_RCR_WOI_ADDR];
      rcr_addr += (d->rcr_woi_pos * sizeof(rcre));
   }

#if DEBUG_RECEIVE
   TI1570_LOG(d,"ti1570_update_rx_cring: posting 0x%x at address 0x%x\n",
              (rde->sp_ptr << 2),rcr_addr);

   physmem_dump_vm(d->vm,rde->sp_ptr<<2,sizeof(ti1570_rx_buffer_t) >> 2);
#endif

   /* we have a RX freeze if the buffer belongs to the host */
   ptr = rcr_addr + OFFSET(ti1570_rcr_entry_t,fbr_entry);
   val = physmem_copy_u32_from_vm(d->vm,ptr);

   if (!(val & TI1570_RCR_OWN)) {
      TI1570_LOG(d,"ti1570_update_rx_cring: RX freeze...\n");
      d->iregs[TI1570_REG_STATUS] |= TI1570_STAT_RX_FRZ;
      return;
   }

   /* fill the RX completion ring entry and write it back to the host */
   memset(&rcre,0,sizeof(rcre));
   
   /* word 0: atm header from last cell received */
   rcre.atm_hdr = atm_hdr;

   /* word 1: error indicator */
   aal_type = rde->ctrl & TI1570_RX_DMA_AAL_TYPE_MASK;
   if (aal_type == TI1570_RX_DMA_AAL_AAL5)
      rcre.error |= TI1570_RCR_AAL5;
   
   rcre.error |= err_ind;

   /* word 2: Start of packet */
   if (fbuf_valid)
      rcre.sp_addr = TI1570_RCR_VALID | rde->sp_ptr;
 
   /* word 3: AAL5 trailer */
   rcre.aal5_trailer = aal5_trailer;
   
   /* word 4: OWN + error entry + free-buffer ring pointer */
   rcre.fbr_entry = rde->fbr_entry & TI1570_RX_DMA_FB_PTR_MASK;
   if (err_ind) rcre.fbr_entry |= TI1570_RCR_ERROR;

   /* byte-swap and write this back to the host memory */
   rcre.atm_hdr      = htonl(rcre.atm_hdr);
   rcre.error        = htonl(rcre.error);
   rcre.sp_addr      = htonl(rcre.sp_addr);
   rcre.aal5_trailer = htonl(rcre.aal5_trailer);
   rcre.fbr_entry    = htonl(rcre.fbr_entry);
   physmem_copy_to_vm(d->vm,&rcre,rcr_addr,sizeof(rcre));

   /* clear the active bit of the RX DMA entry */
   rde->ctrl &= ~TI1570_RX_DMA_ACT;

   /* update the internal position pointer */
   if (rde->ctrl & TI1570_RX_DMA_RCR_SELECT) {
      rcr_end = d->iregs[TI1570_REG_RX_CRING_SIZE] & TI1570_RCR_SIZE_MASK;

      if ((d->rcr_wi_pos++) == rcr_end)
         d->rcr_wi_pos = 0;

      /* generate the appropriate IRQ */
      d->iregs[TI1570_REG_STATUS] |= TI1570_STAT_CP_RX;
      dev_pa_a1_update_irq_status(d);
   } else  {
      rcr_end = (d->iregs[TI1570_REG_RX_CRING_SIZE] >> 16);
      rcr_end &= TI1570_RCR_SIZE_MASK;

      if ((d->rcr_woi_pos++) == rcr_end)
         d->rcr_woi_pos = 0;
   }
}

/* 
 * Acquire a free RX buffer.
 *
 * Returns FALSE if no buffer is available (buffer starvation).
 */
static int ti1570_acquire_rx_buffer(struct pa_a1_data *d,
                                    ti1570_rx_dma_entry_t *rde,
                                    ti1570_rx_buf_holder_t *rbh,
                                    m_uint32_t atm_hdr)
{  
   ti1570_rx_fbr_entry_t *fbr_entry = NULL;
   m_uint32_t bp_addr,buf_addr,buf_size,buf_idx;
   m_uint32_t ring_index,ring_size;
   m_uint32_t buf_ptr,val;
   int fifo = FALSE;

   /* To keep this fucking compiler quiet */
   ring_size = 0;
   buf_idx = 0;

   if (rde->ctrl & TI1570_RX_DMA_FIFO) { 
      bp_addr  = (rde->fbr_entry & TI1570_RX_DMA_FB_PTR_MASK) << 2;
      buf_ptr  = physmem_copy_u32_from_vm(d->vm,bp_addr);
      buf_size = d->iregs[TI1570_REG_TX_PSR_SIZE] & 0xFFFF;
      fifo = TRUE;

#if DEBUG_RECEIVE
      TI1570_LOG(d,"ti1570_acquire_rx_buffer: acquiring FIFO buffer\n");
#endif
   } 
   else 
   {
      ring_index = rde->fbr_entry & TI1570_RX_DMA_FB_INDEX_MASK;
      fbr_entry = &d->rx_fbr_table[ring_index];

#if DEBUG_RECEIVE
      TI1570_LOG(d,"ti1570_acquire_rx_buffer: acquiring non-FIFO buffer, "
                 "ring index=%u (0x%x)\n",ring_index,ring_index);
#endif

      /* Compute the number of entries in ring */
      ring_size = fbr_entry->ring_size & TI1570_RX_FBR_RS_MASK;
      ring_size >>= TI1570_RX_FBR_RS_SHIFT;
      ring_size = (ring_size << 4) + 15 + 1;

      /* Compute the buffer size */
      buf_size = fbr_entry->ring_size & TI1570_RX_FBR_BS_MASK;
      buf_size >>= TI1570_RX_FBR_BS_SHIFT;

      /* Compute the buffer address */
      buf_idx  = fbr_entry->ring_size & TI1570_RX_FBR_IDX_MASK;
      bp_addr = fbr_entry->fbr_ptr + (buf_idx << 2);

#if DEBUG_RECEIVE
      TI1570_LOG(d,"ti1570_acquire_rx_buffer: ring size=%u (0x%x), "
                 "buf size=%u ATM cells\n",ring_size,ring_size,buf_size);

      TI1570_LOG(d,"ti1570_acquire_rx_buffer: buffer index=%u (0x%x), "
                 "buffer ptr address = 0x%x\n",buf_idx,buf_idx,bp_addr);
#endif

      buf_ptr = physmem_copy_u32_from_vm(d->vm,bp_addr);
   }

#if DEBUG_RECEIVE
   TI1570_LOG(d,"ti1570_acquire_rx_buffer: buf_ptr = 0x%x\n",buf_ptr);
#endif

   /* The TI1570 must own the buffer */
   if (!(buf_ptr & TI1570_RX_BUFPTR_OWN)) {
      TI1570_LOG(d,"ti1570_acquire_rx_buffer: no free buffer available.\n");
      return(FALSE);
   }

   /* 
    * If we are using a ring, we have to clear the OWN bit and increment
    * the index field.
    */
   if (!fifo) {
      buf_ptr &= ~TI1570_RX_BUFPTR_OWN;
      physmem_copy_u32_to_vm(d->vm,bp_addr,buf_ptr);

      if (++buf_idx == ring_size) {
#if DEBUG_RECEIVE
         TI1570_LOG(d,"ti1570_acquire_rx_buffer: buf_idx=0x%x, "
                    "ring_size=0x%x -> resetting buf_idx\n",
                    buf_idx-1,ring_size);
#endif
         buf_idx = 0;
      }

      val = fbr_entry->ring_size & ~TI1570_RX_FBR_IDX_MASK;
      val |= buf_idx;
      fbr_entry->ring_size = val;
   }

   /* Get the buffer address */
   buf_addr = (buf_ptr & TI1570_RX_BUFPTR_MASK) << 2;

#if DEBUG_RECEIVE
   TI1570_LOG(d,"ti1570_acquire_rx_buffer: buf_addr = 0x%x\n",buf_addr);
#endif

   /* Read the buffer descriptor itself and store info for caller */
   rbh->buf_addr = buf_addr;
   rbh->buf_size = buf_size;
   ti1570_read_rx_buffer(d,buf_addr,&rbh->rx_buf);

   /* Clear the control field */
   physmem_copy_u32_to_vm(d->vm,buf_addr+OFFSET(ti1570_rx_buffer_t,ctrl),0);

   /* Store the ATM header in data buffer */
   physmem_copy_u32_to_vm(d->vm,buf_addr+OFFSET(ti1570_rx_buffer_t,atm_hdr),
                          atm_hdr);
   return(TRUE);
}

/* Insert a new free buffer in a RX DMA entry */
static void ti1570_insert_rx_free_buf(struct pa_a1_data *d,
                                      ti1570_rx_dma_entry_t *rde,
                                      ti1570_rx_buf_holder_t *rbh)
{
   m_uint32_t val,aal_type;

   aal_type = rde->ctrl & TI1570_RX_DMA_AAL_TYPE_MASK;

   /* Set current and start of buffer addresses */
   rde->cb_addr = rbh->buf_addr + sizeof(ti1570_rx_buffer_t);
   rde->sb_addr = rbh->buf_addr >> 2;
   
   /* Set the buffer length */
   val = rbh->buf_size;

   if (aal_type == TI1570_RX_DMA_AAL_CNT)
      val |= (rde->aal5_crc & 0xFFFF) << 16;

   rde->cb_len = val;
}

/* Store a RX cell */
static int ti1570_store_rx_cell(struct pa_a1_data *d,
                                ti1570_rx_dma_entry_t *rde,
                                m_uint8_t *atm_cell)
{
   m_uint32_t aal_type,atm_hdr,aal5_trailer,pti,real_eop,pti_eop;
   m_uint32_t prev_buf_addr,buf_len,val,ptr,cnt;
   ti1570_rx_buf_holder_t rbh;
   
   real_eop = pti_eop = FALSE;
   aal_type = rde->ctrl & TI1570_RX_DMA_AAL_TYPE_MASK;
      
   /* Extract PTI from the ATM header */
   atm_hdr = ntohl(*(m_uint32_t *)&atm_cell[0]);
   pti = (atm_hdr & ATM_HDR_PTI_MASK) >> ATM_HDR_PTI_SHIFT;

   /* PTI == 0x1 => EOP */
   if ((pti == 0x01) || (pti == 0x03))
      pti_eop = TRUE;
   
   if (rde->ctrl & TI1570_RX_DMA_WAIT_EOP) {
      TI1570_LOG(d,"ti1570_store_rx_cell: EOP processing, not handled yet.\n");
      return(FALSE);
   }

   /* AAL5 special processing */
   if (aal_type == TI1570_RX_DMA_AAL_AAL5)
   {
      /* Check that we don't exceed 1366 cells for AAL5 */
      /* XXX TODO */
   } 
   else
   {
      /* EOP processing for non counter-based transparent-AAL packets */
      if ((rde->ctrl & TI1570_RX_DMA_WAIT_EOP) && pti_eop)
      {
         /* XXX TODO */
      }
   }

   /* do we have enough room in buffer ? */
   buf_len = rde->cb_len & TI1570_RX_DMA_CB_LEN_MASK;

   if (!buf_len) {
      prev_buf_addr = rde->sb_addr << 2;

      /* acquire a new free buffer */
      if (!ti1570_acquire_rx_buffer(d,rde,&rbh,atm_hdr)) {
         rde->ctrl |= TI1570_RX_DMA_WAIT_EOP;
         return(FALSE);
      }

      /* insert the free buffer in the RX DMA structure */
      ti1570_insert_rx_free_buf(d,rde,&rbh);

      /* chain the buffers (keep SOP/EOP bits intact) */
      ptr = prev_buf_addr + OFFSET(ti1570_rx_buffer_t,ctrl);

      val = physmem_copy_u32_from_vm(d->vm,ptr);
      val |= rde->sb_addr;
      physmem_copy_u32_to_vm(d->vm,ptr,val);

      /* read the new buffer length */
      buf_len = rde->cb_len & TI1570_RX_DMA_CB_LEN_MASK;
   }

   /* copy the ATM payload */
#if DEBUG_RECEIVE
   TI1570_LOG(d,"ti1570_store_rx_cell: storing cell payload at 0x%x "
              "(buf_addr=0x%x)\n",rde->cb_addr,rde->sb_addr << 2);
#endif

   physmem_copy_to_vm(d->vm,&atm_cell[ATM_HDR_SIZE],
                      rde->cb_addr,ATM_PAYLOAD_SIZE);
   rde->cb_addr += ATM_PAYLOAD_SIZE;

   /* update the current buffer length */
   val = rde->cb_len & ~TI1570_RX_DMA_CB_LEN_MASK;
   rde->cb_len = val | (--buf_len);

#if DEBUG_RECEIVE
   TI1570_LOG(d,"ti1570_store_rx_cell: new rde->cb_len = 0x%x, "
              "buf_len=0x%x\n",rde->cb_len,buf_len);
#endif

   /* determine if this is the end of the packet (EOP) */
   if (aal_type == TI1570_RX_DMA_AAL_CNT) 
   {   
      /* counter-based tranparent-AAL packets */
      cnt = rde->cb_len & TI1570_RX_DMA_TR_CNT_MASK;
      cnt >>= TI1570_RX_DMA_TR_CNT_SHIFT;

      /* if the counter reaches 0, this is the EOP */
      if (--cnt == 0)
         real_eop = TRUE;

      val = rde->cb_len & ~TI1570_RX_DMA_TR_CNT_MASK;
      val |= cnt << TI1570_RX_DMA_TR_CNT_SHIFT;
   }
   else {
      /* PTI-based transparent AAL packets or AAL5 */
      if (pti_eop)
         real_eop = TRUE;
   }

   if (real_eop) {
      /* mark the buffer as EOP */
      ptr = (rde->sb_addr << 2) + OFFSET(ti1570_rx_buffer_t,ctrl);
      val = physmem_copy_u32_from_vm(d->vm,ptr);
      val |= TI1570_RX_BUFFER_EOP;
      physmem_copy_u32_to_vm(d->vm,ptr,val);

      /* get the aal5 trailer */
      aal5_trailer = ntohl(*(m_uint32_t *)&atm_cell[ATM_AAL5_TRAILER_POS]);

      /* post the entry into the appropriate RX completion ring */
      ti1570_update_rx_cring(d,rde,atm_hdr,aal5_trailer,0,TRUE);
   }

   return(TRUE);
}

/* Handle a received ATM cell */
static int ti1570_handle_rx_cell(netio_desc_t *nio,
                                 u_char *atm_cell,ssize_t cell_len,
                                 struct pa_a1_data *d)
{
   m_uint32_t atm_hdr,vpi,vci,vci_idx,vci_mask;
   m_uint32_t vci_max,rvd_entry,bptr,pti,ptr;
   ti1570_rx_dma_entry_t *rde = NULL;
   ti1570_rx_buf_holder_t rbh;

   if (cell_len != ATM_CELL_SIZE) {
      TI1570_LOG(d,"invalid RX cell size (%ld)\n",(long)cell_len);
      return(FALSE);
   }

   /* Extract the VPI/VCI used as index in the RX VPI/VCI DMA pointer table */
   atm_hdr = ntohl(*(m_uint32_t *)&atm_cell[0]);
   vpi = (atm_hdr & ATM_HDR_VPI_MASK) >> ATM_HDR_VPI_SHIFT;
   vci = (atm_hdr & ATM_HDR_VCI_MASK) >> ATM_HDR_VCI_SHIFT;
   pti = (atm_hdr & ATM_HDR_PTI_MASK) >> ATM_HDR_PTI_SHIFT;

#if DEBUG_RECEIVE
   TI1570_LOG(d,"ti1570_handle_rx_cell: received cell with VPI/VCI=%u/%u\n",
              vpi,vci);
#endif

   /* Get the entry corresponding to this VPI in RX VPI/VCI dma ptr table */
   rvd_entry = d->rx_vpi_vci_dma_table[vpi];
  
   if (!(rvd_entry & TI1570_RX_VPI_ENABLE)) {
      TI1570_LOG(d,"ti1570_handle_rx_cell: received cell with "
                 "unknown VPI %u (VCI=%u)\n",vpi,vci);
      return(FALSE);
   }

   /* 
    * Special routing for OAM F4 cells:
    *   - VCI 3 : OAM F4 segment cell
    *   - VCI 4 : OAM F4 end-to-end cell
    */
   if ((vci == 3) || (vci == 4))
      rde = &d->rx_dma_table[2];
   else {
      if ((atm_hdr & ATM_PTI_NETWORK) != 0) {      
         switch(pti) {
            case 0x04:   /* OAM F5-segment cell */
            case 0x05:   /* OAM F5 end-to-end cell */
               rde = &d->rx_dma_table[0];
               break;

            case 0x06:
            case 0x07:
               rde = &d->rx_dma_table[1];
               break;
         }
      } else {
         /* 
          * Standard VPI/VCI.
          * Apply the VCI mask if we don't have an OAM cell.
          */
         if (!(atm_hdr & ATM_PTI_NETWORK)) {
            vci_mask = d->iregs[TI1570_REG_TX_RX_FIFO] >> 16;
            vci_idx  = vci & (~vci_mask);

            vci_max = rvd_entry & TI1570_RX_VCI_RANGE_MASK;

            if (vci_idx > vci_max) {
               TI1570_LOG(d,"ti1570_handle_rx_cell: out-of-range VCI %u "
                          "(VPI=%u,vci_mask=%u,vci_max=%u)\n",
                          vci,vpi,vci_mask,vci_max);
               return(FALSE);
            }

#if DEBUG_RECEIVE
            TI1570_LOG(d,"ti1570_handle_rx_cell: VPI/VCI=%u/%u, "
                       "vci_mask=0x%x, vci_idx=%u (0x%x), vci_max=%u (0x%x)\n",
                       vpi,vci,vci_mask,vci_idx,vci_idx,vci_max,vci_max);
#endif
            bptr = (rvd_entry & TI1570_RX_BASE_PTR_MASK);
            bptr >>= TI1570_RX_BASE_PTR_SHIFT;
            bptr = (bptr + vci) * sizeof(ti1570_rx_dma_entry_t);

            if (bptr < TI1570_RX_DMA_TABLE_OFFSET) {
               TI1570_LOG(d,"ti1570_handle_rx_cell: inconsistency in "
                          "RX VPI/VCI table, VPI/VCI=%u/u, bptr=0x%x\n",
                          vpi,vci,bptr);
               return(FALSE);
            }

            bptr -= TI1570_RX_DMA_TABLE_OFFSET;      
            rde = &d->rx_dma_table[bptr / sizeof(ti1570_rx_dma_entry_t)];
         }
      }
   }

   if (!rde) {
      TI1570_LOG(d,"ti1570_handle_rx_cell: no RX DMA table entry found!\n");
      return(FALSE);
   }

   /* The entry must be active */
   if (!(rde->fbr_entry & TI1570_RX_DMA_ON))
      return(FALSE);

   /* Is this the start of a new packet ? */
   if (!(rde->ctrl & TI1570_RX_DMA_ACT)) 
   {
      /* Try to acquire a free buffer */
      if (!ti1570_acquire_rx_buffer(d,rde,&rbh,atm_hdr)) {
         rde->ctrl |= TI1570_RX_DMA_WAIT_EOP;
         return(FALSE);
      }

      /* Insert the free buffer in the RX DMA structure */
      ti1570_insert_rx_free_buf(d,rde,&rbh);
      rde->sp_ptr = rde->sb_addr;

      /* Mark the RX buffer as the start of packet (SOP) */
      ptr = (rde->sb_addr << 2) + OFFSET(ti1570_rx_buffer_t,ctrl);
      physmem_copy_u32_to_vm(d->vm,ptr,TI1570_RX_BUFFER_SOP);

      /* Set ACT bit for the DMA channel */
      rde->ctrl |= TI1570_RX_DMA_ACT;
   }

   /* Store the received cell */
   ti1570_store_rx_cell(d,rde,atm_cell);
   return(TRUE);
}

/*
 * pci_ti1570_read()
 */
static m_uint32_t pci_ti1570_read(cpu_gen_t *cpu,struct pci_device *dev,
                                  int reg)
{
   struct pa_a1_data *d = dev->priv_data;

#if DEBUG_ACCESS
   TI1570_LOG(d,"pci_ti1570_read: read reg 0x%x\n",reg);
#endif

   switch(reg) {
      case PCI_REG_BAR0:
         return(d->dev->phys_addr);
      default:
         return(0);
   }
}

/*
 * pci_ti1570_write()
 */
static void pci_ti1570_write(cpu_gen_t *cpu,struct pci_device *dev,
                             int reg,m_uint32_t value)
{
   struct pa_a1_data *d = dev->priv_data;

#if DEBUG_ACCESS
   TI1570_LOG(d,"pci_ti1570_write: write reg 0x%x, value 0x%x\n",reg,value);
#endif

   switch(reg) {
      case PCI_REG_BAR0:
         vm_map_device(cpu->vm,d->dev,(m_uint64_t)value);
         TI1570_LOG(d,"registers are mapped at 0x%x\n",value);
         break;
   }
}

/*
 * pci_plx9060es_read()
 */
static m_uint32_t pci_plx9060es_read(cpu_gen_t *cpu,struct pci_device *dev,
                                     int reg)
{
   //struct pa_a1_data *d = dev->priv_data;

#if DEBUG_ACCESS
   TI1570_LOG(d,"PLX9060ES","read reg 0x%x\n",reg);
#endif
   switch(reg) {
      default:
         return(0);
   }
}

/*
 * pci_plx9060es_write()
 */
static void pci_plx9060es_write(cpu_gen_t *cpu,struct pci_device *dev,
                                int reg,m_uint32_t value)
{
   //struct pa_a1_data *d = dev->priv_data;

#if DEBUG_ACCESS
   TI1570_LOG(d,"PLX9060ES","write reg 0x%x, value 0x%x\n",reg,value);
#endif

   switch(reg) {
   }
}

/* Reset the TI1570 */
static void ti1570_reset(struct pa_a1_data *d,int clear_ctrl_mem)
{
   ti1570_clear_tx_fifo(d);

   d->tcr_wi_pos = d->tcr_woi_pos = 0;
   d->rcr_wi_pos = d->rcr_woi_pos = 0;

   if (clear_ctrl_mem)
      memset(d->ctrl_mem_ptr,0,TI1570_CTRL_MEM_SIZE);
}

/*
 * dev_c7200_pa_a1_init()
 *
 * Add a PA-A1 port adapter into specified slot.
 */
int dev_c7200_pa_a1_init(vm_instance_t *vm,struct cisco_card *card)
{   
   u_int slot = card->slot_id;
   struct pci_device *pci_dev_ti,*pci_dev_plx;
   struct pa_a1_data *d;
   struct vdevice *dev;
   m_uint8_t *p;

   /* Allocate the private data structure for TI1570 chip */
   if (!(d = malloc(sizeof(*d)))) {
      vm_error(vm,"%s: out of memory\n",card->dev_name);
      return(-1);
   }

   memset(d,0,sizeof(*d));

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_pa("PA-A1"));
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Add PCI device TI1570 */
   pci_dev_ti = pci_dev_add(card->pci_bus,card->dev_name,
                            TI1570_PCI_VENDOR_ID,TI1570_PCI_PRODUCT_ID,
                            0,0,c7200_net_irq_for_slot_port(slot,0),d,
                            NULL,pci_ti1570_read,pci_ti1570_write);

   if (!pci_dev_ti) {
      vm_error(vm,"%s: unable to create PCI device TI1570.\n",
               card->dev_name);
      return(-1);
   }

   /* Add PCI device PLX9060ES */
   pci_dev_plx = pci_dev_add(card->pci_bus,card->dev_name,
                             PLX_9060ES_PCI_VENDOR_ID,
                             PLX_9060ES_PCI_PRODUCT_ID,
                             1,0,-1,d,
                             NULL,pci_plx9060es_read,pci_plx9060es_write);

   if (!pci_dev_plx) {
      vm_error(vm,"%s: unable to create PCI device PLX 9060ES.\n",
               card->dev_name);
      return(-1);
   }

   /* Create the TI1570 structure */
   d->name        = card->dev_name;
   d->vm          = vm;
   d->pci_dev_ti  = pci_dev_ti;
   d->pci_dev_plx = pci_dev_plx;

   /* Allocate the control memory */
   if (!(d->ctrl_mem_ptr = malloc(TI1570_CTRL_MEM_SIZE))) {
      vm_error(vm,"%s: unable to create control memory.\n",card->dev_name);
      return(-1);
   }

   /* Standard tables for the TI1570 */
   p = (m_uint8_t *)d->ctrl_mem_ptr;

   d->iregs = (m_uint32_t *)(p + TI1570_INTERNAL_REGS_OFFSET);
   d->tx_sched_table = (m_uint32_t *)(p + TI1570_TX_SCHED_OFFSET);
   d->tx_dma_table = (ti1570_tx_dma_entry_t *)(p + TI1570_TX_DMA_TABLE_OFFSET);
   d->rx_vpi_vci_dma_table = (m_uint32_t *)(p+TI1570_RX_DMA_PTR_TABLE_OFFSET);
   d->rx_dma_table = (ti1570_rx_dma_entry_t *)(p + TI1570_RX_DMA_TABLE_OFFSET);
   d->rx_fbr_table = (ti1570_rx_fbr_entry_t *)(p + TI1570_FREE_BUFFERS_OFFSET);

   ti1570_reset(d,TRUE);

   /* Create the device itself */
   if (!(dev = dev_create(card->dev_name))) {
      vm_error(vm,"%s: unable to create device.\n",card->dev_name);
      return(-1);
   }

   dev->phys_addr = 0;
   dev->phys_len  = 0x200000;
   dev->handler   = dev_pa_a1_access;

   /* Store device info */
   dev->priv_data = d;
   d->dev = dev;
   
   /* Store device info into the router structure */
   card->drv_info = d;
   return(0);
}

/* Remove a PA-A1 from the specified slot */
int dev_c7200_pa_a1_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa_a1_data *d = card->drv_info;

   /* Remove the PA EEPROM */
   cisco_card_unset_eeprom(card);
   c7200_set_slot_eeprom(VM_C7200(vm),card->slot_id,NULL);

   /* Remove the PCI devices */
   pci_dev_remove(d->pci_dev_ti);
   pci_dev_remove(d->pci_dev_plx);

   /* Remove the device from the VM address space */
   vm_unbind_device(vm,d->dev);
   cpu_group_rebuild_mts(vm->cpu_group);

   /* Free the control memory */
   free(d->ctrl_mem_ptr);

   /* Free the device structure itself */
   free(d->dev);
   free(d);
   return(0);
}

/* Bind a Network IO descriptor to a specific port */
int dev_c7200_pa_a1_set_nio(vm_instance_t *vm,struct cisco_card *card,
                            u_int port_id,netio_desc_t *nio)
{
   struct pa_a1_data *d = card->drv_info;

   if (!d || (port_id > 0))
      return(-1);

   if (d->nio != NULL)
      return(-1);

   d->nio = nio;
   d->tx_tid = ptask_add((ptask_callback)ti1570_scan_tx_sched_table,d,NULL);
   netio_rxl_add(nio,(netio_rx_handler_t)ti1570_handle_rx_cell,d,NULL);
   return(0);
}

/* Unbind a Network IO descriptor to a specific port */
int dev_c7200_pa_a1_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                              u_int port_id)
{
   struct pa_a1_data *d = card->drv_info;

   if (!d || (port_id > 0))
      return(-1);

   if (d->nio) {
      ptask_remove(d->tx_tid);
      netio_rxl_remove(d->nio);
      d->nio = NULL;
   }
   return(0);
}

/* PA-A1 driver */
struct cisco_card_driver dev_c7200_pa_a1_driver = {
   "PA-A1", 1, 0,
   dev_c7200_pa_a1_init,
   dev_c7200_pa_a1_shutdown,
   NULL,
   dev_c7200_pa_a1_set_nio,
   dev_c7200_pa_a1_unset_nio,
   NULL,
};
