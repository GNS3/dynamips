/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __UTILS_H__
#define __UTILS_H__

#include "dynamips_common.h"

#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>

/* Host CPU Types */
#define CPU_x86    0
#define CPU_amd64  1
#define CPU_nojit  2

/* Number of host registers available for JIT */
#if JIT_CPU == CPU_x86
#define JIT_HOST_NREG  8
#elif JIT_CPU == CPU_amd64
#define JIT_HOST_NREG  16
#else
#define JIT_HOST_NREG  0
#endif

/* Host to VM (big-endian) conversion functions */
#if ARCH_BYTE_ORDER == ARCH_BIG_ENDIAN
#define htovm16(x) (x)
#define htovm32(x) (x)
#define htovm64(x) (x)

#define vmtoh16(x) (x)
#define vmtoh32(x) (x)
#define vmtoh64(x) (x)
#else
#define htovm16(x) (htons(x))
#define htovm32(x) (htonl(x))
#define htovm64(x) (swap64(x))

#define vmtoh16(x) (ntohs(x))
#define vmtoh32(x) (ntohl(x))
#define vmtoh64(x) (swap64(x))
#endif

#ifndef COMMON_TYPES
#define COMMON_TYPES
typedef unsigned char m_uint8_t;                                                                                                
typedef signed char m_int8_t;                                                                                                   

typedef unsigned short m_uint16_t;                                                                                              
typedef signed short m_int16_t;                                                                                                 

typedef unsigned int m_uint32_t;                                                                                                
typedef signed int m_int32_t;                                                                                                   

typedef unsigned long long m_uint64_t;                                                                                          
typedef signed long long m_int64_t;                                                                                             

typedef unsigned long m_iptr_t;                                                                                                 
typedef m_uint64_t m_tmcnt_t;                                                                                                   
#endif                                                                                                                          

/* FD pool */
#define FD_POOL_MAX  16

typedef struct fd_pool fd_pool_t;
struct fd_pool {
   int fd[FD_POOL_MAX];
   struct fd_pool *next;
};

/* Forward declarations */
#ifndef CPU_GEN_T
#define CPU_GEN_T
typedef struct cpu_gen cpu_gen_t;
#endif
typedef struct vm_instance vm_instance_t;
typedef struct vm_platform vm_platform_t;
typedef struct mips64_jit_tcb mips64_jit_tcb_t;
typedef struct ppc32_jit_tcb ppc32_jit_tcb_t;
typedef struct jit_op jit_op_t;
typedef struct cpu_tb cpu_tb_t;
typedef struct cpu_tc cpu_tc_t;

/* Translated block function pointer */
typedef void (*insn_tblock_fptr)(void);

/* Host executable page */
typedef struct insn_exec_page insn_exec_page_t;
struct insn_exec_page {
   u_char *ptr;
   insn_exec_page_t *next;
   int flags;
};

/* MIPS instruction */
typedef m_uint32_t mips_insn_t;

/* PowerPC instruction */
typedef m_uint32_t ppc_insn_t;

/* MMAP */
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

/* Macros for double linked list */
#define M_LIST_ADD(item,head,prefix) \
   do { \
      (item)->prefix##_next  = (head); \
      (item)->prefix##_pprev = &(head); \
      \
      if ((head) != NULL) \
         (head)->prefix##_pprev = &(item)->prefix##_next; \
      \
      (head) = (item); \
   }while(0)

