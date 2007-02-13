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
#define DEBUG_ACCESS    0
#define DEBUG_UNKNOWN   0
#define DEBUG_DMA       0
#define DEBUG_MII       0

/* PCI identification */
#define PCI_VENDOR_MARVELL           0x11ab  /* Marvell/Galileo */
#define PCI_PRODUCT_MARVELL_MV64460  0x6485  /* MV-64460 */

/* Interrupt Low Main Cause Register */
#define MV64460_REG_ILMCR   0x0004

#define MV64460_ILMCR_IDMA0_COMP  0x00000010  /* IDMA 0 Transfer completed */ 
#define MV64460_ILMCR_IDMA1_COMP  0x00000020  /* IDMA 1 Transfer completed */ 
#define MV64460_ILMCR_IDMA2_COMP  0x00000040  /* IDMA 2 Transfer completed */ 
#define MV64460_ILMCR_IDMA3_COMP  0x00000080  /* IDMA 3 Transfer completed */ 
#define MV64460_ILMCR_TIMER0_EXP  0x00000100  /* Timer 0 expired */
#define MV64460_ILMCR_TIMER1_EXP  0x00000200  /* Timer 1 expired */
#define MV64460_ILMCR_TIMER2_EXP  0x00000400  /* Timer 2 expired */
#define MV64460_ILMCR_TIMER3_EXP  0x00000800  /* Timer 3 expired */

/* Interrupt High Main Cause Register */
#define MV64460_REG_IHMCR   0x000c

/* Interrupt masks for CPU0 */
#define MV64460_REG_CPU0_INTR_MASK_LO  0x0014
#define MV64460_REG_CPU0_INTR_MASK_HI  0x001c

#define MV64460_IHMCR_ETH0_SUM    0x00000001  /* Ethernet 0 */
#define MV64460_IHMCR_ETH1_SUM    0x00000002  /* Ethernet 1 */
#define MV64460_IHMCR_ETH2_SUM    0x00000004  /* Ethernet 2 */
#define MV64460_IHMCR_SDMA_SUM    0x00000010  /* Serial DMA */

#define MV64460_IHMCR_GPP_0_7_SUM    0x01000000
#define MV64460_IHMCR_GPP_8_15_SUM   0x02000000
#define MV64460_IHMCR_GPP_16_23_SUM  0x04000000
#define MV64460_IHMCR_GPP_24_31_SUM  0x08000000

/* GPP Interrupt cause and mask registers */
#define MV64460_REG_GPP_INTR_CAUSE   0xf108
#define MV64460_REG_GPP_INTR_MASK    0xf10c

/* SDMA - number of channels */
#define MV64460_SDMA_CHANNELS     2

/* SDMA registers base offsets */
#define MV64460_REG_SDMA0   0x4000
#define MV64460_REG_SDMA1   0x6000

/* SDMA cause register */
#define MV64460_REG_SDMA_CAUSE     0xb800

#define MV64460_SDMA_CAUSE_RXBUF0  0x00000001  /* RX Buffer returned */
#define MV64460_SDMA_CAUSE_RXERR0  0x00000002  /* RX Error */
#define MV64460_SDMA_CAUSE_TXBUF0  0x00000004  /* TX Buffer returned */
#define MV64460_SDMA_CAUSE_TXEND0  0x00000008  /* TX End */
#define MV64460_SDMA_CAUSE_RXBUF1  0x00000010  /* RX Buffer returned */
#define MV64460_SDMA_CAUSE_RXERR1  0x00000020  /* RX Error */
#define MV64460_SDMA_CAUSE_TXBUF1  0x00000040  /* TX Buffer returned */
#define MV64460_SDMA_CAUSE_TXEND1  0x00000080  /* TX End */

/* SDMA register offsets */
#define MV64460_SDMA_SDC          0x0000  /* Configuration Register */
#define MV64460_SDMA_SDCM         0x0008  /* Command Register */
#define MV64460_SDMA_RX_DESC      0x0800  /* RX descriptor */
#define MV64460_SDMA_RX_BUF_PTR   0x0808  /* Current buffer address ? */
#define MV64460_SDMA_SCRDP        0x0810  /* Current RX descriptor */
#define MV64460_SDMA_TX_DESC      0x0c00  /* TX descriptor */
#define MV64460_SDMA_SCTDP        0x0c10  /* Current TX desc. pointer */
#define MV64460_SDMA_SFTDP        0x0c14  /* First TX desc. pointer */

/* SDMA Descriptor Command/Status word */
#define MV64460_SDMA_CMD_O    0x80000000  /* Owner bit */
#define MV64460_SDMA_CMD_AM   0x40000000  /* Auto-mode */
#define MV64460_SDMA_CMD_EI   0x00800000  /* Enable Interrupt */
#define MV64460_SDMA_CMD_F    0x00020000  /* First buffer */
#define MV64460_SDMA_CMD_L    0x00010000  /* Last buffer */

