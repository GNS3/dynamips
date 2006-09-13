/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * NetIO Filtering.
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
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>

#include "registry.h"
#include "net.h"
#include "net_io.h"
#include "net_io_filter.h"

/* Filter list */
static netio_pktfilter_t *pf_list = NULL;

/* Find a filter */
netio_pktfilter_t *netio_filter_find(char *name)
{
   netio_pktfilter_t *pf;

   for(pf=pf_list;pf;pf=pf->next)
      if (!strcmp(pf->name,name))
         return pf;

   return NULL;
}

/* Add a new filter */
int netio_filter_add(netio_pktfilter_t *pf)
{
   if (netio_filter_find(pf->name) != NULL)
      return(-1);

   pf->next = pf_list;
   pf_list = pf;
   return(0);
}

/* Bind a filter to a NIO */
int netio_filter_bind(netio_desc_t *nio,int direction,char *pf_name)
{
   netio_pktfilter_t *pf;

   if (!(pf = netio_filter_find(pf_name)))
      return(-1);

   if (direction == NETIO_FILTER_DIR_RX) {
      nio->rx_filter_data = NULL;
      nio->rx_filter = pf;
   } else {
      nio->tx_filter_data = NULL;
      nio->tx_filter = pf;
   }

   return(0);
}

/* Unbind a filter from a NIO */
int netio_filter_unbind(netio_desc_t *nio,int direction)
{  
   netio_pktfilter_t *pf;
   void **opt;

   if (direction == NETIO_FILTER_DIR_RX) {
      opt = &nio->rx_filter_data;
      pf  = nio->rx_filter;
   } else {
      opt = &nio->tx_filter_data;
      pf  = nio->tx_filter;
   }

   if (!pf)
      return(-1);

   pf->free(nio,opt);
   return(0);
}

/* Setup a filter */
int netio_filter_setup(netio_desc_t *nio,int direction,int argc,char *argv[])
{
   netio_pktfilter_t *pf;
   void **opt;

   if (direction == NETIO_FILTER_DIR_RX) {
      opt = &nio->rx_filter_data;
      pf  = nio->rx_filter;
   } else {
      opt = &nio->tx_filter_data;
      pf  = nio->tx_filter;
   }

   if (!pf)
      return(-1);

   return(pf->setup(nio,opt,argc,argv));
}

/* ======================================================================== */
/* Frequency Dropping ("freq_drop").                                        */
/* ======================================================================== */

struct pf_freqdrop_data {
   int frequency;
   int current;
};

/* Setup filter ressources */
static int pf_freqdrop_setup(netio_desc_t *nio,void **opt,
                             int argc,char *argv[])
{
   struct pf_freqdrop_data *data = *opt;

   if (argc != 1)
      return(-1);

   if (!data) {
      if (!(data = malloc(sizeof(*data))))
         return(-1);

      *opt = data;
   }

   data->current = 0;
   data->frequency = atoi(argv[0]);
   return(0);
}

/* Free ressources used by filter */
static void pf_freqdrop_free(netio_desc_t *nio,void **opt)
{
   if (*opt)
      free(*opt);

   *opt = NULL;
}

/* Packet handler: drop 1 out of n packets */
static int pf_freqdrop_pkt_handler(netio_desc_t *nio,void *pkt,size_t len,
                                   void *opt)
{
   struct pf_freqdrop_data *data = opt;

   if (data != NULL) {
      switch(data->frequency) {
         case -1:
            return(NETIO_FILTER_ACTION_DROP);
         case 0:
            return(NETIO_FILTER_ACTION_PASS);
         default:
            data->current++;
         
            if (data->current == data->frequency) {
               data->current = 0;
               return(NETIO_FILTER_ACTION_DROP);
            }
      }
   }

   return(NETIO_FILTER_ACTION_PASS);
}

/* Packet dropping at 1/n frequency */
static netio_pktfilter_t pf_freqdrop_def = {
   "freq_drop",
   pf_freqdrop_setup,
   pf_freqdrop_free,
   pf_freqdrop_pkt_handler,
   NULL,
};

/* ======================================================================== */
/* Initialization of packet filters.                                        */
/* ======================================================================== */

void netio_filter_load_all(void)
{
   netio_filter_add(&pf_freqdrop_def);
}
