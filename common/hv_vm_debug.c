/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Hypervisor routines for VM debugging.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "device.h"
#include "dev_c7200.h"
#include "dev_vtty.h"
#include "utils.h"
#include "registry.h"
#include "hypervisor.h"

/* Show CPU registers */
static int cmd_show_cpu_regs(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   cpu_gen_t *cpu;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   if ((cpu = cpu_group_find_id(vm->cpu_group,atoi(argv[1]))) != NULL)
      cpu->reg_dump(cpu);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Show CPU MMU info */
static int cmd_show_cpu_mmu(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   cpu_gen_t *cpu;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   if ((cpu = cpu_group_find_id(vm->cpu_group,atoi(argv[1]))) != NULL)
      cpu->mmu_dump(cpu);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Set a CPU register */
static int cmd_set_cpu_reg(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   cpu_gen_t *cpu;
   int reg_index;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   cpu = cpu_group_find_id(vm->cpu_group,atoi(argv[1]));
   reg_index = atoi(argv[2]);
                           
   if (!cpu || (reg_index < 1)) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_BAD_OBJ,1,"Bad CPU or register");
      return(-1);
   }

   /* Set register value */
   cpu->reg_set(cpu,reg_index,strtoull(argv[3],NULL,0));

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Add a breakpoint */
static int cmd_add_cpu_breakpoint(hypervisor_conn_t *conn,
                                  int argc,char *argv[])
{
   vm_instance_t *vm;
   cpu_gen_t *cpu;
   m_uint64_t addr;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   if (!(cpu = cpu_group_find_id(vm->cpu_group,atoi(argv[1])))) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_BAD_OBJ,1,"Bad CPU");
      return(-1);
   }

   addr = strtoull(argv[2],NULL,0);
   cpu->add_breakpoint(cpu,addr);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Remove a breakpoint */
static int cmd_remove_cpu_breakpoint(hypervisor_conn_t *conn,
                                     int argc,char *argv[])
{
   vm_instance_t *vm;
   cpu_gen_t *cpu;
   m_uint64_t addr;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   if (!(cpu = cpu_group_find_id(vm->cpu_group,atoi(argv[1])))) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_BAD_OBJ,1,"Bad CPU");
      return(-1);
   }

   addr = strtoull(argv[2],NULL,0);
   cpu->remove_breakpoint(cpu,addr);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Write a 32-bit memory word in physical memory */
static int cmd_pmem_w32(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   m_uint64_t addr;
   m_uint32_t value;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   /* Write word */
   addr  = strtoull(argv[2],NULL,0);
   value = strtoul(argv[3],NULL,0);
   physmem_copy_u32_to_vm(vm,addr,value);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Read a 32-bit memory word */
static int cmd_pmem_r32(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   m_uint64_t addr;
   m_uint32_t value;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   /* Write word */
   addr  = strtoull(argv[2],NULL,0);
   value = physmem_copy_u32_from_vm(vm,addr);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"0x%8.8x",value);
   return(0);
}

/* Write a 16-bit memory word */
static int cmd_pmem_w16(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   m_uint64_t addr;
   m_uint16_t value;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   /* Write word */
   addr  = strtoull(argv[2],NULL,0);
   value = strtoul(argv[3],NULL,0);
   physmem_copy_u16_to_vm(vm,addr,value);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Read a 16-bit memory word */
static int cmd_pmem_r16(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   m_uint64_t addr;
   m_uint16_t value;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   /* Write word */
   addr  = strtoull(argv[2],NULL,0);
   value = physmem_copy_u16_from_vm(vm,addr);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"0x%4.4x",value);
   return(0);
}

/* VM debug commands */
static hypervisor_cmd_t vm_cmd_array[] = {
   { "show_cpu_regs", 2, 2, cmd_show_cpu_regs, NULL },
   { "show_cpu_mmu", 2, 2, cmd_show_cpu_mmu, NULL },
   { "set_cpu_reg", 4, 4, cmd_set_cpu_reg, NULL },
   { "add_cpu_breakpoint", 3, 3, cmd_add_cpu_breakpoint, NULL },
   { "remove_cpu_breakpoint", 3, 3, cmd_remove_cpu_breakpoint, NULL },
   { "pmem_w32", 4, 4, cmd_pmem_w32, NULL },
   { "pmem_r32", 3, 3, cmd_pmem_r32, NULL },
   { "pmem_w16", 4, 4, cmd_pmem_w16, NULL },
   { "pmem_r16", 3, 3, cmd_pmem_r16, NULL },
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor VM debugging initialization */
int hypervisor_vm_debug_init(void)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module("vm_debug",NULL);
   assert(module != NULL);

   hypervisor_register_cmd_array(module,vm_cmd_array);
   return(0);
}
