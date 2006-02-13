/*  
 * Cisco C7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Utility functions.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#ifdef __CYGWIN__
#include <malloc.h>
#endif

#include "utils.h"

extern FILE *log_file;

/* Add an element to a list */
m_list_t *m_list_add(m_list_t **head,void *data)
{
   m_list_t *item;

   if ((item = malloc(sizeof(*item))) != NULL) {
      item->data = data;
      item->next = *head;
      *head = item;
   }

   return item;
}

/* Dynamic sprintf */
char *dyn_sprintf(const char *fmt,...)
{
   int n,size = 512;
   va_list ap;
   char *p;

   if ((p = malloc(size)) == NULL)
      return NULL;

   for(;;)
   {
      /* Try to print in the allocated space */
      va_start(ap,fmt);
      n = vsnprintf(p,size,fmt,ap);
      va_end(ap);

      /* If that worked, return the string */
      if ((n > -1) && (n < size))
         return p;

      /* Else try again with more space. */
      if (n > -1)
         size = n + 1;
      else
         size *= 2;

      if ((p = realloc(p,size)) == NULL)
         return NULL;
   }
}

/* Parse a MAC address */
int parse_mac_addr(m_eth_addr_t *addr,char *str)
{
   u_int v[M_ETH_LEN];
   int i,res;

   res = sscanf(str,"%x:%x:%x:%x:%x:%x",&v[0],&v[1],&v[2],&v[3],&v[4],&v[5]);

   for(i=0;i<M_ETH_LEN;i++)
      addr->octet[i] = v[i];

   return((res == 6) ? 0 : -1);
}

/* Split a string */
int strsplit(char *str,char delim,char **array,int max_count)
{
   int i,pos = 0;
   size_t len;
   char *ptr;

   for(i=0;i<max_count;i++)
      array[i] = NULL;

   do {
      if (pos == max_count)
         goto error;

      ptr = strchr(str,delim);
      if (!ptr)
         ptr = str + strlen(str);

      len = ptr - str;

      if (!(array[pos] = malloc(len+1)))
         goto error;

      memcpy(array[pos],str,len);
      array[pos][len] = 0;

      str = ptr + 1;
      pos++;
   }while(*ptr);
   
   return(pos);

 error:
   for(i=0;i<max_count;i++)
      free(array[i]);
   return(-1);
}

/* Ugly function that dumps a structure in hexa and ascii. */
void mem_dump(FILE *f_output,u_char *pkt,u_int len)
{
   u_int x,i = 0, tmp;

   while (i < len)
   {
      if ((len - i) > 16)
         x = 16;
      else x = len - i;

      fprintf(f_output,"%4.4x: ",i);

      for (tmp=0;tmp<x;tmp++)
         fprintf(f_output,"%2.2x ",pkt[i+tmp]);
      for (tmp=x;tmp<16;tmp++) fprintf(f_output,"   ");

      for (tmp=0;tmp<x;tmp++) {
         char c = pkt[i+tmp];

         if (((c >= 'A') && (c <= 'Z')) ||
             ((c >= 'a') && (c <= 'z')) ||
             ((c >= '0') && (c <= '9')))
            fprintf(f_output,"%c",c);
         else
            fputs(".",f_output);
      }

      i += x;
      fprintf(f_output,"\n");
   }

   fprintf(f_output,"\n");
}

/* Logging function */
void m_log(char *module,char *fmt,...)
{
   struct timeval now;
   static char buf[256];
   time_t ct;
   va_list ap;

   gettimeofday(&now,0);
   ct = now.tv_sec;
   strftime(buf,sizeof(buf),"%b %d %H:%M:%S",localtime(&ct));
   fprintf(log_file,"%s.%03ld %s: ",buf,now.tv_usec/1000,module);

   va_start(ap,fmt);
   vfprintf(log_file,fmt,ap);
   va_end(ap);

   fflush(log_file);
}

/* Allocate aligned memory */
void *m_memalign(size_t boundary,size_t size)
{
   void *p;

#ifdef __linux__
   if (posix_memalign((void *)&p,boundary,size))
#else
#ifdef __CYGWIN__
   if (!(p = memalign(boundary,size)))
#else
   if (!(p = malloc(size)))    
#endif
#endif
      return NULL;

   assert(((m_iptr_t)p & (boundary-1)) == 0);
   return p;
}