/* SDMA Command Register (SDCM) */
#define MV64460_SDCM_ERD      0x00000080  /* Enable RX DMA */
#define MV64460_SDCM_AR       0x00008000  /* Abort Receive */
#define MV64460_SDCM_STD      0x00010000  /* Stop TX */
#define MV64460_SDCM_TXD      0x00800000  /* TX Demand */
#define MV64460_SDCM_AT       0x80000000  /* Abort Transmit */

/* SDMA RX/TX descriptor */
struct sdma_desc {
   m_uint32_t buf_size;
   m_uint32_t cmd_stat;
   m_uint32_t next_ptr;
   m_uint32_t buf_ptr;
};

/* SDMA channel */
struct sdma_channel {
   m_uint32_t sdc;
   m_uint32_t sdcm;
   m_uint32_t rx_desc;
   m_uint32_t rx_buf_ptr;
   m_uint32_t scrdp;
   m_uint32_t tx_desc;
   m_uint32_t sctdp;
   m_uint32_t sftdp;

   /* Associated VTTY for UART */
   vtty_t *vtty;
};

/* MV64460 system controller private data */
struct mv64460_data {
   char *name;
   vm_obj_t vm_obj;
   struct vdevice dev;
   struct pci_device *pci_dev;
   vm_instance_t *vm;

   /* Interrupt Main Cause Low and High registers */
   m_uint32_t intr_lo,intr_hi;

   /* CPU0 interrupt masks */
   m_uint32_t cpu0_intr_mask_lo,cpu0_intr_mask_hi;

   /* GPP interrupts */
   m_uint32_t gpp_intr,gpp_mask;

   /* SDMA channels */
   m_uint32_t sdma_cause;
   struct sdma_channel sdma[MV64460_SDMA_CHANNELS];

   /* PCI busses */
   struct pci_bus *bus[2];
};

/* Update the interrupt status for CPU 0 */
static void mv64460_ic_update_cpu0_status(struct mv64460_data *d)
{
   cpu_ppc_t *cpu0 = CPU_PPC32(d->vm->boot_cpu);
   m_uint32_t lo_act,hi_act;

   d->intr_lo = d->intr_hi = 0;

   /* Serial DMA */
   if (d->sdma_cause)
      d->intr_hi |= MV64460_IHMCR_SDMA_SUM;

   /* Test GPP bits */
   if (d->gpp_intr & d->gpp_mask & 0x000000FF)
      d->intr_hi |= MV64460_IHMCR_GPP_0_7_SUM;

   if (d->gpp_intr & d->gpp_mask & 0x0000FF00)
      d->intr_hi |= MV64460_IHMCR_GPP_8_15_SUM;

   if (d->gpp_intr & d->gpp_mask & 0x00FF0000)
      d->intr_hi |= MV64460_IHMCR_GPP_16_23_SUM;

   if (d->gpp_intr & d->gpp_mask & 0xFF000000)
      d->intr_hi |= MV64460_IHMCR_GPP_24_31_SUM;

   lo_act = d->intr_lo & d->cpu0_intr_mask_lo;
   hi_act = d->intr_hi & d->cpu0_intr_mask_hi;

   cpu0->irq_pending = lo_act || hi_act;
   cpu0->irq_check = cpu0->irq_pending;
}

/* Send contents of a SDMA buffer to the associated VTTY */
static void mv64460_sdma_send_buf_to_vtty(struct mv64460_data *d,
                                          struct sdma_channel *chan,
                                          struct sdma_desc *desc)
{
   m_uint32_t buf_addr,len,clen;
   char buffer[512];

   len = (desc->buf_size >> 16) & 0xFFFF;
   buf_addr = desc->buf_ptr;

   //vm_log(d->vm,"SDMA","len=0x%8.8x, buf_addr=0x%8.8x\n",len,buf_addr);

   while(len > 0) {
      if (len > sizeof(buffer))
         clen = sizeof(buffer);
      else
         clen = len;

      physmem_copy_from_vm(d->vm,buffer,buf_addr,clen);
      vtty_put_buffer(chan->vtty,buffer,clen);

      len -= clen;
      buf_addr += clen;
   }
}

/* Fetch a SDMA descriptor */
static void mv64460_sdma_fetch_desc(struct mv64460_data *d,m_uint32_t addr,
                                    struct sdma_desc *desc)
{
   physmem_copy_from_vm(d->vm,desc,addr,sizeof(struct sdma_desc));

