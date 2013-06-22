/*
 * Copyright (c) 2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * linux_eth.c: module used to send/receive Ethernet packets.
 *
 * Specific to the Linux operating system.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <sys/ioctl.h>
#include <netinet/if_ether.h>
#include <linux/if.h>
#include <linux/if_packet.h>

#include "linux_eth.h"

/* Get interface index of specified device */
int lnx_eth_get_dev_index(char *name)
{
   struct ifreq if_req;
   int fd;

   /* Create dummy file descriptor */
   if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      fprintf(stderr,"eth_get_dev_index: socket: %s\n",strerror(errno));
      return(-1);
   }

   memset((void *)&if_req,0,sizeof(if_req));
   strcpy(if_req.ifr_name,name);

   if (ioctl(fd,SIOCGIFINDEX,&if_req) < 0) {
      fprintf(stderr,"eth_get_dev_index: SIOCGIFINDEX: %s\n",strerror(errno));
      close(fd);
      return(-1);
   }

   close(fd);
   return(if_req.ifr_ifindex);
}

/* Initialize a new ethernet raw socket */
int lnx_eth_init_socket(char *device)
{
   struct sockaddr_ll sa;
   struct packet_mreq mreq;
   int sck;

   if ((sck = socket(PF_PACKET,SOCK_RAW,htons(ETH_P_ALL))) == -1) {
      fprintf(stderr,"eth_init_socket: socket: %s\n",strerror(errno));
      return(-1);
   }

   memset(&sa,0,sizeof(struct sockaddr_ll));
   sa.sll_family = AF_PACKET;
   sa.sll_protocol = htons(ETH_P_ALL);
   sa.sll_hatype = ARPHRD_ETHER;
   sa.sll_halen = ETH_ALEN;
   sa.sll_ifindex = lnx_eth_get_dev_index(device);

   memset(&mreq,0,sizeof(mreq));
   mreq.mr_ifindex = sa.sll_ifindex;
   mreq.mr_type = PACKET_MR_PROMISC;

   if (bind(sck,(struct sockaddr *)&sa,sizeof(struct sockaddr_ll)) == -1) {
      fprintf(stderr,"eth_init_socket: bind: %s\n",strerror(errno));
      return(-1);
   }

   if (setsockopt(sck,SOL_PACKET,PACKET_ADD_MEMBERSHIP,
                  &mreq,sizeof(mreq)) == -1) 
   {
      fprintf(stderr,"eth_init_socket: setsockopt: %s\n",strerror(errno));
      return(-1);
   }

   return(sck);
}

/* Send an ethernet frame */
ssize_t lnx_eth_send(int sck,int dev_id,char *buffer,size_t len)
{
   struct sockaddr_ll sa;

   memset(&sa,0,sizeof(struct sockaddr_ll));
   sa.sll_family = AF_PACKET;
   sa.sll_protocol = htons(ETH_P_ALL);
   sa.sll_hatype = ARPHRD_ETHER;
   sa.sll_halen = ETH_ALEN;
   sa.sll_ifindex = dev_id;

   return(sendto(sck,buffer,len,0,(struct sockaddr *)&sa,sizeof(sa)));
}

/* Receive an ethernet frame */
ssize_t lnx_eth_recv(int sck,char *buffer,size_t len)
{
   return(recv(sck,buffer,len,0));
}
