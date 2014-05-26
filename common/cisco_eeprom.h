/*  
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot.  All rights reserved.
 *
 * Cisco EEPROM manipulation functions.
 */

#ifndef __CISCO_EEPROM_H__
#define __CISCO_EEPROM_H__

#include "utils.h"

/*
CISCO EEPROM format version 1 (size=0x20?)
0x00: 01(version)
0x01: XX(product id)
// TODO format might depend on the type or class of hardware
0x02: XX (.) XX(Hardware revision)
0x04: XX XX XX XX(Serial number)
0x08: XX (-) XX XX (-) XX(Part number)
0x0C: XX(Test history)
0x0D: XX (-) XX (-) XX(RMA number)
0x10: XX(Board Revision)
0x11: ...FF(padding?)
0x17: XX(Connector type)(0=PCI,1=Wan Module,other=PCI)
0x18: ...FF(padding?)
// 0x20+ is optional? ignored if FF FF...
0x26: XX XX XX XX(Version Identifier)(4 chars)
0x2A: XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX XX(FRU Part Number)(18 chars)
0x3C: ...FF(padding?)
*/

/*
CISCO EEPROM format version 4 (size=0x80?)
0x00: 04(version) FF(padding?)
0x02: {
   // {00000000b}.* adds 0x100 to id?
   // {LLDDDDDDb} has length=2^LLb(1,2,4) and id=DDDDDDb
   // {11DDDDDDb TTLLLLLLb} has id=DDDDDDb, length=LLLLLLb and type=TTb(00b=hex,01b=number,10b=string,11b=hex or reserved?)
   : 01 XX(Number of Slots)
   : 02 XX(Fab Version)
   : 03 XX(RMA Test History)
   : 04 XX(RMA History)
   : 05 XX(Connector Type)
   : 06 XX(EHSA Preferred Master)
   : 07 XX(Vendor ID)
   : 09 XX(Processor type)
   : 0B XX(Power Supply Type: 0=AC, !0=DC)
   : 0C XX(ignored?)
   : 40 XX XX(product id)
   : 41 XX (.) XX (Hardware Revision)
   : 42 XX XX(Board Revision)
   : 43 XXXX(MAC Address block size)
   : 44 XX XX(Capabilities)
   : 45 XX XX(Self test result)
   : 4A XX XX(Radio Country Code)
   : 80 XXXX XXXX(Deviation Number)
   : 81 XX (-) XX (-) XX (-) XX(RMA Number)
   : 82 XX (-) XXXX (-) XX(Part Number)
   : 83 XXXXXXXX(Hardware date code)
   : 84 XX XX XX XX(Manufacturing Engineer)
   : 85 XX (-) XXXX (-) XX(Fab Part Number)
   : C0 46 XX XX (-) XX XX XX (-) XX(Part Number)(number)
   : C1 8B XX XX XX XX XX XX XX XX XX XX XX(PCB Serial Number)(string)
   : C2 8B XX XX XX XX XX XX XX XX XX XX XX(Chassis Serial Number)(string)
   : C3 06 XX XX XX XX XX XX(Chassis MAC Address)
   : C4 08 XX XX XX XX XX XX XX XX(Manufacturing Test Data)
   : C5 08 XX XX XX XX XX XX XX XX(Field Diagnostics Data)
   : C6 8A XX XX XX XX XX XX XX XX XX XX(CLEI Code)(string)
   : C7 ?? XX?(ignored?)
   : C8 09 XX[min dBmV] XX[max dBmV] XX[num_values=3] XXXX[value_0] XXXX[value_1] XXXX[value_2](Calibration Data)
   : C9 ?? XX?(Platform features)(hex)
   : CB 88 XX XX XX XX XX XX XX XX(Product (FRU) Number)(string)
   : CF 06 XXXX (.) XXXX (.) XXXX(Base MAC Address)
}.*
0x??: ...FF(padding?)
*/

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

