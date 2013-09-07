/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * NM-16ESW ethernet switch module (experimental!)
 *
 * It's an attempt of proof of concept, so not optimized at all at this time.
 * Only L2 switching will be managed (no L3 at all).
 *
 * To do next: QoS features (CoS/DSCP handling).
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
#include "timer.h"
#include "net.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_nm_16esw.h"

/* Debugging flags */
#define DEBUG_ACCESS     0
#define DEBUG_UNKNOWN    0
#define DEBUG_MII        0
#define DEBUG_MEM        0
#define DEBUG_REG        0
#define DEBUG_TRANSMIT   0
#define DEBUG_RECEIVE    0
#define DEBUG_FORWARD    0
#define DEBUG_MIRROR     0
#define DEBUG_ARL        0

/* Invalid VLAN value */
#define VLAN_INVALID   0xFFF

/* Maximum packet size */
#define BCM5600_MAX_PKT_SIZE  2048

/* PCI vendor/product codes */
#define BCM5605_PCI_VENDOR_ID   0x14e4
#define BCM5605_PCI_PRODUCT_ID  0x5605

/* "S-channel" commands */
#define BCM5600_SCHAN_CMD_LINKSCAN    0x13
#define BCM5600_SCHAN_CMD_EXEC        0x80
#define BCM5600_SCHAN_CMD_READ_MII    0x90
#define BCM5600_SCHAN_CMD_WRITE_MII   0x91

/* Opcodes */
#define BCM5600_OP_BP_WARN_STATUS     0x01
#define BCM5600_OP_BP_DISCARD_STATUS  0x02
#define BCM5600_OP_COS_QSTAT_NOTIFY   0x03
#define BCM5600_OP_HOL_STAT_NOTIFY    0x04
#define BCM5600_OP_GBP_FULL_NOTIFY    0x05
#define BCM5600_OP_GBP_AVAIL_NOTIFY   0x06
#define BCM5600_OP_READ_MEM_CMD       0x07
#define BCM5600_OP_READ_MEM_ACK       0x08
#define BCM5600_OP_WRITE_MEM_CMD      0x09
#define BCM5600_OP_WRITE_MEM_ACK      0x0A
#define BCM5600_OP_READ_REG_CMD       0x0B
#define BCM5600_OP_READ_REG_ACK       0x0C
#define BCM5600_OP_WRITE_REG_CMD      0x0D
#define BCM5600_OP_WRITE_REG_ACK      0x0E
#define BCM5600_OP_ARL_INSERT_CMD     0x0F
#define BCM5600_OP_ARL_INSERT_DONE    0x10
#define BCM5600_OP_ARL_DELETE_CMD     0x11
#define BCM5600_OP_ARL_DELETE_DONE    0x12
#define BCM5600_OP_LINKSTAT_NOTIFY    0x13
#define BCM5600_OP_MEM_FAIL_NOTIFY    0x14
#define BCM5600_OP_INIT_CFAP          0x15
#define BCM5600_OP_INIT_SFAP          0x16
#define BCM5600_OP_ENTER_DEBUG_MODE   0x17
#define BCM5600_OP_EXIT_DEBUG_MODE    0x18
#define BCM5600_OP_ARL_LOOKUP_CMD     0x19

/* Command details */
#define BCM5600_CMD_OP_MASK      0xFC000000
#define BCM5600_CMD_OP_SHIFT     26
#define BCM5600_CMD_DST_MASK     0x03F00000
#define BCM5600_CMD_DST_SHIFT    20
#define BCM5600_CMD_SRC_MASK     0x000FC000
#define BCM5600_CMD_SRC_SHIFT    14
#define BCM5600_CMD_LEN_MASK     0x00003F80
#define BCM5600_CMD_LEN_SHIFT    7
#define BCM5600_CMD_EBIT_MASK    0x00000040
#define BCM5600_CMD_EBIT_SHIFT   6
#define BCM5600_CMD_ECODE_MASK   0x00000030
#define BCM5600_CMD_ECODE_SHIFT  4
#define BCM5600_CMD_COS_MASK     0x0000000E
#define BCM5600_CMD_COS_SHIFT    1
#define BCM5600_CMD_CPU_MASK     0x00000001
#define BCM5600_CMD_CPU_SHIFT    0

/* Memory zones */
#define BCM5600_ADDR_ARLCNT0     0x01000000
#define BCM5600_ADDR_ARLCNT1     0x01100000
#define BCM5600_ADDR_ARLCNT2     0x01200000
#define BCM5600_ADDR_ARL0        0x02000000
#define BCM5600_ADDR_ARL1        0x02100000
#define BCM5600_ADDR_ARL2        0x02200000
#define BCM5600_ADDR_PTABLE0     0x03000000
#define BCM5600_ADDR_PTABLE1     0x03100000
#define BCM5600_ADDR_PTABLE2     0x03200000
#define BCM5600_ADDR_VTABLE0     0x05000000
#define BCM5600_ADDR_VTABLE1     0x05100000
#define BCM5600_ADDR_VTABLE2     0x05200000
#define BCM5600_ADDR_TTR0        0x06000000
#define BCM5600_ADDR_TBMAP0      0x06010000
#define BCM5600_ADDR_TTR1        0x06100000
#define BCM5600_ADDR_TBMAP1      0x06110000
#define BCM5600_ADDR_TTR2        0x06200000
#define BCM5600_ADDR_TBMAP2      0x06210000
#define BCM5600_ADDR_IMASK0      0x07000000
#define BCM5600_ADDR_IRULE0      0x07020000
#define BCM5600_ADDR_IMASK1      0x07100000
#define BCM5600_ADDR_IRULE1      0x07120000
#define BCM5600_ADDR_IMASK2      0x07200000
#define BCM5600_ADDR_IRULE2      0x07220000
#define BCM5600_ADDR_GIMASK      0x07300000
#define BCM5600_ADDR_GIRULE      0x07320000
#define BCM5600_ADDR_MARL0       0x08000000
#define BCM5600_ADDR_MARL1       0x08100000
#define BCM5600_ADDR_MARL2       0x08200000
#define BCM5600_ADDR_L3          0x09000000
#define BCM5600_ADDR_DEFIP       0x09010000
#define BCM5600_ADDR_L3INTF      0x09020000
#define BCM5600_ADDR_IPMC        0x09030000
#define BCM5600_ADDR_CBPHDR      0x0A600000
#define BCM5600_ADDR_CAB0        0x0A610000
#define BCM5600_ADDR_CAB1        0x0A620000
#define BCM5600_ADDR_CAB2        0x0A630000
#define BCM5600_ADDR_CAB3        0x0A640000
#define BCM5600_ADDR_CCP         0x0A650000
#define BCM5600_ADDR_PPP         0x0A660000
#define BCM5600_ADDR_CFAP        0x0A670000
#define BCM5600_ADDR_SFAP        0x0A680000
#define BCM5600_ADDR_CBPDATA0    0x0A6A0000
#define BCM5600_ADDR_CBPDATA1    0x0A6B0000
#define BCM5600_ADDR_CBPDATA2    0x0A6C0000
#define BCM5600_ADDR_CBPDATA3    0x0A6D0000
#define BCM5600_ADDR_PID         0x0A900000
#define BCM5600_ADDR_XQ_BASE     0x0B600000
#define BCM5600_ADDR_GBP         0x12000000

/* Number of "Data Words" */
#define BCM5600_DW_MAX  32

/* === VTABLE definitions === */
/* Word 0 */
#define BCM5600_VTABLE_VLAN_TAG_MASK      0x00000FFF

/* Word 1: Port bitmap */
#define BCM5600_VTABLE_PORT_BMAP_MASK     0x1FFFFFFF

/* Word 2: Untagged port bitmap */
#define BCM5600_VTABLE_UT_PORT_BMAP_MASK  0x1FFFFFFF

/* Word 3: Module bitmap */
#define BCM5600_VTABLE_MOD_BMAP_MASK      0xFFFFFFFF

/* === PTABLE definitions === */
/* Word 0 */
#define BCM5600_PTABLE_VLAN_TAG_MASK    0x00000FFF
#define BCM5600_PTABLE_SP_ST_MASK       0x00003000
#define BCM5600_PTABLE_SP_ST_SHIFT      12
#define BCM5600_PTABLE_PRT_DIS_MASK     0x000FC000
#define BCM5600_PTABLE_PRT_DIS_SHIFT    14
#define BCM5600_PTABLE_JUMBO_FLAG       0x00100000
#define BCM5600_PTABLE_RTAG_MASK        0x00E00000
#define BCM5600_PTABLE_RTAG_SHIFT       21
#define BCM5600_PTABLE_TGID_MASK        0x07000000
#define BCM5600_PTABLE_TGID_SHIFT       24
#define BCM5600_PTABLE_TRUNK_FLAG       0x08000000
#define BCM5600_PTABLE_CPU_FLAG         0x10000000
#define BCM5600_PTABLE_PTYPE_MASK       0x60000000
#define BCM5600_PTABLE_PTYPE_SHIFT      29
#define BCM5600_PTABLE_BPDU_FLAG        0x80000000

/* Word 1 */
#define BCM5600_PTABLE_PORT_BMAP_MASK   0x1FFFFFFF
#define BCM5600_PTABLE_MI_FLAG          0x20000000
#define BCM5600_PTABLE_CML_MASK         0xC0000000
#define BCM5600_PTABLE_CML_SHIFT        30

/* Word 2 */
#define BCM5600_PTABLE_UT_PORT_BMAP_MASK  0x1FFFFFFF

/* Word 4 */
#define BCM5600_PTABLE_DSCP_MASK        0x0000003F
#define BCM5600_PTABLE_DSCP_SHIFT       0
#define BCM5600_PTABLE_DSE_MODE_MASK    0x000000C0
#define BCM5600_PTABLE_DSE_MODE_SHIFT   6
#define BCM5600_PTABLE_RPE_FLAG         0x00000100
#define BCM5600_PTABLE_PRI_MASK         0x00000E00
#define BCM5600_PTABLE_PRI_SHIFT        9
#define BCM5600_PTABLE_L3_DIS_FLAG      0x00001000


/* === ARL (Addess Resolution Logic) definitions === */
/* Word 0: MAC address LSB */

/* Word 1 */
#define BCM5600_ARL_MAC_MSB_MASK        0x0000FFFF
#define BCM5600_ARL_VLAN_TAG_MASK       0x0FFF0000
#define BCM5600_ARL_VLAN_TAG_SHIFT      16
#define BCM5600_ARL_COS_DST_MASK        0x70000000
#define BCM5600_ARL_COS_DST_SHIFT       28
#define BCM5600_ARL_CPU_FLAG            0x80000000

/* Word 2 */
#define BCM5600_ARL_L3_FLAG             0x00000001
#define BCM5600_ARL_SD_DIS_MASK         0x00000006
#define BCM5600_ARL_SD_DIS_SHIFT        1
#define BCM5600_ARL_ST_FLAG             0x00000008
#define BCM5600_ARL_HIT_FLAG            0x00000010
#define BCM5600_ARL_COS_SRC_MASK        0x000000E0
#define BCM5600_ARL_COS_SRC_SHIFT       5
#define BCM5600_ARL_TRUNK_FLAG          0x00000100
#define BCM5600_ARL_TGID_MASK           0x00000E00
#define BCM5600_ARL_TGID_SHIFT          9
#define BCM5600_ARL_RTAG_MASK           0x00007000
#define BCM5600_ARL_RTAG_SHIFT          12
#define BCM5600_ARL_PORT_MASK           0x001F8000
#define BCM5600_ARL_PORT_SHIFT          15
#define BCM5600_ARL_SCP_FLAG            0x00200000
#define BCM5600_ARL_MOD_ID_MASK         0x07C00000
#define BCM5600_ARL_MOD_ID_SHIFT        22

