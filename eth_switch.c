/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Virtual Ethernet switch with VLAN/Trunk support.
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
#include <assert.h>

#include "utils.h"
#include "net.h"
#include "registry.h"
#include "net_io.h"
#include "eth_switch.h"

/* Debbuging message */
void ethsw_debug(ethsw_table_t *t,char *fmt,...)
{
   char module[128];
   va_list ap;

   if (t->debug) {
      va_start(ap,fmt);
      snprintf(module,sizeof(module),"ETHSW %s",t->name);
      m_flog(log_file,module,fmt,ap);
      va_end(ap);
   }
}


/* Compute hash index on the specified MAC address and VLAN */
static inline u_int ethsw_hash_index(n_eth_addr_t *addr,u_int vlan_id)
{
   u_int h_index;

   h_index =  (addr->eth_addr_byte[0] << 8) | addr->eth_addr_byte[1];
   h_index ^= (addr->eth_addr_byte[2] << 8) | addr->eth_addr_byte[3];
   h_index ^= (addr->eth_addr_byte[4] << 8) | addr->eth_addr_byte[5];
   h_index ^= vlan_id;
   return(h_index & (ETHSW_HASH_SIZE - 1));
}

/* Invalidate the whole MAC address table */
static void ethsw_invalidate(ethsw_table_t *t)
{
   memset(t->mac_addr_table,0,sizeof(t->mac_addr_table));
}

/* Invalidate entry of the MAC address table referring to the specified NIO */
static void ethsw_invalidate_port(ethsw_table_t *t,netio_desc_t *nio)
{
   ethsw_mac_entry_t *entry;
   int i;
   
   for(i=0;i<ETHSW_HASH_SIZE;i++) {
      entry = &t->mac_addr_table[i];
      if (entry->nio == nio) {
         entry->nio = NULL;
         entry->vlan_id = 0;
      }
   }
}

/* Push a 802.1Q tag */
static void dot1q_push_tag(m_uint8_t *pkt,ethsw_packet_t *sp,u_int vlan)
{   
   n_eth_dot1q_hdr_t *hdr;

   memcpy(pkt,sp->pkt,(N_ETH_HLEN - 2));

   hdr = (n_eth_dot1q_hdr_t *)pkt;
   hdr->type    = htons(N_ETH_PROTO_DOT1Q);
   hdr->vlan_id = htons(sp->input_vlan);

   memcpy(pkt + sizeof(n_eth_dot1q_hdr_t),
          sp->pkt + (N_ETH_HLEN - 2),
          sp->pkt_len - (N_ETH_HLEN - 2));
}

/* Pop a 802.1Q tag */
static void dot1q_pop_tag(m_uint8_t *pkt,ethsw_packet_t *sp)
{
   memcpy(pkt,sp->pkt,(N_ETH_HLEN - 2));

   memcpy(pkt + (N_ETH_HLEN - 2),
          sp->pkt + sizeof(n_eth_dot1q_hdr_t),
          sp->pkt_len - sizeof(n_eth_dot1q_hdr_t));
}

/* Input vector for ACCESS ports */
static void ethsw_iv_access(ethsw_table_t *t,ethsw_packet_t *sp,
                            netio_desc_t *op)
{
   m_uint8_t pkt[ETHSW_MAX_PKT_SIZE+4];

   switch(op->vlan_port_type) {
      /* Access -> Access: no special treatment */
      case ETHSW_PORT_TYPE_ACCESS:  
         netio_send(op,sp->pkt,sp->pkt_len);
         break;

      /* Access -> 802.1Q: push tag */
      case ETHSW_PORT_TYPE_DOT1Q:
         /* 
          * If the native VLAN of output port is the same as input,
          * forward the packet without adding the tag.
          */
         if (op->vlan_id == sp->input_vlan) {
            netio_send(op,sp->pkt,sp->pkt_len);
         } else {
            dot1q_push_tag(pkt,sp,op->vlan_id);
            netio_send(op,pkt,sp->pkt_len+4);
         }
         break;

      default:
         fprintf(stderr,"ethsw_iv_access: unknown port type %u\n",
                 op->vlan_port_type);
   }
}

