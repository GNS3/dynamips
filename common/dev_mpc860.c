/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * MPC860 internal devices.
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
#include "dev_lxt970a.h"
#include "dev_mpc860.h"

/* Debugging flags */
#define DEBUG_ACCESS    0
#define DEBUG_UNKNOWN   1
#define DEBUG_IDMA      0
#define DEBUG_SPI       0
#define DEBUG_SCC       0
#define DEBUG_FEC       0

#define MPC860_TXRING_PASS_COUNT  16

/* Dual-Port RAM */
#define MPC860_DPRAM_OFFSET   0x2000
#define MPC860_DPRAM_SIZE     0x2000
#define MPC860_DPRAM_END      (MPC860_DPRAM_OFFSET + MPC860_DPRAM_SIZE)

/* MPC860 registers */
#define MPC860_REG_SWSR       0x000e  /* Software Service Register */
#define MPC860_REG_SIPEND     0x0010  /* SIU Interrupt Pending Register */
#define MPC860_REG_SIMASK     0x0014  /* SIU Interrupt Mask Register */
#define MPC860_REG_PIPR       0x00f0  /* PCMCIA Interface Input Pins Reg. */
#define MPC860_REG_TBSCR      0x0200  /* Timebase Status and Control Reg. */
#define MPC860_REG_PISCR      0x0240  /* Periodic Int. Status and Ctrl Reg. */
#define MPC860_REG_IDSR1      0x0910  /* IDMA1 Status Register */
#define MPC860_REG_IDMR1      0x0914  /* IDMA1 Mask Register */
#define MPC860_REG_IDSR2      0x0918  /* IDMA2 Status Register */
#define MPC860_REG_IDMR2      0x091c  /* IDMA2 Mask Register */
#define MPC860_REG_CICR       0x0940  /* CPM Int Config Register */
#define MPC860_REG_CIPR       0x0944  /* CPM Int Pending Register */
#define MPC860_REG_CIMR       0x0948  /* CPM Int Mask Register */
#define MPC860_REG_PCSO       0x0964  /* Port C Special Options Register */
#define MPC860_REG_PCDAT      0x0966  /* Port C Data Register */
#define MPC860_REG_CPCR       0x09c0  /* CP Command Register */
#define MPC860_REG_SCC_BASE   0x0a00  /* SCC register base */
#define MPC860_REG_SPMODE     0x0aa0  /* SPI Mode Register */
#define MPC860_REG_SPIE       0x0aa6  /* SPI Event Register */
#define MPC860_REG_SPIM       0x0aaa  /* SPI Mask Register */
#define MPC860_REG_SPCOM      0x0aad  /* SPI Command Register */
#define MPC860_REG_PBDAT      0x0ac4  /* Port B Data Register */
#define MPC860_REG_FEC_BASE   0x0e00  /* FEC register base */
#define MPC860_REG_FEC_END    0x0f84  /* FEC register end */

/* ======================================================================== */

/* CICR (CPM Interrupt Config Register) */
#define MPC860_CICR_IRL_MASK    0x0000E000  /* Interrupt Level */
#define MPC860_CICR_IRL_SHIFT   13
#define MPC860_CICR_IEN         0x00000080  /* Interrupt Enable */

/* CIPR (CPM Interrupt Pending Register) */
#define MPC860_CIPR_PC15      0x80000000
#define MPC860_CIPR_SCC1      0x40000000
#define MPC860_CIPR_SCC2      0x20000000
#define MPC860_CIPR_SCC3      0x10000000
#define MPC860_CIPR_SCC4      0x08000000
#define MPC860_CIPR_PC14      0x04000000
#define MPC860_CIPR_TIMER1    0x02000000
#define MPC860_CIPR_PC13      0x01000000
#define MPC860_CIPR_PC12      0x00800000
#define MPC860_CIPR_SDMA      0x00400000
#define MPC860_CIPR_IDMA1     0x00200000
#define MPC860_CIPR_IDMA2     0x00100000
#define MPC860_CIPR_TIMER2    0x00040000
#define MPC860_CIPR_RTT       0x00020000
#define MPC860_CIPR_I2C       0x00010000
#define MPC860_CIPR_PC11      0x00008000
#define MPC860_CIPR_PC10      0x00004000
#define MPC860_CIPR_TIMER3    0x00001000
#define MPC860_CIPR_PC9       0x00000800
#define MPC860_CIPR_PC8       0x00000400
#define MPC860_CIPR_PC7       0x00000200
#define MPC860_CIPR_TIMER4    0x00000080
#define MPC860_CIPR_PC6       0x00000040
#define MPC860_CIPR_SPI       0x00000020
#define MPC860_CIPR_SMC1      0x00000010
#define MPC860_CIPR_SMC2      0x00000008
#define MPC860_CIPR_PC5       0x00000004
#define MPC860_CIPR_PC4       0x00000002

/* CPCR (CP Command Register) */
#define MPC860_CPCR_RST       0x8000    /* CP reset command */
#define MPC860_CPCR_FLG       0x0001    /* Command Semaphore Flag */

#define MPC860_CPCR_CHNUM_MASK    0x00F0  /* Channel Number */
#define MPC860_CPCR_CHNUM_SHIFT   4

#define MPC860_CPCR_OPCODE_MASK   0x0F00  /* Opcode */
#define MPC860_CPCR_OPCODE_SHIFT  8

/* CP channels */
#define MPC860_CHAN_SCC1          0x00
#define MPC860_CHAN_I2C_IDMA1     0x01
#define MPC860_CHAN_SCC2          0x04
#define MPC860_CHAN_SPI_IDMA2_RT  0x05
#define MPC860_CHAN_SCC3          0x08
#define MPC860_CHAN_SMC1          0x09
#define MPC860_CHAN_SCC4          0x0c
#define MPC860_CHAN_SMC2_PIP      0x0d

/* ======================================================================== */

/* IDMA Status Register */
#define MPC860_IDSR_OB        0x0001    /* Out of Buffers */
#define MPC860_IDSR_DONE      0x0002    /* Buffer chain done */
#define MPC860_IDSR_AD        0x0004    /* Auxiliary done */

/* Offsets of IDMA channels (from DPRAM base) */
#define MPC860_IDMA1_BASE     0x1cc0
#define MPC860_IDMA2_BASE     0x1dc0

/* Size of an IDMA buffer descriptor */
#define MPC860_IDMA_BD_SIZE   16

/* IDMA Buffer Descriptor Control Word */
#define MPC860_IDMA_CTRL_V    0x8000    /* Valid Bit */
#define MPC860_IDMA_CTRL_W    0x2000    /* Wrap */
#define MPC860_IDMA_CTRL_I    0x1000    /* Interrupt for this BD */
#define MPC860_IDMA_CTRL_L    0x0800    /* Last buffer of chain */
#define MPC860_IDMA_CTRL_CM   0x0200    /* Continuous mode */

/* IDMA buffer descriptor */
struct mpc860_idma_bd {
   m_uint16_t offset;      /* Offset in DPRAM memory */

   m_uint16_t ctrl;        /* Control Word */
   m_uint8_t  dfcr,sfcr;   /* Src/Dst Function code registers */
   m_uint32_t buf_len;     /* Buffer Length */
   m_uint32_t src_bp;      /* Source buffer pointer */
   m_uint32_t dst_bp;      /* Destination buffer pointer */
};

/* ======================================================================== */

/* SPI Mode Register (SPMODE) */
#define MPC860_SPMODE_LOOP  0x4000 /* Loop mode */
#define MPC860_SPMODE_CI    0x2000 /* Clock Invert */
#define MPC860_SPMODE_CP    0x1000 /* Clock Phase */
#define MPC860_SPMODE_DIV16 0x0800 /* Divide by 16 (SPI clock generator) */
#define MPC860_SPMODE_REV   0x0400 /* Reverse Data */
#define MPC860_SPMODE_MS    0x0200 /* Master/Slave mode select */
#define MPC860_SPMODE_EN    0x0100 /* Enable SPI */

#define MPC860_SPMODE_LEN_MASK   0x00F0  /* Data length (4 - 11 bits) */
#define MPC860_SPMODE_LEN_SHIFT  4

#define MPC860_SPMODE_PM_MASK    0x000F  /* Prescale Modulus Select */

/* SPI Event/Mask Registers (SPIE/SPIM) */
#define MPC860_SPIE_MME   0x20   /* MultiMaster Error */
#define MPC860_SPIE_TXE   0x10   /* TX Error */
#define MPC860_SPIE_BSY   0x04   /* Busy (no RX buffer available) */
#define MPC860_SPIE_TXB   0x02   /* TX Buffer */
#define MPC860_SPIE_RXB   0x01   /* RX Buffer */

/* SPI Command Register (SPCOM) */
#define MPC860_SPCOM_STR  0x80   /* Start Transmit */

/* Offsets of SPI parameters (from DPRAM base) */
#define MPC860_SPI_BASE        0x1d80
#define MPC860_SPI_BASE_ADDR   0x1dac

/* Size of an SPI buffer descriptor */
#define MPC860_SPI_BD_SIZE  8

/* SPI RX Buffer Descriptor Control Word */
#define MPC860_SPI_RXBD_CTRL_E    0x8000    /* Empty */
#define MPC860_SPI_RXBD_CTRL_W    0x2000    /* Wrap */
#define MPC860_SPI_RXBD_CTRL_I    0x1000    /* Interrupt */
#define MPC860_SPI_RXBD_CTRL_L    0x0800    /* Last */
#define MPC860_SPI_RXBD_CTRL_CM   0x0200    /* Continuous Mode */
#define MPC860_SPI_RXBD_CTRL_OV   0x0002    /* Overrun */
#define MPC860_SPI_RXBD_CTRL_ME   0x0001    /* MultiMaster Error */

