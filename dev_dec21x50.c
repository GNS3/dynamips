/* 
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Cisco C7200 (Predator) DEC21050/DEC21150 PCI bridges.
 * This is just a fake device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "pci_dev.h"

#define PCI_VENDOR_DEC         0x1011
#define PCI_PRODUCT_DEC_21050  0x0001
#define PCI_PRODUCT_DEC_21150  0x0023

/*
 * dev_dec21050_init()
 */
int dev_dec21050_init(struct pci_data *pci_data,int pci_bus,int pci_device)
{
   struct pci_device *dev;
   
   dev = pci_dev_add_basic(pci_data,"dec21050",
                           PCI_VENDOR_DEC,PCI_PRODUCT_DEC_21050,
                           pci_bus,pci_device,0);
   return((dev != NULL) ? 0 : -1);
}

/*
 * dev_dec21150_init()
 */
int dev_dec21150_init(struct pci_data *pci_data,int pci_bus,int pci_device)
{
   struct pci_device *dev;
   
   dev = pci_dev_add_basic(pci_data,"dec21150",
                           PCI_VENDOR_DEC,PCI_PRODUCT_DEC_21150,
                           pci_bus,pci_device,0);
   return((dev != NULL) ? 0 : -1);
}
