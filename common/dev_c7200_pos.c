/*  
 * Cisco router Simulation Platform.
 * Copyright (c) 2005-2007 Christophe Fillot.  All rights reserved.
 *
 * EEPROM types:
 *   - 0x95: PA-POS-OC3SMI
 *   - 0x96: PA-POS-OC3MM
 *
 * Just an experimentation (I don't have any PA-POS-OC3).
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
#include "dev_plx.h"

/* Debugging flags */
#define DEBUG_ACCESS    0
#define DEBUG_UNKNOWN   0
#define DEBUG_TRANSMIT  0
#define DEBUG_RECEIVE   0

/* PCI vendor/product codes */
#define POS_OC3_PCI_VENDOR_ID    0x10b5
#define POS_OC3_PCI_PRODUCT_ID   0x9060

/* Maximum packet size */
#define POS_OC3_MAX_PKT_SIZE  8192

/* RX descriptors */
#define POS_OC3_RXDESC_OWN        0x80000000  /* Ownership */
#define POS_OC3_RXDESC_WRAP       0x40000000  /* Wrap ring */
#define POS_OC3_RXDESC_CONT       0x08000000  /* Packet continues */
#define POS_OC3_RXDESC_LEN_MASK   0x1fff

/* TX descriptors */
#define POS_OC3_TXDESC_OWN        0x80000000  /* Ownership */
#define POS_OC3_TXDESC_WRAP       0x40000000  /* Wrap ring */
#define POS_OC3_TXDESC_CONT       0x08000000  /* Packet continues */
#define POS_OC3_TXDESC_LEN_MASK   0x1fff

/* RX Descriptor */
struct rx_desc {
   m_uint32_t rdes[2];
};

/* TX Descriptor */
struct tx_desc {
   m_uint32_t tdes[2];
};

/* PA-POS-OC3 Data */
struct pos_oc3_data {
   char *name;

   /* IRQ clearing count */
   u_int irq_clearing_count;

   /* Control register #1 */
   m_uint16_t ctrl_reg1;

   /* CRC size */
   u_int crc_size;

   /* physical addresses for start and end of RX/TX rings */
   m_uint32_t rx_start,rx_end,tx_start,tx_end;
  
   /* physical addresses of current RX and TX descriptors */
   m_uint32_t rx_current,tx_current;

   /* Virtual machine */
   vm_instance_t *vm;

   /* Virtual devices */
   char *rx_name,*tx_name,*cs_name;
   vm_obj_t *rx_obj,*tx_obj,*cs_obj;
   struct vdevice rx_dev,tx_dev,cs_dev;

   /* PCI device information */
   struct vdevice dev;
   struct pci_device *pci_dev;

   /* NetIO descriptor */
   netio_desc_t *nio;

   /* TX ring scanner task id */
   ptask_id_t tx_tid;
};

/* Log a PA-POS-OC3 message */
#define POS_LOG(d,msg...) vm_log((d)->vm,(d)->name,msg)

/*
 * pos_access()
 */
static void *dev_pos_access(cpu_gen_t *cpu,struct vdevice *dev,
                            m_uint32_t offset,u_int op_size,u_int op_type,
                            m_uint64_t *data)
{
   struct pos_oc3_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->name,"read  access to offset = 0x%x, pc = 0x%llx\n",
              offset,cpu_get_pc(cpu));
   } else {
      if (offset != 0x404)
         cpu_log(cpu,d->name,"write access to vaddr = 0x%x, pc = 0x%llx, "
                 "val = 0x%llx\n",offset,cpu_get_pc(cpu),*data);
   }
#endif

   switch(offset) {
      case 0x404:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;
      case 0x406:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;
      case 0x407:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->name,
                    "read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,d->name,
                    "write to unknown addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   return NULL;
}

/*
 * pos_rx_access()
 */