/* SPI TX Buffer Descriptor Control Word */
#define MPC860_SPI_TXBD_CTRL_R    0x8000    /* Ready Bit */
#define MPC860_SPI_TXBD_CTRL_W    0x2000    /* Wrap */
#define MPC860_SPI_TXBD_CTRL_I    0x1000    /* Interrupt */
#define MPC860_SPI_TXBD_CTRL_L    0x0800    /* Last */
#define MPC860_SPI_TXBD_CTRL_CM   0x0200    /* Continuous Mode */
#define MPC860_SPI_TXBD_CTRL_UN   0x0002    /* Underrun */
#define MPC860_SPI_TXBD_CTRL_ME   0x0001    /* MultiMaster Error */

/* SPI buffer descriptor */
struct mpc860_spi_bd {
   m_uint16_t offset;     /* Offset in DPRAM memory */

   m_uint16_t ctrl;       /* Control Word */
   m_uint16_t buf_len;    /* Buffer Length */
   m_uint32_t bp;         /* Buffer Pointer */
};

/* ======================================================================== */

/* Number of SCC channels */
#define MPC860_SCC_NR_CHAN   4

/* Maximum buffer size for SCC */
#define MPC860_SCC_MAX_PKT_SIZE  32768

/* Offsets of SCC channels (from DPRAM base) */
#define MPC860_SCC1_BASE   0x1c00
#define MPC860_SCC2_BASE   0x1d00
#define MPC860_SCC3_BASE   0x1e00
#define MPC860_SCC4_BASE   0x1f00

/* GSMR Low register */
#define MPC860_GSMRL_MODE_MASK   0x0000000F

/* SCC Modes */
#define MPC860_SCC_MODE_HDLC    0x00
#define MPC860_SCC_MODE_UART    0x04
#define MPC860_SCC_MODE_BISYNC  0x08
#define MPC860_SCC_MODE_ETH     0x0c

/* SCC Event (SCCE) register */
#define MPC860_SCCE_TXB    0x0002   /* TX buffer sent */
#define MPC860_SCCE_RXB    0x0001   /* RX buffer ready */

/* Size of an SCC buffer descriptor */
#define MPC860_SCC_BD_SIZE  8

/* SCC RX Buffer Descriptor Control Word */
#define MPC860_SCC_RXBD_CTRL_E    0x8000    /* Empty */
#define MPC860_SCC_RXBD_CTRL_W    0x2000    /* Wrap */
#define MPC860_SCC_RXBD_CTRL_I    0x1000    /* Interrupt */
#define MPC860_SCC_RXBD_CTRL_L    0x0800    /* Last */
#define MPC860_SCC_RXBD_CTRL_F    0x0400    /* First */
#define MPC860_SCC_RXBD_CTRL_CM   0x0200    /* Continuous Mode */
#define MPC860_SCC_RXBD_CTRL_OV   0x0002    /* Overrun */

/* SCC TX Buffer Descriptor Control Word */
#define MPC860_SCC_TXBD_CTRL_R    0x8000    /* Ready Bit */
#define MPC860_SCC_TXBD_CTRL_W    0x2000    /* Wrap */
#define MPC860_SCC_TXBD_CTRL_I    0x1000    /* Interrupt */
#define MPC860_SCC_TXBD_CTRL_L    0x0800    /* Last */
#define MPC860_SCC_TXBD_CTRL_TC   0x0400    /* Send TX CRC */
#define MPC860_SCC_TXBD_CTRL_CM   0x0200    /* Continuous Mode */
#define MPC860_SCC_TXBD_CTRL_UN   0x0002    /* Underrun */

/* SCC buffer descriptor */
struct mpc860_scc_bd {
   m_uint16_t offset;     /* Offset in DPRAM memory */

   m_uint16_t ctrl;       /* Control Word */
   m_uint16_t buf_len;    /* Buffer Length */
   m_uint32_t bp;         /* Buffer Pointer */
};

/* ======================================================================== */

/* FEC Ethernet Control Register */
#define MPC860_ECNTRL_FEC_PIN_MUX  0x00000004   /* FEC enable */
#define MPC860_ECNTRL_ETHER_EN     0x00000002   /* Ethernet Enable */
#define MPC860_ECNTRL_RESET        0x00000001   /* Reset Ethernet controller */

/* FEC Interrupt Vector Register */
#define MPC860_IVEC_ILEVEL_MASK    0xE0000000   /* Interrupt Level */
#define MPC860_IVEC_ILEVEL_SHIFT   29

/* FEC Interrupt Event Register */
#define MPC860_IEVENT_HBERR    0x80000000   /* Hearbeat Error */
#define MPC860_IEVENT_BABR     0x40000000   /* Babbling Receive Error */
#define MPC860_IEVENT_BABT     0x20000000   /* Babbling Transmit Error */
#define MPC860_IEVENT_GRA      0x10000000   /* Graceful Stop Complete */
#define MPC860_IEVENT_TFINT    0x08000000   /* Transmit Frame Interrupt */
#define MPC860_IEVENT_TXB      0x04000000   /* Transmit Buffer Interrupt */
#define MPC860_IEVENT_RFINT    0x02000000   /* Receive Frame Interrupt */
#define MPC860_IEVENT_RXB      0x01000000   /* Receive Buffer Interrupt */
#define MPC860_IEVENT_MII      0x00800000   /* MII Interrupt */
#define MPC860_IEVENT_EBERR    0x00400000   /* Ethernet Bus Error */

/* MII data register */
#define MPC860_MII_OP_MASK     0x30000000   /* Opcode (10b:read,11b:write) */
#define MPC860_MII_OP_SHIFT    28
#define MPC860_MII_PHY_MASK    0x0F800000   /* PHY device */
#define MPC860_MII_PHY_SHIFT   23
#define MPC860_MII_REG_MASK    0x007C0000   /* PHY register */
#define MPC860_MII_REG_SHIFT   18

/* Size of an FEC buffer descriptor */
#define MPC860_FEC_BD_SIZE  8

/* Maximum packet size for FEC */
#define MPC860_FEC_MAX_PKT_SIZE   2048

/* FEC RX Buffer Descriptor Control Word */
#define MPC860_FEC_RXBD_CTRL_E    0x8000    /* Empty */
#define MPC860_FEC_RXBD_CTRL_RO1  0x4000    /* For software use */
#define MPC860_FEC_RXBD_CTRL_W    0x2000    /* Wrap */
#define MPC860_FEC_RXBD_CTRL_RO2  0x1000    /* For software use */
#define MPC860_FEC_RXBD_CTRL_L    0x0800    /* Last */
#define MPC860_FEC_RXBD_CTRL_M    0x0100    /* Miss */
#define MPC860_FEC_RXBD_CTRL_BC   0x0080    /* Broadcast DA */
#define MPC860_FEC_RXBD_CTRL_MC   0x0040    /* Multicast DA */
#define MPC860_FEC_RXBD_CTRL_LG   0x0020    /* RX Frame length violation */
#define MPC860_FEC_RXBD_CTRL_NO   0x0010    /* RX non-octet aligned frame */
#define MPC860_FEC_RXBD_CTRL_SH   0x0008    /* Short Frame */
#define MPC860_FEC_RXBD_CTRL_CR   0x0004    /* RX CRC Error */
#define MPC860_FEC_RXBD_CTRL_OV   0x0002    /* Overrun */
#define MPC860_FEC_RXBD_CTRL_TR   0x0001    /* Truncated Frame */

/* FEC TX Buffer Descriptor Control Word */
#define MPC860_FEC_TXBD_CTRL_R    0x8000    /* Ready Bit */
#define MPC860_FEC_TXBD_CTRL_TO1  0x4000    /* For software use */
#define MPC860_FEC_TXBD_CTRL_W    0x2000    /* Wrap */
#define MPC860_FEC_TXBD_CTRL_TO2  0x1000    /* For software use */
#define MPC860_FEC_TXBD_CTRL_L    0x0800    /* Last */
#define MPC860_FEC_TXBD_CTRL_TC   0x0400    /* Send TX CRC */
#define MPC860_FEC_TXBD_CTRL_DEF  0x0200    /* Defer Indication */
#define MPC860_FEC_TXBD_CTRL_HB   0x0100    /* Heartbeat Error */
#define MPC860_FEC_TXBD_CTRL_LC   0x0080    /* Late Collision */
#define MPC860_FEC_TXBD_CTRL_RL   0x0040    /* Retranmission Limit */
#define MPC860_FEC_TXBD_CTRL_UN   0x0002    /* Underrun */
#define MPC860_FEC_TXBD_CTRL_CSL  0x0001    /* Carrier Sense Lost */

/* FEC buffer descriptor */
struct mpc860_fec_bd {
   m_uint32_t bd_addr;    /* Address in external memory */

   m_uint16_t ctrl;       /* Control Word */
   m_uint16_t buf_len;    /* Buffer Length */
   m_uint32_t bp;         /* Buffer Pointer */
};

/* ======================================================================== */

struct mpc860_scc_chan {
   netio_desc_t *nio;

   /* General SCC mode register (high and low parts) */
   m_uint32_t gsmr_hi,gsmr_lo;

   /* Protocol-Specific mode register */
   m_uint32_t psmr;

   /* SCC Event and Mask registers */
   m_uint16_t scce,sccm;

   /* TX packet */
   u_char tx_pkt[MPC860_SCC_MAX_PKT_SIZE];
};

/* MPC860 private data */
struct mpc860_data {
   char *name;
   vm_obj_t vm_obj;
   struct vdevice dev;
   struct pci_device *pci_dev;
   vm_instance_t *vm;

   /* SIU Interrupt Pending Register and Interrupt Mask Register */
   m_uint32_t sipend,simask;

   /* CPM Interrupt Configuration Register */
   m_uint32_t cicr;

   /* CPM Interrupt Pending Register and Interrupt Mask Register */
   m_uint32_t cipr,cimr;

