/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * ATM bridge (RFC1483)
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
#include "atm.h"
#include "atm_vsar.h"
#include "atm_bridge.h"

#define ATM_BRIDGE_LOCK(t)   pthread_mutex_lock(&(t)->lock)
#define ATM_BRIDGE_UNLOCK(t) pthread_mutex_unlock(&(t)->lock)

/* Acquire a reference to an ATM bridge (increment reference count) */
atm_bridge_t *atm_bridge_acquire(char *name)
{
   return(registry_find(name,OBJ_TYPE_ATM_BRIDGE));
}

/* Release an ATM switch (decrement reference count) */
int atm_bridge_release(char *name)
{
   return(registry_unref(name,OBJ_TYPE_ATM_BRIDGE));
}

/* Receive an ATM cell */
static int atm_bridge_recv_cell(netio_desc_t *nio,
                                u_char *atm_cell,ssize_t cell_len,
                                atm_bridge_t *t)
{   
   m_uint32_t atm_hdr,vpi,vci;
   int status,res = 0;

   if (cell_len != ATM_CELL_SIZE)
      return(-1);

   ATM_BRIDGE_LOCK(t);

   /* check the VPI/VCI */
   atm_hdr = m_ntoh32(atm_cell);

   vpi = (atm_hdr & ATM_HDR_VPI_MASK) >> ATM_HDR_VPI_SHIFT;
   vci = (atm_hdr & ATM_HDR_VCI_MASK) >> ATM_HDR_VCI_SHIFT;
   
   if ((t->vpi != vpi) || (t->vci != vci))
      goto done;

   if ((status = atm_aal5_recv(&t->arc,atm_cell)) == 1) {
      /* Got AAL5 packet, check RFC1483b encapsulation */
      if ((t->arc.len > ATM_RFC1483B_HLEN) && 
          !memcmp(t->arc.buffer,atm_rfc1483b_header,ATM_RFC1483B_HLEN)) 
      {
         netio_send(t->eth_nio,
                    t->arc.buffer+ATM_RFC1483B_HLEN,
                    t->arc.len-ATM_RFC1483B_HLEN);
      }  
       
      atm_aal5_recv_reset(&t->arc);
   } else {
      if (status < 0) {
         atm_aal5_recv_reset(&t->arc);
         res = -1;
      }
   }

 done:
   ATM_BRIDGE_UNLOCK(t);
   return(res);
}

/* Receive an Ethernet packet */
static int atm_bridge_recv_pkt(netio_desc_t *nio,u_char *pkt,ssize_t len,
                               atm_bridge_t *t)
{
   return(atm_aal5_send_rfc1483b(t->atm_nio,t->vpi,t->vci,pkt,len));
}

