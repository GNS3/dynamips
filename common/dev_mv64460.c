/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Marvell MV64460 system controller.
 *
 * Based on GT9100 documentation and Linux kernel sources.
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
#include "dev_vtty.h"
#include "dev_mv64460.h"

/* Debugging flags */
#define DEBUG_ACCESS       0
#define DEBUG_UNKNOWN      0
#define DEBUG_DMA          0
#define DEBUG_MII          0
#define DEBUG_ETH_UNKNOWN  1
#define DEBUG_ETH_TX       1
#define DEBUG_ETH_RX       1
#define DEBUG_ETH_HASH     0

/* PCI identification */
#define PCI_VENDOR_MARVELL           0x11ab  /* Marvell/Galileo */
#define PCI_PRODUCT_MARVELL_MV64460  0x6485  /* MV-64460 */

/* FIXME */
#define MV64460_MAX_PKT_SIZE  2048

/* Interrupt Low Main Cause Register */
#define MV64460_REG_ILMCR   0x0004

#define MV64460_ILMCR_IDMA0_COMP         0x00000010  /* IDMA 0 */ 
#define MV64460_ILMCR_IDMA1_COMP         0x00000020  /* IDMA 1 */ 
#define MV64460_ILMCR_IDMA2_COMP         0x00000040  /* IDMA 2 */ 
#define MV64460_ILMCR_IDMA3_COMP         0x00000080  /* IDMA 3 */ 
#define MV64460_ILMCR_TIMER0_EXP         0x00000100  /* Timer 0 */
#define MV64460_ILMCR_TIMER1_EXP         0x00000200  /* Timer 1 */
#define MV64460_ILMCR_TIMER2_EXP         0x00000400  /* Timer 2 */
#define MV64460_ILMCR_TIMER3_EXP         0x00000800  /* Timer 3 */
#define MV64460_ILMCR_P1_GPP_0_7_SUM     0x01000000  /* GPP 0-7 (CPU1) */
#define MV64460_ILMCR_P1_GPP_8_15_SUM    0x02000000  /* GPP 8-15 (CPU1) */
#define MV64460_ILMCR_P1_GPP_16_23_SUM   0x04000000  /* GPP 16-23 (CPU1) */
#define MV64460_ILMCR_P1_GPP_24_31_SUM   0x08000000  /* GPP 24-31 (CPU1) */

/* Interrupt High Main Cause Register */
#define MV64460_REG_IHMCR   0x000c

#define MV64460_IHMCR_ETH0_SUM           0x00000001  /* Ethernet0 summary */
#define MV64460_IHMCR_ETH1_SUM           0x00000002  /* Ethernet1 summary */
#define MV64460_IHMCR_ETH2_SUM           0x00000004  /* Ethernet2 summary */
#define MV64460_IHMCR_SDMA0_SUM          0x00000010  /* Serial DMA0 */
#define MV64460_IHMCR_SDMA1_SUM          0x00000040  /* Serial DMA1 */
#define MV64460_IHMCR_MPSC0_SUM          0x00000100  /* MPSC0 */
#define MV64460_IHMCR_MPSC1_SUM          0x00000200  /* MPSC1 */
#define MV64460_IHMCR_ETH_RX_SUM(i)      (0x00000400 << ((i) * 3))
#define MV64460_IHMCR_ETH_TX_SUM(i)      (0x00000800 << ((i) * 3))
#define MV64460_IHMCR_ETH_MISC_SUM(i)    (0x00001000 << ((i) * 3))
#define MV64460_IHMCR_P0_GPP_0_7_SUM     0x01000000  /* GPP 0-7 (CPU0) */
#define MV64460_IHMCR_P0_GPP_8_15_SUM    0x02000000  /* GPP 8-15 (CPU0) */
#define MV64460_IHMCR_P0_GPP_16_23_SUM   0x04000000  /* GPP 16-23 (CPU0) */
#define MV64460_IHMCR_P0_GPP_24_31_SUM   0x08000000  /* GPP 24-31 (CPU0) */

/* Interrupt registers: CPU_INTn[0], CPU_INTn[1], INT0n, INT1n */
#define MV64460_REG_CPU_INTN0_MASK_LO    0x0014
#define MV64460_REG_CPU_INTN0_MASK_HI    0x001c
#define MV64460_REG_CPU_INTN0_SEL_CAUSE  0x0024

#define MV64460_REG_CPU_INTN1_MASK_LO    0x0034
#define MV64460_REG_CPU_INTN1_MASK_HI    0x003c
#define MV64460_REG_CPU_INTN1_SEL_CAUSE  0x0044

#define MV64460_REG_INT0N_MASK_LO        0x0054
#define MV64460_REG_INT0N_MASK_HI        0x005c
#define MV64460_REG_INT0N_SEL_CAUSE      0x0064

#define MV64460_REG_INT1N_MASK_LO        0x0074
#define MV64460_REG_INT1N_MASK_HI        0x007c
#define MV64460_REG_INT1N_SEL_CAUSE      0x0084

#define MV64460_IC_CAUSE_MASK   0x3FFFFFFF   /* Shadow of cause register */
#define MV64460_IC_CAUSE_SEL    0x40000000   /* Low or High register */
#define MV64460_IC_CAUSE_STAT   0x80000000   /* Interrupt status */

/* GPP Interrupt cause and mask registers */
#define MV64460_REG_GPP_INTR_CAUSE   0xf108
#define MV64460_REG_GPP_INTR_MASK0   0xf10c
#define MV64460_REG_GPP_INTR_MASK1   0xf114
#define MV64460_REG_GPP_VALUE_SET    0xf118
#define MV64460_REG_GPP_VALUE_CLEAR  0xf11c

/* TX Descriptor */
#define MV64460_TXDESC_OWN        0x80000000  /* Ownership */
#define MV64460_TXDESC_AM         0x40000000  /* Auto-mode */
#define MV64460_TXDESC_EI         0x00800000  /* Enable Interrupt */
#define MV64460_TXDESC_GC         0x00400000  /* Generate CRC */
#define MV64460_TXDESC_P          0x00040000  /* Padding */
#define MV64460_TXDESC_F          0x00020000  /* First buffer of packet */
#define MV64460_TXDESC_L          0x00010000  /* Last buffer of packet */
#define MV64460_TXDESC_ES         0x00008000  /* Error Summary */
#define MV64460_TXDESC_RC         0x00003c00  /* Retransmit Count */
#define MV64460_TXDESC_COL        0x00000200  /* Collision */
#define MV64460_TXDESC_RL         0x00000100  /* Retransmit Limit Error */
#define MV64460_TXDESC_UR         0x00000040  /* Underrun Error */
#define MV64460_TXDESC_LC         0x00000020  /* Late Collision Error */

#define MV64460_TXDESC_BC_MASK    0xFFFF0000  /* Number of bytes to transmit */
#define MV64460_TXDESC_BC_SHIFT   16

/* RX Descriptor */
#define MV64460_RXDESC_OWN        0x80000000  /* Ownership */
#define MV64460_RXDESC_AM         0x40000000  /* Auto-mode */
#define MV64460_RXDESC_EI         0x00800000  /* Enable Interrupt */
#define MV64460_RXDESC_F          0x00020000  /* First buffer of packet */
#define MV64460_RXDESC_L          0x00010000  /* Last buffer of packet */
#define MV64460_RXDESC_ES         0x00008000  /* Error Summary */
#define MV64460_RXDESC_IGMP       0x00004000  /* IGMP packet detected */
#define MV64460_RXDESC_HE         0x00002000  /* Hash Table Expired */
#define MV64460_RXDESC_M          0x00001000  /* Missed Frame */
#define MV64460_RXDESC_FT         0x00000800  /* Frame Type (802.3/Ethernet) */
#define MV64460_RXDESC_SF         0x00000100  /* Short Frame Error */
#define MV64460_RXDESC_MFL        0x00000080  /* Maximum Frame Length Error */
#define MV64460_RXDESC_OR         0x00000040  /* Overrun Error */
#define MV64460_RXDESC_COL        0x00000010  /* Collision */
#define MV64460_RXDESC_CE         0x00000001  /* CRC Error */

#define MV64460_RXDESC_BC_MASK    0x0000FFFF  /* Byte count */
#define MV64460_RXDESC_BS_MASK    0xFFFF0000  /* Buffer size */
#define MV64460_RXDESC_BS_SHIFT   16

/* === IDMA =============================================================== */

#define MV64460_IDMA_CHANNELS   4

/* Address decoding registers */
#define MV64460_IDMA_BAR_REGS   8  /* 8 BAR registers */
#define MV64460_IDMA_HAR_REGS   4  /* High address remap for BAR0-3 */

#define MV64460_REG_IDMA_DEC_BASE   0x0A00
#define MV64460_REG_IDMA_BAR(x)     (0x0A00 + ((x) << 3))
#define MV64460_REG_IDMA_SR(x)      (0x0A04 + ((x) << 3))
#define MV64460_REG_IDMA_HAR(x)     (0x0A60 + ((x) << 2))
#define MV64460_REG_IDMA_CAP(x)     (0x0A70 + ((x) << 2))
#define MV64460_REG_IDMA_BARE       0x0A80

/* Channel registers */
#define MV64460_REG_IDMA_BASE       0x0800
#define MV64460_REG_IDMA_BC(x)      (0x0800 + ((x) << 2))
#define MV64460_REG_IDMA_SRC(x)     (0x0810 + ((x) << 2))
#define MV64460_REG_IDMA_DST(x)     (0x0820 + ((x) << 2))
#define MV64460_REG_IDMA_NDP(x)     (0x0830 + ((x) << 2))
#define MV64460_REG_IDMA_CDP(x)     (0x0870 + ((x) << 2))
#define MV64460_REG_IDMA_CTRL_LO(x) (0x0840 + ((x) << 2))
#define MV64460_REG_IDMA_CTRL_HI(x) (0x0880 + ((x) << 2))

/* Interrupt registers */
#define MV64460_REG_IDMA_IC   0x08c0
#define MV64460_REG_IDMA_IM   0x08c4

/* Galileo IDMA channel */
struct idma_channel {
   m_uint32_t byte_count;
   m_uint32_t src_addr;
   m_uint32_t dst_addr;
   m_uint32_t cdptr;
   m_uint32_t nrptr;
   m_uint32_t ctrl_lo,ctrl_hi;
   m_uint32_t cap;
};

/* === Serial DMA (SDMA) ================================================== */

/* SDMA - number of channels */
#define MV64460_SDMA_CHANNELS     2

/* SDMA channel */
struct sdma_channel {
   u_int id;
   
   m_uint32_t sdc;
   m_uint32_t sdcm;
   m_uint32_t rx_desc;
   m_uint32_t rx_buf_ptr;
   m_uint32_t scrdp;
   m_uint32_t tx_desc;
   m_uint32_t sctdp;
   m_uint32_t sftdp;
};

/* SDMA registers base offsets */
#define MV64460_REG_SDMA0   0x4000
#define MV64460_REG_SDMA1   0x6000

/* SDMA cause and mask registers */
#define MV64460_REG_SDMA_CAUSE     0xb800
#define MV64460_REG_SDMA_MASK      0xb880

#define MV64460_SDMA_CAUSE_SDMA0   0x0000000F
#define MV64460_SDMA_CAUSE_SDMA1   0x00000F00

#define MV64460_SDMA_CAUSE_RXBUF0  0x00000001  /* RX Buffer returned */
#define MV64460_SDMA_CAUSE_RXERR0  0x00000002  /* RX Error */
#define MV64460_SDMA_CAUSE_TXBUF0  0x00000004  /* TX Buffer returned */
#define MV64460_SDMA_CAUSE_TXEND0  0x00000008  /* TX End */
#define MV64460_SDMA_CAUSE_RXBUF1  0x00000100  /* RX Buffer returned */
#define MV64460_SDMA_CAUSE_RXERR1  0x00000200  /* RX Error */
#define MV64460_SDMA_CAUSE_TXBUF1  0x00000400  /* TX Buffer returned */
#define MV64460_SDMA_CAUSE_TXEND1  0x00000800  /* TX End */

/* SDMA register offsets (per channel) */
#define MV64460_SDMA_SDC          0x0000  /* Configuration Register */
#define MV64460_SDMA_SDCM         0x0008  /* Command Register */
#define MV64460_SDMA_RX_DESC      0x0800  /* RX descriptor */
#define MV64460_SDMA_SCRDP        0x0810  /* Current RX descriptor */
#define MV64460_SDMA_TX_DESC      0x0c00  /* TX descriptor */
#define MV64460_SDMA_SCTDP        0x0c10  /* Current TX desc. pointer */
#define MV64460_SDMA_SFTDP        0x0c14  /* First TX desc. pointer */