/* === Multicast ARL definitions === */
/* Word 0: MAC address LSB */

/* Word 1 */
#define BCM5600_MARL_MAC_MSB_MASK       0x0000FFFF
#define BCM5600_MARL_VLAN_TAG_MASK      0x0FFF0000
#define BCM5600_MARL_VLAN_TAG_SHIFT     16
#define BCM5600_MARL_COS_DST_MASK       0x70000000
#define BCM5600_MARL_COS_DST_SHIFT      28

/* Word 2 */
#define BCM5600_MARL_PORT_BMAP_MASK     0x1FFFFFFF

/* Word 3 */
#define BCM5600_MARL_UT_PORT_BMAP_MASK  0x1FFFFFFF

/* Word 4 */
#define BCM5600_MARL_MOD_BMAP_MASK      0xFFFFFFFF

/* === Trunk bitmap === */
#define BCM5600_TBMAP_MASK     0x0FFFFFFF

/* === Trunk table === */
/* Word 0 */
#define BCM5600_TTR_TP0_MASK   0x0000003F
#define BCM5600_TTR_TP0_SHIFT  0
#define BCM5600_TTR_TP1_MASK   0x00000FC0
#define BCM5600_TTR_TP1_SHIFT  6
#define BCM5600_TTR_TP2_MASK   0x0003F000
#define BCM5600_TTR_TP2_SHIFT  12
#define BCM5600_TTR_TP3_MASK   0x00FC0000
#define BCM5600_TTR_TP3_SHIFT  18
#define BCM5600_TTR_TP4_MASK   0x3F000000
#define BCM5600_TTR_TP4_SHIFT  24

/* Word 1 */
#define BCM5600_TTR_TP5_MASK   0x0000003F
#define BCM5600_TTR_TP5_SHIFT  0
#define BCM5600_TTR_TP6_MASK   0x00000FC0
#define BCM5600_TTR_TP6_SHIFT  6
#define BCM5600_TTR_TP7_MASK   0x0003F000
#define BCM5600_TTR_TP7_SHIFT  12

#define BCM5600_TTR_TG_SIZE_MASK    0x003C0000
#define BCM5600_TTR_TG_SIZE_SHIFT   18

/* Trunks (port aggregation) */
#define BCM5600_MAX_TRUNKS  6
#define BCM5600_MAX_PORTS_PER_TRUNK  8

/* ======================================================================= */

/* Transmit descriptor size */
#define BCM5600_TXD_SIZE       32
#define BCM5600_TXD_RING_CONT  0x80000000   /* ring is continuing */
#define BCM5600_TXD_UNKNOWN    0x04000000   /* valid packet (?) */
#define BCM5600_TXD_NEOP       0x00040000   /* end of packet if not set */

/* Receive descriptor size */
#define BCM5600_RXD_SIZE       32
#define BCM5600_RXD_RING_CONT  0x80000000   /* ring is continuing */
#define BCM5600_RXD_UNKNOWN    0x00040000   /* unknown */

/* Interrupt sources */
#define BCM5600_INTR_STAT_ITER_DONE  0x00100000  /* Unknown */
#define BCM5600_INTR_RX_UNDERRUN     0x00000400  /* RX ring underrun */
#define BCM5600_INTR_RX_AVAIL        0x00000200  /* packet available */
#define BCM5600_INTR_TX_UNDERRUN     0x00000100  /* TX ring underrun */
#define BCM5600_INTR_LINKSTAT_MOD    0x00000010  /* Link status modified */

/* ======================================================================= */

/* Port Mirroring */
#define BCM5600_MIRROR_ENABLE     0x40
#define BCM5600_MIRROR_PORT_MASK  0x3F

/* ======================================================================= */

#define BCM5600_REG_HASH_SIZE  8192

/* BCM5600 register */
struct bcm5600_reg {
   m_uint32_t addr;
   m_uint32_t value;
   struct bcm5600_reg *next;
};

/* BCM5600 table */
struct bcm5600_table {
   char *name; 
   long offset;
   m_uint32_t addr;
   u_int min_index,max_index;
   u_int nr_words;
};

/* BCM5600 in-transit packet */
struct bcm5600_pkt {
   /* Received packet data */
   u_char *pkt;
   ssize_t pkt_len;

   /* Rewritten packet (802.1Q tag pushed or poped) */
   u_char *rewr_pkt;
   int rewrite_done;

   /* Original VLAN (-1 for untagged packet) and Real VLAN */
   int orig_vlan,real_vlan;

   /* VLAN entry */
   m_uint32_t *vlan_entry;

   /* Ingress Port and Egress Port bitmap */
   u_int ingress_port,egress_bitmap,egress_ut_bitmap;
   u_int egress_filter_bitmap;

   /* RX descriptor */
   m_uint32_t rdes[4];

   /* Packet sent to CPU */
   u_int sent_to_cpu;
};

/* BCM5600 physical port */
struct bcm5600_port {
   netio_desc_t *nio;
   u_int id;
   char name[32];
};

/* NM-16ESW private data */
struct nm_16esw_data {
   char *name;
   u_int nr_port;

   vm_instance_t *vm;
   struct vdevice *dev;
   struct pci_device *pci_dev;

   pthread_mutex_t lock;

   /* Ager task */
   timer_id ager_tid;

   /* S-channel command and command result */
   m_uint32_t schan_cmd,schan_cmd_res;
   
   /* Data Words */
   m_uint32_t dw[BCM5600_DW_MAX];

   /* Interrupt mask */
   m_uint32_t intr_mask;

   /* MII registers */
   m_uint16_t mii_regs[64][32];
   m_uint32_t mii_input,mii_output;
   u_int mii_intr;

   /* RX/TX rings addresses */
   m_uint32_t rx_ring_addr,tx_ring_addr;
   m_uint32_t tx_current,tx_end_scan;
   m_uint32_t rx_current,rx_end_scan;

   /* TX ring scanner task id */
   ptask_id_t tx_tid;

   /* TX buffer */
   u_char tx_buffer[BCM5600_MAX_PKT_SIZE];
   u_int tx_bufsize;
   
   /* Port Mirroring */
   u_int mirror_dst_port;
   u_int mirror_egress_ports;

   /* Registers hash table */
   struct bcm5600_reg *reg_hash_table[BCM5600_REG_HASH_SIZE];
   
   /* Most used tables... */
   struct bcm5600_table *t_ptable,*t_vtable;
   struct bcm5600_table *t_arl,*t_marl;
   struct bcm5600_table *t_tbmap,*t_ttr;

   /* Ports (only 16 are "real" and usable) */
   struct bcm5600_port ports[32];
   
   /* CPU port */
   u_int cpu_port;

   /* Current egress port of all trunks */
   u_int trunk_last_egress_port[BCM5600_MAX_TRUNKS];

   /* ARL count table */
   m_uint32_t *arl_cnt;

   /* ARL (Address Resolution Logic) Table */
   m_uint32_t *arl_table;

   /* Multicast ARL Table */
   m_uint32_t *marl_table;

   /* VTABLE (VLAN Table) */
   m_uint32_t *vtable;

   /* Trunks */
   m_uint32_t *ttr,*tbmap;

   /* PTABLE (Port Table) */
   m_uint32_t *ptable;
};

/* NM-16ESW Port physical port mapping table (Cisco => BCM) */
static int nm16esw_port_mapping[] = {
   2, 0, 6, 4, 10, 8, 14, 12, 3, 1, 7, 5, 11, 9, 15, 13,
};

/* Log a BCM message */
#define BCM_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/* Lock/Unlock primitives */
#define BCM_LOCK(d)    pthread_mutex_lock(&(d)->lock)
#define BCM_UNLOCK(d)  pthread_mutex_unlock(&(d)->lock)

/* Trunk group info */
struct bcm5600_tg_info {
   u_int index,mask,shift;
};

static struct bcm5600_tg_info tg_info[8] = {
   { 0, BCM5600_TTR_TP0_MASK, BCM5600_TTR_TP0_SHIFT },
   { 0, BCM5600_TTR_TP1_MASK, BCM5600_TTR_TP1_SHIFT },
   { 0, BCM5600_TTR_TP2_MASK, BCM5600_TTR_TP2_SHIFT },
   { 0, BCM5600_TTR_TP3_MASK, BCM5600_TTR_TP3_SHIFT },
   { 0, BCM5600_TTR_TP4_MASK, BCM5600_TTR_TP4_SHIFT },
   { 1, BCM5600_TTR_TP5_MASK, BCM5600_TTR_TP5_SHIFT },
   { 1, BCM5600_TTR_TP6_MASK, BCM5600_TTR_TP6_SHIFT },
   { 1, BCM5600_TTR_TP7_MASK, BCM5600_TTR_TP7_SHIFT },
};

/* Return port status (up or down), based on the MII register */
static int bcm5600_mii_port_status(struct nm_16esw_data *d,u_int port)
{
   u_int mii_ctrl;

   mii_ctrl = d->mii_regs[port][0x00];

   /* Isolate bit */
   return(!(mii_ctrl & 0x400));
}

/* Build port status bitmap */
static m_uint32_t bcm5600_mii_port_status_bmp(struct nm_16esw_data *d)
{
   m_uint32_t bmp;
   int i;
   
   for(i=0,bmp=0;i<d->nr_port;i++)
      if (bcm5600_mii_port_status(d,i))
         bmp |= 1 << i;

   return(bmp);
}

/* Read a MII register */
static void bcm5600_mii_read(struct nm_16esw_data *d)
{
   m_uint8_t port,reg;

   port = (d->mii_input >> 16) & 0xFF;
   reg  = (d->mii_input >> 24) & 0xFF;

   if ((port < 32) && (reg < 32)) {
      d->mii_output = d->mii_regs[port][reg];

      switch(reg) {
         case 0x00:
            d->mii_output &= ~0x8200;
            break;
         case 0x01:
            if (d->ports[port].nio && bcm5600_mii_port_status(d,port))
               d->mii_output = 0x782C;
            else
               d->mii_output = 0;
            break;
         case 0x02:
            d->mii_output = 0x40;
            break;
         case 0x03:
            d->mii_output = 0x61d4;
            break;
         case 0x04:
            d->mii_output = 0x1E1;
            break;
         case 0x05:
            d->mii_output = 0x41E1;
            break;
         default:
            d->mii_output = 0;
      }
   }
}

/* Write a MII register */
static void bcm5600_mii_write(struct nm_16esw_data *d)
{
   m_uint8_t port,reg;
   m_uint16_t isolation;

   port = (d->mii_input >> 16) & 0xFF;
   reg  = (d->mii_input >> 24) & 0xFF;

   if ((port < 32) && (reg < 32))
   {
#if DEBUG_MII
      BCM_LOG(d,"MII: port 0x%4.4x, reg 0x%2.2x: writing 0x%4.4x\n",
              port,reg,d->mii_input & 0xFFFF);
#endif

      /* Check if PHY isolation status is changing */
      if (reg == 0) {
         isolation = (d->mii_input ^ d->mii_regs[port][reg]) & 0x400;

         if (isolation) {
#if DEBUG_MII
            BCM_LOG(d,"MII: port 0x%4.4x: generating IRQ\n",port);
#endif
            d->mii_intr = TRUE;
            pci_dev_trigger_irq(d->vm,d->pci_dev);
         }
      }
      
      d->mii_regs[port][reg] = d->mii_input & 0xFFFF;
   }
}

/* Hash function for register */
static u_int bcm5600_reg_get_hash(m_uint32_t addr)
{
   return((addr ^ (addr >> 16)) & (BCM5600_REG_HASH_SIZE - 1));
}