static void *dev_pos_rx_access(cpu_gen_t *cpu,struct vdevice *dev,
                               m_uint32_t offset,u_int op_size,u_int op_type,
                               m_uint64_t *data)
{
   struct pos_oc3_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->name,"read  access to offset = 0x%x, pc = 0x%llx\n",
              offset,cpu_get_pc(cpu));
   } else {
      cpu_log(cpu,d->name,"write access to vaddr = 0x%x, pc = 0x%llx, "
              "val = 0x%llx\n",offset,cpu_get_pc(cpu),*data);
   }
#endif

   switch(offset) {
      case 0x04:
         if (op_type == MTS_READ)
            *data = d->rx_start;
         else
            d->rx_start = *data;
         break;

      case 0x08:
         if (op_type == MTS_READ)
            *data = d->rx_current;
         else
            d->rx_current = *data;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->rx_name,
                    "read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,d->rx_name,
                    "write to unknown addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   return NULL;
}

/*
 * pos_tx_access()
 */
static void *dev_pos_tx_access(cpu_gen_t *cpu,struct vdevice *dev,
                               m_uint32_t offset,u_int op_size,u_int op_type,
                               m_uint64_t *data)
{
   struct pos_oc3_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->tx_name,"read  access to offset = 0x%x, pc = 0x%llx\n",
              offset,cpu_get_pc(cpu));
   } else {
      cpu_log(cpu,d->tx_name,"write access to vaddr = 0x%x, pc = 0x%llx, "
              "val = 0x%llx\n",offset,cpu_get_pc(cpu),*data);
   }
#endif

   switch(offset) {
     case 0x04:
         if (op_type == MTS_READ)
            *data = d->tx_start;
         else
            d->tx_start = *data;
         break;

      case 0x08:
         if (op_type == MTS_READ)
            *data = d->tx_current;
         else
            d->tx_current = *data;
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->tx_name,
                    "read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,d->tx_name,
                    "write to unknown addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   return NULL;
}

/*
 * pos_cs_access()
 */
static void *dev_pos_cs_access(cpu_gen_t *cpu,struct vdevice *dev,
                               m_uint32_t offset,u_int op_size,u_int op_type,
                               m_uint64_t *data)
{
   struct pos_oc3_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,d->cs_name,"read  access to offset = 0x%x, pc = 0x%llx\n",
              offset,cpu_get_pc(cpu));
   } else {
      cpu_log(cpu,d->cs_name,"write access to vaddr = 0x%x, pc = 0x%llx, "
              "val = 0x%llx\n",offset,cpu_get_pc(cpu),*data);
   }
#endif

   switch(offset) {
      case 0x300000:
      case 0x300004:
      case 0x30001c:
         if (op_type == MTS_READ) {
            *data = 0x00000FFF;

            /* Add a delay before clearing the IRQ */
            if (++d->irq_clearing_count == 20) {
               pci_dev_clear_irq(d->vm,d->pci_dev);
               d->irq_clearing_count = 0;
            }
         }     
         break;

      case 0x300008:
         if (op_type == MTS_READ)
            *data = 0x000007F;      
         break;

      case 0x300028:
         if (op_type == MTS_READ) {
            *data = d->ctrl_reg1;
         } else {
            d->ctrl_reg1 = *data;

            switch(*data) {
               case 0x06:
                  d->crc_size = 2;
                  break;
               case 0x07:
                  d->crc_size = 4;
                  break;
               default:
                  d->crc_size = 2;
                  cpu_log(cpu,d->cs_name,
                          "unknown value 0x%4.4llx written in ctrl_reg1\n",
                          *data);
            }
            cpu_log(cpu,d->cs_name,"CRC size set to 0x%4.4x\n",d->crc_size);
         }
         break;

#if DEBUG_UNKNOWN
      default:
         if (op_type == MTS_READ) {
            cpu_log(cpu,d->cs_name,
                    "read from unknown addr 0x%x, pc=0x%llx (size=%u)\n",
                    offset,cpu_get_pc(cpu),op_size);
         } else {
            cpu_log(cpu,d->cs_name,
                    "write to unknown addr 0x%x, value=0x%llx, "
                    "pc=0x%llx (size=%u)\n",
                    offset,*data,cpu_get_pc(cpu),op_size);
         }
#endif
   }

   return NULL;
}

