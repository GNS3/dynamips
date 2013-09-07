/*  
 * Cisco router Simulation Platform.
 * Copyright (C) 2005-2006 Christophe Fillot.  All rights reserved.
 *
 * EEPROM types:
 *   - 0x3d: PA-4B
 *   - 0x3e: PA-8B
 *
 * Vernon Missouri offered a PA-4B.
 *
 * It is based on the Munich32 chip:
 *   http://www.infineon.com//upload/Document/cmc_upload/migrated_files/document_files/Datasheet/m32_34m.pdf
 * 
 * There is also one TP3420A per BRI port.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_c7200.h"

/* Debugging flags */
#define DEBUG_ACCESS    0
#define DEBUG_TRANSMIT  0
#define DEBUG_RECEIVE   0

/* PCI vendor/product codes */
#define BRI_PCI_VENDOR_ID    0x10ee
#define BRI_PCI_PRODUCT_ID   0x4013

/* Memory used by the munich32 chip */
#define MUNICH32_MEM_SIZE    0x40000

/* Maximum packet size */
#define M32_MAX_PKT_SIZE  8192

/* 32 timeslots and 32 channels for a Munich32 chip */
#define M32_NR_TIMESLOTS  32
#define M32_NR_CHANNELS   32

/* Offsets */
#define M32_OFFSET_TS     0x0c   /* Timeslots */
#define M32_OFFSET_CHAN   0x8c   /* Channel specification */
#define M32_OFFSET_CRDA   0x28c  /* Current RX descriptor address */
#define M32_OFFSET_CTDA   0x30c  /* Current TX descriptor address */

/* Action Specification */
#define M32_AS_PCM_MASK     0xE0000000   /* PCM Highway Format */
#define M32_AS_PCM_SHIFT    29
#define M32_AS_MFL_MASK     0x1FFF0000   /* Maximum Frame Length */
#define M32_AS_MFL_SHIFT    16
#define M32_AS_IN           0x00008000   /* Initialization Procedure */
#define M32_AS_ICO          0x00004000   /* Initialize Channel Only */
#define M32_AS_CHAN_MASK    0x00001F00   /* Channel Number */
#define M32_AS_CHAN_SHIFT   8
#define M32_AS_IM           0x00000080   /* Interrupt Mask */
#define M32_AS_RES          0x00000040   /* Reset */
#define M32_AS_LOOPS_MASK   0x00000038   /* Loops (LOC,LOOP,LOOPI) */
#define M32_AS_LOOPS_SHIFT  3
#define M32_AS_IA           0x00000004   /* Interrupt Attention */

/* Interrupt Information */
#define M32_II_INT    0x80000000   /* Interrupt */
#define M32_II_VN3    0x20000000   /* Silicon version number */
#define M32_II_VN2    0x10000000
#define M32_II_VN1    0x08000000
#define M32_II_FRC    0x04000000   /* Framing bits changed */
#define M32_II_ARACK  0x00008000   /* Action Request Acknowledge */
#define M32_II_ARF    0x00004000   /* Action Request Failed */
#define M32_II_HI     0x00002000   /* Host Initiated Interrupt */
#define M32_II_FI     0x00001000   /* Frame Indication */
#define M32_II_IFC    0x00000800   /* Idle Flag Change */
#define M32_II_SF     0x00000400   /* Short Frame */
#define M32_II_ERR    0x00000200   /* Error condition */
#define M32_II_FO     0x00000100   /* Overflow/Underflow */
#define M32_II_RT     0x00000020   /* Direction (Transmit/Receive Int) */

/* Timeslot Assignment */
#define M32_TS_TTI         0x20000000   /* Transmit Timeslot Inhibit */
#define M32_TS_TCN_MASK    0x1F000000   /* Transmit Channel Number Mask */
#define M32_TS_TCN_SHIFT   24
#define M32_TS_TFM_MASK    0x00FF0000   /* Transmit Fill Mask */
#define M32_TS_TFM_SHIFT   16
#define M32_TS_RTI         0x00002000   /* Receive Timeslot Inhibit */
#define M32_TS_RCN_MASK    0x00001F00   /* Receive Channel Number Mask */
#define M32_TS_RCN_SHIFT   8
#define M32_TS_RFM_MASK    0x000000FF   /* Receive Fill Mask */
#define M32_TS_RFM_SHIFT   0