/* SDCR: SDMA Configuration Register */
#define MV64460_SDCR_RFT      0x00000001    /* Receive FIFO Threshold */
#define MV64460_SDCR_SFM      0x00000002    /* Single Frame Mode */
#define MV64460_SDCR_RC       0x0000003c    /* Retransmit count */
#define MV64460_SDCR_BLMR     0x00000040    /* Big/Little Endian RX mode */
#define MV64460_SDCR_BLMT     0x00000080    /* Big/Litlle Endian TX mode */
#define MV64460_SDCR_POVR     0x00000100    /* PCI override */
#define MV64460_SDCR_RIFB     0x00000200    /* RX IRQ on frame boundary */
#define MV64460_SDCR_BSZ      0x00003000    /* Burst size */

/* SDCMR: SDMA Command Register */
#define MV64460_SDCMR_ERD     0x00000080         /* Enable RX DMA */
#define MV64460_SDCMR_AR      0x00008000         /* Abort Receive */
#define MV64460_SDCMR_STD     0x00010000         /* Stop TX */
#define MV64460_SDCMR_STDH    MV64460_SDCMR_STD  /* Stop TX High */
#define MV64460_SDCMR_STDL    0x00020000         /* Stop TX Low */
#define MV64460_SDCMR_TXD     0x00800000         /* TX Demand */
#define MV64460_SDCMR_TXDH    MV64460_SDCMR_TXD  /* Start TX High */
#define MV64460_SDCMR_TXDL    0x01000000         /* Start TX Low */
#define MV64460_SDCMR_AT      0x80000000         /* Abort Transmit */

/* SDMA RX/TX descriptor */
struct sdma_desc {
   m_uint32_t buf_size;
   m_uint32_t cmd_stat;
   m_uint32_t next_ptr;
   m_uint32_t buf_ptr;
};

/* SDMA Descriptor Command/Status word */
#define MV64460_SDMA_CMD_O    0x80000000  /* Owner bit */
#define MV64460_SDMA_CMD_AM   0x40000000  /* Auto-mode */
#define MV64460_SDMA_CMD_EI   0x00800000  /* Enable Interrupt */
#define MV64460_SDMA_CMD_F    0x00020000  /* First buffer */
#define MV64460_SDMA_CMD_L    0x00010000  /* Last buffer */

/* === MultiProtocol Serial Controller (MPSC) ============================= */

/* 2 MPSC channels */
#define MV64460_MPSC_CHANNELS  2

/* MPSC channel */
struct mpsc_channel {
   m_uint32_t mmcrl;
   m_uint32_t mmcrh;
   m_uint32_t mpcr;
   m_uint32_t chr[10];

   vtty_t *vtty;
   netio_desc_t *nio;
};

/* MPSC registers base offsets */
#define MV64460_REG_MPSC0     0x8000
#define MV64460_REG_MPSC1     0x9000

#define MV64460_MPSC_MMCRL    0x0000      /* Main Config Register Low */
#define MV64460_MPSC_MMCRH    0x0004      /* Main Config Register High */
#define MV64460_MPSC_MPCR     0x0008      /* Protocol Config Register */
#define MV64460_MPSC_CHR1     0x000C
#define MV64460_MPSC_CHR2     0x0010
#define MV64460_MPSC_CHR3     0x0014
#define MV64460_MPSC_CHR4     0x0018
#define MV64460_MPSC_CHR5     0x001C
#define MV64460_MPSC_CHR6     0x0020
#define MV64460_MPSC_CHR7     0x0024
#define MV64460_MPSC_CHR8     0x0028
#define MV64460_MPSC_CHR9     0x002C
#define MV64460_MPSC_CHR10    0x0030

#define MV64460_MMCRL_MODE_MASK   0x0000007

#define MV64460_MPSC_MODE_HDLC    0
#define MV64460_MPSC_MODE_UART    4
#define MV64460_MPSC_MODE_BISYNC  5

/* === Gigabit Ethernet Ports ============================================= */
#define MV64460_ETH_PORTS     3

#define MV64460_ETH_RX_QUEUES  8
#define MV64460_ETH_TX_QUEUES  8

#define MV64460_REG_ETH_START  0x2000
#define MV64460_REG_ETH_END    0x4000

/* Base Address and Size Registers */
#define MV64460_ETH_WINDOWS 6
#define MV64460_REG_ETH_BARE    0x2290   /* Base Address Enable */
#define MV64460_REG_ETH_BA(i)   (0x2200 + ((i) << 3))
#define MV64460_REG_ETH_SR(i)   (0x2204 + ((i) << 3))

/* Port access protect */
#define MV64460_REG_ETH_EPAP(i) (0x2294 + ((i) << 2))

/* Default PHY addresses */
#define MV64460_ETH_PHY0_ADDR   0x08
#define MV64460_ETH_PHY1_ADDR   0x09
#define MV64460_ETH_PHY2_ADDR   0x0A

/* SMI register */
#define MV64460_ETH_SMI_DATA_MASK      0x0000FFFF
#define MV64460_ETH_SMI_PHYAD_MASK     0x001F0000  /* PHY Device Address */
#define MV64460_ETH_SMI_PHYAD_SHIFT    16
#define MV64460_ETH_SMI_REGAD_MASK     0x03e00000  /* PHY Device Reg Addr */
#define MV64460_ETH_SMI_REGAD_SHIFT    21
#define MV64460_ETH_SMI_OPCODE_MASK    0x04000000  /* Op (0: write, 1: read) */
#define MV64460_ETH_SMI_OPCODE_READ    0x04000000
#define MV64460_ETH_SMI_RVALID_FLAG    0x08000000  /* Read Valid */
#define MV64460_ETH_SMI_BUSY_FLAG      0x10000000  /* Busy: 1=in progress */

#define MV64460_REG_ETH_PHY_ADDR    0x2000
#define MV64460_REG_ETH_SMI         0x2004

/* Port registers */
#define MV64460_REG_ETH_PCR      0x2400  /* Port Configuration */
#define MV64460_REG_ETH_PCXR     0x2404  /* Port Configuration Extend */
#define MV64460_REG_ETH_EVLANE   0x2410  /* VLAN Ethertype */
#define MV64460_REG_ETH_MACAL    0x2414  /* MAC Address Low */
#define MV64460_REG_ETH_MACAH    0x2418  /* MAC Address High */
#define MV64460_REG_ETH_SDCR     0x241c  /* SDMA Configuration */
#define MV64460_REG_ETH_TQC      0x2448  /* Transmit Queue Command */
#define MV64460_REG_ETH_IC       0x2460  /* Port Interrupt Cause */
#define MV64460_REG_ETH_ICE      0x2464  /* Port Interrupt Cause Extend */
#define MV64460_REG_ETH_PIM      0x2468  /* Port Interrupt Mask */
#define MV64460_REG_ETH_PEIM     0x246c  /* Port Extend Interrupt Mask */
#define MV64460_REG_ETH_RQC      0x2680  /* Receive Queue Command */

#define MV64460_REG_ETH_CRDP(i)  (0x260c + ((i) << 4))
#define MV64460_REG_ETH_TCQDP(i) (0x26c0 + ((i) << 2))

/* Transmit Command Queue */
#define MV64460_ETH_TQC_ENQ(i)   (0x0001 << (i))
#define MV64460_ETH_TQC_DISQ(i)  (0x0100 << (i))

/* Receive Command Queue */
#define MV64460_ETH_RQC_ENQ(i)   (0x0001 << (i))
#define MV64460_ETH_RQC_DISQ(i)  (0x0100 << (i))

/* MIB registers */
#define MV64460_REG_ETH_MIB_GOOD_RX_BYTES   0x3000
#define MV64460_REG_ETH_MIB_GOOD_RX_FRAMES  0x3010
#define MV64460_REG_ETH_MIB_GOOD_TX_BYTES   0x3038
#define MV64460_REG_ETH_MIB_GOOD_TX_FRAMES  0x3040

/* Port Configuration Register (PCR) */
#define MV64460_ETH_PCR_UPM             0x00000001  /* Promiscuous */
#define MV64460_ETH_PCR_RXQ_MASK        0x0000000E  /* Defaut RX queue */
#define MV64460_ETH_PCR_RXQ_SHIFT       1
#define MV64460_ETH_PCR_RXQ_ARP_MASK    0x00000070  /* Defaut RX queue */
#define MV64460_ETH_PCR_RXQ_ARP_SHIFT   4
#define MV64460_ETH_PCR_RB              0x00000080  /* Reject Broadcast */
#define MV64460_ETH_PCR_RBIP            0x00000100  /* Reject IP Broadcast */
#define MV64460_ETH_PCR_RBARP           0x00000200  /* Reject ARP Broadcast */
#define MV64460_ETH_PCR_TCP_CAPEN       0x00004000  /* TCP capture enable  */
#define MV64460_ETH_PCR_UDP_CAPEN       0x00008000  /* TCP capture enable  */
#define MV64460_ETH_PCR_TCPQ_MASK       0x00070000  /* TCP Queue */
#define MV64460_ETH_PCR_TCPQ_SHIFT      16
#define MV64460_ETH_PCR_UDPQ_MASK       0x00380000  /* UDP Queue */
#define MV64460_ETH_PCR_UDPQ_SHIFT      19
#define MV64460_ETH_PCR_BPDUQ_MASK      0x01C00000  /* UDP Queue */
#define MV64460_ETH_PCR_BPDUQ_SHIFT     22
#define MV64460_ETH_PCR_RXCS            0x02000000  /* RX TCP Checksum mode */

/* Summary masks */
#define MV64460_ETH_IC_SUM_MASK      0x7FFFFFFF
#define MV64460_ETH_ICE_SUM_MASK     0x7FFFFFFF

/* RX, TX and Misc masks */
#define MV64460_ETH_INT_MISC_ICE_MASK   0x00F10000
#define MV64460_ETH_INT_RX_IC_MASK      0x0007FFFC
#define MV64460_ETH_INT_RX_ICE_MASK     0x00060000
#define MV64460_ETH_INT_TX_IC_MASK      0x7FF80000
#define MV64460_ETH_INT_TX_ICE_MASK     0x0008FFFF

/* Port Interrupt Cause */
#define MV64460_ETH_IC_RXBUF      0x00000001
#define MV64460_ETH_IC_EXTEND     0x00000002
#define MV64460_ETH_IC_RXBUFQ(i)  (0x00000004 << (i))
#define MV64460_ETH_IC_RXERR      0x00000400
#define MV64460_ETH_IC_RXERRQ(i)  (0x00000800 << (i))
#define MV64460_ETH_IC_TXEND(i)   (0x00080000 << (i))
#define MV64460_ETH_IC_SUM        0x80000000

/* Port Interrupt Cause Extend */
#define MV64460_ETH_ICE_TXBUF(i)  (0x00000001 << (i))
#define MV64460_ETH_ICE_TXERR(i)  (0x00000100 << (i))
#define MV64460_ETH_ICE_PHY_STC   0x00010000
#define MV64460_ETH_ICE_RXOVR     0x00040000
#define MV64460_ETH_ICE_TXUDR     0x00080000
#define MV64460_ETH_ICE_LNKC      0x00100000
#define MV64460_ETH_ICE_PART      0x00200000
#define MV64460_ETH_ICE_ANDONE    0x00400000
#define MV64460_ETH_ICE_IAERR     0x00800000
#define MV64460_ETH_ICE_SUM       0x80000000

/* Ethernet TX Descriptor */
#define MV64460_ETH_TXDESC_ES            0x00000001  /* Error summary */
#define MV64460_ETH_TXDESC_LLC           0x00000200  /* LLC/SNAP */
#define MV64460_ETH_TXDESC_L4CHK_MODE    0x00000400  /* L4 Cksum Mode */
#define MV64460_ETH_TXDESC_IPHLEN_MASK   0x00007800  /* IP Header Length */
#define MV64460_ETH_TXDESC_IPHLEN_SHIFT  11
#define MV64460_ETH_TXDESC_VLAN          0x00008000  /* VLAN tag */
#define MV64460_ETH_TXDESC_L4_TYPE       0x00010000  /* L4 Cksum Mode */
#define MV64460_ETH_TXDESC_GL4CHK        0x00020000  /* TCP/UDP checksum */
#define MV64460_ETH_TXDESC_GIPCHK        0x00040000  /* IP checksum */
#define MV64460_ETH_TXDESC_P             0x00080000  /* Padding */
#define MV64460_ETH_TXDESC_L             0x00100000  /* Last buffer */
#define MV64460_ETH_TXDESC_F             0x00200000  /* First buffer */
#define MV64460_ETH_TXDESC_GC            0x00400000  /* Ethernet CRC */
#define MV64460_ETH_TXDESC_EI            0x00800000  /* Enable Interrupt */
#define MV64460_ETH_TXDESC_AM            0x40000000  /* Auto mode */
#define MV64460_ETH_TXDESC_OWN           0x80000000  /* OWNer (1=DMA,0=CPU) */

