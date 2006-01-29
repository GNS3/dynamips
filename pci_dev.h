/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef	__PCI_DEV_H__
#define	__PCI_DEV_H__

#include "utils.h"
#include "pcireg.h"

#define	PCI_BUS_ADDR  0xcf8
#define	PCI_BUS_DATA  0xcfc

/* PCI device */
struct pci_device {
   char *name;
   u_int vendor_id,product_id;
   int bus,device,function,irq;
   void *priv_data;

   void	(*init)(struct pci_device *dev);
   m_uint32_t (*read_register)(struct pci_device *dev,int reg);
   void (*write_register)(struct pci_device *dev,int reg,m_uint32_t value);

   struct pci_device *next;
};

/* PCI data holder */
struct pci_data {
   char *name;
   m_uint32_t pci_addr;
   struct pci_device *dev_list;
};

/* PCI IO device */
struct pci_io_dev {
   m_uint32_t start,end;
   struct vdevice *real_dev;
   dev_handler_t handler;
   struct pci_io_dev *next;
};

/* PCI I/O space private structure */
struct pci_io_data {
   struct pci_io_dev *dev_list;
};

/* Trigger a PCI device IRQ (future use) */
void pci_dev_trigger_irq(cpu_mips_t *cpu,struct pci_device *dev);

/* Clear a PCI device IRQ (future use) */
void pci_dev_clear_irq(cpu_mips_t *cpu,struct pci_device *dev);

/* PCI Device lookup */
struct pci_device *pci_dev_lookup(struct pci_data *pci_data,
                                  int bus,int device,int function);

/* Handle the address register access */
void pci_dev_addr_handler(cpu_mips_t *cpu,struct pci_data *pci_data,
                          u_int op_type,m_uint64_t *data);

/* Handle the data register access */
void pci_dev_data_handler(cpu_mips_t *cpu,struct pci_data *pci_data,
                          u_int op_type,m_uint64_t *data);

/* Add a PCI device */
struct pci_device *
pci_dev_add(struct pci_data *pci_data,
            char *name,u_int vendor_id,u_int product_id,
            int bus,int device,int function,int irq,
            void *priv_data,
            void (*init)(struct pci_device *dev),
            m_uint32_t (*read_register)(struct pci_device *dev,int reg),
            void (*write_register)(struct pci_device *dev,
                                   int reg,m_uint32_t value));

/* Add a basic PCI device that just returns a Vendor/Product ID */
struct pci_device *
pci_dev_add_basic(struct pci_data *pci_data,
                  char *name,u_int vendor_id,u_int product_id,
                  int bus,int device,int function);

/* Create a PCI data holder */
struct pci_data *pci_data_create(char *name);

/* Show PCI device list */
void pci_dev_show_list(struct pci_data *pci_data);

/* Add a new PCI I/O device */
int pci_io_add(struct pci_io_data *d,m_uint32_t start,m_uint32_t end,
               struct vdevice *dev,dev_handler_t handler);

/* Initialize PCI I/O space */
struct pci_io_data *pci_io_init(cpu_group_t *cpu_group,m_uint64_t paddr);

#endif
