/*  
 * Cisco C7200 (Predator) simulation platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 *
 * Cisco EEPROM manipulation functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "utils.h"
#include "cisco_eeprom.h"

/* Get a byte from an EEPROM */
int cisco_eeprom_get_byte(m_uint16_t *eeprom,size_t eeprom_len,
                          size_t offset,m_uint8_t *val)
{
   m_uint16_t tmp;

   if (offset >= eeprom_len)
      return(-1);

   tmp = eeprom[offset >> 1];

   if (!(offset & 1))
      tmp >>= 8;

   *val = tmp & 0xFF;
   return(0);
}

/* Set a byte to an EEPROM */
int cisco_eeprom_set_byte(m_uint16_t *eeprom,size_t eeprom_len,
                          size_t offset,m_uint8_t val)
{
   m_uint16_t tmp;

   if (offset >= eeprom_len)
      return(-1);

   tmp = eeprom[offset >> 1];

   if (offset & 1)
      tmp = (tmp & 0xFF00) | val;
   else
      tmp = (tmp & 0x00FF) | (val << 8);

   eeprom[offset >> 1] = tmp;
   return(0);
}

/* Get an EEPROM region */
int cisco_eeprom_get_region(m_uint16_t *eeprom,size_t eeprom_len,
                            size_t offset,m_uint8_t *data,size_t data_len)
{
   size_t i;

   for(i=0;i<data_len;i++) {
      if (cisco_eeprom_get_byte(eeprom,eeprom_len,offset+i,&data[i]) == -1)
         return(-1);
   }

   return(0);
}

/* Set an EEPROM region */
int cisco_eeprom_set_region(m_uint16_t *eeprom,size_t eeprom_len,
                            size_t offset,m_uint8_t *data,size_t data_len)
{
   size_t i;

   for(i=0;i<data_len;i++) {
      if (cisco_eeprom_set_byte(eeprom,eeprom_len,offset+i,data[i]) == -1)
         return(-1);
   }

   return(0);
}

/* Get a field of a Cisco EEPROM v4 */
int cisco_eeprom_v4_get_field(m_uint16_t *eeprom,size_t eeprom_len,
                              m_uint8_t *type,m_uint8_t *len,size_t *offset)
{
   m_uint8_t tmp;

   /* Read field type */
   if (cisco_eeprom_get_byte(eeprom,eeprom_len,(*offset)++,type) == -1)
      return(-1);

   /* No more field */
   if (*type == 0xFF)
      return(0);

   /* Get field length */
   tmp = (*type >> 6) & 0x03;

   if (tmp == 0x03) {
      /* Variable len */
      if (cisco_eeprom_get_byte(eeprom,eeprom_len,(*offset)++,&tmp) == -1)
         return(-1);

      *len = tmp & 0x0F;
   } else {
      /* Fixed len */
      *len = 1 << tmp;
   }

   return(1);
}

/* Dump a Cisco EEPROM with format version 4 */
void cisco_eeprom_v4_dump(m_uint16_t *eeprom,size_t eeprom_len)
{
   m_uint8_t type,len,tmp;
   size_t i,offset=2;

   printf("Dumping EEPROM contents:\n");

   do {
      /* Read field */
      if (cisco_eeprom_v4_get_field(eeprom,eeprom_len,&type,&len,&offset) < 1)
         break;

      printf("  Field 0x%2.2x: ",type);

      for(i=0;i<len;i++) {
         if (cisco_eeprom_get_byte(eeprom,eeprom_len,offset+i,&tmp) == -1)
            break;

         printf("%2.2x ",tmp);
      }

      printf("\n");

      offset += len;
   }while(offset < eeprom_len);
}

/* Returns the offset of the specified field */
int cisco_eeprom_v4_find_field(m_uint16_t *eeprom,size_t eeprom_len,
                               m_uint8_t field_type,size_t *field_offset)
{
   m_uint8_t type,len;
   size_t offset=2;

   do {
      /* Read field */
      if (cisco_eeprom_v4_get_field(eeprom,eeprom_len,&type,&len,&offset) < 1)
         break;

      if (type == field_type) {
         *field_offset = offset;
         return(0);
      }

      offset += len;
   }while(offset < eeprom_len);

   return(-1);
}
