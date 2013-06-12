/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Hypervisor NIO bridge routines.
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

#include "utils.h"
#include "net.h"
#include "crc.h"
#include "net_io.h"
#include "net_io_bridge.h"
#include "registry.h"
#include "hypervisor.h"

/* Create a new NIO bridge */
static int cmd_create(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_bridge_t *t;

   if (!(t = netio_bridge_create(argv[0]))) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create NIO bridge '%s'",
                            argv[0]);
      return(-1);
   }

   netio_bridge_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO bridge '%s' created",argv[0]);
   return(0);
}

/* Delete an NIO bridge */
static int cmd_delete(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int res;

   res = netio_bridge_delete(argv[0]);

   if (res == 1) {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO bridge '%s' deleted",
                            argv[0]);
   } else {
      hypervisor_send_reply(conn,HSC_ERR_DELETE,1,
                            "unable to delete NIO bridge '%s'",argv[0]);
   }

   return(res);
}

/* 
 * Add a NIO to a bridge
 *
 * Parameters: <bridge_name> <nio_name>
 */
static int cmd_add_nio(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_bridge_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO_BRIDGE)))
      return(-1);
   
   if (netio_bridge_add_netio(t,argv[1]) == -1) {
      netio_bridge_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "unable to bind NIO '%s' to bridge '%s'",
                            argv[1],argv[0]);
      return(-1);
   }

   netio_bridge_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' bound.",argv[1]);
   return(0);
}

/* 
 * Remove a NIO from a bridge
 *
 * Parameters: <bridge_name> <nio_name>
 */
static int cmd_remove_nio(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_bridge_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO_BRIDGE)))
      return(-1);
   
   if (netio_bridge_remove_netio(t,argv[1]) == -1) {
      netio_bridge_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "unable to bind NIO '%s' to bridge '%s'",
                            argv[1],argv[0]);
      return(-1);
   }

   netio_bridge_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' unbound.",argv[1]);
   return(0);
}

/* Show info about a NIO bridge object */
static void cmd_show_list(registry_entry_t *entry,void *opt,int *err)
{
   hypervisor_conn_t *conn = opt;
   hypervisor_send_reply(conn,HSC_INFO_MSG,0,"%s",entry->name);
}

/* Bridge switch List */
static int cmd_list(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int err = 0;
   registry_foreach_type(OBJ_TYPE_NIO_BRIDGE,cmd_show_list,conn,&err);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* NIO bridge commands */
static hypervisor_cmd_t nio_bridge_cmd_array[] = {
   { "create", 1, 1, cmd_create, NULL },
   { "delete", 1, 1, cmd_delete, NULL },
   { "add_nio", 2, 2, cmd_add_nio, NULL },
   { "remove_nio", 2, 2, cmd_remove_nio, NULL },
   { "list", 0, 0, cmd_list, NULL },
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor NIO bridge initialization */
int hypervisor_nio_bridge_init(void)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module("nio_bridge",NULL);
   assert(module != NULL);

   hypervisor_register_cmd_array(module,nio_bridge_cmd_array);
   return(0);
}