/*
 * Get the address of the next RX descriptor.
 */
static m_uint32_t rxdesc_get_next(struct pos_oc3_data *d,m_uint32_t rxd_addr,
                                  struct rx_desc *rxd)
{
   m_uint32_t nrxd_addr;

   if (rxd->rdes[0] & POS_OC3_RXDESC_WRAP)
      nrxd_addr = d->rx_start;
   else
      nrxd_addr = rxd_addr + sizeof(struct rx_desc);

   return(nrxd_addr);
}

/* Read an RX descriptor */
static void rxdesc_read(struct pos_oc3_data *d,m_uint32_t rxd_addr,
                        struct rx_desc *rxd)
{
#if DEBUG_RECEIVE
   POS_LOG(d,"reading RX descriptor at address 0x%x\n",rxd_addr);
#endif

   /* get the next descriptor from VM physical RAM */
   physmem_copy_from_vm(d->vm,rxd,rxd_addr,sizeof(struct rx_desc));

   /* byte-swapping */
   rxd->rdes[0] = vmtoh32(rxd->rdes[0]);
   rxd->rdes[1] = vmtoh32(rxd->rdes[1]);
}

/* 
 * Try to acquire the specified RX descriptor. Returns TRUE if we have it.
 * It assumes that the byte-swapping is done.
 */
static inline int rxdesc_acquire(m_uint32_t rdes0)
{
   return(rdes0 & POS_OC3_RXDESC_OWN);
}

/* Put a packet in buffer of a descriptor */
static ssize_t rxdesc_put_pkt(struct pos_oc3_data *d,struct rx_desc *rxd,
                              u_char **pkt,ssize_t *pkt_len)
{
   ssize_t len,cp_len;

   len = rxd->rdes[0] & POS_OC3_RXDESC_LEN_MASK;

   /* compute the data length to copy */
   cp_len = m_min(len,*pkt_len);

#if DEBUG_RECEIVE
   POS_LOG(d,"copying %d bytes at 0x%x\n",cp_len,rxd->rdes[1]);
#endif
      
   /* copy packet data to the VM physical RAM */
   physmem_copy_to_vm(d->vm,*pkt,rxd->rdes[1],cp_len);
      
   *pkt += cp_len;
   *pkt_len -= cp_len;
   return(cp_len);
}

/*
 * Put a packet in the RX ring.
 */
static void dev_pos_oc3_receive_pkt(struct pos_oc3_data *d,
                                    u_char *pkt,ssize_t pkt_len)
{
   m_uint32_t rx_start,rxdn_addr,rxdn_rdes0;
   struct rx_desc rxd0,rxdn,*rxdc;
   ssize_t cp_len,tot_len = pkt_len;
   u_char *pkt_ptr = pkt;
   int i;

   if (d->rx_start == 0)
      return;

   /* Truncate the packet if it is too big */
   pkt_len = m_min(pkt_len,POS_OC3_MAX_PKT_SIZE);

   /* Copy the current rxring descriptor */
   rxdesc_read(d,d->rx_current,&rxd0);
   
   /* We must have the first descriptor... */
   if (!rxdesc_acquire(rxd0.rdes[0]))
      return;

   /* Remember the first RX descriptor address */
   rx_start = d->rx_current;

   for(i=0,rxdc=&rxd0;tot_len>0;i++)
   {
      /* Put data into the descriptor buffers */
      cp_len = rxdesc_put_pkt(d,rxdc,&pkt_ptr,&tot_len);

      /* Get address of the next descriptor */
      rxdn_addr = rxdesc_get_next(d,d->rx_current,rxdc);

      /* We have finished if the complete packet has been stored */
      if (tot_len == 0) {
         rxdc->rdes[0] = (cp_len + d->crc_size);

         if (i != 0)
            physmem_copy_u32_to_vm(d->vm,d->rx_current,rxdc->rdes[0]);

         d->rx_current = rxdn_addr;
         break;
      }

#if DEBUG_RECEIVE
      POS_LOG(d,"trying to acquire new descriptor at 0x%x\n",rxdn_addr);
#endif
      /* Get status of the next descriptor to see if we can acquire it */
      rxdn_rdes0 = physmem_copy_u32_from_vm(d->vm,rxdn_addr);

      if (!rxdesc_acquire(rxdn_rdes0))
         rxdc->rdes[0] = 0;  /* error, no buf available (special flag?) */
      else
         rxdc->rdes[0] = POS_OC3_RXDESC_CONT;  /* packet continues */

      rxdc->rdes[0] |= cp_len;

      /* Update the new status (only if we are not on the first desc) */
      if (i != 0)
         physmem_copy_u32_to_vm(d->vm,d->rx_current,rxdc->rdes[0]);

      /* Update the RX pointer */
      d->rx_current = rxdn_addr;

      if (!(rxdc->rdes[0] & POS_OC3_RXDESC_CONT))
         break;

      /* Read the next descriptor from VM physical RAM */
      rxdesc_read(d,rxdn_addr,&rxdn);
      rxdc = &rxdn;
   }

   /* Update the first RX descriptor */
   physmem_copy_u32_to_vm(d->vm,rx_start,rxd0.rdes[0]);

   /* Generate IRQ on CPU */
   pci_dev_trigger_irq(d->vm,d->pci_dev);
}

