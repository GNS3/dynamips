/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * ATM utility functions and Virtual ATM switch.
 *
 * HEC and AAL5 CRC computation functions are from Charles Michael Heard
 * and can be found at (no licence specified, this is to check!):
 *
 *    http://cell-relay.indiana.edu/cell-relay/publications/software/CRC/
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

#include "atm.h"
#include "net_io.h"
#include "utils.h"

/********************************************************************/
#define HEC_GENERATOR   0x107               /* x^8 + x^2 +  x  + 1  */
#define COSET_LEADER    0x055               /* x^6 + x^4 + x^2 + 1  */

static m_uint8_t syndrome_table[256];

/* generate a table of CRC-8 syndromes for all possible input bytes */
static void gen_syndrome_table(void)
{
   int i,j,syndrome;

   for(i=0;i<256;i++) {
      syndrome = i;
      
      for(j=0;j<8;j++) {
         if (syndrome & 0x80)
            syndrome = (syndrome << 1) ^ HEC_GENERATOR;
         else
            syndrome = (syndrome << 1);
      }
      syndrome_table[i] = (unsigned char)syndrome;
   }
}

/* Compute HEC field for ATM header */
m_uint8_t atm_compute_hec(m_uint8_t *cell_header)
{
   register m_uint8_t hec_accum = 0;
   register int i;

   /* 
    * calculate CRC-8 remainder over first four bytes of cell header.
    * exclusive-or with coset leader & insert into fifth header byte.
    */
   for(i=0;i<4;i++)
      hec_accum = syndrome_table[hec_accum ^ cell_header[i]];
   
   return(hec_accum ^ COSET_LEADER);
}

/* Insert HEC field into an ATM header */
void atm_insert_hec(m_uint8_t *cell_header)
{
   cell_header[4] = atm_compute_hec(cell_header);
}

#define POLYNOMIAL 0x04c11db7L
static m_uint32_t crc_table[256];

/* Generate the table of CRC remainders for all possible bytes */
static void gen_crc_table(void)
{
   m_uint32_t crc_accum;
   int i,j;  

   for(i=0;i<256;i++) { 
      crc_accum = ((m_uint32_t)i << 24);

      for(j=0;j<8;j++) {
         if (crc_accum & 0x80000000L)
            crc_accum = (crc_accum << 1) ^ POLYNOMIAL;
         else
            crc_accum = (crc_accum << 1); 
      }
      
      crc_table[i] = crc_accum; 
   }
}

/* Update the CRC on the data block one byte at a time */
m_uint32_t atm_update_crc(m_uint32_t crc_accum,m_uint8_t *ptr,int len)
{
   register int i,j;

   for(j=0;j<len;j++) { 
      i = ((int)(crc_accum >> 24) ^ *ptr++) & 0xff;
      crc_accum = (crc_accum << 8) ^ crc_table[i]; 
   }
   
   return(crc_accum);
}

/* Initialize ATM code (for checksums) */
void atm_init(void)
{
   gen_syndrome_table();
   gen_crc_table();
}

/* VPC hash function */
static inline u_int atmsw_vpc_hash(u_int vpi)
{
   return((vpi ^ (vpi >> 8)) & (ATMSW_VP_HASH_SIZE-1));
}

/* VCC hash function */
static inline u_int atmsw_vcc_hash(u_int vpi,u_int vci)
{
   return((vpi ^ vci) & (ATMSW_VC_HASH_SIZE-1));
}

/* VP lookup */
atm_vp_swconn_t *atmsw_vp_lookup(atm_sw_table_t *t,netio_desc_t *input,
                                 u_int vpi)
{
   atm_vp_swconn_t *swc;
   
   for(swc=t->vp_table[atmsw_vpc_hash(vpi)];swc;swc=swc->next)
      if ((swc->input == input) && (swc->vpi_in == vpi))
         return swc;

   return NULL;
}

/* VC lookup */
atm_vc_swconn_t *atmsw_vc_lookup(atm_sw_table_t *t,netio_desc_t *input,
                                 u_int vpi,u_int vci)
{
   atm_vc_swconn_t *swc;

   for(swc=t->vc_table[atmsw_vcc_hash(vpi,vci)];swc;swc=swc->next)
      if ((swc->input == input) && (swc->vpi_in == vpi) && 
          (swc->vci_in == vci))
         return swc;

   return NULL;
}

