/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Virtual Ethernet switch definitions.
 */

#ifndef __ETH_SWITCH_H__
#define __ETH_SWITCH_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "net_io.h"

/* Hash entries for the MAC address table */
#define ETHSW_HASH_SIZE  4096

/* Maximum port number */
#define ETHSW_MAX_NIO    64

/* Maximum packet size */
#define ETHSW_MAX_PKT_SIZE  2048

/* Port types: access or 802.1Q */
enum {
   ETHSW_PORT_TYPE_ACCESS = 1,
   ETHSW_PORT_TYPE_DOT1Q,
   ETHSW_PORT_TYPE_ISL,
};

/* Received packet */
typedef struct ethsw_packet ethsw_packet_t;
struct ethsw_packet {
   u_char *pkt;
   ssize_t pkt_len;
   netio_desc_t *input_port;
   u_int input_vlan;
   int input_tag;
};

/* MAC address table entry */
typedef struct ethsw_mac_entry ethsw_mac_entry_t;
struct ethsw_mac_entry {
   netio_desc_t *nio;
   n_eth_addr_t mac_addr;
   m_uint16_t vlan_id;
};

/* Virtual Ethernet switch */
typedef struct ethsw_table ethsw_table_t;
struct ethsw_table {
   char *name;
   pthread_mutex_t lock;
   int debug;
   
   /* Virtual Ports */
   netio_desc_t *nio[ETHSW_MAX_NIO];

   /* MAC address table */
   ethsw_mac_entry_t mac_addr_table[ETHSW_HASH_SIZE];
};

/* Packet input vector */
typedef void (*ethsw_input_vector_t)(ethsw_table_t *t,ethsw_packet_t *sp,
                                     netio_desc_t *output_port);

/* "foreach" vector */
typedef void (*ethsw_foreach_entry_t)(ethsw_table_t *t,
                                      ethsw_mac_entry_t *entry,
                                      void *opt);

#define ETHSW_LOCK(t)   pthread_mutex_lock(&(t)->lock)
#define ETHSW_UNLOCK(t) pthread_mutex_unlock(&(t)->lock)

/* Acquire a reference to an Ethernet switch (increment reference count) */
ethsw_table_t *ethsw_acquire(char *name);

/* Release an Ethernet switch (decrement reference count) */
int ethsw_release(char *name);

/* Create a virtual ethernet switch */
ethsw_table_t *ethsw_create(char *name);

/* Add a NetIO descriptor to a virtual ethernet switch */
int ethsw_add_netio(ethsw_table_t *t,char *nio_name);

/* Remove a NetIO descriptor from a virtual ethernet switch */
int ethsw_remove_netio(ethsw_table_t *t,char *nio_name);

/* Clear the MAC address table */
int ethsw_clear_mac_addr_table(ethsw_table_t *t);

/* Iterate over all entries of the MAC address table */
int ethsw_iterate_mac_addr_table(ethsw_table_t *t,ethsw_foreach_entry_t cb,
                                 void *opt_arg);

/* Set port as an access port */
int ethsw_set_access_port(ethsw_table_t *t,char *nio_name,u_int vlan_id);

/* Set port as a 802.1q trunk port */
int ethsw_set_dot1q_port(ethsw_table_t *t,char *nio_name,u_int native_vlan);

/* Save the configuration of a switch */
void ethsw_save_config(ethsw_table_t *t,FILE *fd);

/* Save configurations of all Ethernet switches */
void ethsw_save_config_all(FILE *fd);

/* Delete a virtual ethernet switch */
int ethsw_delete(char *name);

/* Delete all virtual ethernet switches */
int ethsw_delete_all(void);

/* Start a virtual Ethernet switch */
int ethsw_start(char *filename);

#endif