/* Handle the RX ring */
static int dev_pos_oc3_handle_rxring(netio_desc_t *nio,
                                     u_char *pkt,ssize_t pkt_len,
                                     struct pos_oc3_data *d)
{
#if DEBUG_RECEIVE
   POS_LOG(d,"receiving a packet of %d bytes\n",pkt_len);
   mem_dump(log_file,pkt,pkt_len);
#endif

   dev_pos_oc3_receive_pkt(d,pkt,pkt_len);
   return(TRUE);
}

/* Read a TX descriptor */
static void txdesc_read(struct pos_oc3_data *d,m_uint32_t txd_addr,
                        struct tx_desc *txd)
{
   /* get the next descriptor from VM physical RAM */
   physmem_copy_from_vm(d->vm,txd,txd_addr,sizeof(struct tx_desc));

   /* byte-swapping */
   txd->tdes[0] = vmtoh32(txd->tdes[0]);
   txd->tdes[1] = vmtoh32(txd->tdes[1]);
}

/* Set the address of the next TX descriptor */
static void txdesc_set_next(struct pos_oc3_data *d,struct tx_desc *txd)
{
   if (txd->tdes[0] & POS_OC3_TXDESC_WRAP)
      d->tx_current = d->tx_start;
   else
      d->tx_current += sizeof(struct tx_desc);
}