/* Create a VP switch connection */
int atmsw_create_vpc(atm_sw_table_t *t,netio_desc_t *input,u_int vpi_in,
                     netio_desc_t *output,u_int vpi_out)
{
   atm_vp_swconn_t *swc;
   u_int hbucket;

   if (!(swc = atmsw_vp_lookup(t,input,vpi_in))) {
      if (!(swc = malloc(sizeof(*swc))))
         return(-1);

      memset(swc,0,sizeof(*swc));
      hbucket = atmsw_vpc_hash(vpi_in);
      swc->next = t->vp_table[hbucket];
      t->vp_table[hbucket] = swc;
   }

   swc->input   = input;
   swc->output  = output;
   swc->vpi_in  = vpi_in;
   swc->vpi_out = vpi_out;
   return(0);
}

/* Create a VC switch connection */
int atmsw_create_vcc(atm_sw_table_t *t,
                     netio_desc_t *input,u_int vpi_in,u_int vci_in,
                     netio_desc_t *output,u_int vpi_out,u_int vci_out)
{
   atm_vc_swconn_t *swc;
   u_int hbucket;

   /* ensure that there is not already VP switching */
   if (atmsw_vp_lookup(t,input,vpi_in) != NULL) {
      fprintf(stderr,"atmsw_create_vcc: VP switching already exists for "
              "VPI=%u\n",vpi_in);
      return(-1);
   }

   if (!(swc = atmsw_vc_lookup(t,input,vpi_in,vci_in))) {
      if (!(swc = malloc(sizeof(*swc))))
         return(-1);

      memset(swc,0,sizeof(*swc));
      hbucket = atmsw_vcc_hash(vpi_in,vci_in);
      swc->next = t->vc_table[hbucket];
      t->vc_table[hbucket] = swc;
   }

   swc->input   = input;
   swc->output  = output;
   swc->vpi_in  = vpi_in;
   swc->vci_in  = vci_in;
   swc->vpi_out = vpi_out;
   swc->vci_out = vci_out;
   return(-1);
}

/* Create a virtual switch table */
atm_sw_table_t *atmsw_create_table(void)
{
   atm_sw_table_t *t;

   if (!(t = malloc(sizeof(*t))))
      return NULL;

   memset(t,0,sizeof(*t));
   return t;
}

/* Add a NetIO descriptor to an ATM switch table */
int atmsw_add_netio(atm_sw_table_t *t,netio_desc_t *nio)
{
   int i;

   /* try to find a free slot */
   for(i=0;i<ATMSW_NIO_MAX;i++)
      if (t->nio[i] == NULL)
         break;
   
   if (i == ATMSW_NIO_MAX)
      return(-1);

   t->nio[i] = nio;
   return(0);
}

/* Find a NetIO descriptor given its name */
netio_desc_t *atmsw_find_netio_by_name(atm_sw_table_t *t,char *name)
{
   int i;

   for(i=0;i<ATMSW_NIO_MAX;i++)
      if ((t->nio[i] != NULL) && !strcmp(t->nio[i]->name,name))
         return t->nio[i];

   return NULL;
}

/* VP switching */
void atmsw_vp_switch(atm_vp_swconn_t *vpc,m_uint8_t *cell)
{
   m_uint32_t atm_hdr;

   /* rewrite the atm header with new vpi */
   atm_hdr =  ntohl(*(m_uint32_t *)cell);
   atm_hdr =  atm_hdr & ~ATM_HDR_VPI_MASK;
   atm_hdr |= vpc->vpi_out << ATM_HDR_VPI_SHIFT;
   *(m_uint32_t *)cell = htonl(atm_hdr);

   /* recompute HEC field */
   atm_insert_hec(cell);

   /* update the statistics counter */
   vpc->cell_cnt++;
}

/* VC switching */
void atmsw_vc_switch(atm_vc_swconn_t *vcc,m_uint8_t *cell)
{
   m_uint32_t atm_hdr;

   /* rewrite the atm header with new vpi/vci */
   atm_hdr = ntohl(*(m_uint32_t *)cell);

   atm_hdr =  atm_hdr & ~(ATM_HDR_VPI_MASK|ATM_HDR_VCI_MASK);
   atm_hdr |= vcc->vpi_out << ATM_HDR_VPI_SHIFT;
   atm_hdr |= vcc->vci_out << ATM_HDR_VCI_SHIFT;
   *(m_uint32_t *)cell = htonl(atm_hdr);

   /* recompute HEC field */
   atm_insert_hec(cell);

   /* update the statistics counter */
   vcc->cell_cnt++;
}