#define MV64460_ETH_TXDESC_BC_MASK       0xFFFF0000  /* TX Byte Count */
#define MV64460_ETH_TXDESC_BC_SHIFT      16

/* Ethernet RX Descriptor */
#define MV64460_ETH_RXDESC_ES            0x00000001  /* Error summary */
#define MV64460_ETH_RXDESC_L4CHK_MASK    0x0007FFF8  /* L4 Checksum */
#define MV64460_ETH_RXDESC_L4CHK_SHIFT   3
#define MV64460_ETH_RXDESC_VLAN          0x00080000  /* VLAN tag */
#define MV64460_ETH_RXDESC_BPDU          0x00100000  /* BPDU */
#define MV64460_ETH_RXDESC_L4P_MASK      0x00600000  /* L4 Protocol */
#define MV64460_ETH_RXDESC_L4P_SHIFT     21
#define MV64460_ETH_RXDESC_L2V2          0x00800000  /* Layer 2 - EthV2 */
#define MV64460_ETH_RXDESC_L3IP          0x01000000  /* Layer 3 - IP */
#define MV64460_ETH_RXDESC_IPH_OK        0x02000000  /* IP Header OK */
#define MV64460_ETH_RXDESC_L             0x04000000  /* Last Buffer */
#define MV64460_ETH_RXDESC_F             0x08000000  /* First Buffer */
#define MV64460_ETH_RXDESC_U             0x10000000  /* Unknown Dst Addr */
#define MV64460_ETH_RXDESC_EI            0x20000000  /* Enable Interrupt */
#define MV64460_ETH_RXDESC_L4CHK_OK      0x40000000  /* L4 Cksum OK */
#define MV64460_ETH_RXDESC_OWN           0x80000000  /* OWNer (1=DMA,0=CPU) */

#define MV64460_ETH_RXDESC_L4P_TCP       0x00
#define MV64460_ETH_RXDESC_L4P_UDP       0x01
#define MV64460_ETH_RXDESC_L4P_OTHER     0x02

#define MV64460_ETH_RXDESC_BC_MASK    0xFFFF0000  /* Byte count */
#define MV64460_ETH_RXDESC_BC_SHIFT   16
#define MV64460_ETH_RXDESC_BS_MASK    0x0000FFF8  /* Buffer size */
#define MV64460_ETH_RXDESC_BS_SHIFT   0

#define MV64460_ETH_RXDESC_IPV4_FRG   0x00000004  /* IPv4 fragmented */

/* Ethernet port */
struct eth_port {
   u_int id;

   netio_desc_t *nio;
   m_uint32_t pcr,pcxr,sdcr;
   m_uint16_t vlan_ether_type;

   /* MAC Address */
   n_eth_addr_t mac_addr;

   /* RX and TX Queues */
   m_uint32_t rqc,tqc;
   m_uint32_t crdp[MV64460_ETH_RX_QUEUES];
   m_uint32_t tcqdp[MV64460_ETH_TX_QUEUES];

   /* Interrupt Registers */
   m_uint32_t pic,pice,pim,peim;

   /* MIB counters */
   m_uint64_t mib_good_rx_bytes;
   m_uint32_t mib_good_rx_frames;
   m_uint64_t mib_good_tx_bytes;
   m_uint32_t mib_good_tx_frames;
};

/* === Integrated SRAM ==================================================== */
#define MV64460_SRAM_WIDTH    18
#define MV64460_SRAM_SIZE     (1 << MV64460_SRAM_WIDTH)

#define MV64460_REG_SRAM_BASE    0x268

/* SRAM base register */
#define MV64460_SRAM_BASE_MASK   0x000FFFFC
#define MV64460_SRAM_BASE_SHIFT  2

/* ======================================================================== */

/* MV64460 system controller private data */
struct mv64460_data {
   char *name;
   vm_obj_t vm_obj;
   struct vdevice dev;
   struct pci_device *pci_dev;
   vm_instance_t *vm;
   pthread_mutex_t lock;

   /* Interrupt controller registers */
   m_uint32_t intr_lo,intr_hi;
   m_uint32_t cpu_intn0_mask_lo,cpu_intn0_mask_hi;
   m_uint32_t cpu_intn1_mask_lo,cpu_intn1_mask_hi;
   m_uint32_t int0n_mask_lo,int0n_mask_hi;
   m_uint32_t int1n_mask_lo,int1n_mask_hi;

   /* GPP interrupts */
   m_uint32_t gpp_intr,gpp_mask0,gpp_mask1;

   /* IDMA registers */
   m_uint32_t idma_ic,idma_im,idma_bare;
   m_uint32_t idma_bar[MV64460_IDMA_BAR_REGS];
   m_uint32_t idma_sr[MV64460_IDMA_BAR_REGS];
   m_uint32_t idma_har[MV64460_IDMA_HAR_REGS];
   struct idma_channel idma[MV64460_IDMA_CHANNELS];
   
   /* SDMA channels */
   m_uint32_t sdma_cause,sdma_mask;
   struct sdma_channel sdma[MV64460_SDMA_CHANNELS];

   /* MPSC - MultiProtocol Serial Controller */
   struct mpsc_channel mpsc[MV64460_MPSC_CHANNELS];

   /* Ethernet ports */
   ptask_id_t eth_tx_tid;
   m_uint32_t eth_bare;
   m_uint32_t eth_ba[MV64460_ETH_WINDOWS];
   m_uint32_t eth_sr[MV64460_ETH_WINDOWS];
   m_uint32_t eth_pap[MV64460_ETH_PORTS];
   m_uint16_t eth_phy_addr;
   m_uint32_t smi_reg;
   m_uint16_t mii_regs[32][32];
   struct eth_port eth_ports[MV64460_ETH_PORTS];

   /* Integrated SRAM */
   struct vdevice sram_dev;

   /* PCI busses */
   struct pci_bus *bus[2];
};

#define MV64460_LOCK(d)   pthread_mutex_lock(&(d)->lock)
#define MV64460_UNLOCK(d) pthread_mutex_unlock(&(d)->lock)

/* Log a GT message */
#define MV64460_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* ======================================================================== */
/* Forward declarations                                                     */
/* ======================================================================== */
static u_int mv64460_mpsc_get_channel_mode(struct mv64460_data *d,u_int id);

/* ======================================================================== */
/* General interrupt control                                                */
/* ======================================================================== */

/* Returns a select cause register */
static m_uint32_t mv64460_ic_get_sel_cause(struct mv64460_data *d,
                                           m_uint32_t mask_lo,
                                           m_uint32_t mask_hi)
{
   int lo_act,hi_act;
   m_uint32_t res;

   lo_act = d->intr_lo & mask_lo;
   hi_act = d->intr_hi & mask_hi;
            
   if (!lo_act && hi_act) {
      res = (d->intr_hi & MV64460_IC_CAUSE_MASK) | MV64460_IC_CAUSE_SEL;
   } else {
      res = d->intr_lo & MV64460_IC_CAUSE_MASK;

      if (lo_act && hi_act)
         res |= MV64460_IC_CAUSE_STAT;
   }

   return(res);
}

/* Update the interrupt status for CPU 0 */
static void mv64460_ic_update_cpu0_status(struct mv64460_data *d)
{
   cpu_ppc_t *cpu0 = CPU_PPC32(d->vm->boot_cpu);
   m_uint32_t lo_act,hi_act;

   lo_act = d->intr_lo & d->cpu_intn0_mask_lo;
   hi_act = d->intr_hi & d->cpu_intn0_mask_hi;

   cpu0->irq_pending = lo_act || hi_act;
   cpu0->irq_check = cpu0->irq_pending;
}

/* Update GPIO interrupt status */
static void mv64460_gpio_update_int_status(struct mv64460_data *d)
{
   /* GPIO 0-7 */
   if (d->gpp_intr & d->gpp_mask0 & 0x000000FF)
      d->intr_hi |= MV64460_IHMCR_P0_GPP_0_7_SUM;
   else
      d->intr_hi &= ~MV64460_IHMCR_P0_GPP_0_7_SUM;

   /* GPIO 8-15 */
   if (d->gpp_intr & d->gpp_mask0 & 0x0000FF00)
      d->intr_hi |= MV64460_IHMCR_P0_GPP_8_15_SUM;
   else
      d->intr_hi &= ~MV64460_IHMCR_P0_GPP_8_15_SUM;

   /* GPIO 16-23 */
   if (d->gpp_intr & d->gpp_mask0 & 0x00FF0000)
      d->intr_hi |= MV64460_IHMCR_P0_GPP_16_23_SUM;
   else
      d->intr_hi &= ~MV64460_IHMCR_P0_GPP_16_23_SUM;

   /* GPIO 24-32 */
   if (d->gpp_intr & d->gpp_mask0 & 0xFF000000)
      d->intr_hi |= MV64460_IHMCR_P0_GPP_24_31_SUM;
   else
      d->intr_hi &= ~MV64460_IHMCR_P0_GPP_24_31_SUM;

   mv64460_ic_update_cpu0_status(d);
}

/* ======================================================================== */
/* IDMA                                                                     */
/* ======================================================================== */

/*
 * IDMA channel registers access.
 */
static int mv64460_idma_access(cpu_gen_t *cpu,struct vdevice *dev,
                               m_uint32_t offset,u_int op_size,u_int op_type,
                               m_uint64_t *data)
{   
   struct mv64460_data *mv_data = dev->priv_data;
   int cid;

   if ((offset & 0xFF00) != MV64460_REG_IDMA_BASE)
      return(FALSE);

   switch(offset) {
      case MV64460_REG_IDMA_IM:
         if (op_type == MTS_READ)
            *data = mv_data->idma_im;
         else
            mv_data->idma_im = *data;
         break;

      case MV64460_REG_IDMA_BC(0):
      case MV64460_REG_IDMA_BC(1):
      case MV64460_REG_IDMA_BC(2):
      case MV64460_REG_IDMA_BC(3):
         cid = (offset - MV64460_REG_IDMA_BC(0)) >> 2;

         if (op_type == MTS_READ)
            *data = mv_data->idma[cid].byte_count;
         else
            mv_data->idma[cid].byte_count = *data;
         break;

      case MV64460_REG_IDMA_SRC(0):
      case MV64460_REG_IDMA_SRC(1):
      case MV64460_REG_IDMA_SRC(2):
      case MV64460_REG_IDMA_SRC(3):
         cid = (offset - MV64460_REG_IDMA_SRC(0)) >> 2;

         if (op_type == MTS_READ)
            *data = mv_data->idma[cid].src_addr;
         else
            mv_data->idma[cid].src_addr = *data;
         break;

      case MV64460_REG_IDMA_DST(0):
      case MV64460_REG_IDMA_DST(1):
      case MV64460_REG_IDMA_DST(2):
      case MV64460_REG_IDMA_DST(3):
         cid = (offset - MV64460_REG_IDMA_DST(0)) >> 2;

         if (op_type == MTS_READ)
            *data = mv_data->idma[cid].dst_addr;
         else
            mv_data->idma[cid].dst_addr = *data;
         break;

      case MV64460_REG_IDMA_NDP(0):
      case MV64460_REG_IDMA_NDP(1):
      case MV64460_REG_IDMA_NDP(2):
      case MV64460_REG_IDMA_NDP(3):
         cid = (offset - MV64460_REG_IDMA_NDP(0)) >> 2;

         if (op_type == MTS_READ)
            *data = mv_data->idma[cid].nrptr;
         else
            mv_data->idma[cid].nrptr = *data;
         break;

      case MV64460_REG_IDMA_CDP(0):
      case MV64460_REG_IDMA_CDP(1):
      case MV64460_REG_IDMA_CDP(2):
      case MV64460_REG_IDMA_CDP(3):
         cid = (offset - MV64460_REG_IDMA_CDP(0)) >> 2;

         if (op_type == MTS_READ)
            *data = mv_data->idma[cid].cdptr;
         else
            mv_data->idma[cid].cdptr = *data;
         break;

      case MV64460_REG_IDMA_CTRL_LO(0):
      case MV64460_REG_IDMA_CTRL_LO(1):
      case MV64460_REG_IDMA_CTRL_LO(2):
      case MV64460_REG_IDMA_CTRL_LO(3):
         cid = (offset - MV64460_REG_IDMA_CTRL_LO(0)) >> 2;

         if (op_type == MTS_READ)
            *data = mv_data->idma[cid].ctrl_lo;
         else
            mv_data->idma[cid].ctrl_lo = *data;
         break;

      case MV64460_REG_IDMA_CTRL_HI(0):
      case MV64460_REG_IDMA_CTRL_HI(1):
      case MV64460_REG_IDMA_CTRL_HI(2):
      case MV64460_REG_IDMA_CTRL_HI(3):
         cid = (offset - MV64460_REG_IDMA_CTRL_HI(0)) >> 2;

         if (op_type == MTS_READ)
            *data = mv_data->idma[cid].ctrl_hi;
         else
            mv_data->idma[cid].ctrl_hi = *data;
         break;

      default:
         return(FALSE);
   }

   return(TRUE);
}