/* Transmit Descriptor */
#define M32_TXDESC_FE        0x80000000   /* Frame End */
#define M32_TXDESC_HOLD      0x40000000   /* Hold=0: usable by Munich */
#define M32_TXDESC_HI        0x20000000   /* Host Initiated Interrupt */
#define M32_TXDESC_NO_MASK   0x1FFF0000   /* Number of bytes */
#define M32_TXDESC_NO_SHIFT  16
#define M32_TXDESC_V110      0x00008000   /* V.110/X.30 frame */
#define M32_TXDESC_CSM       0x00000800   /* CRC Select per Message */
#define M32_TXDESC_FNUM      0x000001FF   /* Inter-Frame Time-Fill chars */

/* Munich32 TX descriptor */
struct m32_tx_desc {
   m_uint32_t params;    /* Size + Flags */
   m_uint32_t tdp;       /* Transmit Data Pointer */
   m_uint32_t ntdp;      /* Next Transmit Descriptor Pointer */
};

/* Receive Descriptor (parameters) */
#define M32_RXDESC_HOLD      0x40000000   /* Hold */
#define M32_RXDESC_HI        0x20000000   /* Host Initiated Interrupt */
#define M32_RXDESC_NO_MASK   0x1FFF0000   /* Size of receive data section */
#define M32_RXDESC_NO_SHIFT  16

/* Receive Descriptor (status) */
#define M32_RXDESC_FE        0x80000000   /* Frame End */
#define M32_RXDESC_C         0x40000000
#define M32_RXDESC_BNO_MASK  0x1FFF0000   /* Bytes stored in data section */
#define M32_RXDESC_BNO_SHIFT 16
#define M32_RXDESC_SF        0x00004000
#define M32_RXDESC_LOSS      0x00002000   /* Error in sync pattern */
#define M32_RXDESC_CRCO      0x00001000   /* CRC error */
#define M32_RXDESC_NOB       0x00000800   /* Bit content not divisible by 8 */
#define M32_RXDESC_LFD       0x00000400   /* Long Frame Detected */ 
#define M32_RXDESC_RA        0x00000200   /* Receive Abort */
#define M32_RXDESC_ROF       0x00000100   /* Overflow of internal buffer */

/* Munich32 RX descriptor */
struct m32_rx_desc {
   m_uint32_t params;    /* RX parameters (hold, hi, ...) */
   m_uint32_t status;    /* Status */
   m_uint32_t rdp;       /* Receive Data Pointer */
   m_uint32_t nrdp;      /* Next Receive Descriptor Pointer */
};

/* Munich32 channel */
struct m32_channel {
   m_uint32_t status;
   m_uint32_t frda;
   m_uint32_t ftda;
   m_uint32_t itbs;

   /* Physical addresses of current RX and TX descriptors */
   m_uint32_t rx_current,tx_current;

   /* Poll mode */
   u_int poll_mode;
};

/* Munich32 chip data */
struct m32_data {
   /* Virtual machine */
   vm_instance_t *vm;

   /* TX ring scanner task id */
   ptask_id_t tx_tid;

   /* Interrupt Queue */
   m_uint32_t iq_base_addr;
   m_uint32_t iq_cur_addr;
   u_int iq_size;

   /* Timeslots */
   m_uint32_t timeslots[M32_NR_TIMESLOTS];

   /* Channels */
   struct m32_channel channels[M32_NR_CHANNELS];
   
   /* Embedded config memory */
   m_uint32_t cfg_mem[MUNICH32_MEM_SIZE/4];
};

/* === TP3420 SID === */

