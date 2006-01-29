/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * NetIO bridge.
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
#include "net_io.h"
#include "net_io_bridge.h"

#define PKT_MAX_SIZE 2048

int netio_bridge_fabric(netio_bridge_t *t)
{
   u_char pkt[PKT_MAX_SIZE];
   int i,j,res,fd,max_fd=-1;
   ssize_t len;
   fd_set rfds;

   for(;;) {
      FD_ZERO(&rfds);
   
      for(i=0;i<NETIO_BRIDGE_MAX_NIO;i++)
         if (t->nio[i] != NULL) {
            fd = netio_get_fd(t->nio[i]);
            if (fd != -1) {
               if (fd > max_fd) max_fd = fd;
               FD_SET(fd,&rfds);
            }
         }

      res = select(max_fd+1,&rfds,NULL,NULL,NULL);
      
      if (res == -1)
         continue;

      for(i=0;i<NETIO_BRIDGE_MAX_NIO;i++)
         if (t->nio[i] != NULL) {
            fd = netio_get_fd(t->nio[i]);

            if ((fd != -1) && FD_ISSET(fd,&rfds)) {
               len = netio_recv(t->nio[i],pkt,PKT_MAX_SIZE);

               for(j=0;j<NETIO_BRIDGE_MAX_NIO;j++) {
                  if ((t->nio[j] != NULL) && (j != i))
                     netio_send(t->nio[j],pkt,len);                     
               }
            }
         }
   }

   return(0);
}

/* Add a NetIO descriptor to an virtual bridge */
int netio_bridge_add_netio(netio_bridge_t *t,netio_desc_t *nio)
{
   int i;

   /* try to find a free slot */
   for(i=0;i<NETIO_BRIDGE_MAX_NIO;i++)
      if (t->nio[i] == NULL)
         break;
   
   if (i == NETIO_BRIDGE_MAX_NIO)
      return(-1);

   t->nio[i] = nio;
   return(0);
}

/* Create a new interface */
int netio_bridge_cfg_create_if(netio_bridge_t *t,char **tokens,int count)
{
   netio_desc_t *nio = NULL;
   int nio_type;

   nio_type = netio_get_type(tokens[0]);
   switch(nio_type) {
      case NETIO_TYPE_UNIX:
         if (count != 3) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for UNIX NIO\n");
            break;
         }

         nio = netio_desc_create_unix(tokens[1],tokens[2]);
         break;

      case NETIO_TYPE_TAP:
         if (count != 2) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for TAP NIO\n");
            break;
         }

         nio = netio_desc_create_tap(tokens[1]);
         break;

      case NETIO_TYPE_UDP:
         if (count != 4) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for UDP NIO\n");
            break;
         }

         nio = netio_desc_create_udp(atoi(tokens[1]),tokens[2],
                                     atoi(tokens[3]));
         break;

      case NETIO_TYPE_TCP_CLI:
         if (count != 3) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for TCP CLI NIO\n");
            break;
         }

         nio = netio_desc_create_tcp_cli(tokens[1],tokens[2]);
         break;

      case NETIO_TYPE_TCP_SER:
         if (count != 2) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for TCP SER NIO\n");
            break;
         }

         nio = netio_desc_create_tcp_ser(tokens[1]);
         break;

#ifdef LINUX_ETH
      case NETIO_TYPE_LINUX_ETH:
         if (count != 2) {
            fprintf(stderr,"NETIO_BRIDGE: invalid number of arguments "
                    "for Linux Eth NIO\n");
            break;
         }
         
         nio = netio_desc_create_lnxeth(tokens[1]);
         break;
#endif

      default:
         fprintf(stderr,"NETIO_BRIDGE: unknown/invalid NETIO type '%s'\n",
                 tokens[2]);
   }

   if (!nio) {
      fprintf(stderr,"NETIO_BRIDGE: unable to create NETIO descriptor");
      return(-1);
   }

   if (netio_bridge_add_netio(t,nio) == -1) {
      fprintf(stderr,"NETIO_BRIDGE: unable to add NETIO descriptor.\n");
      return(-1);
   }

   return(0);
}

#define NETIO_BRIDGE_MAX_TOKENS  16

/* Handle a configuration line */
int netio_bridge_handle_cfg_line(netio_bridge_t *t,char *str)
{  
   char *tokens[NETIO_BRIDGE_MAX_TOKENS];
   int count;

   if ((count = strsplit(str,':',tokens,NETIO_BRIDGE_MAX_TOKENS)) <= 1)
      return(-1);

   return(netio_bridge_cfg_create_if(t,tokens,count));
}

/* Read a configuration file */
int netio_bridge_read_cfg_file(netio_bridge_t *t,char *filename)
{
   char buffer[1024],*ptr;
   FILE *fd;

   if (!(fd = fopen(filename,"r"))) {
      perror("fopen");
      return(-1);
   }
   
   while(!feof(fd)) {
      fgets(buffer,sizeof(buffer),fd);
      
      /* skip comments */
      if ((ptr = strchr(buffer,'#')) != NULL)
         *ptr = 0;

      /* skip end of line */
      if ((ptr = strchr(buffer,'\n')) != NULL)
         *ptr = 0;

      /* analyze non-empty lines */
      if (strchr(buffer,':'))
         netio_bridge_handle_cfg_line(t,buffer);
   }
   
   fclose(fd);
   return(0);
}

/* Virtual bridge thread */
void *netio_bridge_thread_main(void *arg)
{
   netio_bridge_t *t = arg;

   printf("Started Virtual Bridge.\n");
   netio_bridge_fabric(t);
   return NULL;
}

/* Start a virtual bridge */
int netio_bridge_start(char *filename)
{
   netio_bridge_t *t;

   if (!(t = malloc(sizeof(*t)))) {
      fprintf(stderr,"NETIO_BRIDGE: unable to create bridge.\n");
      return(-1);
   }

   memset(t,0,sizeof(*t));

   if (netio_bridge_read_cfg_file(t,filename) == -1) {
      fprintf(stderr,"NETIO_BRIDGE: unable to parse configuration file.\n");
      return(-1);
   }

   if (pthread_create(&t->thread,NULL,netio_bridge_thread_main,t) != 0) {
      fprintf(stderr,"NETIO_BRIDGE: unable to create thread.\n");
      return(-1);
   }
   
   return(0);
}
