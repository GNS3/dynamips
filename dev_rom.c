/*
 * Cisco C7200 (Predator) ROM emulation
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
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

/* Embedded ROM */
static m_uint8_t microcode[] = {
#include "microcode_dump.inc"
};

/*
 * dev_rom_access()
 */
void *dev_rom_access(cpu_mips_t *cpu,struct vdevice *dev,
                     m_uint32_t offset,u_int op_size,u_int op_type,
                     m_uint64_t *data)
{
   if (op_type == MTS_WRITE) {
      m_log("ROM","write attempt at address 0x%llx (data=0x%llx)\n",
            dev->phys_addr+offset,*data);
      return NULL;
   }

   if (offset >= sizeof(microcode)) {
      *data = 0;
      return NULL;
   }

   return((void *)(microcode + offset));
}

/* dev_rom_init() */
int dev_rom_init(cpu_group_t *cpu_group,m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev;

   if (!(dev = dev_create("rom"))) {
      fprintf(stderr,"ROM: unable to create device.\n");
      return(-1);
   }

   dev->phys_addr = paddr;
   dev->phys_len  = len;
   dev->handler   = dev_rom_access;

   /* Map this device to all CPU */
   cpu_group_bind_device(cpu_group,dev);
   return(0);
}
