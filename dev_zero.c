/*
 * Cisco C7200 (Predator) Simulation Platform.
 * Copyright (C) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Zeroed memory zone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#include "atm.h"
#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

/*
 * dev_zero_access()
 */
void *dev_zero_access(cpu_mips_t *cpu,struct vdevice *dev,
                      m_uint32_t offset,u_int op_size,u_int op_type,
                      m_uint64_t *data)
{
   if (op_type == MTS_READ)
      *data = 0;

   return NULL;
}

/* dev_zero_init() */
int dev_zero_init(cpu_group_t *cpu_group,char *name,
                  m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev;

   if (!(dev = dev_create(name))) {
      fprintf(stderr,"ZERO: unable to create device.\n");
      return(-1);
   }

   dev->phys_addr = paddr;
   dev->phys_len  = len;
   dev->handler   = dev_zero_access;

   /* Map this device to all CPU */
   cpu_group_bind_device(cpu_group,dev);
   return(0);
}