/*
 * IDMA decode registers access.
 */
static int mv64460_idma_dec_access(cpu_gen_t *cpu,struct vdevice *dev,
                                   m_uint32_t offset,u_int op_size,
                                   u_int op_type, m_uint64_t *data)
{   
   struct mv64460_data *mv_data = dev->priv_data;
   int reg;

   if ((offset & 0xFF00) != MV64460_REG_IDMA_DEC_BASE)
      return(FALSE);
   
   switch(offset) {
      case MV64460_REG_IDMA_BARE:
         if (op_type == MTS_READ)
            *data = mv_data->idma_bare;
         else
            mv_data->idma_bare = *data;
         break;

      case MV64460_REG_IDMA_BAR(0):
      case MV64460_REG_IDMA_BAR(1):
      case MV64460_REG_IDMA_BAR(2):
      case MV64460_REG_IDMA_BAR(3):
      case MV64460_REG_IDMA_BAR(4):
      case MV64460_REG_IDMA_BAR(5):
      case MV64460_REG_IDMA_BAR(6):
      case MV64460_REG_IDMA_BAR(7):
         reg = (offset - MV64460_REG_IDMA_BAR(0)) >> 3;

         if (op_type == MTS_READ)
            *data = mv_data->idma_bar[reg];
         else
            mv_data->idma_bar[reg] = *data;
         break;

      case MV64460_REG_IDMA_SR(0):
      case MV64460_REG_IDMA_SR(1):
      case MV64460_REG_IDMA_SR(2):
      case MV64460_REG_IDMA_SR(3):
      case MV64460_REG_IDMA_SR(4):
      case MV64460_REG_IDMA_SR(5):
      case MV64460_REG_IDMA_SR(6):
      case MV64460_REG_IDMA_SR(7):
         reg = (offset - MV64460_REG_IDMA_SR(0)) >> 3;

         if (op_type == MTS_READ)
            *data = mv_data->idma_sr[reg];
         else
            mv_data->idma_sr[reg] = *data;
         break;

      case MV64460_REG_IDMA_HAR(0):
      case MV64460_REG_IDMA_HAR(1):
      case MV64460_REG_IDMA_HAR(2):
      case MV64460_REG_IDMA_HAR(3):
         reg = (offset - MV64460_REG_IDMA_HAR(0)) >> 2;

         if (op_type == MTS_READ)
            *data = mv_data->idma_har[reg];
         else
            mv_data->idma_har[reg] = *data;
         break;

      case MV64460_REG_IDMA_CAP(0):
      case MV64460_REG_IDMA_CAP(1):
      case MV64460_REG_IDMA_CAP(2):
      case MV64460_REG_IDMA_CAP(3):
         reg = (offset - MV64460_REG_IDMA_CAP(0)) >> 2;

         if (op_type == MTS_READ)
            *data = mv_data->idma[reg].cap;
         else
            mv_data->idma[reg].cap = *data;
         break;

      default:
         return(FALSE);
   }

   return(TRUE);
}

/* ======================================================================== */
/* SDMA (Serial DMA)                                                        */
/* ======================================================================== */

/* Update SDMA interrupt status */
static void mv64460_sdma_update_int_status(struct mv64460_data *d)
{
   if (d->sdma_cause & d->sdma_mask & MV64460_SDMA_CAUSE_SDMA0) {
      d->intr_hi |= MV64460_IHMCR_SDMA0_SUM;
   } else {
      d->intr_hi &= ~MV64460_IHMCR_SDMA0_SUM;
   }

   if (d->sdma_cause & d->sdma_mask & MV64460_SDMA_CAUSE_SDMA1) {
      d->intr_hi |= MV64460_IHMCR_SDMA1_SUM;
   } else {
      d->intr_hi &= ~MV64460_IHMCR_SDMA1_SUM;
   }

   mv64460_ic_update_cpu0_status(d);
}

/* Set SDMA cause register for a channel */
static inline void mv64460_sdma_set_cause(struct mv64460_data *d,u_int chan_id,
                                          u_int value)
{
   d->sdma_cause |= value << (chan_id << 3);
}

/* Read a SDMA descriptor from memory */
static void mv64460_sdma_desc_read(struct mv64460_data *d,m_uint32_t addr,
                                   struct sdma_desc *desc)
{
   physmem_copy_from_vm(d->vm,desc,addr,sizeof(struct sdma_desc));

   /* byte-swapping */
   desc->buf_size = vmtoh32(desc->buf_size);
   desc->cmd_stat = vmtoh32(desc->cmd_stat);
   desc->next_ptr = vmtoh32(desc->next_ptr);
   desc->buf_ptr  = vmtoh32(desc->buf_ptr);
}

/* Write a SDMA descriptor to memory */
static void mv64460_sdma_desc_write(struct mv64460_data *d,m_uint32_t addr,
                                    struct sdma_desc *desc)
{
   struct sdma_desc tmp;
   
   /* byte-swapping */
   tmp.cmd_stat = vmtoh32(desc->cmd_stat);
   tmp.buf_size = vmtoh32(desc->buf_size);
   tmp.next_ptr = vmtoh32(desc->next_ptr);
   tmp.buf_ptr  = vmtoh32(desc->buf_ptr);

   physmem_copy_to_vm(d->vm,&tmp,addr,sizeof(struct sdma_desc));
}

/* Send contents of a SDMA buffer */
static void mv64460_sdma_send_buffer(struct mv64460_data *d,u_int chan_id,
                                     u_char *buffer,m_uint32_t len)
{
   struct mpsc_channel *channel;
   u_int mode;

   channel = &d->mpsc[chan_id];
   mode = mv64460_mpsc_get_channel_mode(d,chan_id);
   
   switch(mode) {
      case MV64460_MPSC_MODE_HDLC:
         if (channel->nio != NULL)
            netio_send(channel->nio,buffer,len);
         break;

      case MV64460_MPSC_MODE_UART:
         if (channel->vtty != NULL)
            vtty_put_buffer(channel->vtty,(char *)buffer,len);            
         break;
   }
}

/* Start TX DMA process */
static int mv64460_sdma_tx_start(struct mv64460_data *d,
                                 struct sdma_channel *chan)
{   
   u_char pkt[MV64460_MAX_PKT_SIZE],*pkt_ptr;
   struct sdma_desc txd0,ctxd,*ptxd;
   m_uint32_t tx_start,tx_current;
   m_uint32_t len,tot_len;
   int abort = FALSE;

   tx_start = tx_current = chan->sctdp;

   if (!tx_start)
      return(FALSE);

   ptxd = &txd0;
   mv64460_sdma_desc_read(d,tx_start,ptxd);

   /* If we don't own the first descriptor, we cannot transmit */
   if (!(txd0.cmd_stat & MV64460_TXDESC_OWN))
      return(FALSE);

   /* Empty packet for now */
   pkt_ptr = pkt;
   tot_len = 0;

   for(;;)
   {
      /* Copy packet data to the buffer */
      len = ptxd->buf_size & MV64460_TXDESC_BC_MASK;
      len >>= MV64460_TXDESC_BC_SHIFT;

      physmem_copy_from_vm(d->vm,pkt_ptr,ptxd->buf_ptr,len);
      pkt_ptr += len;
      tot_len += len;

      /* Clear the OWN bit if this is not the first descriptor */
      if (!(ptxd->cmd_stat & MV64460_TXDESC_F)) {
         ptxd->cmd_stat &= ~MV64460_TXDESC_OWN;
         physmem_copy_u32_to_vm(d->vm,tx_current+4,ptxd->cmd_stat);
      }

      //ptxd->buf_size &= 0xFFFF0000;
      //physmem_copy_u32_to_vm(d->vm,tx_current,ptxd->buf_size);

      tx_current = ptxd->next_ptr;

      /* Last descriptor or no more desc available ? */
      if (ptxd->cmd_stat & MV64460_TXDESC_L)
         break;

      if (!tx_current) {
         abort = TRUE;
         break;
      }

      /* Fetch the next descriptor */
      mv64460_sdma_desc_read(d,tx_current,&ctxd);
      ptxd = &ctxd;
   }

   if ((tot_len != 0) && !abort) {
#if DEBUG_SDMA
      MV64460_LOG(d,"SDMA%u: sending packet of %u bytes\n",tot_len);
      mem_dump(log_file,pkt,tot_len);
#endif
      /* send it on wire */
      mv64460_sdma_send_buffer(d,chan->id,pkt,tot_len);

      /* Signal that a TX buffer has been transmitted */
      mv64460_sdma_set_cause(d,chan->id,MV64460_SDMA_CAUSE_TXBUF0);
   }

   /* Clear the OWN flag of the first descriptor */
   txd0.cmd_stat &= ~MV64460_TXDESC_OWN;
   physmem_copy_u32_to_vm(d->vm,tx_start+4,txd0.cmd_stat);

   chan->sctdp = tx_current;

   if (abort || !tx_current) {
      mv64460_sdma_set_cause(d,chan->id,MV64460_SDMA_CAUSE_TXEND0);
      chan->sdcm &= ~MV64460_SDCMR_TXD;
   }

   /* Update interrupt status */
   mv64460_sdma_update_int_status(d);
   return(TRUE);
}

/* Put a packet in buffer of a descriptor */
static void mv64460_sdma_rxdesc_put_pkt(struct mv64460_data *d,
                                        struct sdma_desc *rxd,
                                        u_char **pkt,ssize_t *pkt_len)
{
   ssize_t len,cp_len;

   len = (rxd->buf_size & MV64460_RXDESC_BS_MASK) >> MV64460_RXDESC_BS_SHIFT;
   
   /* compute the data length to copy */
   cp_len = m_min(len,*pkt_len);
   
   /* copy packet data to the VM physical RAM */
   physmem_copy_to_vm(d->vm,*pkt,rxd->buf_ptr,cp_len);
      
   /* set the byte count in descriptor */
   rxd->buf_size |= cp_len;

   *pkt += cp_len;
   *pkt_len -= cp_len;
}

/* Put a packet into SDMA buffers */
static int mv64460_sdma_handle_rxqueue(struct mv64460_data *d,
                                       struct sdma_channel *channel,
                                       u_char *pkt,ssize_t pkt_len)
{
   m_uint32_t rx_start,rx_current;
   struct sdma_desc rxd0,rxdn,*rxdc;
   ssize_t tot_len = pkt_len;
   u_char *pkt_ptr = pkt;
   int i;

   /* Truncate the packet if it is too big */
   pkt_len = m_min(pkt_len,MV64460_MAX_PKT_SIZE);

   /* Copy the first RX descriptor */
   if (!(rx_start = rx_current = channel->scrdp))
      goto dma_error;

   /* Load the first RX descriptor */
   mv64460_sdma_desc_read(d,rx_start,&rxd0);

#if DEBUG_SDMA
   MV64460_LOG(d,"SDMA channel %u: reading desc at 0x%8.8x "
               "[buf_size=0x%8.8x,cmd_stat=0x%8.8x,"
               "next_ptr=0x%8.8x,buf_ptr=0x%8.8x]\n",
               channel->id,rx_start,rxd0.buf_size,rxd0.cmd_stat,
               rxd0.next_ptr,rxd0.buf_ptr);
#endif

   for(i=0,rxdc=&rxd0;tot_len>0;i++)
   {
      /* We must own the descriptor */
      if (!(rxdc->cmd_stat & MV64460_RXDESC_OWN))
         goto dma_error;

      /* Put data into the descriptor buffer */
      mv64460_sdma_rxdesc_put_pkt(d,rxdc,&pkt_ptr,&tot_len);

      /* Clear the OWN bit */
      rxdc->cmd_stat &= ~MV64460_RXDESC_OWN;

      /* We have finished if the complete packet has been stored */
      if (tot_len == 0) {
         rxdc->cmd_stat |= MV64460_RXDESC_L;

         /* Fake HDLC CRC */
         if (mv64460_mpsc_get_channel_mode(d,channel->id) == 
             MV64460_MPSC_MODE_HDLC) 
         {
            rxdc->buf_size += 2;  /* Add 2 bytes for CRC */
         }
      }

      /* Update the descriptor in host memory (but not the 1st) */
      if (i != 0)
         mv64460_sdma_desc_write(d,rx_current,rxdc);

      /* Get address of the next descriptor */
      rx_current = rxdc->next_ptr;

      if (tot_len == 0)
         break;

      if (!rx_current)
         goto dma_error;

      /* Read the next descriptor from VM physical RAM */
      mv64460_sdma_desc_read(d,rx_current,&rxdn);
      rxdc = &rxdn;
   }

   /* Update the RX pointers */
   channel->scrdp = rx_current;
       
   /* Update the first RX descriptor */
   rxd0.cmd_stat |= MV64460_RXDESC_F;
   mv64460_sdma_desc_write(d,rx_start,&rxd0);

   /* Indicate that we have a frame ready */
   mv64460_sdma_set_cause(d,channel->id,MV64460_SDMA_CAUSE_RXBUF0);
   mv64460_sdma_update_int_status(d);
   return(TRUE);

 dma_error:
   mv64460_sdma_set_cause(d,channel->id,MV64460_SDMA_CAUSE_RXERR0);
   mv64460_sdma_update_int_status(d);
   return(FALSE);
}