/* Find a register entry */
static struct bcm5600_reg *bcm5600_reg_find(struct nm_16esw_data *d,
                                            m_uint32_t addr)
{   
   struct bcm5600_reg *reg;
   u_int h_index;

   h_index = bcm5600_reg_get_hash(addr);
   for(reg=d->reg_hash_table[h_index];reg;reg=reg->next)
      if (reg->addr == addr)
         return reg;

   return NULL;
}

/* Read a register */
static m_uint32_t bcm5600_reg_read(struct nm_16esw_data *d,m_uint32_t addr)
{
   struct bcm5600_reg *reg;

   if (!(reg = bcm5600_reg_find(d,addr)))
      return(0);

   return(reg->value);
}

/* Write a register */
static int bcm5600_reg_write(struct nm_16esw_data *d,m_uint32_t addr,
                             m_uint32_t value)
{
   struct bcm5600_reg *reg;
   u_int h_index;

   if ((reg = bcm5600_reg_find(d,addr))) {
      reg->value = value;
      return(0);
   }

   /* create a new register */
   if (!(reg = malloc(sizeof(*reg))))
      return(-1);

   reg->addr  = addr;
   reg->value = value;

   /* insert new register in hash table */
   h_index = bcm5600_reg_get_hash(addr);
   reg->next = d->reg_hash_table[h_index];
   d->reg_hash_table[h_index] = reg;

   return(0);
}

/* Register special handling */
static void bcm5600_reg_write_special(struct nm_16esw_data *d,
                                      m_uint32_t addr,m_uint32_t value)
{
   switch(addr) {
      case 0x80006:
         d->mirror_dst_port = value;
         break;

      case 0x8000d:
         d->mirror_egress_ports = value;
         break;

      case 0x80009:
         /* age timer */
         break;
   }
}

/* Free memory used to store register info */
static void bcm5600_reg_free(struct nm_16esw_data *d)
{  
   struct bcm5600_reg *reg,*next;
   int i;

   for(i=0;i<BCM5600_REG_HASH_SIZE;i++)
      for(reg=d->reg_hash_table[i];reg;reg=next) {
         next = reg->next;
         free(reg);
      }
}

/* Dump all known registers */
static void bcm5600_reg_dump(struct nm_16esw_data *d,int show_null)
{
   struct bcm5600_reg *reg;
   int i;

   printf("%s: dumping registers:\n",d->name);

   for(i=0;i<BCM5600_REG_HASH_SIZE;i++)
      for(reg=d->reg_hash_table[i];reg;reg=reg->next) {
         if (reg->value || show_null)
            printf("  0x%8.8x: 0x%8.8x\n",reg->addr,reg->value);
      }
}

/* Fill a string buffer with all ports of the specified bitmap */
__maybe_unused static char *bcm5600_port_bitmap_str(struct nm_16esw_data *d,
                                     char *buffer,m_uint32_t bitmap)
{
   char *ptr = buffer;
   int i;

   *ptr = 0;

   for(i=0;i<d->nr_port;i++)
      if (bitmap & (1 << i)) {
         ptr += sprintf(ptr,"%s ",d->ports[i].name);
      }

   return buffer;
}

/* BCM5600 tables */
#define BCM_OFFSET(x) (OFFSET(struct nm_16esw_data,x))

static struct bcm5600_table bcm5600_tables[] = {
   /* ARL tables */
   { "arlcnt0", BCM_OFFSET(arl_cnt), BCM5600_ADDR_ARLCNT0, 0, 0, 1 },
   { "arlcnt1", BCM_OFFSET(arl_cnt), BCM5600_ADDR_ARLCNT1, 0, 0, 1 },
   { "arlcnt2", BCM_OFFSET(arl_cnt), BCM5600_ADDR_ARLCNT2, 0, 0, 1 },

   /* ARL tables */
   { "arl0", BCM_OFFSET(arl_table), BCM5600_ADDR_ARL0, 0, 8191, 3 },
   { "arl1", BCM_OFFSET(arl_table), BCM5600_ADDR_ARL1, 0, 8191, 3 },
   { "arl2", BCM_OFFSET(arl_table), BCM5600_ADDR_ARL2, 0, 8191, 3 },

   /* Multicast ARL tables */
   { "marl0", BCM_OFFSET(marl_table), BCM5600_ADDR_MARL0, 1, 255, 5 },
   { "marl1", BCM_OFFSET(marl_table), BCM5600_ADDR_MARL1, 1, 255, 5 },
   { "marl2", BCM_OFFSET(marl_table), BCM5600_ADDR_MARL2, 1, 255, 5 },

   /* PTABLE - Physical Ports */
   { "ptable0", BCM_OFFSET(ptable), BCM5600_ADDR_PTABLE0, 0, 31, 6 },
   { "ptable1", BCM_OFFSET(ptable), BCM5600_ADDR_PTABLE1, 0, 31, 6 },
   { "ptable2", BCM_OFFSET(ptable), BCM5600_ADDR_PTABLE2, 0, 31, 6 },

   /* VTABLE - VLANs */
   { "vtable0", BCM_OFFSET(vtable), BCM5600_ADDR_VTABLE0, 1, 255, 4 },
   { "vtable1", BCM_OFFSET(vtable), BCM5600_ADDR_VTABLE1, 1, 255, 4 },
   { "vtable2", BCM_OFFSET(vtable), BCM5600_ADDR_VTABLE2, 1, 255, 4 },

   /* TTR */
   { "ttr0", BCM_OFFSET(ttr), BCM5600_ADDR_TTR0, 0, 5, 3 },
   { "ttr1", BCM_OFFSET(ttr), BCM5600_ADDR_TTR1, 0, 5, 3 },
   { "ttr2", BCM_OFFSET(ttr), BCM5600_ADDR_TTR2, 0, 5, 3 },

   /* TBMAP */
   { "tbmap0", BCM_OFFSET(tbmap), BCM5600_ADDR_TBMAP0, 0, 5, 1 },
   { "tbmap1", BCM_OFFSET(tbmap), BCM5600_ADDR_TBMAP1, 0, 5, 1 },
   { "tbmap2", BCM_OFFSET(tbmap), BCM5600_ADDR_TBMAP2, 0, 5, 1 },

   { NULL, -1, 0, 0, 0 },
};

/* Get table size (in number of words) */
static inline u_int bcm5600_table_get_size(struct bcm5600_table *table)
{
   return(table->nr_words * (table->max_index + 1));
}

/* Create automatically tables */
static int bcm5600_table_create(struct nm_16esw_data *d)
{
   struct bcm5600_table *table;
   m_uint32_t *array;
   size_t nr_words;
   int i;

   for(i=0;bcm5600_tables[i].name;i++)
   {
      table = &bcm5600_tables[i];
      nr_words = bcm5600_table_get_size(table);

      if (!(array = calloc(nr_words,sizeof(m_uint32_t)))) {
         fprintf(stderr,"BCM5600: unable to create table '%s'\n",table->name);
         return(-1);
      }
         
      *(PTR_ADJUST(m_uint32_t **,d,table->offset)) = array;
   }

   return(0);
}

/* Free tables */
static void bcm5600_table_free(struct nm_16esw_data *d)
{
   struct bcm5600_table *table;
   m_uint32_t **array;
   int i;

   for(i=0;bcm5600_tables[i].name;i++) {
      table = &bcm5600_tables[i];
      array = (PTR_ADJUST(m_uint32_t **,d,table->offset));
      free(*array);
      
      /* avoid freeing the same table multiple times */
      *array = NULL;
   }
}

/* Find a table given its address */
static struct bcm5600_table *bcm5600_table_find(struct nm_16esw_data *d,
                                                m_uint32_t addr)
{
   int i;

   for(i=0;bcm5600_tables[i].name;i++)
      if (bcm5600_tables[i].addr == addr)
         return(&bcm5600_tables[i]);

#if DEBUG_UNKNOWN
   BCM_LOG(d,"unknown table at address 0x%8.8x\n",addr);
#endif
   return NULL;
}

/* Get a table entry */
static inline m_uint32_t *bcm5600_table_get_entry(struct nm_16esw_data *d,
                                                  struct bcm5600_table *table,
                                                  m_uint32_t index)
{
   m_uint32_t *array;

   if ((index < table->min_index) || (index > table->max_index))
      return NULL;

   array = *(PTR_ADJUST(m_uint32_t **,d,table->offset));
   return(&array[index*table->nr_words]);
}

/* Read a table entry */
static int bcm5600_table_read_entry(struct nm_16esw_data *d)
{
   struct bcm5600_table *table;
   m_uint32_t addr,index,*entry;
   int i;

   addr  = d->dw[1] & 0xFFFF0000;
   index = d->dw[1] & 0x0000FFFF;

   if (!(table = bcm5600_table_find(d,addr))) {
#if DEBUG_UNKNOWN
      BCM_LOG(d,"unknown mem address at address 0x%8.8x\n",d->dw[1]);
#endif
      return(-1);
   }

   if (!(entry = bcm5600_table_get_entry(d,table,index)))
      return(-1);

#if DEBUG_MEM
   BCM_LOG(d,"READ_MEM: addr=0x%8.8x (table %s)\n",d->dw[1],table->name);
#endif

   for(i=0;i<table->nr_words;i++)
      d->dw[i+1] = entry[i];

   return(0);
}

/* Write a table entry */
static int bcm5600_table_write_entry(struct nm_16esw_data *d)
{
   struct bcm5600_table *table;
   m_uint32_t addr,index,*entry;
   int i;

   addr  = d->dw[1] & 0xFFFF0000;
   index = d->dw[1] & 0x0000FFFF;

   if (!(table = bcm5600_table_find(d,addr)))
      return(-1);

   if (!(entry = bcm5600_table_get_entry(d,table,index)))
      return(-1);

   for(i=0;i<table->nr_words;i++)
      entry[i] = d->dw[i+2];

#if DEBUG_MEM
   {
      char buffer[512],*ptr = buffer;

      for(i=0;i<table->nr_words;i++)
         ptr += sprintf(ptr,"data[%d]=0x%8.8x ",i,entry[i]);

      BCM_LOG(d,"WRITE_MEM: addr=0x%8.8x (table %s) %s\n",
              d->dw[1],table->name,buffer);
   }
#endif
   return(0);
}

/* Dump a table (for debugging) */
__unused static int bcm5600_table_dump(struct nm_16esw_data *d,m_uint32_t addr)
{
   struct bcm5600_table *table;
   m_uint32_t *entry;
   int i,j;

   if (!(table = bcm5600_table_find(d,addr)))
      return(-1);

   printf("%s: dumping table \"%s\":\n",d->name,table->name);

   for(i=table->min_index;i<=table->max_index;i++) {
      if (!(entry = bcm5600_table_get_entry(d,table,i)))
         break;

      printf("  %4d:",i);

      for(j=0;j<table->nr_words;j++)
         printf("0x%8.8x ",entry[j]);

      printf("\n");
   }

   printf("\n");
   return(0);
}

/* Dump the VLAN table */
static int bcm5600_dump_vtable(struct nm_16esw_data *d)
{   
   struct bcm5600_table *table;
   struct bcm5600_port *port;
   m_uint32_t *entry,tbmp,ubmp;
   u_int vlan;
   int i,j;

   if (!(table = bcm5600_table_find(d,BCM5600_ADDR_VTABLE0)))
      return(-1);

   printf("%s: dumping VLAN table:\n",d->name);

   for(i=table->min_index;i<=table->max_index;i++) {
      if (!(entry = bcm5600_table_get_entry(d,table,i)))
         break;

      /* Extract the VLAN info */
      vlan = entry[0] & BCM5600_VTABLE_VLAN_TAG_MASK;

      if (vlan == VLAN_INVALID)
         continue;

      printf("  VLAN %4u: ",vlan);

      for(j=0;j<d->nr_port;j++) {
         tbmp = entry[1] & (1 << j);
         ubmp = entry[2] & (1 << j);

         if (tbmp || ubmp) {
            port = &d->ports[j];

            printf("%s (",port->name);

            if (tbmp) 
               printf("T%s",ubmp ? "/" : ") ");

            if (ubmp)
               printf("UT) ");
         }
      }

      printf("\n");
   }

   printf("\n");
   return(0);
}

