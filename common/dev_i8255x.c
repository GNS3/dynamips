/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot.
 *
 * Intel i8255x (eepro100) Ethernet chip emulation.
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
#include "dev_i8255x.h"

/* Debugging flags */
#define DEBUG_MII_REGS   0
#define DEBUG_PCI_REGS   0
#define DEBUG_ACCESS     0
#define DEBUG_TRANSMIT   0
#define DEBUG_RECEIVE    0
#define DEBUG_UNKNOWN    0

/* Intel i8255x PCI vendor/product codes */
#define I8255X_PCI_VENDOR_ID    0x8086
#define I8255X_PCI_PRODUCT_ID   0x1229

/* Maximum packet size */
#define I8255X_MAX_PKT_SIZE  4096

/* MDI Control Register */
#define I8255X_MDI_IE          0x20000000
#define I8255X_MDI_R           0x10000000
#define I8255X_MDI_OP_MASK     0x0C000000
#define I8255X_MDI_OP_SHIFT    26
#define I8255X_MDI_PHY_MASK    0x03E00000
#define I8255X_MDI_PHY_SHIFT   21
#define I8255X_MDI_REG_MASK    0x001F0000
#define I8255X_MDI_REG_SHIFT   16
#define I8255X_MDI_DATA_MASK   0x0000FFFF

/* Microcode size (in dwords) */
#define I8255X_UCODE_SIZE  64

/* Size of configuration block (in dwords) */
#define I8255X_CONFIG_SIZE  6

/* Size of statistical counters (in dwords) */
#define I8255X_STAT_CNT_SIZE  20

/* SCB Command Register (Command byte) */
#define SCB_CMD_RUC_MASK   0x07  /* RU Command */
#define SCB_CMD_RUC_SHIFT  0
#define SCB_CMD_CUC_MASK   0xF0  /* CU Command */
#define SCB_CMD_CUC_SHIFT  4

/* SCB Command Register (Interrupt Control Byte) */
#define SCB_CMD_CX    0x80  /* CX Mask */
#define SCB_CMD_FR    0x40  /* FR Mask */
#define SCB_CMD_CNA   0x20  /* CNA Mask */
#define SCB_CMD_RNR   0x10  /* RNR Mask */
#define SCB_CMD_ER    0x08  /* ER Mask */
#define SCB_CMD_FCP   0x04  /* FCP Mask */
#define SCB_CMD_SI    0x02  /* Software generated interrupt */
#define SCB_CMD_M     0x01  /* Mask Interrupt */

/* SCB Interrupt Mask */
#define SCB_INT_MASK  0xF0

/* SCB Status Word (Stat/ACK byte) */
#define SCB_STAT_CX   0x80  /* CU finished command execution */
#define SCB_STAT_FR   0x40  /* RU finished Frame Reception */
#define SCB_STAT_CNA  0x20  /* CU left active state or entered idle state */
#define SCB_STAT_RNR  0x10  /* RU left ready state */
#define SCB_STAT_MDI  0x08  /* MDI read/write cycle completed */
#define SCB_STAT_SWI  0x04  /* Software generated interrupt */
#define SCB_STAT_FCP  0x01  /* Flow Control Pause interrupt */

/* CU states */
#define CU_STATE_IDLE     0x00  /* Idle */
#define CU_STATE_SUSPEND  0x01  /* Suspended */
#define CU_STATE_LPQ_ACT  0x02  /* LPQ Active */
#define CU_STATE_HQP_ACT  0x03  /* HQP Active */

/* RU states */
#define RU_STATE_IDLE     0x00  /* Idle */
#define RU_STATE_SUSPEND  0x01  /* Suspended */
#define RU_STATE_NO_RES   0x02  /* No RX ressources available */
#define RU_STATE_READY    0x04  /* Ready */

/* CU (Command Unit) commands */
#define CU_CMD_NOP                0x00  /* No Operation */
#define CU_CMD_START              0x01  /* Start */
#define CU_CMD_RESUME             0x02  /* Resume */
#define CU_CMD_LOAD_DUMP_CNT      0x04  /* Load Dump Counters Address */
#define CU_CMD_DUMP_STAT_CNT      0x05  /* Dump Statistical Counters */
#define CU_CMD_LOAD_CU_BASE       0x06  /* Load CU Base */
#define CU_CMD_DUMP_RST_STAT_CNT  0x07  /* Dump & Reset Stat Counters */
#define CU_CMD_STAT_RESUME        0x0a  /* Static Resume */