/* Handle RX packet for a SDMA channel */
_unused static int mv64460_sdma_handle_rx_pkt(netio_desc_t *nio,
                                      u_char *pkt,ssize_t pkt_len,
                                      struct mv64460_data *d,void *arg)
{
   u_int chan_id = (u_int)(u_long)arg;

   MV64460_LOCK(d);   
   mv64460_sdma_handle_rxqueue(d,&d->sdma[chan_id],pkt,pkt_len);
   MV64460_UNLOCK(d);
   return(TRUE);
}

/* Input on VTTY */
static void mv64460_sdma_vtty_input(vtty_t *vtty)
{
   struct mv64460_data *d = vtty->priv_data;
   struct sdma_channel *chan = &d->sdma[vtty->user_arg];
   u_char c;

   c = vtty_get_char(vtty);
   mv64460_sdma_handle_rxqueue(d,chan,&c,1);
}

/* Bind a VTTY to a SDMA/MPSC channel */
int mv64460_sdma_bind_vtty(struct mv64460_data *d,u_int chan_id,vtty_t *vtty)
{
   if (chan_id >= MV64460_MPSC_CHANNELS)
      return(-1);

   vtty->priv_data = d;
   vtty->user_arg = chan_id;
   vtty->read_notifier = mv64460_sdma_vtty_input;

   d->mpsc[chan_id].vtty = vtty;
   return(0);
}

/*
 * SDMA registers access.
 */
static int mv64460_sdma_access(cpu_gen_t *cpu,struct vdevice *dev,
                               m_uint32_t offset,u_int op_size,u_int op_type,
                               m_uint64_t *data)
{   
   struct mv64460_data *mv_data = dev->priv_data;
   struct sdma_channel *channel;
   int id = -1;

   /* Access to SDMA channel 0 registers ? */
   if ((offset >= MV64460_REG_SDMA0) && 
       (offset < (MV64460_REG_SDMA0 + 0x1000))) 
   {
      offset -= MV64460_REG_SDMA0;
      id = 0;
   }

   /* Access to SDMA channel 1 registers ? */
   if ((offset >= MV64460_REG_SDMA1) && 
       (offset < (MV64460_REG_SDMA1 + 0x1000))) 
   {
      offset -= MV64460_REG_SDMA1;
      id = 1;
   }
   
   if (id == -1)
      return(FALSE);

   channel = &mv_data->sdma[id];

   switch(offset) {
      case MV64460_SDMA_SDCM:
         if (op_type == MTS_READ)
            ; //*data = chan->sdcm;
         else {
            channel->sdcm = *data;

            if (channel->sdcm & MV64460_SDCMR_TXD) {
               while(mv64460_sdma_tx_start(mv_data,channel))
                  ;
            }
         }
         break;

      case MV64460_SDMA_SCRDP:
         if (op_type == MTS_READ)
            *data = channel->scrdp;
         else
            channel->scrdp = *data;
         break;

      case MV64460_SDMA_SCTDP:
         if (op_type == MTS_READ)
            *data = channel->sctdp;
         else
            channel->sctdp = *data;
         break;

      case MV64460_SDMA_SFTDP:
         if (op_type == MTS_READ)
            *data = channel->sftdp;
         else
            channel->sftdp = *data;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"MV64460/SDMA",
                    "read access to unknown register 0x%x, pc=0x%llx\n",
                    offset,cpu_get_pc(cpu));
         } else {
            cpu_log(cpu,"MV64460/SDMA",
                    "write access to unknown register 0x%x, value=0x%llx, "
                    "pc=0x%llx\n",offset,*data,cpu_get_pc(cpu));
         }
#endif
   }

   return(TRUE);
}

/* ======================================================================== */
/* MPSC (MultiProtocol Serial Controller)                                   */
/* ======================================================================== */

/* Get mode (HDLC,UART,...) of the specified channel */
static u_int mv64460_mpsc_get_channel_mode(struct mv64460_data *d,u_int id)
{
   struct mpsc_channel *channel;
      
   channel = &d->mpsc[id];
   return(channel->mmcrl & MV64460_MMCRL_MODE_MASK);
}

/* Handle a MPSC channel */
static int mv64460_mpsc_access(cpu_gen_t *cpu,struct vdevice *dev,
                               m_uint32_t offset,u_int op_size,u_int op_type,
                               m_uint64_t *data)
{
   struct mv64460_data *mv_data = dev->priv_data;
   struct mpsc_channel *channel;
   u_int reg;
   int id = -1;

   /* Access to MPSC channel 0 registers ? */
   if ((offset >= MV64460_REG_MPSC0) && 
       (offset < (MV64460_REG_MPSC0 + 0x1000))) 
   {
      offset -= MV64460_REG_MPSC0;
      id = 0;
   }

   /* Access to SDMA channel 1 registers ? */
   if ((offset >= MV64460_REG_MPSC1) && 
       (offset < (MV64460_REG_MPSC1 + 0x1000))) 
   {
      offset -= MV64460_REG_MPSC1;
      id = 1;
   }
   
   if (id == -1)
      return(FALSE);

   channel = &mv_data->mpsc[id];

   switch(offset) {
      /* Main Config Register Low */
      case MV64460_MPSC_MMCRL:
         if (op_type == MTS_READ) {
            *data = channel->mmcrl;
         } else {
#if DEBUG_MPSC            
            MV64460_LOG(gt_data,"MPSC channel %u set in mode %llu\n",
                        chan_id,*data & 0x07);
#endif
            channel->mmcrl = *data;
         }
         break;

      /* Main Config Register High */
      case MV64460_MPSC_MMCRH:
         if (op_type == MTS_READ)
            *data = channel->mmcrh;
         else
            channel->mmcrh = *data;
         break;

      /* Protocol Config Register */
      case MV64460_MPSC_MPCR:
         if (op_type == MTS_READ)
            *data = channel->mpcr;
         else
            channel->mpcr = *data;
         break;

      /* Channel registers */
      case MV64460_MPSC_CHR1: 
      case MV64460_MPSC_CHR2: 
      case MV64460_MPSC_CHR3:
      case MV64460_MPSC_CHR4: 
      case MV64460_MPSC_CHR5: 
      case MV64460_MPSC_CHR6:
      case MV64460_MPSC_CHR7: 
      case MV64460_MPSC_CHR8: 
      case MV64460_MPSC_CHR9:
         //case MV64460_MPSC_CHR10:
         reg = (offset - MV64460_MPSC_CHR1) >> 2;
         if (op_type == MTS_READ)
            *data = channel->chr[reg];
         else
            channel->chr[reg] = *data;
         break;

      case MV64460_MPSC_CHR10:
         if (op_type == MTS_READ)
            *data = channel->chr[9] | 0x20;
         else
            channel->chr[9] = *data;
         break;

      default:
         /* unknown/unmanaged register */
         return(FALSE);
   }

   return(TRUE);
}

/* ======================================================================== */
/* Gigabit Ethernet Controller                                              */
/* ======================================================================== */

/* Update the interrupt status for port interrupt register */
static void mv64460_eth_update_pic(struct mv64460_data *d,
                                   struct eth_port *port)
{
   if (port->pic & port->pim & MV64460_ETH_IC_SUM_MASK) {
      port->pic |= MV64460_ETH_IC_SUM;
      d->intr_hi |= MV64460_IHMCR_ETH0_SUM << port->id;
   } else {
      port->pic &= ~MV64460_ETH_IC_SUM;
      d->intr_hi &= ~(MV64460_IHMCR_ETH0_SUM << port->id);
   }

   /* Update the various summary bits in high cause register */
   if ((port->pic & MV64460_ETH_INT_RX_IC_MASK) ||
       (port->pice & MV64460_ETH_INT_RX_ICE_MASK))
   {
      d->intr_hi |= MV64460_IHMCR_ETH_RX_SUM(port->id);
   } else {
      d->intr_hi &= ~(MV64460_IHMCR_ETH_RX_SUM(port->id));
   }

   if ((port->pic & MV64460_ETH_INT_TX_IC_MASK) ||
       (port->pice & MV64460_ETH_INT_TX_ICE_MASK))
   {
      d->intr_hi |= MV64460_IHMCR_ETH_TX_SUM(port->id);
   } else {
      d->intr_hi &= ~(MV64460_IHMCR_ETH_TX_SUM(port->id));
   }

   if (port->pice & MV64460_ETH_INT_MISC_ICE_MASK) {
      d->intr_hi |= MV64460_IHMCR_ETH_MISC_SUM(port->id);
   } else {
      d->intr_hi &= ~(MV64460_IHMCR_ETH_MISC_SUM(port->id));
   }
   
   /* Update the interrupt status */
   mv64460_ic_update_cpu0_status(d);
}

/* Update the interrupt status for port interrupt extend register */
static void mv64460_eth_update_pice(struct mv64460_data *d,
                                    struct eth_port *port)
{
   if (port->pice & port->peim & MV64460_ETH_ICE_SUM_MASK) {
      port->pice |= MV64460_ETH_ICE_SUM;
   } else {
      port->pice &= ~MV64460_ETH_ICE_SUM;
   }

   if (port->pice) {
      port->pic |= MV64460_ETH_IC_EXTEND;
   } else {
      port->pic &= ~MV64460_ETH_IC_EXTEND;
   }   

   mv64460_eth_update_pic(d,port);
}

/* Handle a TX queue (single packet) */
static int mv64460_eth_handle_port_txqueue(struct mv64460_data *d,
                                           struct eth_port *port,
                                           int queue)
{   
   u_char pkt[MV64460_MAX_PKT_SIZE],*pkt_ptr;
   struct sdma_desc txd0,ctxd,*ptxd;
   m_uint32_t tx_start,tx_current;
   m_uint32_t len,tot_len;
   int abort = FALSE;

   /* Check if this TX queue is enabled */
   if (!(port->tqc & MV64460_ETH_TQC_ENQ(queue)))
      return(FALSE);

   /* Copy the current txring descriptor */
   tx_start = tx_current = port->tcqdp[queue];

   if (!tx_start)
      return(FALSE);

   ptxd = &txd0;
   mv64460_sdma_desc_read(d,tx_start,ptxd);

   /* If we don't own the first descriptor, we cannot transmit */
   if (!(txd0.cmd_stat & MV64460_ETH_TXDESC_OWN))
      return(FALSE);

   /* Empty packet for now */
   pkt_ptr = pkt;
   tot_len = 0;

   for(;;) {
#if DEBUG_ETH_TX
      MV64460_LOG(d,"mv64460_eth_handle_txqueue: loop: "
                  "cmd_stat=0x%x, buf_size=0x%x, "
                  "next_ptr=0x%x, buf_ptr=0x%x\n",
                  ptxd->cmd_stat,ptxd->buf_size,
                  ptxd->next_ptr,ptxd->buf_ptr);
#endif

      if (!(ptxd->cmd_stat & MV64460_ETH_TXDESC_OWN)) {
         MV64460_LOG(d,"mv64460_eth_handle_txqueue: descriptor not owned!\n");
         abort = TRUE;
         break;
      }

      /* Copy packet data to the buffer */
      len = ptxd->buf_size & MV64460_ETH_TXDESC_BC_MASK;
      len >>= MV64460_ETH_TXDESC_BC_SHIFT;

      physmem_copy_from_vm(d->vm,pkt_ptr,ptxd->buf_ptr,len);
      pkt_ptr += len;
      tot_len += len;

      /* Clear the OWN bit if this is not the first descriptor */
      if (!(ptxd->cmd_stat & MV64460_ETH_TXDESC_F)) {
         ptxd->cmd_stat &= ~MV64460_ETH_TXDESC_OWN;
         physmem_copy_u32_to_vm(d->vm,tx_current,ptxd->cmd_stat);
      }

      tx_current = ptxd->next_ptr;

      /* Last descriptor or no more desc available ? */
      if (ptxd->cmd_stat & MV64460_ETH_TXDESC_L)
         break;

      if (!tx_current) {
         abort = TRUE;
         break;
      }

      /* Fetch the next descriptor */
      mv64460_sdma_desc_read(d,tx_current,&ctxd);
      ptxd = &ctxd;
   }

   if ((tot_len != 0) && !abort) {
#if DEBUG_ETH_TX
      MV64460_LOG(d,"Eth%u: sending packet of %u bytes\n",port->id,tot_len);
      mem_dump(log_file,pkt,tot_len);
#endif
      /* send it on wire */
      netio_send(port->nio,pkt,tot_len);

      /* Update MIB counters */
      port->mib_good_tx_bytes += tot_len;
      port->mib_good_tx_frames++;
   }

   /* Clear the OWN flag of the first descriptor */
   txd0.cmd_stat &= ~MV64460_ETH_TXDESC_OWN;
   physmem_copy_u32_to_vm(d->vm,tx_start+4,txd0.cmd_stat);

   port->tcqdp[queue] = tx_current;
   
   /* Notify host about transmitted packet */
   port->pice |= MV64460_ETH_ICE_TXBUF(queue);

   if (abort) {
      /* TX underrun */
      port->pice |= MV64460_ETH_ICE_TXUDR;
      port->pice |= MV64460_ETH_ICE_TXERR(queue);
   } else {
      /* End of queue has been reached */
      if (!tx_current)
         port->pic |= MV64460_ETH_IC_TXEND(queue);
   }

   /* Update the interrupt status */
   mv64460_eth_update_pice(d,port);
   return(TRUE);
}

