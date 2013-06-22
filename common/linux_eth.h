/*
 * Copyright (c) 2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * linux_eth.c: module used to send/receive Ethernet packets.
 *
 * Specific to the Linux operating system.
 */

#ifndef __LINUX_ETH_H__
#define __LINUX_ETH_H__  1

#include <sys/types.h>

/* Get interface index of specified device */
int lnx_eth_get_dev_index(char *name);

/* Initialize a new ethernet raw socket */
int lnx_eth_init_socket(char *device);

/* Send an ethernet frame */
ssize_t lnx_eth_send(int sck,int dev_id,char *buffer,size_t len);

/* Receive an ethernet frame */
ssize_t lnx_eth_recv(int sck,char *buffer,size_t len);

#endif