/* Input vector for 802.1Q ports */
static void ethsw_iv_dot1q(ethsw_table_t *t,ethsw_packet_t *sp,
                           netio_desc_t *op)
{
   m_uint8_t pkt[ETHSW_MAX_PKT_SIZE+4];

   /* If we don't have an input tag, we work temporarily as an access port */
   if (!sp->input_tag) {
      ethsw_iv_access(t,sp,op);
      return;
   }

   switch(op->vlan_port_type) {
      /* 802.1Q -> Access: pop tag */
      case ETHSW_PORT_TYPE_ACCESS:
         dot1q_pop_tag(pkt,sp);
         netio_send(op,pkt,sp->pkt_len-4);
         break;

      /* 802.1Q -> 802.1Q: pop tag if native VLAN in output otherwise no-op */
      case ETHSW_PORT_TYPE_DOT1Q:
         if (op->vlan_id == sp->input_vlan) {
            dot1q_pop_tag(pkt,sp);
            netio_send(op,pkt,sp->pkt_len-4);
         } else {
            netio_send(op,sp->pkt,sp->pkt_len);
         }
         break;

      default:
         fprintf(stderr,"ethsw_iv_dot1q: unknown port type %u\n",
                 op->vlan_port_type);
   }
}

/* Flood a packet */
static void ethsw_flood(ethsw_table_t *t,ethsw_packet_t *sp)
{
   ethsw_input_vector_t input_vector;
   netio_desc_t *op;
   int i;

   input_vector = sp->input_port->vlan_input_vector;
   assert(input_vector != NULL);

   for(i=0;i<ETHSW_MAX_NIO;i++) {
      op = t->nio[i];

      if (!op || (op == sp->input_port))
         continue;

      /* skip output port configured in access mode with a different vlan */
      if ((op->vlan_port_type == ETHSW_PORT_TYPE_ACCESS) &&
          (op->vlan_id != sp->input_vlan))
         continue;

      /* send the packet on output port */
      input_vector(t,sp,op);
   }
}

/* Forward a packet */
static void ethsw_forward(ethsw_table_t *t,ethsw_packet_t *sp)
{
   n_eth_hdr_t *hdr = (n_eth_hdr_t *)sp->pkt;
   ethsw_input_vector_t input_vector;
   ethsw_mac_entry_t *entry;
   u_int h_index;

   /* Learn the source MAC address */
   h_index = ethsw_hash_index(&hdr->saddr,sp->input_vlan);
   entry = &t->mac_addr_table[h_index];

   entry->nio      = sp->input_port;
   entry->vlan_id  = sp->input_vlan;
   entry->mac_addr = hdr->saddr;

   /* If we have a broadcast/multicast packet, flood it */
   if (eth_addr_is_mcast(&hdr->daddr)) {
      ethsw_debug(t,"multicast dest, flooding packet.\n");
      ethsw_flood(t,sp);
      return;
   }

   /* Lookup on the destination MAC address (unicast) */
   h_index = ethsw_hash_index(&hdr->daddr,sp->input_vlan);
   entry = &t->mac_addr_table[h_index];

   /* If the dest MAC is unknown, flood the packet */
   if (memcmp(&entry->mac_addr,&hdr->daddr,N_ETH_ALEN) || 
       (entry->vlan_id != sp->input_vlan)) 
   {
      ethsw_debug(t,"unknown dest, flooding packet.\n");
      ethsw_flood(t,sp);
      return;
   }

   /* Forward the packet to the output port only */
   if (entry->nio != sp->input_port) {
      input_vector = sp->input_port->vlan_input_vector;
      assert(input_vector != NULL);
      input_vector(t,sp,entry->nio);
   } else {
      ethsw_debug(t,"source and dest ports identical, dropping.\n");
   }
}