/* RU (Receive Unit) commands */
#define RU_CMD_NOP                0x00  /* No Operation */
#define RU_CMD_START              0x01  /* Start */
#define RU_CMD_RESUME             0x02  /* Resume */
#define RU_CMD_RX_DMA_REDIRECT    0x03  /* Receive DMA redirect */
#define RU_CMD_ABORT              0x04  /* Abort */
#define RU_CMD_LOAD_HDS           0x05  /* Load Header Data Size */
#define RU_CMD_LOAD_RU_BASE       0x06  /* Load RU Base */

/* CB (Command Block) commands */
#define CB_CMD_NOP            0x00  /* No Operation */
#define CB_CMD_IADDR_SETUP    0x01  /* Individual Address Setup */
#define CB_CMD_CONFIGURE      0x02  /* Configure Device Parameters */
#define CB_CMD_XCAST_SETUP    0x03  /* Multicast Address Setup */
#define CB_CMD_TRANSMIT       0x04  /* Transmit a single frame */
#define CB_CMD_LOAD_UCODE     0x05  /* Load Microcode */
#define CB_CMD_DUMP           0x06  /* Dump Internal Registers */
#define CB_CMD_DIAGNOSE       0x07  /* Diagnostics */

/* CB (Command Block) control/status word */
#define CB_CTRL_EL          0x80000000   /* Last command in CBL */
#define CB_CTRL_S           0x40000000   /* Suspend CU after completion */
#define CB_CTRL_I           0x20000000   /* Interrupt at end of exec (CX) */
#define CB_CTRL_SF          0x00080000   /* Mode: 0=simplified,1=flexible */
#define CB_CTRL_CMD_MASK    0x00070000   /* Command */
#define CB_CTRL_CMD_SHIFT   16
#define CB_CTRL_C           0x00008000   /* Execution status (1=completed) */
#define CB_CTRL_OK          0x00002000   /* Command success */

/* CB Transmit Command */
#define TXCB_NUM_MASK    0xFF000000   /* TBD Number */
#define TXCB_NUM_SHIFT   24
#define TXCB_EOF         0x00008000   /* Whole frame in TxCB */
#define TXCB_BLK_SIZE    0x00003FFF   /* TxCB Byte count */

/* Receive Frame Descriptor (RxFD) control status/word */
#define RXFD_CTRL_EL        0x80000000   /* Last RXFD in RFA */
#define RXFD_CTRL_S         0x40000000   /* Suspend RU after completion */
#define RXFD_CTRL_H         0x00100000   /* Header RXFD */
#define RXFD_CTRL_SF        0x00080000   /* Mode: 0=simplified,1=flexible */
#define RXFD_CTRL_C         0x00008000   /* Execution status (1=completed) */
#define RXFD_CTRL_OK        0x00002000   /* Packet OK */
#define RXFD_CTRL_CRC_ERR   0x00000800   /* CRC Error */
#define RXFD_CTRL_CRC_AL    0x00000400   /* Alignment Error */
#define RXFD_CTRL_NO_RES    0x00000200   /* No Ressources */
#define RXFD_CTRL_DMA_OV    0x00000100   /* DMA Overrun */
#define RXFD_CTRL_FTS       0x00000080   /* Frame Too Short */
#define RXFD_CTRL_TL        0x00000020   /* Type/Length */
#define RXFD_CTRL_ERR       0x00000010   /* RX Error */
#define RXFD_CTRL_NAM       0x00000004   /* No Address Match */
#define RXFD_CTRL_IAM       0x00000002   /* Individual Address Match */
#define RXFD_CTRL_COLL      0x00000001   /* RX Collision */

#define RXFD_EOF            0x00008000   /* End Of Frame */
#define RXFD_SIZE_MASK      0x00003FFF   /* Size mask */

#define RXBD_CTRL_EOF       0x00008000   /* End Of Frame */
#define RXBD_CTRL_F         0x00004000   /* Buffer used  */

/* Tx Buffer Descriptor */
struct i8255x_txbd {
   m_uint32_t buf_addr;
   m_uint32_t buf_size;
};

/* CU (Command Unit) Action */
struct i8255x_cu_action {
   m_uint32_t ctrl;
   m_uint32_t link_offset;
   m_uint32_t txbd_addr;
   m_uint32_t txbd_count;
};

/* RX Buffer Descriptor */
struct i8255x_rxbd {
   m_uint32_t ctrl;
   m_uint32_t rxbd_next;
   m_uint32_t buf_addr;
   m_uint32_t buf_size;
};

/* RX Frame Descriptor */
struct i8255x_rxfd {
   m_uint32_t ctrl;
   m_uint32_t link_offset;
   m_uint32_t rxbd_addr;
   m_uint32_t rxbd_size;
};

/* Statistical counters indexes */
#define STAT_CNT_TX_GOOD     0   /* Transmit good frames */
#define STAT_CNT_RX_GOOD     9   /* Receive good frames */
#define STAT_CNT_RX_RES_ERR  12  /* Receive resource errors */