/* Handle all TX queues of the specified port */
static void mv64460_eth_handle_port_txqueues(struct mv64460_data *d,u_int port)
{
   int i;

   for(i=0;i<MV64460_ETH_TX_QUEUES;i++)
      mv64460_eth_handle_port_txqueue(d,&d->eth_ports[port],i);
}

/* Handle all TX queues of all Ethernet ports */
static int mv64460_eth_handle_txqueues(struct mv64460_data *d)
{
   int i;

   MV64460_LOCK(d);

   for(i=0;i<MV64460_ETH_PORTS;i++)
      mv64460_eth_handle_port_txqueues(d,i);

   MV64460_UNLOCK(d);
   return(TRUE);
}

/* Put a packet in buffer of a descriptor */
static void mv64460_eth_rxdesc_put_pkt(struct mv64460_data *d,
                                       struct sdma_desc *rxd,
                                       u_char **pkt,ssize_t *pkt_len,
                                       int pos)
{
   ssize_t len,cp_len;
   m_uint32_t buf_ptr;

   buf_ptr = rxd->buf_ptr;
   len = rxd->buf_size & MV64460_ETH_RXDESC_BS_MASK;
   len >>= MV64460_ETH_RXDESC_BS_SHIFT;
   
   /* copy packet data to the VM physical RAM */
   if (pos == 0) {
      buf_ptr += 2;
      len -= 2;
   }

   /* compute the data length to copy */
   cp_len = m_min(len,*pkt_len);

   /* copy packet data to the VM physical RAM */
   physmem_copy_to_vm(d->vm,*pkt,buf_ptr,cp_len);
   *pkt += cp_len;
   *pkt_len -= cp_len;
}

/* Put a packet in the specified RX queue */
static int mv64460_eth_handle_rxqueue(struct mv64460_data *d,u_int port_id,
                                      u_int queue,n_pkt_ctx_t *ctx)
{
   struct eth_port *port = &d->eth_ports[port_id];
   m_uint32_t rx_start,rx_current;
   struct sdma_desc rxd0,rxdn,*rxdc;
   ssize_t tot_len = ctx->pkt_len;
   u_char *pkt_ptr = ctx->pkt;
   u_int ip_proto;
   int i;

   /* Copy the first RX descriptor */
   if (!(rx_start = rx_current = port->crdp[queue]))
      goto dma_error;

   /* Load the first RX descriptor */
   mv64460_sdma_desc_read(d,rx_start,&rxd0);

#if DEBUG_ETH_RX
   MV64460_LOG(d,"port %u/queue %u: reading desc at 0x%8.8x "
               "[buf_size=0x%8.8x,cmd_stat=0x%8.8x,"
               "next_ptr=0x%8.8x,buf_ptr=0x%8.8x]\n",
               port_id,queue,rx_start,
               rxd0.buf_size,rxd0.cmd_stat,rxd0.next_ptr,rxd0.buf_ptr);
#endif

   for(i=0,rxdc=&rxd0;tot_len>0;i++)
   {
      /* We must own the descriptor */
      if (!(rxdc->cmd_stat & MV64460_ETH_RXDESC_OWN))
         goto dma_error;

      /* Put data into the descriptor buffer */
      mv64460_eth_rxdesc_put_pkt(d,rxdc,&pkt_ptr,&tot_len,i);

      /* Clear the OWN bit */
      rxdc->cmd_stat &= ~MV64460_ETH_RXDESC_OWN;

      /* We have finished if the complete packet has been stored */
      if (tot_len == 0)
         rxdc->cmd_stat |= MV64460_ETH_RXDESC_L;

      /* Update the descriptor in host memory (but not the 1st) */
      if (i != 0)
         mv64460_sdma_desc_write(d,rx_current,rxdc);
      
      /* Get address of the next descriptor */
      rx_current = rxdc->next_ptr;
      
      if (tot_len == 0)
         break;

      if (!rx_current)
         goto dma_error;

      /* Read the next descriptor from VM physical RAM */
      mv64460_sdma_desc_read(d,rx_current,&rxdn);
      rxdc = &rxdn;
   }

   /* Update the RX pointer */
   port->crdp[queue] = rx_current;

   /* Update the first RX descriptor */
   rxd0.cmd_stat |= MV64460_ETH_RXDESC_F;
          
   /* Analyze Layer 2 */
   if (ctx->flags & N_PKT_CTX_FLAG_ETHV2)
      rxd0.cmd_stat |= MV64460_ETH_RXDESC_L2V2;

   if (ctx->flags & N_PKT_CTX_FLAG_VLAN)
      rxd0.cmd_stat |= MV64460_ETH_RXDESC_VLAN;

   /* Analyze Layer 3 */
   if (ctx->flags & N_PKT_CTX_FLAG_L3_IP) {
      rxd0.cmd_stat |= MV64460_ETH_RXDESC_L3IP;
      
      if (ctx->flags & N_PKT_CTX_FLAG_IPH_OK) {
         u_int rxcs,frag;
         m_uint16_t cksum;

         rxd0.cmd_stat |= MV64460_ETH_RXDESC_IPH_OK;

         if (ctx->flags & N_PKT_CTX_FLAG_IP_FRAG) {
            rxd0.buf_size |= MV64460_ETH_RXDESC_IPV4_FRG;
            frag = TRUE;
         } else {
            frag = FALSE;
         }

         rxcs = port->pcr & MV64460_ETH_PCR_RXCS;

         /* Set Layer 4 protocol info */
         switch(ctx->ip_l4_proto) {
            case N_IP_PROTO_TCP:
               ip_proto = MV64460_ETH_RXDESC_L4P_TCP;

               if (!frag && rxcs) {
                  cksum = pkt_ctx_tcp_cksum(ctx,TRUE);

                  if (cksum == ntohs(ctx->tcp->cksum))
                     rxd0.cmd_stat |= MV64460_ETH_RXDESC_L4CHK_OK;
               } else {
                  cksum = pkt_ctx_tcp_cksum(ctx,FALSE);
               }

               /* set the computed checksum */
               rxd0.cmd_stat |= cksum << MV64460_ETH_RXDESC_L4CHK_SHIFT;
               break;

            case N_IP_PROTO_UDP:
               ip_proto = MV64460_ETH_RXDESC_L4P_UDP;

               if (!frag && rxcs) {
                  cksum = pkt_ctx_tcp_cksum(ctx,TRUE);

                  if (!ctx->udp->cksum || (cksum == ntohs(ctx->udp->cksum)))
                     rxd0.cmd_stat |= MV64460_ETH_RXDESC_L4CHK_OK;
               } else {
                  cksum = pkt_ctx_tcp_cksum(ctx,FALSE);
               }

               /* set the computed checksum */
               rxd0.cmd_stat |= cksum << MV64460_ETH_RXDESC_L4CHK_SHIFT;
               break;

            default:
               ip_proto = MV64460_ETH_RXDESC_L4P_OTHER;
         }

         rxd0.cmd_stat |= ip_proto << MV64460_ETH_RXDESC_L4P_SHIFT;
      }
   }

   /* Set the buffer size */
   tot_len = ctx->pkt_len + N_ETH_CRC_LEN + 2;
   rxd0.buf_size &= ~MV64460_ETH_RXDESC_BC_MASK;
   rxd0.buf_size |= tot_len << MV64460_ETH_RXDESC_BC_SHIFT;
   mv64460_sdma_desc_write(d,rx_start,&rxd0);

   /* Update MIB counters */
   port->mib_good_rx_bytes += ctx->pkt_len;
   port->mib_good_rx_frames++;

   /* Indicate that we have a frame ready */
   port->pic |= MV64460_ETH_IC_RXBUFQ(queue) | MV64460_ETH_IC_RXBUF;
   mv64460_eth_update_pic(d,port);
   return(TRUE);

 dma_error:
   port->pic |= MV64460_ETH_IC_RXERRQ(queue) | MV64460_ETH_IC_RXERR;
   mv64460_eth_update_pic(d,port);
   return(FALSE);
}

/* Handle RX packet for an Ethernet port */
static int mv64460_eth_handle_rx_pkt(netio_desc_t *nio,
                                     u_char *pkt,ssize_t pkt_len,
                                     struct mv64460_data *d,void *arg)
{
   u_int queue,port_id = (u_int)(u_long)arg;
   struct eth_port *port;
   n_pkt_ctx_t ctx;

   port = &d->eth_ports[port_id];

   MV64460_LOCK(d);
   queue = 0;  /* At this time, only put packet in queue 0 */

   pkt_ctx_analyze(&ctx,pkt,pkt_len);

   /* Check if queue is active */
   if (!(port->rqc & MV64460_ETH_RQC_ENQ(queue))) {
      MV64460_UNLOCK(d);
      return(FALSE);
   }

   mv64460_eth_handle_rxqueue(d,port_id,queue,&ctx);
   MV64460_UNLOCK(d);
   return(TRUE);
}

/* Read a MII register */
static m_uint32_t mv64460_eth_mii_read(struct mv64460_data *d)
{
   m_uint32_t port,reg;
   m_uint32_t res = 0;

   port = (d->smi_reg & MV64460_ETH_SMI_PHYAD_MASK);
   port >>= MV64460_ETH_SMI_PHYAD_SHIFT;

   reg  = (d->smi_reg & MV64460_ETH_SMI_REGAD_MASK);
   reg >>= MV64460_ETH_SMI_REGAD_SHIFT;

#if DEBUG_MII
   MV64460_LOG(d,"MII: port 0x%4.4x, reg 0x%2.2x: reading.\n",port,reg);
#endif

   if (reg < 32) {
      res = d->mii_regs[port][reg];

      switch(reg) {
         case 0x00:
            res &= ~0x8200; /* clear reset bit and autoneg restart */
            res |= 0x2100;
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
            res = 0x0141;
            break;
         case 0x03:
            res = 0x0cd4;
            break;
         case 0x04:
            res = 0x1E1;
            break;
         case 0x05:
            res = 0x41E1;
            break;
         case 0x06:
            res = 0x0001;
            break;
         case 0x11:
            res = 0x4700;
            break;
         case 0x19:
            res = 0x800F;
            break;
         default:
            res = 0x0000;
      }
   }

   /* Mark the data as ready */
   res |= MV64460_ETH_SMI_RVALID_FLAG;

   return(res);
}

/* Write a MII register */
static void mv64460_eth_mii_write(struct mv64460_data *d)
{
   m_uint32_t port,reg;
   m_uint16_t isolation;

   port = (d->smi_reg & MV64460_ETH_SMI_PHYAD_MASK);
   port >>= MV64460_ETH_SMI_PHYAD_SHIFT;

   reg = (d->smi_reg & MV64460_ETH_SMI_REGAD_MASK);
   reg >>= MV64460_ETH_SMI_REGAD_SHIFT;

   if (reg < 32)
   {
#if DEBUG_MII
      MV64460_LOG(d,"MII: port 0x%4.4x, reg 0x%2.2x: writing 0x%4.4x\n",
                  port,reg,d->smi_reg & MV64460_ETH_SMI_DATA_MASK);
#endif

      /* Check if PHY isolation status is changing */
      if (reg == 0) {
         isolation = (d->smi_reg ^ d->mii_regs[port][reg]) & 0x400;

         if (isolation) {
#if DEBUG_MII
            MV64460_LOG(d,"MII: port 0x%4.4x: generating IRQ\n",port);
#endif
            printf("FIRING !!!\n");
            d->eth_ports[port].pice |= MV64460_ETH_ICE_PHY_STC;
            mv64460_eth_update_pice(d,&d->eth_ports[port]);
         }
      }

      d->mii_regs[port][reg] = d->smi_reg & MV64460_ETH_SMI_DATA_MASK;
   }
}

