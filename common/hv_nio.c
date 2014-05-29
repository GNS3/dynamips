/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Hypervisor NIO routines.
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
#include "atm.h"
#include "frame_relay.h"
#include "crc.h"
#include "net_io.h"
#include "net_io_bridge.h"
#include "net_io_filter.h"
#ifdef GEN_ETH
#include "gen_eth.h"
#endif
#include "registry.h"
#include "hypervisor.h"

/* 
 * Create a UDP NIO
 *
 * Parameters: <nio_name> <local_port> <remote_host> <remote_port>
 */
static int cmd_create_udp(hypervisor_conn_t *conn,int argc,char *argv[])
{   
   netio_desc_t *nio;

   nio = netio_desc_create_udp(argv[0],atoi(argv[1]),argv[2],atoi(argv[3]));

   if (!nio) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create UDP NIO");
      return(-1);
   }

   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' created",argv[0]);
   return(0);
}

/* 
 * Create a Auto UDP NIO
 *
 * Parameters: <nio_name> <local_addr> <local_port_start> <local_port_end>
 */
static int cmd_create_udp_auto(hypervisor_conn_t *conn,int argc,char *argv[])
{   
   netio_desc_t *nio;
   int local_port;
   
   nio = netio_desc_create_udp_auto(argv[0],argv[1],atoi(argv[2]),atoi(argv[3]));
   
   if (!nio) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create UDP Auto NIO");
      return(-1);
   }
   
   local_port = netio_udp_auto_get_local_port(nio);
   
   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"%d",local_port);
   return(0);
}

/*
 * Connect an UDP Auto NIO to a remote host/port.
 *
 * Parameters: <nio_name> <remote_host> <remote_port>
 */ 
static int cmd_connect_udp_auto(hypervisor_conn_t *conn,int argc,char *argv[])
{   
   netio_desc_t *nio;
   int res;
   
   if (!(nio = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO)))
      return(-1);
   
   res = netio_udp_auto_connect(nio,argv[1],atoi(argv[2]));
   netio_release(argv[0]);
   
   if (res == 0) {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' connected",argv[0]);
      return(0);
   } else {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,"unable to connect NIO");
      return(-1);
   }
}


/* 
 * Create a Multicast NIO
 *
 * Parameters: <nio_name> <mcast_group> <mcast_port>
 */
static int cmd_create_mcast(hypervisor_conn_t *conn,int argc,char *argv[])
{   
   netio_desc_t *nio;

   nio = netio_desc_create_mcast(argv[0],argv[1],atoi(argv[2]));

   if (!nio) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create Multicast NIO");
      return(-1);
   }

   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' created",argv[0]);
   return(0);
}

/* Set TTL for a Multicast NIO */
static int cmd_set_mcast_ttl(hypervisor_conn_t *conn,int argc,char *argv[])
{   
   netio_desc_t *nio;

   if (!(nio = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO)))
      return(-1);

   netio_mcast_set_ttl(nio,atoi(argv[1]));

   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' TTL changed",argv[0]);
   return(0);
}

/* 
 * Create a UNIX NIO
 *
 * Parameters: <nio_name> <local_file> <remote_file>
 */
static int cmd_create_unix(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;

   nio = netio_desc_create_unix(argv[0],argv[1],argv[2]);

   if (!nio) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,"unable to create UNIX NIO");
      return(-1);
   }

   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' created",argv[0]);
   return(0);
}

/* 
 * Create a VDE NIO
 *
 * Parameters: <nio_name> <control_file> <local_file>
 */
static int cmd_create_vde(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;

   nio = netio_desc_create_vde(argv[0],argv[1],argv[2]);

   if (!nio) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,"unable to create VDE NIO");
      return(-1);
   }

   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' created",argv[0]);
   return(0);
}

/* 
 * Create a TAP NIO
 *
 * Parameters: <nio_name> <tap_device>
 */
static int cmd_create_tap(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;

   nio = netio_desc_create_tap(argv[0],argv[1]);

   if (!nio) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,"unable to create TAP NIO");
      return(-1);
   }

   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' created",argv[0]);
   return(0);
}

/* 
 * Create a generic ethernet PCAP NIO
 *
 * Parameters: <nio_name> <eth_device>
 */
