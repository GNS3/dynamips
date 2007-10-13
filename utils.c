/*  
 * Cisco router simulation platform.
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
#include <signal.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
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
   char *p,*p2;

   if ((p = malloc(size)) == NULL) {
      perror("dyn_sprintf: malloc");
      return NULL;
   }

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

      if ((p2 = realloc(p,size)) == NULL) {
         perror("dyn_sprintf: realloc");
         free(p);
         return NULL;
      }

      p = p2;
   }
}

/* Split a string */
int m_strsplit(char *str,char delim,char **array,int max_count)
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

/* Tokenize a string */
int m_strtok(char *str,char delim,char **array,int max_count)
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

      while(*ptr == delim) 
         ptr++;

      str = ptr;
      pos++;
   }while(*ptr);
   
   return(pos);

 error:
   for(i=0;i<max_count;i++)
      free(array[i]);
   return(-1);
}

/* Quote a string */
char *m_strquote(char *buffer,size_t buf_len,char *str)
{
   char *p;
   
   if (!(p = strpbrk(str," \t\"'")))
      return str;

   snprintf(buffer,buf_len,"\"%s\"",str);
   return buffer;
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
   fflush(f_output);
}

/* Logging function */
void m_flog(FILE *fd,char *module,char *fmt,va_list ap)
{
   struct timeval now;
   static char buf[256];
   struct tm tmn;
   time_t ct;

   gettimeofday(&now,0);
   ct = now.tv_sec;
   localtime_r(&ct,&tmn);

   strftime(buf,sizeof(buf),"%b %d %H:%M:%S",&tmn);
   if (fd) {
      fprintf(fd,"%s.%03ld %s: ",buf,(long)now.tv_usec/1000,module);
      vfprintf(fd,fmt,ap);
      fflush(fd);
   }
}

/* Logging function */
void m_log(char *module,char *fmt,...)
{
   va_list ap;

   va_start(ap,fmt);
   m_flog(log_file,module,fmt,ap);
   va_end(ap);
}

/* Returns a line from specified file (remove trailing '\n') */
char *m_fgets(char *buffer,int size,FILE *fd)
{
   int len;

   buffer[0] = '\0';
   fgets(buffer,size,fd);

   if ((len = strlen(buffer)) == 0)
      return NULL;

   /* remove trailing '\n' */
   if (buffer[len-1] == '\n')
      buffer[len-1] = '\0';

   return buffer;
}

/* Read a file and returns it in a buffer */
ssize_t m_read_file(char *filename,u_char **buffer)
{
   u_char tmp[256],*ptr,*nptr;
   size_t len,tot_len;
   FILE *fd;

   *buffer = ptr = NULL;
   tot_len = 0;

   /* Open file for reading */
   if ((fd = fopen(filename,"r")) == NULL)
      return(-1);

   while((len = fread(tmp,1,sizeof(tmp),fd)) > 0)
   {
      /* Reallocate memory */
      nptr = realloc(ptr,tot_len+len+1);
      if (nptr == NULL) {
         if (ptr) free(ptr);
         fclose(fd);
         return(-1);
      }

      ptr = nptr;

      /* Ok, memory could be allocated */
      memcpy(&ptr[tot_len],tmp,len);
      tot_len += len;
   }

   fclose(fd);
   *buffer = ptr;
   return(tot_len);
}

/* Allocate aligned memory */
void *m_memalign(size_t boundary,size_t size)
{
   void *p;

#if defined(__linux__) || HAS_POSIX_MEMALIGN
   if (posix_memalign((void *)&p,boundary,size))
#else
#if defined(__CYGWIN__) || defined(SUNOS)
   if (!(p = memalign(boundary,size)))
#else
   if (!(p = malloc(size)))    
#endif
#endif
      return NULL;

   assert(((m_iptr_t)p & (boundary-1)) == 0);
   return p;
}

