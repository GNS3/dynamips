
/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Dallas DS1620 Temperature sensors.
 */

#ifndef __DEV_DS1620_H__
#define __DEV_DS1620_H__

#include "dynamips_common.h"

struct ds1620_data {
   u_int state;
   u_int clk_bit;
   int temp;

   /* command input */
   m_uint8_t cmd;
   u_int cmd_pos;
   
   /* data input/output */
   m_uint16_t data;
   u_int data_pos;
   u_int data_len;

   /* registers */
   m_uint8_t reg_config;
   m_uint16_t reg_th,reg_tl;
};

/* Set CLK bit */
static inline void ds1620_set_clk_bit(struct ds1620_data *d,u_int clk_bit)
{
   d->clk_bit = clk_bit;
}

/* Set temperature */
void ds1620_set_temp(struct ds1620_data *d,int temp);

/* Set reset bit */
void ds1620_set_rst_bit(struct ds1620_data *d,u_int rst_bit);

/* Write data bit */
void ds1620_write_data_bit(struct ds1620_data *d,u_int data_bit);

/* Read data bit */
u_int ds1620_read_data_bit(struct ds1620_data *d);

/* Initialize a DS1620 */
void ds1620_init(struct ds1620_data *d,int temp);

#endif
