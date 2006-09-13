/*
 * Copyright (c) 2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * gen_eth: module used to send/receive Ethernet packets.
 *
 * Use libpcap (0.9+) to receive and send packets.
 */

#ifndef __GEN_ETH_H__
#define __GEN_ETH_H__  1

#include <sys/types.h>
#include <pcap.h>

/* Initialize a generic ethernet driver */
pcap_t *gen_eth_init(char *device);

/* Free resources of a generic ethernet driver */
void gen_eth_close(pcap_t *p);

/* Send an ethernet frame */
ssize_t gen_eth_send(pcap_t *p,char *buffer,size_t len);

/* Receive an ethernet frame */
ssize_t gen_eth_recv(pcap_t *p,char *buffer,size_t len);

/* Display Ethernet interfaces of the system */
int gen_eth_show_dev_list(void);

#endif
