/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * XXX TODO: proper context save/restore for CPUs.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "rbtree.h"
#include "mips64.h"
#include "dynamips.h"
#include "cp0.h"
#include "memory.h"
#include "device.h"

/* MIPS general purpose registers names */
char *mips64_gpr_reg_names[MIPS64_GPR_NR] = {
   "zr", "at", "v0", "v1", "a0", "a1", "a2", "a3",
   "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
   "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
   "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
};

/* Get register index given its name */
int mips64_get_reg_index(char *name)
{
   int i;

   for(i=0;i<MIPS64_GPR_NR;i++)
      if (!strcmp(mips64_gpr_reg_names[i],name))
         return(i);

   return(-1);
}

/* Initialize a MIPS64 processor */
void mips64_init(cpu_mips_t *cpu)
{
   memset(cpu,0,sizeof(*cpu));
   cpu->addr_bus_mask = 0xFFFFFFFFFFFFFFFFULL;
   cpu->pc = MIPS_ROM_PC;
   cpu->gpr[MIPS_GPR_SP] = MIPS_ROM_SP;
   cpu->cp0.reg[MIPS_CP0_STATUS] = MIPS_CP0_STATUS_BEV;

   cpu->insn_block_tree = rbtree_create((tree_fcompare)insn_block_cmp,NULL);
}

/* Update the IRQ flag (inline) */
static forced_inline void mips64_update_irq_flag_fast(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint32_t imask,sreg_mask;

   cpu->irq_pending = FALSE;

   sreg_mask = MIPS_CP0_STATUS_IE|MIPS_CP0_STATUS_EXL|MIPS_CP0_STATUS_ERL;

   if ((cp0->reg[MIPS_CP0_STATUS] & sreg_mask) == MIPS_CP0_STATUS_IE) {
      imask = cp0->reg[MIPS_CP0_STATUS] & MIPS_CP0_STATUS_IMASK;
      if (unlikely(cp0->reg[MIPS_CP0_CAUSE] & imask))
         cpu->irq_pending = TRUE;
   }
}

/* Update the IRQ flag */
void mips64_update_irq_flag(cpu_mips_t *cpu)
{
   mips64_update_irq_flag_fast(cpu);
}

/* Generate an exception */
void mips64_trigger_exception(cpu_mips_t *cpu,u_int exc_code,int bd_slot)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint64_t cause,vector;

   /* we don't set EPC if EXL is set */
   if (!(cp0->reg[MIPS_CP0_STATUS] & MIPS_CP0_STATUS_EXL))
   {
      cp0->reg[MIPS_CP0_EPC] = cpu->pc;

      /* keep IM, set exception code and bd slot */
      cause = cp0->reg[MIPS_CP0_CAUSE] & MIPS_CP0_CAUSE_IMASK;

      if (bd_slot)
         cause |= MIPS_CP0_CAUSE_BD_SLOT;
      else
         cause &= ~MIPS_CP0_CAUSE_BD_SLOT;

      cause |= (exc_code << MIPS_CP0_CAUSE_SHIFT);
      cp0->reg[MIPS_CP0_CAUSE] = cause;

      /* XXX properly set vector */
      vector = 0x180ULL;
   }
   else
   {
      /* keep IM and set exception code */
      cause = cp0->reg[MIPS_CP0_CAUSE] & MIPS_CP0_CAUSE_IMASK;
      cause |= (exc_code << MIPS_CP0_CAUSE_SHIFT);
      cp0->reg[MIPS_CP0_CAUSE] = cause;

      /* set vector */
      vector = 0x180ULL;
   }

   /* Set EXL bit in status register */
   cp0->reg[MIPS_CP0_STATUS] |= MIPS_CP0_STATUS_EXL;

   /* Use bootstrap vectors ? */
   if (cp0->reg[MIPS_CP0_STATUS] & MIPS_CP0_STATUS_BEV)
      cpu->pc = 0xffffffffbfc00200ULL + vector;
   else
      cpu->pc = 0xffffffff80000000ULL + vector;

   /* Clear the pending IRQ flag */
   mips64_update_irq_flag_fast(cpu);
}