/* Dump the "trunk" ports */
static int bcm5600_dump_trunks(struct nm_16esw_data *d)
{   
   struct bcm5600_table *table;
   struct bcm5600_port *port;
   m_uint32_t *entry;
   int i,j;

   if (!(table = bcm5600_table_find(d,BCM5600_ADDR_TBMAP0)))
      return(-1);

   printf("%s: trunk ports:\n",d->name);

   for(i=table->min_index;i<=table->max_index;i++) {
      if (!(entry = bcm5600_table_get_entry(d,table,i)))
         break;

      if (!entry[0])
         continue;

      printf("  Trunk %d: ",i);

      for(j=0;j<d->nr_port;j++) {
         if (entry[0] & (1 << j)) {
            port = &d->ports[j];
            printf("%s ",port->name);
         }
      }

      printf("\n");
   }

   printf("\n");
   return(0);
}

/* Dump the physical port info */
static int bcm5600_dump_ports(struct nm_16esw_data *d)
{   
   struct bcm5600_table *table;
   struct bcm5600_port *port;
   m_uint32_t *entry;
   u_int vlan,tgid;
   int i;

   if (!(table = bcm5600_table_find(d,BCM5600_ADDR_PTABLE0)))
      return(-1);

   printf("%s: physical ports:\n",d->name);

   for(i=0;i<d->nr_port;i++) {
      if (!(entry = bcm5600_table_get_entry(d,table,i)))
         break;

      port = &d->ports[i];
      vlan = entry[0] & BCM5600_PTABLE_VLAN_TAG_MASK;
      
      printf("  %-10s: VLAN %u",port->name,vlan);

      if (entry[0] & BCM5600_PTABLE_TRUNK_FLAG) {
         tgid = entry[0] & BCM5600_PTABLE_TGID_MASK;
         tgid >>= BCM5600_PTABLE_TGID_SHIFT;

         printf(", Trunk Group %u ",tgid);
      }

      printf("\n");
   }

   printf("\n");
   return(0);
}

/* Dump the physical port bitmaps */
static int bcm5600_dump_port_bitmaps(struct nm_16esw_data *d)
{   
   struct bcm5600_table *table;
   struct bcm5600_port *port;
   m_uint32_t *entry,tbmp,ubmp;
   int i,j;

   if (!(table = bcm5600_table_find(d,BCM5600_ADDR_PTABLE0)))
      return(-1);

   printf("%s: dumping bitmaps of the port table:\n",d->name);

   for(i=0;i<d->nr_port;i++) {
      if (!(entry = bcm5600_table_get_entry(d,table,i)))
         break;

      port = &d->ports[i];

      printf("  %-10s: ",port->name);

      for(j=0;j<d->nr_port;j++) {
         tbmp = entry[1] & (1 << j);
         ubmp = entry[2] & (1 << j);

         if (tbmp || ubmp) {
            printf("%s (",d->ports[j].name);

            if (tbmp) 
               printf("T%s",ubmp ? "/" : ") ");

            if (ubmp)
               printf("UT) ");
         }
      }

      printf("\n");
   }

   printf("\n");
   return(0);
}

/* Dump main tables */
static void bcm5600_dump_main_tables(struct nm_16esw_data *d)
{
   bcm5600_dump_ports(d);
   bcm5600_dump_port_bitmaps(d);
   bcm5600_dump_vtable(d);
   bcm5600_dump_trunks(d);
}

/* Find a free ARL entry */
static int bcm5600_find_free_arl_entry(struct nm_16esw_data *d)
{   
   struct bcm5600_table *table = d->t_arl;

   if (d->arl_cnt[0] == table->max_index)
      return(-1);

   return(d->arl_cnt[0] - 1);
}

/* ARL Lookup. TODO: this must be optimized in the future. */
static inline int bcm5600_gen_arl_lookup(struct nm_16esw_data *d,
                                         struct bcm5600_table *table,
                                         u_int index_start,u_int index_end,
                                         n_eth_addr_t *mac_addr,
                                         u_int vlan)
{
   m_uint32_t *entry,tmp[2],mask;
   int i;

   tmp[0]  = mac_addr->eth_addr_byte[2] << 24;
   tmp[0] |= mac_addr->eth_addr_byte[3] << 16;
   tmp[0] |= mac_addr->eth_addr_byte[4] << 8;
   tmp[0] |= mac_addr->eth_addr_byte[5];

   tmp[1] = (mac_addr->eth_addr_byte[0] << 8) | mac_addr->eth_addr_byte[1];
   tmp[1] |= vlan << BCM5600_ARL_VLAN_TAG_SHIFT;

   mask = BCM5600_ARL_VLAN_TAG_MASK | BCM5600_ARL_MAC_MSB_MASK;

   for(i=index_start;i<index_end;i++) {
      entry = bcm5600_table_get_entry(d,table,i);

      if ((entry[0] == tmp[0]) && ((entry[1] & mask) == tmp[1]))
         return(i);
   }

   return(-1);
}

/* ARL Lookup */
static inline int bcm5600_arl_lookup(struct nm_16esw_data *d,
                                     n_eth_addr_t *mac_addr,
                                     u_int vlan)
{
   struct bcm5600_table *table = d->t_arl;
   return(bcm5600_gen_arl_lookup(d,table,1,d->arl_cnt[0]-1,mac_addr,vlan));
}

/* MARL Lookup */
static inline int bcm5600_marl_lookup(struct nm_16esw_data *d,
                                      n_eth_addr_t *mac_addr,
                                      u_int vlan)
{
   struct bcm5600_table *table = d->t_marl;
   return(bcm5600_gen_arl_lookup(d,table,table->min_index,table->max_index+1,
                                 mac_addr,vlan));
}

/* Invalidate an ARL entry */
static void bcm5600_invalidate_arl_entry(m_uint32_t *entry)
{
   entry[0] = entry[1] = entry[2] = 0;
}

/* Insert an entry into the ARL table */
static int bcm5600_insert_arl_entry(struct nm_16esw_data *d)
{   
   struct bcm5600_table *table = d->t_arl;
   m_uint32_t *entry,mask;
   int i,index;

   mask = BCM5600_ARL_VLAN_TAG_MASK | BCM5600_ARL_MAC_MSB_MASK;

   for(i=0;i<d->arl_cnt[0]-1;i++) {
      entry = bcm5600_table_get_entry(d,table,i);

      /* If entry already exists, just modify it */
      if ((entry[0] == d->dw[1]) && ((entry[1] & mask) == (d->dw[2] & mask))) {
         entry[0] = d->dw[1];
         entry[1] = d->dw[2];
         entry[2] = d->dw[3];
         d->dw[1] = i;
         return(0);
      }
   }

   index = d->arl_cnt[0] - 1;

   entry = bcm5600_table_get_entry(d,table,index);
   entry[0] = d->dw[1];
   entry[1] = d->dw[2];
   entry[2] = d->dw[3];
   d->dw[1] = index;
   
   d->arl_cnt[0]++;
   return(0);
}

/* Delete an entry from the ARL table */
static int bcm5600_delete_arl_entry(struct nm_16esw_data *d)
{  
   struct bcm5600_table *table;
   m_uint32_t *entry,*last_entry,mac_msb;
   u_int cvlan,vlan;
   int i;

   if (!(table = bcm5600_table_find(d,BCM5600_ADDR_ARL0)))
      return(-1);

   vlan = d->dw[2] & BCM5600_ARL_VLAN_TAG_MASK;
   vlan >>= BCM5600_ARL_VLAN_TAG_SHIFT;

   mac_msb = d->dw[2] & BCM5600_ARL_MAC_MSB_MASK;

   for(i=table->min_index;i<=table->max_index;i++) {
      entry = bcm5600_table_get_entry(d,table,i);

      /* compare VLANs and MAC addresses */
      cvlan = (entry[1] & BCM5600_ARL_VLAN_TAG_MASK);
      cvlan >>= BCM5600_ARL_VLAN_TAG_SHIFT;

      if ((cvlan == vlan) && (entry[0] == d->dw[1]) &&
          ((entry[1] & BCM5600_ARL_MAC_MSB_MASK) == mac_msb))
      {            
         d->dw[1] = i;

         last_entry = bcm5600_table_get_entry(d,d->t_arl,d->arl_cnt[0]-2);
            
         entry[0] = last_entry[0];
         entry[1] = last_entry[1];
         entry[2] = last_entry[2];

         d->arl_cnt[0]--;
         return(i);
      }
   }

   return(0);
}

/* Reset the ARL tables */
static int bcm5600_reset_arl(struct nm_16esw_data *d)
{
   struct bcm5600_table *table;
   m_uint32_t *entry;
   int i;
   
   if (!(table = bcm5600_table_find(d,BCM5600_ADDR_ARL0)))
      return(-1);

   for(i=table->min_index;i<=table->max_index;i++) {
      entry = bcm5600_table_get_entry(d,table,i);
      bcm5600_invalidate_arl_entry(entry);
   }

   return(0);
}

/* MAC Address Ager */
static int bcm5600_arl_ager(struct nm_16esw_data *d)
{
   m_uint32_t *entry,*last_entry;
   int i;

   BCM_LOCK(d);

   for(i=1;i<d->arl_cnt[0]-1;i++) {
      entry = bcm5600_table_get_entry(d,d->t_arl,i);
      assert(entry);

      if (entry[2] & BCM5600_ARL_ST_FLAG)
         continue;

      /* The entry has expired, purge it */
      if (!(entry[2] & BCM5600_ARL_HIT_FLAG)) {
         last_entry = bcm5600_table_get_entry(d,d->t_arl,d->arl_cnt[0]-2);
        
         entry[0] = last_entry[0];
         entry[1] = last_entry[1];
         entry[2] = last_entry[2];

         d->arl_cnt[0]--;
         i--;
      } else {
         entry[2] &= ~BCM5600_ARL_HIT_FLAG;
      }
   }

   BCM_UNLOCK(d);
   return(TRUE);
}

/* Get the VTABLE entry matching the specified VLAN */
static m_uint32_t *bcm5600_vtable_get_entry_by_vlan(struct nm_16esw_data *d,
                                                    u_int vlan)
{
   struct bcm5600_table *table = d->t_vtable;
   m_uint32_t *entry;
   int i;

   for(i=table->min_index;i<=table->max_index;i++) {
      if (!(entry = bcm5600_table_get_entry(d,table,i)))
         break;

      if ((entry[0] & BCM5600_VTABLE_VLAN_TAG_MASK) == vlan)
         return entry;
   }

   return NULL;
}

/* Read memory command */
static void bcm5600_handle_read_mem_cmd(struct nm_16esw_data *d)
{
   int i;

   if (bcm5600_table_read_entry(d) != 0) {
      for(i=1;i<BCM5600_DW_MAX;i++)
         d->dw[i] = 0;
   }

   d->dw[0] = BCM5600_OP_READ_MEM_ACK << BCM5600_CMD_OP_SHIFT;
}