/* Handle the TX ring */
static int dev_pos_oc3_handle_txring(struct pos_oc3_data *d)
{
   u_char pkt[POS_OC3_MAX_PKT_SIZE],*pkt_ptr;
   m_uint32_t clen,tot_len,norm_len;
   m_uint32_t tx_start,addr;
   struct tx_desc txd0,ctxd,*ptxd;
   int i,done = FALSE;

   if ((d->tx_start == 0) || (d->nio == NULL))
      return(FALSE);

   /* Copy the current txring descriptor */
   tx_start = d->tx_current;   
   ptxd = &txd0;
   txdesc_read(d,d->tx_current,ptxd);

   /* If we don't own the descriptor, we cannot transmit */
   if (!(txd0.tdes[0] & POS_OC3_TXDESC_OWN))
      return(FALSE);

#if DEBUG_TRANSMIT
   POS_LOG(d,"pos_oc3_handle_txring: 1st desc: tdes[0]=0x%x, tdes[1]=0x%x\n",
           ptxd->tdes[0],ptxd->tdes[1]);
#endif

   pkt_ptr = pkt;
   tot_len = 0;
   i = 0;

   do {
#if DEBUG_TRANSMIT
      POS_LOG(d,"pos_oc3_handle_txring: loop: tdes[0]=0x%x, tdes[1]=0x%x\n",
              ptxd->tdes[0],ptxd->tdes[1]);
#endif

      if (!(ptxd->tdes[0] & POS_OC3_TXDESC_OWN)) {
         POS_LOG(d,"pos_oc3_handle_txring: descriptor not owned!\n");
         return(FALSE);
      }

      clen = ptxd->tdes[0] & POS_OC3_TXDESC_LEN_MASK;

      /* Be sure that we have length not null */
      if (clen != 0) {
         addr = ptxd->tdes[1];

         norm_len = normalize_size(clen,4,0);
         physmem_copy_from_vm(d->vm,pkt_ptr,addr,norm_len);
         mem_bswap32(pkt_ptr,norm_len);
      }

      pkt_ptr += clen;
      tot_len += clen;

      /* Clear the OWN bit if this is not the first descriptor */
      if (i != 0)
         physmem_copy_u32_to_vm(d->vm,d->tx_current,0);

      /* Go to the next descriptor */
      txdesc_set_next(d,ptxd);

      /* Copy the next txring descriptor */
      if (ptxd->tdes[0] & POS_OC3_TXDESC_CONT) {
         txdesc_read(d,d->tx_current,&ctxd);
         ptxd = &ctxd;
         i++;
      } else
         done = TRUE;
   }while(!done);

   if (tot_len != 0) {
#if DEBUG_TRANSMIT
      POS_LOG(d,"sending packet of %u bytes (flags=0x%4.4x)\n",
              tot_len,txd0.tdes[0]);
      mem_dump(log_file,pkt,tot_len);
#endif   
      /* send it on wire */
      netio_send(d->nio,pkt,tot_len);
   }

   /* Clear the OWN flag of the first descriptor */
   txd0.tdes[0] &= ~POS_OC3_TXDESC_OWN;
   physmem_copy_u32_to_vm(d->vm,tx_start,txd0.tdes[0]);

   /* Interrupt on completion */
   pci_dev_trigger_irq(d->vm,d->pci_dev);
   return(TRUE);
}

/*
 * pci_pos_read()
 */
static m_uint32_t pci_pos_read(cpu_gen_t *cpu,struct pci_device *dev,int reg)
{
   struct pos_oc3_data *d = dev->priv_data;

#if DEBUG_ACCESS
   POS_LOG(d,"read PCI register 0x%x\n",reg);
#endif

   switch(reg) {
      case PCI_REG_BAR0:
         return(d->dev.phys_addr);
      default:
         return(0);
   }
}

/*
 * pci_pos_write()
 */
static void pci_pos_write(cpu_gen_t *cpu,struct pci_device *dev,
                          int reg,m_uint32_t value)
{
   struct pos_oc3_data *d = dev->priv_data;

#if DEBUG_ACCESS
   POS_LOG(d,"write 0x%x to PCI register 0x%x\n",value,reg);
#endif

   switch(reg) {
      case PCI_REG_BAR0:
         vm_map_device(cpu->vm,&d->dev,(m_uint64_t)value);
         POS_LOG(d,"registers are mapped at 0x%x\n",value);
         break;
   }
}

/*
 * dev_c7200_pa_pos_init()
 *
 * Add a PA-POS port adapter into specified slot.
 */