   /* byte-swapping */
   desc->buf_size = vmtoh32(desc->buf_size);
   desc->cmd_stat = vmtoh32(desc->cmd_stat);
   desc->next_ptr = vmtoh32(desc->next_ptr);
   desc->buf_ptr  = vmtoh32(desc->buf_ptr);
}

/* Start TX DMA process */
static void mv64460_sdma_tx_start(struct mv64460_data *d,
                                  struct sdma_channel *chan)
{
   struct sdma_desc desc;
   m_uint32_t desc_addr;

   desc_addr = chan->sctdp;

   //vm_log(d->vm,"SDMA","TX fetch starting: 0x%8.8x\n",desc_addr);

   while(desc_addr != 0)
   {
      //vm_log(d->vm,"SDMA","fetching descriptor at 0x%8.8x\n",desc_addr);

      /* Fetch the descriptor */
      mv64460_sdma_fetch_desc(d,desc_addr,&desc);
      chan->sctdp = desc_addr;

#if 0
      vm_log(d->vm,"SDMA","buf_size=0x%8.8x, cmd_stat=0x%8.8x, "
             "next_ptr=0x%8.8x, buf_ptr=0x%8.8x\n",
             desc.buf_size,desc.cmd_stat,desc.next_ptr,desc.buf_ptr);
#endif

      if (!(desc.cmd_stat & MV64460_SDMA_CMD_O)) {
         d->sdma_cause |= 4;
         mv64460_ic_update_cpu0_status(d);
         return;
      }

      mv64460_sdma_send_buf_to_vtty(d,chan,&desc);

      desc.buf_size &= 0xFFFF0000;
      desc.cmd_stat &= ~MV64460_SDMA_CMD_O;

      physmem_copy_u32_to_vm(d->vm,desc_addr,desc.buf_size);
      physmem_copy_u32_to_vm(d->vm,desc_addr+4,desc.cmd_stat);

      desc_addr = desc.next_ptr;
   }

   d->sdma_cause |= 4;
   mv64460_ic_update_cpu0_status(d);

   /* Clear the TXD bit */
   chan->sdcm &= ~MV64460_SDCM_TXD;
}

/* Put data into a RX DMA buffer */
static void mv64460_sdma_put_rx_data(struct mv64460_data *d,
                                     struct sdma_channel *chan,
                                     char *buffer,size_t buf_len)
{
   struct sdma_desc desc;
   m_uint32_t desc_addr;

   desc_addr = chan->scrdp;

   /* Fetch the current SDMA buffer */
   mv64460_sdma_fetch_desc(d,desc_addr,&desc);

#if 0
   vm_log(d->vm,"SDMA_RX","buf_size=0x%8.8x, cmd_stat=0x%8.8x, "
          "next_ptr=0x%8.8x, buf_ptr=0x%8.8x\n",
          desc.buf_size,desc.cmd_stat,desc.next_ptr,desc.buf_ptr);
#endif

   if (!(desc.cmd_stat & MV64460_SDMA_CMD_O)) {
      d->sdma_cause |= 1;
      mv64460_ic_update_cpu0_status(d);
      return;
   }

   physmem_copy_to_vm(d->vm,buffer,desc.buf_ptr,1);

   desc.buf_size |= 0x00000001;
   desc.cmd_stat &= ~MV64460_SDMA_CMD_O;
      
   physmem_copy_u32_to_vm(d->vm,desc_addr,desc.buf_size);
   physmem_copy_u32_to_vm(d->vm,desc_addr+4,desc.cmd_stat);

   chan->scrdp = desc.next_ptr;

   d->sdma_cause |= 1;
   mv64460_ic_update_cpu0_status(d);
}

/* Input on VTTY 0 */
static void mv64460_tty_input_s0(vtty_t *vtty)
{
   struct mv64460_data *d = vtty->priv_data;
   struct sdma_channel *chan = &d->sdma[0];
   char c;

   c = vtty_get_char(vtty);
   mv64460_sdma_put_rx_data(d,chan,&c,1);
}

/* Input on VTTY 0 */
static void mv64460_tty_input_s1(vtty_t *vtty)
{
   struct mv64460_data *d = vtty->priv_data;
   struct sdma_channel *chan = &d->sdma[1];
   char c;

   c = vtty_get_char(vtty);
   mv64460_sdma_put_rx_data(d,chan,&c,1);
}

/* Bind a VTTY to a SDMA channel */
int mv64460_sdma_bind_vtty(struct mv64460_data *d,u_int chan_id,vtty_t *vtty)
{
   switch(chan_id) {
      case 0:
         vtty->priv_data = d;
         vtty->read_notifier = mv64460_tty_input_s0;
         break;
      case 1:
         vtty->priv_data = d;
         vtty->read_notifier = mv64460_tty_input_s1;
         break;
      default:
         return(-1);
   }

   d->sdma[chan_id].vtty = vtty;
   return(0);
}