/* Write memory command */
static void bcm5600_handle_write_mem_cmd(struct nm_16esw_data *d)
{
   bcm5600_table_write_entry(d);
   d->dw[0] = BCM5600_OP_WRITE_MEM_ACK << BCM5600_CMD_OP_SHIFT;
}

/* Handle a "general" command */
static void bcm5600_handle_gen_cmd(struct nm_16esw_data *d)
{
   m_uint32_t op;
   __maybe_unused m_uint32_t src,dst,len;

   /* Extract the opcode */
   op  = (d->dw[0] & BCM5600_CMD_OP_MASK) >> BCM5600_CMD_OP_SHIFT;
   src = (d->dw[0] & BCM5600_CMD_SRC_MASK) >> BCM5600_CMD_SRC_SHIFT;
   dst = (d->dw[0] & BCM5600_CMD_DST_MASK) >> BCM5600_CMD_DST_SHIFT;
   len = (d->dw[0] & BCM5600_CMD_LEN_MASK) >> BCM5600_CMD_LEN_SHIFT;

#if DEBUG_ACCESS
   BCM_LOG(d,"gen_cmd: opcode 0x%2.2x [src=0x%2.2x,dst=0x%2.2x,len=0x%2.2x] "
           "(dw[0]=0x%8.8x, dw[1]=0x%8.8x, dw[2]=0x%8.8x, dw[3]=0x%8.8x)\n",
           op,src,dst,len,d->dw[0],d->dw[1],d->dw[2],d->dw[3]);
#endif

   switch(op) {
      case BCM5600_OP_READ_MEM_CMD:
         bcm5600_handle_read_mem_cmd(d);
         break;

      case BCM5600_OP_WRITE_MEM_CMD:
         bcm5600_handle_write_mem_cmd(d);
         break;

      case BCM5600_OP_READ_REG_CMD:
         d->dw[0] = BCM5600_OP_READ_REG_ACK << BCM5600_CMD_OP_SHIFT;
#if DEBUG_REG
         BCM_LOG(d,"READ_REG: reg_addr=0x%8.8x\n",d->dw[1]);
#endif
         d->dw[1] = bcm5600_reg_read(d,d->dw[1]);
         break;

      case BCM5600_OP_WRITE_REG_CMD:
         d->dw[0] = BCM5600_OP_WRITE_REG_ACK << BCM5600_CMD_OP_SHIFT;
#if DEBUG_REG
         BCM_LOG(d,"WRITE_REG: reg_addr=0x%8.8x val=0x%8.8x\n",
                 d->dw[1],d->dw[2]);
#endif
         bcm5600_reg_write(d,d->dw[1],d->dw[2]);
         bcm5600_reg_write_special(d,d->dw[1],d->dw[2]);
         break;

      case BCM5600_OP_ARL_INSERT_CMD:
         d->dw[0] = BCM5600_OP_ARL_INSERT_DONE << BCM5600_CMD_OP_SHIFT;

#if DEBUG_ARL
         BCM_LOG(d,"ARL_INSERT_CMD "
                 "(dw[1]=0x%8.8x,dw[2]=0x%8.8x,dw[3]=0x%8.8x)\n",
                 d->dw[1],d->dw[2],d->dw[3]);
#endif
         bcm5600_insert_arl_entry(d);
         break;

      case BCM5600_OP_ARL_DELETE_CMD:
         d->dw[0] = BCM5600_OP_ARL_DELETE_DONE << BCM5600_CMD_OP_SHIFT;

#if DEBUG_ARL
         BCM_LOG(d,"ARL_DELETE_CMD (dw[1]=0x%8.8x,dw[2]=0x%8.8x)\n",
                 d->dw[1],d->dw[2]);
#endif
         bcm5600_delete_arl_entry(d);
         break;

      case BCM5600_OP_ARL_LOOKUP_CMD:
         d->dw[0] = BCM5600_OP_READ_MEM_ACK << BCM5600_CMD_OP_SHIFT;
         break;

      default:
         BCM_LOG(d,"unknown opcode 0x%8.8x (cmd=0x%8.8x)\n",op,d->dw[0]);
   }
}

/* Handle a s-channel command */
static void bcm5600_handle_schan_cmd(struct nm_16esw_data *d,m_uint32_t cmd)
{
   d->schan_cmd = cmd;

#if DEBUG_ACCESS
   BCM_LOG(d,"s-chan command 0x%8.8x\n",cmd);
#endif

   switch(cmd) {
      case BCM5600_SCHAN_CMD_EXEC:
         bcm5600_handle_gen_cmd(d);
         d->schan_cmd_res = 0x00008002;
         break;

      case BCM5600_SCHAN_CMD_READ_MII:
         bcm5600_mii_read(d);
         d->schan_cmd_res = 0x00048000;
         break;

      case BCM5600_SCHAN_CMD_WRITE_MII:
         bcm5600_mii_write(d);
         d->schan_cmd_res = 0x00048000;
         break;

      case BCM5600_SCHAN_CMD_LINKSCAN:
         d->schan_cmd_res = 0x0;
         break;

      default:
#if DEBUG_UNKNOWN
         BCM_LOG(d,"unknown s-chan command 0x%8.8x\n",cmd);
#endif
         d->schan_cmd_res = 0xFFFFFFFF;
   }
}

/*
 * dev_bcm5605_access()
 */
void *dev_bcm5605_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct nm_16esw_data *d = dev->priv_data;
   u_int reg;

   if (op_type == MTS_READ)
      *data = 0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      BCM_LOG(d,"read  access to offset=0x%x, pc=0x%llx\n",
              offset,cpu_get_pc(cpu));
   } else {
      BCM_LOG(d,"write access to offset=0x%x, pc=0x%llx, val=0x%llx\n",
              offset,cpu_get_pc(cpu),*data);
   }
