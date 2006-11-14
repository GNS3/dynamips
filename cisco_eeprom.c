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

/* ====================================================================== */
/* NM-1E: 1 Ethernet Port Network Module EEPROM                           */
/* ====================================================================== */
static const m_uint16_t eeprom_nm_1e_data[] = {
   0x0143, 0x0100, 0x0075, 0xCD81, 0x500D, 0xA201, 0x0000, 0x0000,
   0x5800, 0x0000, 0x9803, 0x2000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* NM-4E: 4 Ethernet Port Network Module EEPROM                           */
/* ====================================================================== */
static const m_uint16_t eeprom_nm_4e_data[] = {
   0x0142, 0x0100, 0x0075, 0xCD81, 0x500D, 0xA201, 0x0000, 0x0000,
   0x5800, 0x0000, 0x9803, 0x2000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* NM-1FE-TX: 1 FastEthernet Port Network Module EEPROM                   */
/* ====================================================================== */
static const m_uint16_t eeprom_nm_1fe_tx_data[] = {
   0x0144, 0x0100, 0x0075, 0xCD81, 0x500D, 0xA201, 0x0000, 0x0000,
   0x5800, 0x0000, 0x9803, 0x2000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* NM-16ESW: 16 FastEthernet Port Switch Network Module EEPROM            */
/* ====================================================================== */
static m_uint16_t eeprom_nm_16esw_data[] = {
   0x04FF, 0x4002, 0xA941, 0x0100, 0xC046, 0x0320, 0x003B, 0x3401,
   0x4245, 0x3080, 0x0000, 0x0000, 0x0203, 0xC18B, 0x3030, 0x3030,
   0x3030, 0x3030, 0x3030, 0x3003, 0x0081, 0x0000, 0x0000, 0x0400,
   0xCF06, 0x0013, 0x1A1D, 0x0BD1, 0x4300, 0x11FF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 
};

/* ====================================================================== */
/* NM-4T: 4 Serial Network Module EEPROM                                  */
/* ====================================================================== */
static m_uint16_t eeprom_nm_4t_data[] = {
   0x0154, 0x0101, 0x009D, 0x2D64, 0x5009, 0x0A02, 0x0000, 0x0000,
   0x5800, 0x0000, 0x9811, 0x0300, 0x0005, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* NM EEPROMs                                                             */
/* ====================================================================== */

static const struct cisco_eeprom eeprom_nm_array[] = {
   { "NM-1E", (m_uint16_t *)eeprom_nm_1e_data, sizeof(eeprom_nm_1e_data)/2 },
   { "NM-4E", (m_uint16_t *)eeprom_nm_4e_data, sizeof(eeprom_nm_4e_data)/2 },
   { "NM-1FE-TX", (m_uint16_t *)eeprom_nm_1fe_tx_data, 
     sizeof(eeprom_nm_1fe_tx_data)/2 },
   { "NM-16ESW", (m_uint16_t *)eeprom_nm_16esw_data, 
     sizeof(eeprom_nm_16esw_data)/2 },
   { "NM-4T", eeprom_nm_4t_data, sizeof(eeprom_nm_4t_data)/2 },
   { NULL, NULL, 0 },
};

/* Find a NM EEPROM */
const struct cisco_eeprom *cisco_eeprom_find_nm(char *name)
{
   return(cisco_eeprom_find(eeprom_nm_array,name));
}

/* ====================================================================== */
/* PA-FE-TX: 1 FastEthernet Port Adapter EEPROM                           */
/* ====================================================================== */
static const m_uint16_t eeprom_pa_fe_tx_data[] = {
   0x0111, 0x0102, 0xffff, 0xffff, 0x4906, 0x9804, 0x0000, 0x0000,
   0x6000, 0x0000, 0x9812, 0x1700, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* PA-4E: 4 Ethernet Port Adapter EEPROM                                  */
/* ====================================================================== */
static const m_uint16_t eeprom_pa_4e_data[] = {
   0x0102, 0x010E, 0xFFFF, 0xFFFF, 0x4906, 0x1404, 0x0000, 0x0000,
   0x5000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* PA-8E: 8 Ethernet Port Adapter EEPROM                                  */
/* ====================================================================== */
static const m_uint16_t eeprom_pa_8e_data[] = {
   0x0101, 0x010E, 0xFFFF, 0xFFFF, 0x4906, 0x1404, 0x0000, 0x0000,
   0x5000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* PA-4T+: 4 Serial Port Adapter EEPROM                                   */
/* ====================================================================== */
static m_uint16_t eeprom_pa_4t_data[] = {
   0x010C, 0x010F, 0xffff, 0xffff, 0x4906, 0x2E07, 0x0000, 0x0000,
   0x5000, 0x0000, 0x0010, 0x2400, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* PA-8T: 8 Serial Port Adapter EEPROM                                    */
/* ====================================================================== */
static m_uint16_t eeprom_pa_8t_data[] = {
   0x010E, 0x010F, 0xffff, 0xffff, 0x4906, 0x2E07, 0x0000, 0x0000,
   0x5000, 0x0000, 0x0010, 0x2400, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* PA-A1: 1 ATM Port Adapter EEPROM                                       */
/* ====================================================================== */
static const m_uint16_t eeprom_pa_a1_data[] = {
   0x0117, 0x010F, 0xffff, 0xffff, 0x4906, 0x2E07, 0x0000, 0x0000,
   0x5000, 0x0000, 0x0010, 0x2400, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* PA-POS-OC3: 1 POS Port Adapter EEPROM                                   */
/* ====================================================================== */
static const m_uint16_t eeprom_pa_pos_oc3_data[] = {
   0x0196, 0x0202, 0xffff, 0xffff, 0x490C, 0x7806, 0x0000, 0x0000,
   0x5000, 0x0000, 0x0208, 0x1900, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* PA-4B: 4 BRI Port Adapter EEPROM                                       */
/* ====================================================================== */
static const m_uint16_t eeprom_pa_4b_data[] = {
   0x013D, 0x0202, 0xffff, 0xffff, 0x490C, 0x7806, 0x0000, 0x0000,
   0x5000, 0x0000, 0x0208, 0x1900, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* PA-MC-8TE1                                                             */
/* ====================================================================== */
static const m_uint16_t eeprom_pa_mc8te1_data[] = {
   0x04FF, 0x4003, 0x4E41, 0x0200, 0xC18B, 0x4A41, 0x4530, 0x3834,
   0x3159, 0x3251, 0x3082, 0x491D, 0x7D02, 0x4241, 0x3003, 0x0081,
   0x0000, 0x0000, 0x0400, 0x8000, 0x0127, 0x9BCB, 0x9450, 0x412D,
   0x4D43, 0x2D38, 0x5445, 0x312B, 0x2020, 0x2020, 0x2020, 0x2020,
   0x20C0, 0x4603, 0x2000, 0x4BBB, 0x02FF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* ====================================================================== */
/* PA EEPROMs                                                             */
/* ====================================================================== */

static const struct cisco_eeprom eeprom_pa_array[] = {
   { "PA-FE-TX", (m_uint16_t *)eeprom_pa_fe_tx_data, 
     sizeof(eeprom_pa_fe_tx_data)/2 },
   { "PA-4E", (m_uint16_t *)eeprom_pa_4e_data, sizeof(eeprom_pa_4e_data)/2 },
   { "PA-8E", (m_uint16_t *)eeprom_pa_8e_data, sizeof(eeprom_pa_8e_data)/2 },
   { "PA-4T+", eeprom_pa_4t_data, sizeof(eeprom_pa_4t_data)/2 },
   { "PA-8T", eeprom_pa_8t_data, sizeof(eeprom_pa_8t_data)/2 },
   { "PA-A1", (m_uint16_t *)eeprom_pa_a1_data, sizeof(eeprom_pa_a1_data)/2 },
   { "PA-POS-OC3", (m_uint16_t *)eeprom_pa_pos_oc3_data,
     sizeof(eeprom_pa_pos_oc3_data)/2 },
   { "PA-4B", (m_uint16_t *)eeprom_pa_4b_data, sizeof(eeprom_pa_4b_data)/2 },
   { "PA-MC-8TE1", (m_uint16_t *)eeprom_pa_mc8te1_data, 
     sizeof(eeprom_pa_mc8te1_data)/2 },
   { NULL, NULL, 0 },
};

/* Find a PA EEPROM */
const struct cisco_eeprom *cisco_eeprom_find_pa(char *name)
{
   return(cisco_eeprom_find(eeprom_pa_array,name));
}

/* ====================================================================== */
/* Utility functions                                                      */
/* ====================================================================== */

/* Find an EEPROM in the specified EEPROM array */
const struct cisco_eeprom *
cisco_eeprom_find(const struct cisco_eeprom *eeproms,char *name)
{
   int i;

   for(i=0;eeproms[i].name;i++)
      if (!strcmp(eeproms[i].name,name))
         return(&eeproms[i]);

   return NULL;
}

/* Copy an EEPROM */
int cisco_eeprom_copy(struct cisco_eeprom *dst,const struct cisco_eeprom *src)
{
   m_uint16_t *data;

   if (!src || !src)
      return(-1);

   cisco_eeprom_free(dst);

   if (!(data = malloc(src->len << 1)))
      return(-1);

   memcpy(data,src->data,src->len << 1);
   dst->name = src->name;
   dst->data = data;
   dst->len  = src->len;
   return(0);
}

/* Free resources used by an EEPROM */
void cisco_eeprom_free(struct cisco_eeprom *eeprom)
{
   if (eeprom && eeprom->data) {
      free(eeprom->data);
      eeprom->data = NULL;
      eeprom->len  = 0;
   }
}

/* Return TRUE if the specified EEPROM contains usable data */
int cisco_eeprom_valid(struct cisco_eeprom *eeprom)
{
   return((eeprom && eeprom->data) ? TRUE : FALSE);
}

/* Get a byte from an EEPROM */
int cisco_eeprom_get_byte(struct cisco_eeprom *eeprom,
                          size_t offset,m_uint8_t *val)
{
   m_uint16_t tmp;
   
   if (offset >= (eeprom->len << 1))
      return(-1);

   tmp = eeprom->data[offset >> 1];

   if (!(offset & 1))
      tmp >>= 8;

   *val = tmp & 0xFF;
   return(0);
}

/* Set a byte to an EEPROM */
int cisco_eeprom_set_byte(struct cisco_eeprom *eeprom,
                          size_t offset,m_uint8_t val)
{
   m_uint16_t tmp;

   if (offset >= (eeprom->len << 1))
      return(-1);

   tmp = eeprom->data[offset >> 1];

   if (offset & 1)
      tmp = (tmp & 0xFF00) | val;
   else
      tmp = (tmp & 0x00FF) | (val << 8);

   eeprom->data[offset >> 1] = tmp;
   return(0);
}

/* Get an EEPROM region */
int cisco_eeprom_get_region(struct cisco_eeprom *eeprom,size_t offset,
                            m_uint8_t *data,size_t data_len)
{
   size_t i;

   for(i=0;i<data_len;i++) {
      if (cisco_eeprom_get_byte(eeprom,offset+i,&data[i]) == -1)
         return(-1);
   }

   return(0);
}

/* Set an EEPROM region */
int cisco_eeprom_set_region(struct cisco_eeprom *eeprom,size_t offset,
                            m_uint8_t *data,size_t data_len)
{
   size_t i;

   for(i=0;i<data_len;i++) {
      if (cisco_eeprom_set_byte(eeprom,offset+i,data[i]) == -1)
         return(-1);
   }

   return(0);
}

/* Get a field of a Cisco EEPROM v4 */
int cisco_eeprom_v4_get_field(struct cisco_eeprom *eeprom,m_uint8_t *type,
                              m_uint8_t *len,size_t *offset)
{
   m_uint8_t tmp;

   /* Read field type */
   if (cisco_eeprom_get_byte(eeprom,(*offset)++,type) == -1)
      return(-1);

   /* No more field */
   if (*type == 0xFF)
      return(0);

   /* Get field length */
   tmp = (*type >> 6) & 0x03;

   if (tmp == 0x03) {
      /* Variable len */
      if (cisco_eeprom_get_byte(eeprom,(*offset)++,&tmp) == -1)
         return(-1);

      *len = tmp & 0x0F;
   } else {
      /* Fixed len */
      *len = 1 << tmp;
   }

   return(1);
}

/* Dump a Cisco EEPROM with format version 4 */
void cisco_eeprom_v4_dump(struct cisco_eeprom *eeprom)
{
   m_uint8_t type,len,tmp;
   size_t i,offset=2;

   printf("Dumping EEPROM contents:\n");

   do {
      /* Read field */
      if (cisco_eeprom_v4_get_field(eeprom,&type,&len,&offset) < 1)
         break;

      printf("  Field 0x%2.2x: ",type);

      for(i=0;i<len;i++) {
         if (cisco_eeprom_get_byte(eeprom,offset+i,&tmp) == -1)
            break;

         printf("%2.2x ",tmp);
      }

      printf("\n");

      offset += len;
   }while(offset < (eeprom->len << 1));
}

/* Returns the offset of the specified field */
int cisco_eeprom_v4_find_field(struct cisco_eeprom *eeprom,
                               m_uint8_t field_type,
                               size_t *field_offset)
{
   m_uint8_t type,len;
   size_t offset=2;

   do {
      /* Read field */
      if (cisco_eeprom_v4_get_field(eeprom,&type,&len,&offset) < 1)
         break;

      if (type == field_type) {
         *field_offset = offset;
         return(0);
      }

      offset += len;
   }while(offset < (eeprom->len << 1));

   return(-1);
}