/*
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
fastcall void mips64_exec_inc_cp0_cnt(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;

   cpu->cp0_virt_cnt_reg++;

   if (unlikely((cpu->cp0_virt_cnt_reg == cpu->cp0_virt_cmp_reg))) {
      cp0->reg[MIPS_CP0_COUNT] = (m_uint32_t)cp0->reg[MIPS_CP0_COMPARE];
      cp0->reg[MIPS_CP0_CAUSE] |= MIPS_CP0_CAUSE_IBIT7;
      mips64_update_irq_flag_fast(cpu);
   }
}

/* Trigger the Timer IRQ */
fastcall void mips64_trigger_timer_irq(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;

   cp0->reg[MIPS_CP0_COUNT] = (m_uint32_t)cp0->reg[MIPS_CP0_COMPARE];
   cp0->reg[MIPS_CP0_CAUSE] |= MIPS_CP0_CAUSE_IBIT7;
   mips64_update_irq_flag_fast(cpu);
}

/* Execute ERET instruction */
fastcall void mips64_exec_eret(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;

   if (cp0->reg[MIPS_CP0_STATUS] & MIPS_CP0_STATUS_ERL) {
      cp0->reg[MIPS_CP0_STATUS] &= ~MIPS_CP0_STATUS_ERL;
      cpu->pc = cp0->reg[MIPS_CP0_ERR_EPC];
   } else {
      cp0->reg[MIPS_CP0_STATUS] &= ~MIPS_CP0_STATUS_EXL;
      cpu->pc = cp0->reg[MIPS_CP0_EPC];
   }

   /* We have to clear the LLbit */
   cpu->ll_bit = 0;      

   /* Update the pending IRQ flag */
   mips64_update_irq_flag_fast(cpu);
}

/* Execute SYSCALL instruction */
fastcall void mips64_exec_syscall(cpu_mips_t *cpu)
{
#if DEBUG_SYSCALL
   printf("MIPS64: SYSCALL at PC=0x%llx (RA=0x%llx)\n"
          "   a0=0x%llx, a1=0x%llx, a2=0x%llx, a3=0x%llx\n",
          cpu->pc, cpu->gpr[MIPS_GPR_RA],
          cpu->gpr[MIPS_GPR_A0], cpu->gpr[MIPS_GPR_A1], 
          cpu->gpr[MIPS_GPR_A2], cpu->gpr[MIPS_GPR_A3]);
#endif

   /* XXX TODO: Branch Delay slot */
   mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_SYSCALL,0);
}

/* Execute BREAK instruction */
fastcall void mips64_exec_break(cpu_mips_t *cpu,u_int code)
{
   printf("MIPS64: BREAK instruction (code=%u)\n",code);

   /* XXX TODO: Branch Delay slot */
   mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_BP,0);
}

/* Trigger a Trap Exception */
asmlinkage void mips64_trigger_trap_exception(cpu_mips_t *cpu)
{  
   /* XXX TODO: Branch Delay slot */
   mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_TRAP,0);
}

/* Trigger IRQs */
fastcall void mips64_trigger_irq(cpu_mips_t *cpu)
{
   mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_INTERRUPT,0);
}

/* Set an IRQ */
void mips64_set_irq(cpu_mips_t *cpu,u_int irq)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint32_t m;

   m = (1 << (irq + MIPS_CP0_CAUSE_ISHIFT)) & MIPS_CP0_CAUSE_IMASK;
   cp0->reg[MIPS_CP0_CAUSE] |= m;
   mips64_update_irq_flag(cpu);
}

/* Clear an IRQ */
void mips64_clear_irq(cpu_mips_t *cpu,u_int irq)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint32_t m;

   m = (1 << (irq + MIPS_CP0_CAUSE_ISHIFT)) & MIPS_CP0_CAUSE_IMASK;
   cp0->reg[MIPS_CP0_CAUSE] &= ~m;
   mips64_update_irq_flag(cpu);
}

/* DMFC1 */
fastcall void mips64_exec_dmfc1(cpu_mips_t *cpu,u_int gp_reg,u_int cp1_reg)
{
   cpu->gpr[gp_reg] = cpu->fpu.reg[cp1_reg];
}

/* DMTC1 */
fastcall void mips64_exec_dmtc1(cpu_mips_t *cpu,u_int gp_reg,u_int cp1_reg)
{
   cpu->fpu.reg[cp1_reg] = cpu->gpr[gp_reg];
}

