/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * PCI devices.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#define DEBUG_PCI 1

#define GET_PCI_ADDR(offset,mask) ((pci_data->pci_addr >> offset) & mask)

/* Trigger a PCI device IRQ (future use) */
void pci_dev_trigger_irq(cpu_mips_t *cpu,struct pci_device *dev)
{
   if (dev->irq != -1)
      mips64_set_irq(cpu,dev->irq);
}

/* Clear a PCI device IRQ (future use) */
void pci_dev_clear_irq(cpu_mips_t *cpu,struct pci_device *dev)
{
   if (dev->irq != -1)
      mips64_clear_irq(cpu,dev->irq);
}

/* PCI Device lookup */
struct pci_device *pci_dev_lookup(struct pci_data *pci_data,
                                  int bus,int device,int function)
{
   struct pci_device *dev;

   for(dev=pci_data->dev_list;dev;dev=dev->next)
      if ((dev->bus == bus) && (dev->function == function) &&
          (dev->device == device))
         return dev;

   return NULL;
}

/* Handle the address register access */
void pci_dev_addr_handler(cpu_mips_t *cpu,struct pci_data *pci_data,
                          u_int op_type,m_uint64_t *data)
{
   if (op_type == MTS_WRITE)
      pci_data->pci_addr = swap32(*data);
   else
      *data = swap32(pci_data->pci_addr);
}

/*
 * Handle the data register access.
 *
 * The address of requested register is first written at address 0xcf8
 * (with pci_dev_addr_handler).
 *
 * The data is read/written at address 0xcfc.
 */
void pci_dev_data_handler(cpu_mips_t *cpu,struct pci_data *pci_data,
                          u_int op_type,m_uint64_t *data)
{   
   struct pci_device *dev;
   int bus,device,function,reg;

   if (op_type == MTS_READ)
      *data = 0;

   /*
    * http://www.mega-tokyo.com/osfaq2/index.php/PciSectionOfPentiumVme
    *
    * 31      : Enable Bit 
    * 30 - 24 : Reserved
    * 23 - 16 : Bus Number
    * 15 - 11 : Device Number
    * 10 -  8 : Function Number
    *  7 -  2 : Register Number
    *  1 -  0 : always 00
    */
   bus      = GET_PCI_ADDR(16,0xff);
   device   = GET_PCI_ADDR(11,0x1f);
   function = GET_PCI_ADDR(8,0x7);
   reg      = GET_PCI_ADDR(0,0xff);
   
   /* Find the corresponding PCI device */
   dev = pci_dev_lookup(pci_data,bus,device,function);

#if DEBUG_PCI
   if (op_type == MTS_READ)
      m_log("PCI","read request at pc=0x%llx: "
            "bus=%d,device=%d,function=%d,reg=%d\n",
            cpu->pc, bus, device, function, reg);
   else
      m_log("PCI","write request (data=0x%8.8x) at pc=0x%llx: "
            "bus=%d,device=%d,function=%d,reg=%d\n",
            swap32(*data), cpu->pc, bus, device, function, reg);
#endif

   if (!dev) {
      if (op_type == MTS_READ)
         m_log("PCI","read request for unknown device at pc=0x%llx "
               "(bus=%d,device=%d,function=%d,reg=%d).\n",
               cpu->pc, bus, device, function, reg);
      else
         m_log("PCI","write request (data=0x%8.8x) for unknown "
               "device at pc=0x%llx (bus=%d,device=%d,function=%d,reg=%d).\n",
               swap32(*data), cpu->pc, bus, device, function, reg);

      /* Returns an invalid device ID */
      if ((op_type == MTS_READ) && (reg == PCI_ID_REG))
         *data = 0xffffffff;
   } else {
      if (op_type == MTS_WRITE) {
         if (dev->write_register != NULL)
            dev->write_register(dev,reg,swap32(*data));
      } else {
         if (reg == PCI_ID_REG)
            *data = swap32((dev->product_id << 16) | dev->vendor_id);
         else {
            if (dev->read_register != NULL)
               *data = swap32(dev->read_register(dev,reg));
         }
      }
   }
}

/* Add a PCI device */
struct pci_device *
pci_dev_add(struct pci_data *pci_data,char *name,
            u_int vendor_id,u_int product_id,
            int bus,int device,int function,int irq,void *priv_data,
            void (*init)(struct pci_device *dev),
            m_uint32_t (*read_register)(struct pci_device *dev,int reg),
            void (*write_register)(struct pci_device *dev,
                                   int reg,m_uint32_t value))
{
   struct pci_device *dev;

   if ((dev = pci_dev_lookup(pci_data,bus,device,function)) != NULL) {
      fprintf(stderr,"pci_dev_add: bus %d, device %d, function %d already "
              "registered (device '%s').\n",bus,device,function,dev->name);
      return NULL;
   }

   /* we can create safely the new device */
   if (!(dev = malloc(sizeof(*dev)))) {
      fprintf(stderr,"pci_dev_add: unable to create new PCI device.\n");
      return NULL;
   }

   memset(dev,0,sizeof(*dev));
   dev->name = name;
   dev->vendor_id = vendor_id;
   dev->product_id = product_id;
   dev->bus = bus;
   dev->device = device;
   dev->function = function;
   dev->irq = irq;
   dev->priv_data = priv_data;
   dev->init = init;
   dev->read_register = read_register;
   dev->write_register = write_register;

   dev->next = pci_data->dev_list;
   pci_data->dev_list = dev;

   if (init) init(dev);
   return dev;
}

/* Add a basic PCI device that just returns a Vendor/Product ID */
struct pci_device *
pci_dev_add_basic(struct pci_data *pci_data,
                  char *name,u_int vendor_id,u_int product_id,
                  int bus,int device,int function)
{
   return(pci_dev_add(pci_data,name,vendor_id,product_id,
                      bus,device,function,-1,NULL,
                      NULL,NULL,NULL));
}

/* Create a PCI data holder */
struct pci_data *pci_data_create(char *name)
{
   struct pci_data *d;

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"pci_data_create: unable to create PCI info.\n");
      return NULL;
   }

   memset(d,0,sizeof(*d));
   d->name = name;
   return d;
}

/* Show PCI device list */
void pci_dev_show_list(struct pci_data *pci_data)
{
   struct pci_device *dev;

   if (!pci_data)
      return;

   printf("PCI Bus \"%s\" Device list:\n",pci_data->name);

   for(dev=pci_data->dev_list;dev;dev=dev->next) {
      printf("   %-15s: ID %4.4x:%4.4x, Bus %2u, Dev. %2u, Func. %2u",
             dev->name,dev->vendor_id,dev->product_id,
             dev->bus,dev->device,dev->function);

      if (dev->irq != -1)
         printf(", IRQ: %d\n",dev->irq);
      else
         printf("\n");
   }

   printf("\n");
}