/* Activation / Desactivation */
#define TP3420_SID_NOP    0xFF  /* No Operation */
#define TP3420_SID_PDN    0x00  /* Power Down */
#define TP3420_SID_PUP    0x20  /* Power Up */
#define TP3420_SID_DR     0x01  /* Deactivation Request */
#define TP3420_SID_FI2    0x02  /* Force Info 2 (NT Only) */
#define TP3420_SID_MMA    0x1F  /* Monitor Mode Activation */

/* Device Modes */
#define TP3420_SID_NTA    0x04  /* NT Mode, Adaptive Sampling */
#define TP3420_SID_NTF    0x05  /* NT Mode, Fixed Sampling */
#define TP3420_SID_TES    0x06  /* TE Mode, Digital System Interface Slave */
#define TP3420_SID_TEM    0x07  /* TE Mode, Digital System Interface Master */

/* Digital Interface Formats */
#define TP3420_SID_DIF1   0x08  /* Digital System Interface Format 1 */
#define TP3420_SID_DIF2   0x09  /* Digital System Interface Format 2 */
#define TP3420_SID_DIF3   0x0A  /* Digital System Interface Format 3 */
#define TP3420_SID_DIF4   0x0B  /* Digital System Interface Format 4 */

/* BCLK Frequency Settings */
#define TP3420_SID_BCLK1  0x98  /* Set BCLK to 2.048 Mhz */
#define TP3420_SID_BCLK2  0x99  /* Set BCLK to 256 Khz */
#define TP3420_SID_BCLK3  0x9A  /* Set BCLK to 512 Khz */
#define TP3420_SID_BCLK4  0x9B  /* Set BCLK to 2.56 Mhz */

/* B Channel Exchange */
#define TP3420_SID_BDIR   0x0C  /* B Channels Mapped Direct (B1->B1,B2->B2) */
#define TP3420_SID_BEX    0x0D  /* B Channels Exchanged (B1->B2,B2->B1) */

/* D Channel Access */
#define TP3420_SID_DREQ1  0x0E  /* D Channel Request, Class 1 Message */
#define TP3420_SID_DREQ2  0x0F  /* D Channel Request, Class 2 Message */

/* D Channel Access Control */
#define TP3420_SID_DACCE  0x90  /* Enable D-Channel Access Mechanism */
#define TP3420_SID_DACCD  0x91  /* Disable D-Channel Access Mechanism */
#define TP3420_SID_EBIT0  0x96  /* Force Echo Bit to 0 */
#define TP3420_SID_EBITI  0x97  /* Force Echo Bit to Inverted Received D bit */
#define TP3420_SID_EBITN  0x9C  /* Reset EBITI and EBIT0 to Normal Condition */
#define TP3420_SID_DCKE   0xF1  /* D Channel Clock Enable */

/* End Of Message (EOM) Interrupt */
#define TP3420_SID_EIE    0x10  /* EOM Interrupt Enabled */
#define TP3420_SID_EID    0x11  /* EOM Interrupt Disabled */

/* B1 Channel Enable/Disable */
#define TP3420_SID_B1E    0x14  /* B1 Channel Enabled */
#define TP3420_SID_B1D    0x15  /* B1 Channel Disabled */

/* B2 Channel Enable/Disable */
#define TP3420_SID_B2E    0x16  /* B2 Channel Enabled */
#define TP3420_SID_B2D    0x17  /* B2 Channel Disabled */

/* Loopback Tests Modes */
#define TP3420_SID_CAL    0x1B  /* Clear All Loopbacks */

/* Control Device State Reading */
#define TP3420_SID_ENST   0x92  /* Enable the Device State Output on NOCST */
#define TP3420_SID_DISST  0x93  /* Disable the Device State Output on NOCST */

/* PIN Signal Selection */
#define TP3420_SID_PINDEF 0xE0  /* Redefine PIN signals */

/* TP3420 Status Register */
#define TP3420_SR_LSD     0x02  /* Line Signal Detected Far-End */
#define TP3420_SR_AP      0x03  /* Activation Pending */
#define TP3420_SR_AI      0x0C  /* Activation Indication */
#define TP3420_SR_EI      0x0E  /* Error Indication */
#define TP3420_SR_DI      0x0F  /* Deactivation Indication */
#define TP3420_SR_EOM     0x06  /* End of D-channel TX message */
#define TP3420_SR_CON     0x07  /* Lost Contention for D channel */

