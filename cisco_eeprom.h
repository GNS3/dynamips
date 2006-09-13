/*  
 * Cisco C7200 (Predator) simulation platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 *
 * Cisco EEPROM manipulation functions.
 */

#ifndef __CISCO_EEPROM_H__
#define __CISCO_EEPROM_H__

#include "utils.h"

/* Get a byte from an EEPROM */
int cisco_eeprom_get_byte(m_uint16_t *eeprom,size_t eeprom_len,
                          size_t offset,m_uint8_t *val);

/* Set a byte to an EEPROM */
int cisco_eeprom_set_byte(m_uint16_t *eeprom,size_t eeprom_len,
                          size_t offset,m_uint8_t val);

/* Get an EEPROM region */
int cisco_eeprom_get_region(m_uint16_t *eeprom,size_t eeprom_len,
                            size_t offset,m_uint8_t *data,size_t data_len);

/* Set an EEPROM region */
int cisco_eeprom_set_region(m_uint16_t *eeprom,size_t eeprom_len,
                            size_t offset,m_uint8_t *data,size_t data_len);

/* Dump a Cisco EEPROM with format version 4 */
void cisco_eeprom_v4_dump(m_uint16_t *eeprom,size_t eeprom_len);

/* Returns the offset of the specified field */
int cisco_eeprom_v4_find_field(m_uint16_t *eeprom,size_t eeprom_len,
                               m_uint8_t field_type,size_t *field_offset);

#endif