int dev_c7200_pa_pos_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct pos_oc3_data *d;
   u_int slot = card->slot_id;

   /* Allocate the private data structure for PA-POS-OC3 chip */
   if (!(d = malloc(sizeof(*d)))) {
      vm_error(vm,"%s: out of memory\n",card->dev_name);
      return(-1);
   }

   memset(d,0,sizeof(*d));
   d->name = card->dev_name;
   d->vm   = vm;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_pa("PA-POS-OC3"));
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Initialize RX device */
   d->rx_name = dyn_sprintf("%s_RX",card->dev_name);
   dev_init(&d->rx_dev);
   d->rx_dev.name      = d->rx_name;
   d->rx_dev.priv_data = d;
   d->rx_dev.handler   = dev_pos_rx_access;

   /* Initialize TX device */
   d->tx_name = dyn_sprintf("%s_TX",card->dev_name);
   dev_init(&d->tx_dev);
   d->tx_dev.name      = d->tx_name;
   d->tx_dev.priv_data = d;
   d->tx_dev.handler   = dev_pos_tx_access;

   /* Initialize CS device */
   d->cs_name = dyn_sprintf("%s_CS",card->dev_name);
   dev_init(&d->cs_dev);
   d->cs_dev.name      = d->cs_name;
   d->cs_dev.priv_data = d;
   d->cs_dev.handler   = dev_pos_cs_access;

   /* Initialize PLX9060 for RX part */
   d->rx_obj = dev_plx9060_init(vm,d->rx_name,card->pci_bus,0,&d->rx_dev);

   /* Initialize PLX9060 for TX part */
   d->tx_obj = dev_plx9060_init(vm,d->tx_name,card->pci_bus,1,&d->tx_dev);

   /* Initialize PLX9060 for CS part (CS=card status, chip status, ... ?) */
   d->cs_obj = dev_plx9060_init(vm,d->cs_name,card->pci_bus,2,&d->cs_dev);

   /* Unknown PCI device here (will be mapped at 0x30000) */
   dev_init(&d->dev);
   d->dev.name      = card->dev_name;
   d->dev.priv_data = d;
   d->dev.phys_len  = 0x10000;
   d->dev.handler   = dev_pos_access;

   d->pci_dev = pci_dev_add(card->pci_bus,card->dev_name,0,0,3,0,
                            c7200_net_irq_for_slot_port(slot,0),
                            d,NULL,pci_pos_read,pci_pos_write);

   /* Store device info into the router structure */
   card->drv_info = d;
   return(0);
}

/* Remove a PA-POS-OC3 from the specified slot */
int dev_c7200_pa_pos_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   struct pos_oc3_data *d = card->drv_info;

   /* Remove the PA EEPROM */
   cisco_card_unset_eeprom(card);
   c7200_set_slot_eeprom(VM_C7200(vm),card->slot_id,NULL);

   /* Remove the PCI device */
   pci_dev_remove(d->pci_dev);

   /* Remove the PLX9060 chips */
   vm_object_remove(vm,d->rx_obj);
   vm_object_remove(vm,d->tx_obj);
   vm_object_remove(vm,d->cs_obj);

   /* Remove the devices from the CPU address space */
   vm_unbind_device(vm,&d->rx_dev);
   vm_unbind_device(vm,&d->tx_dev);
   vm_unbind_device(vm,&d->cs_dev);

   vm_unbind_device(vm,&d->dev);
   cpu_group_rebuild_mts(vm->cpu_group);

   /* Free the device structure itself */
   free(d);
   return(0);
}

/* Bind a Network IO descriptor to a specific port */
int dev_c7200_pa_pos_set_nio(vm_instance_t *vm,struct cisco_card *card,
                             u_int port_id,netio_desc_t *nio)
{
   struct pos_oc3_data *d = card->drv_info;

   if (!d || (port_id > 0))
      return(-1);

   if (d->nio != NULL)
      return(-1);

   d->nio = nio;
   d->tx_tid = ptask_add((ptask_callback)dev_pos_oc3_handle_txring,d,NULL);
   netio_rxl_add(nio,(netio_rx_handler_t)dev_pos_oc3_handle_rxring,d,NULL);
   return(0);
}

/* Bind a Network IO descriptor to a specific port */
int dev_c7200_pa_pos_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                               u_int port_id)
{
   struct pos_oc3_data *d = card->drv_info;

   if (!d || (port_id > 0))
      return(-1);

   if (d->nio) {
      ptask_remove(d->tx_tid);
      netio_rxl_remove(d->nio);
      d->nio = NULL;
   }
   return(0);
}

/* PA-POS-OC3 driver */
struct cisco_card_driver dev_c7200_pa_pos_oc3_driver = {
   "PA-POS-OC3", 1, 0,
   dev_c7200_pa_pos_init,
   dev_c7200_pa_pos_shutdown,
   NULL,
   dev_c7200_pa_pos_set_nio,
   dev_c7200_pa_pos_unset_nio,
   NULL,
};
