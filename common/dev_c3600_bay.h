/*
 * Cisco 3600 simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Network Module helper.
 */

#ifndef __DEV_C3600_BAY_H__
#define __DEV_C3600_BAY_H__

#include "pci_dev.h"
#include "dev_c3600.h"

/* NM Information */
struct nm_bay_info {
   int pci_bridge_device;
   int pci_device;
};

/* Get a NM bay information */
struct nm_bay_info *c3600_nm_get_bay_info(u_int chassis,u_int nm_bay);

#endif