   /* IDMA status and mask registers */
   m_uint8_t idsr[2],idmr[2];

   /* Port B Data Register */
   m_uint32_t pbdat,pcdat;

   /* SPI callback for TX data */
   mpc860_spi_tx_callback_t spi_tx_callback;
   void *spi_user_arg;

   /* SCC channels */
   struct mpc860_scc_chan scc_chan[MPC860_SCC_NR_CHAN];

   /* FEC (Fast Ethernet Controller) */
   m_uint32_t fec_rdes_start,fec_xdes_start;
   m_uint32_t fec_rdes_current,fec_xdes_current;
   m_uint32_t fec_rbuf_size;

   /* FEC Interrupt Event/Mask registers */
   m_uint32_t fec_ievent,fec_imask,fec_ivec;
   m_uint32_t fec_ecntrl;

   /* FEC NetIO */
   netio_desc_t *fec_nio;

   /* FEC MII registers */
   m_uint32_t fec_mii_data;
   m_uint16_t fec_mii_regs[32];
   m_uint8_t fec_mii_last_read_reg;

   /* Dual-Port RAM */
   m_uint8_t dpram[MPC860_DPRAM_SIZE];
};

/* Log a MPC message */
#define MPC_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* ======================================================================== */

/* DPRAM access routines */
static inline m_uint8_t dpram_r8(struct mpc860_data *d,m_uint16_t offset)
{
   return(d->dpram[offset]);
}

_unused static inline void dpram_w8(struct mpc860_data *d,m_uint16_t offset,
                            m_uint8_t val)
{   
   d->dpram[offset] = val;
}

static inline m_uint16_t dpram_r16(struct mpc860_data *d,m_uint16_t offset)
{
   m_uint16_t val;

   val = (m_uint16_t)d->dpram[offset] << 8;
   val |= d->dpram[offset+1];
   return(val);
}

static inline void dpram_w16(struct mpc860_data *d,m_uint16_t offset,
                             m_uint16_t val)
{  
   d->dpram[offset]   = val >> 8;
   d->dpram[offset+1] = val & 0xFF;
}

static inline m_uint32_t dpram_r32(struct mpc860_data *d,m_uint16_t offset)
{
   m_uint32_t val;

   val =  d->dpram[offset]   << 24;
   val |= d->dpram[offset+1] << 16;
   val |= d->dpram[offset+2] << 8;
   val |= d->dpram[offset+3];
   return(val);
}

_unused static inline void dpram_w32(struct mpc860_data *d,m_uint16_t offset,
                             m_uint32_t val)
{
   d->dpram[offset]   = val >> 24;
   d->dpram[offset+1] = val >> 16;
   d->dpram[offset+2] = val >> 8;
   d->dpram[offset+3] = val;
}

/* ======================================================================== */

/* Update interrupt status */
static void mpc860_update_irq_status(struct mpc860_data *d)
{
   cpu_ppc_t *cpu = CPU_PPC32(d->vm->boot_cpu);

   cpu->irq_pending = d->sipend & d->simask;
   cpu->irq_check = cpu->irq_pending;
}

/* Map level to SIU Interrupt Pending Register bit */
static inline u_int mpc860_get_siu_lvl(u_int level)
{
   return(16 + ((7 - level) << 1));
}

/* Update CPM interrupt status */
static void mpc860_update_cpm_int_status(struct mpc860_data *d)
{
   u_int level,siu_bit;

   level = (d->cicr & MPC860_CICR_IRL_MASK) >> MPC860_CICR_IRL_SHIFT;
   siu_bit = mpc860_get_siu_lvl(level);

   if ((d->cipr & d->cimr) && (d->cicr & MPC860_CICR_IEN))
      mpc860_set_pending_irq(d,siu_bit);
   else
      mpc860_clear_pending_irq(d,siu_bit);
}

/* ======================================================================== */
/* IDMA                                                                     */
/* ======================================================================== */

/* Update an IDMA status register */
static int mpc860_idma_update_idsr(struct mpc860_data *d,u_int id)
{
   u_int cpm_int;

   switch(id) {
      case 0:
         cpm_int = MPC860_CIPR_IDMA1;
         break;
      case 1:
         cpm_int = MPC860_CIPR_IDMA2;
         break;
      default:
         return(-1);
   }

   if (d->idsr[id] & d->idmr[id])
      d->cipr |= cpm_int;
   else
      d->cipr &= ~cpm_int;

   mpc860_update_cpm_int_status(d);
   return(0);
}

/* Process to an IDMA transfer for the specified buffer descriptor */
static void mpc860_idma_transfer(struct mpc860_data *d,
                                 struct mpc860_idma_bd *bd)
{
   physmem_dma_transfer(d->vm,bd->src_bp,bd->dst_bp,bd->buf_len);
}

/* Fetch an IDMA descriptor from Dual-Port RAM */
static int mpc860_idma_fetch_bd(struct mpc860_data *d,m_uint16_t bd_addr,
                                struct mpc860_idma_bd *bd)
{
   if ((bd_addr < MPC860_DPRAM_OFFSET) || (bd_addr > MPC860_DPRAM_END))
      return(-1);

   bd->offset = bd_addr - MPC860_DPRAM_OFFSET;

   /* Fetch control word */
   bd->ctrl = dpram_r16(d,bd->offset+0x00);

   /* Fetch function code registers */
   bd->dfcr = dpram_r8(d,bd->offset+0x02);
   bd->sfcr = dpram_r8(d,bd->offset+0x03);

   /* Fetch buffer length, source and destination addresses */
   bd->buf_len = dpram_r32(d,bd->offset+0x04);
   bd->src_bp  = dpram_r32(d,bd->offset+0x08);
   bd->dst_bp  = dpram_r32(d,bd->offset+0x0c);

#if DEBUG_IDMA
   MPC_LOG(d,"fetched IDMA BD at 0x%4.4x, src_bp=0x%8.8x, dst_bp=0x%8.8x "
           "len=%d\n",bd->offset,bd->src_bp,bd->dst_bp,bd->buf_len);
#endif

   return(0);
}

/* Start an IDMA channel */
static int mpc860_idma_start_channel(struct mpc860_data *d,u_int id)
{
   struct mpc860_idma_bd bd;
   m_uint16_t dma_base,bd_offset;

   switch(id) {
      case 0:
         dma_base = MPC860_IDMA1_BASE;
         break;
      case 1:
         dma_base = MPC860_IDMA2_BASE;
         break;
      default:
         return(-1);
   }

   /* Get the IBASE register (offset 0) */
   bd_offset = dpram_r16(d,dma_base+0x00);

   while(1) {
      /* Fetch a descriptor */
      if (mpc860_idma_fetch_bd(d,bd_offset,&bd) == -1)
         return(-1);

      if (!(bd.ctrl & MPC860_IDMA_CTRL_V)) {
         d->idsr[id] |= MPC860_IDSR_OB;
         break;
      }

      /* Run the DMA transfer */
      mpc860_idma_transfer(d,&bd);

      /* Clear the Valid bit */
      bd.ctrl &= ~MPC860_IDMA_CTRL_V;
      dpram_w16(d,bd_offset-MPC860_DPRAM_OFFSET+0x00,bd.ctrl);

      /* Generate an interrupt for this buffer ? */
      if (bd.ctrl & MPC860_IDMA_CTRL_I)
         d->idsr[id] |= MPC860_IDSR_AD;

      /* Stop if this is the last buffer of chain */
      if (bd.ctrl & MPC860_IDMA_CTRL_L) {
         d->idsr[id] |= MPC860_IDSR_DONE;
         break;
      }

      bd_offset += sizeof(MPC860_IDMA_BD_SIZE);
   }

   mpc860_idma_update_idsr(d,id);
   return(0);
}

/* ======================================================================== */
/* SPI (Serial Peripheral Interface)                                        */
/* ======================================================================== */

/* Initialize SPI RX parameters */
static void mpc860_spi_init_rx_params(struct mpc860_data *d)
{ 
   m_uint16_t spi_base,rbase;

   spi_base = dpram_r16(d,MPC860_SPI_BASE_ADDR);

   /* Get the RBASE (offset 0) and store it in RBPTR */
   rbase = dpram_r16(d,spi_base+0x00);
   dpram_w16(d,spi_base+0x10,rbase);
}

/* Initialize SPI TX parameters */
static void mpc860_spi_init_tx_params(struct mpc860_data *d)
{
   m_uint16_t spi_base,tbase;

   spi_base = dpram_r16(d,MPC860_SPI_BASE_ADDR);

   /* Get the TBASE (offset 2) and store it in TBPTR */
   tbase = dpram_r16(d,spi_base+0x02);
   dpram_w16(d,spi_base+0x20,tbase);
}

/* Initialize SPI RX/TX parameters */
static void mpc860_spi_init_rx_tx_params(struct mpc860_data *d)
{
   mpc860_spi_init_rx_params(d);
   mpc860_spi_init_tx_params(d);
}

/* Fetch a SPI buffer descriptor */
static int mpc860_spi_fetch_bd(struct mpc860_data *d,m_uint16_t bd_addr,
                               struct mpc860_spi_bd *bd)
{
   if ((bd_addr < MPC860_DPRAM_OFFSET) || (bd_addr > MPC860_DPRAM_END))
      return(-1);

   bd->offset = bd_addr - MPC860_DPRAM_OFFSET;

   /* Fetch control word */
   bd->ctrl = dpram_r16(d,bd->offset+0x00);

   /* Fetch buffer length and buffer pointer */
   bd->buf_len = dpram_r16(d,bd->offset+0x02);
   bd->bp      = dpram_r32(d,bd->offset+0x04);

#if DEBUG_SPI
   MPC_LOG(d,"fetched SPI BD at 0x%4.4x, bp=0x%8.8x, len=%d\n",
           bd->offset,bd->bp,bd->buf_len);
#endif

