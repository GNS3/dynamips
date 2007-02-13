/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __PCI_IO_H__
#define __PCI_IO_H__

#include "pci_dev.h"

/* PCI I/O data */
struct pci_io_data {
   struct vdevice dev;
   struct pci_io_device *dev_list;
};

/* Add a new PCI I/O device */
struct pci_io_device *pci_io_add(struct pci_io_data *d,
                                 m_uint32_t start,m_uint32_t end,
                                 struct vdevice *dev,dev_handler_t handler);

/* Remove a PCI I/O device */
void pci_io_remove(struct pci_io_device *dev);

/* Remove PCI I/O space */
void pci_io_data_remove(vm_instance_t *vm,struct pci_io_data *d);

/* Initialize PCI I/O space */
struct pci_io_data *pci_io_data_init(vm_instance_t *vm,m_uint64_t paddr);

#endif