/* Intel i8255x private data */
struct i8255x_data {
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

   /* CU and RU current states */
   u_int cu_state,ru_state;

   /* CU/RU bases + current offsets */
   m_uint32_t cu_base,ru_base;
   m_uint32_t cu_offset,ru_offset;

   /* SCB general pointer */
   m_uint32_t scb_gptr;

   /* SCB Interrupt Control */
   m_uint8_t scb_ic;

   /* SCB Status Acknowledge (for interrupts) */
   m_uint8_t scb_stat_ack;

   /* Statistical counters address */
   m_uint32_t stat_cnt_addr;

   /* MII registers */
   m_uint32_t mii_ctrl;
   u_int mii_regs[32][32];

   /* MAC Individual Address */
   n_eth_addr_t iaddr;

   /* Configuration data */
   m_uint32_t config_data[I8255X_CONFIG_SIZE];

   /* Microcode */
   m_uint32_t microcode[I8255X_UCODE_SIZE];

   /* Statistical counters */
   m_uint32_t stat_counters[I8255X_STAT_CNT_SIZE];

   /* TX packet buffer */
   m_uint8_t tx_buffer[I8255X_MAX_PKT_SIZE];
};

#define EEPRO_LOCK(d)   pthread_mutex_lock(&(d)->lock)
#define EEPRO_UNLOCK(d) pthread_mutex_unlock(&(d)->lock)

/* Log an message */
#define EEPRO_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

enum {
   MII_OPCODE_WRITE = 1,
   MII_OPCODE_READ,
};

/* Read a MII register */
static m_uint16_t mii_reg_read(struct i8255x_data *d)
{
   u_int mii_phy,mii_reg;

   mii_phy = (d->mii_ctrl & I8255X_MDI_PHY_MASK) >> I8255X_MDI_PHY_SHIFT;
   mii_reg = (d->mii_ctrl & I8255X_MDI_REG_MASK) >> I8255X_MDI_REG_SHIFT;

#if DEBUG_MII_REGS
   EEPRO_LOG(d,"MII PHY read %d reg %d\n",mii_phy,mii_reg);
#endif

   switch(mii_reg) {
      case 0x00:
         return((d->mii_regs[mii_phy][mii_reg] & ~0x8200) | 0x2000);
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
         return(d->mii_regs[mii_phy][mii_reg]);
   }
}

/* Write a MII register */
static void mii_reg_write(struct i8255x_data *d)
{
   u_int mii_phy,mii_reg,mii_data;
   
   mii_phy = (d->mii_ctrl & I8255X_MDI_PHY_MASK) >> I8255X_MDI_PHY_SHIFT;
   mii_reg = (d->mii_ctrl & I8255X_MDI_REG_MASK) >> I8255X_MDI_REG_SHIFT;
   mii_data = d->mii_ctrl & I8255X_MDI_DATA_MASK;

#if DEBUG_MII_REGS
   EEPRO_LOG(d,"MII PHY write %d reg %d value %04x\n",
             mii_phy,mii_reg,mii_data);
#endif

   d->mii_regs[mii_phy][mii_reg] = mii_data;
}

/* Update interrupt status */
static void dev_i8255x_update_irq_status(struct i8255x_data *d)
{
   /* If interrupts are masked, clear IRQ */
   if (d->scb_ic & SCB_CMD_M) {
      pci_dev_clear_irq(d->vm,d->pci_dev);
      return;
   }

   /* Software generated interrupt ? */
   if (d->scb_ic & SCB_CMD_SI) {
      pci_dev_trigger_irq(d->vm,d->pci_dev);
      return;
   }

   /* Hardware interrupt ? */
   if (d->scb_stat_ack & (~d->scb_ic & SCB_INT_MASK))
      pci_dev_trigger_irq(d->vm,d->pci_dev);
   else
      pci_dev_clear_irq(d->vm,d->pci_dev);
}

/* Fetch a CB (Command Block) */
static void dev_i8255x_fetch_cb(struct i8255x_data *d,m_uint32_t addr,
                                struct i8255x_cu_action *action)
{
   physmem_copy_from_vm(d->vm,action,addr,sizeof(*action));
   action->ctrl        = vmtoh32(action->ctrl);
   action->link_offset = vmtoh32(action->link_offset);
   action->txbd_addr   = vmtoh32(action->txbd_addr);
   action->txbd_count  = vmtoh32(action->txbd_count);
}

/* Fetch a TX buffer descriptor */
static void dev_i8255x_fetch_txbd(struct i8255x_data *d,m_uint32_t addr,
                                  struct i8255x_txbd *bd)
{
   physmem_copy_from_vm(d->vm,bd,addr,sizeof(*bd));
   bd->buf_addr = vmtoh32(bd->buf_addr);
   bd->buf_size = vmtoh32(bd->buf_size);
}