   return(0);
}

/* Start SPI transmit */
static int mpc860_spi_start_tx(struct mpc860_data *d)
{
   struct mpc860_spi_bd bd;
   m_uint16_t bd_offset;
   m_uint16_t spi_base;
   u_char buffer[512];
   u_int buf_len;

   spi_base = dpram_r16(d,MPC860_SPI_BASE_ADDR);

   /* Get the TBPTR (offset 0x20) register */
   bd_offset = dpram_r16(d,spi_base+0x20);

   while(1) {
      /* Fetch a TX descriptor */
      if (mpc860_spi_fetch_bd(d,bd_offset,&bd) == -1)
         return(FALSE);

      /* If the descriptor is not ready, stop now */
      if (!(bd.ctrl & MPC860_SPI_TXBD_CTRL_R))
         return(FALSE);

      /* Extract the data */
      buf_len = bd.buf_len;

      if (bd.buf_len > sizeof(buffer)) {
         MPC_LOG(d,"SPI: buffer too small for transmit.\n");
         buf_len = sizeof(buffer);
      }

      physmem_copy_from_vm(d->vm,buffer,bd.bp,buf_len);

      /* Send the data to the user callback (if specified) */
      if (d->spi_tx_callback != NULL)
         d->spi_tx_callback(d,buffer,buf_len,d->spi_user_arg);

      /* Clear the Ready bit of the TX descriptor */
      bd.ctrl &= ~MPC860_SPI_TXBD_CTRL_R;
      dpram_w16(d,bd.offset+0x00,bd.ctrl);

      /* Set pointer on next TX descriptor (wrap ring if necessary) */
      if (bd.ctrl & MPC860_SPI_TXBD_CTRL_W) {
         bd_offset = dpram_r16(d,spi_base+0x02);
      } else {
         bd_offset += MPC860_SPI_BD_SIZE;
      }
      dpram_w16(d,spi_base+0x20,bd_offset);

      /* Stop if this is the last buffer in chain */
      if (bd.ctrl & MPC860_SPI_TXBD_CTRL_L)
         break;
   }

   return(TRUE);
}

/* Put a buffer into SPI receive buffers */
int mpc860_spi_receive(struct mpc860_data *d,u_char *buffer,u_int len)
{
   struct mpc860_spi_bd bd;
   m_uint16_t bd_offset;
   m_uint16_t spi_base;
   u_int clen,mrblr;

   spi_base = dpram_r16(d,MPC860_SPI_BASE_ADDR);

   /* Get the RBPTR (offset 0x10) */
   bd_offset = dpram_r16(d,spi_base+0x10);

   /* Get the maximum buffer size */
   mrblr = dpram_r16(d,spi_base+0x06);

   while(len > 0) {
      /* Fetch a RX descriptor */
      if (mpc860_spi_fetch_bd(d,bd_offset,&bd) == -1)
         return(FALSE);

      /* If the buffer is not empty, do not use it */
      if (!(bd.ctrl & MPC860_SPI_RXBD_CTRL_E))
         return(FALSE);

      /* Write data into the RX buffer */
      clen = m_min(mrblr,len);
      physmem_copy_to_vm(d->vm,buffer,bd.bp,clen);
      buffer += clen;
      len -= clen;

      /* Update the length field */
      dpram_w16(d,bd.offset+0x02,clen);

      /* If no more data, set the "Last" bit */
      if (!len)
         bd.ctrl |= MPC860_SPI_RXBD_CTRL_L;

      /* Clear the Empty bit of the RX descriptor */
      bd.ctrl &= ~MPC860_SPI_RXBD_CTRL_E;
      dpram_w16(d,bd.offset+0x00,bd.ctrl);

      /* Set pointer on next RX descriptor */
      if (bd.ctrl & MPC860_SPI_RXBD_CTRL_W) {
         bd_offset = dpram_r16(d,spi_base+0x00);
      } else {
         bd_offset += MPC860_SPI_BD_SIZE;
      }
      dpram_w16(d,spi_base+0x10,bd_offset);
   }

   if (len > 0)
      MPC_LOG(d,"SPI: no buffers available for receive.\n");

   return(0);
}

/* Set SPI TX callback */
void mpc860_spi_set_tx_callback(struct mpc860_data *d,
                                mpc860_spi_tx_callback_t cbk,
                                void *user_arg)
{
   d->spi_tx_callback = cbk;
   d->spi_user_arg = user_arg;
}

/* ======================================================================== */
/* SCC (Serial Communication Controller)                                    */
/* ======================================================================== */

typedef struct {
   u_int cipr_irq;
   m_uint32_t dpram_base;
}scc_chan_info_t;

static scc_chan_info_t scc_chan_info[MPC860_SCC_NR_CHAN] = {
   { MPC860_CIPR_SCC1, MPC860_SCC1_BASE },
   { MPC860_CIPR_SCC2, MPC860_SCC2_BASE },
   { MPC860_CIPR_SCC3, MPC860_SCC3_BASE },
   { MPC860_CIPR_SCC4, MPC860_SCC4_BASE },
};

/* Initialize SCC RX parameters */
static void mpc860_scc_init_rx_params(struct mpc860_data *d,u_int scc_chan)
{ 
   m_uint16_t scc_base,rbase;

   scc_base = scc_chan_info[scc_chan].dpram_base;
   
   /* Get the RBASE (offset 0) and store it in RBPTR */
   rbase = dpram_r16(d,scc_base+0x00);
   dpram_w16(d,scc_base+0x10,rbase);
}

/* Initialize SCC TX parameters */
static void mpc860_scc_init_tx_params(struct mpc860_data *d,u_int scc_chan)
{
   m_uint16_t scc_base,tbase;

   scc_base = scc_chan_info[scc_chan].dpram_base;

   /* Get the TBASE (offset 2) and store it in TBPTR */
   tbase = dpram_r16(d,scc_base+0x02);
   dpram_w16(d,scc_base+0x20,tbase);
}

/* Initialize SCC RX/TX parameters */
static void mpc860_scc_init_rx_tx_params(struct mpc860_data *d,u_int scc_chan)
{
   mpc860_scc_init_rx_params(d,scc_chan);
   mpc860_scc_init_tx_params(d,scc_chan);
}

/* Set an SCC interrupt */
static int mpc860_scc_update_irq(struct mpc860_data *d,u_int scc_chan)
{
   struct mpc860_scc_chan *chan = &d->scc_chan[scc_chan];

   if (chan->scce & chan->sccm)
      d->cipr |= scc_chan_info[scc_chan].cipr_irq;
   else
      d->cipr &= ~scc_chan_info[scc_chan].cipr_irq;

   mpc860_update_cpm_int_status(d);
   return(0);
}

/* Fetch a SCC buffer descriptor */
static int mpc860_scc_fetch_bd(struct mpc860_data *d,m_uint16_t bd_addr,
                               struct mpc860_scc_bd *bd)
{
   if ((bd_addr < MPC860_DPRAM_OFFSET) || (bd_addr > MPC860_DPRAM_END))
      return(-1);

   bd->offset = bd_addr - MPC860_DPRAM_OFFSET;

   /* Fetch control word */
   bd->ctrl = dpram_r16(d,bd->offset+0x00);

   /* Fetch buffer length and buffer pointer */
   bd->buf_len = dpram_r16(d,bd->offset+0x02);
   bd->bp      = dpram_r32(d,bd->offset+0x04);

#if DEBUG_SCC
   MPC_LOG(d,"fetched SCC BD at 0x%4.4x, bp=0x%8.8x, len=%d\n",
           bd->offset,bd->bp,bd->buf_len);
#endif

   return(0);
}

/* Handle the TX ring of an SCC channel (transmit a single packet) */
static int mpc860_scc_handle_tx_ring_single(struct mpc860_data *d,
                                            u_int scc_chan)
{   
   struct mpc860_scc_bd txd0,ctxd,*ptxd;
   struct mpc860_scc_chan *chan;
   scc_chan_info_t *scc_info;
   m_uint16_t bd_offset;
   m_uint32_t clen,tot_len;
   u_char *pkt_ptr;
   int done = FALSE;
   int irq = FALSE;
   
   scc_info = &scc_chan_info[scc_chan];
   chan = &d->scc_chan[scc_chan];
   
   /* Get the TBPTR (offset 0x20) register */
   bd_offset = dpram_r16(d,scc_info->dpram_base+0x20);

   /* Try to acquire the first descriptor */
   ptxd = &txd0;
   mpc860_scc_fetch_bd(d,bd_offset,ptxd);

   /* If we don't own the first descriptor, we cannot transmit */
   if (!(txd0.ctrl & MPC860_SCC_TXBD_CTRL_R))
      return(FALSE);

   /* Empty packet for now */
   pkt_ptr = chan->tx_pkt;
   tot_len = 0;

   do {
      /* Copy data into the buffer */
      clen = ptxd->buf_len;
      physmem_copy_from_vm(d->vm,pkt_ptr,ptxd->bp,clen);
      pkt_ptr += clen;
      tot_len += clen;

      /* Signal IRQ ? */
      if (ptxd->ctrl & MPC860_SCC_TXBD_CTRL_I)
         irq = TRUE;

      /* 
       * Clear the ready bit (except for the first descriptor, 
       * which is cleared when the full packet has been sent).
       */
      if (ptxd != &txd0) {
         ptxd->ctrl &= ~MPC860_SCC_TXBD_CTRL_R;
         dpram_w16(d,ptxd->offset+0x00,ptxd->ctrl);
      }

      /* Set pointer on next TX descriptor (wrap ring if necessary) */
      if (ptxd->ctrl & MPC860_SCC_TXBD_CTRL_W) {
         bd_offset = dpram_r16(d,scc_info->dpram_base+0x02);
      } else {
         bd_offset += MPC860_SCC_BD_SIZE;
      }
      dpram_w16(d,scc_info->dpram_base+0x20,bd_offset);

      /* If this is the last descriptor, we have finished */
      if (!(ptxd->ctrl & MPC860_SCC_TXBD_CTRL_L)) {
         mpc860_scc_fetch_bd(d,bd_offset,&ctxd);
         ptxd = &ctxd;
      } else {
         done = TRUE;
      }
   }while(!done);

   if (tot_len != 0) {
#if DEBUG_SCC
      MPC_LOG(d,"SCC%u: sending packet of %u bytes\n",scc_chan+1,tot_len);
      mem_dump(log_file,chan->tx_pkt,tot_len);
#endif
      /* send packet on wire */
      netio_send(chan->nio,chan->tx_pkt,tot_len);
   }

   /* Clear the Ready bit of the first TX descriptor */
   txd0.ctrl &= ~MPC860_SCC_TXBD_CTRL_R;
   dpram_w16(d,txd0.offset+0x00,txd0.ctrl);

   /* Trigger SCC IRQ */
   if (irq) {
      chan->scce |= MPC860_SCCE_TXB;
      mpc860_scc_update_irq(d,scc_chan);
   }
   
   return(TRUE);
}

