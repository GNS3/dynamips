/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * CRC functions.
 */

#ifndef __CRC_H__
#define __CRC_H__

#include <sys/types.h>
#include "utils.h"

extern m_uint16_t crc12_array[],crc16_array[];
extern m_uint32_t crc32_array[];

/* Compute a CRC-12 hash on a 32-bit integer */
static forced_inline m_uint32_t crc12_hash_u32(m_uint32_t val)
{
   register m_uint32_t crc=0;
   register int i;

   for(i=0;i<4;i++) {
      crc = (crc >> 8) ^ crc12_array[(crc^val) & 0xff];
      val >>= 8;
   }

   return(crc);
}

/* Compute a CRC-16 hash on a 32-bit integer */
static forced_inline m_uint32_t crc16_hash_u32(m_uint32_t val)
{
   register m_uint32_t crc=0;
   register int i;

   for(i=0;i<4;i++) {
      crc = (crc >> 8) ^ crc16_array[(crc^val) & 0xff];
      val >>= 8;
   }

   return(crc);
}

/* Compute a CRC-32 on the specified block */
static forced_inline 
m_uint32_t crc32_compute(m_uint32_t crc_accum,m_uint8_t *ptr,int len)
{
   register m_uint32_t c = crc_accum;
   int n;
   
   for (n = 0; n < len; n++) {
      c = crc32_array[(c ^ ptr[n]) & 0xff] ^ (c >> 8);
   }

   return(~c);
}


/* Initialize CRC algorithms */
void crc_init(void);

#endif
