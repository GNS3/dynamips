/*  
 * Cisco C7200 (Predator) Simulation Platform.
 * Copyright (C) 2005-2006 Christophe Fillot.  All rights reserved.
 *
 * EEPROM types:
 *   - 0x96: PA-POS-OC3MM
 *
 * Not working at all, just an experimentation (I don't have any PA-POS-OC3)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_c7200.h"
#include "dev_c7200_bay.h"

/* Debugging flags */
#define DEBUG_ACCESS     1
#define DEBUG_TRANSMIT   1
#define DEBUG_RECEIVE    1

/* PCI vendor/product codes */
#define POS_OC3_PCI_VENDOR_ID    0x10b5
#define POS_OC3_PCI_PRODUCT_ID   0x9060

/* Maximum packet size */
#define POS_OC3_MAX_PKT_SIZE  8192

/* RX descriptors */
#define POS_OC3_RXDESC_OWN        0x80000000  /* Ownership */
#define POS_OC3_RXDESC_WRAP       0x40000000  /* Wrap ring (?) */
#define POS_OC3_RXDESC_FS         0x02000000  /* First Segment (?) */
#define POS_OC3_RXDESC_LS         0x01000000  /* Last Segment (?) */
#define POS_OC3_RXDESC_LEN_MASK   0xfff

/* TX descriptors */
#define POS_OC3_TXDESC_OWN        0x80000000  /* Ownership */
#define POS_OC3_TXDESC_WRAP       0x40000000  /* Wrap ring (?) */
#define POS_OC3_TXDESC_LEN_MASK   0xfff
#define POS_OC3_TXDESC_FS         0x80000000  /* First Segment (?) */
#define POS_OC3_TXDESC_LS         0x40000000  /* Last Segment (?) */
#define POS_OC3_TXDESC_ADDR_MASK  0x3fffffff  /* Buffer address (?) */

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
   m_uint32_t bay_addr;
   m_uint32_t cbma_addr;

   /* physical addresses for start and end of RX/TX rings */
   m_uint32_t rx_start,rx_end,tx_start,tx_end;
  
   /* physical addresses of current RX and TX descriptors */
   m_uint32_t rx_current,tx_current;

   /* "Managing" CPU */
   cpu_mips_t *mgr_cpu;

   /* Virtual device */
   struct vdevice *dev;

   /* PCI device information */
   struct pci_device *pci_dev;

   /* NetIO descriptor */
   netio_desc_t *nio;

   /* Thread used to walk through RX ring */
   pthread_t rx_thread;
};

/* EEPROM definition */
static unsigned short eeprom_pos_oc3_data[64] = {
   0x0196, 0x0202, 0xffff, 0xffff, 0x490C, 0x7806, 0x0000, 0x0000,
   0x5000, 0x0000, 0x0208, 0x1900, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF,
};

static struct c7200_eeprom eeprom_pos_oc3 = {
   "PA-POS-OC3MM", eeprom_pos_oc3_data, sizeof(eeprom_pos_oc3_data)/2,
};

/*
 *  pos_oc3_access()
 */
void *pos_oc3_access(cpu_mips_t *cpu,struct vdevice *dev,m_uint32_t offset,
                     u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct pos_oc3_data *d = dev->priv_data;

   if (op_type == MTS_READ)
      *data = 0;

#if DEBUG_ACCESS
   if (op_type == MTS_READ)
      m_log(d->name,"read  access to offset = 0x%x, pc = 0x%llx\n",
            offset,cpu->pc);
   else
      if (offset != 0x30404)
         m_log(d->name,"write access to vaddr = 0x%x, pc = 0x%llx, "
               "val = 0x%llx\n",offset,cpu->pc,*data);
#endif

   /* Specific cases */
   switch(offset) {
      case 0x30404:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;
      case 0x30406:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;
      case 0x30407:
         if (op_type == MTS_READ)
            *data = 0xFFFFFFFF;
         break;

      case 0x200004:
         if (op_type == MTS_READ)
            *data = d->rx_start;
         else
            d->rx_start = *data;
         break;

      case 0x200008:
         if (op_type == MTS_READ)
            *data = d->rx_current;
         else
            d->rx_current = *data;
         break;

      case 0x300004:
         if (op_type == MTS_READ)
            *data = d->tx_start;
         else
            d->tx_start = *data;
         break;

      case 0x300008:
         if (op_type == MTS_READ)
            *data = d->tx_current;
         else
            d->tx_current = *data;
         break;

      case 0x700000:
      case 0x700004:
      case 0x70001c:
         if (op_type == MTS_READ)
            *data = 0x00000FFF;         
         break;

      case 0x700008:
         if (op_type == MTS_READ)
            *data = 0x000007F;      
         break;

   }

   return NULL;
}

