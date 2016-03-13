/*
 * Cisco router simulation platform.
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
#include "mempool.h"
#include "registry.h"
#include "net_io.h"
#include "frame_relay.h"

#define DEBUG_FRSW  0

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
frsw_conn_t *frsw_dlci_lookup(frsw_table_t *t,netio_desc_t *input,u_int dlci)
{
   frsw_conn_t *vc;
   
   for(vc=t->dlci_table[frsw_dlci_hash(dlci)];vc;vc=vc->hash_next)
      if ((vc->input == input) && (vc->dlci_in == dlci))
         return vc;

   return NULL;
}

/* Handle a ANSI LMI packet */
ssize_t frsw_handle_lmi_ansi_pkt(frsw_table_t *t,netio_desc_t *input,
                                 m_uint8_t *pkt,ssize_t len)
{
   m_uint8_t resp[FR_MAX_PKT_SIZE],*pres,*preq;
   m_uint8_t itype,isize;
   int msg_type,seq_ok;
   ssize_t rlen;
   frsw_conn_t *sc;
   u_int dlci;

   if ((len <= sizeof(lmi_ansi_hdr)) || 
       memcmp(pkt,lmi_ansi_hdr,sizeof(lmi_ansi_hdr)))
      return(-1);

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

            if (input->fr_lmi_seq != preq[3]) {
               m_log(input->name,"resynchronization with LMI sequence...\n");
               input->fr_lmi_seq = preq[3];
            } 

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
      for(sc=input->fr_conn_list;sc;sc=sc->next) {
         dlci = sc->dlci_in;
#if DEBUG_FRSW
         m_log(input->name,"sending LMI adv for DLCI %u\n",dlci);
#endif
         pres[0] = 0x07;
         pres[1] = 0x03;
         pres[2] = dlci >> 4;
         pres[3] = 0x80 | ((dlci & 0x0f) << 3);
         pres[4] = 0x82;
         pres += 5;
      }
   }

   rlen = pres - resp;

#if DEBUG_FRSW
   m_log(input->name,"sending ANSI LMI packet:\n");
   mem_dump(log_file,resp,rlen);
#endif

   netio_send(input,resp,rlen);
   return(0);
}

/* DLCI switching */
void frsw_dlci_switch(frsw_conn_t *vc,m_uint8_t *pkt)
{
   pkt[0] = (pkt[0] & 0x03) | ((vc->dlci_out >> 4) << 2);
   pkt[1] = (pkt[1] & 0x0f) | ((vc->dlci_out & 0x0f) << 4);

   /* update the statistics counter */
   vc->count++;
}

/* Handle a Frame-Relay packet */
ssize_t frsw_handle_pkt(frsw_table_t *t,netio_desc_t *input,
                        m_uint8_t *pkt,ssize_t len)
{
   netio_desc_t *output = NULL;
   frsw_conn_t *vc;
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

#if DEBUG_FRSW
   if (output) {
      m_log(input->name,"Switching packet to interface %s.\n",output->name);
   } else {
      m_log(input->name,"Unable to switch packet.\n");
   }
#endif

   /* Send the packet on output interface */
   slen = netio_send(output,pkt,len);

   if (len != slen) {
      t->drop++;
      return(-1);
   }

   return(0);
}

/* Receive a Frame-Relay packet */
static int frsw_recv_pkt(netio_desc_t *nio,u_char *pkt,ssize_t pkt_len,
                         frsw_table_t *t)
{
   int res;

   FRSW_LOCK(t);
   res = frsw_handle_pkt(t,nio,pkt,pkt_len);
   FRSW_UNLOCK(t);
   return(res);
}

/* Acquire a reference to a Frame-Relay switch (increment reference count) */
frsw_table_t *frsw_acquire(char *name)
{
   return(registry_find(name,OBJ_TYPE_FRSW));
}

/* Release a Frame-Relay switch (decrement reference count) */
int frsw_release(char *name)
{
   return(registry_unref(name,OBJ_TYPE_FRSW));
}

/* Create a virtual switch table */
frsw_table_t *frsw_create_table(char *name)
{
   frsw_table_t *t;

   /* Allocate a new switch structure */
   if (!(t = malloc(sizeof(*t))))
      return NULL;

   memset(t,0,sizeof(*t));
   pthread_mutex_init(&t->lock,NULL);
   mp_create_fixed_pool(&t->mp,"Frame-Relay Switch");

   if (!(t->name = mp_strdup(&t->mp,name)))
      goto err_name;

   /* Record this object in registry */
   if (registry_add(t->name,OBJ_TYPE_FRSW,t) == -1) {
      fprintf(stderr,"frsw_create_table: unable to create switch '%s'\n",name);
      goto err_reg;
   }

   return t;

 err_reg:
 err_name:
   mp_free_pool(&t->mp);
   free(t);
   return NULL;
}

