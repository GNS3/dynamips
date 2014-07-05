/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * PCI devices.
 *
 * Very interesting docs:
 *   http://www.science.unitn.it/~fiorella/guidelinux/tlk/node72.html
 *   http://www.science.unitn.it/~fiorella/guidelinux/tlk/node76.html
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#define DEBUG_PCI 1

#define GET_PCI_ADDR(offset,mask) ((pci_bus->pci_addr >> offset) & mask)

/* Trigger a PCI device IRQ */
void pci_dev_trigger_irq(vm_instance_t *vm,struct pci_device *dev)
{
   if (dev->irq != -1)
      vm_set_irq(vm,dev->irq);
}

/* Clear a PCI device IRQ */
void pci_dev_clear_irq(vm_instance_t *vm,struct pci_device *dev)
{
   if (dev->irq != -1)
      vm_clear_irq(vm,dev->irq);
}

/* Swapping function */
static inline m_uint32_t pci_swap(m_uint32_t val,int swap)
{
   return((swap) ? swap32(val) : val);
}

/* PCI bus lookup */
struct pci_bus *pci_bus_lookup(struct pci_bus *pci_bus_root,int bus)
{
   struct pci_bus *next_bus,*cur_bus = pci_bus_root;
   struct pci_bridge *bridge;

   while(cur_bus != NULL) {
      if (cur_bus->bus == bus)
         return cur_bus;
      
      /* Try busses behind PCI bridges */
      next_bus = NULL;

      for(bridge=cur_bus->bridge_list;bridge;bridge=bridge->next) {
         /* 
          * Specific case: final bridge with no checking of secondary
          * bus number. Dynamically programming.
          */
         if (bridge->skip_bus_check) {
            pci_bridge_set_bus_info(bridge,cur_bus->bus,bus,bus);
            bridge->skip_bus_check = FALSE;
            return bridge->pci_bus;
         }

         if ((bus >= bridge->sec_bus) && (bus <= bridge->sub_bus)) {
            next_bus = bridge->pci_bus;
            break;
         }
      }

      cur_bus = next_bus;
   }

   return NULL;
}

/* PCI device local lookup */
struct pci_device *pci_dev_lookup_local(struct pci_bus *pci_bus,
                                        int device,int function)
{
   struct pci_device *dev;

   for(dev=pci_bus->dev_list;dev;dev=dev->next)
      if ((dev->device == device) && (dev->function == function))
         return dev;

   return NULL;
}

/* PCI Device lookup */
struct pci_device *pci_dev_lookup(struct pci_bus *pci_bus_root,
                                  int bus,int device,int function)
{
   struct pci_bus *req_bus;

   /* Find, try to find the request bus */
   if (!(req_bus = pci_bus_lookup(pci_bus_root,bus)))
      return NULL;

   /* Walk through devices present on this bus */
   return pci_dev_lookup_local(req_bus,device,function);
}

/* Handle the address register access */
void pci_dev_addr_handler(cpu_gen_t *cpu,struct pci_bus *pci_bus,
                          u_int op_type,int swap,m_uint64_t *data)
{
   if (op_type == MTS_WRITE)
      pci_bus->pci_addr = pci_swap(*data,swap);
   else
      *data = pci_swap(pci_bus->pci_addr,swap);
}

/*
 * Handle the data register access.
 *
 * The address of requested register is first written at address 0xcf8
 * (with pci_dev_addr_handler).
 *
 * The data is read/written at address 0xcfc.
 */