/* Handle the TX ring of the specified SCC channel (multiple pkts possible) */
static int mpc860_scc_handle_tx_ring(struct mpc860_data *d,u_int scc_chan)
{
   int i;

   for(i=0;i<MPC860_TXRING_PASS_COUNT;i++)
      if (!mpc860_scc_handle_tx_ring_single(d,scc_chan))
         break;

   return(TRUE);
}

/* Handle RX packet for an SCC channel */
static int mpc860_scc_handle_rx_pkt(netio_desc_t *nio,
                                    u_char *pkt,ssize_t pkt_len,
                                    struct mpc860_data *d,void *arg)
{       
   struct mpc860_scc_bd rxd0,crxd,*prxd;
   struct mpc860_scc_chan *chan;
   u_int scc_chan = (u_int)(u_long)arg;
   scc_chan_info_t *scc_info;
   m_uint16_t bd_offset;
   ssize_t clen,tot_len;
   u_char *pkt_ptr;
   u_int mrblr;
   int irq = FALSE;
   
   scc_info = &scc_chan_info[scc_chan];
   chan = &d->scc_chan[scc_chan];

   /* Get the RBPTR (offset 0x10) register */
   bd_offset = dpram_r16(d,scc_info->dpram_base+0x10);

   /* Get the maximum buffer size */
   mrblr = dpram_r16(d,scc_info->dpram_base+0x06);

   /* Try to acquire the first descriptor */
   prxd = &rxd0;
   mpc860_scc_fetch_bd(d,bd_offset,prxd);

   /* If we don't own the first descriptor, we cannot transmit */
   if (!(rxd0.ctrl & MPC860_SCC_RXBD_CTRL_E))
      return(FALSE);

   pkt_ptr = pkt;
   tot_len = pkt_len;

   while(tot_len > 0) {
      /* Write data into the RX buffer */
      clen = m_min(mrblr,tot_len);
      physmem_copy_to_vm(d->vm,pkt_ptr,prxd->bp,clen);
      pkt_ptr += clen;
      tot_len -= clen;

      /* Signal IRQ ? */
      if (prxd->ctrl & MPC860_SCC_RXBD_CTRL_I)
         irq = TRUE;

      /* Set the Last flag if we have finished */
      if (!tot_len) {
         /* Set the full length */
         switch(chan->gsmr_lo & MPC860_GSMRL_MODE_MASK) {
            case MPC860_SCC_MODE_ETH:
               pkt_len += 4;
               break;
            case MPC860_SCC_MODE_HDLC:
               pkt_len += 2;
               break;
         }

         dpram_w16(d,prxd->offset+0x02,pkt_len);
         prxd->ctrl |= MPC860_SCC_RXBD_CTRL_L;
      } else {
         /* Update the length field */
         dpram_w16(d,prxd->offset+0x02,clen);
      }

      /* 
       * Clear the empty bit (except for the first descriptor, 
       * which is cleared when the full packet has been stored).
       */
      if (prxd != &rxd0) {
         prxd->ctrl &= ~MPC860_SCC_RXBD_CTRL_E;
         dpram_w16(d,prxd->offset+0x00,prxd->ctrl);
      }

      /* Set pointer on next RX descriptor (wrap ring if necessary) */
      if (prxd->ctrl & MPC860_SCC_RXBD_CTRL_W) {
         bd_offset = dpram_r16(d,scc_info->dpram_base+0x00);
      } else {
         bd_offset += MPC860_SCC_BD_SIZE;
      }
      dpram_w16(d,scc_info->dpram_base+0x10,bd_offset);

      /* If this is the last descriptor, we have finished */
      if (!tot_len) {
         mpc860_scc_fetch_bd(d,bd_offset,&crxd);
         prxd = &crxd;
      }
   }

   /* Clear the Empty bit of the first RX descriptor and set First bit */
   rxd0.ctrl &= ~MPC860_SCC_RXBD_CTRL_E;
   rxd0.ctrl |= MPC860_SCC_RXBD_CTRL_F;
   dpram_w16(d,rxd0.offset+0x00,rxd0.ctrl);

   /* Trigger SCC IRQ */
   if (irq) {
      d->scc_chan[scc_chan].scce |= MPC860_SCCE_RXB;
      mpc860_scc_update_irq(d,scc_chan);
   }

   return(TRUE);
}

/* Set NIO for the specified SCC channel */
int mpc860_scc_set_nio(struct mpc860_data *d,u_int scc_chan,netio_desc_t *nio)
{
   struct mpc860_scc_chan *chan;

   if (!d || (scc_chan >= MPC860_SCC_NR_CHAN))
      return(-1);

   chan = &d->scc_chan[scc_chan];

   /* check that a NIO is not already bound */
   if (chan->nio != NULL)
      return(-1);

   chan->nio = nio;
   netio_rxl_add(nio,(netio_rx_handler_t)mpc860_scc_handle_rx_pkt,
                 d,(void *)(u_long)scc_chan);
   return(0);
}

/* Unset NIO of the specified SCC channel */
int mpc860_scc_unset_nio(struct mpc860_data *d,u_int scc_chan)
{
   struct mpc860_scc_chan *chan;

   if (!d || (scc_chan >= MPC860_SCC_NR_CHAN))
      return(-1);

   chan = &d->scc_chan[scc_chan]; 

   if (chan->nio != NULL) {
      netio_rxl_remove(chan->nio);
      chan->nio = NULL;
   }

   return(0);
}

/* 
 * SCC register access.
 *
 * SCC1: 0x0a00 to 0x0a1f
 * SCC2: 0x0a20 to 0x0a3f
 * SCC3: 0x0a40 to 0x0a5f
 * SCC4: 0x0a60 to 0x0a7f
 */
static int dev_mpc860_scc_access(struct mpc860_data *d,m_uint32_t offset,
                                 u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct mpc860_scc_chan *chan;
   u_int scc_chan,reg;

   /* Extract channel ID and register */
   scc_chan = (offset >> 5) & 0x03;
   reg = offset & 0x1F;

   chan = &d->scc_chan[scc_chan];

   switch(reg) {
      /* GSMRL - General SCC mode register (Low part) */
      case 0x00:
         if (op_type == MTS_READ)
            *data = chan->gsmr_lo;
         else
            chan->gsmr_lo = *data;
         break;

      /* GSMRH - General SCC mode register (High part) */
      case 0x04:
         if (op_type == MTS_READ)
            *data = chan->gsmr_hi;
         else
            chan->gsmr_hi = *data;
         break;

      /* PSMR - Protocol-Specific Mode Register */
      case 0x08:
         if (op_type == MTS_READ)
            *data = chan->psmr;
         else
            chan->psmr = *data;
         break;

      /* TOD - Transmit On Demand */
      case 0x0c:
         if ((op_type == MTS_WRITE) && (*data & 0x8000))
            mpc860_scc_handle_tx_ring(d,scc_chan);
         break;

      /* SCCE - SCC Event Register */
      case 0x10:
         if (op_type == MTS_READ)
            *data = chan->scce;
         else {
            chan->scce &= ~(*data);
            mpc860_scc_update_irq(d,scc_chan);
         }
         break;

      /* SCCM - SCC Mask Register */
      case 0x14:
         if (op_type == MTS_READ)
            *data = chan->sccm;
         else {
            chan->sccm = *data;
            mpc860_scc_update_irq(d,scc_chan);
         }
         break;
   }

   return(0);
}

/* ======================================================================== */
/* FEC (Fast Ethernet Controller)                                           */
/* ======================================================================== */

/* link status */
static int mpc860_fec_link_is_up(struct mpc860_data *d)
{
   return(d->fec_nio && !(d->fec_mii_regs[LX970A_CR] & LX970A_CR_POWERDOWN));
}

/* Trigger interrupt for FEC */
static void mpc860_fec_update_irq_status(struct mpc860_data *d)
{
   u_int level,siu_bit;

   level = (d->fec_ivec & MPC860_IVEC_ILEVEL_MASK) >> MPC860_IVEC_ILEVEL_SHIFT;
   siu_bit = mpc860_get_siu_lvl(level);

   if (d->fec_ievent & d->fec_imask)
      mpc860_set_pending_irq(d,siu_bit);
   else
      mpc860_clear_pending_irq(d,siu_bit);
}

/* Fetch a FEC buffer descriptor, located in external memory */
static int mpc860_fec_fetch_bd(struct mpc860_data *d,m_uint32_t bd_addr,
                               struct mpc860_fec_bd *bd)
{
   m_uint32_t w0,w1;

   /* Set BD address */
   bd->bd_addr = bd_addr;

   w0 = physmem_copy_u32_from_vm(d->vm,bd_addr);
   w1 = physmem_copy_u32_from_vm(d->vm,bd_addr+4);

