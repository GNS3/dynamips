/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * NMC93C46 Serial EEPROM.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "nmc93c46.h"

#define DEBUG_EEPROM  0

/* Internal states */
enum {
   EEPROM_STATE_INACTIVE = 0,
   EEPROM_STATE_WAIT_CMD,
   EEPROM_STATE_DATAOUT,
};

/* Check chip select */
void nmc93c46_check_cs(struct nmc93c46_group *g,u_int old,u_int new)
{
   int i,res;

   for(i=0;i<g->nr_eeprom;i++)
   {
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

         if (res == 2)
            g->state[i].state = EEPROM_STATE_WAIT_CMD;
         else
            g->state[i].state = EEPROM_STATE_INACTIVE;
      }
   }
}

/* Check clock set for a specific group */
void nmc93c46_check_clk_group(struct nmc93c46_group *g,int group_id,
                              u_int old,u_int new)
{
   struct cisco_eeprom *eeprom;
   u_int cmd,op,addr,pos;
   u_int clk_bit, din_bit;

   clk_bit = g->def[group_id]->clock_bit;
   din_bit = g->def[group_id]->din_bit;

   if (g->debug)
   {
      printf("EEPROM %s(%d): check_clk: check_bit(old,new,select_bit) "
             "[%8.8x, %8.8x, %d (mask = %8.8x)] = %d\n",
             g->description, group_id,
             old,new, clk_bit, 1 << clk_bit, check_bit(old,new,clk_bit));
   }

   /* CLK bit set ? */
   if (check_bit(old,new,clk_bit) != 2)
      return;

   switch(g->state[group_id].state)
   {
      case EEPROM_STATE_WAIT_CMD:
         /* Read DATAIN bit */
         if (new & (1 << din_bit))
            g->state[group_id].cmd_val |= (1 << g->state[group_id].cmd_len);

         g->state[group_id].cmd_len++;

         /* Command is complete ? */
         if ((g->state[group_id].cmd_len == NMC93C46_CMD_BITLEN) &&
             (g->state[group_id].cmd_val & 1))
         {
#if DEBUG_EEPROM
            printf("nmc93c46: %s(%d): command = %x\n", 
                   g->description,group_id,g->state[group_id].cmd_val);
#endif
            g->state[group_id].cmd_len = 0;

            /* we have the command! extract the opcode */
            cmd = g->state[group_id].cmd_val;
            op = cmd & 0x7;
             
            switch(op) {
               case NMC93C46_CMD_READ:
                  g->state[group_id].state = EEPROM_STATE_DATAOUT;
                  g->state[group_id].dataout_pos = 0;
                  break;
#if DEBUG_EEPROM
               default:
                  printf("nmc93c46: unhandled opcode %d\n",op);
#endif
            }
         }

         break;

      case EEPROM_STATE_DATAOUT:
         /* 
          * user want to read data. we read 16-bits.
          * extract address (6 bits) from command.
          */
          
         cmd = g->state[group_id].cmd_val;
         addr = (cmd >> 3) & 0x3f;

#if DEBUG_EEPROM
         if (g->state[group_id].dataout_pos == 0)
            printf("nmc93c46: %s(%d): read addr = %x (%d), data=%4.4x, "
                   "val = %4.4x\n",
                   g->description,group_id,
                   addr,addr,g->def[group_id]->data[addr],
                   g->state[group_id].cmd_val);
#endif
          
         pos = g->state[group_id].dataout_pos++;
         eeprom = g->eeprom[group_id];

         if (eeprom && eeprom->data && (addr < eeprom->len)) {
            g->state[group_id].dataout_val = eeprom->data[addr] & (1 << pos);
         } else {
            /* access out of bounds */
            g->state[group_id].dataout_val = (1 << pos);
         }

         if (g->state[group_id].dataout_pos == NMC93C46_CMD_DATALEN) {
            g->state[group_id].state = EEPROM_STATE_INACTIVE;
            g->state[group_id].dataout_pos = 0;
         }
         break;

#if DEBUG_EEPROM
      default:
         printf("nmc93c46: unhandled state %d\n",g->state[group_id].state);
#endif
   }
}

/* Check clock set for all group */
void nmc93c46_check_clk(struct nmc93c46_group *g,u_int old,u_int new)
{
   int i;

   for(i=0;i<g->nr_eeprom;i++)
      nmc93c46_check_clk_group(g,i,old,new);
}

/* Handle write */
void nmc93c46_write(struct nmc93c46_group *g,u_int data)
{
   u_int new = data, old = g->eeprom_reg;

   nmc93c46_check_cs(g,old,new);
   nmc93c46_check_clk(g,old,new);
   g->eeprom_reg = new;
}

/* Handle read */
u_int nmc93c46_read(struct nmc93c46_group *g)
{
   u_int res;
   int i;

   res = g->eeprom_reg;

   for(i=0;i<g->nr_eeprom;i++) {
      if (g->state[i].dataout_val)
         res |= 1 << g->def[i]->dout_bit;
      else
         res &= ~(1 << g->def[i]->dout_bit);
   }

   return(res);
}