/* Handle an ATM cell */
ssize_t atmsw_handle_cell(atm_sw_table_t *t,netio_desc_t *input,
                          m_uint8_t *cell)
{
   m_uint32_t atm_hdr,vpi,vci;
   netio_desc_t *output = NULL;
   atm_vp_swconn_t *vpc;
   atm_vc_swconn_t *vcc;
   ssize_t len;

   /* Extract VPI/VCI information */
   atm_hdr = ntohl(*(m_uint32_t *)cell);

   vpi = (atm_hdr & ATM_HDR_VPI_MASK) >> ATM_HDR_VPI_SHIFT;
   vci = (atm_hdr & ATM_HDR_VCI_MASK) >> ATM_HDR_VCI_SHIFT;

   /* VP switching */
   if ((vpc = atmsw_vp_lookup(t,input,vpi)) != NULL) {
      atmsw_vp_switch(vpc,cell);
      output = vpc->output;
   } else {  
      /* VC switching */
      if ((vcc = atmsw_vc_lookup(t,input,vpi,vci)) != NULL) {
         atmsw_vc_switch(vcc,cell);
         output = vcc->output;
      }
   }

   len = netio_send(output,cell,ATM_CELL_SIZE);
   
   if (len != ATM_CELL_SIZE)
      t->cell_drop++;
   return(-1);
}

/* Virtual ATM switch fabric */
int atmsw_fabric(atm_sw_table_t *t)
{
   u_char atm_cell[ATM_CELL_SIZE];
   int i,res,fd,max_fd=-1;
   ssize_t len;
   fd_set rfds;

   for(;;) {
      FD_ZERO(&rfds);
   
      for(i=0;i<ATMSW_NIO_MAX;i++)
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

      for(i=0;i<ATMSW_NIO_MAX;i++)
         if (t->nio[i] != NULL) {
            fd = netio_get_fd(t->nio[i]);

            if ((fd != -1) && FD_ISSET(fd,&rfds)) {
               len = netio_recv(t->nio[i],atm_cell,ATM_CELL_SIZE);
               if (len != ATM_CELL_SIZE)
                  continue;

               atmsw_handle_cell(t,t->nio[i],atm_cell);
            }
         }
   }

   return(0);
}

