/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __UTILS_H__
#define __UTILS_H__

#include <sys/types.h>
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
#else
#define forced_inline inline
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

/* Max and min macro */
#define m_max(a,b) (((a) > (b)) ? (a) : (b))
#define m_min(a,b) (((a) < (b)) ? (a) : (b))

/* Compute offset of a field in a structure */
#define OFFSET(st,f)     ((long)&((st *)(NULL))->f)

/* Ethernet MAC Address Length */
#define M_ETH_LEN  6

/* Ethernet address */
typedef struct m_eth_addr m_eth_addr_t;
struct m_eth_addr {
   m_uint8_t octet[M_ETH_LEN];
} __attribute__ ((__packed__));

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

/* Get current time in number of ms since epoch */
static inline m_tmcnt_t m_gettime(void)
{
   struct timeval tvp;

   gettimeofday(&tvp,NULL);
   return(((m_tmcnt_t)tvp.tv_sec * 1000) + ((m_tmcnt_t)tvp.tv_usec / 1000));
}

/* Dynamic sprintf */
char *dyn_sprintf(const char *fmt,...);

/* Parse a MAC address */
int parse_mac_addr(m_eth_addr_t *addr,char *str);

/* Split a string */
int strsplit(char *str,char delim,char **array,int max_count);

/* Ugly function that dumps a structure in hexa and ascii. */
void mem_dump(FILE *f_output,u_char *pkt,u_int len);

/* Logging function */
void m_log(char *module,char *fmt,...);

/* Allocate aligned memory */
void *m_memalign(size_t boundary,size_t size);

#endif