/* MFC1 */
fastcall void mips64_exec_mfc1(cpu_mips_t *cpu,u_int gp_reg,u_int cp1_reg)
{
   m_int64_t val;

   val = cpu->fpu.reg[cp1_reg] & 0xffffffff;
   cpu->gpr[gp_reg] = sign_extend(val,32);
}

/* MTC1 */
fastcall void mips64_exec_mtc1(cpu_mips_t *cpu,u_int gp_reg,u_int cp1_reg)
{
   cpu->fpu.reg[cp1_reg] = cpu->gpr[gp_reg] & 0xffffffff;
}

/* Virtual breakpoint */
fastcall void mips64_run_breakpoint(cpu_mips_t *cpu)
{
   printf("Virtual Breakpoint reached at PC=0x%llx\n",cpu->pc);
   mips64_dump_regs(cpu);
   memlog_dump(cpu);
}

/* Dump registers of a MIPS64 processor */
void mips64_dump_regs(cpu_mips_t *cpu)
{
   int i;

   printf("MIPS64 Registers:\n");

   for(i=0;i<MIPS64_GPR_NR/2;i++) {
      printf("  %s ($%2d) = 0x%16.16llx   %s ($%2d) = 0x%16.16llx\n",
             mips64_gpr_reg_names[i*2], i*2, cpu->gpr[i*2],
             mips64_gpr_reg_names[(i*2)+1], (i*2)+1, cpu->gpr[(i*2)+1]);
   }

   printf("  lo = 0x%16.16llx, hi = 0x%16.16llx\n", cpu->lo, cpu->hi);
   printf("  pc = 0x%16.16llx\n\n", cpu->pc);

   printf("  cp0 conf_reg = 0x%llx\n", cpu->cp0.reg[MIPS_CP0_CONFIG]);
   printf("  cp0 prid_reg = 0x%llx\n", cpu->cp0.reg[MIPS_CP0_PRID]);
   printf("  cp0 cnt_reg = 0x%llx\n", cpu->cp0.reg[MIPS_CP0_COUNT]);
   printf("  cp0 cmp_reg = 0x%llx\n", cpu->cp0.reg[MIPS_CP0_COMPARE]);

   printf("\n");
}

/* Dump a memory block */
void mips64_dump_memory(cpu_mips_t *cpu,m_uint64_t vaddr,u_int count)
{
   void *haddr;
   u_int i;

   for(i=0;i<count;i++,vaddr+=4) 
   {
      if ((i & 3) == 0)
         printf("\n  0x%16.16llx: ",vaddr);

      haddr = cpu->mem_op_lookup(cpu,vaddr);
      
      if (haddr)
         printf("0x%8.8x ",htovm32(*(m_uint32_t *)haddr));
      else
         printf("XXXXXXXXXX ");
   }

   printf("\n\n");
}

/* Dump the stack */
void mips64_dump_stack(cpu_mips_t *cpu,u_int count)
{   
   printf("MIPS Stack Dump at 0x%16.16llx:",cpu->gpr[MIPS_GPR_SP]);
   mips64_dump_memory(cpu,cpu->gpr[MIPS_GPR_SP],count);
}

/* Save the CPU state into a file */
int mips64_save_state(cpu_mips_t *cpu,char *filename)
{
   FILE *fd;
   int i;

   if (!(fd = fopen(filename,"w"))) {
      perror("mips64_save_state: fopen");
      return(-1);
   }

   /* pc, lo and hi */
   fprintf(fd,"pc: %16.16llx\n",cpu->pc);
   fprintf(fd,"lo: %16.16llx\n",cpu->lo);
   fprintf(fd,"hi: %16.16llx\n",cpu->hi);

   /* general purpose registers */
   for(i=0;i<MIPS64_GPR_NR;i++)
      fprintf(fd,"%s: %16.16llx\n",
              mips64_gpr_reg_names[i],cpu->gpr[i]);

   printf("\n");

   /* cp0 registers */
   for(i=0;i<MIPS64_CP0_REG_NR;i++)
      fprintf(fd,"%s: %16.16llx\n",
              mips64_cp0_reg_names[i],cpu->cp0.reg[i]);

   printf("\n");

   /* cp1 registers */
   for(i=0;i<MIPS64_CP1_REG_NR;i++)
      fprintf(fd,"fpu%d: %16.16llx\n",i,cpu->fpu.reg[i]);

   printf("\n");

   /* tlb entries */
   for(i=0;i<MIPS64_TLB_ENTRIES;i++) {
      fprintf(fd,"tlb%d_mask: %16.16llx\n",i,cpu->cp0.tlb[i].mask);
      fprintf(fd,"tlb%d_hi: %16.16llx\n",i,cpu->cp0.tlb[i].hi);
      fprintf(fd,"tlb%d_lo0: %16.16llx\n",i,cpu->cp0.tlb[i].lo0);
      fprintf(fd,"tlb%d_lo1: %16.16llx\n",i,cpu->cp0.tlb[i].lo1);
   }

   fclose(fd);
   return(0);
}

