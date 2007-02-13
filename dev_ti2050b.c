/* 
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Texas Instruments PCI205B PCI bridge.
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

#define PCI_VENDOR_TI         0x104C
#define PCI_PRODUCT_PCI2050B  0xAC28

/*
 * dev_ti2050b_init()
 */
int dev_ti2050b_init(struct pci_bus *pci_bus,int pci_device,
                     struct pci_bus *sec_bus)
{
   struct pci_device *dev;
   
   dev = pci_bridge_create_dev(pci_bus,"ti2050b",
                               PCI_VENDOR_TI,PCI_PRODUCT_PCI2050B,
                               pci_device,0,sec_bus,NULL,NULL);
   return((dev != NULL) ? 0 : -1);
}