void pci_dev_data_handler(cpu_gen_t *cpu,struct pci_bus *pci_bus,
                          u_int op_type,int swap,m_uint64_t *data)
{   
   struct pci_device *dev;
   int bus,device,function,reg;

   if (op_type == MTS_READ)
      *data = 0x0;

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
   dev = pci_dev_lookup(pci_bus,bus,device,function);

   if (!dev) {
      if (op_type == MTS_READ) {
         cpu_log(cpu,"PCI","read request for unknown device at pc=0x%llx "
                 "(bus=%d,device=%d,function=%d,reg=0x%2.2x).\n",
                 cpu_get_pc(cpu), bus, device, function, reg);
      } else {
         cpu_log(cpu,"PCI","write request (data=0x%8.8x) for unknown device "
                 "at pc=0x%llx (bus=%d,device=%d,function=%d,reg=0x%2.2x).\n",
                 pci_swap(*data,swap), cpu_get_pc(cpu), 
                 bus, device, function, reg);
      }

      /* Returns an invalid device ID */
      if ((op_type == MTS_READ) && (reg == PCI_REG_ID))
         *data = 0xffffffff;
   } else {
#if DEBUG_PCI
      if (op_type == MTS_READ) {
         cpu_log(cpu,"PCI","read request for device '%s' at pc=0x%llx: "
                 "bus=%d,device=%d,function=%d,reg=0x%2.2x\n",
                 dev->name, cpu_get_pc(cpu), bus, device, function, reg);
      } else {
         cpu_log(cpu,"PCI","write request (data=0x%8.8x) for device '%s' at pc=0x%llx: "
                 "bus=%d,device=%d,function=%d,reg=0x%2.2x\n",
                 pci_swap(*data,swap), dev->name, cpu_get_pc(cpu), 
                 bus, device, function, reg);
      }
#endif
      if (op_type == MTS_WRITE) {
         if (dev->write_register != NULL)
            dev->write_register(cpu,dev,reg,pci_swap(*data,swap));
      } else {
         if (reg == PCI_REG_ID)
            *data = pci_swap((dev->product_id << 16) | dev->vendor_id,swap);
         else {
            if (dev->read_register != NULL)
               *data = pci_swap(dev->read_register(cpu,dev,reg),swap);
         }
      }
   }
}

/* Add a PCI bridge */
struct pci_bridge *pci_bridge_add(struct pci_bus *pci_bus)
{
   struct pci_bridge *bridge;

   if (!pci_bus)
      return NULL;

   if (!(bridge = malloc(sizeof(*bridge)))) {
      fprintf(stderr,"pci_bridge_add: unable to create new PCI bridge.\n");
      return NULL;
   }

   memset(bridge,0,sizeof(*bridge));
   bridge->pri_bus = pci_bus->bus;
   bridge->sec_bus = -1;
   bridge->sub_bus = -1;
   bridge->pci_bus = NULL;

   /* Insert the bridge in the double-linked list */
   bridge->next = pci_bus->bridge_list;
   bridge->pprev = &pci_bus->bridge_list;

   if (pci_bus->bridge_list != NULL)
      pci_bus->bridge_list->pprev = &bridge->next;

   pci_bus->bridge_list = bridge;
   return bridge;
}

/* Remove a PCI bridge from the double-linked list */
static inline void pci_bridge_remove_from_list(struct pci_bridge *bridge)
{
   if (bridge->next)
      bridge->next->pprev = bridge->pprev;

   if (bridge->pprev)
      *(bridge->pprev) = bridge->next;
}

/* Remove a PCI bridge */
void pci_bridge_remove(struct pci_bridge *bridge)
{
   if (bridge != NULL) {
      pci_bridge_remove_from_list(bridge);
      free(bridge);
   }
}

/* Map secondary bus to a PCI bridge */
void pci_bridge_map_bus(struct pci_bridge *bridge,struct pci_bus *pci_bus)
{
   if (bridge != NULL) {
      bridge->pci_bus = pci_bus;

      if (bridge->pci_bus != NULL)
         bridge->pci_bus->bus = bridge->sec_bus;
   }
}

/* Set PCI bridge bus info */
void pci_bridge_set_bus_info(struct pci_bridge *bridge,
                             int pri_bus,int sec_bus,int sub_bus)
{
   if (bridge != NULL) {
      bridge->pri_bus = pri_bus;
      bridge->sec_bus = sec_bus;
      bridge->sub_bus = sub_bus;

      if (bridge->pci_bus != NULL)
         bridge->pci_bus->bus = bridge->sec_bus;
   }
}

