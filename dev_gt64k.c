/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Galileo GT64010/GT64120A system controller.
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

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#define DEBUG_UNKNOWN  1
#define DEBUG_DMA      0

#define PCI_VENDOR_GALILEO           0x11ab  /* Galileo Technology */
#define PCI_PRODUCT_GALILEO_GT64010  0x0146  /* GT-64010 */
#define PCI_PRODUCT_GALILEO_GT64011  0x4146  /* GT-64011 */
#define PCI_PRODUCT_GALILEO_GT64120  0x4620  /* GT-64120 */

/* DMA definitions */
#define GT64K_DMA_CHANNELS   4

#define GT64K_DMA_FLYBY_ENABLE  0x00000001  /* FlyBy Enable */  
#define GT64K_DMA_FLYBY_RDWR    0x00000002  /* SDRAM Read/Write (FlyBy) */
#define GT64K_DMA_SRC_DIR       0x0000000c  /* Source Direction */
#define GT64K_DMA_DST_DIR       0x00000030  /* Destination Direction */
#define GT64K_DMA_DATA_LIMIT    0x000001c0  /* Data Transfer Limit */
#define GT64K_DMA_CHAIN_MODE    0x00000200  /* Chained Mode */
#define GT64K_DMA_INT_MODE      0x00000400  /* Interrupt Mode */
#define GT64K_DMA_TRANS_MODE    0x00000800  /* Transfer Mode */
#define GT64K_DMA_CHAN_ENABLE   0x00001000  /* Channel Enable */
#define GT64K_DMA_FETCH_NEXT    0x00002000  /* Fetch Next Record */
#define GT64K_DMA_ACT_STATUS    0x00004000  /* DMA Activity Status */
#define GT64K_DMA_SDA           0x00008000  /* Source/Destination Alignment */
#define GT64K_DMA_MDREQ         0x00010000  /* Mask DMA Requests */
#define GT64K_DMA_CDE           0x00020000  /* Close Descriptor Enable */
#define GT64K_DMA_EOTE          0x00040000  /* End-of-Transfer (EOT) Enable */
#define GT64K_DMA_EOTIE         0x00080000  /* EOT Interrupt Enable */
#define GT64K_DMA_ABORT         0x00100000  /* Abort DMA Transfer */
#define GT64K_DMA_SLP           0x00600000  /* Override Source Address */
#define GT64K_DMA_DLP           0x01800000  /* Override Dest Address */
#define GT64K_DMA_RLP           0x06000000  /* Override Record Address */
#define GT64K_DMA_REQ_SRC       0x10000000  /* DMA Request Source */

/* Galileo GT-64k DMA channel */
struct dma_channel {
   m_uint32_t byte_count;
   m_uint32_t src_addr;
   m_uint32_t dst_addr;
   m_uint32_t cdptr;
   m_uint32_t nrptr;
   m_uint32_t ctrl;
};

/* Galileo GT-64k system controller */
struct gt64k_data {
   struct pci_data *bus[2];
   struct pci_device *pci_dev;
   struct dma_channel dma[GT64K_DMA_CHANNELS];
   cpu_mips_t *mgr_cpu;
   m_uint32_t int_cause_reg;
   m_uint32_t int_mask_reg;
};

/* Fetch a DMA record (chained mode) */
static void gt64k_dma_fetch_rec(cpu_mips_t *cpu,struct dma_channel *channel)
{
   m_uint32_t ptr;
 
#if DEBUG_DMA
   m_log("GT64K_DMA","fetching record at address 0x%x\n",channel->nrptr);
#endif

   /* fetch the record from RAM */
   ptr = channel->nrptr;
   channel->byte_count = swap32(physmem_copy_u32_from_vm(cpu,ptr));
   channel->src_addr   = swap32(physmem_copy_u32_from_vm(cpu,ptr+0x04));
   channel->dst_addr   = swap32(physmem_copy_u32_from_vm(cpu,ptr+0x08));
   channel->nrptr      = swap32(physmem_copy_u32_from_vm(cpu,ptr+0x0c));
   
   /* clear the "fetch next record bit" */
   channel->ctrl &= ~GT64K_DMA_FETCH_NEXT;
}