/* Receive a packet and prepare its forwarding */
static inline int ethsw_receive(ethsw_table_t *t,netio_desc_t *nio,
                                u_char *pkt,ssize_t pkt_len)
{   
   n_eth_dot1q_hdr_t *dot1q_hdr;
   n_eth_isl_hdr_t *isl_hdr;
   n_eth_hdr_t *eth_hdr;
   n_eth_llc_hdr_t *llc_hdr;
   ethsw_packet_t sp;
   u_char *ptr;

   sp.input_port = nio;
   sp.input_vlan = 0;
   sp.pkt        = pkt;
   sp.pkt_len    = pkt_len;

   /* Skip runt packets */
   if (sp.pkt_len < N_ETH_HLEN)
      return(-1);

   /* Determine the input VLAN */
   switch(nio->vlan_port_type) {
      case ETHSW_PORT_TYPE_ACCESS:
         sp.input_vlan = nio->vlan_id;
         break;

      case ETHSW_PORT_TYPE_DOT1Q:
         dot1q_hdr = (n_eth_dot1q_hdr_t *)sp.pkt;

         /* use the native VLAN if no tag is found */
         if (ntohs(dot1q_hdr->type) != N_ETH_PROTO_DOT1Q) {
            sp.input_vlan = nio->vlan_id;
            sp.input_tag  = FALSE;
         } else {
            sp.input_vlan = ntohs(dot1q_hdr->vlan_id) & 0xFFF;
            sp.input_tag  = TRUE;
         }
         break;

      case ETHSW_PORT_TYPE_ISL:
         /* Check that we have an ISL packet */
         eth_hdr = (n_eth_hdr_t *)pkt;

         if (!eth_addr_is_cisco_isl(&eth_hdr->daddr))
            break;

         /* Verify LLC header */
         llc_hdr = PTR_ADJUST(n_eth_llc_hdr_t *,eth_hdr,sizeof(n_eth_hdr_t));
         if (!eth_llc_check_snap(llc_hdr))
            break;

         /* Get the VLAN id */
         isl_hdr = PTR_ADJUST(n_eth_isl_hdr_t *,llc_hdr,
                              sizeof(n_eth_llc_hdr_t));
         ptr = (u_char *)&isl_hdr->vlan;
         sp.input_vlan = (((u_int)ptr[0] << 8) | ptr[1]) >> 1;
         break;

      default:
         fprintf(stderr,"ethsw_receive: unknown port type %u\n",
                 nio->vlan_port_type);
         return(-1);
   }

   if (sp.input_vlan != 0)
      ethsw_forward(t,&sp);
   return(0);
}

/* Receive a packet (handle the locking part) */
static int ethsw_recv_pkt(netio_desc_t *nio,u_char *pkt,ssize_t pkt_len,
                          ethsw_table_t *t)
{
   ETHSW_LOCK(t);
   ethsw_receive(t,nio,pkt,pkt_len);
   ETHSW_UNLOCK(t);
   return(0);
}

/* Set a port as an access port with the specified VLAN */
static void set_access_port(netio_desc_t *nio,u_int vlan_id)
{
   nio->vlan_port_type    = ETHSW_PORT_TYPE_ACCESS;
   nio->vlan_id           = vlan_id;
   nio->vlan_input_vector = ethsw_iv_access;
}

/* Set a port as a 802.1Q trunk port */
static void set_dot1q_port(netio_desc_t *nio,u_int native_vlan)
{
   nio->vlan_port_type    = ETHSW_PORT_TYPE_DOT1Q;
   nio->vlan_id           = native_vlan;
   nio->vlan_input_vector = ethsw_iv_dot1q;
}

/* Acquire a reference to an Ethernet switch (increment reference count) */
ethsw_table_t *ethsw_acquire(char *name)
{
   return(registry_find(name,OBJ_TYPE_ETHSW));
}

/* Release an Ethernet switch (decrement reference count) */
int ethsw_release(char *name)
{
   return(registry_unref(name,OBJ_TYPE_ETHSW));
}

