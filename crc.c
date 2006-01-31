/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * CRC functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>

#include "utils.h"

#define CRC12_POLY 0x0f01
#define CRC16_POLY 0xa001

m_uint32_t crc12_array[256],crc16_array[256];

/* initialize CRC-12 algorithm */
void crc12_init(void)
{
   m_uint16_t crc,c;
   int i,j;

   for(i=0;i<256;i++) {
      crc = 0;
      c = (m_uint16_t)i;

      for(j=0;j<8;j++) {
         if ((crc ^ c) & 0x0001)
            crc = (crc >> 1) ^ CRC12_POLY;
         else
            crc =  crc >> 1;
         
         c = c >> 1;
      }

      crc12_array[i] = crc;
   }
}

/* initialize CRC-16 algorithm */
void crc16_init(void)
{
   m_uint16_t crc,c;
   int i,j;

   for(i=0;i<256;i++) {
      crc = 0;
      c = (m_uint16_t)i;

      for(j=0;j<8;j++) {
         if ((crc ^ c) & 0x0001)
            crc = (crc >> 1) ^ CRC16_POLY;
         else
            crc =  crc >> 1;
         
         c = c >> 1;
      }

      crc16_array[i] = crc;
   }
}

/* initialize crc algorithms */
void crc_init(void)
{
   crc12_init();
   crc16_init();
}
