/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * NetIO bridges.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

#include "utils.h"
#include "registry.h"
#include "net_io.h"
#include "net_io_bridge.h"

#define PKT_MAX_SIZE 2048

/* Receive a packet */
static int netio_bridge_recv_pkt(netio_desc_t *nio,u_char *pkt,ssize_t pkt_len,
                                 netio_bridge_t *t)
{
   int i;

   NETIO_BRIDGE_LOCK(t);

   for(i=0;i<NETIO_BRIDGE_MAX_NIO;i++)
      if ((t->nio[i] != NULL) && (t->nio[i] != nio))
         netio_send(t->nio[i],pkt,pkt_len);
   
   NETIO_BRIDGE_UNLOCK(t);
   return(0);
}

/* Acquire a reference to NetIO bridge from the registry (inc ref count) */
netio_desc_t *netio_bridge_acquire(char *name)
{
   return(registry_find(name,OBJ_TYPE_NIO_BRIDGE));
}

/* Release a NetIO bridge (decrement reference count) */
int netio_bridge_release(char *name)
{
   return(registry_unref(name,OBJ_TYPE_NIO_BRIDGE));
}

/* Create a virtual bridge */
netio_bridge_t *netio_bridge_create(char *name)
{
   netio_bridge_t *t;

   /* Allocate a new bridge structure */
   if (!(t = malloc(sizeof(*t))))
      return NULL;

   memset(t,0,sizeof(*t));
   pthread_mutex_init(&t->lock,NULL);

   if (!(t->name = strdup(name)))
      goto err_name;

   /* Record this object in registry */
   if (registry_add(t->name,OBJ_TYPE_NIO_BRIDGE,t) == -1) {
      fprintf(stderr,"netio_bridge_create: unable to register bridge '%s'\n",
              name);
      goto err_reg;
   }

   return t;

 err_reg:
   free(t->name);
 err_name:
   free(t);
   return NULL;
}

/* Add a NetIO descriptor to a virtual bridge */
int netio_bridge_add_netio(netio_bridge_t *t,char *nio_name)
{
   netio_desc_t *nio;
   int i;

   NETIO_BRIDGE_LOCK(t);

   /* Try to find a free slot in the NIO array */
   for(i=0;i<NETIO_BRIDGE_MAX_NIO;i++)
      if (t->nio[i] == NULL)
         break;
   
   /* No free slot found ... */
   if (i == NETIO_BRIDGE_MAX_NIO)
      goto error;

   /* Acquire the NIO descriptor and increment its reference count */
   if (!(nio = netio_acquire(nio_name)))
      goto error;

   t->nio[i] = nio;
   netio_rxl_add(nio,(netio_rx_handler_t)netio_bridge_recv_pkt,t,NULL);
   NETIO_BRIDGE_UNLOCK(t);
   return(0);

 error:
   NETIO_BRIDGE_UNLOCK(t);
   return(-1);
}

/* Free resources used by a NIO in a bridge */
static void netio_bridge_free_nio(netio_desc_t *nio)
{
   netio_rxl_remove(nio);
   netio_release(nio->name);
}

/* Remove a NetIO descriptor from a virtual bridge */
int netio_bridge_remove_netio(netio_bridge_t *t,char *nio_name)
{
   netio_desc_t *nio;
   int i;

   NETIO_BRIDGE_LOCK(t);

   if (!(nio = registry_exists(nio_name,OBJ_TYPE_NIO)))
      goto error;

   /* Try to find the NIO in the NIO array */
   for(i=0;i<NETIO_BRIDGE_MAX_NIO;i++)
      if (t->nio[i] == nio)
         break;

   if (i == NETIO_BRIDGE_MAX_NIO)
      goto error;

   /* Remove the NIO from the RX multiplexer */
   netio_bridge_free_nio(t->nio[i]);
   t->nio[i] = NULL;

   NETIO_BRIDGE_UNLOCK(t);
   return(0);

 error:
   NETIO_BRIDGE_UNLOCK(t);
   return(-1);
}

/* Save the configuration of a bridge */
void netio_bridge_save_config(netio_bridge_t *t,FILE *fd)
{
   int i;
   
   fprintf(fd,"nio_bridge create %s\n",t->name);

   for(i=0;i<NETIO_BRIDGE_MAX_NIO;i++)
      fprintf(fd,"nio_bridge add_nio %s %s\n",t->name,t->nio[i]->name);

   fprintf(fd,"\n");
}

/* Save configurations of all NIO bridges */
static void netio_bridge_reg_save_config(registry_entry_t *entry,
                                             void *opt,int *err)
{
   netio_bridge_save_config((netio_bridge_t *)entry->data,(FILE *)opt);
}

void netio_bridge_save_config_all(FILE *fd)
{
   registry_foreach_type(OBJ_TYPE_NIO_BRIDGE,netio_bridge_reg_save_config,
                         fd,NULL);
}