/* NO Change Return status */
#define TP3420_SR_NOC     0x00  /* NOC Status after DISST command */
#define TP3420_SR_NOCST   0x80  /* NOC Status after ENST command */

/* BRI Channel Index */
#define BRI_CHAN_INDEX_B1  0
#define BRI_CHAN_INDEX_B2  1
#define BRI_CHAN_INDEX_D   2

/* PA-4B Data */
struct pa_4b_data {
   char *name;

   /* Virtual machine */
   vm_instance_t *vm;

   /* Virtual device */
   struct vdevice *dev;

   /* PCI device information */
   struct pci_device *pci_dev;

   /* NetIO descriptor */
   netio_desc_t *nio;

   /* Munich32 data and base offset */
   struct m32_data m32_data;
   u_int m32_offset;
};

/* Log a PA-4B/PA-8B message */
#define BRI_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* Read a configuration word */
static inline m_uint32_t m32_get_cfgw(struct m32_data *d,m_uint32_t offset)
{
   return(d->cfg_mem[offset >> 2]);
}

/* Write a configuration word */
static inline void m32_set_cfgw(struct m32_data *d,m_uint32_t offset,
                                m_uint32_t val)
{
   d->cfg_mem[offset >> 2] = val;
}

/* Post an interrupt into the interrupt queue */
static int m32_post_interrupt(struct m32_data *d,m_uint32_t iq_value)
{
   if (!d->iq_base_addr)
      return(-1);
   
   /* The INT bit is mandatory */
   iq_value |= M32_II_INT;

#if 0
   printf("M32: Posting interrupt iq_val=0x%8.8x at 0x%8.8x\n",
          iq_value,d->iq_cur_addr);
#endif

   physmem_copy_u32_to_vm(d->vm,d->iq_cur_addr,iq_value);
   d->iq_cur_addr += sizeof(m_uint32_t);

   if (d->iq_cur_addr >= (d->iq_base_addr + d->iq_size))
      d->iq_cur_addr = d->iq_base_addr;

   return(0);
}

/* Fetch a timeslot assignment */
__unused static int m32_fetch_ts_assign(struct m32_data *d,u_int ts_id)
{
   m_uint32_t offset;

   offset = M32_OFFSET_TS + (ts_id * sizeof(m_uint32_t));
   d->timeslots[ts_id] = m32_get_cfgw(d,offset);
   return(0);
}

/* Fetch all timeslot assignments */
static int m32_fetch_all_ts(struct m32_data *d)
{
   m_uint32_t offset = M32_OFFSET_TS;
   u_int i;

   for(i=0;i<M32_NR_TIMESLOTS;i++,offset+=sizeof(m_uint32_t))
      d->timeslots[i] = m32_get_cfgw(d,offset);

   return(0);
}

/* Show timeslots assignments (debugging) */
__maybe_unused static void m32_show_ts_assign(struct m32_data *d)
{
   m_uint32_t ts;
   u_int i;

   printf("MUNICH32 timeslots:\n");

   for(i=0;i<M32_NR_TIMESLOTS;i++) {
      ts = d->timeslots[i];

      if ((ts & (M32_TS_TTI|M32_TS_RTI)) != (M32_TS_TTI|M32_TS_RTI)) {
         printf("  Timeslot %2u: ",i);

         if (!(ts & M32_TS_TTI)) {
            printf("TCN=%2u TFM=0x%2.2x  ",
                   (ts & M32_TS_TCN_MASK) >> M32_TS_TCN_SHIFT,
                   (ts & M32_TS_TFM_MASK) >> M32_TS_TFM_SHIFT);
         }

         if (!(ts & M32_TS_RTI)) {
            printf("RCN=%2u RFM=0x%2.2x",
                   (ts & M32_TS_RCN_MASK) >> M32_TS_RCN_SHIFT,
                   (ts & M32_TS_RFM_MASK) >> M32_TS_RFM_SHIFT);
         }

         printf("\n");
      }
   }

   printf("\n");
}