   bd->ctrl    = w0 >> 16;
   bd->buf_len = w0 & 0xFFFF;
   bd->bp      = w1;

#if DEBUG_FEC
   MPC_LOG(d,"fetched FEC BD at 0x%8.8x, bp=0x%8.8x, len=%d\n",
           bd->bd_addr,bd->bp,bd->buf_len);
#endif

   return(0);
}

/* Handle the TX ring of the FEC (transmit a single packet) */
static int mpc860_fec_handle_tx_ring_single(struct mpc860_data *d)
{  
   u_char tx_pkt[MPC860_FEC_MAX_PKT_SIZE];
   struct mpc860_fec_bd txd0,ctxd,*ptxd;
   m_uint32_t clen,tot_len;
   u_char *pkt_ptr;
   int done = FALSE;

   if (!d->fec_xdes_current)
      return(FALSE);

   /* Try to acquire the first descriptor */
   ptxd = &txd0;
   mpc860_fec_fetch_bd(d,d->fec_xdes_current,ptxd);

   /* If we don't own the first descriptor, we cannot transmit */
   if (!(txd0.ctrl & MPC860_FEC_TXBD_CTRL_R))
      return(FALSE);

   /* Empty packet for now */
   pkt_ptr = tx_pkt;
   tot_len = 0;

   do {
      /* Copy data into the buffer */
      clen = ptxd->buf_len;
      physmem_copy_from_vm(d->vm,pkt_ptr,ptxd->bp,clen);
      pkt_ptr += clen;
      tot_len += clen;

      /* 
       * Clear the ready bit (except for the first descriptor, 
       * which is cleared when the full packet has been sent).
       */
      if (ptxd != &txd0) {
         ptxd->ctrl &= ~MPC860_FEC_TXBD_CTRL_R;
         physmem_copy_u16_to_vm(d->vm,ptxd->bd_addr+0x00,ptxd->ctrl);
      }

      /* Set pointer on next TX descriptor (wrap ring if necessary) */
      if (ptxd->ctrl & MPC860_FEC_TXBD_CTRL_W) {
         d->fec_xdes_current = d->fec_xdes_start;
      } else {
         d->fec_xdes_current += MPC860_FEC_BD_SIZE;
      }

      /* If this is the last descriptor, we have finished */
      if (!(ptxd->ctrl & MPC860_FEC_TXBD_CTRL_L)) {
         mpc860_fec_fetch_bd(d,d->fec_xdes_current,&ctxd);
         ptxd = &ctxd;
      } else {
         done = TRUE;
      }
   }while(!done);

   if (tot_len != 0) {
#if DEBUG_FEC
      MPC_LOG(d,"FEC: sending packet of %u bytes\n",tot_len);
      mem_dump(d->vm->log_fd,tx_pkt,tot_len);
#endif
      /* send packet on wire */
      netio_send(d->fec_nio,tx_pkt,tot_len);
   }

   /* Clear the Ready bit of the first TX descriptor */
   txd0.ctrl &= ~MPC860_FEC_TXBD_CTRL_R;
   physmem_copy_u16_to_vm(d->vm,txd0.bd_addr+0x00,txd0.ctrl);

   /* Trigger FEC IRQ */
   d->fec_ievent |= MPC860_IEVENT_TFINT | MPC860_IEVENT_TXB;
   mpc860_fec_update_irq_status(d);

   /* link down, report no heartbeat and lost carrier */
   if (!mpc860_fec_link_is_up(d)) {
      ptxd->ctrl |= MPC860_FEC_TXBD_CTRL_HB | MPC860_FEC_TXBD_CTRL_CSL;
      physmem_copy_u16_to_vm(d->vm,ptxd->bd_addr+0x00,ptxd->ctrl);
      d->fec_ievent |= MPC860_IEVENT_HBERR;
      mpc860_fec_update_irq_status(d);
   }

   return(TRUE);
}

/* Handle the TX ring of the FEC (multiple pkts possible) */
static int mpc860_fec_handle_tx_ring(struct mpc860_data *d)
{
   int i;

   for(i=0;i<MPC860_TXRING_PASS_COUNT;i++)
      if (!mpc860_fec_handle_tx_ring_single(d))
         break;

   return(TRUE);
}

/* Handle RX packet for the Fast Ethernet Controller */
static int mpc860_fec_handle_rx_pkt(netio_desc_t *nio,
                                    u_char *pkt,ssize_t pkt_len,
                                    struct mpc860_data *d,void *arg)
{
   n_eth_hdr_t *hdr = (n_eth_hdr_t *)pkt;
   struct mpc860_fec_bd rxd0,crxd,*prxd;
   ssize_t clen,tot_len;
   u_char *pkt_ptr;

   if (!d->fec_rdes_current)
      return(FALSE);

   /* Try to acquire the first descriptor */
   prxd = &rxd0;
   mpc860_fec_fetch_bd(d,d->fec_rdes_current,prxd);

   /* If we don't own the first descriptor, we cannot transmit */
   if (!(rxd0.ctrl & MPC860_FEC_RXBD_CTRL_E))
      return(FALSE);

   pkt_ptr = pkt;
   tot_len = pkt_len;

   while(tot_len > 0) {
      /* Write data into the RX buffer */
      clen = m_min(d->fec_rbuf_size,tot_len);
      physmem_copy_to_vm(d->vm,pkt_ptr,prxd->bp,clen);
      pkt_ptr += clen;
      tot_len -= clen;

      /* Set the Last flag if we have finished */
      if (!tot_len) {
         /* Set the full length */
         physmem_copy_u16_to_vm(d->vm,prxd->bd_addr+0x02,pkt_len+4);
         prxd->ctrl |= MPC860_FEC_RXBD_CTRL_L;
         
         if (eth_addr_is_bcast(&hdr->daddr))
            prxd->ctrl |= MPC860_FEC_RXBD_CTRL_BC;
         else if (eth_addr_is_mcast(&hdr->daddr))
            prxd->ctrl |= MPC860_FEC_RXBD_CTRL_MC;
      } else {
         /* Update the length field */
         physmem_copy_u16_to_vm(d->vm,prxd->bd_addr+0x02,clen);
      }

      /* 
       * Clear the empty bit (except for the first descriptor, 
       * which is cleared when the full packet has been stored).
       */
      if (prxd != &rxd0) {
         prxd->ctrl &= ~MPC860_FEC_RXBD_CTRL_E;
         physmem_copy_u16_to_vm(d->vm,prxd->bd_addr+0x00,prxd->ctrl);
      }

      /* Set pointer on next RX descriptor (wrap ring if necessary) */
      if (prxd->ctrl & MPC860_FEC_RXBD_CTRL_W) {
         d->fec_rdes_current = d->fec_rdes_start;
      } else {
         d->fec_rdes_current += MPC860_FEC_BD_SIZE;
      }

      /* If this is the last descriptor, we have finished */
      if (!tot_len) {
         mpc860_fec_fetch_bd(d,d->fec_rdes_current,&crxd);
         prxd = &crxd;
      }
   }

   /* Clear the Empty bit of the first RX descriptor */
   rxd0.ctrl &= ~MPC860_FEC_RXBD_CTRL_E;
   physmem_copy_u16_to_vm(d->vm,rxd0.bd_addr+0x00,rxd0.ctrl);

   /* Trigger FEC IRQ */
   d->fec_ievent |= MPC860_IEVENT_RFINT | MPC860_IEVENT_RXB;
   mpc860_fec_update_irq_status(d);
   return(TRUE);
}

/* MII update registers */
static void mpc860_fec_mii_update_regs(struct mpc860_data *d)
{
   m_uint16_t *regs = d->fec_mii_regs;
   if (mpc860_fec_link_is_up(d)) {
      /* link up, LX970A_SR_LINKSTATUS is latch down */
      regs[LX970A_CSR] |= LX970A_CSR_LINK;
      if (!(regs[LX970A_CR] & LX970A_CR_ANENABLE)) {
         /* manual selection */
         if ((regs[LX970A_CR] & LX970A_CR_SPEEDSELECT)) {
            /* 100Mbps */
            regs[LX970A_CSR] |= LX970A_CSR_SPEED;
         } else {
            /* 10 Mbps */
            regs[LX970A_CSR] &= ~LX970A_CSR_SPEED;
         }
         if ((regs[LX970A_CR] & LX970A_CR_DUPLEXMODE)) {
            /* full duplex */
            regs[LX970A_CSR] |= LX970A_CSR_DUPLEXMODE;
         } else {
            /* half duplex */
            regs[LX970A_CSR] &= ~LX970A_CSR_DUPLEXMODE;
         }
      } else {
         /* auto-negotiation, assume partner is standard 10/100 eth */
          regs[LX970A_ANLPAR] = (LX970A_ANLPAR_ACKNOWLEDGE |
                                 LX970A_ANLPAR_100TX_FD | LX970A_ANLPAR_100TX_HD |
                                 LX970A_ANLPAR_10T_FD | LX970A_ANLPAR_10T_HD);
         if ((regs[LX970A_ANAR] & LX970A_ANAR_100TX_FD)) {
            /* 100Mbps full duplex */
            regs[LX970A_CSR] |= (LX970A_CSR_DUPLEXMODE | LX970A_CSR_SPEED);
         } else if ((regs[LX970A_ANAR] & LX970A_ANAR_100TX_HD)) {
            /* 100Mbps half duplex */
            regs[LX970A_CSR] = ((regs[LX970A_CSR] & ~LX970A_CSR_DUPLEXMODE) | LX970A_CSR_SPEED);
         } else if ((regs[LX970A_ANAR] & LX970A_ANAR_10T_FD)) {
            /* 10Mbps full duplex */
            regs[LX970A_CSR] = (LX970A_CSR_DUPLEXMODE | (regs[LX970A_CSR] & ~LX970A_CSR_SPEED));
         } else {
            /* 10Mbps half duplex */
            regs[LX970A_ANAR] |= LX970A_ANAR_10T_HD;
            regs[LX970A_CSR] &= ~(LX970A_CSR_DUPLEXMODE | LX970A_CSR_SPEED);
         }
         d->fec_mii_regs[LX970A_CR] &= ~(LX970A_CR_ANRESTART);
         d->fec_mii_regs[LX970A_SR] |= LX970A_SR_ANCOMPLETE;
         d->fec_mii_regs[LX970A_CSR] |= LX970A_CSR_ANCOMPLETE;
      }
   } else {
      /* link down or administratively down */
      d->fec_mii_regs[LX970A_SR] &= ~(LX970A_SR_ANCOMPLETE|LX970A_SR_LINKSTATUS);
      d->fec_mii_regs[LX970A_CSR] &= ~(LX970A_CSR_LINK|LX970A_CSR_DUPLEXMODE|LX970A_CSR_SPEED|LX970A_CSR_ANCOMPLETE);
   }
}