/* Transmit a frame */
static int dev_i8255x_send_tx_pkt(struct i8255x_data *d,m_uint32_t cb_addr,
                                  struct i8255x_cu_action *action)
{
   m_uint32_t i,blk_size,tx_size,txbd_addr,txbd_cnt;
   struct i8255x_txbd txbd;
   m_uint8_t *tx_ptr;
   m_uint32_t norm_len;

   /* === Simplified mode: copy the data directly from the TxCB === */
   if (!(action->ctrl & CB_CTRL_SF)) {
      tx_size = action->txbd_count & TXCB_BLK_SIZE;

      norm_len = normalize_size(tx_size,4,0);
      physmem_copy_from_vm(d->vm,d->tx_buffer,cb_addr+0x10,norm_len);
      mem_bswap32(d->tx_buffer,norm_len);
      goto do_transmit;
   }
   
   /* === Flexible mode === */
   tx_ptr  = d->tx_buffer;
   tx_size = 0;

   if (action->txbd_addr == 0xFFFFFFFF) {
      txbd_addr = cb_addr + 0x10;
   } else {
      /* copy the data directly from the TxCB if present */
      blk_size = action->txbd_count & TXCB_BLK_SIZE;

      if (blk_size > 0) {
         tx_size = action->txbd_count & TXCB_BLK_SIZE;

         norm_len = normalize_size(tx_size,4,0);
         physmem_copy_from_vm(d->vm,tx_ptr,cb_addr+0x10,norm_len);
         mem_bswap32(tx_ptr,norm_len);

         tx_ptr += tx_size;
      }

      txbd_addr = action->txbd_addr;
   }
   
   txbd_cnt = (action->txbd_count & TXCB_NUM_MASK) >> TXCB_NUM_SHIFT;

   /* 
    * Fetch all Tx buffer descriptors and copy data from each separate buffer.
    */
   for(i=0;i<txbd_cnt;i++) {
      dev_i8255x_fetch_txbd(d,txbd_addr,&txbd);

      norm_len = normalize_size(txbd.buf_size,4,0);
      physmem_copy_from_vm(d->vm,tx_ptr,txbd.buf_addr,norm_len);
      mem_bswap32(tx_ptr,norm_len);

      tx_ptr  += txbd.buf_size;
      tx_size += txbd.buf_size;

      txbd_addr += sizeof(txbd);
   }

 do_transmit:
   d->stat_counters[STAT_CNT_TX_GOOD]++;

#if DEBUG_TRANSMIT
   EEPRO_LOG(d,"sending packet of %u bytes\n",tx_size);
   mem_dump(log_file,d->tx_buffer,tx_size);
#endif
   netio_send(d->nio,d->tx_buffer,tx_size);
   return(TRUE);
}

/* Process an indidual CB (Command Block) */
static void dev_i8255x_process_cb(struct i8255x_data *d,m_uint32_t cb_addr,
                                  struct i8255x_cu_action *action)
{
   m_uint32_t tmp[2];
   u_int cmd,res;

   cmd = (action->ctrl & CB_CTRL_CMD_MASK) >> CB_CTRL_CMD_SHIFT;

   switch(cmd) {
      /* No Operation */
      case CB_CMD_NOP:
         res = TRUE;
         break;
    
      /* Transmit a frame */
      case CB_CMD_TRANSMIT:
         res = dev_i8255x_send_tx_pkt(d,cb_addr,action);
         break;

      /* Configure */
      case CB_CMD_CONFIGURE:
         physmem_copy_from_vm(d->vm,d->config_data,cb_addr+0x08,
                              I8255X_CONFIG_SIZE * sizeof(m_uint32_t));
         mem_bswap32(d->config_data,I8255X_CONFIG_SIZE * sizeof(m_uint32_t));
         res = TRUE;
         break;

      /* Individual address setup */
      case CB_CMD_IADDR_SETUP:
         tmp[0] = physmem_copy_u32_from_vm(d->vm,cb_addr+0x08);
         tmp[1] = physmem_copy_u32_from_vm(d->vm,cb_addr+0x0c);

         d->iaddr.eth_addr_byte[0] = tmp[0];
         d->iaddr.eth_addr_byte[1] = tmp[0] >> 8;
         d->iaddr.eth_addr_byte[2] = tmp[0] >> 16;
         d->iaddr.eth_addr_byte[3] = tmp[0] >> 24;
         d->iaddr.eth_addr_byte[4] = tmp[1];
         d->iaddr.eth_addr_byte[5] = tmp[1] >> 8;

         EEPRO_LOG(d,"iaddr set to: %2.2x%2.2x.%2.2x%2.2x.%2.2x%2.2x\n",
                   d->iaddr.eth_addr_byte[0],d->iaddr.eth_addr_byte[1],
                   d->iaddr.eth_addr_byte[2],d->iaddr.eth_addr_byte[3],
                   d->iaddr.eth_addr_byte[4],d->iaddr.eth_addr_byte[5]);
                  
         res = TRUE;
         break;

      /* Load Microcode */
      case CB_CMD_LOAD_UCODE:
         physmem_copy_from_vm(d->vm,d->microcode,cb_addr+0x08,
                              I8255X_UCODE_SIZE * sizeof(m_uint32_t));
         mem_bswap32(d->microcode,I8255X_UCODE_SIZE * sizeof(m_uint32_t));
         EEPRO_LOG(d,"microcode loaded\n");
         res = TRUE;
         break;

      /* Unsupported command */
      default:
         EEPRO_LOG(d,"unsupported CB command 0x%2.2x (cb_addr=0x%8.8x)\n",
                   cmd,cb_addr);
         res = TRUE;
   }