/*
 * SDMA registers access.
 */
static int mv64460_sdma_access(struct mv64460_data *d,cpu_gen_t *cpu,
                               m_uint32_t offset,m_uint32_t op_type,
                               m_uint64_t *data)
{
   struct sdma_channel *chan;
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

   if (op_type == MTS_WRITE)
      *data = swap32(*data);

   chan = &d->sdma[id];
   switch(offset) {
      case MV64460_SDMA_SDCM:
         if (op_type == MTS_READ)
            ; //*data = chan->sdcm;
         else {
            chan->sdcm = *data;

            if (chan->sdcm & MV64460_SDCM_TXD)
               mv64460_sdma_tx_start(d,chan);
         }
         break;

      case MV64460_SDMA_SCRDP:
         if (op_type == MTS_READ)
            *data = chan->scrdp;
         else
            chan->scrdp = *data;
         break;

      case MV64460_SDMA_SCTDP:
         if (op_type == MTS_READ)
            *data = chan->sctdp;
         else
            chan->sctdp = *data;
         break;

      case MV64460_SDMA_SFTDP:
         if (op_type == MTS_READ)
            *data = chan->sftdp;
         else
            chan->sftdp = *data;
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

   if (op_type == MTS_READ)
      *data = swap32(*data);

   /* Update the interrupt status */
   mv64460_ic_update_cpu0_status(d);
   return(TRUE);
}

/*
 * dev_mv64460_access()
 */
void *dev_mv64460_access(cpu_gen_t *cpu,struct vdevice *dev,m_uint32_t offset,
                         u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct mv64460_data *mv_data = dev->priv_data;

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

   if (op_type == MTS_READ)
      *data = 0x0;

   if (mv64460_sdma_access(mv_data,cpu,offset,op_type,data))
      return NULL;

   if (op_type == MTS_WRITE)
      *data = swap32(*data);

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

      /* CPU0 Interrupt Mask Low */
      case MV64460_REG_CPU0_INTR_MASK_LO:
         if (op_type == MTS_READ)
            *data = mv_data->cpu0_intr_mask_lo;
         else
            mv_data->cpu0_intr_mask_lo = *data;
         break;

      /* CPU0 Interrupt Mask High */
      case MV64460_REG_CPU0_INTR_MASK_HI:
         if (op_type == MTS_READ)
            *data = mv_data->cpu0_intr_mask_hi;
         else
            mv_data->cpu0_intr_mask_hi = *data;
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

      /* MII */
      case 0x2004:
         if (op_type == MTS_READ)
            *data = 0x08000000;
         break;

      /* GPP interrupt cause */
      case MV64460_REG_GPP_INTR_CAUSE:
         if (op_type == MTS_READ)
            *data = mv_data->gpp_intr;
         break;

      /* GPP interrupt mask */
      case MV64460_REG_GPP_INTR_MASK:
         if (op_type == MTS_READ)
            *data = mv_data->gpp_mask;
         else
            mv_data->gpp_mask = *data;
         break;

      case 0x8030:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

      case 0x9030:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

      /* SDMA cause register */
      case MV64460_REG_SDMA_CAUSE:
         if (op_type == MTS_READ)
            *data = mv_data->sdma_cause;
         else
            mv_data->sdma_cause &= *data;
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

   if (op_type == MTS_READ)
      *data = swap32(*data);

   /* Update the interrupt status */
   mv64460_ic_update_cpu0_status(mv_data);
   return NULL;
}

/* Set value of GPP register */
void dev_mv64460_set_gpp_reg(struct mv64460_data *d,m_uint32_t val)
{
   d->gpp_intr = val;
   mv64460_ic_update_cpu0_status(d);
}

/* Set a GPP interrupt */
void dev_mv64460_set_gpp_intr(struct mv64460_data *d,u_int irq)
{
   d->gpp_intr |= 1 << irq;
   mv64460_ic_update_cpu0_status(d);

#if 0
   printf("SET_GPP_INTR: lo=0x%8.8x, hi=0x%8.8x\n",d->intr_lo,d->intr_hi);
   printf("gpp_intr = 0x%8.8x, gpp_mask = 0x%8.8x\n",d->gpp_intr,d->gpp_mask);
#endif
}

/* Clear a GPP interrupt */
void dev_mv64460_clear_gpp_intr(struct mv64460_data *d,u_int irq)
{
   d->gpp_intr &= ~(1 << irq);
   mv64460_ic_update_cpu0_status(d);
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

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"mv64460: unable to create device data.\n");
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->name = name;
   d->vm = vm;
   d->bus[0] = vm->pci_bus[0];
   d->bus[1] = vm->pci_bus[1];

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

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}
