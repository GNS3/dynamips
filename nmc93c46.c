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

/* Check status of a bit */
static inline int check_bit(u_int old,u_int new,int bit)
{
   int mask = 1 << bit;

   if ((old & mask) && !(new & mask))
      return(1);   /* bit unset */
   
   if (!(old & mask) && (new & mask))
      return(2);   /* bit set */

   /* no change */
   return(0);
}

/* Check chip select */
void nmc93c46_check_cs(struct nmc93c46_eeprom *p,u_int old,u_int new)
{
   int i,res;

   for(i=0;i<p->nr_groups;i++)
   {
      if (p->debug)
      {
         printf("EEPROM %s(%d): check_cs: check_bit(old,new,select_bit) "
                "[%8.8x, %8.8x, %d (mask = %8.8x)] = %d\n",
                p->description, i,
                old, new, p->def[i]->select_bit, 1 << p->def[i]->select_bit,
                check_bit(old,new,p->def[i]->select_bit));
      }

      if ((res = check_bit(old,new,p->def[i]->select_bit)) != 0) {
         p->state[i].cmd_len = 0;     /* no bit for command sent now */
         p->state[i].cmd_val = 0;

         if (res == 2)
            p->state[i].state = EEPROM_STATE_WAIT_CMD;
         else
            p->state[i].state = EEPROM_STATE_INACTIVE;
      }
   }
}

/* Check clock set for a specific group */
void nmc93c46_check_clk_group(struct nmc93c46_eeprom *p,int group_id,
                              u_int old,u_int new)
{
   unsigned int cmd,op,addr,pos;
   u_int clk_bit, din_bit;

   clk_bit = p->def[group_id]->clock_bit;
   din_bit = p->def[group_id]->din_bit;

   if (p->debug)
   {
      printf("EEPROM %s(%d): check_clk: check_bit(old,new,select_bit)"
             "[%8.8x, %8.8x, %d (mask = %8.8x)] = %d\n",
             p->description, group_id,
             old,new, clk_bit, 1 << clk_bit, check_bit(old,new,clk_bit));
   }

   /* CLK bit set ? */
   if (check_bit(old,new,clk_bit) != 2)
      return;

   switch(p->state[group_id].state)
   {
      case EEPROM_STATE_WAIT_CMD:
         /* Read DATAIN bit */
         if (new & (1 << din_bit))
            p->state[group_id].cmd_val |= (1 << p->state[group_id].cmd_len);

         p->state[group_id].cmd_len++;

         /* Command is complete ? */
         if (p->state[group_id].cmd_len == NMC93C46_CMD_BITLEN) 
         {
#if DEBUG_EEPROM
            printf("nmc93c46: %s(%d): command = %x\n", 
                   p->description,group_id,p->state[group_id].cmd_val);
#endif
            p->state[group_id].cmd_len = 0;

            /* we have the command! extract the opcode */
            cmd = p->state[group_id].cmd_val;
            op = cmd & 0x3;
             
            switch(op) {
               case NMC93C46_CMD_READ:
                  p->state[group_id].state = EEPROM_STATE_DATAOUT;
                  p->state[group_id].dataout_pos = 0;
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
          
         cmd = p->state[group_id].cmd_val;
         addr = (cmd >> 3) & 0x3f;

#if DEBUG_EEPROM
         if (p->state[group_id].dataout_pos == 0)
            printf("nmc93c46: %s(%d): read addr = %x (%d), data=%4.4x, "
                   "val = %4.4x\n",
                   p->description,group_id,
                   addr,addr,p->def[group_id]->data[addr],
                   p->state[group_id].cmd_val);
#endif
          
         pos = p->state[group_id].dataout_pos++;

         if (addr <= p->def[group_id]->data_len) {
            p->state[group_id].dataout_val = 
               p->def[group_id]->data[addr] & (1 << pos);
         } else {
            /* access out of bounds */
            p->state[group_id].dataout_val = (1 << pos);
         }

         if (p->state[group_id].dataout_pos == NMC93C46_CMD_DATALEN) {
            p->state[group_id].state = EEPROM_STATE_INACTIVE;
            p->state[group_id].dataout_pos = 0;
         }
         break;

#if DEBUG_EEPROM
      default:
         printf("nmc93c46: unhandled state %d\n",p->state[group_id].state);
#endif
   }
}

/* Check clock set for all group */
void nmc93c46_check_clk(struct nmc93c46_eeprom *p,u_int old,u_int new)
{
   int i;

   for(i=0;i<p->nr_groups;i++)
      nmc93c46_check_clk_group(p,i,old,new);
}

/* Handle write */
void nmc93c46_write(struct nmc93c46_eeprom *p,u_int data)
{
   u_int new = data, old = p->eeprom_reg;

   nmc93c46_check_cs(p,old,new);
   nmc93c46_check_clk(p,old,new);
   p->eeprom_reg = new;
}

/* Handle read */
unsigned int nmc93c46_read(struct nmc93c46_eeprom *p)
{
   unsigned int res;
   int i;

   res = p->eeprom_reg;

   for(i=0;i<p->nr_groups;i++) {
      if (p->state[i].dataout_val)
         res |= 1 << p->def[i]->dout_bit;
      else
         res &= ~(1 << p->def[i]->dout_bit);
   }

   return(res);
}

