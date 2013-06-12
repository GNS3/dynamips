/* 
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * DEC21050/DEC21150 PCI bridges.
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

#define PCI_VENDOR_DEC         0x1011
#define PCI_PRODUCT_DEC_21050  0x0001
#define PCI_PRODUCT_DEC_21052  0x0021
#define PCI_PRODUCT_DEC_21150  0x0023
#define PCI_PRODUCT_DEC_21152  0x0024
#define PCI_PRODUCT_DEC_21154  0x0026


/*
 * dev_dec21050_init()
 */
int dev_dec21050_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus)
{
   struct pci_device *dev;
   
   dev = pci_bridge_create_dev(pci_bus,"dec21050",
                               PCI_VENDOR_DEC,PCI_PRODUCT_DEC_21050,
                               pci_device,0,sec_bus,NULL,NULL);
   return((dev != NULL) ? 0 : -1);
}

/*
 * dev_dec21052_init()
 */
int dev_dec21052_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus)
{
   struct pci_device *dev;
   
   dev = pci_bridge_create_dev(pci_bus,"dec21052",
                               PCI_VENDOR_DEC,PCI_PRODUCT_DEC_21052,
                               pci_device,0,sec_bus,NULL,NULL);
   return((dev != NULL) ? 0 : -1);
}

/*
 * dev_dec21150_init()
 */
int dev_dec21150_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus)
{
   struct pci_device *dev;
   
   dev = pci_bridge_create_dev(pci_bus,"dec21150",
                               PCI_VENDOR_DEC,PCI_PRODUCT_DEC_21150,
                               pci_device,0,sec_bus,NULL,NULL);
   return((dev != NULL) ? 0 : -1);
}

/*
 * dev_dec21152_init()
 */
int dev_dec21152_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus)
{
   struct pci_device *dev;
   
   dev = pci_bridge_create_dev(pci_bus,"dec21152",
                               PCI_VENDOR_DEC,PCI_PRODUCT_DEC_21152,
                               pci_device,0,sec_bus,NULL,NULL);
   return((dev != NULL) ? 0 : -1);
}

/*
 * dev_dec21154_init()
 */
int dev_dec21154_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus)
{
   struct pci_device *dev;
   
   dev = pci_bridge_create_dev(pci_bus,"dec21154",
                               PCI_VENDOR_DEC,PCI_PRODUCT_DEC_21154,
                               pci_device,0,sec_bus,NULL,NULL);
   return((dev != NULL) ? 0 : -1);
}
