/*
 * Simulates a NMC93C46 Serial EEPROM.
 * Copyright (c) 2005,2006 Christophe Fillot.
 */

#ifndef __NMC93C46_H__
#define __NMC93C46_H__

#include <sys/types.h>

/* 8 groups with 4 differents bits (clock,select,data_in,data_out) */
#define NMC93C46_MAX_GROUPS  8

/* NMC93C46 EEPROM command bit length */
#define NMC93C46_CMD_BITLEN  9

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

struct nmc93c46_group_def {
   u_int clock_bit;
   u_int select_bit;
   u_int din_bit;
   u_int dout_bit;
   unsigned short *data;
   unsigned int data_len;
};

struct nmc93c46_group_state {   
   u_int cmd_len;
   u_int cmd_val;
   u_int state;
   u_int dataout_pos;
   u_int dataout_val;
};

struct nmc93c46_eeprom {
   u_int nr_groups;
   u_int eeprom_reg;
   char *description;
   int debug;
   struct nmc93c46_group_def *def[NMC93C46_MAX_GROUPS];
   struct nmc93c46_group_state state[NMC93C46_MAX_GROUPS];
};

/* Handle write */
void nmc93c46_write(struct nmc93c46_eeprom *p,unsigned int data);

/* Handle read */
unsigned int nmc93c46_read(struct nmc93c46_eeprom *p);

#endif /* __NMC93C46_H__ */