/* Handle control register of a DMA channel */
static void gt64k_dma_handle_ctrl(cpu_mips_t *cpu,struct gt64k_data *gt_data,
                                  int chan_id)
{
   struct dma_channel *channel = &gt_data->dma[chan_id];
   int done;

   if (channel->ctrl & GT64K_DMA_FETCH_NEXT) {
      if (channel->nrptr == 0) {
         m_log("GT64K_DMA","trying to load a NULL DMA record...\n");
         return;
      }

      gt64k_dma_fetch_rec(cpu,channel);
   }

   if (channel->ctrl & GT64K_DMA_CHAN_ENABLE) 
   {
      do {
         done = TRUE;

#if DEBUG_DMA
         m_log("GT64K_DMA",
               "starting transfer from 0x%x to 0x%x (size=%u bytes)\n",
               channel->src_addr,channel->dst_addr,channel->byte_count&0xFFFF);
#endif
         physmem_dma_transfer(cpu,channel->src_addr,channel->dst_addr,
                              channel->byte_count & 0xFFFF);

         /* chained mode */
         if (!(channel->ctrl & GT64K_DMA_CHAIN_MODE)) {
            if (channel->nrptr) {
               gt64k_dma_fetch_rec(cpu,channel);
               done = FALSE;
            }
         }
      }while(!done);

      /* Trigger DMA interrupt */
      gt_data->int_cause_reg |= 1 << (4 + chan_id);
      pci_dev_trigger_irq(gt_data->mgr_cpu,gt_data->pci_dev);
   }
}

#define DMA_REG(ch,reg_name) \
   if (op_type == MTS_WRITE) \
      gt_data->dma[ch].reg_name = swap32(*data); \
   else \
      *data = swap32(gt_data->dma[ch].reg_name);

/* Handle a DMA channel */
static int gt64k_dma_access(cpu_mips_t *cpu,struct vdevice *dev,
                            m_uint32_t offset,u_int op_size,u_int op_type,
                            m_uint64_t *data)
{
   struct gt64k_data *gt_data = dev->priv_data;

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
            gt64k_dma_handle_ctrl(cpu,gt_data,0);
         return(1);

      case 0x844:
         DMA_REG(1,ctrl);
         if (op_type == MTS_WRITE) 
            gt64k_dma_handle_ctrl(cpu,gt_data,1);
         return(1);

      case 0x848:
         DMA_REG(2,ctrl);
         if (op_type == MTS_WRITE) 
            gt64k_dma_handle_ctrl(cpu,gt_data,2);
         return(1);

      case 0x84c:
         DMA_REG(3,ctrl);
         if (op_type == MTS_WRITE) 
            gt64k_dma_handle_ctrl(cpu,gt_data,3);
         return(1);
   }

   return(0);
}

/*
 * dev_gt64010_access()
 */
void *dev_gt64010_access(cpu_mips_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct gt64k_data *gt_data = dev->priv_data;
	
   if (op_type == MTS_READ)
      *data = 0;

   if (gt64k_dma_access(cpu,dev,offset,op_size,op_type,data) != 0)
      return NULL;

   switch(offset) {
      /* ===== DRAM Settings (completely faked, 128 Mb) ===== */
      case 0x008:    /* ras10_low */
         if (op_type == MTS_READ)
            *data = 0x00000000;
         break;
      case 0x010:    /* ras10_high */
         if (op_type == MTS_READ)
            *data = 0x0000001F;
         break;
      case 0x018:    /* ras32_low */
         if (op_type == MTS_READ)
            *data = 0x00000020;
         break;
      case 0x020:    /* ras32_high */
         if (op_type == MTS_READ)
            *data = 0x0000003F;
         break;
      case 0x400:    /* ras0_low */
         if (op_type == MTS_READ)
            *data = 0x00000000;
         break;
      case 0x404:    /* ras0_high */
         if (op_type == MTS_READ)
            *data = 0x0000001F;
         break;
      case 0x408:    /* ras1_low */
         if (op_type == MTS_READ)
            *data = 0x00000020;
         break;
      case 0x40c:    /* ras1_high */
         if (op_type == MTS_READ)
            *data = 0x0000003F;
         break;
      case 0x410:    /* ras2_low */
         if (op_type == MTS_READ)
            *data = 0x00000040;
         break;
      case 0x414:    /* ras2_high */
         if (op_type == MTS_READ)
            *data = 0x0000005F;
         break;
      case 0x418:    /* ras3_low */
         if (op_type == MTS_READ)
            *data = 0x00000060;
         break;
      case 0x41c:    /* ras3_high */
         if (op_type == MTS_READ)
            *data = 0x0000007F;
         break;

      case 0xc00:    /* pci_cmd */
         if (op_type == MTS_READ)
            *data = swap32(0x00008001);
         break;

      /* ===== Interrupt Cause Register ===== */
      case 0xc18:
         if (op_type == MTS_READ)
            *data = swap32(gt_data->int_cause_reg);
         else
            gt_data->int_cause_reg = swap32(*data);
         break;

      /* ===== Interrupt Mask Register ===== */
      case 0xc1c:
         if (op_type == MTS_READ)
            *data = swap32(gt_data->int_mask_reg);
         else
            gt_data->int_mask_reg = swap32(*data);
         break;

      /* ===== PCI Configuration ===== */
      case PCI_BUS_ADDR:    /* pci configuration address (0xcf8) */
         pci_dev_addr_handler(cpu,gt_data->bus[0],op_type,data);
         break;

      case PCI_BUS_DATA:    /* pci data address (0xcfc) */
         pci_dev_data_handler(cpu,gt_data->bus[0],op_type,data);
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ)
            m_log("GT64010","read from addr 0x%x, pc=0x%llx\n",offset,cpu->pc);
         else
            m_log("GT64010","write to addr 0x%x, value=0x%llx, pc=0x%llx\n",
                  offset,*data,cpu->pc);
#endif
   }

   return NULL;
}

