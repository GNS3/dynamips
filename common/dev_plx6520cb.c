/* 
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * PLX6520CB PCI bridge.
 * This is just a fake device.
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

#define PCI_VENDOR_PLX          0x10b5
#define PCI_PRODUCT_PLX_6520CB  0x6520


/*
 * dev_plx6520cb_init()
 */
int dev_plx6520cb_init(struct pci_bus *pci_bus,int pci_device,
                       struct pci_bus *sec_bus)
{
   struct pci_device *dev;
   
   dev = pci_bridge_create_dev(pci_bus,"plx6520cb",
                               PCI_VENDOR_PLX,PCI_PRODUCT_PLX_6520CB,
                               pci_device,0,sec_bus,NULL,NULL);
   return((dev != NULL) ? 0 : -1);
}