/* Create a virtual ATM bridge */
atm_bridge_t *atm_bridge_create(char *name)
{
   atm_bridge_t *t;

   /* Allocate a new switch structure */
   if (!(t = malloc(sizeof(*t))))
      return NULL;

   memset(t,0,sizeof(*t));
   pthread_mutex_init(&t->lock,NULL);
   atm_aal5_recv_reset(&t->arc);

   if (!(t->name = strdup(name)))
      goto err_name;

   /* Record this object in registry */
   if (registry_add(t->name,OBJ_TYPE_ATM_BRIDGE,t) == -1) {
      fprintf(stderr,"atm_bridge_create: unable to create bridge '%s'\n",
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

/* Configure an ATM bridge */
int atm_bridge_configure(atm_bridge_t *t,char *eth_nio,
                         char *atm_nio,u_int vpi,u_int vci)
{
   netio_desc_t *e_nio,*a_nio;

   ATM_BRIDGE_LOCK(t);

   if (t->eth_nio || t->atm_nio)
      goto error;

   e_nio = netio_acquire(eth_nio);
   a_nio = netio_acquire(atm_nio);

   if (!e_nio || !a_nio)
      goto error;

   t->eth_nio = e_nio;
   t->atm_nio = a_nio;
   t->vpi     = vpi;
   t->vci     = vci;

   /* Add ATM RX listener */
   if (netio_rxl_add(t->atm_nio,(netio_rx_handler_t)atm_bridge_recv_cell,
                     t,NULL) == -1)
      goto error;

   /* Add Ethernet RX listener */
   if (netio_rxl_add(t->eth_nio,(netio_rx_handler_t)atm_bridge_recv_pkt,
                     t,NULL) == -1)
      goto error;

   ATM_BRIDGE_UNLOCK(t);
   return(0);

 error:
   ATM_BRIDGE_UNLOCK(t);
   return(-1);
}

/* Release NIO used by an ATM bridge */
static void atm_bridge_clear_config(atm_bridge_t *t)
{
   if (t != NULL) {
      /* release ethernet NIO */
      if (t->eth_nio) {
         netio_rxl_remove(t->eth_nio);
         netio_release(t->eth_nio->name);
      }

      /* release ATM NIO */
      if (t->atm_nio) {
         netio_rxl_remove(t->atm_nio);
         netio_release(t->atm_nio->name);
      }

      t->eth_nio = t->atm_nio = NULL;
   }
}

/* Unconfigure an ATM bridge */
int atm_bridge_unconfigure(atm_bridge_t *t)
{
   ATM_BRIDGE_LOCK(t);
   atm_bridge_clear_config(t);
   ATM_BRIDGE_UNLOCK(t);
   return(0);
}

/* Free resources used by an ATM bridge */
static int atm_bridge_free(void *data,void *arg)
{
   atm_bridge_t *t = data;

   atm_bridge_clear_config(t);
   free(t->name);
   free(t);
   return(TRUE);
}

/* Delete an ATM bridge */
int atm_bridge_delete(char *name)
{
   return(registry_delete_if_unused(name,OBJ_TYPE_ATM_BRIDGE,
                                    atm_bridge_free,NULL));
}

/* Delete all ATM switches */
int atm_bridge_delete_all(void)
{
   return(registry_delete_type(OBJ_TYPE_ATM_BRIDGE,atm_bridge_free,NULL));
}

/* Create a new interface */
int atm_bridge_cfg_create_if(atm_bridge_t *t,char **tokens,int count)
{
   netio_desc_t *nio = NULL;
   int nio_type;

   /* at least: IF, interface name, NetIO type */
   if (count < 3) {
      fprintf(stderr,"atmsw_cfg_create_if: invalid interface description\n");
      return(-1);
   }
   
   nio_type = netio_get_type(tokens[2]);
   switch(nio_type) {
      case NETIO_TYPE_UNIX:
         if (count != 5) {
            fprintf(stderr,"ATMSW: invalid number of arguments "
                    "for UNIX NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_unix(tokens[1],tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_UDP:
         if (count != 6) {
            fprintf(stderr,"ATMSW: invalid number of arguments "
                    "for UDP NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_udp(tokens[1],atoi(tokens[3]),
                                     tokens[4],atoi(tokens[5]));
         break;

      case NETIO_TYPE_MCAST:
         if (count != 5) {
            fprintf(stderr,"ATMSW: invalid number of arguments "
                    "for Multicast NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_mcast(tokens[1],tokens[3],atoi(tokens[4]));
         break;

      case NETIO_TYPE_TCP_CLI:
         if (count != 5) {
            fprintf(stderr,"ATMSW: invalid number of arguments "
                    "for TCP CLI NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_tcp_cli(tokens[1],tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TCP_SER:
         if (count != 4) {
            fprintf(stderr,"ATMSW: invalid number of arguments "
                    "for TCP SER NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_tcp_ser(tokens[1],tokens[3]);
         break;

      default:
         fprintf(stderr,"ATMSW: unknown/invalid NETIO type '%s'\n",
                 tokens[2]);
   }

   if (!nio) {
      fprintf(stderr,"ATMSW: unable to create NETIO descriptor of "
              "interface %s\n",tokens[1]);
      return(-1);
   }

   netio_release(nio->name);
   return(0);
}

/* Bridge setup */
int atm_bridge_cfg_setup(atm_bridge_t *t,char **tokens,int count)
{
   /* 5 parameters: "BRIDGE", Eth_IF, ATM_IF, VPI, VCI */
   if (count != 5) {
      fprintf(stderr,"ATM Bridge: invalid VPC descriptor.\n");
      return(-1);
   }

   return(atm_bridge_configure(t,tokens[1],tokens[2],
                               atoi(tokens[3]),atoi(tokens[4])));
}

#define ATM_BRIDGE_MAX_TOKENS  16

/* Handle an ATMSW configuration line */
int atm_bridge_handle_cfg_line(atm_bridge_t *t,char *str)
{  
   char *tokens[ATM_BRIDGE_MAX_TOKENS];
   int count;

   if ((count = m_strsplit(str,':',tokens,ATM_BRIDGE_MAX_TOKENS)) <= 1)
      return(-1);

   if (!strcmp(tokens[0],"IF"))
      return(atm_bridge_cfg_create_if(t,tokens,count));
   else if (!strcmp(tokens[0],"BRIDGE"))
      return(atm_bridge_cfg_setup(t,tokens,count));

   fprintf(stderr,"ATM Bridge: "
           "Unknown statement \"%s\" (allowed: IF,BRIDGE)\n",
           tokens[0]);
   return(-1);
}


/* Read an ATM bridge configuration file */
int atm_bridge_read_cfg_file(atm_bridge_t *t,char *filename)
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
         atm_bridge_handle_cfg_line(t,buffer);
   }
   
   fclose(fd);
   return(0);
}

/* Start a virtual ATM bridge */
int atm_bridge_start(char *filename)
{
   atm_bridge_t *t;

   if (!(t = atm_bridge_create("default"))) {
      fprintf(stderr,"ATM Bridge: unable to create virtual fabric table.\n");
      return(-1);
   }

   if (atm_bridge_read_cfg_file(t,filename) == -1) {
      fprintf(stderr,"ATM Bridge: unable to parse configuration file.\n");
      return(-1);
   }

   atm_bridge_release("default");
   return(0);
}