/*
 * Get the address of the next RX descriptor.
 */
static m_uint32_t rxdesc_get_next(struct pos_oc3_data *d,m_uint32_t rxd_addr)
{
   m_uint32_t nrxd_addr;

   if (rxd_addr == d->rx_end)
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
   m_log(d->name,"reading RX descriptor at address 0x%x\n",rxd_addr);
#endif

   /* get the next descriptor from VM physical RAM */
   physmem_copy_from_vm(d->mgr_cpu,rxd,rxd_addr,sizeof(struct rx_desc));

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
   m_log(d->name,"copying %d bytes at 0x%x\n",cp_len,rxd->rdes[1]);
#endif
      
   /* copy packet data to the VM physical RAM */
   physmem_copy_to_vm(d->mgr_cpu,*pkt,rxd->rdes[1],cp_len);
      
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
      rxdn_addr = rxdesc_get_next(d,d->rx_current);

      /* We have finished if the complete packet has been stored */
      if (tot_len == 0) {
         rxdc->rdes[0] = POS_OC3_RXDESC_LS;
         rxdc->rdes[0] |= cp_len;

         if (i != 0)
            physmem_copy_u32_to_vm(d->mgr_cpu,d->rx_current,rxdc->rdes[0]);

         d->rx_current = rxdn_addr;
         break;
      }

#if DEBUG_RECEIVE
      m_log(d->name,"trying to acquire new descriptor at 0x%x\n",rxdn_addr);
#endif

      /* Get status of the next descriptor to see if we can acquire it */
      rxdn_rdes0 = physmem_copy_u32_from_vm(d->mgr_cpu,rxdn_addr);

      if (!rxdesc_acquire(rxdn_rdes0))
         rxdc->rdes[0] = POS_OC3_RXDESC_LS /*| POC_OC3_RXDESC_OVERRUN*/;
      else
         rxdc->rdes[0] = 0x00000000;  /* ok, no special flag */

      rxdc->rdes[0] |= cp_len;

      /* Update the new status (only if we are not on the first desc) */
      if (i != 0)
         physmem_copy_u32_to_vm(d->mgr_cpu,d->rx_current,rxdc->rdes[0]);

      /* Update the RX pointer */
      d->rx_current = rxdn_addr;

      if (rxdc->rdes[0] & POS_OC3_RXDESC_LS)
         break;

      /* Read the next descriptor from VM physical RAM */
      rxdesc_read(d,rxdn_addr,&rxdn);
      rxdc = &rxdn;
   }

   /* Update the first RX descriptor */
   rxd0.rdes[0] |= POS_OC3_RXDESC_FS;
   physmem_copy_u32_to_vm(d->mgr_cpu,rx_start,rxd0.rdes[0]);

   /* Indicate that we have a frame ready */
   //d->csr[5] |= DEC21140_CSR5_RI;

   /* Generate IRQ on CPU */
   pci_dev_trigger_irq(d->mgr_cpu,d->pci_dev);
}

/* Handle the RX ring */
static void dev_pos_oc3_handle_rxring(struct pos_oc3_data *d)
{      
   u_char pkt[POS_OC3_MAX_PKT_SIZE];
   ssize_t pkt_len;

   pkt_len = netio_recv(d->nio,pkt,POS_OC3_MAX_PKT_SIZE);

   if (pkt_len < 0) {
      m_log(d->name,"net_io RX failed %s\n",strerror(errno));
      return;
   }

#if DEBUG_RECEIVE
   m_log(d->name,"receiving a packet of %d bytes\n",pkt_len);
   mem_dump(log_file,pkt,pkt_len);
#endif

   dev_pos_oc3_receive_pkt(d,pkt,pkt_len);
}