/* Add a PCI device */
struct pci_device *
pci_dev_add(struct pci_bus *pci_bus,char *name,
            u_int vendor_id,u_int product_id,
            int device,int function,int irq,
            void *priv_data,pci_init_t init,
            pci_reg_read_t read_register,
            pci_reg_write_t write_register)
{
   struct pci_device *dev;

   if (!pci_bus)
      return NULL;

   if ((dev = pci_dev_lookup_local(pci_bus,device,function)) != NULL) {
      fprintf(stderr,"pci_dev_add: bus %s, device %d, function %d already "
              "registered (device '%s').\n",
              pci_bus->name,device,function,dev->name);
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
   dev->pci_bus = pci_bus;
   dev->device = device;
   dev->function = function;
   dev->irq = irq;
   dev->priv_data = priv_data;
   dev->init = init;
   dev->read_register = read_register;
   dev->write_register = write_register;

   /* Insert the device in the double-linked list */
   dev->next = pci_bus->dev_list;
   dev->pprev = &pci_bus->dev_list;

   if (pci_bus->dev_list != NULL)
      pci_bus->dev_list->pprev = &dev->next;

   pci_bus->dev_list = dev;

   if (init) init(dev);
   return dev;
}

/* Add a basic PCI device that just returns a Vendor/Product ID */
struct pci_device *
pci_dev_add_basic(struct pci_bus *pci_bus,
                  char *name,u_int vendor_id,u_int product_id,
                  int device,int function)
{
   return(pci_dev_add(pci_bus,name,vendor_id,product_id,
                      device,function,-1,NULL,
                      NULL,NULL,NULL));
}

/* Remove a device from the double-linked list */
static inline void pci_dev_remove_from_list(struct pci_device *dev)
{
   if (dev->next)
      dev->next->pprev = dev->pprev;

   if (dev->pprev)
      *(dev->pprev) = dev->next;
}

/* Remove a PCI device */
void pci_dev_remove(struct pci_device *dev)
{
   if (dev != NULL) {
      pci_dev_remove_from_list(dev);
      free(dev);
   }
}

/* Remove a PCI device given its ID (bus,device,function) */
int pci_dev_remove_by_id(struct pci_bus *pci_bus,
                         int bus,int device,int function)
{
   struct pci_device *dev;

   if (!(dev = pci_dev_lookup(pci_bus,bus,device,function)))
      return(-1);

   pci_dev_remove(dev);
   return(0);
}

/* Remove a PCI device given its name */
int pci_dev_remove_by_name(struct pci_bus *pci_bus,char *name)
{
   struct pci_device *dev,*next;
   int count = 0;

   for(dev=pci_bus->dev_list;dev;dev=next) {
      next = dev->next;

      if (!strcmp(dev->name,name)) {
         pci_dev_remove(dev);
         count++;
      }
   }

   return(count);
}

/* Create a PCI bus */
struct pci_bus *pci_bus_create(char *name,int bus)
{
   struct pci_bus *d;

   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"pci_bus_create: unable to create PCI info.\n");
      return NULL;
   }

   memset(d,0,sizeof(*d));
   d->name = strdup(name);
   d->bus  = bus;
   return d;
}

/* Delete a PCI bus */
void pci_bus_remove(struct pci_bus *pci_bus)
{
   struct pci_device *dev,*next;
   struct pci_bridge *bridge,*next_bridge;

   if (pci_bus) {
      /* Remove all devices */
      for(dev=pci_bus->dev_list;dev;dev=next) {
         next = dev->next;
         free(dev);
      }

      /* Remove all bridges */
      for(bridge=pci_bus->bridge_list;bridge;bridge=next_bridge) {
         next_bridge = bridge->next;
         free(bridge);
      }

      /* Free the structure itself */
      free(pci_bus->name);
      free(pci_bus);
   }
}