/* Block specified signal for calling thread */
int m_signal_block(int sig)
{
   sigset_t sig_mask;
   sigemptyset(&sig_mask);
   sigaddset(&sig_mask,sig);
   return(pthread_sigmask(SIG_BLOCK,&sig_mask,NULL));
}

/* Unblock specified signal for calling thread */
int m_signal_unblock(int sig)
{
   sigset_t sig_mask;
   sigemptyset(&sig_mask);
   sigaddset(&sig_mask,sig);
   return(pthread_sigmask(SIG_UNBLOCK,&sig_mask,NULL));
}

/* Set non-blocking mode on a file descriptor */
int m_fd_set_non_block(int fd)
{
   int flags;

   if ((flags = fcntl(fd,F_GETFL,0)) < 1)
      return(-1);

   return(fcntl(fd,F_SETFL, flags | O_NONBLOCK));
}

/* Map a memory zone from a file */
u_char *memzone_map_file(int fd,size_t len)
{
   return(mmap(NULL,len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(off_t)0));
}

/* Map a memory zone from a file, with copy-on-write (COW) */
u_char *memzone_map_cow_file(int fd,size_t len)
{
   return(mmap(NULL,len,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,(off_t)0));
}

/* Create a file to serve as a memory zone */
int memzone_create_file(char *filename,size_t len,u_char **ptr)
{
   int fd;

   if ((fd = open(filename,O_CREAT|O_RDWR,S_IRWXU)) == -1) {
      perror("memzone_create_file: open");
      return(-1);
   }

   if (ftruncate(fd,len) == -1) {
      perror("memzone_create_file: ftruncate");
      close(fd);
      return(-1);
   }

   *ptr = memzone_map_file(fd,len);

   if (!*ptr) {
      close(fd);
      fd = -1;
   }

   return(fd);
}

/* Open a file to serve as a COW memory zone */
int memzone_open_cow_file(char *filename,size_t len,u_char **ptr)
{
   int fd;

   if ((fd = open(filename,O_RDWR,S_IRWXU)) == -1) {
      perror("memzone_open_file: open");
      return(-1);
   }

   *ptr = memzone_map_cow_file(fd,len);

   if (!*ptr) {
      close(fd);
      fd = -1;
   }

   return(fd);
}

/* Open a file and map it in memory */
int memzone_open_file(char *filename,u_char **ptr,off_t *fsize)
{
   struct stat fprop;
   int fd;

   if ((fd = open(filename,O_RDWR,S_IRWXU)) == -1)
      return(-1);

   if (fstat(fd,&fprop) == -1)
      goto err_fstat;

   *fsize = fprop.st_size;
   if (!(*ptr = memzone_map_file(fd,*fsize)))
      goto err_mmap;
   
   return(fd);

 err_mmap:   
 err_fstat:
   close(fd);
   return(-1);
}

/* Compute NVRAM checksum */
m_uint16_t nvram_cksum(m_uint16_t *ptr,size_t count) 
{
   m_uint32_t sum = 0;

   while(count > 1) {
      sum = sum + ntohs(*ptr);
      ptr++;
      count -= sizeof(m_uint16_t);
   }

   if (count > 0) 
      sum = sum + ((ntohs(*ptr) & 0xFF) << 8); 

   while(sum>>16)
      sum = (sum & 0xffff) + (sum >> 16);

   return(~sum);
}

/* Byte-swap a memory block */
void mem_bswap32(void *ptr,size_t len)
{
   m_uint32_t *p = ptr;
   size_t count = len >> 2;
   int i;

   for(i=0;i<count;i++,p++)
      *p = swap32(*p);
}

/* Reverse a byte */
m_uint8_t m_reverse_u8(m_uint8_t val)
{
   m_uint8_t res = 0;
   int i;

   for(i=0;i<8;i++)
      if (val & (1 << i))
         res |= 1 << (7 - i);
   
   return(res);
}
