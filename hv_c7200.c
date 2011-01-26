/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Hypervisor C7200 routines.
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
#include "dev_c7200.h"
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

/* Set the NPE type */
static int cmd_set_npe(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;

   if (!(vm = hypervisor_find_vm(conn,argv[0])))
      return(-1);

   if ((c7200_npe_set_type(VM_C7200(vm),argv[1])) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to set NPE type for router '%s'",
                            argv[0]);
      return(-1);
   }

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Set the Midplane type */
static int cmd_set_midplane(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;

   if (!(vm = hypervisor_find_vm(conn,argv[0])))
      return(-1);

   if ((c7200_midplane_set_type(VM_C7200(vm),argv[1])) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to set Midplane type for router '%s'",
                            argv[0]);
      return(-1);
   }

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

   if (( c7200_set_system_id(VM_C7200(vm),argv[1])) == -1) {
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

   if ((c7200_midplane_set_mac_addr(VM_C7200(vm),argv[1])) == -1) {
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

/* 
 * Set temperature for a DS1620 sensor.
 * This can be used to simulate environmental problems (overheat)
 */
static int cmd_set_temp_sensor(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   c7200_t *router;
   u_int index;
   int temp;

   if (!(vm = hypervisor_find_vm(conn,argv[0])))
      return(-1);

   router = VM_C7200(vm);

   index = atoi(argv[1]);
   temp = atoi(argv[2]);

   if (index < C7200_TEMP_SENSORS)
      ds1620_set_temp(&router->ds1620_sensors[index],temp);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* 
 * Set power supply status.
 * This can be used to simulate environmental problems (power loss)
 */
static int cmd_set_power_supply(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   c7200_t *router;
   u_int index;
   int status;

   if (!(vm = hypervisor_find_vm(conn,argv[0])))
      return(-1);

   router = VM_C7200(vm);

   index = atoi(argv[1]);
   status = atoi(argv[2]);

   if (status)
      router->ps_status |= 1 << index;
   else
      router->ps_status &= ~(1 << index);

   /* only 2 power supplies */
   router->ps_status &= 0x03;

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Show C7200 hardware */
static int cmd_show_hardware(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   c7200_t *router;

   if (!(vm = hypervisor_find_vm(conn,argv[0])))
      return(-1);

   router = VM_C7200(vm);
   c7200_show_hardware(router);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Show info about C7200 object */
static void cmd_show_c7200_list(registry_entry_t *entry,void *opt,int *err)
{
   hypervisor_conn_t *conn = opt;
   vm_instance_t *vm = entry->data;

   if (vm->platform == conn->cur_module->opt)
      hypervisor_send_reply(conn,HSC_INFO_MSG,0,"%s",entry->name);
}

/* C7200 List */
static int cmd_c7200_list(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int err = 0;
   registry_foreach_type(OBJ_TYPE_VM,cmd_show_c7200_list,conn,&err);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* C7200 commands */
static hypervisor_cmd_t c7200_cmd_array[] = {
   { "set_npe", 2, 2, cmd_set_npe, NULL },
   { "set_midplane", 2, 2, cmd_set_midplane, NULL },
   { "set_mac_addr", 2, 2, cmd_set_mac_addr, NULL },
   { "set_system_id", 2, 2, cmd_set_system_id, NULL },
   { "set_temp_sensor", 3, 3, cmd_set_temp_sensor, NULL },
   { "set_power_supply", 3, 3, cmd_set_power_supply, NULL },
   { "show_hardware", 1, 1, cmd_show_hardware, NULL },
   { "list", 0, 0, cmd_c7200_list, NULL },
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor C7200 initialization */
int hypervisor_c7200_init(vm_platform_t *platform)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module(platform->name,platform);
   assert(module != NULL);

   hypervisor_register_cmd_array(module,c7200_cmd_array);
   return(0);
}
