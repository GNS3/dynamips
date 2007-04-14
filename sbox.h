/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * S-box functions.
 */

#ifndef __SBOX_H__
#define __SBOX_H__

#include <sys/types.h>
#include "utils.h"

extern m_uint32_t sbox_array[];

static inline m_uint32_t sbox_compute(m_uint8_t *data,int len)
{
   m_uint32_t hash = 0;
   
   while(len > 0) {
      hash ^= sbox_array[*data];
      hash *= 3;
      data++;
   }

   return(hash);
}

static forced_inline m_uint32_t sbox_u32(m_uint32_t val)
{
   m_uint32_t hash = 0;

   hash ^= sbox_array[(m_uint8_t)val];
   hash *= 3;
   val >>= 8;

   hash ^= sbox_array[(m_uint8_t)val];
   hash *= 3;
   val >>= 8;

   hash ^= sbox_array[(m_uint8_t)val];
   hash *= 3;
   val >>= 8;

   hash ^= sbox_array[(m_uint8_t)val];
   return(hash);
}

#endif
