/*
 * Copyright (c) 2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * gen_eth.c: module used to send/receive Ethernet packets.
 *
 * Use libpcap (0.9+) or WinPcap to receive and send packets.
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

#include "utils.h"
#include "gen_eth.h"

/* Initialize a generic ethernet driver */
pcap_t *gen_eth_init(char *device)
{
   char pcap_errbuf[PCAP_ERRBUF_SIZE];
   pcap_t *p;

   if (!(p = pcap_open_live(device,2048,1,10,pcap_errbuf))) {
      fprintf(stderr,"gen_eth_init: unable to open device '%s' "
              "with PCAP (%s)\n",device,pcap_errbuf);
      return NULL;
   }

   return p;
}

/* Free ressources of a generic ethernet driver */
void gen_eth_close(pcap_t *p)
{
   pcap_close(p);
}

/* Send an ethernet frame */
ssize_t gen_eth_send(pcap_t *p,char *buffer,size_t len)
{
   return(pcap_sendpacket(p,buffer,len));
}

/* Receive an ethernet frame */
ssize_t gen_eth_recv(pcap_t *p,char *buffer,size_t len)
{
   struct pcap_pkthdr pkt_info;
   u_char *pkt_ptr;
   ssize_t rlen;

   if (!(pkt_ptr = (u_char *)pcap_next(p,&pkt_info)))
      return(-1);

   rlen = m_min(len,pkt_info.caplen);
   memcpy(buffer,pkt_ptr,rlen);
   return(rlen);
}

/* Display Ethernet interfaces of the system */
int gen_eth_show_dev_list(void)
{
   char pcap_errbuf[PCAP_ERRBUF_SIZE];
   pcap_if_t *dev_list,*dev;

   printf("Network device list:\n\n");

   if (pcap_findalldevs(&dev_list,pcap_errbuf) < 0) {
      fprintf(stderr,"PCAP: unable to find device list (%s)\n",pcap_errbuf);
      return(-1);
   }

   for(dev=dev_list;dev;dev=dev->next) {
      printf("   %s: %s\n",
             dev->name,
             dev->description ? dev->description : "no info provided");
   }

   printf("\n");

   pcap_freealldevs(dev_list);
   return(0);
}