#endif

   BCM_LOCK(d);

   switch(offset) {
      case 0x50:
         if (op_type == MTS_WRITE) {
            bcm5600_handle_schan_cmd(d,*data);
         } else {
            *data = d->schan_cmd_res;
         }
         break;

      case 0x140:
         if (op_type == MTS_READ)
            *data = bcm5600_mii_port_status_bmp(d);
         break;

      /* MII input register */
      case 0x158:
         if (op_type == MTS_WRITE)
            d->mii_input = *data;
         break;

      /* MII output register */
      case 0x15c:
         if (op_type == MTS_READ)
            *data = d->mii_output;
         break;         

      /* Unknown (related to RX/TX rings ?) */
      case 0x104:
         break;

      /* TX ring address */
      case 0x110:
         if (op_type == MTS_READ)
            *data = d->tx_ring_addr;
         else {
            d->tx_ring_addr = d->tx_current = *data;   
            d->tx_end_scan = 0;
#if DEBUG_TRANSMIT
            BCM_LOG(d,"tx_ring_addr = 0x%8.8x\n",d->tx_ring_addr);
#endif
         }
         break;

      /* RX ring address */
      case 0x114:
         if (op_type == MTS_READ)
            *data = d->rx_ring_addr;
         else {
            d->rx_ring_addr = d->rx_current = *data;
            d->rx_end_scan = 0;
#if DEBUG_RECEIVE
            BCM_LOG(d,"rx_ring_addr = 0x%8.8x\n",d->rx_ring_addr);
#endif
         }
         break;

      /* Interrupt status */
      case 0x144:
         if (op_type == MTS_READ) {
            *data = 0;

            /* RX/TX underrun (end of rings reached) */
            if (d->tx_end_scan)
               *data |= BCM5600_INTR_TX_UNDERRUN;

            if (d->rx_end_scan)
               *data |= BCM5600_INTR_RX_UNDERRUN;

            /* RX packet available */
            *data |= BCM5600_INTR_RX_AVAIL;

            /* Link status changed */
            if (d->mii_intr) {
               *data |= BCM5600_INTR_LINKSTAT_MOD;
               d->mii_intr = FALSE;
            }

            pci_dev_clear_irq(d->vm,d->pci_dev);
         }
         break;

      /* Interrupt mask */
      case 0x148:
         if (op_type == MTS_READ)
            *data = d->intr_mask;
         else
            d->intr_mask = *data;
         break;

      /* Data Words */
      case 0x800 ... 0x850:
         reg = (offset - 0x800) >> 2;

         if (op_type == MTS_READ)
            *data = d->dw[reg];
         else
            d->dw[reg] = *data;
         break;

#if DEBUG_UNKNOWN
      /* Unknown offset */
      default:
         if (op_type == MTS_READ) {
            BCM_LOG(d,"read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            BCM_LOG(d,"write to unknown addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   BCM_UNLOCK(d);
   return NULL;
}

/* Show mirroring status */
static int bcm5600_mirror_show_status(struct nm_16esw_data *d)
{
   m_uint32_t *port,dst_port;
   int i;

   printf("Mirroring status: "); 

   if (!(d->mirror_dst_port & BCM5600_MIRROR_ENABLE)) {
      printf("disabled.\n\n");
      return(FALSE);
   }

   printf("enabled. Dest port: ");

   dst_port = d->mirror_dst_port & BCM5600_MIRROR_PORT_MASK;

   if (dst_port < 32)
      printf("%s\n",d->ports[dst_port].name);
   else
      printf("none set.\n");

   /* Ingress info */
   printf("  Ingress Ports: ");

   for(i=0;i<d->nr_port;i++) {
      port = bcm5600_table_get_entry(d,d->t_ptable,i);
      if (port[1] & BCM5600_PTABLE_MI_FLAG)
         printf("%s ",d->ports[i].name);
   }

   printf("\n");

   /* Egress info */
   printf("  Egress Ports: ");
  
   for(i=0;i<d->nr_port;i++)
      if (d->mirror_egress_ports & (1 << i))
         printf("%s ",d->ports[i].name);

   printf("\n\n");
   return(TRUE);
}

/* Mirror a packet */
static int bcm5600_mirror_pkt(struct nm_16esw_data *d,struct bcm5600_pkt *p,
                              int reason)
{
   u_int mport;

   if (!(d->mirror_dst_port & BCM5600_MIRROR_ENABLE))
      return(FALSE);

#if DEBUG_MIRROR
   if (reason == 0) {
      BCM_LOG(d,"mirroring packet on ingress port %s\n",
              d->ports[p->ingress_port]);
   } else {
      BCM_LOG(d,"mirroring packet on egress port (input port %s)\n",
              d->ports[p->ingress_port]);
   }
   mem_dump(d->vm->log_fd,pkt,pkt_len);
#endif

   mport = d->mirror_dst_port & BCM5600_MIRROR_PORT_MASK;
   if (mport < 32)
      netio_send(d->ports[mport].nio,p->pkt,p->pkt_len);
   return(TRUE);
}

/* Put a packet into the RX ring (tag it if necessary) */
static int bcm5600_send_pkt_to_cpu(struct nm_16esw_data *d,
                                   struct bcm5600_pkt *p)
{
   m_uint32_t pkt_addr,pkt_len,dot1q_data;

   /* If the packet was already sent to CPU, don't send it again */
   if (p->sent_to_cpu)
      return(FALSE);

   pkt_addr = p->rdes[0];
   pkt_len  = p->pkt_len;

   if (p->orig_vlan != -1) {
      /* 802.1Q packet: copy it directly */
      physmem_copy_to_vm(d->vm,p->pkt,pkt_addr,pkt_len);
   } else {
      /* untagged packet: copy the dst and src addresses first */
      physmem_copy_to_vm(d->vm,p->pkt,pkt_addr,N_ETH_HLEN - 2);

      /* add the 802.1Q protocol field (0x8100) + VLAN info */
      dot1q_data = (N_ETH_PROTO_DOT1Q << 16) | p->real_vlan;
      physmem_copy_u32_to_vm(d->vm,pkt_addr+N_ETH_HLEN-2,dot1q_data);

      /* copy the payload */
      physmem_copy_to_vm(d->vm,p->pkt+N_ETH_HLEN-2,
                         pkt_addr+sizeof(n_eth_dot1q_hdr_t),
                         pkt_len - (N_ETH_HLEN - 2));
      pkt_len += 4;
   }

   physmem_copy_u32_to_vm(d->vm,d->rx_current+0x14,0x40000000 + (pkt_len+4));
   physmem_copy_u32_to_vm(d->vm,d->rx_current+0x18,0x100 + p->ingress_port);
   p->sent_to_cpu = TRUE;

#if DEBUG_RECEIVE
   BCM_LOG(d,"sending packet to CPU (orig_vlan=%d).\n",p->orig_vlan);
#endif
   return(TRUE);
}

/* Source MAC address learning */
static int bcm5600_src_mac_learning(struct nm_16esw_data *d,
                                    struct bcm5600_pkt *p)
{  
   n_eth_hdr_t *eth_hdr = (n_eth_hdr_t *)p->pkt;
   n_eth_addr_t *src_mac = &eth_hdr->saddr;
   m_uint32_t *arl_entry,*src_port,*trunk;
   u_int trunk_id,old_ingress_port;
   int src_mac_index;

   trunk = NULL;
   trunk_id = 0;

   /* Skip multicast sources */
   if (eth_addr_is_mcast(src_mac))
      return(FALSE);

   src_port = bcm5600_table_get_entry(d,d->t_ptable,p->ingress_port);
   assert(src_port != NULL);

   /* 
    * The packet comes from a trunk port. Prevent sending the packet
    * to the other ports of the trunk.
    */
   if (src_port[0] & BCM5600_PTABLE_TRUNK_FLAG) {
      trunk_id = src_port[0] & BCM5600_PTABLE_TGID_MASK;
      trunk_id >>= BCM5600_PTABLE_TGID_SHIFT;

      trunk = bcm5600_table_get_entry(d,d->t_tbmap,trunk_id);
      assert(trunk != NULL);

      p->egress_filter_bitmap |= trunk[0] & BCM5600_TBMAP_MASK;
   }

   /* Source MAC address learning */
   src_mac_index = bcm5600_arl_lookup(d,src_mac,p->real_vlan);

   if (src_mac_index != -1) {
      arl_entry = bcm5600_table_get_entry(d,d->t_arl,src_mac_index);
      assert(arl_entry != NULL);

      old_ingress_port = arl_entry[2] & BCM5600_ARL_PORT_MASK;
      old_ingress_port >>= BCM5600_ARL_PORT_SHIFT;

      if (old_ingress_port != p->ingress_port) 
      {
         /*
          * Determine if we have a station movement.
          * If we have a trunk, check if the old ingress port is member
          * of this trunk, in this case this is not a movement.
          */
         if (trunk != NULL) {
            if (trunk[0] & (1 << old_ingress_port))
               arl_entry[2] |= BCM5600_ARL_HIT_FLAG;
            else 
               arl_entry[2] &= ~BCM5600_ARL_HIT_FLAG;
         } else {
            arl_entry[2] &= ~(BCM5600_ARL_TRUNK_FLAG|BCM5600_ARL_HIT_FLAG);
            arl_entry[2] &= ~BCM5600_ARL_TGID_MASK;
         }

         /* Change the ingress port */
         arl_entry[2] &= ~BCM5600_ARL_PORT_MASK;
         arl_entry[2] |= p->ingress_port << BCM5600_ARL_PORT_SHIFT;
         return(TRUE);
      }

      arl_entry[2] |= BCM5600_ARL_HIT_FLAG;
      return(TRUE);
   }

#if DEBUG_FORWARD
      BCM_LOG(d,"source MAC address unknown, learning it.\n");
#endif

   /* Add the new learned MAC address */
   src_mac_index = bcm5600_find_free_arl_entry(d);

   if (src_mac_index == -1) {
      BCM_LOG(d,"no free entries in ARL table!\n");
      return(FALSE);
   }

   arl_entry = bcm5600_table_get_entry(d,d->t_arl,src_mac_index);
   assert(arl_entry != NULL);
    
   /* Fill the new ARL entry */
   arl_entry[0]  = src_mac->eth_addr_byte[2] << 24;
   arl_entry[0] |= src_mac->eth_addr_byte[3] << 16;
   arl_entry[0] |= src_mac->eth_addr_byte[4] << 8;
   arl_entry[0] |= src_mac->eth_addr_byte[5];

   arl_entry[1]  = src_mac->eth_addr_byte[0] << 8;
   arl_entry[1] |= src_mac->eth_addr_byte[1];
   arl_entry[1] |= p->real_vlan << BCM5600_ARL_VLAN_TAG_SHIFT;

   arl_entry[2]  = BCM5600_ARL_HIT_FLAG;
   arl_entry[2] |= p->ingress_port << BCM5600_ARL_PORT_SHIFT;

   if (trunk != NULL) {
      arl_entry[2] |= BCM5600_ARL_TRUNK_FLAG;
      arl_entry[2] |= (trunk_id << BCM5600_ARL_TGID_SHIFT);
   }

   d->arl_cnt[0]++;
   return(TRUE);
}

/* Select an egress port the specified trunk */
static int bcm5600_trunk_egress_port(struct nm_16esw_data *d,
                                     struct bcm5600_pkt *p,
                                     u_int trunk_id)
{   
   __maybe_unused n_eth_hdr_t *eth_hdr = (n_eth_hdr_t *)p->pkt;
   __maybe_unused u_int i, hash;
   struct bcm5600_tg_info *tgi;
   m_uint32_t *ttr_entry;
   u_int nr_links;
   u_int port_id;

   ttr_entry = bcm5600_table_get_entry(d,d->t_ttr,trunk_id);
   assert(ttr_entry != NULL);

   nr_links = ttr_entry[1] & BCM5600_TTR_TG_SIZE_MASK;
   nr_links >>= BCM5600_TTR_TG_SIZE_SHIFT;

#if 0
   /* Hash on source and destination MAC addresses */
   for(i=0,hash=0;i<N_ETH_ALEN;i++) {
      hash ^= eth_hdr->saddr.eth_addr_byte[i];
      hash ^= eth_hdr->daddr.eth_addr_byte[i];
   }

   hash ^= (hash >> 4);
   port_id = hash % nr_links;

   /* Maximum of 8 ports per trunk */
   assert(hash < BCM5600_MAX_PORTS_PER_TRUNK);
#else
   port_id = d->trunk_last_egress_port[trunk_id] + 1;
   port_id %= nr_links;   
#endif

   /* Save the latest port used for this trunk */
   d->trunk_last_egress_port[trunk_id] = port_id;

   /* Select the egress port */
   tgi = &tg_info[port_id];
   return((ttr_entry[tgi->index] & tgi->mask) >> tgi->shift);
}

/* Destination address lookup (take the forwarding decision) */
static int bcm5600_dst_mac_lookup(struct nm_16esw_data *d,
                                  struct bcm5600_pkt *p)
{
   n_eth_hdr_t *eth_hdr = (n_eth_hdr_t *)p->pkt;
   n_eth_addr_t *dst_mac = &eth_hdr->daddr;
   struct bcm5600_table *arl_table;
   m_uint32_t *arl_entry;
   u_int egress_port;
   u_int trunk_id;
   int dst_mac_index;
   int is_mcast;

   /* Select the appropriate ARL table and do the lookup on dst MAC + VLAN */
   if (eth_addr_is_mcast(dst_mac)) {
      is_mcast = TRUE;
      arl_table = d->t_marl;
      dst_mac_index = bcm5600_marl_lookup(d,dst_mac,p->real_vlan);
   } else {
      is_mcast = FALSE;
      arl_table = d->t_arl;
      dst_mac_index = bcm5600_arl_lookup(d,dst_mac,p->real_vlan);
   }

   /*
    * Destination Lookup Failure (DLF).
    * 
    * Use the VLAN bitmap to compute the Egress port bitmap.
    * Remove the ingress port from it.
    */
   if (dst_mac_index == -1) {
#if DEBUG_FORWARD
      BCM_LOG(d,"Destination MAC address "
              "%2.2x%2.2x.%2.2x%2.2x.%2.2x%2.2x unknown, flooding.\n",
              dst_mac->eth_addr_byte[0],dst_mac->eth_addr_byte[1],
              dst_mac->eth_addr_byte[2],dst_mac->eth_addr_byte[3],  
              dst_mac->eth_addr_byte[4],dst_mac->eth_addr_byte[5]);
#endif
      p->egress_bitmap = p->vlan_entry[1] & BCM5600_VTABLE_PORT_BMAP_MASK;

      /* Add the CPU to the egress ports */
      p->egress_bitmap |= 1 << d->cpu_port;

      p->egress_ut_bitmap = p->vlan_entry[2];
      p->egress_ut_bitmap &= BCM5600_VTABLE_UT_PORT_BMAP_MASK;
      return(TRUE);
   }

   /* The MAC address was found in the ARL/MARL table */
   arl_entry = bcm5600_table_get_entry(d,arl_table,dst_mac_index);
   assert(arl_entry != NULL);

   /* If the CPU bit is set, send a copy of the packet to the CPU */
   if (arl_entry[1] & BCM5600_ARL_CPU_FLAG)
      bcm5600_send_pkt_to_cpu(d,p);

   if (!is_mcast) {
      /* Unicast: send the packet to the port or trunk found in ARL table */
      if (arl_entry[2] & BCM5600_ARL_TRUNK_FLAG) {
         trunk_id = arl_entry[2] & BCM5600_ARL_TGID_MASK;
         trunk_id >>= BCM5600_ARL_TGID_SHIFT;

         /* Select an output port for this trunk */
         egress_port = bcm5600_trunk_egress_port(d,p,trunk_id);

#if DEBUG_FORWARD
         BCM_LOG(d,"Sending packet to trunk port %u, egress port %u\n",
                 trunk_id,egress_port);
#endif
      } else {
         egress_port = arl_entry[2] & BCM5600_ARL_PORT_MASK;
         egress_port >>= BCM5600_ARL_PORT_SHIFT;
      }

      p->egress_bitmap = 1 << egress_port;
      p->egress_ut_bitmap = p->vlan_entry[2] & 
         BCM5600_VTABLE_UT_PORT_BMAP_MASK;
   } else {
      /* Multicast: send the packet to the egress ports found in MARL table */
      p->egress_bitmap = arl_entry[2] & BCM5600_MARL_PORT_BMAP_MASK;
      p->egress_ut_bitmap = arl_entry[3] & BCM5600_MARL_UT_PORT_BMAP_MASK;
   }

#if DEBUG_FORWARD
   {
      char buffer[1024];

      BCM_LOG(d,"bitmap: 0x%8.8x, filter: 0x%8.8x\n",
              p->egress_bitmap,p->egress_filter_bitmap);

      bcm5600_port_bitmap_str(d,buffer,p->egress_bitmap);

      /* without egress port filtering */
      if (*buffer)
         BCM_LOG(d,"forwarding to egress port list w/o filter: %s\n",buffer);
      else
         BCM_LOG(d,"w/o filter: empty egress port list.\n");

      /* with egress port filtering */
      bcm5600_port_bitmap_str(d,buffer,
                              p->egress_bitmap & ~p->egress_filter_bitmap);

      if (*buffer)
         BCM_LOG(d,"forwarding to egress port list w/ filter: %s\n",buffer);
   }
#endif

   return(p->egress_bitmap != 0);
}

/* Prototype for a packet sending function */
typedef void (*bcm5600_send_pkt_t)(struct nm_16esw_data *d,
                                   struct bcm5600_pkt *p,
                                   netio_desc_t *nio);

/* Directly forward a packet (not rewritten) */
static void bcm5600_send_pkt_direct(struct nm_16esw_data *d,
                                    struct bcm5600_pkt *p,
                                    netio_desc_t *nio)
{
   netio_send(nio,p->pkt,p->pkt_len);
}

/* Send a packet with a 802.1Q tag */
static void bcm5600_send_pkt_push_dot1q(struct nm_16esw_data *d,
                                        struct bcm5600_pkt *p,
                                        netio_desc_t *nio)
{
   n_eth_dot1q_hdr_t *hdr;

   if (!p->rewrite_done) {
      memcpy(p->rewr_pkt,p->pkt,(N_ETH_HLEN - 2));

      hdr = (n_eth_dot1q_hdr_t *)p->rewr_pkt;
      hdr->type    = htons(N_ETH_PROTO_DOT1Q);
      hdr->vlan_id = htons(p->real_vlan);

      memcpy(p->rewr_pkt + sizeof(n_eth_dot1q_hdr_t),
             p->pkt + (N_ETH_HLEN - 2),
             p->pkt_len - (N_ETH_HLEN - 2));

      p->rewrite_done = TRUE;
   }
   
   netio_send(nio,p->rewr_pkt,p->pkt_len+4);
}

/* Send a packet deleting its 802.1Q tag */
static void bcm5600_send_pkt_pop_dot1q(struct nm_16esw_data *d,
                                       struct bcm5600_pkt *p,
                                       netio_desc_t *nio)
{
   if (!p->rewrite_done) {
      memcpy(p->rewr_pkt,p->pkt,(N_ETH_HLEN - 2));

      memcpy(p->rewr_pkt + (N_ETH_HLEN - 2),
             p->pkt + sizeof(n_eth_dot1q_hdr_t),
             p->pkt_len - sizeof(n_eth_dot1q_hdr_t));

      p->rewrite_done = TRUE;
   }
   
   netio_send(nio,p->rewr_pkt,p->pkt_len-4);
}

/* Forward a packet on physical ports (egress bitmap must be defined) */
static int bcm5600_forward_pkt(struct nm_16esw_data *d,struct bcm5600_pkt *p)
{
   u_char rewr_pkt[BCM5600_MAX_PKT_SIZE];
   bcm5600_send_pkt_t send_pkt;
   u_int egress_untagged,trunk_id;
   m_uint32_t *dst_port,*trunk;
   int i;

   p->egress_bitmap &= ~p->egress_filter_bitmap;
   
   if (!p->egress_bitmap)
      return(FALSE);

   /* Process egress mirroring (if enabled) */
   if (p->egress_bitmap & d->mirror_egress_ports)      
      bcm5600_mirror_pkt(d,p,1);

   /* No rewrite done at this time */
   p->rewr_pkt = rewr_pkt;
   p->rewrite_done = FALSE;

   /* Forward to CPU port ? */
   if (p->egress_bitmap & (1 << d->cpu_port))
      bcm5600_send_pkt_to_cpu(d,p);

   for(i=0;i<d->nr_port;i++) {
      if (!(p->egress_bitmap & (1 << i)))
         continue;

      /* 
       * If this port is a member of a trunk, remove all other ports to avoid
       * duplicate frames (typically, when a dest MAC address is unknown
       * or for a broadcast/multicast).
       */
      dst_port = bcm5600_table_get_entry(d,d->t_ptable,i);
      assert(dst_port != NULL);

      if (dst_port[0] & BCM5600_PTABLE_TRUNK_FLAG) {
         trunk_id = dst_port[0] & BCM5600_PTABLE_TGID_MASK;
         trunk_id >>= BCM5600_PTABLE_TGID_SHIFT;

         trunk = bcm5600_table_get_entry(d,d->t_tbmap,trunk_id);
         assert(trunk != NULL);
         
         p->egress_bitmap &= ~trunk[0];
      }

      /* select the appropriate output vector */
      if (p->orig_vlan == 0)
         send_pkt = bcm5600_send_pkt_direct;
      else {
         egress_untagged = p->egress_ut_bitmap & (1 << i);

         if (p->orig_vlan == -1) {
            /* Untagged packet */
            if (egress_untagged)
               send_pkt = bcm5600_send_pkt_direct;
            else
               send_pkt = bcm5600_send_pkt_push_dot1q;
         } else {
            /* Tagged packet */
            if (egress_untagged)
               send_pkt = bcm5600_send_pkt_pop_dot1q;
            else
               send_pkt = bcm5600_send_pkt_direct;
         }
      }

#if DEBUG_FORWARD > 1
      BCM_LOG(d,"forwarding on port %s (vector=%p)\n",
              d->ports[i].name,send_pkt);
#endif
      send_pkt(d,p,d->ports[i].nio);
   }

   return(TRUE);
}

/* Determine if the specified MAC address matches a BPDU */
static inline int bcm5600_is_bpdu(n_eth_addr_t *m)
{
   /* PVST+ */
   if (!memcmp(m,"\x01\x00\x0c\xcc\xcc\xcd",6))
      return(TRUE);

   /* Classical 802.1D */
   if (!memcmp(m,"\x01\x80\xc2\x00\x00",5) && !(m->eth_addr_byte[5] & 0xF0))
      return(TRUE);

   /* 
    * CDP: this is cleary a hack, but IOS seems to program this address
    * in BPDU registers.
    */
   if (!memcmp(m,"\x01\x00\x0c\xcc\xcc\xcc",6))
      return(TRUE);
   
   return(FALSE);
}

/* Handle a received packet */
static int bcm5600_handle_rx_pkt(struct nm_16esw_data *d,struct bcm5600_pkt *p)
{
   m_uint32_t *port_entry;
   n_eth_dot1q_hdr_t *eth_hdr;
   u_int discard;

   /* No egress port at this time */
   p->egress_bitmap = 0;

   /* Never send back frames to the source port */
   p->egress_filter_bitmap = 1 << p->ingress_port;

   if (!(port_entry = bcm5600_table_get_entry(d,d->t_ptable,p->ingress_port)))
      return(FALSE);

   /* Analyze the Ethernet header */
   eth_hdr = (n_eth_dot1q_hdr_t *)p->pkt;

   /* Determine VLAN */
   if (ntohs(eth_hdr->type) != N_ETH_PROTO_DOT1Q) {
      p->orig_vlan = -1;
      p->real_vlan = port_entry[0] & BCM5600_PTABLE_VLAN_TAG_MASK;

     /* TODO: 802.1p/CoS remarking */
     if (port_entry[4] & BCM5600_PTABLE_RPE_FLAG) {
     }
   } else {
      p->orig_vlan = p->real_vlan = ntohs(eth_hdr->vlan_id) & 0xFFF;
   }

   /* Check that this VLAN exists */
   if (!(p->vlan_entry = bcm5600_vtable_get_entry_by_vlan(d,p->real_vlan)))
      return(FALSE);

   /* Check for the reserved addresses (BPDU for spanning-tree) */
   if (bcm5600_is_bpdu(&eth_hdr->daddr)) {
#if DEBUG_RECEIVE
      BCM_LOG(d,"Received a BPDU packet:\n");
      mem_dump(d->vm->log_fd,p->pkt,p->pkt_len);
#endif
      p->egress_bitmap |= 1 << d->cpu_port;
      return(bcm5600_forward_pkt(d,p));
   }

   /* Check that this port is a member of this VLAN */
   if (!(p->vlan_entry[1] & (1 << p->ingress_port)))
      return(FALSE);

   /* Discard packet ? */
   discard = port_entry[0] & BCM5600_PTABLE_PRT_DIS_MASK;
   discard >>= BCM5600_PTABLE_PRT_DIS_SHIFT;

   if ((p->orig_vlan == -1) && discard) {
      if (discard != 0x20) {
         printf("\n\n\n"
                "-----------------------------------------------------------"
                "---------------------------------\n"
                "Unspported feature: please post your current configuration "
                "on http://www.ipflow.utc.fr/blog/\n"
                "-----------------------------------------------------------"
                "---------------------------------\n");
      }

      /* Drop the packet */
      return(FALSE);
   }

   /* Mirroring on Ingress ? */
   if (port_entry[1] & BCM5600_PTABLE_MI_FLAG)
      bcm5600_mirror_pkt(d,p,0);

#if DEBUG_RECEIVE
   BCM_LOG(d,"%s: received a packet on VLAN %u\n",
           d->ports[p->ingress_port].name,p->real_vlan);
#endif

   /* Source MAC address learning */
   if (!bcm5600_src_mac_learning(d,p))
      return(FALSE);

   /* Take forwarding decision based on destination MAC address */
   if (!bcm5600_dst_mac_lookup(d,p))
      return(FALSE);

   /* Send the packet to the egress ports */
   return(bcm5600_forward_pkt(d,p));
}

/* Handle a packet to transmit */
static int bcm5600_handle_tx_pkt(struct nm_16esw_data *d,
                                 struct bcm5600_pkt *p,
                                 u_int egress_bitmap)
{   
   n_eth_dot1q_hdr_t *eth_hdr;
   
   /* Never send back frames to the source port */
   p->egress_filter_bitmap = 1 << p->ingress_port;

   /* We take the complete forwarding decision if bit 23 is set */
   if (egress_bitmap & (1 << 23)) {
      /* No egress port at this time */
      p->egress_bitmap = 0;

      /* The packet must be tagged so that we can determine the VLAN */
      eth_hdr = (n_eth_dot1q_hdr_t *)p->pkt;

      if (ntohs(eth_hdr->type) != N_ETH_PROTO_DOT1Q) {
         BCM_LOG(d,"bcm5600_handle_tx_pkt: untagged packet ?\n");
         return(FALSE);
      }

      /* Find the appropriate, check it exists (just in case) */
      p->orig_vlan = p->real_vlan = ntohs(eth_hdr->vlan_id) & 0xFFF;

      if (!(p->vlan_entry = bcm5600_vtable_get_entry_by_vlan(d,p->real_vlan)))
        return(FALSE);

#if DEBUG_TRANSMIT
      BCM_LOG(d,"Transmitting a packet from TX ring to VLAN %u\n",
              p->real_vlan);
#endif

      /* Take forwarding decision based on destination MAC address */
      if (!bcm5600_dst_mac_lookup(d,p))
         return(FALSE);
   } else {
#if DEBUG_TRANSMIT
      BCM_LOG(d,"Transmitting natively a packet from TX ring.\n");
#endif
      /* The egress ports are specified, send the packet natively */
      p->orig_vlan = 0;
      p->egress_bitmap = egress_bitmap;
   }

   /* Send the packet to the egress ports */
   return(bcm5600_forward_pkt(d,p));
}

/* Handle the TX ring */
static int dev_bcm5600_handle_txring(struct nm_16esw_data *d)
{
   struct bcm5600_pkt pkt_data;
   m_uint32_t tdes[4],txd_len;

   BCM_LOCK(d);

   if (!d->tx_current || d->tx_end_scan) {
      BCM_UNLOCK(d);
      return(FALSE);
   }

   /* Read the current TX descriptor */
   physmem_copy_from_vm(d->vm,tdes,d->tx_current,4*sizeof(m_uint32_t));
   tdes[0] = vmtoh32(tdes[0]);
   tdes[1] = vmtoh32(tdes[1]);
   tdes[2] = vmtoh32(tdes[2]);
   tdes[3] = vmtoh32(tdes[3]);
   
#if DEBUG_TRANSMIT
   BCM_LOG(d,"=== TRANSMIT PATH ===\n");

   BCM_LOG(d,"tx_current=0x%8.8x, "
           "tdes[0]=0x%8.8x, tdes[1]=0x%8.8x, tdes[2]=0x%8.8x\n",
           d->tx_current,tdes[0],tdes[1],tdes[2]);
#endif

   /* Get the buffer size */
   txd_len = tdes[1] & 0x7FF;

   /* Check buffer size */
   if ((d->tx_bufsize + txd_len) >= sizeof(d->tx_buffer))
      goto done;

   /* Copy the packet from memory */
   physmem_copy_from_vm(d->vm,d->tx_buffer+d->tx_bufsize,tdes[0],txd_len);
   d->tx_bufsize += txd_len;

   /* Packet not complete: handle it later */
   if (tdes[1] & BCM5600_TXD_NEOP)
      goto done;

#if DEBUG_TRANSMIT
   mem_dump(d->vm->log_fd,d->tx_buffer,d->tx_bufsize);
#endif

   /* Transmit the packet */
   pkt_data.ingress_port = d->cpu_port;
   pkt_data.pkt = d->tx_buffer;
   pkt_data.pkt_len = d->tx_bufsize - 4;
   pkt_data.sent_to_cpu = TRUE;
   bcm5600_handle_tx_pkt(d,&pkt_data,tdes[2]);

   /* Reset the TX buffer (packet fully transmitted) */
   d->tx_bufsize = 0;

 done:
   /* We have reached end of ring: trigger the TX underrun interrupt */
   if (!(tdes[1] & BCM5600_TXD_RING_CONT)) {
      d->tx_end_scan = 1;
      pci_dev_trigger_irq(d->vm,d->pci_dev);
      BCM_UNLOCK(d);
      return(TRUE);
   } 

   /* Go to the next descriptor */
   d->tx_current += BCM5600_TXD_SIZE;
   BCM_UNLOCK(d);   
   return(TRUE);
}

/* Handle the RX ring */
static int dev_bcm5600_handle_rxring(netio_desc_t *nio,
                                     u_char *pkt,ssize_t pkt_len,
                                     struct nm_16esw_data *d,  
                                     struct bcm5600_port *port)
{  
   struct bcm5600_pkt pkt_data;
   m_uint32_t rxd_len;

#if DEBUG_RECEIVE
   BCM_LOG(d,"=== RECEIVE PATH ===\n");

   BCM_LOG(d,"%s: received a packet of %ld bytes.\n",
           port->name,(u_long)pkt_len);
   mem_dump(d->vm->log_fd,pkt,pkt_len);
#endif

   BCM_LOCK(d);

   if (!d->rx_current || d->rx_end_scan) {
      BCM_UNLOCK(d);
      return(FALSE);
   }
   
   /* Read the current TX descriptor */
   physmem_copy_from_vm(d->vm,pkt_data.rdes,d->rx_current,
                        (4 * sizeof(m_uint32_t)));

   pkt_data.rdes[0] = vmtoh32(pkt_data.rdes[0]);
   pkt_data.rdes[1] = vmtoh32(pkt_data.rdes[1]);
   pkt_data.rdes[2] = vmtoh32(pkt_data.rdes[2]);
   pkt_data.rdes[3] = vmtoh32(pkt_data.rdes[3]);

#if DEBUG_RECEIVE
   BCM_LOG(d,"rx_current=0x%8.8x, "
           "rdes[0]=0x%8.8x, rdes[1]=0x%8.8x, rdes[2]=0x%8.8x\n",
           d->rx_current,pkt_data.rdes[0],pkt_data.rdes[1],pkt_data.rdes[2]);
#endif

   /* Get the buffer size */
   rxd_len = pkt_data.rdes[1] & 0x7FF;

   if (pkt_len > rxd_len) {
      BCM_UNLOCK(d);
      return(FALSE);
   }

   /* Fill the packet info */
   pkt_data.ingress_port = port->id;
   pkt_data.pkt = pkt;
   pkt_data.pkt_len = pkt_len;
   pkt_data.sent_to_cpu = FALSE;

   /* Handle the packet */
   bcm5600_handle_rx_pkt(d,&pkt_data);
  
   /* Signal only an interrupt when a packet has been sent to the CPU */
   if (pkt_data.sent_to_cpu) {
      /* We have reached end of ring: trigger the RX underrun interrupt */
      if (!(pkt_data.rdes[1] & BCM5600_RXD_RING_CONT)) {
         d->rx_end_scan = 1;
         pci_dev_trigger_irq(d->vm,d->pci_dev);
         BCM_UNLOCK(d);
         return(TRUE);
      }

      /* A packet was received */
      pci_dev_trigger_irq(d->vm,d->pci_dev);

      /* Go to the next descriptor */
      d->rx_current += BCM5600_RXD_SIZE;
   }

   BCM_UNLOCK(d);
   return(TRUE);
}

/* pci_bcm5605_read() */
static m_uint32_t pci_bcm5605_read(cpu_gen_t *cpu,struct pci_device *dev,
                                   int reg)
{
   struct nm_16esw_data *d = dev->priv_data;

   switch(reg) {
      case PCI_REG_BAR0:
         return(d->dev->phys_addr);
      default:
         return(0);
   }
}

/* pci_bcm5605_write() */
static void pci_bcm5605_write(cpu_gen_t *cpu,struct pci_device *dev,
                              int reg,m_uint32_t value)
{
   struct nm_16esw_data *d = dev->priv_data;

   switch(reg) {
      case PCI_REG_BAR0:
         vm_map_device(cpu->vm,d->dev,(m_uint64_t)value);
         BCM_LOG(d,"BCM5600 registers are mapped at 0x%x\n",value);
         break;
   }
}

/* Rewrite the base MAC address */
int dev_nm_16esw_burn_mac_addr(vm_instance_t *vm,u_int nm_bay,
                               struct cisco_eeprom *eeprom)
{
   m_uint8_t eeprom_ver;
   size_t offset;
   n_eth_addr_t addr;
   m_uint16_t pid;

   pid = (m_uint16_t)getpid();

   /* Generate automatically the MAC address */
   addr.eth_addr_byte[0] = vm_get_mac_addr_msb(vm);
   addr.eth_addr_byte[1] = vm->instance_id & 0xFF;
   addr.eth_addr_byte[2] = pid >> 8;
   addr.eth_addr_byte[3] = pid & 0xFF;
   addr.eth_addr_byte[4] = 0xF0 + nm_bay;
   addr.eth_addr_byte[5] = 0x00;

   /* Read EEPROM format version */
   cisco_eeprom_get_byte(eeprom,0,&eeprom_ver);

   if (eeprom_ver != 4)
      return(-1);

   if (cisco_eeprom_v4_find_field(eeprom,0xCF,&offset) == -1)
      return(-1);

   cisco_eeprom_set_region(eeprom,offset,addr.eth_addr_byte,6);
   return(0);
}

/* Initialize a NM-16ESW module */
struct nm_16esw_data *
dev_nm_16esw_init(vm_instance_t *vm,char *name,u_int nm_bay,
                  struct pci_bus *pci_bus,int pci_device,int irq)
{
   struct nm_16esw_data *data;
   struct bcm5600_port *port;
   struct vdevice *dev;
   int i,port_id;

   /* Allocate the private data structure */
   if (!(data = malloc(sizeof(*data)))) {
      fprintf(stderr,"%s: out of memory\n",name);
      return NULL;
   }

   memset(data,0,sizeof(*data));
   pthread_mutex_init(&data->lock,NULL);
   data->name = name;
   data->nr_port = 16;
   data->vm = vm;

   /* Create the BCM5600 tables */
   if (bcm5600_table_create(data) == -1)
      return NULL;

   /* Clear the various tables */
   bcm5600_reset_arl(data);
   data->arl_cnt[0] = 1;
   data->t_ptable = bcm5600_table_find(data,BCM5600_ADDR_PTABLE0);
   data->t_vtable = bcm5600_table_find(data,BCM5600_ADDR_VTABLE0);
   data->t_arl    = bcm5600_table_find(data,BCM5600_ADDR_ARL0);
   data->t_marl   = bcm5600_table_find(data,BCM5600_ADDR_MARL0);
   data->t_tbmap  = bcm5600_table_find(data,BCM5600_ADDR_TBMAP0);
   data->t_ttr    = bcm5600_table_find(data,BCM5600_ADDR_TTR0);

   /* Initialize ports */
   data->cpu_port = 27;

   for(i=0;i<data->nr_port;i++) {
      port_id = nm16esw_port_mapping[i];

      port = &data->ports[port_id];
      port->id = port_id;
      snprintf(port->name,sizeof(port->name),"Fa%u/%d",nm_bay,i);
   }

   /* Create the BCM5605 PCI device */
   data->pci_dev = pci_dev_add(pci_bus,name,
                               BCM5605_PCI_VENDOR_ID,BCM5605_PCI_PRODUCT_ID,
                               pci_device,0,irq,data,
                               NULL,pci_bcm5605_read,pci_bcm5605_write);

   if (!data->pci_dev) {
      fprintf(stderr,"%s: unable to create PCI device.\n",name);
      return NULL;
   }

   /* Create the BCM5605 device itself */
   if (!(dev = dev_create(name))) {
      fprintf(stderr,"%s: unable to create device.\n",name);
      return NULL;
   }

   dev->phys_addr = 0;
   dev->phys_len  = 0x200000;
   dev->handler   = dev_bcm5605_access;

   /* Store device info */
   dev->priv_data = data;
   data->dev = dev;

   /* Create the TX ring scanner */
   data->tx_tid = ptask_add((ptask_callback)dev_bcm5600_handle_txring,
                            data,NULL);

   /* Start the MAC address ager */
   data->ager_tid = timer_create_entry(15000,FALSE,10,
                                       (timer_proc)bcm5600_arl_ager,data);
   return data;
}

/* Remove a NM-16ESW from the specified slot */
int dev_nm_16esw_remove(struct nm_16esw_data *data)
{
   /* Stop the Ager */
   timer_remove(data->ager_tid);

   /* Stop the TX ring task */
   ptask_remove(data->tx_tid);

   /* Remove device + PCI stuff */
   pci_dev_remove(data->pci_dev);
   vm_unbind_device(data->vm,data->dev);
   cpu_group_rebuild_mts(data->vm->cpu_group);
   free(data->dev);

   /* Free all tables and registers */
   bcm5600_table_free(data);
   bcm5600_reg_free(data);
   free(data);
   return(0);
}

/* Bind a Network IO descriptor */
int dev_nm_16esw_set_nio(struct nm_16esw_data *d,u_int port_id,
                         netio_desc_t *nio)
{
   struct bcm5600_port *port;

   if (!d || (port_id >= d->nr_port))
      return(-1);

   /* define the new NIO */
   port = &d->ports[nm16esw_port_mapping[port_id]];
   port->nio = nio;
   netio_rxl_add(nio,(netio_rx_handler_t)dev_bcm5600_handle_rxring,d,port);
   return(0);
}

/* Unbind a Network IO descriptor */
int dev_nm_16esw_unset_nio(struct nm_16esw_data *d,u_int port_id)
{
   struct bcm5600_port *port;

   if (!d || (port_id >= d->nr_port))
      return(-1);

   port = &d->ports[nm16esw_port_mapping[port_id]];

   if (port->nio) {
      netio_rxl_remove(port->nio);
      port->nio = NULL;
   }

   return(0);
}

/* Show debugging information */
int dev_nm_16esw_show_info(struct nm_16esw_data *d)
{   
   BCM_LOCK(d);
   printf("ARL count = %u\n\n",d->arl_cnt[0]);
   bcm5600_dump_main_tables(d);
   bcm5600_mirror_show_status(d);
   bcm5600_reg_dump(d,FALSE);
   BCM_UNLOCK(d);
   return(0);
}