/* Read a TX descriptor */
static void txdesc_read(struct pos_oc3_data *d,m_uint32_t txd_addr,
                        struct tx_desc *txd)
{
   /* get the next descriptor from VM physical RAM */
   physmem_copy_from_vm(d->mgr_cpu,txd,txd_addr,sizeof(struct tx_desc));

   /* byte-swapping */
   txd->tdes[0] = vmtoh32(txd->tdes[0]);
   txd->tdes[1] = vmtoh32(txd->tdes[1]);
}

/* Set the address of the next TX descriptor */
static void txdesc_set_next(struct pos_oc3_data *d)
{
   if (d->tx_current == d->tx_end)
      d->tx_current = d->tx_start;
   else
      d->tx_current += sizeof(struct tx_desc);
}

/* Handle the TX ring */
static int dev_pos_oc3_handle_txring(struct pos_oc3_data *d)
{
   u_char pkt[POS_OC3_MAX_PKT_SIZE],*pkt_ptr;
   m_uint32_t tx_start,clen,tot_len,addr;
   struct tx_desc txd0,ctxd,*ptxd;
   int done = FALSE;

   if ((d->tx_start == 0) || (d->nio == NULL))
      return(FALSE);

   /* Copy the current txring descriptor */
   tx_start = d->tx_current;   
   ptxd = &txd0;
   txdesc_read(d,d->tx_current,ptxd);

   /* If the we don't own the descriptor, we cannot transmit */
   if (!(txd0.tdes[0] & POS_OC3_TXDESC_OWN))
      return(FALSE);

#if DEBUG_TRANSMIT
   m_log(d->name,"pos_oc3_handle_txring: 1st desc: "
         "tdes[0]=0x%x, tdes[1]=0x%x\n",ptxd->tdes[0],ptxd->tdes[1]);
#endif

   pkt_ptr = pkt;
   tot_len = 0;

   do {
#if DEBUG_TRANSMIT
      m_log(d->name,"pos_oc3_handle_txring: loop: "
            "tdes[0]=0x%x, tdes[1]=0x%x\n",ptxd->tdes[0],ptxd->tdes[1]);
#endif

      if (!(ptxd->tdes[0] & POS_OC3_TXDESC_OWN)) {
         m_log(d->name,"pos_oc3_handle_txring: descriptor not owned!\n");
         return(FALSE);
      }

      clen = ptxd->tdes[0] & POS_OC3_TXDESC_LEN_MASK;

      /* Be sure that we have length not null */
      if (clen != 0) {
         addr = ptxd->tdes[1] & POS_OC3_TXDESC_ADDR_MASK;
         physmem_copy_from_vm(d->mgr_cpu,pkt_ptr,addr,clen);
      }

      pkt_ptr += clen;
      tot_len += clen;

      /* Clear the OWN bit if this is not the first descriptor */
      if (!(ptxd->tdes[1] & POS_OC3_TXDESC_FS))
         physmem_copy_u32_to_vm(d->mgr_cpu,d->tx_current,0);

      /* Go to the next descriptor */
      txdesc_set_next(d);

      /* Copy the next txring descriptor */
      if (!(ptxd->tdes[1] & POS_OC3_TXDESC_LS)) {
         txdesc_read(d,d->tx_current,&ctxd);
         ptxd = &ctxd;
      } else
         done = TRUE;
   }while(!done);

   if (tot_len != 0) {
#if DEBUG_TRANSMIT
      m_log(d->name,"sending of packet of %u bytes (flags=0x%4.4x)\n",
            tot_len,txd0.tdes[0]);
      mem_dump(log_file,pkt,tot_len);
#endif
      /* send it on wire */
      netio_send(d->nio,pkt,tot_len);
   }

   /* Clear the OWN flag of the first descriptor */
   physmem_copy_u32_to_vm(d->mgr_cpu,tx_start,0);

   /* Interrupt on completion ? */
   pci_dev_trigger_irq(d->mgr_cpu,d->pci_dev);   
   return(TRUE);
}