#ifdef GEN_ETH
static int cmd_create_gen_eth(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;

   nio = netio_desc_create_geneth(argv[0],argv[1]);

   if (!nio) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create generic ethernet NIO");
      return(-1);
   }

   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' created",argv[0]);
   return(0);
}
#endif

/* 
 * Create a linux raw ethernet NIO
 *
 * Parameters: <nio_name> <eth_device>
 */
#ifdef LINUX_ETH
static int cmd_create_linux_eth(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;

   nio = netio_desc_create_lnxeth(argv[0],argv[1]);

   if (!nio) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create Linux raw ethernet NIO");
      return(-1);
   }

   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' created",argv[0]);
   return(0);
}
#endif

/* 
 * Create a Null NIO
 *
 * Parameters: <nio_name>
 */
static int cmd_create_null(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;

   nio = netio_desc_create_null(argv[0]);

   if (!nio) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create Null NIO");
      return(-1);
   }

   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' created",argv[0]);
   return(0);
}

/* 
 * Create a FIFO NIO
 *
 * Parameters: <nio_name>
 */
static int cmd_create_fifo(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;

   nio = netio_desc_create_fifo(argv[0]);

   if (!nio) {
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                            "unable to create FIFO NIO");
      return(-1);
   }

   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' created",argv[0]);
   return(0);
}

/* 
 * Establish a cross-connect between 2 FIFO NIO
 *
 * Parameters: <nio_A_name> <nio_B_name>
 */
static int cmd_crossconnect_fifo(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *a,*b;

   if (!(a = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO)))
      return(-1);

   if (!(b = hypervisor_find_object(conn,argv[1],OBJ_TYPE_NIO))) {
      netio_release(argv[0]);
      return(-1);
   }

   netio_fifo_crossconnect(a,b);

   netio_release(argv[0]);
   netio_release(argv[1]);

   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Rename a NIO */
static int cmd_rename(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;
   char *newname;

   if (!(nio = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO)))
      return(-1);

   if (registry_exists(argv[1],OBJ_TYPE_NIO)) {
      netio_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_RENAME,1,
                            "unable to rename NIO '%s', '%s' already exists",
                            argv[0],argv[1]);
      return(-1);
   }

   if(!(newname = strdup(argv[1]))) {
      netio_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_RENAME,1,
                            "unable to rename NIO '%s', out of memory",
                            argv[0]);
      return(-1);
   }

   if (registry_rename(argv[0],newname,OBJ_TYPE_NIO)) {
      free(newname);
      netio_release(argv[0]);
      hypervisor_send_reply(conn,HSC_ERR_RENAME,1,
                            "unable to rename NIO '%s'",
                            argv[0]);
      return(-1);
   }

   free(nio->name);
   nio->name = newname;

   netio_release(argv[1]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' renamed to '%s'",argv[0],argv[1]);
   return(0);
}

/* Delete a NIO */
static int cmd_delete(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int res;

   res = netio_delete(argv[0]);

   if (res == 1) {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,"NIO '%s' deleted",argv[0]);
   } else {
      hypervisor_send_reply(conn,HSC_ERR_DELETE,1,
                            "unable to delete NIO '%s'",argv[0]);
   }

   return(res);
}

/* 
 * Enable/Disable debugging for an NIO
 *
 * Parameters: <nio_name> <debug_level>
 */
static int cmd_set_debug(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;

   if (!(nio = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO)))
      return(-1);

   nio->debug = atoi(argv[1]);

   netio_release(argv[0]);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Bind a packet filter */
static int cmd_bind_filter(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;
   int res;

   if (!(nio = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO)))
      return(-1);
   
   res = netio_filter_bind(nio,atoi(argv[1]),argv[2]);
   netio_release(argv[0]);

   if (!res) {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   } else {
      hypervisor_send_reply(conn,HSC_ERR_UNK_OBJ,1,
                            "Unknown filter %s",argv[2]);
   }
   return(0);
}

/* Unbind a packet filter */
static int cmd_unbind_filter(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;
   int res;

   if (!(nio = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO)))
      return(-1);
   
   res = netio_filter_unbind(nio,atoi(argv[1]));
   netio_release(argv[0]);

   if (!res) {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   } else {
      hypervisor_send_reply(conn,HSC_ERR_UNK_OBJ,1,
                            "No filter previously defined");
   }
   return(0);
}


