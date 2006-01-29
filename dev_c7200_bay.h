/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Cisco C7200 (Predator) Port Adapter Bays.
 */

#ifndef __DEV_C7200_BAY_H__
#define __DEV_C7200_BAY_H__

#include "pci_dev.h"
#include "dev_c7200.h"

#define PCI_BAY_SPACE  0x00800000  /* 8 Mb for each PA bay */

/* PA Information */
struct pa_bay_info {
   int pci_device;          /* Device ID */
   int pci_primary_bus;     /* Primary Bus */
   int pci_secondary_bus;   /* Secondary Bus */
   m_uint64_t phys_addr;    /* Physical Address */
};

/* Set PA EEPROM definition */
int c7200_pa_set_eeprom(u_int pa_bay,struct c7200_eeprom *eeprom);

/* Get a PA bay information */
struct pa_bay_info *c7200_get_pa_bay_info(u_int pa_bay);

/* Get PCI bus ID for a PA */
int c7200_get_pa_bus(u_int pa_bay);

/* Get MMIO address for a PA */
int c7200_get_mmio_addr(u_int pa_bay,m_uint64_t *addr);

#endif
