/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Hypervisor Ethernet switch routines.
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
#include "eth_switch.h"
#include "crc.h"
#include "net_io.h"
#include "registry.h"
#include "hypervisor.h"

/* Create a new Ethernet switch object */
static int cmd_create(hypervisor_conn_t *conn,int argc,char *argv[])
{
   ethsw_table_t *t;

   if (!(t = ethsw_create(argv[0]))) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create Ethernet switch '%s'",
                            argv[0]);
      return(-1);
   }

   ethsw_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"ETHSW '%s' created",argv[0]);
   return(0);
}

/* Delete an Ethernet switch */
static int cmd_delete(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int res;

   res = ethsw_delete(argv[0]);

   if (res == 1) {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,"ETHSW '%s' deleted",argv[0]);
   } else {
      hypervisor_send_reply(conn,HSC_ERR_DELETE,1,
                            "unable to delete ETHSW '%s'",argv[0]);
   }

   return(res);
}

/* 
 * Add a NIO to an Ethernet switch.
 *
 * Parameters: <ethsw_name> <nio_name>
 */
static int cmd_add_nio(hypervisor_conn_t *conn,int argc,char *argv[])
{
   ethsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ETHSW)))
      return(-1);
   
   if (ethsw_add_netio(t,argv[1]) == -1) {
      ethsw_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "unable to bind NIO '%s' to switch '%s'",
                            argv[1],argv[0]);
      return(-1);
   }

   ethsw_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' bound.",argv[1]);
   return(0);
}

/* 
 * Remove a NIO from an Ethernet switch
 *
 * Parameters: <ethsw_name> <nio_name>
 */
static int cmd_remove_nio(hypervisor_conn_t *conn,int argc,char *argv[])
{
   ethsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ETHSW)))
      return(-1);
   
   if (ethsw_remove_netio(t,argv[1]) == -1) {
      ethsw_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "unable to bind NIO '%s' to switch '%s'",
                            argv[1],argv[0]);
      return(-1);
   }

   ethsw_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' unbound.",argv[1]);
   return(0);
}

/* 
 * Set a port as an access port.
 *
 * Parameters: <ethsw_name> <nio> <VLAN>
 */
static int cmd_set_access_port(hypervisor_conn_t *conn,int argc,char *argv[])
{
   ethsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ETHSW)))
      return(-1);
   
   if (ethsw_set_access_port(t,argv[1],atoi(argv[2])) == -1) {
      ethsw_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "unable to apply port settings");
      return(-1);
   }

   ethsw_release(argv[0]);   
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"Port settings OK");
   return(0);
}

/* 
 * Set a port as a trunk (802.1Q) port.
 *
 * Parameters: <ethsw_name> <nio> <native_VLAN>
 */
static int cmd_set_dot1q_port(hypervisor_conn_t *conn,int argc,char *argv[])
{
   ethsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ETHSW)))
      return(-1);
   
   if (ethsw_set_dot1q_port(t,argv[1],atoi(argv[2])) == -1) {
      ethsw_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_BINDING,1,
                            "unable to apply port settings");
      return(-1);
   }

   ethsw_release(argv[0]);   
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"Port settings OK");
   return(0);
}

/* Clear the MAC address table */
static int cmd_clear_mac_addr_table(hypervisor_conn_t *conn,
                                   int argc,char *argv[])
{
   ethsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ETHSW)))
      return(-1);

   ethsw_clear_mac_addr_table(t);
   ethsw_release(argv[0]);   
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Show the MAC address table */
static void cmd_show_mac_addr_entry(ethsw_table_t *t,ethsw_mac_entry_t *entry,
                                    hypervisor_conn_t *conn)
{
   hypervisor_send_reply(conn,HSC_INFO_MSG,0,
                         "%2.2x%2.2x.%2.2x%2.2x.%2.2x%2.2x  %u  %s",
                         entry->mac_addr.eth_addr_byte[0],
                         entry->mac_addr.eth_addr_byte[1],
                         entry->mac_addr.eth_addr_byte[2],
                         entry->mac_addr.eth_addr_byte[3],
                         entry->mac_addr.eth_addr_byte[4],
                         entry->mac_addr.eth_addr_byte[5],
                         entry->vlan_id,
                         entry->nio->name);
}

static int cmd_show_mac_addr_table(hypervisor_conn_t *conn,
                                   int argc,char *argv[])
{
   ethsw_table_t *t;

   if (!(t = hypervisor_find_object(conn,argv[0],OBJ_TYPE_ETHSW)))
      return(-1);
   
   ethsw_iterate_mac_addr_table(t,
                                (ethsw_foreach_entry_t)cmd_show_mac_addr_entry,
                                conn);

   ethsw_release(argv[0]);   
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}


/* Show info about a ETHSW object */
static void cmd_show_list(registry_entry_t *entry,void *opt,int *err)
{
   hypervisor_conn_t *conn = opt;
   hypervisor_send_reply(conn,HSC_INFO_MSG,0,"%s",entry->name);
}

/* Ethernet switch List */
static int cmd_list(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int err = 0;
   registry_foreach_type(OBJ_TYPE_ETHSW,cmd_show_list,conn,&err);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* ETHSW commands */
static hypervisor_cmd_t ethsw_cmd_array[] = {
   { "create", 1, 1, cmd_create, NULL },
   { "delete", 1, 1, cmd_delete, NULL },
   { "add_nio", 2, 2, cmd_add_nio, NULL },
   { "remove_nio", 2, 2, cmd_remove_nio, NULL },
   { "set_access_port", 3, 3, cmd_set_access_port, NULL },
   { "set_dot1q_port", 3, 3, cmd_set_dot1q_port, NULL },
   { "clear_mac_addr_table", 1, 1, cmd_clear_mac_addr_table, NULL },
   { "show_mac_addr_table", 1, 1, cmd_show_mac_addr_table, NULL },
   { "list", 0, 0, cmd_list, NULL },
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor Ethernet switch initialization */
int hypervisor_ethsw_init(void)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module("ethsw",NULL);
   assert(module != NULL);

   hypervisor_register_cmd_array(module,ethsw_cmd_array);
   return(0);
}