/* Read a 64-bit unsigned integer */
static m_uint64_t mips64_hex_u64(char *str,int *err)
{
   m_uint64_t res = 0;
   u_char c;

   /* remove leading spaces */
   while((*str == ' ') || (*str == '\t'))
      str++;

   while(*str) {
      c = *str;

      if ((c >= '0') && (c <= '9'))
         res = (res << 4) + (c - '0');

      if ((c >= 'a') && (c <= 'f'))
         res = (res << 4) + ((c - 'a') + 10);

      if ((c >= 'A') && (c <= 'F'))
         res = (res << 4) + ((c - 'A') + 10);

      str++;
   }

   return(res);
}

/* Restore the CPU state from a file */
int mips64_restore_state(cpu_mips_t *cpu,char *filename)
{
   char buffer[4096],*sep,*value,*ep,*field;
   size_t len;
   FILE *fd;
   int index;

   if (!(fd = fopen(filename,"r"))) {
      perror("mips64_restore_state: fopen");
      return(-1);
   }

   while(!feof(fd))
   {
      *buffer = 0;
      fgets(buffer,sizeof(buffer),fd);
      len = strlen(buffer);

      if (buffer[len-1] == '\n')
         buffer[len-1] = 0;

      sep = strchr(buffer,':');
      if (!sep) continue;

      value = sep + 1;
      *sep = 0;

      /* gpr ? */
      if ((index = mips64_get_reg_index(buffer)) != -1) {
         cpu->gpr[index] = mips64_hex_u64(value,NULL);
         continue;
      }

      /* cp0 register ? */
      if ((index = cp0_get_reg_index(buffer)) != -1) {
         cpu->cp0.reg[index] = mips64_hex_u64(value,NULL);
         continue;
      }

      /* cp1 register ? */
      if ((len > 3) && (!strncmp(buffer,"fpu",3))) {
         index = atoi(buffer+3);
         cpu->fpu.reg[index] = mips64_hex_u64(value,NULL);        
      }

      /* tlb entry ? */
      if ((len > 3) && (!strncmp(buffer,"tlb",3))) {
         ep = strchr(buffer,'_');

         if (ep) {
            index = atoi(buffer+3);
            field = ep + 1;
            
            if (!strcmp(field,"mask")) {
               cpu->cp0.tlb[index].mask = mips64_hex_u64(value,NULL);
               continue;
            }

            if (!strcmp(field,"hi")) {
               cpu->cp0.tlb[index].hi = mips64_hex_u64(value,NULL);
               continue;
            }

            if (!strcmp(field,"lo0")) {
               cpu->cp0.tlb[index].lo0 = mips64_hex_u64(value,NULL);
               continue;
            }

            if (!strcmp(field,"lo1")) {
               cpu->cp0.tlb[index].lo1 = mips64_hex_u64(value,NULL);
               continue;
            }
         }
      }
      
      /* pc, lo, hi ? */
      if (!strcmp(buffer,"pc")) {
         cpu->pc = mips64_hex_u64(value,NULL);
         continue;
      }

      if (!strcmp(buffer,"lo")) {
         cpu->lo = mips64_hex_u64(value,NULL);
         continue;
      }

      if (!strcmp(buffer,"hi")) {
         cpu->hi = mips64_hex_u64(value,NULL);
         continue;
      }
   }

   cp0_map_all_tlb_to_mts(cpu);

   mips64_dump_regs(cpu);
   tlb_dump(cpu);

   fclose(fd);
   return(0);
}
