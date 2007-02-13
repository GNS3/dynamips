/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 *
 * AP1011 - Sturgeon HyperTransport-PCI Bridge.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#define AP1011_PCI_VENDOR_ID   0x14D9
#define AP1011_PCI_PRODUCT_ID  0x0010

/*
 * pci_ap1011_read()
 *
 * Read a PCI register.
 */
static m_uint32_t pci_ap1011_read(cpu_gen_t *cpu,struct pci_device *dev,
                                  int reg)
{
   switch (reg) {
      case 0x08:
         return(0x06040000);
      case 0x34:
         return(0x00000040);
      case 0x40:
         return(0x00210008);
      case 0x44:
         return(0x00000020);
      case 0x48:
         return(0x000000C0);
      default:
         return(0);
   }
}

/* Create an AP1011 Sturgeon HyperTransport-PCI Bridge */
int dev_ap1011_init(struct pci_bus *pci_bus,int pci_device,
                    struct pci_bus *sec_bus)
{
   struct pci_device *dev;

   dev = pci_bridge_create_dev(pci_bus,"ap1011",
                               AP1011_PCI_VENDOR_ID,AP1011_PCI_PRODUCT_ID,
                               pci_device,0,sec_bus,pci_ap1011_read,NULL);

   return((dev != NULL) ? 0 : -1);
}
