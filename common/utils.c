/*  
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Utility functions.
 */

#include "dynamips_common.h"

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
#include <sys/mman.h>
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

/* 
 * Decode from hex.
 *
 * hex to raw bytes, returning count of bytes.
 * maxlen limits output buffer size.
 */
int hex_decode(unsigned char *out,const unsigned char *in,int maxlen)
{
#define BAD -1
   static const char hexval[] = {
      BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
      BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
      BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
        0,  1,  2,  3,   4,  5,  6,  7,   8,  9,BAD,BAD, BAD,BAD,BAD,BAD,
      BAD, 10, 11, 12,  13, 14, 15,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
      BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD,
      BAD, 10, 11, 12,  13, 14, 15,BAD, BAD,BAD,BAD,BAD, BAD,BAD,BAD,BAD
   };
   int len = 0;
   int empty = TRUE;

   for(;len < maxlen;) {
      if (*in > sizeof(hexval) || hexval[*in] == BAD)
         break;

      if (empty) {
         *out = (unsigned char)hexval[*in] << 4;
      }
      else {
         *out |= (unsigned char)hexval[*in];
         ++out;
         ++len;
      }
      ++in;
      empty = !empty;
   }

   return(len);
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
   struct tm tmn;
   time_t ct;
   char buf[256];

   if (fd != NULL) {
      gettimeofday(&now,0);
      ct = now.tv_sec;
      localtime_r(&ct,&tmn);

      strftime(buf,sizeof(buf),"%b %d %H:%M:%S",&tmn);

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

/* Write an array of string to a logfile */
void m_flog_str_array(FILE *fd,int count,char *str[])
{
   int i;

   for(i=0;i<count;i++)
      fprintf(fd,"%s ",str[i]);

   fprintf(fd,"\n");
   fflush(fd);
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
int m_read_file(const char *filename,u_char **buffer,size_t *length)
{
   FILE *fd;
   long len;

   // open
   fd = fopen(filename,"rb");
   if (fd == NULL) {
      return(-1);
   }

   // len
   fseek(fd, 0, SEEK_END);
   len = ftell(fd);
   fseek(fd, 0, SEEK_SET);
   if (len < 0 || ferror(fd)) {
      fclose(fd);
      return(-1);
   }

   if (length) {
      *length = (size_t)len;
   }

    // data
   if (buffer) {
      *buffer = (u_char *)malloc((size_t)len);
      if (*buffer == NULL || fread(*buffer, (size_t)len, 1, fd) != 1) {
         free(*buffer);
         *buffer = NULL;
         fclose(fd);
         return(-1);
      }
   }

   // close
   fclose(fd);
   return(0);
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

/* Sync a memory zone */
int memzone_sync(void *addr, size_t len)
{
   return(msync(addr, len, MS_SYNC));
}

/* Sync all mappings of a memory zone */
int memzone_sync_all(void *addr, size_t len)
{
   return(msync(addr, len, MS_SYNC | MS_INVALIDATE));
}

/* Unmap a memory zone */
int memzone_unmap(void *addr, size_t len)
{
   return(munmap(addr, len));
}

/* Return a memory zone or NULL on error */
static void *mmap_or_null(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
   void *ptr;

   if ((ptr = mmap(addr, length, prot, flags, fd, offset)) == MAP_FAILED)
      return(NULL);

   return(ptr);
}

/* Map a memory zone as an executable area */
u_char *memzone_map_exec_area(size_t len)
{
   return(mmap_or_null(NULL,len,PROT_EXEC|PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,(off_t)0));
}

/* Map a memory zone from a file */
u_char *memzone_map_file(int fd,size_t len)
{
   return(mmap_or_null(NULL,len,PROT_READ|PROT_WRITE,MAP_SHARED,fd,(off_t)0));
}

/* Map a memory zone from a ro file */
u_char *memzone_map_file_ro(int fd,size_t len)
{
   return(mmap_or_null(NULL,len,PROT_READ,MAP_PRIVATE,fd,(off_t)0));
}

/* Map a memory zone from a file, with copy-on-write (COW) */
u_char *memzone_map_cow_file(int fd,size_t len)
{
   return(mmap_or_null(NULL,len,PROT_READ|PROT_WRITE,MAP_PRIVATE,fd,(off_t)0));
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

   if ((fd = open(filename,O_RDONLY,S_IRWXU)) == -1) {
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

int memzone_open_file_ro(char *filename,u_char **ptr,off_t *fsize)
{
   struct stat fprop;
   int fd;

   if ((fd = open(filename,O_RDONLY,S_IRWXU)) == -1)
      return(-1);

   if (fstat(fd,&fprop) == -1)
      goto err_fstat;

   *fsize = fprop.st_size;
   if (!(*ptr = memzone_map_file_ro(fd,*fsize)))
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
   m_uint32_t *p _not_aligned = ptr;
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

/* Generate a pseudo random block of data */
void m_randomize_block(m_uint8_t *buf,size_t len)
{
   int i;

   for(i=0;i<len;i++)
      buf[i] = rand() & 0xFF;
}

/* Free an FD pool */
void fd_pool_free(fd_pool_t *pool)
{
   fd_pool_t *p,*next;
   int i;
   
   for(p=pool;p;p=next) {
      next = p->next;
      
      for(i=0;i<FD_POOL_MAX;i++) {
         if (p->fd[i] != -1) {
            shutdown(p->fd[i],2);
            close(p->fd[i]);
         }
      }
               
      if (pool != p)
         free(p);
   }
}

/* Initialize an empty pool */
void fd_pool_init(fd_pool_t *pool)
{
   int i;
   
   for(i=0;i<FD_POOL_MAX;i++)
      pool->fd[i] = -1;

   pool->next = NULL;
}

/* Get a free slot for a FD in a pool */
int fd_pool_get_free_slot(fd_pool_t *pool,int **slot)
{
   fd_pool_t *p;
   int i;
   
   for(p=pool;p;p=p->next) {
      for(i=0;i<FD_POOL_MAX;i++) {
         if (p->fd[i] == -1) {
            *slot = &p->fd[i];
            return(0);
         }
      }
   }
   
   /* No free slot, allocate a new pool */
   if (!(p = malloc(sizeof(*p))))
      return(-1);

   fd_pool_init(p);      
   *slot = &p->fd[0];
      
   p->next = pool->next;
   pool->next = p;
   return(0);
}

/* Fill a FD set and get the maximum FD in order to use with select */
int fd_pool_set_fds(fd_pool_t *pool,fd_set *fds)
{
   fd_pool_t *p;
   int i,max_fd = -1;
   
   for(p=pool;p;p=p->next)
      for(i=0;i<FD_POOL_MAX;i++) {
         if (p->fd[i] != -1) {
            FD_SET(p->fd[i],fds);
            
            if (p->fd[i] > max_fd)
               max_fd = p->fd[i];
         }
      }
      
   return(max_fd);
}

/* Send a buffer to all FDs of a pool */
int fd_pool_send(fd_pool_t *pool,void *buffer,size_t len,int flags)
{
   fd_pool_t *p;
   ssize_t res;
   int i,err;
   
   for(p=pool,err=0;p;p=p->next)
      for(i=0;i<FD_POOL_MAX;i++) {
         if (p->fd[i] == -1)
            continue;
         
         res = send(p->fd[i],buffer,len,flags);
         
         if (res != len) {
            shutdown(p->fd[i],2);
            close(p->fd[i]);
            p->fd[i] = -1;
            err++;
         }
      }
      
   return(err);
}

/* Call a function for each FD having incoming data */
int fd_pool_check_input(fd_pool_t *pool,fd_set *fds,
                        void (*cbk)(int *fd_slot,void *opt),void *opt)
{
   fd_pool_t *p;
   int i,count;
   
   for(p=pool,count=0;p;p=p->next)
      for(i=0;i<FD_POOL_MAX;i++) {
         if ((p->fd[i] != -1) && FD_ISSET(p->fd[i],fds)) {
            cbk(&p->fd[i],opt);
            count++;
         }
      }
   
   return(count);
}

/* Equivalent to fprintf, but for a posix fd */
ssize_t fd_printf(int fd,int flags,char *fmt,...)
{
   char buffer[2048];
   va_list ap;
    
   va_start(ap,fmt);
   vsnprintf(buffer,sizeof(buffer),fmt,ap);
   va_end(ap);
   
   return(send(fd,buffer,strlen(buffer),flags));
}
