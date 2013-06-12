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
#include "atm.h"
#include "crc.h"
#include "net_io.h"
#include "registry.h"
#include "hypervisor.h"

/* Create a new ATMSW object */
static int cmd_create(hypervisor_conn_t *conn,int argc,char *argv[])
{
   atmsw_table_t *t;

   if (!(t = atmsw_create_table(argv[0]))) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create ATM switch '%s'",
                            argv[0]);
      return(-1);
   }

   atmsw_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"ATMSW '%s' created",argv[0]);
   return(0);
}

/* Delete an ATM switch */
static int cmd_delete(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int res;

   res = atmsw_delete(argv[0]);

   if (res == 1) {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,"ATMSW '%s' deleted",argv[0]);
   } else {
      hypervisor_send_reply(conn,HSC_ERR_DELETE,1,
                            "unable to delete ATMSW '%s'",argv[0]);
   }

   return(res);
}

/* 
 * Create a Virtual Path Connection
 *
 * Parameters: <atmsw_name> <input_nio> <input_vpi> <output_nio> <output_vpi>
 */
static int cmd_create_vpc(hypervisor_conn_t *conn,int argc,char *argv[])
{
   atmsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ATMSW)))
      return(-1);

   /* create the connection */
   if (atmsw_create_vpc(t,argv[1],atoi(argv[2]),argv[3],atoi(argv[4])) == -1) {
      atmsw_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,"unable to create VPC");
      return(-1);
   }

   atmsw_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"VPC created");
   return(0);
}

/* 
 * Delete a Virtual Path Connection
 *
 * Parameters: <atmsw_name> <input_nio> <input_vpi> <output_nio> <output_vpi>
 */
static int cmd_delete_vpc(hypervisor_conn_t *conn,int argc,char *argv[])
{
   atmsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ATMSW)))
      return(-1);

   /* delete the connection */
   if (atmsw_delete_vpc(t,argv[1],atoi(argv[2]),argv[3],atoi(argv[4])) == -1) {
      atmsw_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,"unable to delete VPC");
      return(-1);
   }

   atmsw_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"VPC deleted");
   return(0);
}

/* 
 * Create a Virtual Circuit Connection
 *
 * Parameters: <atmsw_name> <input_nio> <input_vpi> <input_vci>
 *             <output_nio> <output_vpi> <output_vci>
 */
static int cmd_create_vcc(hypervisor_conn_t *conn,int argc,char *argv[])
{
   atmsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ATMSW)))
      return(-1);
   
   /* create the connection */
   if (atmsw_create_vcc(t,argv[1],atoi(argv[2]),atoi(argv[3]),
                        argv[4],atoi(argv[5]),atoi(argv[6])) == -1) 
   {
      atmsw_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,"unable to create VCC");
      return(-1);
   }

   atmsw_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"VCC created");
   return(0);
}

/* 
 * Delete a Virtual Circuit Connection
 *
 * Parameters: <atmsw_name> <input_nio> <input_vpi> <input_vci>
 *             <output_nio> <output_vpi> <output_vci>
 */
static int cmd_delete_vcc(hypervisor_conn_t *conn,int argc,char *argv[])
{
   atmsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ATMSW)))
      return(-1);
   
   /* create the connection */
   if (atmsw_delete_vcc(t,argv[1],atoi(argv[2]),atoi(argv[3]),
                        argv[4],atoi(argv[5]),atoi(argv[6])) == -1) 
   {
      atmsw_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,"unable to delete VCC");
      return(-1);
   }

   atmsw_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"VCC deleted");
   return(0);
}

/* Show info about a ATM switch object */
static void cmd_show_list(registry_entry_t *entry,void *opt,int *err)
{
   hypervisor_conn_t *conn = opt;
   hypervisor_send_reply(conn,HSC_INFO_MSG,0,"%s",entry->name);
}

/* ATM switch List */
static int cmd_list(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int err = 0;
   registry_foreach_type(OBJ_TYPE_ATMSW,cmd_show_list,conn,&err);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* ATMSW commands */
static hypervisor_cmd_t atmsw_cmd_array[] = {
   { "create", 1, 1, cmd_create, NULL },
   { "delete", 1, 1, cmd_delete, NULL },
   { "create_vpc", 5, 5, cmd_create_vpc, NULL },
   { "delete_vpc", 5, 5, cmd_delete_vpc, NULL },
   { "create_vcc", 7, 7, cmd_create_vcc, NULL },
   { "delete_vcc", 7, 7, cmd_delete_vcc, NULL },
   { "list", 0, 0, cmd_list, NULL },
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor ATM switch initialization */
int hypervisor_atmsw_init(void)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module("atmsw",NULL);
   assert(module != NULL);

   hypervisor_register_cmd_array(module,atmsw_cmd_array);
   return(0);
}
