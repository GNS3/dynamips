/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * NMC93C46/NMC93C56 Serial EEPROM.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nmc93cX6.h"

#define DEBUG_EEPROM  0

/* Internal states */
enum {
   EEPROM_STATE_INACTIVE = 0,
   EEPROM_STATE_WAIT_CMD,
   EEPROM_STATE_DATAOUT,
};

/* Get command length for the specified group */
static u_int nmc94cX6_get_cmd_len(struct nmc93cX6_group *g)
{
   switch(g->eeprom_type) {
      case EEPROM_TYPE_NMC93C46:
         return(NMC93C46_CMD_BITLEN);
      case EEPROM_TYPE_NMC93C56:
         return(NMC93C56_CMD_BITLEN);
      default:
         return(0);
   }
}

/* Extract EEPROM data address */
static u_int nmc94cX6_get_addr(struct nmc93cX6_group *g,u_int cmd)
{
   switch(g->eeprom_type) {
      case EEPROM_TYPE_NMC93C46:
         return((cmd >> 3) & 0x3f);
      case EEPROM_TYPE_NMC93C56:
         return(m_reverse_u8((cmd >> 3) & 0xff));
      default:
         return(0);
   }
}

/* Check chip select */
static void nmc93cX6_check_cs(struct nmc93cX6_group *g,u_int old,u_int new)
{
   int i,res;

   for(i=0;i<g->nr_eeprom;i++)
   {
      if (g->dout_status == EEPROM_DOUT_HIGH)
         g->state[i].dataout_val = 1;

      if (g->debug)
      {
         printf("EEPROM %s(%d): check_cs:  check_bit(old,new,select_bit) "
                "[%8.8x, %8.8x, %d (mask = %8.8x)] = %d\n",
                g->description, i,
                old, new, g->def[i]->select_bit, 1 << g->def[i]->select_bit,
                check_bit(old,new,g->def[i]->select_bit));
      }

      if ((res = check_bit(old,new,g->def[i]->select_bit)) != 0) {
         g->state[i].cmd_len = 0;     /* no bit for command sent now */
         g->state[i].cmd_val = 0;
         //g->state[i].dataout_val = 1;

         if (res == 2)
            g->state[i].state = EEPROM_STATE_WAIT_CMD;
         else
            g->state[i].state = EEPROM_STATE_INACTIVE;
      }
   }
}

/* Check clock set for a specific group */
static void nmc93cX6_check_clk_group(struct nmc93cX6_group *g,int group_id,
                                     u_int old,u_int new)
{
   struct cisco_eeprom *eeprom;
   u_int cmd,op,addr,pos;
   u_int clk_bit, din_bit;
   u_int cmd_len;

   clk_bit = g->def[group_id]->clock_bit;
   din_bit = g->def[group_id]->din_bit;

   if (g->debug)
   {
      printf("EEPROM %s(%d): check_clk: check_bit(old,new,select_bit) "
             "[%8.8x, %8.8x, %d (mask = %8.8x)] = %d\n",
             g->description, group_id,
             old, new, clk_bit, 1 << clk_bit, check_bit(old,new,clk_bit));
   }

   /* CLK bit set ? */
   if (check_bit(old,new,clk_bit) != 2)
      return;

   switch(g->state[group_id].state)
   {
      case EEPROM_STATE_WAIT_CMD:
         /* The first bit must be set to "1" */
         if ((g->state[group_id].cmd_len == 0) && !(new & (1 << din_bit)))
            break;

         /* Read DATAIN bit */
         if (new & (1 << din_bit))
            g->state[group_id].cmd_val |= (1 << g->state[group_id].cmd_len);

         g->state[group_id].cmd_len++;

         cmd_len = nmc94cX6_get_cmd_len(g);

         /* Command is complete ? */
         if (g->state[group_id].cmd_len == cmd_len)
         {
#if DEBUG_EEPROM
            printf("nmc93cX6: %s(%d): command = %x\n", 
                   g->description,group_id,g->state[group_id].cmd_val);
#endif
            g->state[group_id].cmd_len = 0;

            /* we have the command! extract the opcode */
            cmd = g->state[group_id].cmd_val;
            op = cmd & 0x7;

            switch(op) {
               case NMC93CX6_CMD_READ:
                  g->state[group_id].state = EEPROM_STATE_DATAOUT;
                  g->state[group_id].dataout_pos = 0;
                  break;
#if DEBUG_EEPROM
               default:
                  printf("nmc93cX6: unhandled opcode %d\n",op);
#endif
            }
         }

         break;

      case EEPROM_STATE_DATAOUT:
         /* 
          * user want to read data. we read 16-bits.
          * extract address (6/9 bits) from command.
          */
          
         cmd = g->state[group_id].cmd_val;
         addr = nmc94cX6_get_addr(g,cmd);

#if DEBUG_EEPROM
         if (g->state[group_id].dataout_pos == 0) {
            printf("nmc93cX6: %s(%d): "
                   "read addr=%x (%d), val=%4.4x [eeprom=%p]\n",
                   g->description,group_id,addr,addr,
                   g->state[group_id].cmd_val,
                   g->eeprom[group_id]);
         }
#endif
          
         pos = g->state[group_id].dataout_pos++;

         if (g->reverse_data)
            pos = 15 - pos;

         eeprom = g->eeprom[group_id];

         if (eeprom && eeprom->data && (addr < eeprom->len)) {
            g->state[group_id].dataout_val = eeprom->data[addr] & (1 << pos);
         } else {
            /* access out of bounds */
            g->state[group_id].dataout_val = (1 << pos);
         }

         if (g->state[group_id].dataout_pos == NMC93CX6_CMD_DATALEN) {
            g->state[group_id].state = EEPROM_STATE_INACTIVE;
            g->state[group_id].dataout_pos = 0;
         }
         break;

#if DEBUG_EEPROM
      default:
         printf("nmc93cX6: unhandled state %d\n",g->state[group_id].state);
#endif
   }
}

/* Check clock set for all group */
void nmc93cX6_check_clk(struct nmc93cX6_group *g,u_int old,u_int new)
{
   int i;

   for(i=0;i<g->nr_eeprom;i++)
      nmc93cX6_check_clk_group(g,i,old,new);
}

/* Handle write */
void nmc93cX6_write(struct nmc93cX6_group *g,u_int data)
{
   u_int new = data, old = g->eeprom_reg;

   nmc93cX6_check_cs(g,old,new);
   nmc93cX6_check_clk(g,old,new);
   g->eeprom_reg = new;
}

/* Returns the TRUE if the EEPROM is active */
u_int nmc93cX6_is_active(struct nmc93cX6_group *g,u_int group_id)
{
   return(g->eeprom_reg & (1 << g->def[group_id]->select_bit));
}

/* Returns the DOUT bit value */
u_int nmc93cX6_get_dout(struct nmc93cX6_group *g,u_int group_id)
{
   if (g->state[group_id].dataout_val)
      return(1 << g->def[group_id]->dout_bit);
   else
      return(0);
}

/* Handle read */
u_int nmc93cX6_read(struct nmc93cX6_group *g)
{
   u_int res;
   int i;

   res = g->eeprom_reg;

   for(i=0;i<g->nr_eeprom;i++) {
      if (!(g->eeprom_reg & (1 << g->def[i]->select_bit)))
         continue;

      if (g->state[i].dataout_val)
         res |= 1 << g->def[i]->dout_bit;
      else
         res &= ~(1 << g->def[i]->dout_bit);
   }

   return(res);
}

