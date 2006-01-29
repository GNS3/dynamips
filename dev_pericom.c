/* 
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Cisco C7200 (Predator) Pericom PCI bridge.
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

#define PCI_VENDOR_PERICOM   0x12d8
#define PCI_PRODUCT_PERICOM  0x8150

/*
 * dev_pericom_init()
 */
int dev_pericom_init(struct pci_data *pci_data,int pci_bus,int pci_device)
{
   struct pci_device *dev;
   
   dev = pci_dev_add_basic(pci_data,"pericom",
                           PCI_VENDOR_PERICOM,PCI_PRODUCT_PERICOM,
                           pci_bus,pci_device,0);
   return((dev != NULL) ? 0 : -1);
}