/* Create a virtual ethernet switch */
ethsw_table_t *ethsw_create(char *name)
{
   ethsw_table_t *t;

   /* Allocate a new switch structure */
   if (!(t = malloc(sizeof(*t))))
      return NULL;

   memset(t,0,sizeof(*t));
   pthread_mutex_init(&t->lock,NULL);

   if (!(t->name = strdup(name)))
      goto err_name;

   /* Record this object in registry */
   if (registry_add(t->name,OBJ_TYPE_ETHSW,t) == -1) {
      fprintf(stderr,"ethsw_create: unable to register switch '%s'\n",name);
      goto err_reg;
   }

   return t;

 err_reg:
   free(t->name);
 err_name:
   free(t);
   return NULL;
}

/* Add a NetIO descriptor to a virtual ethernet switch */
int ethsw_add_netio(ethsw_table_t *t,char *nio_name)
{
   netio_desc_t *nio;
   int i;

   ETHSW_LOCK(t);

   /* Try to find a free slot in the NIO array */
   for(i=0;i<ETHSW_MAX_NIO;i++)
      if (t->nio[i] == NULL)
         break;
   
   /* No free slot found ... */
   if (i == ETHSW_MAX_NIO)
      goto error;

   /* Acquire the NIO descriptor and increment its reference count */
   if (!(nio = netio_acquire(nio_name)))
      goto error;

   /* By default, the port is an access port in VLAN 1 */
   set_access_port(nio,1);

   t->nio[i] = nio;
   netio_rxl_add(nio,(netio_rx_handler_t)ethsw_recv_pkt,t,NULL);
   ETHSW_UNLOCK(t);
   return(0);

 error:
   ETHSW_UNLOCK(t);
   return(-1);
}

/* Free resources used by a NIO */
static void ethsw_free_nio(netio_desc_t *nio)
{
   netio_rxl_remove(nio);
   netio_release(nio->name);
}

/* Remove a NetIO descriptor from a virtual ethernet switch */
int ethsw_remove_netio(ethsw_table_t *t,char *nio_name)
{
   netio_desc_t *nio;
   int i;

   ETHSW_LOCK(t);

   if (!(nio = registry_exists(nio_name,OBJ_TYPE_NIO)))
      goto error;

   /* Try to find the NIO in the NIO array */
   for(i=0;i<ETHSW_MAX_NIO;i++)
      if (t->nio[i] == nio)
         break;

   if (i == ETHSW_MAX_NIO)
      goto error;

   /* Invalidate this port in the MAC address table */
   ethsw_invalidate_port(t,nio);
   t->nio[i] = NULL;
   
   ETHSW_UNLOCK(t);

   /* Remove the NIO from the RX multiplexer */
   ethsw_free_nio(nio);
   return(0);

 error:  
   ETHSW_UNLOCK(t);
   return(-1);
}

/* Clear the MAC address table */
int ethsw_clear_mac_addr_table(ethsw_table_t *t)
{
   ETHSW_LOCK(t);
   ethsw_invalidate(t);
   ETHSW_UNLOCK(t);
   return(0);
}

/* Iterate over all entries of the MAC address table */
int ethsw_iterate_mac_addr_table(ethsw_table_t *t,ethsw_foreach_entry_t cb,
                                 void *opt_arg)
{
   ethsw_mac_entry_t *entry;
   int i;

   ETHSW_LOCK(t);

   for(i=0;i<ETHSW_HASH_SIZE;i++) {
      entry = &t->mac_addr_table[i];
      
      if (!entry->nio)
         continue;

      cb(t,entry,opt_arg);
   }

   ETHSW_UNLOCK(t);
   return(0);
}

/* Set port as an access port */
int ethsw_set_access_port(ethsw_table_t *t,char *nio_name,u_int vlan_id)
{
   int i,res = -1;

   ETHSW_LOCK(t);
   
   for(i=0;i<ETHSW_MAX_NIO;i++)
      if (t->nio[i] && !strcmp(t->nio[i]->name,nio_name)) {
         set_access_port(t->nio[i],vlan_id);
         res = 0;
         break;
      }

   ETHSW_UNLOCK(t);
   return(res);
}