/* Handle Ethernet registers */
static int mv64460_eth_access(cpu_gen_t *cpu,struct vdevice *dev,
                              m_uint32_t offset,u_int op_size,u_int op_type,
                              m_uint64_t *data)
{
   struct mv64460_data *mv_data = dev->priv_data;
   struct eth_port *port;
   u_int group,reg;
   int port_id;

   if ((offset < MV64460_REG_ETH_START) || (offset >= MV64460_REG_ETH_END))
      return(FALSE);

   switch(offset) {
      case MV64460_REG_ETH_BARE:
         if (op_type == MTS_READ)
            *data = mv_data->eth_bare;
         else
            mv_data->eth_bare = *data;
         break;

      case MV64460_REG_ETH_BA(0):
      case MV64460_REG_ETH_BA(1):
      case MV64460_REG_ETH_BA(2):
      case MV64460_REG_ETH_BA(3):
      case MV64460_REG_ETH_BA(4):
      case MV64460_REG_ETH_BA(5):
         reg = (offset - MV64460_REG_ETH_BA(0)) >> 3;
         
         if (op_type == MTS_READ)
            *data = mv_data->eth_ba[reg];
         else
            mv_data->eth_ba[reg] = *data;
         break;

      case MV64460_REG_ETH_SR(0):
      case MV64460_REG_ETH_SR(1):
      case MV64460_REG_ETH_SR(2):
      case MV64460_REG_ETH_SR(3):
      case MV64460_REG_ETH_SR(4):
      case MV64460_REG_ETH_SR(5):
         reg = (offset - MV64460_REG_ETH_SR(0)) >> 3;
         
         if (op_type == MTS_READ)
            *data = mv_data->eth_sr[reg];
         else
            mv_data->eth_sr[reg] = *data;
         break;

      case MV64460_REG_ETH_EPAP(0):
      case MV64460_REG_ETH_EPAP(1):
      case MV64460_REG_ETH_EPAP(2):
         reg = (offset - MV64460_REG_ETH_EPAP(0)) >> 2;

         if (op_type == MTS_READ)
            *data = mv_data->eth_pap[reg];
         else
            mv_data->eth_pap[reg] = *data;
         break;

      case MV64460_REG_ETH_PHY_ADDR:
         if (op_type == MTS_READ)
            *data = mv_data->eth_phy_addr;
         else
            mv_data->eth_phy_addr = *data;
         break;

      case MV64460_REG_ETH_SMI:
         if (op_type == MTS_WRITE) {
            mv_data->smi_reg = *data;

            if (!(mv_data->smi_reg & MV64460_ETH_SMI_OPCODE_READ))
               mv64460_eth_mii_write(mv_data);
         } else {
            *data = 0;

            if (mv_data->smi_reg & MV64460_ETH_SMI_OPCODE_READ)
               *data = mv64460_eth_mii_read(mv_data);
         }
         break;
   }

   /* Handle port-specific registers */
   group = offset >> 8;

   switch(group) {
      case 0x24:
      case 0x26:
      case 0x27:
         port_id = 0;
         break;
      case 0x28:
      case 0x2A:
      case 0x2B:
         port_id = 1;
         break;
      case 0x2C:
      case 0x2E:
      case 0x2F:
         port_id = 2;
         break;
      case 0x30:
      case 0x31:
         port_id = (offset >> 7) & 0x03;
         offset -= (port_id * 0x80);
         break;
      default:
         port_id = -1;
   }

   if ((port_id == -1) || (port_id >= MV64460_ETH_PORTS))
      return(TRUE);

   if ((group & 0xF0) != 0x30)
      offset -= (port_id * 0x0400);

   port = &mv_data->eth_ports[port_id];

   switch(offset) {
      case 0x2444:
         if (op_type == MTS_READ)
            *data = 0x27;
         break;

      case 0x243c:
         if (op_type == MTS_READ)
            *data = 0x1;
         break;

      case MV64460_REG_ETH_PCR:
         if (op_type == MTS_READ)
            *data = port->pcr;
         else
            port->pcr = *data;
         break;

      case MV64460_REG_ETH_PCXR:
         if (op_type == MTS_READ)
            *data = port->pcxr;
         else
            port->pcxr = *data;
         break;

      case MV64460_REG_ETH_EVLANE:
         if (op_type == MTS_READ)
            *data = port->vlan_ether_type;
         else
            port->vlan_ether_type = *data;
         break;   

      case MV64460_REG_ETH_MACAL:
         if (op_type == MTS_READ) {
            *data  = port->mac_addr.eth_addr_byte[4] << 8;
            *data |= port->mac_addr.eth_addr_byte[5];
         } else {
            port->mac_addr.eth_addr_byte[4] = *data >> 8;
            port->mac_addr.eth_addr_byte[5] = *data & 0xFF;
         }
         break;

      case MV64460_REG_ETH_MACAH:
         if (op_type == MTS_READ) {
            *data  = port->mac_addr.eth_addr_byte[0] << 24;
            *data |= port->mac_addr.eth_addr_byte[1] << 16;
            *data |= port->mac_addr.eth_addr_byte[2] << 8;
            *data |= port->mac_addr.eth_addr_byte[3];
         } else {
            port->mac_addr.eth_addr_byte[0] = *data >> 24;
            port->mac_addr.eth_addr_byte[1] = *data >> 16;
            port->mac_addr.eth_addr_byte[2] = *data >> 8;
            port->mac_addr.eth_addr_byte[3] = *data & 0xFF;
         }
         break;

      case MV64460_REG_ETH_SDCR:
         if (op_type == MTS_READ)
            *data = port->sdcr;
         else
            port->sdcr = *data;
         break;

      case MV64460_REG_ETH_TQC:
         if (op_type == MTS_READ) {
            *data = port->tqc;
         } else {
            int i;

            for(i=0;i<MV64460_ETH_TX_QUEUES;i++) {
               if (*data & MV64460_ETH_TQC_ENQ(i)) {
                  port->tqc |= MV64460_ETH_TQC_ENQ(i);
                  port->tqc &= ~MV64460_ETH_TQC_DISQ(i);
               }

               if (*data & MV64460_ETH_TQC_DISQ(i)) {
                  port->tqc &= ~MV64460_ETH_TQC_ENQ(i);
                  port->tqc |= MV64460_ETH_TQC_DISQ(i);
               }
            }
         }
         break;

      case MV64460_REG_ETH_RQC:
         if (op_type == MTS_READ) {
            *data = port->rqc;
         } else {
            int i;

            for(i=0;i<MV64460_ETH_RX_QUEUES;i++) {
               if (*data & MV64460_ETH_RQC_ENQ(i)) {
                  port->rqc |= MV64460_ETH_RQC_ENQ(i);
                  port->rqc &= ~MV64460_ETH_RQC_DISQ(i);
               }

               if (*data & MV64460_ETH_RQC_DISQ(i)) {
                  port->rqc &= ~MV64460_ETH_RQC_ENQ(i);
                  port->rqc |= MV64460_ETH_RQC_DISQ(i);
               }
            }
         }
         break;

      case MV64460_REG_ETH_IC:
         if (op_type == MTS_READ) {
            *data = port->pic;
         } else {
            port->pic &= *data;
            mv64460_eth_update_pic(mv_data,port);
         }
         break;

      case MV64460_REG_ETH_ICE:
         if (op_type == MTS_READ) {
            *data = port->pice;
         } else {
            port->pice &= *data;
            mv64460_eth_update_pice(mv_data,port);
         }
         break;

      case MV64460_REG_ETH_PIM:
         if (op_type == MTS_READ)
            *data = port->pim;
         else
            port->pim = *data;
         break;

      case MV64460_REG_ETH_PEIM:
         if (op_type == MTS_READ)
            *data = port->peim;
         else
            port->peim = *data;
         break;

      case MV64460_REG_ETH_CRDP(0):
      case MV64460_REG_ETH_CRDP(1):
      case MV64460_REG_ETH_CRDP(2):
      case MV64460_REG_ETH_CRDP(3):
      case MV64460_REG_ETH_CRDP(4):
      case MV64460_REG_ETH_CRDP(5):
      case MV64460_REG_ETH_CRDP(6):
      case MV64460_REG_ETH_CRDP(7):
         reg = (offset - MV64460_REG_ETH_CRDP(0)) >> 4;

         if (op_type == MTS_READ)
            *data = port->crdp[reg];
         else
            port->crdp[reg] = *data;
         break;

      case MV64460_REG_ETH_TCQDP(0):
      case MV64460_REG_ETH_TCQDP(1):
      case MV64460_REG_ETH_TCQDP(2):
      case MV64460_REG_ETH_TCQDP(3):
      case MV64460_REG_ETH_TCQDP(4):
      case MV64460_REG_ETH_TCQDP(5):
      case MV64460_REG_ETH_TCQDP(6):
      case MV64460_REG_ETH_TCQDP(7):
         reg = (offset - MV64460_REG_ETH_TCQDP(0)) >> 2;

         if (op_type == MTS_READ)
            *data = port->tcqdp[reg];
         else
            port->tcqdp[reg] = *data;
         break;

      case MV64460_REG_ETH_MIB_GOOD_RX_BYTES:
         if (op_type == MTS_READ) {
            *data = port->mib_good_rx_bytes >> 32;
         } else {
            port->mib_good_rx_bytes &= 0xFFFFFFFFULL;
            port->mib_good_rx_bytes |= *data << 32;
         }
         break;

      case MV64460_REG_ETH_MIB_GOOD_RX_BYTES+4:
         if (op_type == MTS_READ)
            *data = port->mib_good_rx_bytes;
         else
            port->mib_good_rx_bytes = *data;
         break;

      case MV64460_REG_ETH_MIB_GOOD_RX_FRAMES:
         if (op_type == MTS_READ)
            *data = port->mib_good_rx_frames;
         else
            port->mib_good_rx_frames = *data;
         break;

      case MV64460_REG_ETH_MIB_GOOD_TX_BYTES:
         if (op_type == MTS_READ) {
            *data = port->mib_good_tx_bytes >> 32;
         } else {
            port->mib_good_tx_bytes &= 0xFFFFFFFFULL;
            port->mib_good_tx_bytes |= *data << 32;
         }
         break;

      case MV64460_REG_ETH_MIB_GOOD_TX_BYTES+4:
         if (op_type == MTS_READ)
            *data = port->mib_good_tx_bytes;
         else
            port->mib_good_tx_bytes = *data;
         break;

      case MV64460_REG_ETH_MIB_GOOD_TX_FRAMES:
         if (op_type == MTS_READ)
            *data = port->mib_good_tx_frames;
         else
            port->mib_good_tx_frames = *data;
         break;

      default:
#if DEBUG_ETH_UNKNOWN
         if (op_type == MTS_READ) {
            MV64460_LOG(mv_data,
                        "read access to unknown ETH register 0x%x, "
                        "pc=0x%llx\n",
                        offset,cpu_get_pc(cpu));
         } else {
            MV64460_LOG(mv_data,
                        "write access to unknown ETH register 0x%x, "
                        "value=0x%llx, pc=0x%llx\n",
                        offset,*data,cpu_get_pc(cpu));
         }
#endif
         return(FALSE);
   }

   return(TRUE);
}

/* Bind a NIO to an Ethernet port */
int dev_mv64460_eth_set_nio(struct mv64460_data *d,u_int port_id,
                            netio_desc_t *nio)
{
   struct eth_port *port;

   if (!d || (port_id >= MV64460_ETH_PORTS))
      return(-1);

   port = &d->eth_ports[port_id];

   /* check that a NIO is not already bound */
   if (port->nio != NULL)
      return(-1);

   port->nio = nio;
   netio_rxl_add(nio,(netio_rx_handler_t)mv64460_eth_handle_rx_pkt,
                 d,(void *)(u_long)port_id);
   return(0);
}

/* Unbind a NIO from an Ethernet port */
int dev_mv64460_eth_unset_nio(struct mv64460_data *d,u_int port_id)
{
   struct eth_port *port;

   if (!d || (port_id >= MV64460_ETH_PORTS))
      return(-1);

   port = &d->eth_ports[port_id];

   if (port->nio != NULL) {
      netio_rxl_remove(port->nio);
      port->nio = NULL;
   }

   return(0);
}

/* ======================================================================== */

/*
 * dev_mv64460_access()
 */
void *dev_mv64460_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct mv64460_data *mv_data = dev->priv_data;

   MV64460_LOCK(mv_data);

   if (op_type == MTS_READ) {
      *data = 0;
   } else {
      if (op_size == 4)
         *data = swap32(*data);
   }

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"MV64460",
              "read access to register 0x%x, pc=0x%llx\n",
              offset,cpu_get_pc(cpu));
   } else {
      cpu_log(cpu,"MV64460",
              "write access to register 0x%x, value=0x%llx, pc=0x%llx\n",
              offset,*data,cpu_get_pc(cpu));
   }