   /* Set the completed bit with the result */
   action->ctrl |= CB_CTRL_C;
   if (res) action->ctrl |= CB_CTRL_OK;

   /* Update control word */
   physmem_copy_u32_to_vm(d->vm,cb_addr,action->ctrl);
}

/* Process a CBL (Command Block List) */
static void dev_i8255x_process_cbl(struct i8255x_data *d)
{
   struct i8255x_cu_action action;
   m_uint32_t cb_addr;

   for(;;) {
      cb_addr = d->cu_base + d->cu_offset;
      dev_i8255x_fetch_cb(d,cb_addr,&action);

      /* Execute command */
      dev_i8255x_process_cb(d,cb_addr,&action);

      /* Interrupt at end of execution ? */
      if (action.ctrl & CB_CTRL_I)
         d->scb_stat_ack |= SCB_STAT_CX;

      /* Return to idle state ? */
      if (action.ctrl & CB_CTRL_EL) {
         d->cu_state = CU_STATE_IDLE;
         d->scb_stat_ack |= SCB_STAT_CNA;
         break;
      } else {
         /* Enter suspended state ? */
         if (action.ctrl & CB_CTRL_S) {
            d->cu_state = CU_STATE_SUSPEND;
            d->scb_stat_ack |= SCB_STAT_CNA;
            break;
         }
      }

      /* Go to next descriptor */
      d->cu_offset = action.link_offset;
   }
   
   /* Update interrupt status */
   dev_i8255x_update_irq_status(d);
}

/* Resume a Command Block List */
static int dev_i8255x_cu_resume(struct i8255x_data *d)
{
   struct i8255x_cu_action action;
   m_uint32_t cu_addr;

   /* If we are in idle state, ignore the command */
   if (d->cu_state == CU_STATE_IDLE)
      return(FALSE);

   cu_addr = d->cu_base + d->cu_offset;

   /* Check if the previous block has still the S bit set */
   dev_i8255x_fetch_cb(d,cu_addr,&action);
   
   if (action.ctrl & CB_CTRL_S)
      return(FALSE);
   
   d->cu_offset = action.link_offset;
   d->cu_state  = CU_STATE_LPQ_ACT;
   dev_i8255x_process_cbl(d);
   return(TRUE);
}

/* Dump Statistical counters */
static void dev_i8255x_dump_stat_cnt(struct i8255x_data *d)
{
   m_uint32_t counters[I8255X_STAT_CNT_SIZE];

   memcpy(counters,d->stat_counters,sizeof(counters));
   mem_bswap32(counters,sizeof(counters));
   physmem_copy_to_vm(d->vm,counters,d->stat_cnt_addr,sizeof(counters));
}

/* Process a CU command */
static void dev_i8255x_process_cu_cmd(struct i8255x_data *d,u_int cuc)
{
   switch(cuc) {
      /* No Operation */
      case CU_CMD_NOP:
         break;

      /* Start */
      case CU_CMD_START:
         d->cu_offset = d->scb_gptr;
         d->cu_state  = CU_STATE_LPQ_ACT;
         dev_i8255x_process_cbl(d);
         break;

      /* Resume */
      case CU_CMD_RESUME:
         dev_i8255x_cu_resume(d);
         break;

      /* Load CU base */
      case CU_CMD_LOAD_CU_BASE:
         d->cu_base = d->scb_gptr;
         break;

      /* Load Dump Counters Address */
      case CU_CMD_LOAD_DUMP_CNT:
         d->stat_cnt_addr = d->scb_gptr;
         break;

      /* Dump Statistical Counters */
      case CU_CMD_DUMP_STAT_CNT:
         dev_i8255x_dump_stat_cnt(d);
         break;

      /* Dump Statistical Counters and reset them */
      case CU_CMD_DUMP_RST_STAT_CNT:
         dev_i8255x_dump_stat_cnt(d);
         memset(d->stat_counters,0,sizeof(d->stat_counters));
         break;

      default:
         EEPRO_LOG(d,"unsupported CU command 0x%2.2x\n",cuc);
   }
}

