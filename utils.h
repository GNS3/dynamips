/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdarg.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>

/* True/False definitions */
#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE  1
#endif

/* Endianness */
#define ARCH_BIG_ENDIAN     0x4321
#define ARCH_LITTLE_ENDIAN  0x1234

#if defined(PPC) || defined(__powerpc__) || defined(__ppc__)
#define ARCH_BYTE_ORDER ARCH_BIG_ENDIAN
#elif defined(__sparc) || defined(__sparc__)
#define ARCH_BYTE_ORDER ARCH_BIG_ENDIAN
#elif defined(__alpha) || defined(__alpha__)
#define ARCH_BYTE_ORDER ARCH_LITTLE_ENDIAN
#elif defined(__i386) || defined(__i386__) || defined(i386)
#define ARCH_BYTE_ORDER ARCH_LITTLE_ENDIAN
#elif defined(__x86_64__)
#define ARCH_BYTE_ORDER ARCH_LITTLE_ENDIAN
#endif

#ifndef ARCH_BYTE_ORDER
#error Please define your architecture in utils.h!
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

/* Useful attributes for functions */
#define asmlinkage __attribute__((regparm(0)))
#define fastcall   __attribute__((regparm(3)))

#if __GNUC__ > 2
#define forced_inline inline __attribute__((always_inline))
#define no_inline __attribute__ ((noinline))
#else
#define forced_inline inline
#define no_inline
#endif

#if __GNUC__ > 2
/* http://kerneltrap.org/node/4705 */
#define likely(x)    __builtin_expect((x),1)
#define unlikely(x)  __builtin_expect((x),0)
#else
#define likely(x)    (x)
#define unlikely(x)  (x)
#endif

/* Common types */
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

/* Forward declarations */
typedef struct vm_instance vm_instance_t;
typedef struct insn_block insn_block_t;
typedef struct insn_exec_page insn_exec_page_t;

/* MIPS instruction */
typedef m_uint32_t mips_insn_t;

/* Max and min macro */
#define m_max(a,b) (((a) > (b)) ? (a) : (b))
#define m_min(a,b) (((a) < (b)) ? (a) : (b))

/* A simple macro for adjusting pointers */
#define PTR_ADJUST(type,ptr,size) (type)((char *)(ptr) + (size))

/* Size of a field in a structure */
#define SIZEOF(st,field) (sizeof(((st *)NULL)->field))

/* Compute offset of a field in a structure */
#define OFFSET(st,f)     ((long)&((st *)(NULL))->f)

/* MMAP */
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

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
   m_uint32_t tlb_index;
}mts_map_t;

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

/* Ugly function that dumps a structure in hexa and ascii. */
void mem_dump(FILE *f_output,u_char *pkt,u_int len);

/* Logging function */
void m_flog(FILE *fd,char *module,char *fmt,va_list ap);

/* Logging function */
void m_log(char *module,char *fmt,...);

/* Returns a line from specified file (remove trailing '\n') */
char *m_fgets(char *buffer,int size,FILE *fd);

/* Read a file and returns it in a buffer */
ssize_t m_read_file(char *filename,char **buffer);

/* Allocate aligned memory */
void *m_memalign(size_t boundary,size_t size);

#endif
