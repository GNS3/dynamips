/*
 * Cisco C7200 (Predator) Remote Control Module.
 * Copyright (C) 2002006 Christophe Fillot.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>

#include "utils.h"
#include "mips64.h"
#include "cp0.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_c7200.h"
#include "dev_c7200_bay.h"

/*
 * dev_remote_control_access()
 */
void *dev_remote_control_access(cpu_mips_t *cpu,struct vdevice *dev,
                                m_uint32_t offset,u_int op_size,u_int op_type,
                                m_uint64_t *data)
{
   struct c7200_router *router = dev->priv_data;
   static char buffer[512];
   static u_int buf_pos = 0;
   size_t len;

   if (op_type == MTS_READ)
      *data = 0;

   switch(offset) {
      /* IOSEMU ID */
      case 0x000: 
         if (op_type == MTS_READ)
            *data = IOSEMU_ID;
         break;

      /* CPU ID */
      case 0x004: 
         if (op_type == MTS_READ)
            *data = 0;   /* XXX */
         break;

      /* Display CPU registers */
      case 0x008:
         if (op_type == MTS_WRITE)
            mips64_dump_regs(cpu);
         break;

      /* Display CPU TLB */
      case 0x00c:
         if (op_type == MTS_WRITE)
            tlb_dump(cpu);
         break;

      /* Instruction trace */
      case 0x010:
         if (op_type == MTS_WRITE)
            insn_itrace = *data;
         else
            *data = insn_itrace;
         break;

      /* RAM size */
      case 0x014: 
         if (op_type == MTS_READ)
            *data = router->ram_size;
         break;

      /* ROM size */
      case 0x018: 
         if (op_type == MTS_READ)
            *data = router->rom_size;
         break;

      /* NVRAM size */
      case 0x01c: 
         if (op_type == MTS_READ)
            *data = router->nvram_size;
         break;             

      /* Config Register */
      case 0x020:
         if (op_type == MTS_READ)
            *data = router->conf_reg;
         break;

      /* ELF entry point */
      case 0x024: 
         if (op_type == MTS_READ)
            *data = router->ios_entry_point;
         break;      

      /* Restart IOS Image */
      case 0x028:
         /* not implemented */
         break;

      /* Debugging/Log message: /!\ physical address */
      case 0x02c:
         if (op_type == MTS_WRITE) {
            len = physmem_strlen(cpu,*data);
            if (len < sizeof(buffer)) {
               physmem_copy_from_vm(cpu,buffer,*data,len+1);
               m_log("ROM",buffer);
            }
         }
         break;

      /* Buffering */
      case 0x030:
         if (buf_pos < (sizeof(buffer)-1)) {
            buffer[buf_pos++] = *data & 0xFF;
            buffer[buf_pos] = 0;

            if (buffer[buf_pos-1] == '\n') {
               m_log("ROM","%s",buffer);
               buf_pos = 0;
            }
         } else
            buf_pos = 0;
         break;
   }

   return NULL;
}

/* remote control device */
int dev_create_remote_control(c7200_t *router,m_uint64_t paddr,m_uint32_t len)
{
   struct vdevice *dev;

   if (!(dev = dev_create("remote_ctrl"))) {
      fprintf(stderr,"remote_ctrl: unable to create device.\n");
      return(-1);
   }

   dev->phys_addr = paddr;
   dev->phys_len  = len;
   dev->handler   = dev_remote_control_access;
   dev->priv_data = router;

   cpu_group_bind_device(router->cpu_group,dev);
   return(0);
}