/*
 * dev_gt64120_access()
 */
void *dev_gt64120_access(cpu_mips_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct gt64k_data *gt_data = dev->priv_data;
   
   if (op_type == MTS_READ)
      *data = 0;

   if (gt64k_dma_access(cpu,dev,offset,op_size,op_type,data) != 0)
      return NULL;

   switch(offset) {
      /* ===== DRAM Settings (completely faked, 128 Mb) ===== */
      case 0x008:    /* ras10_low */
         if (op_type == MTS_READ)
            *data = 0x00000000;
         break;
      case 0x010:    /* ras10_high */
         if (op_type == MTS_READ)
            *data = 0x0000001F;
         break;
      case 0x018:    /* ras32_low */
         if (op_type == MTS_READ)
            *data = 0x00000020;
         break;
      case 0x020:    /* ras32_high */
         if (op_type == MTS_READ)
            *data = 0x0000003F;
         break;
      case 0x400:    /* ras0_low */
         if (op_type == MTS_READ)
            *data = 0x00000000;
         break;
      case 0x404:    /* ras0_high */
         if (op_type == MTS_READ)
            *data = 0x0000001F;
         break;
      case 0x408:    /* ras1_low */
         if (op_type == MTS_READ)
            *data = 0x00000020;
         break;
      case 0x40c:    /* ras1_high */
         if (op_type == MTS_READ)
            *data = 0x0000003F;
         break;
      case 0x410:    /* ras2_low */
         if (op_type == MTS_READ)
            *data = 0x00000040;
         break;
      case 0x414:    /* ras2_high */
         if (op_type == MTS_READ)
            *data = 0x0000005F;
         break;
      case 0x418:    /* ras3_low */
         if (op_type == MTS_READ)
            *data = 0x00000060;
         break;
      case 0x41c:    /* ras3_high */
         if (op_type == MTS_READ)
            *data = 0x0000007F;
         break;

      case 0xc00:    /* pci_cmd */
         if (op_type == MTS_READ)
            *data = swap32(0x00008001);
         break;

      /* ===== Interrupt Cause Register ===== */
      case 0xc18:
         if (op_type == MTS_READ)
            *data = swap32(gt_data->int_cause_reg);
         else
            gt_data->int_cause_reg = swap32(*data);
         break;

      /* ===== Interrupt Mask Register ===== */
      case 0xc1c:
         if (op_type == MTS_READ)
            *data = swap32(gt_data->int_mask_reg);
         else
            gt_data->int_mask_reg = swap32(*data);
         break;

      /* ===== PCI Bus 1 ===== */
      case 0xcf0:
         pci_dev_addr_handler(cpu,gt_data->bus[1],op_type,data);
         break;

      case 0xcf4:
         pci_dev_data_handler(cpu,gt_data->bus[1],op_type,data);
         break;
         
      /* ===== PCI Bus 0 ===== */
      case PCI_BUS_ADDR:    /* pci configuration address (0xcf8) */
         pci_dev_addr_handler(cpu,gt_data->bus[0],op_type,data);
         break;

      case PCI_BUS_DATA:    /* pci data address (0xcfc) */
         pci_dev_data_handler(cpu,gt_data->bus[0],op_type,data);
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ)
            m_log("GT64120","read from addr 0x%x, pc=0x%llx\n",offset,cpu->pc);
         else
            m_log("GT64120","write to addr 0x%x, value=0x%llx, pc=0x%llx\n",
                  offset,*data,cpu->pc);
#endif
   }

   return NULL;
}

