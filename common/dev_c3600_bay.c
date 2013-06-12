/*
 * Cisco 3600 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Cisco 3600 Network Modules. Info are obtained with "show pci bridge".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "cpu.h"
#include "vm.h"
#include "device.h"
#include "dev_c3600_bay.h"

/* C3620 NM info */
static struct nm_bay_info c3620_nm_bays[2] = {
   { -1, 0x05 },      /* Slot 0: PCI bus 0, device 5 */
   { -1, 0x0d },      /* Slot 1: PCI bus 0, device 13 */
};

/* C3640 NM info */
static struct nm_bay_info c3640_nm_bays[4] = {
   { 0x03, 0x00 },    /* Slot 0: PCI bus 2, device 0 */
   { 0x02, 0x00 },    /* Slot 1: PCI bus 1, device 0 */
   {   -1, 0x08 },    /* Slot 2: PCI bus 2, device 8 */
   {   -1, 0x08 },    /* Slot 3: PCI bus 1, device 8 */
};

/* C3660 NM info */
static struct nm_bay_info c3660_nm_bays[7] = {
   {   -1, 0x06 },    /* Slot 0: PCI bus 0, device 6 */
   { 0x02, 0x00 },    /* Slot 1: PCI bus 2 */
   { 0x07, 0x00 },    /* Slot 2: PCI bus 22 */
   { 0x03, 0x00 },    /* Slot 3: PCI bus 6 */
   { 0x06, 0x00 },    /* Slot 4: PCI bus 18 */
   { 0x04, 0x00 },    /* Slot 5: PCI bus 10 */
   { 0x05, 0x00 },    /* Slot 6: PCI bus 14 */
};

/* Get NM bay information */
struct nm_bay_info *c3600_nm_get_bay_info(u_int chassis,u_int nm_bay)
{
   struct nm_bay_info *bay_info;
   u_int max_bays = 0;

   switch(chassis) {
      case 3620:
         bay_info = c3620_nm_bays;
         max_bays = 2;
         break;
      case 3640:
         bay_info = c3640_nm_bays;
         max_bays = 4;
         break;
      case 3660:
         bay_info = c3660_nm_bays;
         max_bays = 7;
         break;
   }

   if (nm_bay >= max_bays)
      return NULL;
   
   return(&bay_info[nm_bay]);
}
