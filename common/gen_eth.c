/*
 * Copyright (c) 2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * gen_eth.c: module used to send/receive Ethernet packets.
 *
 * Use libpcap (0.9+) or WinPcap (0.4alpha1+) to receive and send packets.
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
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#ifdef CYGWIN
/* Needed for pcap_open() flags */
#define HAVE_REMOTE
#endif

#include "pcap.h"
#include "utils.h"
#include "gen_eth.h"

/* Initialize a generic ethernet driver */
pcap_t *gen_eth_init(char *device)
{
   char pcap_errbuf[PCAP_ERRBUF_SIZE];
   pcap_t *p;

#ifndef CYGWIN
   if (!(p = pcap_open_live(device,2048,TRUE,10,pcap_errbuf)))
      goto pcap_error;

   pcap_setdirection(p,PCAP_D_INOUT);
#ifdef BIOCFEEDBACK
   {
     int on = 1;
     ioctl(pcap_fileno(p), BIOCFEEDBACK, &on);
   }
#endif
#else
   p = pcap_open(device,2048,
                 PCAP_OPENFLAG_PROMISCUOUS | 
                 PCAP_OPENFLAG_NOCAPTURE_LOCAL |
		 PCAP_OPENFLAG_MAX_RESPONSIVENESS |
		 PCAP_OPENFLAG_NOCAPTURE_RPCAP,
		 10,NULL,pcap_errbuf);
		 
   if (!p)
      goto pcap_error;
#endif

   return p;
   
 pcap_error:
   fprintf(stderr,"gen_eth_init: unable to open device '%s' "
           "with PCAP (%s)\n",device,pcap_errbuf);
   return NULL;
}

/* Free resources of a generic ethernet driver */
void gen_eth_close(pcap_t *p)
{
   pcap_close(p);
}

/* Send an ethernet frame */
ssize_t gen_eth_send(pcap_t *p,char *buffer,size_t len)
{
   return(pcap_sendpacket(p,(u_char *)buffer,len));
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
   int res;

   printf("Network device list:\n\n");

#ifndef CYGWIN
   res = pcap_findalldevs(&dev_list,pcap_errbuf);
#else
   res = pcap_findalldevs_ex(PCAP_SRC_IF_STRING,NULL,&dev_list,pcap_errbuf);
#endif

   if (res < 0) {
      fprintf(stderr,"PCAP: unable to find device list (%s)\n",pcap_errbuf);
      return(-1);
   }

   for(dev=dev_list;dev;dev=dev->next) {
      printf("   %s : %s\n",
             dev->name,
             dev->description ? dev->description : "no info provided");
   }

   printf("\n");

   pcap_freealldevs(dev_list);
   return(0);
}