/* MII register defaults */
static void mpc860_fec_mii_defaults(struct mpc860_data *d)
{
   /* default is 100Mb/s full duplex and auto-negotiation */
   memset(d->fec_mii_regs, 0, sizeof(d->fec_mii_regs));
   d->fec_mii_regs[LX970A_CR] = LX970A_CR_DEFAULT;
   d->fec_mii_regs[LX970A_SR] = LX970A_SR_DEFAULT;
   d->fec_mii_regs[LX970A_PIR1] = LX970A_PIR1_DEFAULT;
   d->fec_mii_regs[LX970A_PIR2] = LX970A_PIR2_DEFAULT;
   d->fec_mii_regs[LX970A_ANAR] = LX970A_ANAR_DEFAULT;
   d->fec_mii_regs[LX970A_ANE] = LX970A_ANE_DEFAULT;
   d->fec_mii_regs[LX970A_MR] = LX970A_MR_DEFAULT;
   d->fec_mii_regs[LX970A_IER] = LX970A_IER_DEFAULT;
   d->fec_mii_regs[LX970A_ISR] = LX970A_ISR_DEFAULT;
   d->fec_mii_regs[LX970A_CFGR] = LX970A_CFGR_DEFAULT;
   d->fec_mii_regs[LX970A_CSR] = LX970A_CSR_DEFAULT;

   /* chip is powered up and stable */
   d->fec_mii_regs[LX970A_ISR] = LX970A_ISR_XTALOK;

   mpc860_fec_mii_update_regs(d);
}

/* MII register read access */
static void mpc860_fec_mii_read_access(struct mpc860_data *d,
                                       u_int phy,u_int reg)
{
   m_uint16_t res;

   res = d->fec_mii_regs[reg];

   /* update bits */
   switch (reg) {
      case LX970A_SR:
         /* Latch Low */
         if (mpc860_fec_link_is_up(d)) {
            d->fec_mii_regs[reg] &= LX970A_SR_LINKSTATUS;
         }
         break;
      case LX970A_ISR_MINT:
         if (d->fec_mii_last_read_reg == LX970A_SR) {
            d->fec_mii_regs[reg] &= ~LX970A_ISR_MINT;
         }
      default:
         /* XXX Latch High:
         LX970A_SR_REMOTEFAULT, LX970A_SR_JABBERDETECT
         LX970A_ANE_PDETECTFAULT, LX970A_ANE_PR,
         LX970A_CSR_ANC, LX970A_CSR_PAGERECEIVED */
         break;
   }

#if DEBUG_FEC
   MPC_LOG(d,"FEC: Reading 0x%4.4x (0x%4.4x) from MII phy %d reg %d\n",res,d->fec_mii_regs[reg],phy,reg);
#endif

   d->fec_mii_last_read_reg = reg;
   d->fec_mii_data &= 0xFFFF0000;
   d->fec_mii_data |= res;
}

/* MII register read access */
static void mpc860_fec_mii_write_access(struct mpc860_data *d,
                                        u_int phy,u_int reg)
{
   m_uint16_t data, ro_mask, rw_mask;
   int update_regs = FALSE;

   data = d->fec_mii_data & 0xFFFF;

#if DEBUG_FEC
   MPC_LOG(d,"FEC: Writing 0x%4.4x to MII phy %d reg %d at ia=0x%4.4x,lr=0x%4.4x\n",data,phy,reg,CPU_PPC32(d->vm->boot_cpu)->ia,CPU_PPC32(d->vm->boot_cpu)->lr);
#endif

   switch (reg) {
      case LX970A_CR:
         /* reset, self clearing */
         if ((data & LX970A_CR_RESET)) {
            mpc860_fec_mii_defaults(d);
            return;
         }
         ro_mask = LX970A_CR_RO_MASK;
         rw_mask = LX970A_CR_RW_MASK;
         update_regs = TRUE;
         break;
      case LX970A_ANAR:
         ro_mask = LX970A_ANAR_RO_MASK;
         rw_mask = LX970A_ANAR_RW_MASK;
         break;
      case LX970A_MR:
         ro_mask = LX970A_MR_RO_MASK;
         rw_mask = LX970A_MR_RW_MASK;
         break;
      case LX970A_IER:
         ro_mask = LX970A_IER_RO_MASK;
         rw_mask = LX970A_IER_RW_MASK;
         break;
      case LX970A_CFGR:
         ro_mask = LX970A_CFGR_RO_MASK;
         rw_mask = LX970A_CFGR_RW_MASK;
         break;
      default:
         /* read-only register */
         ro_mask = 0xFFFF;
         rw_mask = 0x0000;
         break;
   }

   d->fec_mii_regs[reg] = (d->fec_mii_regs[reg] & ro_mask) | (data & rw_mask);
   if (update_regs) {
      mpc860_fec_mii_update_regs(d);
   }
}

/* MII register access */
static void mpc860_fec_mii_access(struct mpc860_data *d)
{
   u_int op,phy,reg;

   op =  (d->fec_mii_data & MPC860_MII_OP_MASK)  >> MPC860_MII_OP_SHIFT;
   phy = (d->fec_mii_data & MPC860_MII_PHY_MASK) >> MPC860_MII_PHY_SHIFT;
   reg = (d->fec_mii_data & MPC860_MII_REG_MASK) >> MPC860_MII_REG_SHIFT;

   switch(op) {
      /* MII write */
      case 0x01:   
         mpc860_fec_mii_write_access(d,phy,reg);
         break;

      /* MII read */
      case 0x02:
         mpc860_fec_mii_read_access(d,phy,reg);
         break;

      default:
         MPC_LOG(d,"FEC: unknown MII opcode %u\n",op);
   }

   /* MII access completed */
   d->fec_ievent |= MPC860_IEVENT_MII;
   mpc860_fec_update_irq_status(d);
}

/* 
 * FEC register access (0xE00 to 0xF84).
 */
static int dev_mpc860_fec_access(struct mpc860_data *d,m_uint32_t offset,
                                 u_int op_size,u_int op_type,m_uint64_t *data)
{   
   switch(offset) {
      /* R_DES_START: Beginning of RxBD ring */
      case 0xE10:
         if (op_type == MTS_READ)
            *data = d->fec_rdes_start;
         else
            d->fec_rdes_start = *data & 0xFFFFFFFC;
         break;

      /* X_DES_START: Beginning of TxBD ring */
      case 0xE14:
         if (op_type == MTS_READ)
            *data = d->fec_xdes_start;
         else
            d->fec_xdes_start = *data & 0xFFFFFFFC;
         break;

      /* R_BUFF_SIZE: Receive Buffer Size */
      case 0xE18:
         if (op_type == MTS_READ)
            *data = d->fec_rbuf_size;
         else
            d->fec_rbuf_size = *data & 0x7F0;
         break;

      /* ECNTRL */
      case 0xE40:
         if (op_type == MTS_READ) {
            *data = d->fec_ecntrl;
         } else {
            if (*data & MPC860_ECNTRL_RESET)
               d->fec_ecntrl = 0;
            else {
               if (!(*data & MPC860_ECNTRL_ETHER_EN)) {
                  d->fec_xdes_current = d->fec_xdes_start;
                  d->fec_rdes_current = d->fec_rdes_start;
               }

               d->fec_ecntrl = *data;
            }
         }
         break;

      /* IEVENT: Interrupt Event Register */
      case 0xE44:
         if (op_type == MTS_READ) {
            *data = d->fec_ievent;
         } else {
            d->fec_ievent &= ~(*data);
            mpc860_fec_update_irq_status(d);
         }
         break;

      /* IMASK: Interrupt Mask Register */
      case 0xE48:
         if (op_type == MTS_READ) {
            *data = d->fec_imask;
         } else {
            d->fec_imask = *data;
            mpc860_fec_update_irq_status(d);
         }
         break;

      /* IVEC: Interrupt Vector Register */
      case 0xE4C:
         if (op_type == MTS_READ)
            *data = d->fec_ivec;
         else
            d->fec_ivec = *data;
         break;

      /* X_DES_ACTIVE: TxBD Active Register */
      case 0xE54:
         mpc860_fec_handle_tx_ring(d);
         //printf("x_des_active set\n");
         break;

      /* MII_DATA */
      case 0xE80:
         if (op_type == MTS_READ) {
            *data = d->fec_mii_data;
         } else {
            d->fec_mii_data = *data;
            mpc860_fec_mii_access(d);
         }
         break;
   }

   return(0);
}

/* Set NIO for the Fast Ethernet Controller */
int mpc860_fec_set_nio(struct mpc860_data *d,netio_desc_t *nio)
{
   /* check that a NIO is not already bound */
   if (!d || (d->fec_nio != NULL))
      return(-1);

   d->fec_nio = nio;
   netio_rxl_add(nio,(netio_rx_handler_t)mpc860_fec_handle_rx_pkt,d,NULL);
   mpc860_fec_mii_update_regs(d);
   return(0);
}