/* Create a new interface */
int atmsw_cfg_create_if(atm_sw_table_t *t,char **tokens,int count)
{
   netio_desc_t *nio = NULL;
   int nio_type;

   /* at least: IF, interface name, NetIO type */
   if (count < 3) {
      fprintf(stderr,"atmsw_cfg_create_if: invalid interface description\n");
      return(-1);
   }
   
   /* check the interface name is not already taken */
   if (atmsw_find_netio_by_name(t,tokens[1]) != NULL) {
      fprintf(stderr,"ATMSW: interface %s already exists.\n",tokens[1]);
      return(-1);
   }

   nio_type = netio_get_type(tokens[2]);
   switch(nio_type) {
      case NETIO_TYPE_UNIX:
         if (count != 5) {
            fprintf(stderr,"ATMSW: invalid number of arguments "
                    "for UNIX NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_unix(tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_UDP:
         if (count != 6) {
            fprintf(stderr,"ATMSW: invalid number of arguments "
                    "for UDP NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_udp(atoi(tokens[3]),tokens[4],
                                     atoi(tokens[5]));
         break;

      case NETIO_TYPE_TCP_CLI:
         if (count != 5) {
            fprintf(stderr,"ATMSW: invalid number of arguments "
                    "for TCP CLI NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_tcp_cli(tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TCP_SER:
         if (count != 4) {
            fprintf(stderr,"ATMSW: invalid number of arguments "
                    "for TCP SER NIO '%s'\n",tokens[1]);
            break;
         }

         nio = netio_desc_create_tcp_ser(tokens[3]);
         break;

      default:
         fprintf(stderr,"ATMSW: unknown/invalid NETIO type '%s'\n",
                 tokens[2]);
   }

   if (!nio || !(nio->name = strdup(tokens[1]))) {
      fprintf(stderr,"ATMSW: unable to create NETIO descriptor of "
              "interface %s\n",tokens[1]);
      return(-1);
   }

   if (atmsw_add_netio(t,nio) == -1) {
      fprintf(stderr,"ATMSW: unable to add NETIO descriptor to SW table.\n");
      return(-1);
   }

   return(0);
}

/* Create a new Virtual Path Connection */
int atmsw_cfg_create_vpc(atm_sw_table_t *t,char **tokens,int count)
{
   netio_desc_t *input,*output;

   /* 5 parameters: "VP", InputIF, InVPI, OutputIF, OutVPI */
   if (count != 5) {
      fprintf(stderr,"ATMSW: invalid VPC descriptor.\n");
      return(-1);
   }

   if (!(input = atmsw_find_netio_by_name(t,tokens[1]))) {
      fprintf(stderr,"ATMSW: unknown interface \"%s\"\n",tokens[1]);
      return(-1);
   }

   if (!(output = atmsw_find_netio_by_name(t,tokens[3]))) {
      fprintf(stderr,"ATMSW: unknown interface \"%s\"\n",tokens[1]);
      return(-1);
   }

   return(atmsw_create_vpc(t,input,atoi(tokens[2]),output,atoi(tokens[4])));
}

/* Create a new Virtual Channel Connection */
int atmsw_cfg_create_vcc(atm_sw_table_t *t,char **tokens,int count)
{
   netio_desc_t *input,*output;

   /* 7 parameters: "VP", InputIF, InVPI/VCI, OutputIF, OutVPI/VCI */
   if (count != 7) {
      fprintf(stderr,"ATMSW: invalid VCC descriptor.\n");
      return(-1);
   }

   if (!(input = atmsw_find_netio_by_name(t,tokens[1]))) {
      fprintf(stderr,"ATMSW: unknown interface \"%s\"\n",tokens[1]);
      return(-1);
   }

   if (!(output = atmsw_find_netio_by_name(t,tokens[4]))) {
      fprintf(stderr,"ATMSW: unknown interface \"%s\"\n",tokens[1]);
      return(-1);
   }

   return(atmsw_create_vcc(t,input,atoi(tokens[2]),atoi(tokens[3]),
                           output,atoi(tokens[5]),atoi(tokens[6])));
}

#define ATMSW_MAX_TOKENS  16

/* Handle an ATMSW configuration line */
int atmsw_handle_cfg_line(atm_sw_table_t *t,char *str)
{  
   char *tokens[ATMSW_MAX_TOKENS];
   int count;

   if ((count = strsplit(str,':',tokens,ATMSW_MAX_TOKENS)) <= 1)
      return(-1);

   if (!strcmp(tokens[0],"IF"))
      return(atmsw_cfg_create_if(t,tokens,count));
   else if (!strcmp(tokens[0],"VP"))
      return(atmsw_cfg_create_vpc(t,tokens,count));
   else if (!strcmp(tokens[0],"VC"))
      return(atmsw_cfg_create_vcc(t,tokens,count));

   fprintf(stderr,"ATMSW: Unknown statement \"%s\" (allowed: IF,VP,VC)\n",
           tokens[0]);
   return(-1);
}

/* Read an ATMSW configuration file */
int atmsw_read_cfg_file(atm_sw_table_t *t,char *filename)
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
         atmsw_handle_cfg_line(t,buffer);
   }
   
   fclose(fd);
   return(0);
}

/* Virtual ATM switch thread */
void *atmsw_thread_main(void *arg)
{
   atm_sw_table_t *t = arg;

   printf("Started Virtual ATM switch fabric.\n");
   atmsw_fabric(t);
   return NULL;
}

/* Start a virtual ATM switch */
int atmsw_start(char *filename)
{
   atm_sw_table_t *t;

   if (!(t = atmsw_create_table())) {
      fprintf(stderr,"ATMSW: unable to create virtual fabric table.\n");
      return(-1);
   }

   if (atmsw_read_cfg_file(t,filename) == -1) {
      fprintf(stderr,"ATMSW: unable to parse configuration file.\n");
      return(-1);
   }

   if (pthread_create(&t->thread,NULL,atmsw_thread_main,t) != 0) {
      fprintf(stderr,"ATMSW: unable to create thread.\n");
      return(-1);
   }
   
   return(0);
}