/* Show info about a channels (debugging) */
static void m32_show_channel(struct m32_data *d,u_int chan_id)
{
   struct m32_channel *chan;

   chan = &d->channels[chan_id];
   printf("M32 Channel %u:\n",chan_id);
   printf("   Status : 0x%8.8x\n",chan->status);
   printf("   FRDA   : 0x%8.8x\n",chan->frda);
   printf("   FTDA   : 0x%8.8x\n",chan->ftda);
   printf("   ITBS   : 0x%8.8x\n",chan->itbs);
}

/* Fetch a channel specification */
static int m32_fetch_chan_spec(struct m32_data *d,u_int chan_id)
{
   struct m32_channel *chan;
   m_uint32_t offset;

   offset = M32_OFFSET_CHAN + (chan_id * 4 * sizeof(m_uint32_t));
   chan = &d->channels[chan_id];

   chan->status = m32_get_cfgw(d,offset);
   chan->frda   = m32_get_cfgw(d,offset+4);
   chan->ftda   = m32_get_cfgw(d,offset+8);
   chan->itbs   = m32_get_cfgw(d,offset+12);

   chan->poll_mode = 0;
   chan->rx_current = chan->frda;
   chan->tx_current = chan->ftda;

   m32_set_cfgw(d,M32_OFFSET_CTDA+(chan_id*4),chan->tx_current);

#if 1
   if (chan_id == 2) {
      printf("M32: Fetched channel %u\n",chan_id);
      //m32_show_ts_assign(d);
      m32_show_channel(d,chan_id);
   }
#endif
   return(0);
}

/* Fetch all channel specifications */
static void m32_fetch_all_chan_spec(struct m32_data *d)
{
   u_int i;

   for(i=0;i<M32_NR_CHANNELS;i++)
      m32_fetch_chan_spec(d,i);
}

/* Try to acquire the specified TX descriptor */
static int m32_tx_acquire(struct m32_data *d,m_uint32_t txd_addr,
                          struct m32_tx_desc *txd)
{
   m_uint32_t params;

   params = physmem_copy_u32_from_vm(d->vm,txd_addr);
   if (!(params & M32_TXDESC_HOLD))
      return(FALSE);

   txd->params = params;
   txd->tdp    = physmem_copy_u32_from_vm(d->vm,txd_addr+4);
   txd->ntdp   = physmem_copy_u32_from_vm(d->vm,txd_addr+8);
   return(TRUE);
}

/* Try to acquire the next TX descriptor */
static int m32_tx_acquire_next(struct m32_data *d,m_uint32_t *txd_addr)
{
   m_uint32_t params;

   /* HOLD bit must be reset */
   params = physmem_copy_u32_from_vm(d->vm,*txd_addr);
   if ((params & M32_TXDESC_HOLD))
      return(FALSE);

   *txd_addr = physmem_copy_u32_from_vm(d->vm,(*txd_addr)+8);
   return(TRUE);
}

