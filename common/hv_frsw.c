/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Hypervisor Frame-Relay switch routines.
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
#include "frame_relay.h"
#include "crc.h"
#include "net_io.h"
#include "registry.h"
#include "hypervisor.h"

/* Create a new FRSW object */
static int cmd_create(hypervisor_conn_t *conn,int argc,char *argv[])
{
   frsw_table_t *t;

   if (!(t = frsw_create_table(argv[0]))) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create frame-relay switch '%s'",
                            argv[0]);
      return(-1);
   }

   frsw_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"FRSW '%s' created",argv[0]);
   return(0);
}

/* Delete a Frame-Relay switch */
static int cmd_delete(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int res;

   res = frsw_delete(argv[0]);

   if (res == 1) {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,"FRSW '%s' deleted",argv[0]);
   } else {
      hypervisor_send_reply(conn,HSC_ERR_DELETE,1,
                            "unable to delete FRSW '%s'",argv[0]);
   }

   return(res);
}

/* 
 * Create a Virtual Circuit
 *
 * Parameters: <frsw_name> <input_nio> <input_dlci> <output_nio> <output_dlci>
 */
static int cmd_create_vc(hypervisor_conn_t *conn,int argc,char *argv[])
{
   frsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_FRSW)))
      return(-1);
   
   /* create the connection */
   if (frsw_create_vc(t,argv[1],atoi(argv[2]),argv[3],atoi(argv[4])) == -1) {
      frsw_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,"unable to create VC");
      return(-1);
   }

   frsw_release(argv[0]);   
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"VC created");
   return(0);
}

/*
 * Delete a Virtual Circuit
 *
 * Parameters: <frsw_name> <input_nio> <input_dlci> <output_nio> <output_dlci>
 */
static int cmd_delete_vc(hypervisor_conn_t *conn,int argc,char *argv[])
{
   frsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_FRSW)))
      return(-1);
   
   /* delete the connection */
   if (frsw_delete_vc(t,argv[1],atoi(argv[2]),argv[3],atoi(argv[4])) == -1) {
      frsw_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,"unable to delete VC");
      return(-1);
   }

   frsw_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"VC deleted");
   return(0);
}

/* Show info about a FRSW object */
static void cmd_show_list(registry_entry_t *entry,void *opt,int *err)
{
   hypervisor_conn_t *conn = opt;
   hypervisor_send_reply(conn,HSC_INFO_MSG,0,"%s",entry->name);
}

/* Frame-Relay switch List */
static int cmd_list(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int err = 0;
   registry_foreach_type(OBJ_TYPE_FRSW,cmd_show_list,conn,&err);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* FRSW commands */
static hypervisor_cmd_t frsw_cmd_array[] = {
   { "create", 1, 1, cmd_create, NULL },
   { "delete", 1, 1, cmd_delete, NULL },
   { "create_vc", 5, 5, cmd_create_vc, NULL },
   { "delete_vc", 5, 5, cmd_delete_vc, NULL },
   { "list", 0, 0, cmd_list, NULL },
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor Frame-Relay switch initialization */
int hypervisor_frsw_init(void)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module("frsw",NULL);
   assert(module != NULL);

   hypervisor_register_cmd_array(module,frsw_cmd_array);
   return(0);
}
