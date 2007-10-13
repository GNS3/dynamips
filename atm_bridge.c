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