#define M_LIST_REMOVE(item,prefix) \
   do { \
      if ((item)->prefix##_pprev != NULL) { \
         if ((item)->prefix##_next != NULL) \
            (item)->prefix##_next->prefix##_pprev = (item)->prefix##_pprev; \
         \
         *((item)->prefix##_pprev) = (item)->prefix##_next; \
         \
         (item)->prefix##_pprev = NULL; \
         (item)->prefix##_next  = NULL; \
      } \
   }while(0)


/* List item */
typedef struct m_list m_list_t;
struct m_list {
   void *data;
   m_list_t *next;
};

/* MTS mapping info */
typedef struct {
   m_uint64_t vaddr;
   m_uint64_t paddr;
   m_uint64_t len;
   m_uint32_t cached;
   m_uint32_t offset;
   m_uint32_t flags;
}mts_map_t;

/* Invalid VTLB entry */
#define MTS_INV_ENTRY_MASK  0x00000001

/* MTS entry flags */
#define MTS_FLAG_DEV   0x000000001   /* Virtual device used */
#define MTS_FLAG_COW   0x000000002   /* Copy-On-Write */
#define MTS_FLAG_EXEC  0x000000004   /* Exec page */
#define MTS_FLAG_RO    0x000000008   /* Read-only page */

#define MTS_FLAG_WRCATCH (MTS_FLAG_RO|MTS_FLAG_COW)  /* Catch writes */

/* Virtual TLB entry (32-bit MMU) */
typedef struct mts32_entry mts32_entry_t;
struct mts32_entry {
   m_uint32_t gvpa;   /* Guest Virtual Page Address */
   m_uint32_t gppa;   /* Guest Physical Page Address */
   m_iptr_t   hpa;    /* Host Page Address */
   m_uint32_t flags;  /* Flags */
}__attribute__ ((aligned(16)));

/* Virtual TLB entry (64-bit MMU) */
typedef struct mts64_entry mts64_entry_t;
struct mts64_entry {
   m_uint64_t gvpa;   /* Guest Virtual Page Address */
   m_uint64_t gppa;   /* Guest Physical Page Address */
   m_iptr_t   hpa;    /* Host Page Address */
   m_uint32_t flags;  /* Flags */
}__attribute__ ((aligned(16)));

/* Host register allocation */
#define HREG_FLAG_ALLOC_LOCKED  1
#define HREG_FLAG_ALLOC_FORCED  2

struct hreg_map {
   int hreg,vreg;
   int flags;
   struct hreg_map *prev,*next;
};

/* Global logfile */
extern FILE *log_file;

/* Check status of a bit */
static inline int check_bit(u_int old,u_int new,u_int bit)
{
   int mask = 1 << bit;

   if ((old & mask) && !(new & mask))
      return(1);   /* bit unset */
   
   if (!(old & mask) && (new & mask))
      return(2);   /* bit set */

   /* no change */
   return(0);
}

/* Sign-extension */
static forced_inline m_int64_t sign_extend(m_int64_t x,int len)
{
   len = 64 - len;
   return (x << len) >> len;
}

/* Sign-extension (32-bit) */
static forced_inline m_int32_t sign_extend_32(m_int32_t x,int len)
{
   len = 32 - len;
   return (x << len) >> len;
}

/* Extract bits from a 32-bit values */
static inline int bits(m_uint32_t val,int start,int end)
{
   return((val >> start) & ((1 << (end-start+1)) - 1));
}

/* Normalize a size */
static inline u_int normalize_size(u_int val,u_int nb,int shift) 
{
   return(((val+nb-1) & ~(nb-1)) >> shift);
}

/* Convert a 16-bit number between little and big endian */
static forced_inline m_uint16_t swap16(m_uint16_t value)
{
   return((value >> 8) | ((value & 0xFF) << 8));
}

/* Convert a 32-bit number between little and big endian */
static forced_inline m_uint32_t swap32(m_uint32_t value)
{
   m_uint32_t result;

   result = value >> 24;
   result |= ((value >> 16) & 0xff) << 8;
   result |= ((value >> 8)  & 0xff) << 16;
   result |= (value & 0xff) << 24;
   return(result);
}

/* Convert a 64-bit number between little and big endian */
static forced_inline m_uint64_t swap64(m_uint64_t value)
{
   m_uint64_t result;

   result = (m_uint64_t)swap32(value & 0xffffffff) << 32;
   result |= swap32(value >> 32);
   return(result);
}

/* Get current time in number of msec since epoch */
static inline m_tmcnt_t m_gettime(void)
{
   struct timeval tvp;

   gettimeofday(&tvp,NULL);
   return(((m_tmcnt_t)tvp.tv_sec * 1000) + ((m_tmcnt_t)tvp.tv_usec / 1000));
}

/* Get current time in number of usec since epoch */
static inline m_tmcnt_t m_gettime_usec(void)
{
   struct timeval tvp;

   gettimeofday(&tvp,NULL);
   return(((m_tmcnt_t)tvp.tv_sec * 1000000) + (m_tmcnt_t)tvp.tv_usec);
}

#ifdef __CYGWIN__
#define GET_TIMEZONE _timezone
#else
#define GET_TIMEZONE timezone
#endif

/* Get current time in number of ms (localtime) */
static inline m_tmcnt_t m_gettime_adj(void)
{
   struct timeval tvp;
   struct tm tmx;
   time_t gmt_adjust;
   time_t ct;

   gettimeofday(&tvp,NULL);
   ct = tvp.tv_sec;
   localtime_r(&ct,&tmx);

#if defined(__CYGWIN__) || defined(SUNOS)
   gmt_adjust = -(tmx.tm_isdst ? GET_TIMEZONE - 3600 : GET_TIMEZONE);
#else
   gmt_adjust = tmx.tm_gmtoff;
#endif

   tvp.tv_sec += gmt_adjust;
   return(((m_tmcnt_t)tvp.tv_sec * 1000) + ((m_tmcnt_t)tvp.tv_usec / 1000));
}

/* Get a byte-swapped 16-bit value on a non-aligned area */
static inline m_uint16_t m_ntoh16(m_uint8_t *ptr)
{
   m_uint16_t val = (ptr[0] << 8) | ptr[1];
   return(val);
}

/* Get a byte-swapped 32-bit value on a non-aligned area */
static inline m_uint32_t m_ntoh32(m_uint8_t *ptr)
{
   m_uint32_t val = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
   return(val);
}

/* Set a byte-swapped 16-bit value on a non-aligned area */
static inline void m_hton16(m_uint8_t *ptr,m_uint16_t val)
{
   ptr[0] = val >> 8;
   ptr[1] = val;
}

/* Set a byte-swapped 32-bit value on a non-aligned area */
static inline void m_hton32(m_uint8_t *ptr,m_uint32_t val)
{
   ptr[0] = val >> 24;
   ptr[1] = val >> 16;
   ptr[2] = val >> 8;
   ptr[3] = val;
}

/* Add an element to a list */
m_list_t *m_list_add(m_list_t **head,void *data);

/* Dynamic sprintf */
char *dyn_sprintf(const char *fmt,...);

/* Split a string */
int m_strsplit(char *str,char delim,char **array,int max_count);

/* Tokenize a string */
int m_strtok(char *str,char delim,char **array,int max_count);

/* Quote a string */
char *m_strquote(char *buffer,size_t buf_len,char *str);

/* Decode from hex. */
int hex_decode(unsigned char *out,const unsigned char *in,int maxlen);

/* Ugly function that dumps a structure in hexa and ascii. */
void mem_dump(FILE *f_output,u_char *pkt,u_int len);

/* Logging function */
void m_flog(FILE *fd,char *module,char *fmt,va_list ap);

/* Logging function */
void m_log(char *module,char *fmt,...);

/* Write an array of string to a logfile */
void m_flog_str_array(FILE *fd,int count,char *str[]);

/* Returns a line from specified file (remove trailing '\n') */
char *m_fgets(char *buffer,int size,FILE *fd);

/* Read a file and returns it in a buffer */
int m_read_file(const char *filename,u_char **buffer,size_t *length);

/* Allocate aligned memory */
void *m_memalign(size_t boundary,size_t size);

/* Block specified signal for calling thread */
int m_signal_block(int sig);

/* Unblock specified signal for calling thread */
int m_signal_unblock(int sig);

/* Set non-blocking mode on a file descriptor */
int m_fd_set_non_block(int fd);

/* Sync a memory zone */
int memzone_sync(void *addr, size_t len);

/* Sync all mappings of a memory zone */
int memzone_sync_all(void *addr, size_t len);

/* Unmap a memory zone */
int memzone_unmap(void *addr, size_t len);

/* Map a memory zone as an executable area */
u_char *memzone_map_exec_area(size_t len);

/* Map a memory zone from a file */
u_char *memzone_map_file(int fd,size_t len);

/* Map a memory zone from a file, with copy-on-write (COW) */
u_char *memzone_map_cow_file(int fd,size_t len);

/* Create a file to serve as a memory zone */
int memzone_create_file(char *filename,size_t len,u_char **ptr);

/* Open a file to serve as a COW memory zone */
int memzone_open_cow_file(char *filename,size_t len,u_char **ptr);

/* Open a file and map it in memory */
int memzone_open_file(char *filename,u_char **ptr,off_t *fsize);
int memzone_open_file_ro(char *filename,u_char **ptr,off_t *fsize);

/* Compute NVRAM checksum */
m_uint16_t nvram_cksum(m_uint16_t *ptr,size_t count);

/* Byte-swap a memory block */
void mem_bswap32(void *ptr,size_t len);

/* Reverse a byte */
m_uint8_t m_reverse_u8(m_uint8_t val);

/* Generate a pseudo random block of data */
void m_randomize_block(m_uint8_t *buf,size_t len);

/* Free an FD pool */
void fd_pool_free(fd_pool_t *pool);

/* Initialize an empty pool */
void fd_pool_init(fd_pool_t *pool);

/* Get a free slot for a FD in a pool */
int fd_pool_get_free_slot(fd_pool_t *pool,int **slot);

/* Fill a FD set and get the maximum FD in order to use with select */
int fd_pool_set_fds(fd_pool_t *pool,fd_set *fds);

/* Send a buffer to all FDs of a pool */
int fd_pool_send(fd_pool_t *pool,void *buffer,size_t len,int flags);

/* Call a function for each FD having incoming data */
int fd_pool_check_input(fd_pool_t *pool,fd_set *fds,
                        void (*cbk)(int *fd_slot,void *opt),void *opt);

/* Equivalent to fprintf, but for a posix fd */
ssize_t fd_printf(int fd,int flags,char *fmt,...);

#endif