/* Set port as a 802.1q trunk port */
int ethsw_set_dot1q_port(ethsw_table_t *t,char *nio_name,u_int native_vlan)
{
   int i,res = -1;

   ETHSW_LOCK(t);
   
   for(i=0;i<ETHSW_MAX_NIO;i++)
      if (t->nio[i] && !strcmp(t->nio[i]->name,nio_name)) {
         set_dot1q_port(t->nio[i],native_vlan);
         res = 0;
         break;
      }

   ETHSW_UNLOCK(t);
   return(res);
}

/* Save the configuration of a switch */
void ethsw_save_config(ethsw_table_t *t,FILE *fd)
{
   netio_desc_t *nio;
   int i;
   
   fprintf(fd,"ethsw create %s\n",t->name);

   ETHSW_LOCK(t);

   for(i=0;i<ETHSW_MAX_NIO;i++)
   {
      nio = t->nio[i];

      fprintf(fd,"ethsw add_nio %s %s\n",t->name,nio->name);

      switch(nio->vlan_port_type) {
         case ETHSW_PORT_TYPE_ACCESS:
            fprintf(fd,"ethsw set_access_port %s %s %u\n",
                    t->name,nio->name,nio->vlan_id);
            break;

         case ETHSW_PORT_TYPE_DOT1Q:
            fprintf(fd,"ethsw set_dot1q_port %s %s %u\n",
                    t->name,nio->name,nio->vlan_id);
            break;

         default:
            fprintf(stderr,"ethsw_save_config: unknown port type %u\n",
                    nio->vlan_port_type);
      }
   }

   ETHSW_UNLOCK(t);

   fprintf(fd,"\n");
}

/* Save configurations of all Ethernet switches */
static void ethsw_reg_save_config(registry_entry_t *entry,void *opt,int *err)
{
   ethsw_save_config((ethsw_table_t *)entry->data,(FILE *)opt);
}

void ethsw_save_config_all(FILE *fd)
{
   registry_foreach_type(OBJ_TYPE_ETHSW,ethsw_reg_save_config,fd,NULL);
}

/* Free resources used by a virtual ethernet switch */
static int ethsw_free(void *data,void *arg)
{
   ethsw_table_t *t = data;
   int i;

   for(i=0;i<ETHSW_MAX_NIO;i++) {
      if (!t->nio[i])
         continue;

      ethsw_free_nio(t->nio[i]);
   }

   free(t->name);
   free(t);
   return(TRUE);
}

/* Delete a virtual ethernet switch */
int ethsw_delete(char *name)
{
   return(registry_delete_if_unused(name,OBJ_TYPE_ETHSW,ethsw_free,NULL));
}

/* Delete all virtual ethernet switches */
int ethsw_delete_all(void)
{
   return(registry_delete_type(OBJ_TYPE_ETHSW,ethsw_free,NULL));
}