/* Create a new GT64010 controller */
int dev_gt64010_init(cpu_group_t *cpu_group,m_uint64_t paddr,m_uint32_t len,
                     u_int irq,struct pci_data **pci_data)
{
   struct gt64k_data *gt_data;
   struct vdevice *dev;
   cpu_mips_t *cpu0;

   /* Device IRQ is managed by CPU0 */
   cpu0 = cpu_group_find_id(cpu_group,0);

   if (!(gt_data = malloc(sizeof(*gt_data)))) {
      fprintf(stderr,"gt64010: unable to create device data.\n");
      return(-1);
   }

   /* Create a global PCI bus */
   if (!(gt_data->bus[0] = pci_data_create("MB0/MB1/MB2"))) {
      fprintf(stderr,"gt64010: unable to create PCI data.\n");
      return(-1);
   }

   /* Add the controller as a PCI device */
   gt_data->pci_dev = pci_dev_add(gt_data->bus[0],"gt64010",
                                  PCI_VENDOR_GALILEO,
                                  PCI_PRODUCT_GALILEO_GT64010,
                                  0,0,0,irq,gt_data,NULL,NULL,NULL);
   if (!gt_data->pci_dev) {
      fprintf(stderr,"gt64010: unable to create PCI device.\n");
      return(-1);
   }

   if (!(dev = dev_create("gt64010"))) {
      fprintf(stderr,"gt64010: unable to create device.\n");
      return(-1);
   }

   dev->phys_addr = paddr;
   dev->phys_len  = len;
   dev->handler   = dev_gt64010_access;
   dev->priv_data = gt_data;
   gt_data->mgr_cpu = cpu0;   

   /* Map this device to all CPU */
   cpu_group_bind_device(cpu_group,dev);

   pci_data[0] = gt_data->bus[0];
   pci_data[1] = NULL;
   return(0);
}

/* Create a new GT64120 controller */
int dev_gt64120_init(cpu_group_t *cpu_group,m_uint64_t paddr,m_uint32_t len,
                     u_int irq,struct pci_data **pci_data)
{
   struct gt64k_data *gt_data;
   struct vdevice *dev;
   cpu_mips_t *cpu0;

   /* Device IRQ is managed by CPU0 */
   cpu0 = cpu_group_find_id(cpu_group,0);

   if (!(gt_data = malloc(sizeof(*gt_data)))) {
      fprintf(stderr,"gt64120: unable to create device.\n");
      return(-1);
   }
   
   /* Create two global PCI buses */
   gt_data->bus[0] = pci_data_create("MB0/MB1");
   gt_data->bus[1] = pci_data_create("MB2");

   if (!gt_data->bus[0] || !gt_data->bus[1]) {
      fprintf(stderr,"gt64120: unable to create PCI data.\n");
      return(-1);
   }

   /* Add the controller as a PCI device */
   gt_data->pci_dev = pci_dev_add(gt_data->bus[0],"gt64120",
                                  PCI_VENDOR_GALILEO,
                                  PCI_PRODUCT_GALILEO_GT64120,
                                  0,0,0,irq,gt_data,NULL,NULL,NULL);
   if (!gt_data->pci_dev) {
      fprintf(stderr,"gt64010: unable to create PCI device.\n");
      return(-1);
   }

   if (!(dev = dev_create("gt64120"))) {
      fprintf(stderr,"gt64120: unable to create device.\n");
      return(-1);
   }

   dev->phys_addr = paddr;
   dev->phys_len  = len;
   dev->handler   = dev_gt64120_access;
   dev->priv_data = gt_data;
   gt_data->mgr_cpu = cpu0;   

   /* Map this device to all CPU */
   cpu_group_bind_device(cpu_group,dev);

   pci_data[0] = gt_data->bus[0];
   pci_data[1] = gt_data->bus[1];
   return(0);
}

