/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Frame-Relay switch.
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
#include "frame_relay.h"

#define DEBUG_FRSW    0

/* Number of LMI trailing bytes */
#define LMI_TRAILING_SIZE  3

extern FILE *log_file;

/* ANSI LMI packet header */   
static const m_uint8_t lmi_ansi_hdr[] = { 
   0x00, 0x01, 0x03, 0x08, 0x00, 0x75, 0x95,
};

/* DLCI hash function */
static inline u_int frsw_dlci_hash(u_int dlci)
{
   return((dlci ^ (dlci >> 8)) & (FRSW_HASH_SIZE-1));
}

/* DLCI lookup */
fr_swconn_t *frsw_dlci_lookup(fr_sw_table_t *t,netio_desc_t *input,u_int dlci)
{
   fr_swconn_t *vc;
   
   for(vc=t->dlci_table[frsw_dlci_hash(dlci)];vc;vc=vc->next)
      if (vc->dlci_in == dlci)
         return vc;

   return NULL;
}

/* Create a switch connection */
int frsw_create_vc(fr_sw_table_t *t,netio_desc_t *input,u_int dlci_in,
                   netio_desc_t *output,u_int dlci_out)
{
   fr_swconn_t *vc;
   u_int hbucket;

   if (!(vc = frsw_dlci_lookup(t,input,dlci_in))) {
      if (!(vc = malloc(sizeof(*vc))))
         return(-1);

      memset(vc,0,sizeof(*vc));
      hbucket = frsw_dlci_hash(dlci_in);
      vc->next = t->dlci_table[hbucket];
      t->dlci_table[hbucket] = vc;
   }

   vc->input    = input;
   vc->output   = output;
   vc->dlci_in  = dlci_in;
   vc->dlci_out = dlci_out;
   return(0);
}

/* Create a virtual switch table */
fr_sw_table_t *frsw_create_table(void)
{
   fr_sw_table_t *t;

   if (!(t = malloc(sizeof(*t))))
      return NULL;

   memset(t,0,sizeof(*t));
   return t;
}

/* Add a NetIO descriptor to a Frame-Relay switch table */
int frsw_add_netio(fr_sw_table_t *t,netio_desc_t *nio)
{
   int i;

   /* try to find a free slot */
   for(i=0;i<FRSW_NIO_MAX;i++)
      if (t->nio[i] == NULL)
         break;
   
   if (i == FRSW_NIO_MAX)
      return(-1);

   t->nio[i] = nio;
   return(0);
}

/* Find a NetIO descriptor given its name */
netio_desc_t *frsw_find_netio_by_name(fr_sw_table_t *t,char *name)
{
   int i;

   for(i=0;i<FRSW_NIO_MAX;i++)
      if ((t->nio[i] != NULL) && !strcmp(t->nio[i]->name,name))
         return t->nio[i];

   return NULL;
}