/* Scan a channel TX ring */
static inline int m32_tx_scan(struct m32_data *d,u_int chan_id)
{
   struct m32_channel *chan = &d->channels[chan_id];
   m_uint8_t pkt[M32_MAX_PKT_SIZE];
   struct m32_tx_desc txd;
   m_uint32_t pkt_len;

   if (!chan->tx_current)
      return(FALSE);

   switch(chan->poll_mode) {
      case 0:        
         m32_set_cfgw(d,M32_OFFSET_CTDA+(chan_id*4),chan->tx_current);

         /* Try to transmit data */
         if (!m32_tx_acquire(d,chan->tx_current,&txd))
            return(FALSE);

         printf("M32: TX scanner for channel %u (tx_current=0x%8.8x)\n",
                chan_id,chan->tx_current);

         printf("M32: params=0x%8.8x, next=0x%8.8x.\n",txd.params,txd.ntdp);

         /* The descriptor has been acquired */
         pkt_len = (txd.params & M32_TXDESC_NO_MASK) >> M32_TXDESC_NO_SHIFT;
         physmem_copy_from_vm(d->vm,pkt,txd.tdp,pkt_len);

         printf("M32: data_ptr=0x%x, len=%u\n",txd.tdp,pkt_len);
         mem_dump(stdout,pkt,pkt_len);

         /* Poll the next descriptor (wait for HOLD bit to be reset) */
         chan->poll_mode = 1;  

         if (txd.params & M32_TXDESC_FE) {
            m32_post_interrupt(d,M32_II_FI | chan_id);
            vm_set_irq(d->vm,2);
         }

         break;

      case 1:
         if (!m32_tx_acquire_next(d,&chan->tx_current))
            return(FALSE);

         printf("M32: branching on next descriptor 0x%x\n",chan->tx_current);
         chan->poll_mode = 0;
         break;
   }

   return(TRUE);
}

/* Scan the all channel TX rings */
static void m32_tx_scan_all_channels(struct m32_data *d)
{
   u_int i;

   for(i=0;i<M32_NR_CHANNELS;i++)
      m32_tx_scan(d,i);
}

/* 
 * Handle an action request.
 *
 * IN, ICO and RES bits are mutually exclusive.
 */
static int m32_action_req(struct m32_data *d,m_uint32_t action)
{
   u_int chan_id;

   /* Define a new Interrupt Queue */
   if (action & M32_AS_IA) {
      d->iq_base_addr = d->iq_cur_addr = m32_get_cfgw(d,4);
      d->iq_size = ((m32_get_cfgw(d,8) & 0xFF) + 1) * 16 * sizeof(m_uint32_t);
   }

   /* Initialization Procedure */
   if (action & M32_AS_IN) {
      /* Fetch all timeslots assignments */
      m32_fetch_all_ts(d);

      /* Fetch specification of the specified channel */
      chan_id = (action & M32_AS_CHAN_MASK) >> M32_AS_CHAN_SHIFT;
      m32_fetch_chan_spec(d,chan_id);

      /* Generate acknowledge */
      if (!(action & M32_AS_IM))
         m32_post_interrupt(d,M32_II_ARACK);
   }

   /* Initialize Channel Only */
   if (action & M32_AS_ICO) {
      /* Fetch specification of the specified channel */
      chan_id = (action & M32_AS_CHAN_MASK) >> M32_AS_CHAN_SHIFT;
      m32_fetch_chan_spec(d,chan_id);

      /* Generate acknowledge */
      if (!(action & M32_AS_IM))
         m32_post_interrupt(d,M32_II_ARACK);
   }

   /* Reset */
   if (action & M32_AS_RES) {
      /* Fetch all timeslots assignments */
      m32_fetch_all_ts(d);

      /* Fetch all channel specifications */
      m32_fetch_all_chan_spec(d);
      
      /* Generate acknowledge */
      if (!(action & M32_AS_IM))
         m32_post_interrupt(d,M32_II_ARACK);
   }

   return(0);
}

/* Munich32 general access function */
static void *m32_gen_access(struct m32_data *d,cpu_gen_t *cpu,
                            m_uint32_t offset,u_int op_size,u_int op_type,
                            m_uint64_t *data)
{
   switch(offset) {
      /* Action Specification */
      case 0x0:
         if (op_type == MTS_WRITE)
            m32_action_req(d,*data);
         return NULL;

      /* Configuration memory */
      default:
         switch(op_size) {
            case 4:
               if (op_type == MTS_READ)
                  *data = m32_get_cfgw(d,offset);
               else
                  m32_set_cfgw(d,offset,*data);
               break;

            case 1:
               if (op_type == MTS_READ) {
                  *data = m32_get_cfgw(d,offset & ~0x03);
                  *data >>= (24 - ((offset & 0x03) << 3));
                  *data &= 0xFF;
               } else {
                  printf("UNSUPPORTED(1)!!!!\n");
               }
               break;

            case 2:
               if (op_type == MTS_READ) {
                  *data = m32_get_cfgw(d,offset & ~0x03);
                  *data >>= (16 - ((offset & 0x03) << 3));
                  *data &= 0xFFFF;
               } else {
                  printf("UNSUPPORTED(2)!!!!\n");
               }
               break;

            case 8:
               if (op_type == MTS_READ) {
                  *data = (m_uint64_t)m32_get_cfgw(d,offset) << 32;
                  *data |= m32_get_cfgw(d,offset+4);
               } else {
                  printf("UNSUPPORTED(8)!!!!\n");
               }
               break;

            default:
               printf("UNSUPPORTED (size=%u)!!!\n",op_size);
         }
   }

   return NULL;
}

