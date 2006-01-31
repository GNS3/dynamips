/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * CRC functions.
 */

#ifndef __CRC_H__
#define __CRC_H__

#include <sys/types.h>
#include "utils.h"

extern m_uint16_t crc12_array[],crc16_array[];

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

/* initialize crc algorithms */
void crc_init(void);

#endif