/* Read a configuration register of a PCI bridge */
static m_uint32_t pci_bridge_read_reg(cpu_gen_t *cpu,struct pci_device *dev,
                                      int reg)
{
   struct pci_bridge *bridge = dev->priv_data;
   m_uint32_t val = 0;

   switch(reg) {
      case 0x18:
         return(bridge->cfg_reg_bus);
      default:
         if (bridge->fallback_read != NULL)
            val = bridge->fallback_read(cpu,dev,reg);
   
         /* Returns appropriate PCI bridge class code if nothing defined */
         if ((reg == 0x08) && !val)
            val = 0x06040000;

         return(val);
   }
}

/* Write a configuration register of a PCI bridge */
static void pci_bridge_write_reg(cpu_gen_t *cpu,struct pci_device *dev,
                                 int reg,m_uint32_t value)
{
   struct pci_bridge *bridge = dev->priv_data;
   u_int pri_bus,sec_bus,sub_bus;

   switch(reg) {
      case 0x18:
         bridge->cfg_reg_bus = value;
         sub_bus = (value >> 16) & 0xFF;
         sec_bus = (value >>  8) & 0xFF;
         pri_bus = value & 0xFF;

         /* Modify the PCI bridge settings */
         vm_log(cpu->vm,"PCI",
                "PCI bridge %d,%d,%d -> pri: %2.2u, sec: %2.2u, sub: %2.2u\n",
                dev->pci_bus->bus,dev->device,dev->function,
                pri_bus,sec_bus,sub_bus);

         pci_bridge_set_bus_info(bridge,pri_bus,sec_bus,sub_bus);
         break;
         
      default:
         if (bridge->fallback_write != NULL)
            bridge->fallback_write(cpu,dev,reg,value);
   }
}

/* Create a PCI bridge device */
struct pci_device *pci_bridge_create_dev(struct pci_bus *pci_bus,char *name,
                                         u_int vendor_id,u_int product_id,
                                         int device,int function,
                                         struct pci_bus *sec_bus,
                                         pci_reg_read_t fallback_read,
                                         pci_reg_write_t fallback_write)
{
   struct pci_bridge *bridge;
   struct pci_device *dev;

   /* Create the PCI bridge structure */
   if (!(bridge = pci_bridge_add(pci_bus)))
      return NULL;

   /* Create the PCI device corresponding to the bridge */
   dev = pci_dev_add(pci_bus,name,vendor_id,product_id,device,function,-1,
                     bridge,NULL,pci_bridge_read_reg,pci_bridge_write_reg);
   
   if (!dev)
      goto err_pci_dev;
   
   /* Keep the associated PCI device for this bridge */
   bridge->pci_dev = dev;

   /* Set the fallback functions */
   bridge->fallback_read  = fallback_read;
   bridge->fallback_write = fallback_write;

   /* Map the secondary bus (disabled at startup) */
   pci_bridge_map_bus(bridge,sec_bus);
   return dev;

 err_pci_dev:
   pci_bridge_remove(bridge);
   return NULL;
}

/* Show PCI device list of the specified bus */
static void pci_bus_show_dev_list(struct pci_bus *pci_bus)
{
   struct pci_device *dev;
   struct pci_bridge *bridge;
   char bus_id[32];

   if (!pci_bus)
      return;

   if (pci_bus->bus != -1) {
      snprintf(bus_id,sizeof(bus_id),"%2d",pci_bus->bus);
   } else {
      strcpy(bus_id,"XX");
   }

   for(dev=pci_bus->dev_list;dev;dev=dev->next) {
      printf("   %-18s: ID %4.4x:%4.4x, Bus %s, Dev. %2d, Func. %2d",
             dev->name,dev->vendor_id,dev->product_id,
             bus_id,dev->device,dev->function);

      if (dev->irq != -1)
         printf(", IRQ: %d\n",dev->irq);
      else
         printf("\n");
   }

   for(bridge=pci_bus->bridge_list;bridge;bridge=bridge->next)
      pci_bus_show_dev_list(bridge->pci_bus);
}

/* Show PCI device list */
void pci_dev_show_list(struct pci_bus *pci_bus)
{
   if (!pci_bus)
      return;

   printf("PCI Bus \"%s\" Device list:\n",pci_bus->name);
   pci_bus_show_dev_list(pci_bus);
   printf("\n");
}
