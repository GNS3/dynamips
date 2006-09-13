/*
 * Cisco 7200 (Predator) simulation platform.
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

#include "mips64.h"
#include "dynamips.h"
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

/* Create a C7200 instance */
static int cmd_create(hypervisor_conn_t *conn,int argc,char *argv[])
{
   c7200_t *router;

   if (!(router = c7200_create_instance(argv[0],atoi(argv[1])))) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create C7200 instance '%s'",
                            argv[0]);
      return(-1);
   }

   router->vm->vtty_con_type = VTTY_TYPE_NONE;
   router->vm->vtty_aux_type = VTTY_TYPE_NONE;
   
   vm_release(router->vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"C7200 '%s' created",argv[0]);
   return(0);
}

/* Delete a C7200 instance */
static int cmd_delete(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int res;

   res = c7200_delete_instance(argv[0]);

   if (res == 1) {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,"C7200 '%s' deleted",argv[0]);
   } else {
      hypervisor_send_reply(conn,HSC_ERR_DELETE,1,
                            "unable to delete C7200 '%s'",argv[0]);
   }

   return(res);
}

/* Set the NPE type */
static int cmd_set_npe(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
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

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
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

/* Set the base MAC address for the chassis */
static int cmd_set_mac_addr(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
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

/* Start a C7200 instance */
static int cmd_start(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   c7200_t *router;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);

   if (router->vm->vtty_con_type == VTTY_TYPE_NONE) {
      hypervisor_send_reply(conn,HSC_INFO_MSG,0,
                            "Warning: no console port defined for "
                            "C7200 '%s'",argv[0]);
   }

   if (c7200_init_instance(router) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_START,1,
                            "unable to start instance '%s'",
                            argv[0]);
      return(-1);
   }
   
   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"C7200 '%s' started",argv[0]);
   return(0);
}

/* Stop a C7200 instance */
static int cmd_stop(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   c7200_t *router;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);

   if (c7200_stop_instance(router) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_STOP,1,
                            "unable to stop instance '%s'",
                            argv[0]);
      return(-1);
   }
   
   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"C7200 '%s' stopped",argv[0]);
   return(0);
}

/* Show PA bindings */
static int cmd_pa_bindings(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   c7200_t *router;
   char *pa_type;
   int i;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);

   for(i=0;i<C7200_MAX_PA_BAYS;i++) {
      pa_type = c7200_pa_get_type(router,i);
      if (pa_type)
         hypervisor_send_reply(conn,HSC_INFO_MSG,0,"%u: %s",i,pa_type);
   }
   
   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Show PA NIO bindings */
static int cmd_pa_nio_bindings(hypervisor_conn_t *conn,int argc,char *argv[])
{
   struct c7200_nio_binding *nb;
   struct c7200_pa_bay *bay;
   vm_instance_t *vm;
   c7200_t *router;
   u_int pa_bay;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);
   pa_bay = atoi(argv[1]);

   if (!(bay = c7200_pa_get_info(router,pa_bay))) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_UNK_OBJ,1,"Invalid slot %u",pa_bay);
      return(-1);
   }

   for(nb=bay->nio_list;nb;nb=nb->next)
      hypervisor_send_reply(conn,HSC_INFO_MSG,0,"%u: %s",
                            nb->port_id,nb->nio->name);
   
   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Add a PA binding for the specified slot */