/* Fetch an RxFD (RX Frame Descriptor) */
static void dev_i8255x_fetch_rxfd(struct i8255x_data *d,m_uint32_t addr,
                                  struct i8255x_rxfd *rxfd)
{
   physmem_copy_from_vm(d->vm,rxfd,addr,sizeof(*rxfd));
   rxfd->ctrl        = vmtoh32(rxfd->ctrl);
   rxfd->link_offset = vmtoh32(rxfd->link_offset);
   rxfd->rxbd_addr   = vmtoh32(rxfd->rxbd_addr);
   rxfd->rxbd_size   = vmtoh32(rxfd->rxbd_size);
}

/* Fetch an RxBD (Rx Buffer Descriptor) */
static void dev_i8255x_fetch_rxbd(struct i8255x_data *d,m_uint32_t addr,
                                  struct i8255x_rxbd *rxbd)
{
   physmem_copy_from_vm(d->vm,rxbd,addr,sizeof(*rxbd));
   rxbd->ctrl        = vmtoh32(rxbd->ctrl);
   rxbd->rxbd_next   = vmtoh32(rxbd->rxbd_next);
   rxbd->buf_addr    = vmtoh32(rxbd->buf_addr);
   rxbd->buf_size    = vmtoh32(rxbd->buf_size);
}

/* Store a packet */
static int dev_i8255x_store_rx_pkt(struct i8255x_data *d,
                                   m_uint8_t *pkt,ssize_t pkt_len)
{
   m_uint32_t rxfd_addr,rxbd_addr;
   m_uint32_t rxfd_next,rxbd_next;
   m_uint32_t clen,buf_size,norm_len;
   struct i8255x_rxfd rxfd;
   struct i8255x_rxbd rxbd;
   m_uint8_t *pkt_ptr;
   ssize_t tot_len;

   /* Fetch the RX Frame descriptor */
   rxfd_addr = d->ru_base + d->ru_offset;
   dev_i8255x_fetch_rxfd(d,rxfd_addr,&rxfd);

   /* === Simplified mode === */
   if (!(rxfd.ctrl & RXFD_CTRL_SF)) {
      /* Copy the packet data directly into the frame descriptor */
      norm_len = normalize_size(pkt_len,4,0);
      mem_bswap32(pkt,norm_len);
      physmem_copy_to_vm(d->vm,pkt,rxfd_addr+0x10,norm_len);

      /* Update the RxFD and generate the appropriate interrupt */
      goto update_rxfd;
   }

   /* === Flexible mode === */
   rxbd_addr = d->ru_base + rxfd.rxbd_addr;
   pkt_ptr = pkt;
   tot_len = pkt_len;

   do {
      /* Fetch the RX buffer */
      dev_i8255x_fetch_rxbd(d,rxbd_addr,&rxbd);
      rxbd_next = rxbd.rxbd_next;

      /* Get the current buffer size */
      buf_size = rxbd.buf_size & RXFD_SIZE_MASK;
      clen = m_min(tot_len,buf_size);
      
      /* Copy the data into the buffer */
      norm_len = normalize_size(clen,4,0);
      mem_bswap32(pkt_ptr,norm_len);
      physmem_copy_to_vm(d->vm,pkt_ptr,rxbd.buf_addr,norm_len);

      pkt_ptr += clen;
      tot_len -= clen;

      /* Update RX buffer info */
      if (!tot_len) {
         rxbd.ctrl |= RXBD_CTRL_EOF;
         clen += 4;  /* Add CRC */
      }
      
      rxbd.ctrl |= RXBD_CTRL_F | clen;
      physmem_copy_u32_to_vm(d->vm,rxbd_addr+0x00,rxbd.ctrl);
   }while(tot_len > 0);

   /* Set the next available RxBD in next RxFD */
   rxbd_next = d->ru_base + rxbd.rxbd_next;
   rxfd_next = d->ru_base + rxfd.link_offset;
   physmem_copy_u32_to_vm(d->vm,rxfd_next+0x08,rxbd_next);

   /* Update the RxFD */
 update_rxfd:
   rxfd.ctrl |= RXFD_CTRL_C | RXFD_CTRL_OK;
   rxfd.rxbd_size &= ~0xFFFF;
   rxfd.rxbd_size |= RXFD_EOF | (pkt_len + 4);
   
   physmem_copy_u32_to_vm(d->vm,rxfd_addr+0x00,rxfd.ctrl);
   physmem_copy_u32_to_vm(d->vm,rxfd_addr+0x0c,rxfd.rxbd_size);

   d->stat_counters[STAT_CNT_RX_GOOD]++;

   /* A frame has been received: generate an IRQ */
   d->scb_stat_ack |= SCB_STAT_FR;

   if (rxfd.ctrl & RXFD_CTRL_EL) {
      d->ru_state = RU_STATE_NO_RES;
      d->scb_stat_ack |= SCB_STAT_RNR;
   } else {
      if (rxfd.ctrl & RXFD_CTRL_S) {
         d->ru_state = RU_STATE_SUSPEND;
         d->scb_stat_ack |= SCB_STAT_RNR;
      } else {
         d->ru_offset = rxfd.link_offset;
      }
   }

   dev_i8255x_update_irq_status(d);
   return(TRUE);
}