/* Create a new interface */
static int ethsw_cfg_create_if(ethsw_table_t *t,char **tokens,int count)
{
   netio_desc_t *nio = NULL;
   int nio_type;

   nio_type = netio_get_type(tokens[2]);
   switch(nio_type) {
      case NETIO_TYPE_UNIX:
         if (count != 5) {
            fprintf(stderr,"ETHSW: invalid number of arguments "
                    "for UNIX NIO\n");
            break;
         }

         nio = netio_desc_create_unix(tokens[1],tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TAP:
         if (count != 4) {
            fprintf(stderr,"ETHSW: invalid number of arguments "
                    "for TAP NIO\n");
            break;
         }

         nio = netio_desc_create_tap(tokens[1],tokens[3]);
         break;

      case NETIO_TYPE_UDP:
         if (count != 6) {
            fprintf(stderr,"ETHSW: invalid number of arguments "
                    "for UDP NIO\n");
            break;
         }

         nio = netio_desc_create_udp(tokens[1],atoi(tokens[3]),
                                     tokens[4],atoi(tokens[5]));
         break;

      case NETIO_TYPE_TCP_CLI:
         if (count != 5) {
            fprintf(stderr,"ETHSW: invalid number of arguments "
                    "for TCP CLI NIO\n");
            break;
         }

         nio = netio_desc_create_tcp_cli(tokens[1],tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TCP_SER:
         if (count != 4) {
            fprintf(stderr,"ETHSW: invalid number of arguments "
                    "for TCP SER NIO\n");
            break;
         }

         nio = netio_desc_create_tcp_ser(tokens[1],tokens[3]);
         break;

#ifdef GEN_ETH
      case NETIO_TYPE_GEN_ETH:
         if (count != 4) {
            fprintf(stderr,"ETHSW: invalid number of arguments "
                    "for Generic Ethernet NIO\n");
            break;
         }
         
         nio = netio_desc_create_geneth(tokens[1],tokens[3]);
         break;
#endif

#ifdef LINUX_ETH
      case NETIO_TYPE_LINUX_ETH:
         if (count != 4) {
            fprintf(stderr,"ETHSW: invalid number of arguments "
                    "for Linux Ethernet NIO\n");
            break;
         }
         
         nio = netio_desc_create_lnxeth(tokens[1],tokens[3]);
         break;
#endif

      default:
         fprintf(stderr,"ETHSW: unknown/invalid NETIO type '%s'\n",
                 tokens[2]);
   }

   if (!nio) {
      fprintf(stderr,"ETHSW: unable to create NETIO descriptor\n");
      return(-1);
   }

   if (ethsw_add_netio(t,tokens[1]) == -1) {
      fprintf(stderr,"ETHSW: unable to add NETIO descriptor.\n");
      netio_release(nio->name);
      return(-1);
   }

   netio_release(nio->name);
   return(0);
}

/* Set a port as an access port */
static int ethsw_cfg_set_access_port(ethsw_table_t *t,char **tokens,int count)
{
   /* 3 parameters: "ACCESS", IF, VLAN */
   if (count != 3) {
      fprintf(stderr,"ETHSW: invalid access port description.\n");
      return(-1);
   }

   return(ethsw_set_access_port(t,tokens[1],atoi(tokens[2])));
}

/* Set a port as a 802.1q trunk port */
static int ethsw_cfg_set_dot1q_port(ethsw_table_t *t,char **tokens,int count)
{
   /* 3 parameters: "DOT1Q", IF, Native VLAN */
   if (count != 3) {
      fprintf(stderr,"ETHSW: invalid trunk port description.\n");
      return(-1);
   }

   return(ethsw_set_dot1q_port(t,tokens[1],atoi(tokens[2])));
}

#define ETHSW_MAX_TOKENS  16

/* Handle a ETHSW configuration line */
static int ethsw_handle_cfg_line(ethsw_table_t *t,char *str)
{  
   char *tokens[ETHSW_MAX_TOKENS];
   int count;

   if ((count = m_strsplit(str,':',tokens,ETHSW_MAX_TOKENS)) <= 1)
      return(-1);

   if (!strcmp(tokens[0],"IF"))
      return(ethsw_cfg_create_if(t,tokens,count));
   else if (!strcmp(tokens[0],"ACCESS"))
      return(ethsw_cfg_set_access_port(t,tokens,count));
   else if (!strcmp(tokens[0],"DOT1Q"))
      return(ethsw_cfg_set_dot1q_port(t,tokens,count));

   fprintf(stderr,
           "ETHSW: Unknown statement \"%s\" (allowed: IF,ACCESS,TRUNK)\n",
           tokens[0]);
   return(-1);
}

/* Read a ETHSW configuration file */
static int ethsw_read_cfg_file(ethsw_table_t *t,char *filename)
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
         ethsw_handle_cfg_line(t,buffer);
   }
   
   fclose(fd);
   return(0);
}

/* Start a virtual Ethernet switch */
int ethsw_start(char *filename)
{
   ethsw_table_t *t;

   if (!(t = ethsw_create("default"))) {
      fprintf(stderr,"ETHSW: unable to create virtual fabric table.\n");
      return(-1);
   }

   if (ethsw_read_cfg_file(t,filename) == -1) {
      fprintf(stderr,"ETHSW: unable to parse configuration file.\n");
      return(-1);
   }
   
   ethsw_release("default");
   return(0);
}
