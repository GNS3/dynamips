/*  
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 *
 * Cisco EEPROM manipulation functions.
 */

#ifndef __CISCO_EEPROM_H__
#define __CISCO_EEPROM_H__

#include "utils.h"

/* Cisco EEPROM */
struct cisco_eeprom {
   char *name;
   m_uint16_t *data;
   size_t len;
};

/* Find a NM EEPROM */
const struct cisco_eeprom *cisco_eeprom_find_nm(char *name);

/* Find a PA EEPROM */
const struct cisco_eeprom *cisco_eeprom_find_pa(char *name);

/* Find a WIC EEPROM */
const struct cisco_eeprom *cisco_eeprom_find_wic(char *name);

/* Find a C6k EEPROM */
const struct cisco_eeprom *cisco_eeprom_find_c6k(char *name);

/* Find an EEPROM in the specified EEPROM array */
const struct cisco_eeprom *
cisco_eeprom_find(const struct cisco_eeprom *eeproms,char *name);

/* Copy an EEPROM */
int cisco_eeprom_copy(struct cisco_eeprom *dst,const struct cisco_eeprom *src);

/* Free resources used by an EEPROM */
void cisco_eeprom_free(struct cisco_eeprom *eeprom);

/* Return TRUE if the specified EEPROM contains usable data */
int cisco_eeprom_valid(struct cisco_eeprom *eeprom);

/* Get a byte from an EEPROM */
int cisco_eeprom_get_byte(struct cisco_eeprom *eeprom,
                          size_t offset,m_uint8_t *val);

/* Set a byte to an EEPROM */
int cisco_eeprom_set_byte(struct cisco_eeprom *eeprom,
                          size_t offset,m_uint8_t val);

/* Get an EEPROM region */
int cisco_eeprom_get_region(struct cisco_eeprom *eeprom,size_t offset,
                            m_uint8_t *data,size_t data_len);

/* Set an EEPROM region */
int cisco_eeprom_set_region(struct cisco_eeprom *eeprom,size_t offset,
                            m_uint8_t *data,size_t data_len);

/* Get a field of a Cisco EEPROM v4 */
int cisco_eeprom_v4_get_field(struct cisco_eeprom *eeprom,m_uint8_t *type,
                              m_uint8_t *len,size_t *offset);

/* Dump a Cisco EEPROM with format version 4 */
void cisco_eeprom_v4_dump(struct cisco_eeprom *eeprom);

/* Dump a Cisco EEPROM with unformatted */
void cisco_eeprom_dump(struct cisco_eeprom *eeprom);

/* Returns the offset of the specified field */
int cisco_eeprom_v4_find_field(struct cisco_eeprom *eeprom,
                               m_uint8_t field_type,
                               size_t *field_offset);

#endif

