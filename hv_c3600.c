/*
 * Cisco 3600 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Hypervisor C3600 routines.
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
#include "device.h"
#include "dev_c3600.h"
#include "dev_vtty.h"
#include "utils.h"
#include "net.h"
#include "atm.h"
#include "frame_relay.h"
#include "crc.h"
#include "net_io.h"
#include "net_io_bridge.h"
#ifdef GEN_ETH
#include "gen_eth.h"
#endif
#include "registry.h"
#include "hypervisor.h"

/* Set the chassis type */
static int cmd_set_chassis(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;

   if (!(vm = hypervisor_find_vm(conn,argv[0])))
      return(-1);

   if ((c3600_chassis_set_type(VM_C3600(vm),argv[1])) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to set Chassis type for router '%s'",
                            argv[0]);
      return(-1);
   }

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Set the I/O mem size */
static int cmd_set_iomem(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;

   if (!(vm = hypervisor_find_vm(conn,argv[0])))
      return(-1);

   vm->nm_iomem_size = 0x8000 | atoi(argv[1]);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Set the system id */
static int cmd_set_system_id(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;

   if (!(vm = hypervisor_find_vm(conn,argv[0])))
      return(-1);

   if (( c3600_set_system_id(VM_C3600(vm),argv[1])) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to set the system id for router '%s'",
                            argv[0]);
      return(-1);
   }

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Set the base MAC address for the chassis */
static int cmd_set_mac_addr(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;

   if (!(vm = hypervisor_find_vm(conn,argv[0])))
      return(-1);

   if ((c3600_chassis_set_mac_addr(VM_C3600(vm),argv[1])) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to set MAC address for router '%s'",
                            argv[0]);
      return(-1);
   }

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Show C3600 hardware */
static int cmd_show_hardware(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   c3600_t *router;

   if (!(vm = hypervisor_find_vm(conn,argv[0])))
      return(-1);

   router = VM_C3600(vm);
   c3600_show_hardware(router);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Show info about C3600 object */
static void cmd_show_c3600_list(registry_entry_t *entry,void *opt,int *err)
{
   hypervisor_conn_t *conn = opt;
   vm_instance_t *vm = entry->data;

   if (vm->platform == conn->cur_module->opt)
      hypervisor_send_reply(conn,HSC_INFO_MSG,0,"%s",entry->name);
}

/* C3600 List */
static int cmd_c3600_list(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int err = 0;
   registry_foreach_type(OBJ_TYPE_VM,cmd_show_c3600_list,conn,&err);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* C3600 commands */
static hypervisor_cmd_t c3600_cmd_array[] = {
   { "set_chassis", 2, 2, cmd_set_chassis, NULL },
   { "set_iomem", 2, 2, cmd_set_iomem, NULL },
   { "set_mac_addr", 2, 2, cmd_set_mac_addr, NULL },
   { "set_system_id", 2, 2, cmd_set_system_id, NULL },
   { "show_hardware", 1, 1, cmd_show_hardware, NULL },
   { "list", 0, 0, cmd_c3600_list, NULL },
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor C3600 initialization */
int hypervisor_c3600_init(vm_platform_t *platform)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module(platform->name,platform);
   assert(module != NULL);

   hypervisor_register_cmd_array(module,c3600_cmd_array);
   return(0);
}