/* RX thread */
static void *dev_pos_oc3_rxthread(void *arg)
{
   struct pos_oc3_data *d = arg;

   for(;;)
      dev_pos_oc3_handle_rxring(d);

   return NULL;
}

/*
 * pos_oc3_read()
 */
static m_uint32_t pos_oc3_read(struct pci_device *dev,int reg)
{   
   struct pos_oc3_data *d = dev->priv_data;

#if DEBUG_ACCESS
   m_log(d->name,"read PCI register 0x%x\n",reg);
#endif
   switch(reg) {
      default:
         return(0);
   }
}

/*
 * pos_oc3_write()
 */
static void pos_oc3_write(struct pci_device *dev,int reg,m_uint32_t value)
{
   struct pos_oc3_data *d = dev->priv_data;

#if DEBUG_ACCESS
   m_log(d->name,"write 0x%x to PCI register 0x%x\n",reg,value);
#endif

   switch(reg) {
   }
}

/*
 * dev_c7200_pa_pos_init()
 *
 * Add a PA-POS port adapter into specified slot.
 */
int dev_c7200_pa_pos_init(c7200_t *router,char *name,u_int pa_bay)
{
   struct pa_bay_info *bay_info;
   struct pci_device *pci_dev;
   struct pos_oc3_data *d;
   struct vdevice *dev;
   cpu_mips_t *cpu0;

   /* Device is managed by CPU0 */ 
   cpu0 = cpu_group_find_id(router->cpu_group,0);

   /* Allocate the private data structure for PA-POS-OC3 chip */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"%s (PA-POS-OC3): out of memory\n",name);
      return(-1);
   }

   memset(d,0,sizeof(*d));

   /* Set the EEPROM */
   c7200_pa_set_eeprom(pa_bay,&eeprom_pos_oc3);

   /* Get PCI bus info about this bay */
   if (!(bay_info = c7200_get_pa_bay_info(pa_bay))) {
      fprintf(stderr,"%s: unable to get info for PA bay %u\n",name,pa_bay);
      return(-1);
   }

   /* Add as PCI device PA-POS-OC3 */
   pci_dev = pci_dev_add(router->pa_bay[pa_bay].pci_map,name,
                         POS_OC3_PCI_VENDOR_ID,POS_OC3_PCI_PRODUCT_ID,
                         bay_info->pci_secondary_bus,0,0,C7200_NETIO_IRQ,d,
                         NULL,pos_oc3_read,pos_oc3_write);

   if (!pci_dev) {
      fprintf(stderr,"%s (PA-POS-OC3): unable to create PCI device.\n",name);
      return(-1);
   }

   /* Create the PA-POS-OC3 structure */
   d->name        = name;
   d->bay_addr    = bay_info->phys_addr;
   d->pci_dev     = pci_dev;
   d->mgr_cpu     = cpu0;

   /* Create the device itself */
   if (!(dev = dev_create(name))) {
      fprintf(stderr,"%s (PA-POS-OC3): unable to create device.\n",name);
      return(-1);
   }

   dev->phys_addr = bay_info->phys_addr;
   dev->phys_len  = 0x800000;
   dev->handler   = pos_oc3_access;

   /* Store device info */
   dev->priv_data = d;
   d->dev = dev;

   /* Map this device to all CPU */
   cpu_group_bind_device(router->cpu_group,dev);

   /* Start the TX ring scanner */
   ptask_add((ptask_callback)dev_pos_oc3_handle_txring,d,NULL);

   /* Store device info into the router structure */
   return(c7200_set_slot_drvinfo(router,pa_bay,d));
}

/* Bind a Network IO descriptor to a specific port */
int dev_c7200_pa_pos_set_nio(c7200_t *router,u_int pa_bay,u_int port_id,
                             netio_desc_t *nio)
{
   struct pos_oc3_data *data;

   if ((port_id > 0) || !(data = c7200_get_slot_drvinfo(router,pa_bay)))
      return(-1);

   data->nio = nio;

   /* create the RX thread */
   pthread_create(&data->rx_thread,NULL,dev_pos_oc3_rxthread,data);
   return(0);
}