/* Handle a ANSI LMI packet */
ssize_t frsw_handle_lmi_ansi_pkt(fr_sw_table_t *t,netio_desc_t *input,
                                 m_uint8_t *pkt,ssize_t len)
{
   m_uint8_t resp[FR_MAX_PKT_SIZE],*pres,*preq;
   m_uint8_t itype,isize;
   int i,msg_type,seq_ok;
   ssize_t rlen;
   fr_swconn_t *sc;
   u_int dlci;

   if ((len <= (sizeof(lmi_ansi_hdr) + LMI_TRAILING_SIZE)) || 
       memcmp(pkt,lmi_ansi_hdr,sizeof(lmi_ansi_hdr)))
      return(-1);

   len -= LMI_TRAILING_SIZE;

#if DEBUG_FRSW
   m_log(input->name,"received an ANSI LMI packet:\n");
   mem_dump(log_file,pkt,len);
#endif

   /* Prepare response packet */
   memcpy(resp,lmi_ansi_hdr,sizeof(lmi_ansi_hdr));
   resp[FR_LMI_ANSI_STATUS_OFFSET] = FR_LMI_ANSI_STATUS;

   preq = &pkt[sizeof(lmi_ansi_hdr)];
   pres = &resp[sizeof(lmi_ansi_hdr)];
   
   msg_type = -1;
   seq_ok = FALSE;

   while((preq + 2) < (pkt + len)) {
      /* get item type and size */
      itype = preq[0];
      isize = preq[1];

      /* check packet boundary */
      if ((preq + isize + 2) > (pkt + len)) {
         m_log(input->name,"invalid LMI packet:\n");
         mem_dump(log_file,pkt,len);
         return(-1);
      }

      switch(itype) {
         case 0x01:   /* report information element */
            if (isize != 1) {
               m_log(input->name,"invalid LMI item size.\n");
               return(-1);
            }

            if ((msg_type = preq[2]) > 1) {
               m_log(input->name,"unknown LMI report type 0x%x.\n",msg_type);
               return(-1);
            }

            pres[0] = 0x01;
            pres[1] = 0x01;
            pres[2] = msg_type;
            pres += 3;
            break;

         case 0x03:   /* sequences */
            if (isize != 2) {
               m_log(input->name,"invalid LMI item size.\n");
               return(-1);
            }

            pres[0] = 0x03;
            pres[1] = 0x02;

            input->fr_lmi_seq++;
            if (!input->fr_lmi_seq) input->fr_lmi_seq++;

            pres[2] = input->fr_lmi_seq;
            pres[3] = preq[2];

#if DEBUG_FRSW
            m_log(input->name,"iSSN=0x%x, iRSN=0x%x, oSSN=0x%x, oRSN=0x%x\n",
                  preq[2],preq[3],pres[2],pres[3]);
#endif
            pres += 4;
            seq_ok = TRUE;
            break;
            
         default:
            m_log(input->name,"unknown LMI item type %u\n",itype);
            goto done;
      }

      /* proceed next item */
      preq += isize + 2;
   }

 done:
   if ((msg_type == -1) || !seq_ok) {
       m_log(input->name,"incomplete LMI packet.\n");
       return(-1);
   }

   /* full status, send DLCI info */
   if (msg_type == 0) {
#if DEBUG_FRSW
      m_log(input->name,"LMI full status, advertising DLCIs\n");
#endif
      for(i=0;i<FRSW_HASH_SIZE;i++) {
         for(sc=t->dlci_table[i];sc;sc=sc->next) {
            if (sc->input == input) {
               dlci = sc->dlci_in;

#if DEBUG_FRSW
               m_log(input->name,"sending LMI adv for DLCI %u\n",dlci);
#endif
               pres[0] = 0x07;
               pres[1] = 0x03;
               pres[2] = dlci >> 4;
               pres[3] = 0x80 | (dlci & 0x0f) << 3;
               pres[4] = 0x82;
               pres += 5;
            }
         }
      }
   }

   /* it seems that a trailing is required */
   memset(pres,0,LMI_TRAILING_SIZE);
   pres += LMI_TRAILING_SIZE;
   rlen = pres - resp;

#if DEBUG_FRSW
   m_log(input->name,"sending ANSI LMI packet:\n");
   mem_dump(log_file,resp,rlen);
#endif

   netio_send(input,resp,rlen);
   return(0);
}

/* DLCI switching */
void frsw_dlci_switch(fr_swconn_t *vc,m_uint8_t *pkt)
{
   pkt[0] = (pkt[0] & 0x03) | ((vc->dlci_out >> 4) << 2);
   pkt[1] = (pkt[1] & 0x0f) | ((vc->dlci_out & 0x0f) << 4);

   /* update the statistics counter */
   vc->count++;
}

/* Handle an Frame-Relay packet */
ssize_t frsw_handle_pkt(fr_sw_table_t *t,netio_desc_t *input,
                        m_uint8_t *pkt,ssize_t len)
{
   netio_desc_t *output = NULL;
   fr_swconn_t *vc;
   m_uint32_t dlci;
   ssize_t slen;

   /* Extract DLCI information */
   dlci =  ((pkt[0] & 0xfc) >> 2) << 4;
   dlci |= (pkt[1] & 0xf0) >> 4;

#if DEBUG_FRSW
   m_log(input->name,"Trying to switch packet with input DLCI %u.\n",dlci);
   mem_dump(log_file,pkt,len);
#endif

   /* LMI ? */
   if (dlci == FR_DLCI_LMI_ANSI)
      return(frsw_handle_lmi_ansi_pkt(t,input,pkt,len));

   /* DLCI switching */
   if ((vc = frsw_dlci_lookup(t,input,dlci)) != NULL) {
      frsw_dlci_switch(vc,pkt);
      output = vc->output;
   } 

   slen = netio_send(output,pkt,len);

   if (len != slen) {
      t->drop++;
      return(-1);
   }

   return(0);
}

/* Virtual Frame-Relay switch fabric */
int frsw_fabric(fr_sw_table_t *t)
{
   u_char pkt[FR_MAX_PKT_SIZE];
   int i,res,fd,max_fd=-1;
   ssize_t len;
   fd_set rfds;

   for(;;) {
      FD_ZERO(&rfds);
   
      for(i=0;i<FRSW_NIO_MAX;i++)
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

      for(i=0;i<FRSW_NIO_MAX;i++)
         if (t->nio[i] != NULL) {
            fd = netio_get_fd(t->nio[i]);

            if ((fd != -1) && FD_ISSET(fd,&rfds)) {
               len = netio_recv(t->nio[i],pkt,sizeof(pkt));
               frsw_handle_pkt(t,t->nio[i],pkt,len);
            }
         }
   }

   return(0);
}

