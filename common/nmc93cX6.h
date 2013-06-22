/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot.
 *
 * NMC93C46/NMC93C56 Serial EEPROM.
 */

#ifndef __NMC93CX6_H__
#define __NMC93CX6_H__

#include <sys/types.h>
#include "utils.h"
#include "cisco_eeprom.h"

/* EEPROM types */
enum {
   EEPROM_TYPE_NMC93C46,
   EEPROM_TYPE_NMC93C56,
};

/* EEPROM data bit order */
enum {
   EEPROM_DORD_NORMAL = 0,
   EEPROM_DORD_REVERSED,
};

/* EEPROM debugging */
enum {
   EEPROM_DEBUG_DISABLED = 0,
   EEPROM_DEBUG_ENABLED,
};

/* EEPROM DOUT default status */
enum {
   EEPROM_DOUT_HIGH = 0,
   EEPROM_DOUT_KEEP,
};

/* 8 groups with 4 differents bits (clock,select,data_in,data_out) */
#define NMC93CX6_MAX_EEPROM_PER_GROUP  16

/* NMC93C46 EEPROM command bit length */
#define NMC93C46_CMD_BITLEN   9

/* NMC93C56 EEPROM command bit length */
#define NMC93C56_CMD_BITLEN   11

/* NMC93C46 EEPROM data bit length */
#define NMC93CX6_CMD_DATALEN  16

/* NMC93C46 EEPROM commands:     SB (1) OP(2) Address(6/9) */
#define NMC93CX6_CMD_CONTROL   	 (0x1 | 0x0)
#define NMC93CX6_CMD_WRDS      	 (0x1 | 0x0 | 0x00)
#define NMC93CX6_CMD_ERASE_ALL 	 (0x1 | 0x0 | 0x08)
#define NMC93CX6_CMD_WRITE_ALL 	 (0x1 | 0x0 | 0x10)
#define NMC93CX6_CMD_WREN      	 (0x1 | 0x0 | 0x18)
#define NMC93CX6_CMD_READ      	 (0x1 | 0x2)
#define NMC93CX6_CMD_WRITE     	 (0x1 | 0x4)
#define NMC93CX6_CMD_ERASE     	 (0x1 | 0x6)

struct nmc93cX6_eeprom_def {
   u_int clock_bit;
   u_int select_bit;
   u_int din_bit;
   u_int dout_bit;
};

struct nmc93cX6_eeprom_state {   
   u_int cmd_len;
   u_int cmd_val;
   u_int state;
   u_int dataout_pos;
   u_int dataout_val;
};

struct nmc93cX6_group {
   u_int eeprom_type;
   u_int nr_eeprom;
   u_int eeprom_reg;
   u_int reverse_data;
   u_int dout_status;
   int debug;
   char *description;
   const struct nmc93cX6_eeprom_def *def[NMC93CX6_MAX_EEPROM_PER_GROUP];
   struct nmc93cX6_eeprom_state state[NMC93CX6_MAX_EEPROM_PER_GROUP];
   struct cisco_eeprom *eeprom[NMC93CX6_MAX_EEPROM_PER_GROUP];
};

/* Handle write */
void nmc93cX6_write(struct nmc93cX6_group *g,u_int data);

/* Returns the TRUE if the EEPROM is active */
u_int nmc93cX6_is_active(struct nmc93cX6_group *g,u_int group_id);

/* Returns the DOUT bit value */
u_int nmc93cX6_get_dout(struct nmc93cX6_group *g,u_int group_id);

/* Handle read */
u_int nmc93cX6_read(struct nmc93cX6_group *p);

#endif /* __NMC93CX6_H__ */