static int cmd_add_pa_binding(hypervisor_conn_t *conn,int argc,char *argv[])
{   
   vm_instance_t *vm;
   c7200_t *router;
   u_int pa_bay;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);
   pa_bay = atoi(argv[1]);

   if (c7200_pa_add_binding(router,argv[2],pa_bay) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "C7200 %s: unable to add PA binding for slot %u",
                            argv[0],pa_bay);
      return(-1);
   }

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Remove a PA binding for the specified slot */
static int cmd_remove_pa_binding(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   c7200_t *router;
   u_int pa_bay;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);
   pa_bay = atoi(argv[1]);

   if (c7200_pa_remove_binding(router,pa_bay) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "C7200 %s: unable to remove PA binding for "
                            "slot %u",argv[0],pa_bay);
      return(-1);
   }

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Add a NIO binding to the specified slot/port */
static int cmd_add_nio_binding(hypervisor_conn_t *conn,int argc,char *argv[])
{  
   u_int pa_bay,port_id;
   vm_instance_t *vm;
   c7200_t *router;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);

   pa_bay = atoi(argv[1]);
   port_id = atoi(argv[2]);

   if (c7200_pa_add_nio_binding(router,pa_bay,port_id,argv[3]) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "C7200 %s: unable to add NIO binding for "
                            "interface %u/%u",argv[0],pa_bay,port_id);
      return(-1);
   }

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Remove a NIO binding from the specified slot/port */
static int cmd_remove_nio_binding(hypervisor_conn_t *conn,
                                  int argc,char *argv[])
{
   u_int pa_bay,port_id;
   vm_instance_t *vm;
   c7200_t *router;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);

   pa_bay = atoi(argv[1]);
   port_id = atoi(argv[2]);

   if (c7200_pa_remove_nio_binding(router,pa_bay,port_id) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "C7200 %s: unable to remove NIO binding for "
                            "interface %u/%u",argv[0],pa_bay,port_id);
      return(-1);
   }

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Enable NIO of the specified slot/port */
static int cmd_pa_enable_nio(hypervisor_conn_t *conn,int argc,char *argv[])
{  
   u_int pa_bay,port_id;
   vm_instance_t *vm;
   c7200_t *router;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);

   pa_bay = atoi(argv[1]);
   port_id = atoi(argv[2]);

   if (c7200_pa_enable_nio(router,pa_bay,port_id) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "C7200 %s: unable to enable NIO for "
                            "interface %u/%u",argv[0],pa_bay,port_id);
      return(-1);
   }

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Disable NIO of the specified slot/port */
static int cmd_pa_disable_nio(hypervisor_conn_t *conn,int argc,char *argv[])
{  
   u_int pa_bay,port_id;
   vm_instance_t *vm;
   c7200_t *router;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);

   pa_bay = atoi(argv[1]);
   port_id = atoi(argv[2]);

   if (c7200_pa_disable_nio(router,pa_bay,port_id) == -1) {
      vm_release(vm);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "C7200 %s: unable to unset NIO for "
                            "interface %u/%u",
                            argv[0],pa_bay,port_id);
      return(-1);
   }

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Initialize a PA while the router is running */
static int cmd_pa_init_online(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   c7200_t *router;
   u_int pa_bay;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);

   pa_bay = atoi(argv[1]);
   c7200_pa_init_online(router,pa_bay);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Stop a PA while the router is running */
static int cmd_pa_stop_online(hypervisor_conn_t *conn,int argc,char *argv[])
{     
   vm_instance_t *vm;
   c7200_t *router;
   u_int pa_bay;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
      return(-1);

   router = VM_C7200(vm);

   pa_bay = atoi(argv[1]);
   c7200_pa_stop_online(router,pa_bay);

   vm_release(vm);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Show C7200 hardware */
static int cmd_show_hardware(hypervisor_conn_t *conn,int argc,char *argv[])
{
   vm_instance_t *vm;
   c7200_t *router;

   if (!(vm = hypervisor_find_vm(conn,argv[0],VM_TYPE_C7200)))
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

   if (vm->type == VM_TYPE_C7200)
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
   { "create", 2, 2, cmd_create, NULL },
   { "delete", 1, 1, cmd_delete, NULL },
   { "set_npe", 2, 2, cmd_set_npe, NULL },
   { "set_midplane", 2, 2, cmd_set_midplane, NULL },
   { "set_mac_addr", 2, 2, cmd_set_mac_addr, NULL },
   { "start", 1, 1, cmd_start, NULL },
   { "stop", 1, 1, cmd_stop, NULL },
   { "pa_bindings", 1, 1, cmd_pa_bindings, NULL },
   { "pa_nio_bindings", 2, 2, cmd_pa_nio_bindings, NULL },
   { "add_pa_binding", 3, 3, cmd_add_pa_binding, NULL },
   { "remove_pa_binding", 2, 2, cmd_remove_pa_binding, NULL },
   { "add_nio_binding", 4, 4, cmd_add_nio_binding, NULL },
   { "remove_nio_binding", 3, 3, cmd_remove_nio_binding, NULL },
   { "pa_enable_nio", 3, 3, cmd_pa_enable_nio, NULL },
   { "pa_disable_nio", 3, 3, cmd_pa_disable_nio, NULL },
   { "pa_init_online", 2, 2, cmd_pa_init_online, NULL },
   { "pa_stop_online", 2, 2, cmd_pa_stop_online, NULL },
   { "show_hardware", 1, 1, cmd_show_hardware, NULL },
   { "list", 0, 0, cmd_c7200_list, NULL },
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor C7200 initialization */
int hypervisor_c7200_init(void)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module("c7200");
   assert(module != NULL);

   hypervisor_register_cmd_array(module,c7200_cmd_array);
   return(0);
}