/* Setup a packet filter for a given NIO */
static int cmd_setup_filter(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;
   int res;

   if (!(nio = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO)))
      return(-1);

   res = netio_filter_setup(nio,atoi(argv[1]),argc-2,&argv[2]);
   netio_release(argv[0]);

   if (!res) {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   } else {
      hypervisor_send_reply(conn,HSC_ERR_UNSPECIFIED,1,"Failed to setup filter");
   }
   return(0);
}

/* Get statistics of a NIO */
static int cmd_get_stats(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;
   m_uint64_t spi,spo,sbi,sbo;

   if (!(nio = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO)))
      return(-1);

   spi = nio->stats_pkts_in;
   spo = nio->stats_pkts_out;
   sbi = nio->stats_bytes_in;
   sbo = nio->stats_bytes_out;

   netio_release(argv[0]);

   hypervisor_send_reply(conn,HSC_INFO_OK,1,"%llu %llu %llu %llu",
                         spi,spo,sbi,sbo);
   return(0);
}

/* Reset statistics of a NIO */
static int cmd_reset_stats(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;

   if (!(nio = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO)))
      return(-1);
   
   netio_reset_stats(nio);
   netio_release(argv[0]);

   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Set bandwidth constraint */
static int cmd_set_bandwidth(hypervisor_conn_t *conn,int argc,char *argv[])
{
   netio_desc_t *nio;

   if (!(nio = hypervisor_find_object(conn,argv[0],OBJ_TYPE_NIO)))
      return(-1);
   
   netio_set_bandwidth(nio,atoi(argv[1]));
   netio_release(argv[0]);

   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* Show info about a NIO object */
static void cmd_show_nio_list(registry_entry_t *entry,void *opt,int *err)
{
   hypervisor_conn_t *conn = opt;
   hypervisor_send_reply(conn,HSC_INFO_MSG,0,"%s",entry->name);
}

/* NIO List */
static int cmd_nio_list(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int err = 0;
   registry_foreach_type(OBJ_TYPE_NIO,cmd_show_nio_list,conn,&err);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* NIO commands */
static hypervisor_cmd_t nio_cmd_array[] = {
   { "create_udp", 4, 4, cmd_create_udp, NULL },
   { "create_udp_auto", 4, 4, cmd_create_udp_auto, NULL },
   { "connect_udp_auto", 3, 3, cmd_connect_udp_auto, NULL },
   { "create_mcast", 3, 3, cmd_create_mcast, NULL },
   { "set_mcast_ttl", 2, 2, cmd_set_mcast_ttl, NULL },
   { "create_unix", 3, 3, cmd_create_unix, NULL },
   { "create_vde", 3, 3, cmd_create_vde, NULL },
   { "create_tap", 2, 2, cmd_create_tap, NULL },
#ifdef GEN_ETH
   { "create_gen_eth", 2, 2, cmd_create_gen_eth, NULL },
#endif
#ifdef LINUX_ETH
   { "create_linux_eth", 2, 2, cmd_create_linux_eth, NULL },
#endif
   { "create_null", 1, 1, cmd_create_null, NULL },
   { "create_fifo", 1, 1, cmd_create_fifo, NULL },
   { "crossconnect_fifo", 2, 2, cmd_crossconnect_fifo, NULL },
   { "rename", 2, 2, cmd_rename, NULL },
   { "delete", 1, 1, cmd_delete, NULL },
   { "set_debug", 2, 2, cmd_set_debug, NULL },
   { "bind_filter", 3, 3, cmd_bind_filter, NULL },
   { "unbind_filter", 2, 2, cmd_unbind_filter, NULL },
   { "setup_filter", 2, 10, cmd_setup_filter, NULL },
   { "get_stats", 1, 1, cmd_get_stats },
   { "reset_stats", 1, 1, cmd_reset_stats },
   { "set_bandwidth", 2, 2, cmd_set_bandwidth },
   { "list", 0, 0, cmd_nio_list, NULL },
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor NIO initialization */
int hypervisor_nio_init(void)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module("nio",NULL);
   assert(module != NULL);

   hypervisor_register_cmd_array(module,nio_cmd_array);
   return(0);
}
