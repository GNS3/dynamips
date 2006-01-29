/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Cisco C7200 (Predator) Port Adapter Bays.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_c7200_bay.h"

/*
 * PA bays memory mapped IO.
 *
 * We can find this information with "sh c7200" (TLB part)
 */
#define PHY_ADRSPC_BAY0	  0x48000000 /* Bay 0 (IO card) */
#define PHY_ADRSPC_BAY1	  0x48800000 /* Bay 1 */
#define PHY_ADRSPC_BAY2	  0x4D000000 /* Bay 2 */
#define PHY_ADRSPC_BAY3	  0x49000000 /* Bay 3 */
#define PHY_ADRSPC_BAY4	  0x4D800000 /* Bay 4 */
#define PHY_ADRSPC_BAY5	  0x49800000 /* Bay 5 */
#define PHY_ADRSPC_BAY6	  0x4E000000 /* Bay 6 */

/* 
 * PA are split among 2 two buses.
 *
 * We can find this information with "sh pci bridge <pa_bay>"
 */
static struct pa_bay_info pa_bays[MAX_PA_BAYS] = {
   { -1  , -1  ,   -1, PHY_ADRSPC_BAY0 },    /* no bridge for PA 0 */
   { 0x01, 0x04, 0x05, PHY_ADRSPC_BAY1 },    /* PA 1 */
   { 0x01, 0x0a, 0x0b, PHY_ADRSPC_BAY2 },    /* PA 2 */
   { 0x02, 0x04, 0x06, PHY_ADRSPC_BAY3 },    /* PA 3 */
   { 0x02, 0x0a, 0x0c, PHY_ADRSPC_BAY4 },    /* PA 4 */
   { 0x03, 0x04, 0x07, PHY_ADRSPC_BAY5 },    /* PA 5 */
   { 0x03, 0x0a, 0x0d, PHY_ADRSPC_BAY6 },    /* PA 6 */
};

/* Get a PA bay information */
struct pa_bay_info *c7200_get_pa_bay_info(u_int pa_bay)
{
   if (pa_bay >= MAX_PA_BAYS) {
      fprintf(stderr,"C7200: no info for PA Bay %u\n",pa_bay);
      return NULL;
   }

   return(&pa_bays[pa_bay]);
}

/* Get PCI bus ID for a PA */
int c7200_get_pa_bus(u_int pa_bay)
{
   if (pa_bay >= MAX_PA_BAYS) {
      fprintf(stderr,"C7200: no PCI bus for PA Bay %u\n",pa_bay);
      return(-1);
   }

   return(pa_bays[pa_bay].pci_secondary_bus);
}

/* Get MMIO address for a PA */
int c7200_get_mmio_addr(u_int pa_bay,m_uint64_t *addr)
{
   if (pa_bay >= MAX_PA_BAYS) {
      fprintf(stderr,"C7200: no PCI bus for PA Bay %u\n",pa_bay);
      return(-1);
   }

   *addr = pa_bays[pa_bay].phys_addr;
   return(0);
}