/*
 *  pa_4b_access()
 */
void *pa_4b_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                   u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct pa_4b_data *d = dev->priv_data;
   __maybe_unused static m_uint32_t test1,test2,test3;

   if (op_type == MTS_READ)
      *data = 0xFFFFFFFF;

#if DEBUG_ACCESS
   if (offset >= MUNICH32_MEM_SIZE) {
      if (op_type == MTS_READ) {
         cpu_log(cpu,d->name,"read  access to offset = 0x%x, pc = 0x%llx "
                 "(op_size=%u)\n",offset,cpu_get_pc(cpu),op_size);
      } else {
         cpu_log(cpu,d->name,"write access to vaddr = 0x%x, pc = 0x%llx, "
                 "val = 0x%llx (op_size=%u)\n",
                 offset,cpu_get_pc(cpu),*data,op_size);
      }
   }
#endif

   /* Specific cases */
   switch(offset) {
      case 0x40008:
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

      case 0x40030:
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

      case 0x40000:
         if (op_type == MTS_READ)
            *data = 0xFFFF;
         break;

      case 0x40020:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF; //test2;
         else
            test2 = *data;
         break;

      case 0x40021:
         if (op_type == MTS_READ)
            *data = 0xFF; //test3;
         else
            test3 = *data;
         break;

      case 0x40023:
         if (op_type == MTS_READ)
            *data = 0xFF;
         break;

      case 0x40040:
         if (op_type == MTS_READ)
            *data = 0x04;
         break;

         /* Channels enabled ? */
      case 0x40044:
         if (op_type == MTS_READ)
            *data = 0xFF;  /* 0x02 */
         break;

         /* SID */
      case 0x40050:
         if (op_type == MTS_WRITE) {
            test1 = *data;
         } else {
            switch(test1) {
               case TP3420_SID_PUP:
                  *data = TP3420_SR_AI;
                  vm_set_irq(d->vm,C7200_PA_MGMT_IRQ);
                  break;
               case TP3420_SID_ENST:
                  *data = 0xB0;
                  break;
               default:
                  *data = 0x03;
                  break;
            }
         }
         break;

      default:
         if (offset < MUNICH32_MEM_SIZE)
            return(m32_gen_access(&d->m32_data,cpu,offset - d->m32_offset,
                                  op_size,op_type,data));
   }

   return NULL;
}

/*
 * pci_munich32_read()
 */
static m_uint32_t pci_munich32_read(cpu_gen_t *cpu,struct pci_device *dev,
                                    int reg)
{   
   struct pa_4b_data *d = dev->priv_data;

#if DEBUG_ACCESS
   BRI_LOG(d,"read PCI register 0x%x\n",reg);
#endif
   switch(reg) {
      case PCI_REG_BAR0:
         return(d->dev->phys_addr);
      default:
         return(0);
   }
}

/*
 * pci_munich32_write()
 */
static void pci_munich32_write(cpu_gen_t *cpu,struct pci_device *dev,
                               int reg,m_uint32_t value)
{
   struct pa_4b_data *d = dev->priv_data;

#if DEBUG_ACCESS
   BRI_LOG(d,"write 0x%x to PCI register 0x%x\n",value,reg);
#endif

   switch(reg) {
      case PCI_REG_BAR0:
         vm_map_device(cpu->vm,d->dev,(m_uint64_t)value);
         BRI_LOG(d,"registers are mapped at 0x%x\n",value);
         break;
   }
}

