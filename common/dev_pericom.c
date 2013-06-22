/* 
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Pericom PCI bridge.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "pci_dev.h"

#define PCI_VENDOR_PERICOM   0x12d8
#define PCI_PRODUCT_PERICOM  0x8150

/*
 * dev_pericom_init()
 */
int dev_pericom_init(struct pci_bus *pci_bus,int pci_device,
                     struct pci_bus *sec_bus)
{
   struct pci_device *dev;
   
   dev = pci_bridge_create_dev(pci_bus,"pericom",
                               PCI_VENDOR_PERICOM,PCI_PRODUCT_PERICOM,
                               pci_device,0,sec_bus,NULL,NULL);
   return((dev != NULL) ? 0 : -1);
}