/* Free resources used by a NIO bridge */
static int netio_bridge_free(void *data,void *arg)
{
   netio_bridge_t *t = data;
   int i;

   NETIO_BRIDGE_LOCK(t);

   for(i=0;i<NETIO_BRIDGE_MAX_NIO;i++) {
      if (!t->nio[i])
         continue;

      netio_bridge_free_nio(t->nio[i]);
   }

   NETIO_BRIDGE_UNLOCK(t);
   free(t->name);
   free(t);
   return(TRUE);
}

/* Delete a virtual bridge */
int netio_bridge_delete(char *name)
{
   return(registry_delete_if_unused(name,OBJ_TYPE_NIO_BRIDGE,
                                    netio_bridge_free,NULL));
}

/* Delete all virtual bridges */
int netio_bridge_delete_all(void)
{
   return(registry_delete_type(OBJ_TYPE_NIO_BRIDGE,netio_bridge_free,NULL));
}

/* Create a new interface */
static int netio_bridge_cfg_create_if(netio_bridge_t *t,
                                      char **tokens,int count)
{
   netio_desc_t *nio = NULL;
   int nio_type;

   nio_type = netio_get_type(tokens[1]);
   switch(nio_type) {
      case NETIO_TYPE_UNIX:
         if (count != 4) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for UNIX NIO\n");
            break;
         }

         nio = netio_desc_create_unix(tokens[0],tokens[2],tokens[3]);
         break;

      case NETIO_TYPE_TAP:
         if (count != 3) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for TAP NIO\n");
            break;
         }

         nio = netio_desc_create_tap(tokens[0],tokens[2]);
         break;

      case NETIO_TYPE_UDP:
         if (count != 5) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for UDP NIO\n");
            break;
         }

         nio = netio_desc_create_udp(tokens[0],atoi(tokens[2]),
                                     tokens[3],atoi(tokens[4]));
         break;

      case NETIO_TYPE_TCP_CLI:
         if (count != 4) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for TCP CLI NIO\n");
            break;
         }

         nio = netio_desc_create_tcp_cli(tokens[0],tokens[2],tokens[3]);
         break;

      case NETIO_TYPE_TCP_SER:
         if (count != 3) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for TCP SER NIO\n");
            break;
         }

         nio = netio_desc_create_tcp_ser(tokens[0],tokens[2]);
         break;

#ifdef GEN_ETH
      case NETIO_TYPE_GEN_ETH:
         if (count != 3) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for Generic Ethernet NIO\n");
            break;
         }
         
         nio = netio_desc_create_geneth(tokens[0],tokens[2]);
         break;
#endif

#ifdef LINUX_ETH
      case NETIO_TYPE_LINUX_ETH:
         if (count != 3) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for Linux Ethernet NIO\n");
            break;
         }
         
         nio = netio_desc_create_lnxeth(tokens[0],tokens[2]);
         break;
#endif

      default:
         fprintf(stderr,"NETIO_BRIDGE: unknown/invalid NETIO type '%s'\n",
                 tokens[1]);
   }

   if (!nio) {
      fprintf(stderr,"NETIO_BRIDGE: unable to create NETIO descriptor\n");
      return(-1);
   }

   if (netio_bridge_add_netio(t,tokens[0]) == -1) {
      fprintf(stderr,"NETIO_BRIDGE: unable to add NETIO descriptor.\n");
      netio_release(nio->name);
      return(-1);
   }

   netio_release(nio->name);
   return(0);
}

#define NETIO_BRIDGE_MAX_TOKENS  16

/* Handle a configuration line */
static int netio_bridge_handle_cfg_line(netio_bridge_t *t,char *str)
{  
   char *tokens[NETIO_BRIDGE_MAX_TOKENS];
   int count;

   if ((count = m_strsplit(str,':',tokens,NETIO_BRIDGE_MAX_TOKENS)) <= 2)
      return(-1);

   return(netio_bridge_cfg_create_if(t,tokens,count));
}

/* Read a configuration file */
static int netio_bridge_read_cfg_file(netio_bridge_t *t,char *filename)
{
   char buffer[1024],*ptr;
   FILE *fd;

   if (!(fd = fopen(filename,"r"))) {
      perror("fopen");
      return(-1);
   }
   
   while(!feof(fd)) {
      if (!fgets(buffer,sizeof(buffer),fd))
         break;
      
      /* skip comments and end of line */
      if ((ptr = strpbrk(buffer,"#\r\n")) != NULL)
         *ptr = 0;

      /* analyze non-empty lines */
      if (strchr(buffer,':'))
         netio_bridge_handle_cfg_line(t,buffer);
   }
   
   fclose(fd);
   return(0);
}

/* Start a virtual bridge */
int netio_bridge_start(char *filename)
{
   netio_bridge_t *t;

   if (!(t = netio_bridge_create("default"))) {
      fprintf(stderr,"NETIO_BRIDGE: unable to create virtual fabric table.\n");
      return(-1);
   }

   if (netio_bridge_read_cfg_file(t,filename) == -1) {
      fprintf(stderr,"NETIO_BRIDGE: unable to parse configuration file.\n");
      return(-1);
   }
   
   netio_bridge_release("default");
   return(0);
}
