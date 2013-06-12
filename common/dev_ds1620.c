/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Dallas DS1620 Temperature sensors.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <termios.h>
#include <fcntl.h>
#include <pthread.h>

#include "utils.h"
#include "dev_ds1620.h"

/* DS1620 commands */
#define DS1620_READ_TEMP     0xAA
#define DS1620_READ_COUNTER  0xA0
#define DS1620_READ_SLOPE    0xA9
#define DS1620_WRITE_TH      0x01
#define DS1620_WRITE_TL      0x02
#define DS1620_READ_TH       0xA1
#define DS1620_READ_TL       0xA2
#define DS1620_START_CONVT   0xEE
#define DS1620_STOP_CONVT    0x22
#define DS1620_WRITE_CONFIG  0x0C
#define DS1620_READ_CONFIG   0xAC

/* DS1620 config register */
#define DS1620_CONFIG_STATUS_DONE   0x80
#define DS1620_CONFIG_STATUS_THF    0x40
#define DS1620_CONFIG_STATUS_TLF    0x20
#define DS1620_CONFIG_STATUS_CPU    0x02
#define DS1620_CONFIG_STATUS_1SHOT  0x01

/* Size of various operations in bits (command, config and temp data) */
#define DS1620_CMD_SIZE      8
#define DS1620_CONFIG_SIZE   8
#define DS1620_TEMP_SIZE     9

/* Internal states */
enum {
   DS1620_STATE_CMD_IN,
   DS1620_STATE_DATA_IN,
   DS1620_STATE_DATA_OUT,
};

/* Update status register (TH/TL values) */
static void ds1620_update_status(struct ds1620_data *d)
{
   if (d->temp >= d->reg_th)
      d->reg_config |= DS1620_CONFIG_STATUS_THF;

   if (d->temp <= d->reg_tl)
      d->reg_config |= DS1620_CONFIG_STATUS_TLF;
}

/* Set temperature */
void ds1620_set_temp(struct ds1620_data *d,int temp)
{
   d->temp = temp << 1;
   ds1620_update_status(d);
}

/* Set reset bit */
void ds1620_set_rst_bit(struct ds1620_data *d,u_int rst_bit)
{
   if (!rst_bit) {
      d->state = DS1620_STATE_CMD_IN;
      d->cmd_pos = 0;
      d->cmd = 0;
      d->data = 0;
      d->data_pos = 0;
      d->data_len = 0;
   }
}

/* Set state after command */
static void ds1620_cmd_set_state(struct ds1620_data *d)
{
   d->data = 0;
   d->data_pos = 0;

   switch(d->cmd) {
      case DS1620_READ_TEMP:
         d->state = DS1620_STATE_DATA_OUT;
         d->data_len = DS1620_TEMP_SIZE;
         d->data = d->temp;
         break;

      case DS1620_READ_COUNTER:
      case DS1620_READ_SLOPE:
         d->state = DS1620_STATE_DATA_OUT;
         d->data_len = DS1620_TEMP_SIZE;
         d->data = 0;
         break;

      case DS1620_WRITE_TH:
      case DS1620_WRITE_TL:
         d->state = DS1620_STATE_DATA_IN;
         d->data_len = DS1620_TEMP_SIZE;
         break;

      case DS1620_READ_TH:
         d->state = DS1620_STATE_DATA_OUT;
         d->data_len = DS1620_TEMP_SIZE;
         d->data = d->reg_th;
         break;

      case DS1620_READ_TL:
         d->state = DS1620_STATE_DATA_OUT;
         d->data_len = DS1620_TEMP_SIZE;
         d->data = d->reg_tl;
         break;

      case DS1620_START_CONVT:
      case DS1620_STOP_CONVT:
         d->state = DS1620_STATE_CMD_IN;
         break;

      case DS1620_WRITE_CONFIG:
         d->state = DS1620_STATE_DATA_IN;
         d->data_len = DS1620_CONFIG_SIZE;
         break;

      case DS1620_READ_CONFIG:
         d->state = DS1620_STATE_DATA_OUT;
         d->data_len = DS1620_CONFIG_SIZE;
         d->data = d->reg_config;
         break;
   }
}

/* Execute command */
static void ds1620_exec_cmd(struct ds1620_data *d)
{
   switch(d->cmd) {
      case DS1620_WRITE_TH:
         d->reg_th = d->data;
         break;
      case DS1620_WRITE_TL:
         d->reg_tl = d->data;
         break;
      case DS1620_WRITE_CONFIG:
         d->reg_config = d->data;
         break;
   }

   /* return in command input state */
   d->state = DS1620_STATE_CMD_IN;
}

/* Write data bit */
void ds1620_write_data_bit(struct ds1620_data *d,u_int data_bit)
{
   /* CLK must be low */
   if (d->clk_bit != 0)
      return;

   switch(d->state) {
      case DS1620_STATE_CMD_IN:
         if (data_bit)
            d->cmd |= 1 << d->cmd_pos;
         
         if (++d->cmd_pos == DS1620_CMD_SIZE)
            ds1620_cmd_set_state(d);
         break;

      case DS1620_STATE_DATA_OUT:
         /* ignore input since it shouldn't happen */
         break;

      case DS1620_STATE_DATA_IN:
         if (data_bit)
            d->data |= 1 << d->data_pos;

         if (++d->data_pos == d->data_len) 
            ds1620_exec_cmd(d);
         break;
   }
}

/* Read data bit */
u_int ds1620_read_data_bit(struct ds1620_data *d)
{
   u_int val;

   if (d->state != DS1620_STATE_DATA_OUT)
      return(1);

   val = (d->data >> d->data_pos) & 0x1;

   if (++d->data_pos == d->data_len) {
      /* return in command input state */
      d->state = DS1620_STATE_CMD_IN;
   }

   return(val);
}

/* Initialize a DS1620 */
void ds1620_init(struct ds1620_data *d,int temp)
{
   memset(d,0,sizeof(*d));

   /* reset state */
   ds1620_set_rst_bit(d,0);

   /* set initial temperature */
   ds1620_set_temp(d,temp);

   /* chip in CPU mode (3-wire communications) */
   d->reg_config = DS1620_CONFIG_STATUS_CPU;   
}
