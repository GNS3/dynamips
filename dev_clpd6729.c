/*
 * Cisco C7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Cirrus Logic PD6729 PCI-to-PCMCIA host adapter.
 *
 * TODO: finish the code!
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_c7200.h"

/* Cirrus Logic PD6729 PCI vendor/product codes */
#define CLPD6729_PCI_VENDOR_ID    0x1013
#define CLPD6729_PCI_PRODUCT_ID   0x1100

/*
 * dev_clpd6729_io_access()
 */
static void *dev_clpd6729_io_access(cpu_mips_t *cpu,struct vdevice *dev,
                                    m_uint32_t offset,u_int op_size,
                                    u_int op_type,m_uint64_t *data)
{
   return NULL;
}

/*
 * dev_clpd6729_init()
 */
int dev_clpd6729_init(cpu_group_t *cpu_group,struct pci_data *pci_data,
                      int pci_bus,int pci_device,
                      struct pci_io_data *pci_io_data,
                      m_uint32_t io_start,m_uint32_t io_end)
{
   struct vdevice *dev;

   /* Create the device itself */
   if (!(dev = dev_create("clpd6729"))) {
      fprintf(stderr,"CLPD6729: unable to create device.\n");
      return(-1);
   }

   pci_io_add(pci_io_data,io_start,io_end,dev,dev_clpd6729_io_access);
   pci_dev_add(pci_data,"clpd6729",
               CLPD6729_PCI_VENDOR_ID,CLPD6729_PCI_PRODUCT_ID,
               pci_bus,pci_device,0,-1,dev,NULL,NULL,NULL);

   return(0);
}