/* Create a new interface */
int frsw_cfg_create_if(fr_sw_table_t *t,char **tokens,int count)
{
   netio_desc_t *nio = NULL;
   int nio_type;

   /* at least: IF, interface name, NetIO type */
   if (count < 3) {
      fprintf(stderr,"frsw_cfg_create_if: invalid interface description\n");
      return(-1);
   }
   
   /* check the interface name is not already taken */
   if (frsw_find_netio_by_name(t,tokens[1]) != NULL) {
      fprintf(stderr,"FRSW: interface %s already exists.\n",tokens[1]);
      return(-1);
   }

   nio_type = netio_get_type(tokens[2]);
   switch(nio_type) {
      case NETIO_TYPE_UNIX:
         if (count != 5) {
            fprintf(stderr,"FRSW: invalid number of arguments "
                    "for UNIX NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_unix(tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_UDP:
         if (count != 6) {
            fprintf(stderr,"FRSW: invalid number of arguments "
                    "for UDP NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_udp(atoi(tokens[3]),tokens[4],
                                     atoi(tokens[5]));
         break;

      case NETIO_TYPE_TCP_CLI:
         if (count != 5) {
            fprintf(stderr,"FRSW: invalid number of arguments "
                    "for TCP CLI NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_tcp_cli(tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TCP_SER:
         if (count != 4) {
            fprintf(stderr,"FRSW: invalid number of arguments "
                    "for TCP SER NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_tcp_ser(tokens[3]);
         break;

      default:
         fprintf(stderr,"FRSW: unknown/invalid NETIO type '%s'\n",
                 tokens[2]);
   }

   if (!nio || !(nio->name = strdup(tokens[1]))) {
      fprintf(stderr,"FRSW: unable to create NETIO descriptor of "
              "interface %s\n",tokens[1]);
      return(-1);
   }

   if (frsw_add_netio(t,nio) == -1) {
      fprintf(stderr,"FRSW: unable to add NETIO descriptor to SW table.\n");
      return(-1);
   }

   return(0);
}

/* Create a new virtual circuit */
int frsw_cfg_create_vc(fr_sw_table_t *t,char **tokens,int count)
{
   netio_desc_t *input,*output;

   /* 5 parameters: "VC", InputIF, InDLCI, OutputIF, OutDLCI */
   if (count != 5) {
      fprintf(stderr,"FRSW: invalid VPC descriptor.\n");
      return(-1);
   }

   if (!(input = frsw_find_netio_by_name(t,tokens[1]))) {
      fprintf(stderr,"FRSW: unknown interface \"%s\"\n",tokens[1]);
      return(-1);
   }

   if (!(output = frsw_find_netio_by_name(t,tokens[3]))) {
      fprintf(stderr,"FRSW: unknown interface \"%s\"\n",tokens[1]);
      return(-1);
   }

   return(frsw_create_vc(t,input,atoi(tokens[2]),output,atoi(tokens[4])));
}

#define FRSW_MAX_TOKENS  16

/* Handle a FRSW configuration line */
int frsw_handle_cfg_line(fr_sw_table_t *t,char *str)
{  
   char *tokens[FRSW_MAX_TOKENS];
   int count;

   if ((count = strsplit(str,':',tokens,FRSW_MAX_TOKENS)) <= 1)
      return(-1);

   if (!strcmp(tokens[0],"IF"))
      return(frsw_cfg_create_if(t,tokens,count));
   else if (!strcmp(tokens[0],"VC"))
      return(frsw_cfg_create_vc(t,tokens,count));

   fprintf(stderr,"FRSW: Unknown statement \"%s\" (allowed: IF,VC)\n",
           tokens[0]);
   return(-1);
}

/* Read a FRSW configuration file */
int frsw_read_cfg_file(fr_sw_table_t *t,char *filename)
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
         frsw_handle_cfg_line(t,buffer);
   }
   
   fclose(fd);
   return(0);
}

/* Virtual Frame-Relay switch thread */
void *frsw_thread_main(void *arg)
{
   fr_sw_table_t *t = arg;

   printf("Started Virtual Frame Relay switch fabric.\n");
   frsw_fabric(t);
   return NULL;
}

/* Start a virtual Frame-Relay switch */
int frsw_start(char *filename)
{
   fr_sw_table_t *t;

   if (!(t = frsw_create_table())) {
      fprintf(stderr,"FRSW: unable to create virtual fabric table.\n");
      return(-1);
   }

   if (frsw_read_cfg_file(t,filename) == -1) {
      fprintf(stderr,"FRSW: unable to parse configuration file.\n");
      return(-1);
   }

   if (pthread_create(&t->thread,NULL,frsw_thread_main,t) != 0) {
      fprintf(stderr,"FRSW: unable to create thread.\n");
      return(-1);
   }
   
   return(0);
}