/* Unlink a VC */
static void frsw_unlink_vc(frsw_conn_t *vc)
{
   if (vc) {
      if (vc->next)
         vc->next->pprev = vc->pprev;

      if (vc->pprev)
         *(vc->pprev) = vc->next;
   }
}

/* Free resources used by a VC */
static void frsw_release_vc(frsw_conn_t *vc)
{
   if (vc) {
      /* release input NIO */
      if (vc->input) {
         netio_rxl_remove(vc->input);
         netio_release(vc->input->name);
      }

      /* release output NIO */
      if (vc->output) 
         netio_release(vc->output->name);
   }
}

/* Free resources used by a Frame-Relay switch */
static int frsw_free(void *data,void *arg)
{
   frsw_table_t *t = data;
   frsw_conn_t *vc;
   int i;

   for(i=0;i<FRSW_HASH_SIZE;i++)
      for(vc=t->dlci_table[i];vc;vc=vc->hash_next)
         frsw_release_vc(vc);

   mp_free_pool(&t->mp);
   free(t);
   return(TRUE);
}

/* Delete a Frame-Relay switch */
int frsw_delete(char *name)
{
   return(registry_delete_if_unused(name,OBJ_TYPE_FRSW,frsw_free,NULL));
}

/* Delete all Frame-Relay switches */
int frsw_delete_all(void)
{
   return(registry_delete_type(OBJ_TYPE_FRSW,frsw_free,NULL));
}

/* Create a switch connection */
int frsw_create_vc(frsw_table_t *t,char *nio_input,u_int dlci_in,
                   char *nio_output,u_int dlci_out)
{
   frsw_conn_t *vc,**p;
   u_int hbucket;

   FRSW_LOCK(t);

   /* Allocate a new VC */
   if (!(vc = mp_alloc(&t->mp,sizeof(*vc)))) {
      FRSW_UNLOCK(t);
      return(-1);
   }   

   vc->input    = netio_acquire(nio_input);
   vc->output   = netio_acquire(nio_output);
   vc->dlci_in  = dlci_in;
   vc->dlci_out = dlci_out;
   
   /* Check these NIOs are valid and the input VC does not exists */
   if (!vc->input || !vc->output)
      goto error;

   if (frsw_dlci_lookup(t,vc->input,dlci_in)) {
      fprintf(stderr,"FRSW %s: switching for VC %u on IF %s "
              "already defined.\n",t->name,dlci_in,vc->input->name);
      goto error;
   }

   /* Add as a RX listener */
   if (netio_rxl_add(vc->input,(netio_rx_handler_t)frsw_recv_pkt,t,NULL) == -1)
      goto error;

   hbucket = frsw_dlci_hash(dlci_in);
   vc->hash_next = t->dlci_table[hbucket];
   t->dlci_table[hbucket] = vc;

   for(p=(frsw_conn_t **)&vc->input->fr_conn_list;*p;p=&(*p)->next)
      if ((*p)->dlci_in > dlci_in)
         break;

   vc->next = *p;
   if (*p) (*p)->pprev = &vc->next;
   vc->pprev = p;
   *p = vc;

   FRSW_UNLOCK(t);
   return(0);

 error:
   FRSW_UNLOCK(t);
   frsw_release_vc(vc);
   mp_free(vc);
   return(-1);
}

/* Remove a switch connection */
int frsw_delete_vc(frsw_table_t *t,char *nio_input,u_int dlci_in,
                   char *nio_output,u_int dlci_out)
{
   netio_desc_t *input,*output;
   frsw_conn_t **vc,*p;
   u_int hbucket;

   FRSW_LOCK(t);

   input = registry_exists(nio_input,OBJ_TYPE_NIO);
   output = registry_exists(nio_output,OBJ_TYPE_NIO);

   if (!input || !output) {
      FRSW_UNLOCK(t);
      return(-1);
   }

   hbucket = frsw_dlci_hash(dlci_in);
   for(vc=&t->dlci_table[hbucket];*vc;vc=&(*vc)->hash_next) 
   {
      p = *vc;

      if ((p->input == input) && (p->output == output) &&
          (p->dlci_in == dlci_in) && (p->dlci_out == dlci_out))
      {
         /* Found a matching VC, remove it */
         *vc = (*vc)->hash_next;
         frsw_unlink_vc(p);
         FRSW_UNLOCK(t);

         /* Release NIOs */
         frsw_release_vc(p);
         mp_free(p);
         return(0);
      }
   }

   FRSW_UNLOCK(t);
   return(-1);
}

