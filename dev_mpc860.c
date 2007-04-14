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
#include "dev_mpc860.h"

/* Debugging flags */
#define DEBUG_ACCESS    0
#define DEBUG_UNKNOWN   1
#define DEBUG_IDMA      1

/* Dual-Port RAM */
#define MPC860_DPRAM_OFFSET   0x2000
#define MPC860_DPRAM_SIZE     0x2000
#define MPC860_DPRAM_END      (MPC860_DPRAM_OFFSET + MPC860_DPRAM_SIZE)

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

static inline void dpram_w8(struct mpc860_data *d,m_uint16_t offset,
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

static inline void dpram_w32(struct mpc860_data *d,m_uint16_t offset,
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

/* Update CPM interrupt status */
static void mpc860_update_cpm_int_status(struct mpc860_data *d)
{
   if (d->cipr & d->cimr)
      mpc860_set_pending_irq(d,24);
   else
      mpc860_clear_pending_irq(d,24);
}

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
   void *ptr;

   if ((bd_addr < MPC860_DPRAM_OFFSET) || (bd_addr > MPC860_DPRAM_END))
      return(-1);

   bd->offset = bd_addr - MPC860_DPRAM_OFFSET;
   ptr = &d->dpram[bd->offset];

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
   m_uint16_t dma_base,ibase,bd_offset;

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
   ibase = bd_offset = dpram_r16(d,dma_base+0x00);

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

   switch(offset) {
      /* SWSR - Software Service Register (Watchdog) */
      case 0x000e:
         break;

      /* SIU Interrupt Pending Register */
      case 0x0010:
         if (op_type == MTS_READ)
            *data = d->sipend;
         break;

      /* SIU Interrupt Mask Register */
      case 0x0014:
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
      case 0x00f0:
         if (op_type == MTS_READ)
            *data = 0x3F00F600;
         break;

      /* PISCR - Periodic Interrupt Status and Control Register */
      case 0x0240:
        if (op_type == MTS_WRITE) {
           if (*data & 0x80) {
              d->sipend &= ~0x40000000;
              mpc860_update_irq_status(d);
           }
        }
        break;

      case 0x200:
         if (op_type == MTS_READ)
            *data = 0x45;
         break;

      /* IDMA1 Status and Mask Registers */
      case 0x910:
         if (op_type == MTS_READ) {
            *data = d->idsr[0];
         } else {
            d->idsr[0] &= ~(*data);
         }
         break;

      case 0x914:
         if (op_type == MTS_READ)
            *data = d->idmr[0];
         else
            d->idmr[0] = *data;
         break;

      /* IDMA2 Status and Mask Registers */
      case 0x918:
         if (op_type == MTS_READ)
            *data = d->idsr[1];
         else
            d->idsr[1] &= ~(*data);
         break;

      case 0x91c:
         if (op_type == MTS_READ)
            *data = d->idmr[1];
         else
            d->idmr[1] = *data;
         break;

      /* CIPR - CPM Interrupt Pending Register */
      case 0x944:
         if (op_type == MTS_READ)
            *data = d->cipr;
         else {
            d->cipr &= ~(*data);
            mpc860_update_cpm_int_status(d);
         }
         break;

      /* CIMR - CPM Interrupt Mask Register */
      case 0x948:
         if (op_type == MTS_READ)
            *data = d->cimr;
         else {
            d->cimr = *data;
            mpc860_update_cpm_int_status(d);
         }
         break;

      /* PCSO - Port C Special Options Register */
      case 0x964:
         if (op_type == MTS_WRITE) {
            if (*data & 0x01) {
               MPC_LOG(d,"activating IDMA0\n");
               mpc860_idma_start_channel(d,0);
            }
         }
         break;

      case 0x0966:
         break;

      case 0x9c0:
         if (op_type == MTS_WRITE) {
            printf("OPCODE=0x%llx, CHANNEL=0x%llx\n",
                   (*data >> 8) & 0xF, (*data >> 4) & 0xF);
         }

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

   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   vm_object_add(vm,&d->vm_obj);
   return(0);
}

