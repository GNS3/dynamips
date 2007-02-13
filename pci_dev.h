/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef	__PCI_DEV_H__
#define	__PCI_DEV_H__

#include "utils.h"

#define	PCI_BUS_ADDR  0xcf8
#define	PCI_BUS_DATA  0xcfc

/* PCI ID (Vendor + Device) register */
#define PCI_REG_ID    0x00

/* PCI Base Address Registers (BAR) */
#define PCI_REG_BAR0  0x10
#define PCI_REG_BAR1  0x14
#define PCI_REG_BAR2  0x18
#define PCI_REG_BAR3  0x1c
#define PCI_REG_BAR4  0x20
#define PCI_REG_BAR5  0x24

/* Forward declaration for PCI device */
typedef struct pci_device pci_dev_t;

/* PCI function prototypes */
typedef void (*pci_init_t)(pci_dev_t *dev);
typedef m_uint32_t (*pci_reg_read_t)(cpu_gen_t *cpu,pci_dev_t *dev,int reg);
typedef void (*pci_reg_write_t)(cpu_gen_t *cpu,pci_dev_t *dev,int reg,
                                m_uint32_t value);
/* PCI device */
struct pci_device {
   char *name;
   u_int vendor_id,product_id;
   int device,function,irq;
   void *priv_data;

   /* Parent bus */
   struct pci_bus *pci_bus;

   pci_init_t init;
   pci_reg_read_t read_register;
   pci_reg_write_t write_register;

   struct pci_device *next,**pprev;
};

/* PCI bus */
struct pci_bus {
   char *name;
   m_uint32_t pci_addr;

   /* Bus number */
   int bus;

   /* PCI device list on this bus */
   struct pci_device *dev_list;

   /* PCI bridges to access other busses */
   struct pci_bridge *bridge_list;
};

/* PCI bridge */
struct pci_bridge {
   int pri_bus;   /* Primary Bus */
   int sec_bus;   /* Secondary Bus */
   int sub_bus;   /* Subordinate Bus */
   
   int skip_bus_check;

   /* Bus configuration register */
   m_uint32_t cfg_reg_bus;

   /* PCI bridge device */
   struct pci_device *pci_dev;
   
   /* Secondary PCI bus */
   struct pci_bus *pci_bus;

   /* Fallback handlers to read/write config registers */
   pci_reg_read_t fallback_read;
   pci_reg_write_t fallback_write;

   struct pci_bridge *next,**pprev;
};

/* PCI IO device */
struct pci_io_device {
   m_uint32_t start,end;
   struct vdevice *real_dev;
   dev_handler_t handler;
   struct pci_io_device *next,**pprev;
};

/* Trigger a PCI device IRQ */
void pci_dev_trigger_irq(vm_instance_t *vm,struct pci_device *dev);

/* Clear a PCI device IRQ */
void pci_dev_clear_irq(vm_instance_t *vm,struct pci_device *dev);

/* PCI bus lookup */
struct pci_bus *pci_bus_lookup(struct pci_bus *pci_bus_root,int bus);

/* PCI device local lookup */
struct pci_device *pci_dev_lookup_local(struct pci_bus *pci_bus,
                                        int device,int function);

/* PCI device lookup */
struct pci_device *pci_dev_lookup(struct pci_bus *pci_bus_root,
                                  int bus,int device,int function);

/* Handle the address register access */
void pci_dev_addr_handler(cpu_gen_t *cpu,struct pci_bus *pci_bus,
                          u_int op_type,int swap,m_uint64_t *data);

/* Handle the data register access */
void pci_dev_data_handler(cpu_gen_t *cpu,struct pci_bus *pci_bus,
                          u_int op_type,int swap,m_uint64_t *data);

/* Add a PCI bridge */
struct pci_bridge *pci_bridge_add(struct pci_bus *pci_bus);

/* Remove a PCI bridge */
void pci_bridge_remove(struct pci_bridge *bridge);

/* Map secondary bus to a PCI bridge */
void pci_bridge_map_bus(struct pci_bridge *bridge,struct pci_bus *pci_bus);

/* Set PCI bridge bus info */
void pci_bridge_set_bus_info(struct pci_bridge *bridge,
                             int pri_bus,int sec_bus,int sub_bus);

/* Add a PCI device */
struct pci_device *
pci_dev_add(struct pci_bus *pci_bus,
            char *name,u_int vendor_id,u_int product_id,
            int device,int function,int irq,
            void *priv_data,pci_init_t init,
            pci_reg_read_t read_register,
            pci_reg_write_t write_register);

/* Add a basic PCI device that just returns a Vendor/Product ID */
struct pci_device *
pci_dev_add_basic(struct pci_bus *pci_bus,
                  char *name,u_int vendor_id,u_int product_id,
                  int device,int function);

/* Remove a PCI device */
void pci_dev_remove(struct pci_device *dev);

/* Remove a PCI device given its ID (bus,device,function) */
int pci_dev_remove_by_id(struct pci_bus *pci_bus,
                         int bus,int device,int function);

/* Remove a PCI device given its name */
int pci_dev_remove_by_name(struct pci_bus *pci_bus,char *name);

/* Create a PCI bus */
struct pci_bus *pci_bus_create(char *name,int bus);

/* Delete a PCI bus */
void pci_bus_remove(struct pci_bus *pci_bus);

/* Create a PCI bridge device */
struct pci_device *pci_bridge_create_dev(struct pci_bus *pci_bus,char *name,
                                         u_int vendor_id,u_int product_id,
                                         int device,int function,
                                         struct pci_bus *sec_bus,
                                         pci_reg_read_t fallback_read,
                                         pci_reg_write_t fallback_write);

/* Show PCI device list */
void pci_dev_show_list(struct pci_bus *pci_bus);

#endif