/* Resume reception */
static int dev_i8255x_ru_resume(struct i8255x_data *d)
{
   struct i8255x_rxfd rxfd;
   m_uint32_t rxfd_addr;

   /* If we are not in ready state, ignore the command */
   if (d->ru_state != RU_STATE_READY)
      return(FALSE);

   /* Fetch the RX Frame descriptor */
   rxfd_addr = d->ru_base + d->ru_offset;
   dev_i8255x_fetch_rxfd(d,rxfd_addr,&rxfd);

   /* Check if the previous frame descriptor has still the S bit set */
   if (rxfd.ctrl & RXFD_CTRL_S)
      return(FALSE);
   
   d->ru_offset = rxfd.link_offset;
   d->ru_state  = RU_STATE_READY;
   return(TRUE);
}

/* Process a RU command */
static void dev_i8255x_process_ru_cmd(struct i8255x_data *d,u_int ruc)
{
   switch(ruc) {
      /* No Operation */
      case RU_CMD_NOP:
         break;

      /* Start */
      case RU_CMD_START:
         d->ru_offset = d->scb_gptr;
         d->ru_state  = RU_STATE_READY;
         break;

      /* Resume */
      case RU_CMD_RESUME:
         dev_i8255x_ru_resume(d);
         break;

      /* Load RU base */
      case RU_CMD_LOAD_RU_BASE:         
         d->ru_base = d->scb_gptr;
         break;
         
      default:
         EEPRO_LOG(d,"unsupported RU command 0x%2.2x\n",ruc);
   }
}

/*
 * dev_i8255x_access()
 */
