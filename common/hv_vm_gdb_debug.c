/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Added by : Sebastian 'topo' Muniz
 * Contact  : sebastianmuniz@gmail.com
 *
 * Hypervisor routines for VM debugging with GDB stub routines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
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

#include "gdb_server.h"

/* [GDB] Dump CPU registers */
static int hv_gdb_cmd_dump_cpu_regs(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   cpu_gen_t *cpu;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   if ((cpu = cpu_group_find_id(vm->cpu_group,atoi(argv[1]))) != NULL)
//       cpu->reg_dump(cpu);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Read memory by the specified length */
static int hv_gdb_cmd_read_mem(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   m_uint64_t addr;
   m_uint32_t value;
   m_uint32_t length;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   /* Read the specified ammount of memory */
   addr   = strtoull(argv[1], NULL, 0);
   length = strtoull(argv[2], NULL, 0);
   value  = 0x11223344; //physmem_copy_u32_from_vm(vm,addr);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"0x%8.8x",value);
   return(0);
}

/* Setup the GDB Stub listening port and other initial values */
static int gdb_debug_gdb_init(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   uint res;

   if (!(vm = hypervisor_find_object(conn,argv[0],OBJ_TYPE_VM)))
      return(-1);

   // Set the GDB listener port and initiate GDB Server
   vm->gdb_port = strtoul(argv[1], NULL, 0);   
   res = gdb_server_start_listener(vm);

   vm_release(vm);

   if (res < 0)
   {
      hypervisor_send_reply(conn,HSC_ERR_START,1,"Unable to start GDB server");
      return (-1);
   }

   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* VM GDB debug commands */
static hypervisor_cmd_t vm_cmd_array[] = {
   /*{ "show_cpu_regs", 2, 2, cmd_show_cpu_regs, NULL },
   { "show_cpu_mmu", 2, 2, cmd_show_cpu_mmu, NULL },
   { "set_cpu_reg", 4, 4, cmd_set_cpu_reg, NULL },
   { "add_cpu_breakpoint", 3, 3, cmd_add_cpu_breakpoint, NULL },
   { "remove_cpu_breakpoint", 3, 3, cmd_remove_cpu_breakpoint, NULL },
   { "pmem_w32", 4, 4, cmd_pmem_w32, NULL },
   { "pmem_r32", 3, 3, cmd_pmem_r32, NULL },
   { "pmem_w16", 4, 4, cmd_pmem_w16, NULL },
   { "pmem_r16", 3, 3, cmd_pmem_r16, NULL },
   */
   { "gdb_init",        2, 2, gdb_debug_gdb_init, NULL},
   { "gdb_read_mem",    3, 3, hv_gdb_cmd_read_mem, NULL},
   { "gdb_read_regs",   2, 2, hv_gdb_cmd_dump_cpu_regs, NULL},
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor VM GDB debugging initialization */
int hypervisor_vm_gdb_debug_init(void)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module("vm_gdb_debug",NULL);
   assert(module != NULL);

   hypervisor_register_cmd_array(module,vm_cmd_array);
   return(0);
}
