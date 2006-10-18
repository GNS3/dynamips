/*
 * Simulates a NMC93C46 Serial EEPROM.
 * Copyright (c) 2005,2006 Christophe Fillot.
 */

#ifndef __NMC93C46_H__
#define __NMC93C46_H__

#include <sys/types.h>
#include "utils.h"
#include "cisco_eeprom.h"

/* 8 groups with 4 differents bits (clock,select,data_in,data_out) */
#define NMC93C46_MAX_EEPROM_PER_GROUP  8

/* NMC93C46 EEPROM command bit length */
#define NMC93C46_CMD_BITLEN   9

/* NMC93C46 EEPROM data bit length */
#define NMC93C46_CMD_DATALEN  16

/* NMC93C46 EEPROM commands:     SB (1) OP(2) Address(5) */
#define NMC93C46_CMD_CONTROL   	 (0x1 | 0x0)
#define NMC93C46_CMD_WRDS      	 (0x1 | 0x0 | 0x00)
#define NMC93C46_CMD_ERASE_ALL 	 (0x1 | 0x0 | 0x08)
#define NMC93C46_CMD_WRITE_ALL 	 (0x1 | 0x0 | 0x10)
#define NMC93C46_CMD_WREN      	 (0x1 | 0x0 | 0x18)
#define NMC93C46_CMD_READ      	 (0x1 | 0x2)
#define NMC93C46_CMD_WRITE     	 (0x1 | 0x4)
#define NMC93C46_CMD_ERASE     	 (0x1 | 0x6)

struct nmc93c46_eeprom_def {
   u_int clock_bit;
   u_int select_bit;
   u_int din_bit;
   u_int dout_bit;
};

struct nmc93c46_eeprom_state {   
   u_int cmd_len;
   u_int cmd_val;
   u_int state;
   u_int dataout_pos;
   u_int dataout_val;
};

struct nmc93c46_group {
   u_int nr_eeprom;
   u_int eeprom_reg;
   char *description;
   int debug;
   const struct nmc93c46_eeprom_def *def[NMC93C46_MAX_EEPROM_PER_GROUP];
   struct nmc93c46_eeprom_state state[NMC93C46_MAX_EEPROM_PER_GROUP];
   struct cisco_eeprom *eeprom[NMC93C46_MAX_EEPROM_PER_GROUP];
};

/* Handle write */
void nmc93c46_write(struct nmc93c46_group *g,u_int data);

/* Handle read */
u_int nmc93c46_read(struct nmc93c46_group *p);

#endif /* __NMC93C46_H__ */