void *dev_i8255x_access(cpu_gen_t *cpu,struct vdevice *dev,
                        m_uint32_t offset,u_int op_size,u_int op_type,
                        m_uint64_t *data)
{
   struct i8255x_data *d = dev->priv_data;
   u_int cuc,ruc,mii_op;

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

   EEPRO_LOCK(d);

   switch(offset) {
      /* SCB Command Word (interrupt control byte) */
      case 0x00:
         if (op_type == MTS_WRITE)
            d->scb_ic = *data;
         break;

      /* SCB Command Word (command byte) */
      case 0x01:
         if (op_type == MTS_WRITE) {
            cuc = (*data & SCB_CMD_CUC_MASK) >> SCB_CMD_CUC_SHIFT;
            ruc = (*data & SCB_CMD_RUC_MASK) >> SCB_CMD_RUC_SHIFT;

            /* Process CU and RU commands */
            dev_i8255x_process_cu_cmd(d,cuc);
            dev_i8255x_process_ru_cmd(d,ruc);
         }
         break;

      /* SCB Status Word */
      case 0x02:
         if (op_type == MTS_READ) {
            *data  = d->scb_stat_ack << 8;
         } else {
            d->scb_stat_ack &= ~(*data >> 8);
            dev_i8255x_update_irq_status(d);
         }
         break;

      /* SCB General Pointer */
      case 0x04:
         if (op_type == MTS_WRITE)
            d->scb_gptr = *data;
         else
            *data = d->scb_gptr;
         break;

      /* MDI control register */
      case 0x10:
         if (op_type == MTS_READ) {
            mii_op = (d->mii_ctrl & I8255X_MDI_OP_MASK) >> I8255X_MDI_OP_SHIFT;
            
            if (mii_op == MII_OPCODE_READ) {
               d->mii_ctrl &= ~I8255X_MDI_DATA_MASK;
               d->mii_ctrl |= mii_reg_read(d);
            }

            *data = d->mii_ctrl | I8255X_MDI_R;
         } else {
            d->mii_ctrl = *data;

            mii_op = (d->mii_ctrl & I8255X_MDI_OP_MASK) >> I8255X_MDI_OP_SHIFT;
            if (mii_op == MII_OPCODE_WRITE)
               mii_reg_write(d);
         }
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

   EEPRO_UNLOCK(d);
   return NULL;
}

/* Handle the RX ring */
static int dev_i8255x_handle_rxring(netio_desc_t *nio,
                                    u_char *pkt,ssize_t pkt_len,
                                    struct i8255x_data *d)
{  
   int res = FALSE;
 
   EEPRO_LOCK(d);

   if (d->ru_state == RU_STATE_READY)
      res = dev_i8255x_store_rx_pkt(d,pkt,pkt_len);
   
   EEPRO_UNLOCK(d);
   return(res);
}

/*
 * pci_i8255x_read()
 *
 * Read a PCI register.
 */
static m_uint32_t pci_i8255x_read(cpu_gen_t *cpu,struct pci_device *dev,
                                  int reg)
{
   struct i8255x_data *d = dev->priv_data;

#if DEBUG_PCI_REGS
   EEPRO_LOG(d,"read PCI register 0x%x\n",reg);
#endif

   switch (reg) {
      case 0x00:
         return((I8255X_PCI_PRODUCT_ID << 16) | I8255X_PCI_VENDOR_ID);
      case PCI_REG_BAR0:
         return(d->dev->phys_addr);
      case 0x0c:
         return(0x4000);
      default:
         return(0);
   }
}

/*
 * pci_i8255x_write()
 *
 * Write a PCI register.
 */
static void pci_i8255x_write(cpu_gen_t *cpu,struct pci_device *dev,
                             int reg,m_uint32_t value)
{
   struct i8255x_data *d = dev->priv_data;

#if DEBUG_PCI_REGS
   EEPRO_LOG(d,"write PCI register 0x%x, value 0x%x\n",reg,value);
#endif

   switch(reg) {
      case PCI_REG_BAR0:
         vm_map_device(cpu->vm,d->dev,(m_uint64_t)value);
         EEPRO_LOG(d,"registers are mapped at 0x%x\n",value);
         break;
   }
}

/*
 * dev_i8255x_init()
 */
struct i8255x_data *
dev_i8255x_init(vm_instance_t *vm,char *name,int interface_type,
                struct pci_bus *pci_bus,int pci_device,int irq)
{
   struct i8255x_data *d;
   struct pci_device *pci_dev;
   struct vdevice *dev;

   /* Allocate the private data structure for I8255X */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"%s (i8255x): out of memory\n",name);
      return NULL;
   }

   memset(d,0,sizeof(*d));
   pthread_mutex_init(&d->lock,NULL);

   /* Add as PCI device */
   pci_dev = pci_dev_add(pci_bus,name,
                         I8255X_PCI_VENDOR_ID,I8255X_PCI_PRODUCT_ID,
                         pci_device,0,irq,
                         d,NULL,pci_i8255x_read,pci_i8255x_write);

   if (!pci_dev) {
      fprintf(stderr,"%s (i8255x): unable to create PCI device.\n",name);
      goto err_pci_dev;
   }

   /* Create the device itself */
   if (!(dev = dev_create(name))) {
      fprintf(stderr,"%s (i8255x): unable to create device.\n",name);
      goto err_dev;
   }

   d->name     = name;
   d->vm       = vm;
   d->pci_dev  = pci_dev;
   d->dev      = dev;

   dev->phys_addr = 0;
   dev->phys_len  = 0x10000;
   dev->handler   = dev_i8255x_access;
   dev->priv_data = d;
   return(d);

 err_dev:
   pci_dev_remove(pci_dev);
 err_pci_dev:
   free(d);
   return NULL;
}

/* Remove an Intel i8255x device */
void dev_i8255x_remove(struct i8255x_data *d)
{
   if (d != NULL) {
      pci_dev_remove(d->pci_dev);
      vm_unbind_device(d->vm,d->dev);
      cpu_group_rebuild_mts(d->vm->cpu_group);
      free(d->dev);
      free(d);
   }
}

/* Bind a NIO to an Intel i8255x device */
int dev_i8255x_set_nio(struct i8255x_data *d,netio_desc_t *nio)
{
   /* check that a NIO is not already bound */
   if (d->nio != NULL)
      return(-1);

   d->nio = nio;
   netio_rxl_add(nio,(netio_rx_handler_t)dev_i8255x_handle_rxring,d,NULL);
   return(0);
}

/* Unbind a NIO from an Intel i8255x device */
void dev_i8255x_unset_nio(struct i8255x_data *d)
{
   if (d->nio != NULL) {
      netio_rxl_remove(d->nio);
      d->nio = NULL;
   }
}