/* Save the configuration of a Frame-Relay switch */
void frsw_save_config(frsw_table_t *t,FILE *fd)
{
   frsw_conn_t *vc;
   int i;

   fprintf(fd,"frsw create %s\n",t->name);

   FRSW_LOCK(t);

   for(i=0;i<FRSW_HASH_SIZE;i++) {
      for(vc=t->dlci_table[i];vc;vc=vc->next) {
         fprintf(fd,"frsw create_vc %s %s %u %s %u\n",
                 t->name,vc->input->name,vc->dlci_in,
                 vc->output->name,vc->dlci_out);
      }
   }

   FRSW_UNLOCK(t);

   fprintf(fd,"\n");
}

/* Save configurations of all Frame-Relay switches */
static void frsw_reg_save_config(registry_entry_t *entry,void *opt,int *err)
{
   frsw_save_config((frsw_table_t *)entry->data,(FILE *)opt);
}

void frsw_save_config_all(FILE *fd)
{
   registry_foreach_type(OBJ_TYPE_FRSW,frsw_reg_save_config,fd,NULL);
}

/* Create a new interface */
int frsw_cfg_create_if(frsw_table_t *t,char **tokens,int count)
{
   netio_desc_t *nio = NULL;
   int nio_type;

   /* at least: IF, interface name, NetIO type */
   if (count < 3) {
      fprintf(stderr,"frsw_cfg_create_if: invalid interface description\n");
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

         nio = netio_desc_create_unix(tokens[1],tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_UDP:
         if (count != 6) {
            fprintf(stderr,"FRSW: invalid number of arguments "
                    "for UDP NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_udp(tokens[1],atoi(tokens[3]),
                                     tokens[4],atoi(tokens[5]));
         break;

      case NETIO_TYPE_TCP_CLI:
         if (count != 5) {
            fprintf(stderr,"FRSW: invalid number of arguments "
                    "for TCP CLI NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_tcp_cli(tokens[1],tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TCP_SER:
         if (count != 4) {
            fprintf(stderr,"FRSW: invalid number of arguments "
                    "for TCP SER NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_tcp_ser(tokens[1],tokens[3]);
         break;

      default:
         fprintf(stderr,"FRSW: unknown/invalid NETIO type '%s'\n",
                 tokens[2]);
   }

   if (!nio) {
      fprintf(stderr,"FRSW: unable to create NETIO descriptor of "
              "interface %s\n",tokens[1]);
      return(-1);
   }

   netio_release(nio->name);
   return(0);
}

/* Create a new virtual circuit */
int frsw_cfg_create_vc(frsw_table_t *t,char **tokens,int count)
{
   /* 5 parameters: "VC", InputIF, InDLCI, OutputIF, OutDLCI */
   if (count != 5) {
      fprintf(stderr,"FRSW: invalid VPC descriptor.\n");
      return(-1);
   }

   return(frsw_create_vc(t,tokens[1],atoi(tokens[2]),
                         tokens[3],atoi(tokens[4])));
}

#define FRSW_MAX_TOKENS  16

/* Handle a FRSW configuration line */
int frsw_handle_cfg_line(frsw_table_t *t,char *str)
{  
   char *tokens[FRSW_MAX_TOKENS];
   int count;

   if ((count = m_strsplit(str,':',tokens,FRSW_MAX_TOKENS)) <= 1)
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
int frsw_read_cfg_file(frsw_table_t *t,char *filename)
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
         frsw_handle_cfg_line(t,buffer);
   }
   
   fclose(fd);
   return(0);
}

/* Start a virtual Frame-Relay switch */
int frsw_start(char *filename)
{
   frsw_table_t *t;

   if (!(t = frsw_create_table("default"))) {
      fprintf(stderr,"FRSW: unable to create virtual fabric table.\n");
      return(-1);
   }

   if (frsw_read_cfg_file(t,filename) == -1) {
      fprintf(stderr,"FRSW: unable to parse configuration file.\n");
      return(-1);
   }
   
   frsw_release("default");
   return(0);
}