/* Unset NIO of the Fast Ethernet Controller */
int mpc860_fec_unset_nio(struct mpc860_data *d)
{
   if (!d)
      return(-1);

   if (d->fec_nio != NULL) {
      netio_rxl_remove(d->fec_nio);
      d->fec_nio = NULL;
      mpc860_fec_mii_update_regs(d);
   }

   return(0);
}

/* ======================================================================== */

#define MPC860_CP_FOP(chan,op) (((chan) << 4) + (op))

/* Execute a command sent through CP Command Register (CPCR) */
static void mpc860_exec_cpcr(struct mpc860_data *d,m_uint32_t cpcr)
{
   u_int channel,opcode,fop;

#if DEBUG_UNKNOWN
   if ((cpcr & 0x1)) {
      /* TODO CP reset command (RST) */
      MPC_LOG(d,"CPCR: CP reset command not implemented: cpcr=0x%4.4x\n",cpcr);
   }
#endif

   channel = (cpcr >> 4) & 0x0F;
   opcode  = (cpcr >> 8) & 0x0F;

   fop = MPC860_CP_FOP(channel,opcode);
   
   switch(fop) {
      /* SPI - Init RX and TX params */
      case MPC860_CP_FOP(MPC860_CHAN_SPI_IDMA2_RT,0):
         mpc860_spi_init_rx_tx_params(d);
         break;

      /* SPI - Init RX params */
      case MPC860_CP_FOP(MPC860_CHAN_SPI_IDMA2_RT,1):
         mpc860_spi_init_rx_params(d);
         break;

      /* SPI - Init TX params */
      case MPC860_CP_FOP(MPC860_CHAN_SPI_IDMA2_RT,2):
         mpc860_spi_init_tx_params(d);
         break;

      /* SCC1 - Init RX and TX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC1,0):
         mpc860_scc_init_rx_tx_params(d,0);
         break;

      /* SCC1 - Init RX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC1,1):
         mpc860_scc_init_rx_params(d,0);
         break;

      /* SCC1 - Init TX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC1,2):
         mpc860_scc_init_tx_params(d,0);
         break;

      /* SCC2 - Init RX and TX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC2,0):
         mpc860_scc_init_rx_tx_params(d,1);
         break;

      /* SCC2 - Init RX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC2,1):
         mpc860_scc_init_rx_params(d,1);
         break;

      /* SCC2 - Init TX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC2,2):
         mpc860_scc_init_tx_params(d,1);
         break;

      /* SCC3 - Init RX and TX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC3,0):
         mpc860_scc_init_rx_tx_params(d,2);
         break;

      /* SCC3 - Init RX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC3,1):
         mpc860_scc_init_rx_params(d,2);
         break;

      /* SCC3 - Init TX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC3,2):
         mpc860_scc_init_tx_params(d,2);
         break;

      /* SCC4 - Init RX and TX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC4,0):
         mpc860_scc_init_rx_tx_params(d,3);
         break;

      /* SCC4 - Init RX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC4,1):
         mpc860_scc_init_rx_params(d,3);
         break;

      /* SCC4 - Init TX params */
      case MPC860_CP_FOP(MPC860_CHAN_SCC4,2):
         mpc860_scc_init_tx_params(d,3);
         break;

      default:
         MPC_LOG(d,"CPCR: unknown cmd: channel=0x%4.4x, opcode=0x%4.4x\n",
                 channel,opcode);
   }
}

/*
 * dev_mpc860_access()
 */
void *dev_mpc860_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                        u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct mpc860_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0x0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->name,
              "read from offset 0x%x, pc=0x%llx (size=%u)\n",
              offset,cpu_get_pc(cpu),op_size);
   } else {
      cpu_log(cpu,d->name,
              "write to offset 0x%x, value=0x%llx, pc=0x%llx (size=%u)\n",
              offset,*data,cpu_get_pc(cpu),op_size);
   }
#endif

   /* Handle dual-port RAM access */
   if ((offset >= MPC860_DPRAM_OFFSET) && (offset < MPC860_DPRAM_END))
      return(d->dpram + (offset - MPC860_DPRAM_OFFSET));

   /* Handle SCC channels */
   if ((offset >= MPC860_REG_SCC_BASE) &&
       (offset < (MPC860_REG_SCC_BASE + (4 * 0x20)))) 
   {
      dev_mpc860_scc_access(d,offset,op_size,op_type,data);
      return NULL;
   }
         
   /* Handle Fast Ethernet Controller (FEC) registers */
   if ((offset >= MPC860_REG_FEC_BASE) && (offset <= MPC860_REG_FEC_END))
   {
      dev_mpc860_fec_access(d,offset,op_size,op_type,data);
      return NULL;
   }

   switch(offset) {
      /* SWSR - Software Service Register (Watchdog) */
      case MPC860_REG_SWSR:
         break;

      /* SIU Interrupt Pending Register */
      case MPC860_REG_SIPEND:
         if (op_type == MTS_READ)
            *data = d->sipend;
         break;

      /* SIU Interrupt Mask Register */
      case MPC860_REG_SIMASK:
         if (op_type == MTS_READ) {
            *data = d->simask;
         } else {
            d->simask = *data;
            mpc860_update_irq_status(d);
         }
         break;

      /* 
       * Cisco 2600:
       *   Bit 30: 0=NM in slot 1
       */
      case MPC860_REG_PIPR:
         if (op_type == MTS_READ)
            *data = 0x3F00F600;
         break;

      /* PISCR - Periodic Interrupt Status and Control Register */
      case MPC860_REG_PISCR:
        if (op_type == MTS_WRITE) {
           if (*data & 0x80) {
              d->sipend &= ~0x40000000;
              mpc860_update_irq_status(d);
           }
        }
        break;

      case MPC860_REG_TBSCR:
         if (op_type == MTS_READ)
            *data = 0x45;
         break;

      /* IDMA1 Status and Mask Registers */
      case MPC860_REG_IDSR1:
         if (op_type == MTS_READ) {
            *data = d->idsr[0];
         } else {
            d->idsr[0] &= ~(*data);
         }
         break;

      case MPC860_REG_IDMR1:
         if (op_type == MTS_READ)
            *data = d->idmr[0];
         else
            d->idmr[0] = *data;
         break;

      /* IDMA2 Status and Mask Registers */
      case MPC860_REG_IDSR2:
         if (op_type == MTS_READ)
            *data = d->idsr[1];
         else
            d->idsr[1] &= ~(*data);
         break;

      case MPC860_REG_IDMR2:
         if (op_type == MTS_READ)
            *data = d->idmr[1];
         else
            d->idmr[1] = *data;
         break;

      /* CICR - CPM Interrupt Configuration Register */
      case MPC860_REG_CICR:
         if (op_type == MTS_READ)
            *data = d->cicr;
         else
            d->cicr = *data;         
         break;

      /* CIPR - CPM Interrupt Pending Register */
      case MPC860_REG_CIPR:
         if (op_type == MTS_READ)
            *data = d->cipr;
         else {
            d->cipr &= ~(*data);
            mpc860_update_cpm_int_status(d);
         }
         break;

      /* CIMR - CPM Interrupt Mask Register */
      case MPC860_REG_CIMR:
         if (op_type == MTS_READ)
            *data = d->cimr;
         else {
            d->cimr = *data;
            mpc860_update_cpm_int_status(d);
         }
         break;

      /* PCSO - Port C Special Options Register */
      case MPC860_REG_PCSO:
         if (op_type == MTS_WRITE) {
            if (*data & 0x01) {
#if DEBUG_IDMA
               MPC_LOG(d,"activating IDMA0\n");
#endif
               mpc860_idma_start_channel(d,0);
            }
         }
         break;

      /* PCDAT - Port C Data Register */
      case MPC860_REG_PCDAT:
         if (op_type == MTS_WRITE)
            d->pcdat = *data;
         else
            *data = d->pcdat;
         break;

      /* PBDAT - Port B Data Register */
      case MPC860_REG_PBDAT:
         if (op_type == MTS_WRITE)
            d->pbdat = *data;
         else
            *data = d->pbdat;
         break;

      /* CPCR - CP Command Register */
      case MPC860_REG_CPCR:
         if (op_type == MTS_WRITE)
            mpc860_exec_cpcr(d,(m_uint32_t)(*data));
         break;

      /* SPCOM - SPI Command Register */
      case MPC860_REG_SPCOM:
         if ((op_type == MTS_WRITE) && (*data & MPC860_SPCOM_STR))
            mpc860_spi_start_tx(d);
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->name,
                    "read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,d->name,
                    "write to addr 0x%x, value=0x%llx, pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   return NULL;
}

/* Set IRQ pending status */
void mpc860_set_pending_irq(struct mpc860_data *d,m_uint32_t val)
{
   d->sipend |= 1 << val;
   mpc860_update_irq_status(d);
}

/* Clear a pending IRQ */
void mpc860_clear_pending_irq(struct mpc860_data *d,m_uint32_t val)
{
   d->sipend &= ~(1 << val);
   mpc860_update_irq_status(d);
}

/* Shutdown the MPC860 device */
void dev_mpc860_shutdown(vm_instance_t *vm,struct mpc860_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/* Create the MPC860 device */
int dev_mpc860_init(vm_instance_t *vm,char *name,
                    m_uint64_t paddr,m_uint32_t len)
{
   struct mpc860_data *d;

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"mpc860: unable to create device data.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->name = name;
   d->vm = vm;

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_mpc860_shutdown;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_mpc860_access;

   /* Set the default SPI base address */
   dpram_w16(d,MPC860_SPI_BASE_ADDR,MPC860_SPI_BASE);

   /* Set MII register defaults */
   mpc860_fec_mii_defaults(d);

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}

