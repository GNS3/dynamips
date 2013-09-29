/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Hypervisor ATM switch routines.
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

#include "utils.h"
#include "net.h"
#include "atm_bridge.h"
#include "crc.h"
#include "net_io.h"
#include "registry.h"
#include "hypervisor.h"

/* Create a new ATM bridge object */
static int cmd_create(hypervisor_conn_t *conn,int argc,char *argv[])
{
   atm_bridge_t *t;

   if (!(t = atm_bridge_create(argv[0]))) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create ATM bridge '%s'",
                            argv[0]);
      return(-1);
   }

   atm_bridge_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"ATM bridge '%s' created",argv[0]);
   return(0);
}

/* Rename an ATM bridge */
static int cmd_rename(hypervisor_conn_t *conn,int argc,char *argv[])
{
   atm_bridge_t *t;
   char *newname;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ATM_BRIDGE)))
      return(-1);

   if (registry_exists(argv[1],OBJ_TYPE_ATM_BRIDGE)) {
      atm_bridge_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_RENAME,1,
                            "unable to rename ATM bridge '%s', '%s' already exists",
                            argv[0],argv[1]);
      return(-1);
   }

   if(!(newname = strdup(argv[1]))) {
      atm_bridge_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_RENAME,1,
                            "unable to rename ATM bridge '%s', out of memory",
                            argv[0]);
      return(-1);
   }

   if (registry_rename(argv[0],newname,OBJ_TYPE_ATM_BRIDGE)) {
      free(newname);
      atm_bridge_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_RENAME,1,
                            "unable to rename ATM bridge '%s'",
                            argv[0]);
      return(-1);
   }

   free(t->name);
   t->name = newname;

   atm_bridge_release(argv[1]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"ATM bridge '%s' renamed to '%s'",argv[0],argv[1]);
   return(0);
}

/* Delete an ATM bridge */
static int cmd_delete(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int res;

   res = atm_bridge_delete(argv[0]);

   if (res == 1) {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,
                            "ATM bridge '%s' deleted",argv[0]);
   } else {
      hypervisor_send_reply(conn,HSC_ERR_DELETE,1,
                            "unable to delete ATM bridge '%s'",argv[0]);
   }

   return(res);
}

/* 
 * Configure an ATM bridge
 *
 * Parameters: <atmbr_name> <eth_nio> <atm_nio> <vpi> <vci>
 */
static int cmd_configure(hypervisor_conn_t *conn,int argc,char *argv[])
{
   atm_bridge_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ATM_BRIDGE)))
      return(-1);

   /* create the connection */
   if (atm_bridge_configure(t,argv[1],argv[2],
                            atoi(argv[3]),atoi(argv[4])) == -1)
   {
      atm_bridge_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "unable to configure bridge");
      return(-1);
   }

   atm_bridge_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"ATM bridge configured");
   return(0);
}

/* 
 * Unconfigure a bridge
 */
static int cmd_unconfigure(hypervisor_conn_t *conn,int argc,char *argv[])
{
   atm_bridge_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ATM_BRIDGE)))
      return(-1);

   if (atm_bridge_unconfigure(t) == -1) {
      atm_bridge_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "unable to unconfigure bridge");
      return(-1);
   }

   atm_bridge_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"ATM bridge unconfigured");
   return(0);
}

/* Show info about a ATM bridge object */
static void cmd_show_list(registry_entry_t *entry,void *opt,int *err)
{
   hypervisor_conn_t *conn = opt;
   hypervisor_send_reply(conn,HSC_INFO_MSG,0,"%s",entry->name);
}

/* ATM bridge List */
static int cmd_list(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int err = 0;
   registry_foreach_type(OBJ_TYPE_ATM_BRIDGE,cmd_show_list,conn,&err);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* ATM Bridge commands */
static hypervisor_cmd_t atmbr_cmd_array[] = {
   { "create", 1, 1, cmd_create, NULL },
   { "rename", 2, 2, cmd_rename, NULL },
   { "delete", 1, 1, cmd_delete, NULL },
   { "configure", 5, 5, cmd_configure, NULL },
   { "unconfigure", 1, 1, cmd_unconfigure, NULL },
   { "list", 0, 0, cmd_list, NULL },
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor ATM bridge initialization */
int hypervisor_atm_bridge_init(void)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module("atm_bridge",NULL);
   assert(module != NULL);

   hypervisor_register_cmd_array(module,atmbr_cmd_array);
   return(0);
}