#endif

   /* IDMA channel registers */
   if (mv64460_idma_access(cpu,dev,offset,op_size,op_type,data) != 0)
      goto done;

   /* IDMA decode registers */
   if (mv64460_idma_dec_access(cpu,dev,offset,op_size,op_type,data) != 0)
      goto done;

   /* Serial DMA channel registers */
   if (mv64460_sdma_access(cpu,dev,offset,op_size,op_type,data) != 0)
      goto done;

   /* MPSC registers */
   if (mv64460_mpsc_access(cpu,dev,offset,op_size,op_type,data) != 0)
      goto done;

   /* Gigabit Ethernet registers */
   if (mv64460_eth_access(cpu,dev,offset,op_size,op_type,data) != 0)
      goto done;

   switch(offset) {
      /* Interrupt Main Cause Low */
      case MV64460_REG_ILMCR:
         if (op_type == MTS_READ)
            *data = mv_data->intr_lo;
         break;
         
      /* Interrupt Main Cause High */
      case MV64460_REG_IHMCR:
         if (op_type == MTS_READ)
            *data = mv_data->intr_hi;
         break;

      /* CPU_INTn[0] Mask Low */
      case MV64460_REG_CPU_INTN0_MASK_LO:
         if (op_type == MTS_READ) {
            *data = mv_data->cpu_intn0_mask_lo;
         } else {
            mv_data->cpu_intn0_mask_lo = *data;
            mv64460_ic_update_cpu0_status(mv_data);
         }
         break;

      /* CPU_INTn[0] Mask High */
      case MV64460_REG_CPU_INTN0_MASK_HI:
         if (op_type == MTS_READ) {
            *data = mv_data->cpu_intn0_mask_hi;
         } else {
            mv_data->cpu_intn0_mask_hi = *data;
            mv64460_ic_update_cpu0_status(mv_data);
         }
         break;

      /* CPU_INTn[0] Select Cause (read-only) */
      case MV64460_REG_CPU_INTN0_SEL_CAUSE:
         if (op_type == MTS_READ) {
            *data = mv64460_ic_get_sel_cause(mv_data,
                                             mv_data->cpu_intn0_mask_lo,
                                             mv_data->cpu_intn0_mask_hi);
         }
         break;

      /* CPU_INTn[1] Mask Low */
      case MV64460_REG_CPU_INTN1_MASK_LO:
         if (op_type == MTS_READ)
            *data = mv_data->cpu_intn1_mask_lo;
         else
            mv_data->cpu_intn1_mask_lo = *data;
         break;

      /* CPU_INTn[1] Mask High */
      case MV64460_REG_CPU_INTN1_MASK_HI:
         if (op_type == MTS_READ)
            *data = mv_data->cpu_intn1_mask_hi;
         else
            mv_data->cpu_intn1_mask_hi = *data;
         break;

      /* CPU_INTn[1] Select Cause (read-only) */
      case MV64460_REG_CPU_INTN1_SEL_CAUSE:
         if (op_type == MTS_READ) {
            *data = mv64460_ic_get_sel_cause(mv_data,
                                             mv_data->cpu_intn1_mask_lo,
                                             mv_data->cpu_intn1_mask_hi);
         }
         break;

      /* INT0n Mask Low */
      case MV64460_REG_INT0N_MASK_LO:
         if (op_type == MTS_READ)
            *data = mv_data->int0n_mask_lo;
         else
            mv_data->int0n_mask_lo = *data;
         break;

      /* INT0n Mask High */
      case MV64460_REG_INT0N_MASK_HI:
         if (op_type == MTS_READ)
            *data = mv_data->int0n_mask_hi;
         else
            mv_data->int0n_mask_hi = *data;
         break;

      /* INT0n Select Cause (read-only) */
      case MV64460_REG_INT0N_SEL_CAUSE:
         if (op_type == MTS_READ) {
            *data = mv64460_ic_get_sel_cause(mv_data,
                                             mv_data->int0n_mask_lo,
                                             mv_data->int0n_mask_hi);
         }
         break;

      /* INT1n Mask Low */
      case MV64460_REG_INT1N_MASK_LO:
         if (op_type == MTS_READ)
            *data = mv_data->int1n_mask_lo;
         else
            mv_data->int1n_mask_lo = *data;
         break;

      /* INT1n Mask High */
      case MV64460_REG_INT1N_MASK_HI:
         if (op_type == MTS_READ)
            *data = mv_data->int1n_mask_hi;
         else
            mv_data->int1n_mask_hi = *data;
         break;

      /* INT1n Select Cause (read-only) */
      case MV64460_REG_INT1N_SEL_CAUSE:
         if (op_type == MTS_READ) {
            *data = mv64460_ic_get_sel_cause(mv_data,
                                             mv_data->int1n_mask_lo,
                                             mv_data->int1n_mask_hi);
         }
         break;

      /* ===== PCI Bus 0 ===== */
      case PCI_BUS_ADDR:    /* pci configuration address (0xcf8) */
         pci_dev_addr_handler(cpu,mv_data->bus[0],op_type,FALSE,data);
         break;

      case PCI_BUS_DATA:    /* pci data address (0xcfc) */
         pci_dev_data_handler(cpu,mv_data->bus[0],op_type,FALSE,data);
         break;

      /* ===== PCI Bus 0 ===== */
      case 0xc78:           /* pci configuration address (0xc78) */
         pci_dev_addr_handler(cpu,mv_data->bus[1],op_type,FALSE,data);
         break;

      case 0xc7c:           /* pci data address (0xc7c) */
         pci_dev_data_handler(cpu,mv_data->bus[1],op_type,FALSE,data);
         break;

      /* GPP interrupt cause */
      case MV64460_REG_GPP_INTR_CAUSE:
         if (op_type == MTS_READ)
            *data = mv_data->gpp_intr;
         break;

      /* GPP interrupt mask0 */
      case MV64460_REG_GPP_INTR_MASK0:
         if (op_type == MTS_READ) {
            *data = mv_data->gpp_mask0;
         } else {
            mv_data->gpp_mask0 = *data;
            mv64460_gpio_update_int_status(mv_data);
         }
         break;

      /* GPP interrupt mask1 */
      case MV64460_REG_GPP_INTR_MASK1:
         if (op_type == MTS_READ) {
            *data = mv_data->gpp_mask1;
         } else {
            mv_data->gpp_mask1 = *data;
            mv64460_gpio_update_int_status(mv_data);
         }
         break;

      /* GPP value set */
      case MV64460_REG_GPP_VALUE_SET:
         if (op_type == MTS_WRITE) {
            mv_data->gpp_intr |= *data;
            mv64460_gpio_update_int_status(mv_data);
         }
         break;

      /* GPP value clear */
      case MV64460_REG_GPP_VALUE_CLEAR:
         if (op_type == MTS_WRITE) {
            mv_data->gpp_intr &= ~(*data);
            mv64460_gpio_update_int_status(mv_data);
         }
         break;

      /* SDMA cause register */
      case MV64460_REG_SDMA_CAUSE:
         if (op_type == MTS_READ) {
            *data = mv_data->sdma_cause;
         } else {
            mv_data->sdma_cause = *data;
            mv64460_sdma_update_int_status(mv_data);
         }
         break;

      /* SDMA mask register */
      case MV64460_REG_SDMA_MASK:
         if (op_type == MTS_READ) {
            *data = mv_data->sdma_mask;
         } else {
            mv_data->sdma_mask = *data;
            mv64460_sdma_update_int_status(mv_data);
         }
         break;

      /* Integrated SRAM base address */ 
      case MV64460_REG_SRAM_BASE:
         if (op_type == MTS_READ) {
            *data = mv_data->sram_dev.phys_addr << MV64460_SRAM_WIDTH;
         } else {
            m_uint64_t sram_addr;

            sram_addr = *data & MV64460_SRAM_BASE_MASK;
            sram_addr >>= MV64460_SRAM_BASE_SHIFT;
            sram_addr <<= MV64460_SRAM_WIDTH;

            vm_map_device(mv_data->vm,&mv_data->sram_dev,sram_addr);

            MV64460_LOG(mv_data,"SRAM mapped at 0x%10.10llx\n",
                        mv_data->sram_dev.phys_addr);
         }
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,"MV64460","read from addr 0x%x, pc=0x%llx\n",
                    offset,cpu_get_pc(cpu));
         } else {
            cpu_log(cpu,"MV64460","write to addr 0x%x, value=0x%llx, "
                    "pc=0x%llx\n",offset,*data,cpu_get_pc(cpu));
         }
#endif        
   }

 done:
   MV64460_UNLOCK(mv_data);
   if ((op_type == MTS_READ) && (op_size == 4))
      *data = swap32(*data);
   return NULL;
}

/* Set value of GPP register */
void dev_mv64460_set_gpp_reg(struct mv64460_data *d,m_uint32_t val)
{
   d->gpp_intr = val;
   mv64460_gpio_update_int_status(d);
}

/* Set a GPP interrupt */
void dev_mv64460_set_gpp_intr(struct mv64460_data *d,u_int irq)
{
   d->gpp_intr |= 1 << irq;
   mv64460_gpio_update_int_status(d);
}

/* Clear a GPP interrupt */
void dev_mv64460_clear_gpp_intr(struct mv64460_data *d,u_int irq)
{
   d->gpp_intr &= ~(1 << irq);
   mv64460_gpio_update_int_status(d);
}

/*
 * pci_mv64460_read()
 *
 * Read a PCI register.
 */
static m_uint32_t pci_mv64460_read(cpu_gen_t *cpu,struct pci_device *dev,
                                   int reg)
{   
   switch (reg) {
      default:
         return(0);
   }
}

/* Shutdown a MV64460 system controller */
void dev_mv64460_shutdown(vm_instance_t *vm,struct mv64460_data *d)
{
   if (d != NULL) {     
      /* Stop the Ethernet TX ring scanner */
      ptask_remove(d->eth_tx_tid);

      /* Remove the SRAM */
      dev_remove(vm,&d->sram_dev);

      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Remove the PCI device */
      pci_dev_remove(d->pci_dev);

      /* Free the structure itself */
      free(d);
   }
}

/* Create a new MV64460 controller */
int dev_mv64460_init(vm_instance_t *vm,char *name,
                     m_uint64_t paddr,m_uint32_t len)
{
   struct mv64460_data *d;
   int i;

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"mv64460: unable to create device data.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   pthread_mutex_init(&d->lock,NULL);
   d->name = name;
   d->vm = vm;
   d->bus[0] = vm->pci_bus[0];
   d->bus[1] = vm->pci_bus[1];

   for(i=0;i<MV64460_SDMA_CHANNELS;i++)
      d->sdma[i].id = i;

   vm_object_init(&d->vm_obj);
   d->vm_obj.name = name;
   d->vm_obj.data = d;
   d->vm_obj.shutdown = (vm_shutdown_t)dev_mv64460_shutdown;

   dev_init(&d->dev);
   d->dev.name      = name;
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = len;
   d->dev.handler   = dev_mv64460_access;

   /* Ethernet */
   d->eth_phy_addr =  MV64460_ETH_PHY0_ADDR;
   d->eth_phy_addr |= MV64460_ETH_PHY1_ADDR << 5;
   d->eth_phy_addr |= MV64460_ETH_PHY2_ADDR << 10;

   for(i=0;i<MV64460_ETH_PORTS;i++) {
      d->eth_ports[i].id = i;
      d->eth_ports[i].vlan_ether_type = N_ETH_PROTO_DOT1Q;
   }

   /* Create the SRAM device */
   dev_init(&d->sram_dev);
   d->sram_dev.name = "mv64460_sram";
   d->sram_dev.phys_len = MV64460_SRAM_SIZE;
   d->sram_dev.flags = VDEVICE_FLAG_CACHING;
   d->sram_dev.host_addr = (m_iptr_t)m_memalign(4096,d->sram_dev.phys_len);

   if (!d->sram_dev.host_addr) {
      fprintf(stderr,"mv64460: unable to create SRAM data.\n");
      return(-1);
   }

   /* Add the controller as a PCI device */
   if (!pci_dev_lookup(d->bus[0],0,0,0)) {
      d->pci_dev = pci_dev_add(d->bus[0],name,
                               PCI_VENDOR_MARVELL,PCI_PRODUCT_MARVELL_MV64460,
                               0,0,-1,d,NULL,pci_mv64460_read,NULL);
      if (!d->pci_dev) {
         fprintf(stderr,"mv64460: unable to create PCI device.\n");
         return(-1);
      }
   }

   /* TEST */
   pci_dev_add(d->bus[1],name,
               PCI_VENDOR_MARVELL,PCI_PRODUCT_MARVELL_MV64460,
               0,0,-1,d,NULL,pci_mv64460_read,NULL);

   /* Start the Ethernet TX ring scanner */
   d->eth_tx_tid = ptask_add((ptask_callback)mv64460_eth_handle_txqueues,
                             d,NULL);

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