/*
 * dev_c7200_bri_init()
 *
 * Add a PA-4B/PA-8B port adapter into specified slot.
 */
int dev_c7200_pa_bri_init(vm_instance_t *vm,struct cisco_card *card)
{
   u_int slot = card->slot_id;
   struct pci_device *pci_dev;
   struct pa_4b_data *d;
   struct vdevice *dev;

   /* Allocate the private data structure for PA-4B chip */
   if (!(d = malloc(sizeof(*d)))) {
      vm_error(vm,"%s: out of memory\n",card->dev_name);
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->m32_offset = 0x08;
   d->m32_data.vm = vm;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_pa("PA-4B"));
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Add as PCI device PA-4B */
   pci_dev = pci_dev_add(card->pci_bus,card->dev_name,
                         BRI_PCI_VENDOR_ID,BRI_PCI_PRODUCT_ID,
                         0,0,C7200_NETIO_IRQ,d,
                         NULL,pci_munich32_read,pci_munich32_write);

   if (!pci_dev) {
      vm_error(vm,"%s: unable to create PCI device.\n",card->dev_name);
      return(-1);
   }

   /* Create the PA-4B structure */
   d->name    = card->dev_name;
   d->pci_dev = pci_dev;
   d->vm      = vm;

   /* Create the device itself */
   if (!(dev = dev_create(card->dev_name))) {
      vm_error(vm,"%s: unable to create device.\n",card->dev_name);
      return(-1);
   }

   dev->phys_len = 0x800000;
   dev->handler  = pa_4b_access;

   /* Store device info */
   dev->priv_data = d;
   d->dev = dev;

   /* Store device info into the router structure */
   card->drv_info = d;
   return(0);
}

/* Remove a PA-4B from the specified slot */
int dev_c7200_pa_bri_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa_4b_data *d = card->drv_info;

   /* Remove the PA EEPROM */
   cisco_card_unset_eeprom(card);
   c7200_set_slot_eeprom(VM_C7200(vm),card->slot_id,NULL);

   /* Remove the PCI device */
   pci_dev_remove(d->pci_dev);

   /* Remove the device from the CPU address space */
   vm_unbind_device(vm,d->dev);
   cpu_group_rebuild_mts(vm->cpu_group);

   /* Free the device structure itself */
   free(d->dev);
   free(d);
   return(0);
}

/* Bind a Network IO descriptor to a specific port */
int dev_c7200_pa_bri_set_nio(vm_instance_t *vm,struct cisco_card *card,
                             u_int port_id,netio_desc_t *nio)
{
   struct pa_4b_data *d = card->drv_info;

   if (!d || (port_id > 0))
      return(-1);

   if (d->nio != NULL)
      return(-1);

   d->nio = nio;

   /* TEST */
   d->m32_data.tx_tid = ptask_add((ptask_callback)m32_tx_scan_all_channels,
                                  &d->m32_data,NULL);

   //netio_rxl_add(nio,(netio_rx_handler_t)dev_pa_4b_handle_rxring,d,NULL);
   return(0);
}

/* Bind a Network IO descriptor to a specific port */
int dev_c7200_pa_bri_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                               u_int port_id)
{
   struct pa_4b_data *d = card->drv_info;

   if (!d || (port_id > 0))
      return(-1);

   if (d->nio) {
      /* TEST */
      ptask_remove(d->m32_data.tx_tid);

      //netio_rxl_remove(d->nio);
      d->nio = NULL;
   }
   return(0);
}

/* PA-4B driver */
struct cisco_card_driver dev_c7200_pa_4b_driver = {
   "PA-4B", 0, 0,
   dev_c7200_pa_bri_init,
   dev_c7200_pa_bri_shutdown,
   NULL,
   dev_c7200_pa_bri_set_nio,
   dev_c7200_pa_bri_unset_nio,
   NULL,
};
