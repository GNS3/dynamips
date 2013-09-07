/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * PowerPC (32-bit) step-by-step execution.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>

#include "cpu.h"
#include "vm.h"
#include "ppc32_exec.h"
#include "ppc32_mem.h"
#include "memory.h"
#include "insn_lookup.h"
#include "dynamips.h"

/* Forward declaration of instruction array */
static struct ppc32_insn_exec_tag ppc32_exec_tags[];
static insn_lookup_t *ilt = NULL;

/* ILT */
static forced_inline void *ppc32_exec_get_insn(int index)
{
   return(&ppc32_exec_tags[index]);
}

static int ppc32_exec_chk_lo(struct ppc32_insn_exec_tag *tag,int value)
{
   return((value & tag->mask) == (tag->value & 0xFFFF));
}

static int ppc32_exec_chk_hi(struct ppc32_insn_exec_tag *tag,int value)
{      
   return((value & (tag->mask >> 16)) == (tag->value >> 16));
}

/* Destroy instruction lookup table */
static void destroy_ilt(void)
{
   assert(ilt);
   ilt_destroy(ilt);
   ilt = NULL;
}

/* Initialize instruction lookup table */
void ppc32_exec_create_ilt(void)
{
   int i,count;

   for(i=0,count=0;ppc32_exec_tags[i].exec;i++)
      count++;

   ilt = ilt_create("ppc32e",count,
                    (ilt_get_insn_cbk_t)ppc32_exec_get_insn,
                    (ilt_check_cbk_t)ppc32_exec_chk_lo,
                    (ilt_check_cbk_t)ppc32_exec_chk_hi);

   atexit(destroy_ilt);
}

/* Dump statistics */
void ppc32_dump_stats(cpu_ppc_t *cpu)
{
   int i;

#if NJM_STATS_ENABLE
   printf("\n");

   for(i=0;ppc32_exec_tags[i].exec;i++)
      printf("  * %-10s : %10llu\n",
             ppc32_exec_tags[i].name,ppc32_exec_tags[i].count);

   printf("%llu instructions executed since startup.\n",cpu->insn_exec_count);
#else
   printf("Statistics support is not compiled in.\n");
#endif
}

/* Execute a memory operation */
static forced_inline void ppc32_exec_memop(cpu_ppc_t *cpu,int memop,
                                           m_uint32_t vaddr,u_int dst_reg)
{     
   fastcall ppc_memop_fn fn;
    
   fn = cpu->mem_op_fn[memop];
   fn(cpu,vaddr,dst_reg);
}

/* Fetch an instruction */
static forced_inline int ppc32_exec_fetch(cpu_ppc_t *cpu,m_uint32_t ia,
                                          ppc_insn_t *insn)
{
   m_uint32_t exec_page,offset;

   exec_page = ia & ~PPC32_MIN_PAGE_IMASK;

   if (unlikely(exec_page != cpu->njm_exec_page)) {
      cpu->njm_exec_ptr  = cpu->mem_op_ifetch(cpu,exec_page);
      cpu->njm_exec_page = exec_page;
   }

   offset = (ia & PPC32_MIN_PAGE_IMASK) >> 2;
   *insn = vmtoh32(cpu->njm_exec_ptr[offset]);
   return(0);
}

/* Unknown opcode */
static fastcall int ppc32_exec_unknown(cpu_ppc_t *cpu,ppc_insn_t insn)
{   
   printf("PPC32: unknown opcode 0x%8.8x at ia = 0x%x\n",insn,cpu->ia);
   ppc32_dump_regs(cpu->gen);
   return(0);
}

/* Execute a single instruction */
static forced_inline int 
ppc32_exec_single_instruction(cpu_ppc_t *cpu,ppc_insn_t instruction)
{
   register fastcall int (*exec)(cpu_ppc_t *,ppc_insn_t) = NULL;
   struct ppc32_insn_exec_tag *tag;
   int index;
   
#if DEBUG_INSN_PERF_CNT
   cpu->perf_counter++;
#endif
   
   /* Lookup for instruction */
   index = ilt_lookup(ilt,instruction);
   tag = ppc32_exec_get_insn(index);
   exec = tag->exec;

#if NJM_STATS_ENABLE
   cpu->insn_exec_count++;
   ppc32_exec_tags[index].count++;
#endif
   return(exec(cpu,instruction));
}

/* Execute a single instruction (external) */
fastcall int ppc32_exec_single_insn_ext(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int res;

   res = ppc32_exec_single_instruction(cpu,insn);
   if (likely(!res)) cpu->ia += sizeof(ppc_insn_t);
   return(res);
}

/* Execute a page */
fastcall int ppc32_exec_page(cpu_ppc_t *cpu)
{
   m_uint32_t exec_page,offset;
   ppc_insn_t insn;
   int res;

   exec_page = cpu->ia & PPC32_MIN_PAGE_MASK;
   cpu->njm_exec_page = exec_page;
   cpu->njm_exec_ptr  = cpu->mem_op_lookup(cpu,exec_page,PPC32_MTS_ICACHE);

   do {
      offset = (cpu->ia & PPC32_MIN_PAGE_IMASK) >> 2;
      insn = vmtoh32(cpu->njm_exec_ptr[offset]);

      res = ppc32_exec_single_instruction(cpu,insn);
      if (likely(!res)) cpu->ia += sizeof(ppc_insn_t);
   }while((cpu->ia & PPC32_MIN_PAGE_MASK) == exec_page);

   return(0);
}

/* Run PowerPC code in step-by-step mode */
void *ppc32_exec_run_cpu(cpu_gen_t *gen)
{   
   cpu_ppc_t *cpu = CPU_PPC32(gen);
   pthread_t timer_irq_thread;
   int timer_irq_check = 0;
   ppc_insn_t insn;
   int res;

   if (pthread_create(&timer_irq_thread,NULL,
                      (void *)ppc32_timer_irq_run,cpu))
   {
      fprintf(stderr,"VM '%s': unable to create Timer IRQ thread for CPU%u.\n",
              cpu->vm->name,gen->id);
      cpu_stop(gen);
      return NULL;
   }

   gen->cpu_thread_running = TRUE;
   cpu_exec_loop_set(gen);

 start_cpu:
   for(;;) {
      if (unlikely(gen->state != CPU_STATE_RUNNING))
         break;

      /* Check IRQ */
      if (unlikely(cpu->irq_check))
         ppc32_trigger_irq(cpu);

      /* Handle virtual idle loop */
      if (unlikely(cpu->ia == cpu->idle_pc)) {
         if (++gen->idle_count == gen->idle_max) {
            cpu_idle_loop(gen);
            gen->idle_count = 0;
         }
      }

      /* Handle the virtual CPU clock */
      if (++timer_irq_check == cpu->timer_irq_check_itv) {
         timer_irq_check = 0;

         if (cpu->timer_irq_pending && !cpu->irq_disable && 
             (cpu->msr & PPC32_MSR_EE)) 
         {        
            cpu->timer_irq_armed = 0;
            cpu->timer_irq_pending--;

            vm_set_irq(cpu->vm,0);
            //ppc32_trigger_timer_irq(cpu);
         }
      }

      /* Increment the time base */
      cpu->tb += 100;

      /* Fetch and execute the instruction */
      ppc32_exec_fetch(cpu,cpu->ia,&insn);
      res = ppc32_exec_single_instruction(cpu,insn);

      /* Normal flow ? */
      if (likely(!res)) cpu->ia += sizeof(ppc_insn_t);
   }

   /* Check regularly if the CPU has been restarted */
   while(gen->cpu_thread_running) {
      gen->seq_state++;

      switch(gen->state) {
         case CPU_STATE_RUNNING:
            gen->state = CPU_STATE_RUNNING;
            goto start_cpu;

         case CPU_STATE_HALTED:     
            gen->cpu_thread_running = FALSE;
            pthread_join(timer_irq_thread,NULL);
            break;
      }
      
      /* CPU is paused */
      usleep(200000);
   }

   return NULL;
}

/* ========================================================================= */

/* Update CR0 */
static forced_inline void ppc32_exec_update_cr0(cpu_ppc_t *cpu,m_uint32_t val)
{
   m_uint32_t res;

   if (val & 0x80000000)
      res = 1 << PPC32_CR_LT_BIT;
   else {
      if (val > 0)
         res = 1 << PPC32_CR_GT_BIT;
      else
         res = 1 << PPC32_CR_EQ_BIT;
   }

   if (cpu->xer & PPC32_XER_SO)
      res |= 1 << PPC32_CR_SO_BIT;

   cpu->cr_fields[0] = res;
}

/* 
 * Update Overflow bit from a sum result (r = a + b)
 *
 * (a > 0) && (b > 0) => r > 0, otherwise overflow
 * (a < 0) && (a < 0) => r < 0, otherwise overflow.
 */
static forced_inline void ppc32_exec_ov_sum(cpu_ppc_t *cpu,m_uint32_t r,
                                            m_uint32_t a,m_uint32_t b)
{
   register m_uint32_t sc;

   sc = (~(a ^ b) & (a ^ r) & 0x80000000);
   if (unlikely(sc))
      cpu->xer |= PPC32_XER_SO | PPC32_XER_OV;
   else
      cpu->xer &= ~PPC32_XER_OV;
}

/* 
 * Update Overflow bit from a substraction result (r = a - b)
 *
 * (a > 0) && (b < 0) => r > 0, otherwise overflow
 * (a < 0) && (a > 0) => r < 0, otherwise overflow.
 */
static forced_inline void ppc32_exec_ov_sub(cpu_ppc_t *cpu,m_uint32_t r,
                                            m_uint32_t a,m_uint32_t b)
{
   register m_uint32_t sc;

   sc = ((a ^ b) & (a ^ r) & 0x80000000);
   if (unlikely(sc))
      cpu->xer |= PPC32_XER_SO | PPC32_XER_OV;
   else
      cpu->xer &= ~PPC32_XER_OV;
}

/* 
 * Update CA bit from a sum result (r = a + b)
 */
static forced_inline void ppc32_exec_ca_sum(cpu_ppc_t *cpu,m_uint32_t r,
                                            m_uint32_t a,m_uint32_t b)
{
   cpu->xer_ca = (r < a) ? 1 : 0;
}

/* 
 * Update CA bit from a substraction result (r = a - b)
 */
static forced_inline void ppc32_exec_ca_sub(cpu_ppc_t *cpu,m_uint32_t r,
                                            m_uint32_t a,m_uint32_t b)
{
   cpu->xer_ca = (b > a) ? 1 : 0;
}

/* Check condition code */
static forced_inline int ppc32_check_cond(cpu_ppc_t *cpu,m_uint32_t bo,
                                          m_uint32_t bi)
{
   u_int ctr_ok = TRUE;
   u_int cond_ok;
   u_int cr_bit;

   if (!(bo & 0x04)) {
      cpu->ctr--;
      ctr_ok = (cpu->ctr != 0) ^ ((bo >> 1) & 0x1);
   }

   cr_bit = ppc32_read_cr_bit(cpu,bi);
   cond_ok = (bo >> 4) | ((cr_bit ^ (~bo >> 3)) & 0x1);

   return(ctr_ok & cond_ok);
}

/* MFLR - Move From Link Register */
static fastcall int ppc32_exec_MFLR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);

   cpu->gpr[rd] = cpu->lr;
   return(0);
}

/* MTLR - Move To Link Register */
static fastcall int ppc32_exec_MTLR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);

   cpu->lr = cpu->gpr[rs];
   return(0);
}

/* MFCTR - Move From Counter Register */
static fastcall int ppc32_exec_MFCTR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);

   cpu->gpr[rd] = cpu->ctr;
   return(0);
}

/* MTCTR - Move To Counter Register */
static fastcall int ppc32_exec_MTCTR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);

   cpu->ctr = cpu->gpr[rs];
   return(0);
}

/* ADD */
static fastcall int ppc32_exec_ADD(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   cpu->gpr[rd] = cpu->gpr[ra] + cpu->gpr[rb];
   return(0);
}

/* ADD. */
static fastcall int ppc32_exec_ADD_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t tmp;

   tmp = cpu->gpr[ra] + cpu->gpr[rb];
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[rd] = tmp;
   return(0);
}

/* ADDO - Add with Overflow */
static fastcall int ppc32_exec_ADDO(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,d;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   d = a + b;

   ppc32_exec_ov_sum(cpu,d,a,b);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDO. */
static fastcall int ppc32_exec_ADDO_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,d;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   d = a + b;

   ppc32_exec_ov_sum(cpu,d,a,b);  
   ppc32_exec_update_cr0(cpu,d);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDC - Add Carrying */
static fastcall int ppc32_exec_ADDC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,d;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   d = a + b;

   ppc32_exec_ca_sum(cpu,d,a,b);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDC. */
static fastcall int ppc32_exec_ADDC_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,d;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   d = a + b;

   ppc32_exec_ca_sum(cpu,d,a,b);
   ppc32_exec_update_cr0(cpu,d);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDCO - Add Carrying with Overflow */
static fastcall int ppc32_exec_ADDCO(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,d;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   d = a + b;

   ppc32_exec_ca_sum(cpu,d,a,b);
   ppc32_exec_ov_sum(cpu,d,a,b);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDCO. */
static fastcall int ppc32_exec_ADDCO_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,d;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   d = a + b;

   ppc32_exec_ca_sum(cpu,d,a,b);
   ppc32_exec_ov_sum(cpu,d,a,b);
   ppc32_exec_update_cr0(cpu,d);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDE - Add Extended */
static fastcall int ppc32_exec_ADDE(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,d;
   m_uint32_t carry;

   carry = cpu->xer_ca;
   cpu->xer_ca = 0;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   d = a + b + carry;

   if (((b + carry) < b) || (d < a))
      cpu->xer_ca = 1;

   cpu->gpr[rd] = d;
   return(0);
}

/* ADDE. */
static fastcall int ppc32_exec_ADDE_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,d;
   m_uint32_t carry;

   carry = cpu->xer_ca;
   cpu->xer_ca = 0;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   d = a + b + carry;

   if (((b + carry) < b) || (d < a))
      cpu->xer_ca = 1;

   ppc32_exec_update_cr0(cpu,d);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDEO - Add Extended with Overflow */
static fastcall int ppc32_exec_ADDEO(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,d;
   m_uint32_t carry;

   carry = cpu->xer_ca;
   cpu->xer_ca = 0;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   d = a + b + carry;

   if (((b + carry) < b) || (d < a))
      cpu->xer_ca = 1;

   ppc32_exec_ov_sum(cpu,d,a,b);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDEO. */
static fastcall int ppc32_exec_ADDEO_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,d;
   m_uint32_t carry;

   carry = cpu->xer_ca;
   cpu->xer_ca = 0;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   d = a + b + carry;

   if (((b + carry) < b) || (d < a))
      cpu->xer_ca = 1;

   ppc32_exec_ov_sum(cpu,d,a,b);
   ppc32_exec_update_cr0(cpu,d);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDI - ADD Immediate */
static fastcall int ppc32_exec_ADDI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int imm = bits(insn,0,15);
   register m_uint32_t tmp;

   tmp = sign_extend_32(imm,16);

   if (ra != 0)
      tmp += cpu->gpr[ra];
      
   cpu->gpr[rd] = tmp;
   return(0);
}

/* ADDIC - ADD Immediate with Carry */
static fastcall int ppc32_exec_ADDIC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int imm = bits(insn,0,15);
   register m_uint32_t a,d;

   a = cpu->gpr[ra];
   d = a + sign_extend_32(imm,16);
   ppc32_exec_ca_sum(cpu,d,a,0);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDIC. */
static fastcall int ppc32_exec_ADDIC_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int imm = bits(insn,0,15);
   register m_uint32_t a,d;

   a = cpu->gpr[ra];
   d = a + sign_extend_32(imm,16);
   ppc32_exec_ca_sum(cpu,d,a,0);
   ppc32_exec_update_cr0(cpu,d);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDIS - ADD Immediate Shifted */
static fastcall int ppc32_exec_ADDIS(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);
   register m_uint32_t tmp;

   tmp = imm << 16;

   if (ra != 0)
      tmp += cpu->gpr[ra];
      
   cpu->gpr[rd] = tmp;
   return(0);
}

/* ADDME - Add to Minus One Extended */
static fastcall int ppc32_exec_ADDME(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   register m_uint32_t a,b,d;
   m_uint32_t carry;

   carry = cpu->xer_ca;
   cpu->xer_ca = 0;

   a = cpu->gpr[ra];
   b = 0xFFFFFFFF;
   d = a + b + carry;

   if (((b + carry) < b) || (d < a))
      cpu->xer_ca = 1;

   cpu->gpr[rd] = d;
   return(0);
}

/* ADDME. */
static fastcall int ppc32_exec_ADDME_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   register m_uint32_t a,b,d;
   m_uint32_t carry;

   carry = cpu->xer_ca;
   cpu->xer_ca = 0;

   a = cpu->gpr[ra];
   b = 0xFFFFFFFF;
   d = a + b + carry;

   if (((b + carry) < b) || (d < a))
      cpu->xer_ca = 1;

   ppc32_exec_update_cr0(cpu,d);
   cpu->gpr[rd] = d;
   return(0);
}

/* ADDZE - Add to Zero Extended */
static fastcall int ppc32_exec_ADDZE(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   register m_uint32_t a,d;
   m_uint32_t carry;

   carry = cpu->xer_ca;
   cpu->xer_ca = 0;

   a = cpu->gpr[ra];
   d = a + carry;

   if (d < a)
      cpu->xer_ca = 1;

   cpu->gpr[rd] = d;
   return(0);
}

/* ADDZE. */
static fastcall int ppc32_exec_ADDZE_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   register m_uint32_t a,d;
   m_uint32_t carry;

   carry = cpu->xer_ca;
   cpu->xer_ca = 0;

   a = cpu->gpr[ra];
   d = a + carry;

   if (d < a)
      cpu->xer_ca = 1;

   ppc32_exec_update_cr0(cpu,d);
   cpu->gpr[rd] = d;
   return(0);
}

/* AND */
static fastcall int ppc32_exec_AND(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   cpu->gpr[ra] = cpu->gpr[rs] & cpu->gpr[rb];
   return(0);
}

/* AND. */
static fastcall int ppc32_exec_AND_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t tmp;

   tmp = cpu->gpr[rs] & cpu->gpr[rb];
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* ANDC - AND with Complement */
static fastcall int ppc32_exec_ANDC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   cpu->gpr[ra] = cpu->gpr[rs] & (~cpu->gpr[rb]);
   return(0);
}

/* ANDC. - AND with Complement */
static fastcall int ppc32_exec_ANDC_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t tmp;

   tmp = cpu->gpr[rs] & (~cpu->gpr[rb]);
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* ANDI. - AND Immediate */
static fastcall int ppc32_exec_ANDI_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   register m_uint32_t tmp;

   tmp = cpu->gpr[rs] & imm;
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* ANDIS. - AND Immediate Shifted */
static fastcall int ppc32_exec_ANDIS_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs  = bits(insn,21,25);
   int ra  = bits(insn,16,20);
   m_uint32_t imm  = bits(insn,0,15);
   register m_uint32_t tmp;

   tmp = cpu->gpr[rs] & (imm << 16);
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* B - Branch */
static fastcall int ppc32_exec_B(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   m_uint32_t offset = bits(insn,2,25);

   cpu->ia += sign_extend_32(offset << 2,26);
   return(1);
}

/* BA - Branch Absolute */
static fastcall int ppc32_exec_BA(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   m_uint32_t offset = bits(insn,2,25);

   cpu->ia = sign_extend_32(offset << 2,26);
   return(1);
}

/* BL - Branch and Link */
static fastcall int ppc32_exec_BL(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   m_uint32_t offset = bits(insn,2,25);

   cpu->lr = cpu->ia + 4;
   cpu->ia += sign_extend_32(offset << 2,26);
   return(1);
}

/* BLA - Branch and Link Absolute */
static fastcall int ppc32_exec_BLA(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   m_uint32_t offset = bits(insn,2,25);

   cpu->lr = cpu->ia + 4;
   cpu->ia = sign_extend_32(offset << 2,26);
   return(1);
}

/* BC - Branch Conditional */
static fastcall int ppc32_exec_BC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   int bd = bits(insn,2,15);

   if (ppc32_check_cond(cpu,bo,bi)) {
      cpu->ia += sign_extend_32(bd << 2,16);
      return(1);
   }

   return(0);
}

/* BCA - Branch Conditional (absolute) */
static fastcall int ppc32_exec_BCA(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   int bd = bits(insn,2,15);

   if (ppc32_check_cond(cpu,bo,bi)) {
      cpu->ia = sign_extend_32(bd << 2,16);
      return(1);
   }

   return(0);
}

/* BCL - Branch Conditional and Link */
static fastcall int ppc32_exec_BCL(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   int bd = bits(insn,2,15);

   cpu->lr = cpu->ia + 4;

   if (ppc32_check_cond(cpu,bo,bi)) {
      cpu->ia += sign_extend_32(bd << 2,16);
      return(1);
   }

   return(0);
}

/* BCLA - Branch Conditional and Link (absolute) */
static fastcall int ppc32_exec_BCLA(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   int bd = bits(insn,2,15);

   cpu->lr = cpu->ia + 4;

   if (ppc32_check_cond(cpu,bo,bi)) {
      cpu->ia = sign_extend_32(bd << 2,16);
      return(1);
   }

   return(0);
}

/* BCLR - Branch Conditional to Link register */
static fastcall int ppc32_exec_BCLR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);

   if (ppc32_check_cond(cpu,bo,bi)) {
      cpu->ia = cpu->lr & ~0x3;
      return(1);
   }

   return(0);
}

/* BCLRL - Branch Conditional to Link register */
static fastcall int ppc32_exec_BCLRL(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   m_uint32_t new_ia;

   new_ia  = cpu->lr & ~0x03;
   cpu->lr = cpu->ia + 4;

   if (ppc32_check_cond(cpu,bo,bi)) {
      cpu->ia = new_ia;
      return(1);
   }

   return(0);
}

/* BCCTR - Branch Conditional to Count register */
static fastcall int ppc32_exec_BCCTR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);

   if (ppc32_check_cond(cpu,bo,bi)) {
      cpu->ia = cpu->ctr & ~0x3;
      return(1);
   }

   return(0);
}

/* BCCTRL - Branch Conditional to Count register and Link */
static fastcall int ppc32_exec_BCCTRL(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);

   cpu->lr = cpu->ia + 4;

   if (ppc32_check_cond(cpu,bo,bi)) {
      cpu->ia = cpu->ctr & ~0x3;
      return(1);
   }

   return(0);
}

/* CMP - Compare */
static fastcall int ppc32_exec_CMP(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t res;
   m_int32_t a,b;

   a = (m_int32_t)cpu->gpr[ra];
   b = (m_int32_t)cpu->gpr[rb];

   if (a < b)
      res = 0x08;
   else {
      if (a > b)
         res = 0x04;
      else
         res = 0x02;
   }

   if (cpu->xer & PPC32_XER_SO)
      res |= 0x01;

   cpu->cr_fields[rd] = res;
   return(0);
}

/* CMPI - Compare Immediate */
static fastcall int ppc32_exec_CMPI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t res;
   m_int32_t a,b;

   a = (m_int32_t)cpu->gpr[ra];
   b = sign_extend_32(imm,16);

   if (a < b)
      res = 0x08;
   else {
      if (a > b)
         res = 0x04;
      else
         res = 0x02;
   }

   if (cpu->xer & PPC32_XER_SO)
      res |= 0x01;
   
   cpu->cr_fields[rd] = res;
   return(0);
}

/* CMPL - Compare Logical */
static fastcall int ppc32_exec_CMPL(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t res,a,b;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];

   if (a < b)
      res = 0x08;
   else {
      if (a > b)
         res = 0x04;
      else
         res = 0x02;
   }

   if (cpu->xer & PPC32_XER_SO)
      res |= 0x01;

   cpu->cr_fields[rd] = res;
   return(0);
}

/* CMPLI - Compare Logical Immediate */
static fastcall int ppc32_exec_CMPLI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);
   m_uint32_t res,a;

   a = cpu->gpr[ra];

   if (a < imm)
      res = 0x08;
   else {
      if (a > imm)
         res = 0x04;
      else
         res = 0x02;
   }

   if (cpu->xer & PPC32_XER_SO)
      res |= 0x01;
   
   cpu->cr_fields[rd] = res;
   return(0);
}

/* CNTLZW - Count Leading Zeros Word */
static fastcall int ppc32_exec_CNTLZW(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t val,mask;
   int i;

   val  = cpu->gpr[rs];
   mask = 0x80000000;

   for(i=0;i<32;i++) {
      if (val & mask)
         break;

      mask >>= 1;
   }

   cpu->gpr[ra] = i;
   return(0);
}

/* CRAND - Condition Register AND */
static fastcall int ppc32_exec_CRAND(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);
   m_uint32_t tmp;

   tmp =  ppc32_read_cr_bit(cpu,ba);
   tmp &= ppc32_read_cr_bit(cpu,bb);

   if (tmp & 0x1)
      ppc32_set_cr_bit(cpu,bd);
   else
      ppc32_clear_cr_bit(cpu,bd);

   return(0);
}

/* CREQV - Condition Register Equivalent */
static fastcall int ppc32_exec_CREQV(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);
   m_uint32_t tmp;

   tmp =  ppc32_read_cr_bit(cpu,ba);
   tmp ^= ppc32_read_cr_bit(cpu,bb);

   if (!(tmp & 0x1))
      ppc32_set_cr_bit(cpu,bd);
   else
      ppc32_clear_cr_bit(cpu,bd);

   return(0);
}

/* CRANDC - Condition Register AND with Complement */
static fastcall int ppc32_exec_CRANDC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);
   m_uint32_t tmp;

   tmp =  ppc32_read_cr_bit(cpu,ba);
   tmp &= ~ppc32_read_cr_bit(cpu,bb);

   if (tmp & 0x1)
      ppc32_set_cr_bit(cpu,bd);
   else
      ppc32_clear_cr_bit(cpu,bd);

   return(0);
}

/* CRNAND - Condition Register NAND */
static fastcall int ppc32_exec_CRNAND(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);
   m_uint32_t tmp;

   tmp =  ppc32_read_cr_bit(cpu,ba);
   tmp &= ppc32_read_cr_bit(cpu,bb);

   if (!(tmp & 0x1))
      ppc32_set_cr_bit(cpu,bd);
   else
      ppc32_clear_cr_bit(cpu,bd);

   return(0);
}

/* CRNOR - Condition Register NOR */
static fastcall int ppc32_exec_CRNOR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);
   m_uint32_t tmp;

   tmp =  ppc32_read_cr_bit(cpu,ba);
   tmp |= ppc32_read_cr_bit(cpu,bb);

   if (!(tmp & 0x1))
      ppc32_set_cr_bit(cpu,bd);
   else
      ppc32_clear_cr_bit(cpu,bd);

   return(0);
}

/* CROR - Condition Register OR */
static fastcall int ppc32_exec_CROR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);
   m_uint32_t tmp;

   tmp =  ppc32_read_cr_bit(cpu,ba);
   tmp |= ppc32_read_cr_bit(cpu,bb);

   if (tmp & 0x1)
      ppc32_set_cr_bit(cpu,bd);
   else
      ppc32_clear_cr_bit(cpu,bd);

   return(0);
}

/* CRORC - Condition Register OR with complement */
static fastcall int ppc32_exec_CRORC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);
   m_uint32_t tmp;

   tmp =  ppc32_read_cr_bit(cpu,ba);
   tmp |= ~ppc32_read_cr_bit(cpu,bb);

   if (tmp & 0x1)
      ppc32_set_cr_bit(cpu,bd);
   else
      ppc32_clear_cr_bit(cpu,bd);

   return(0);
}

/* CRXOR - Condition Register XOR */
static fastcall int ppc32_exec_CRXOR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);
   m_uint32_t tmp;

   tmp =  ppc32_read_cr_bit(cpu,ba);
   tmp ^= ppc32_read_cr_bit(cpu,bb);

   if (tmp & 0x1)
      ppc32_set_cr_bit(cpu,bd);
   else
      ppc32_clear_cr_bit(cpu,bd);

   return(0);
}

/* DCBF - Data Cache Block Flush */
static fastcall int ppc32_exec_DCBF(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   //printf("PPC32: DBCF: vaddr=0x%8.8x\n",vaddr);
   return(0);
}

/* DCBI - Data Cache Block Invalidate */
static fastcall int ppc32_exec_DCBI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   //printf("PPC32: DBCI: vaddr=0x%8.8x\n",vaddr);
   return(0);
}

/* DCBT - Data Cache Block Touch */
static fastcall int ppc32_exec_DCBT(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   //printf("PPC32: DBCT: vaddr=0x%8.8x\n",vaddr);
   return(0);
}

/* DCBST - Data Cache Block Store */
static fastcall int ppc32_exec_DCBST(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   //printf("PPC32: DBCST: vaddr=0x%8.8x\n",vaddr);
   return(0);
}

/* DIVW - Divide Word */
static fastcall int ppc32_exec_DIVW(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_int32_t a,b;

   a = (m_int32_t)cpu->gpr[ra];
   b = (m_int32_t)cpu->gpr[rb];

   if (!((b == 0) || ((cpu->gpr[ra] == 0x80000000) && (b == -1))))
      cpu->gpr[rd] = a / b;
   return(0);
}

/* DIVW. - Divide Word */
static fastcall int ppc32_exec_DIVW_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_int32_t a,b,d;

   a = (m_int32_t)cpu->gpr[ra];
   b = (m_int32_t)cpu->gpr[rb];
   d = 0;

   if (!((b == 0) || ((cpu->gpr[ra] == 0x80000000) && (b == -1))))
      d = a / b;

   ppc32_exec_update_cr0(cpu,d);
   cpu->gpr[rd] = d;
   return(0);
}

/* DIVWU - Divide Word Unsigned */
static fastcall int ppc32_exec_DIVWU(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];

   if (b != 0)
      cpu->gpr[rd] = a / b;
   return(0);
}

/* DIVWU. - Divide Word Unsigned */
static fastcall int ppc32_exec_DIVWU_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,d;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   d = 0;

   if (b != 0)
      d = a / b;

   ppc32_exec_update_cr0(cpu,d);
   cpu->gpr[rd] = d;
   return(0);
}

/* EIEIO - Enforce In-order Execution of I/O */
static fastcall int ppc32_exec_EIEIO(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   return(0);
}

/* EQV */
static fastcall int ppc32_exec_EQV(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   cpu->gpr[ra] = ~(cpu->gpr[rs] ^ cpu->gpr[rb]);
   return(0);
}

/* EXTSB - Extend Sign Byte */
static fastcall int ppc32_exec_EXTSB(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);

   cpu->gpr[ra] = sign_extend_32(cpu->gpr[rs],8);
   return(0);
}

/* EXTSB. */
static fastcall int ppc32_exec_EXTSB_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t tmp;

   tmp = sign_extend_32(cpu->gpr[rs],8);
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* EXTSH - Extend Sign Word */
static fastcall int ppc32_exec_EXTSH(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);

   cpu->gpr[ra] = sign_extend_32(cpu->gpr[rs],16);
   return(0);
}

/* EXTSH. */
static fastcall int ppc32_exec_EXTSH_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t tmp;

   tmp = sign_extend_32(cpu->gpr[rs],16);
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* ICBI - Instruction Cache Block Invalidate */
static fastcall int ppc32_exec_ICBI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_ICBI,vaddr,0);
   return(0);
}

/* ISYNC - Instruction Synchronize */
static fastcall int ppc32_exec_ISYNC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   return(0);
}

/* LBZ - Load Byte and Zero */
static fastcall int ppc32_exec_LBZ(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = sign_extend_32(imm,16);
   
   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_LBZ,vaddr,rd);
   return(0);
}

/* LBZU - Load Byte and Zero with Update */
static fastcall int ppc32_exec_LBZU(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + sign_extend_32(imm,16);
   ppc32_exec_memop(cpu,PPC_MEMOP_LBZ,vaddr,rd);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* LBZUX - Load Byte and Zero with Update Indexed */
static fastcall int ppc32_exec_LBZUX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + cpu->gpr[rb];
   ppc32_exec_memop(cpu,PPC_MEMOP_LBZ,vaddr,rd);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* LBZX - Load Byte and Zero Indexed */
static fastcall int ppc32_exec_LBZX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_LBZ,vaddr,rd);
   return(0);
}

/* LHA - Load Half-Word Algebraic */
static fastcall int ppc32_exec_LHA(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = sign_extend_32(imm,16);
   
   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_LHA,vaddr,rd);
   return(0);
}

/* LHAU - Load Half-Word Algebraic with Update */
static fastcall int ppc32_exec_LHAU(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + sign_extend_32(imm,16);
   ppc32_exec_memop(cpu,PPC_MEMOP_LHA,vaddr,rd);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* LHAUX - Load Half-Word Algebraic with Update Indexed */
static fastcall int ppc32_exec_LHAUX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + cpu->gpr[rb];
   ppc32_exec_memop(cpu,PPC_MEMOP_LHA,vaddr,rd);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* LHAX - Load Half-Word Algebraic ndexed */
static fastcall int ppc32_exec_LHAX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_LHA,vaddr,rd);
   return(0);
}

/* LHZ - Load Half-Word and Zero */
static fastcall int ppc32_exec_LHZ(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = sign_extend_32(imm,16);
   
   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_LHZ,vaddr,rd);
   return(0);
}

/* LHZU - Load Half-Word and Zero with Update */
static fastcall int ppc32_exec_LHZU(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + sign_extend_32(imm,16);
   ppc32_exec_memop(cpu,PPC_MEMOP_LHZ,vaddr,rd);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* LHZUX - Load Half-Word and Zero with Update Indexed */
static fastcall int ppc32_exec_LHZUX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + cpu->gpr[rb];
   ppc32_exec_memop(cpu,PPC_MEMOP_LHZ,vaddr,rd);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* LHZX - Load Half-Word and Zero Indexed */
static fastcall int ppc32_exec_LHZX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_LHZ,vaddr,rd);
   return(0);
}

/* LMW - Load Multiple Word */
static fastcall int ppc32_exec_LMW(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;
   int r;

   vaddr = sign_extend_32(imm,16);
   
   if (ra != 0)
      vaddr += cpu->gpr[ra];

   for(r=rd;r<=31;r++) {
      ppc32_exec_memop(cpu,PPC_MEMOP_LWZ,vaddr,r);
      vaddr += sizeof(m_uint32_t);
   }

   return(0);
}

/* LWBRX - Load Word Byte-Reverse Indexed */
static fastcall int ppc32_exec_LWBRX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_LWBR,vaddr,rd);
   return(0);
}

/* LWZ - Load Word and Zero */
static fastcall int ppc32_exec_LWZ(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = sign_extend_32(imm,16);
   
   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_LWZ,vaddr,rd);
   return(0);
}

/* LWZU - Load Word and Zero with Update */
static fastcall int ppc32_exec_LWZU(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + sign_extend_32(imm,16);
   ppc32_exec_memop(cpu,PPC_MEMOP_LWZ,vaddr,rd);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* LWZUX - Load Word and Zero with Update Indexed */
static fastcall int ppc32_exec_LWZUX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + cpu->gpr[rb];
   ppc32_exec_memop(cpu,PPC_MEMOP_LWZ,vaddr,rd);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* LWZX - Load Word and Zero Indexed */
static fastcall int ppc32_exec_LWZX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_LWZ,vaddr,rd);
   return(0);
}

/* LWARX - Load Word and Reserve Indexed */
static fastcall int ppc32_exec_LWARX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   cpu->reserve = 1;
   ppc32_exec_memop(cpu,PPC_MEMOP_LWZ,vaddr,rd);
   return(0);
}

/* LFD - Load Floating-Point Double */
static fastcall int ppc32_exec_LFD(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = sign_extend_32(imm,16);
   
   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_LFD,vaddr,rd);
   return(0);
}

/* LFDU - Load Floating-Point Double with Update */
static fastcall int ppc32_exec_LFDU(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + sign_extend_32(imm,16);
   ppc32_exec_memop(cpu,PPC_MEMOP_LFD,vaddr,rd);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* LFDUX - Load Floating-Point Double with Update Indexed */
static fastcall int ppc32_exec_LFDUX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + cpu->gpr[rb];
   ppc32_exec_memop(cpu,PPC_MEMOP_LFD,vaddr,rd);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* LFDX - Load Floating-Point Double Indexed */
static fastcall int ppc32_exec_LFDX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_LFD,vaddr,rd);
   return(0);
}

/* LSWI - Load String Word Immediate */
static fastcall int ppc32_exec_LSWI(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int nb = bits(insn,11,15);
   m_uint32_t vaddr = 0;
   int r;

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   if (nb == 0)
      nb = 32;

   r = rd - 1;
   cpu->sw_pos = 0;

   while(nb > 0) {
      if (cpu->sw_pos == 0) {
         r = (r + 1) & 0x1F;
         cpu->gpr[r] = 0;
      }
      
      ppc32_exec_memop(cpu,PPC_MEMOP_LSW,vaddr,r);
      cpu->sw_pos += 8;

      if (cpu->sw_pos == 32)
         cpu->sw_pos = 0; 

      vaddr++;
      nb--;
   }

   return(0);
}

/* LSWX - Load String Word Indexed */
static fastcall int ppc32_exec_LSWX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;
   int r,nb;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   nb = cpu->xer & PPC32_XER_BC_MASK;
   r = rd - 1;
   cpu->sw_pos = 0;

   while(nb > 0) {
      if (cpu->sw_pos == 0) {
         r = (r + 1) & 0x1F;
         cpu->gpr[r] = 0;
      }
      
      ppc32_exec_memop(cpu,PPC_MEMOP_LSW,vaddr,r);
      cpu->sw_pos += 8;

      if (cpu->sw_pos == 32)
         cpu->sw_pos = 0; 

      vaddr++;
      nb--;
   }

   return(0);
}

/* MCRF - Move Condition Register Field */
static fastcall int ppc32_exec_MCRF(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,23,25);
   int rs = bits(insn,18,20);

   cpu->cr_fields[rd] = cpu->cr_fields[rs];
   return(0);
}

/* MFCR - Move from Condition Register */
static fastcall int ppc32_exec_MFCR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);

   cpu->gpr[rd] = ppc32_get_cr(cpu);
   return(0);
}

/* MFMSR - Move from Machine State Register */
static fastcall int ppc32_exec_MFMSR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);

   cpu->gpr[rd] = cpu->msr;
   return(0);
}

/* MFTBU - Move from Time Base (Up) */
static fastcall int ppc32_exec_MFTBU(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);

   cpu->gpr[rd] = cpu->tb >> 32;
   return(0);
}

/* MFTBL - Move from Time Base (Lo) */
static fastcall int ppc32_exec_MFTBL(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);

   cpu->tb += 50;

   cpu->gpr[rd] = cpu->tb & 0xFFFFFFFF;
   return(0);
}

/* MFSPR - Move from Special-Purpose Register */
static fastcall int ppc32_exec_MFSPR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd   = bits(insn,21,25);
   int spr0 = bits(insn,16,20);
   int spr1 = bits(insn,11,15);
   u_int spr;
   
   spr = (spr1 << 5) | spr0;
   cpu->gpr[rd] = 0;
   
   //cpu_log(cpu->gen,"SPR","reading SPR=%d at cpu->ia=0x%8.8x\n",spr,cpu->ia);

   if ((spr1 == 0x10) || (spr1 == 0x11)) {
      cpu->gpr[rd] = ppc32_get_bat_spr(cpu,spr);
      return(0);
   }

   switch(spr) {
      case PPC32_SPR_XER:
         cpu->gpr[rd] = cpu->xer | (cpu->xer_ca << PPC32_XER_CA_BIT);
         break;
      case PPC32_SPR_DSISR:
         cpu->gpr[rd] = cpu->dsisr;
         break;
      case PPC32_SPR_DAR:
         cpu->gpr[rd] = cpu->dar;
         break;
      case PPC32_SPR_DEC:
         cpu->gpr[rd] = cpu->dec;
         break;
      case PPC32_SPR_SDR1:
         cpu->gpr[rd] = cpu->sdr1;
         break;
      case PPC32_SPR_SRR0:
         cpu->gpr[rd] = cpu->srr0;
         break;
      case PPC32_SPR_SRR1:
         cpu->gpr[rd] = cpu->srr1;
         break;
      case PPC32_SPR_TBL_READ:
         cpu->gpr[rd] = cpu->tb & 0xFFFFFFFF;
         break;
      case PPC32_SPR_TBU_READ:
         cpu->gpr[rd] = cpu->tb >> 32;
         break;
      case PPC32_SPR_SPRG0:
         cpu->gpr[rd] = cpu->sprg[0];
         break;
      case PPC32_SPR_SPRG1:
         cpu->gpr[rd] = cpu->sprg[1];
         break;
      case PPC32_SPR_SPRG2:
         cpu->gpr[rd] = cpu->sprg[2];
         break;
      case PPC32_SPR_SPRG3:
         cpu->gpr[rd] = cpu->sprg[3];
         break;
      case PPC32_SPR_PVR:
         cpu->gpr[rd] = cpu->pvr;
         break;
      case PPC32_SPR_HID0:
         cpu->gpr[rd] = cpu->hid0;
         break;
      case PPC32_SPR_HID1:
         cpu->gpr[rd] = cpu->hid1;
         break;
      case PPC405_SPR_PID:
         cpu->gpr[rd] = cpu->ppc405_pid;
         break;

      /* MPC860 IMMR */
      case 638:
         cpu->gpr[rd] = cpu->mpc860_immr;
         break;

      default:
         cpu->gpr[rd] = 0x0;
         //printf("READING SPR = %d\n",spr);
   }

   return(0);
}

/* MFSR - Move From Segment Register */
static fastcall int ppc32_exec_MFSR(cpu_ppc_t *cpu,ppc_insn_t insn)
{   
   int rd = bits(insn,21,25);
   int sr = bits(insn,16,19);

   cpu->gpr[rd] = cpu->sr[sr];
   return(0);
}

/* MFSRIN - Move From Segment Register Indirect */
static fastcall int ppc32_exec_MFSRIN(cpu_ppc_t *cpu,ppc_insn_t insn)
{   
   int rd = bits(insn,21,25);
   int rb = bits(insn,11,15);

   cpu->gpr[rd] = cpu->sr[cpu->gpr[rb] >> 28];
   return(0);
}

/* MTCRF - Move to Condition Register Fields */
static fastcall int ppc32_exec_MTCRF(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int crm = bits(insn,12,19);
   int i;
   
   for(i=0;i<8;i++)
      if (crm & (1 << (7 - i)))
         cpu->cr_fields[i] = (cpu->gpr[rs] >> (28 - (i << 2))) & 0x0F;

   return(0);
}

/* MTMSR - Move to Machine State Register */
static fastcall int ppc32_exec_MTMSR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);

   cpu->msr = cpu->gpr[rs];
   cpu->irq_check = (cpu->msr & PPC32_MSR_EE) && cpu->irq_pending;

   //printf("New MSR = 0x%8.8x at cpu->ia=0x%8.8x\n",cpu->msr,cpu->ia);
   return(0);
}

/* MTSPR - Move to Special-Purpose Register */
static fastcall int ppc32_exec_MTSPR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd   = bits(insn,21,25);
   int spr0 = bits(insn,16,20);
   int spr1 = bits(insn,11,15);
   u_int spr;
   
   spr = (spr1 << 5) | spr0;

   //cpu_log(cpu->gen,"SPR","writing SPR=%d, val=0x%8.8x at cpu->ia=0x%8.8x\n",
   //        spr,cpu->ia,cpu->gpr[rd]);

   if ((spr1 == 0x10) || (spr1 == 0x11)) {
      ppc32_set_bat_spr(cpu,spr,cpu->gpr[rd]);
      return(0);
   }

   switch(spr) {
      case PPC32_SPR_XER:
         cpu->xer = cpu->gpr[rd] & ~PPC32_XER_CA;
         cpu->xer_ca = (cpu->gpr[rd] >> PPC32_XER_CA_BIT) & 0x1;
         break;
      case PPC32_SPR_DEC:
         //printf("WRITING DECR 0x%8.8x AT IA=0x%8.8x\n",cpu->gpr[rd],cpu->ia);
         cpu->dec = cpu->gpr[rd];
         cpu->timer_irq_armed = TRUE;
         break;
      case PPC32_SPR_SDR1:
         ppc32_set_sdr1(cpu,cpu->gpr[rd]);
         break;
      case PPC32_SPR_SRR0:
         cpu->srr0 = cpu->gpr[rd];
         break;
      case PPC32_SPR_SRR1:
         cpu->srr1 = cpu->gpr[rd];
         break;
      case PPC32_SPR_SPRG0:
         cpu->sprg[0] = cpu->gpr[rd];
         break;
      case PPC32_SPR_SPRG1:
         cpu->sprg[1] = cpu->gpr[rd];
         break;
      case PPC32_SPR_SPRG2:
         cpu->sprg[2] = cpu->gpr[rd];
         break;
      case PPC32_SPR_SPRG3:
         cpu->sprg[3] = cpu->gpr[rd];
         break;
      case PPC32_SPR_HID0:
         cpu->hid0 = cpu->gpr[rd];
         break;
      case PPC32_SPR_HID1:
         cpu->hid1 = cpu->gpr[rd];
         break;
      case PPC405_SPR_PID:
         cpu->ppc405_pid = cpu->gpr[rd];
         break;
#if 0
      default:
         printf("WRITING SPR=%d, data=0x%8.8x\n",spr,cpu->gpr[rd]);
#endif
   }

   return(0);
}

/* MTSR - Move To Segment Register */
static fastcall int ppc32_exec_MTSR(cpu_ppc_t *cpu,ppc_insn_t insn)
{   
   int rs = bits(insn,21,25);
   int sr = bits(insn,16,19);

   cpu->sr[sr] = cpu->gpr[rs];
   ppc32_mem_invalidate_cache(cpu);
   return(0);
}

/* MULHW - Multiply High Word */
static fastcall int ppc32_exec_MULHW(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_int64_t tmp;
   m_uint32_t res;
 
   tmp =  (m_int64_t)(m_int32_t)cpu->gpr[ra];
   tmp *= (m_int64_t)(m_int32_t)cpu->gpr[rb];
   res = tmp >> 32;

   cpu->gpr[rd] = res;
   return(0);
}

/* MULHW. */
static fastcall int ppc32_exec_MULHW_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_int64_t tmp;
   m_uint32_t res;
 
   tmp =  (m_int64_t)(m_int32_t)cpu->gpr[ra];
   tmp *= (m_int64_t)(m_int32_t)cpu->gpr[rb];
   res = tmp >> 32;
   ppc32_exec_update_cr0(cpu,res);
   cpu->gpr[rd] = res;
   return(0);
}

/* MULHWU - Multiply High Word Unsigned */
static fastcall int ppc32_exec_MULHWU(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint64_t tmp;
   m_uint32_t res;
 
   tmp =  (m_uint64_t)cpu->gpr[ra];
   tmp *= (m_uint64_t)cpu->gpr[rb];
   res = tmp >> 32;

   cpu->gpr[rd] = res;
   return(0);
}

/* MULHWU. */
static fastcall int ppc32_exec_MULHWU_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint64_t tmp;
   m_uint32_t res;
 
   tmp =  (m_uint64_t)cpu->gpr[ra];
   tmp *= (m_uint64_t)cpu->gpr[rb];
   res = tmp >> 32;
   ppc32_exec_update_cr0(cpu,res);
   cpu->gpr[rd] = res;
   return(0);
}

/* MULLI - Multiply Low Immediate */
static fastcall int ppc32_exec_MULLI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);

   cpu->gpr[rd] = (m_int32_t)cpu->gpr[ra] * sign_extend_32(imm,16);
   return(0);
}

/* MULLW - Multiply Low Word */
static fastcall int ppc32_exec_MULLW(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_int64_t tmp;
 
   tmp =  (m_int64_t)(m_int32_t)cpu->gpr[ra];
   tmp *= (m_int64_t)(m_int32_t)cpu->gpr[rb];
   cpu->gpr[rd] = (m_uint32_t)tmp;
   return(0);
}

/* MULLW. */
static fastcall int ppc32_exec_MULLW_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t res;
   m_int64_t tmp;
 
   tmp =  (m_int64_t)(m_int32_t)cpu->gpr[ra];
   tmp *= (m_int64_t)(m_int32_t)cpu->gpr[rb];

   res = (m_uint32_t)tmp;
   ppc32_exec_update_cr0(cpu,res);
   cpu->gpr[rd] = res;
   return(0);
}

/* MULLWO - Multiply Low Word with Overflow */
static fastcall int ppc32_exec_MULLWO(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_int64_t tmp;
 
   tmp =  (m_int64_t)(m_int32_t)cpu->gpr[ra];
   tmp *= (m_int64_t)(m_int32_t)cpu->gpr[rb];

   cpu->xer &= ~PPC32_XER_OV;

   if (unlikely(tmp != (m_int64_t)(m_int32_t)tmp))
      cpu->xer |= PPC32_XER_OV|PPC32_XER_SO;

   cpu->gpr[rd] = (m_uint32_t)tmp;
   return(0);
}

/* MULLWO. */
static fastcall int ppc32_exec_MULLWO_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t res;
   m_int64_t tmp;
 
   tmp =  (m_int64_t)(m_int32_t)cpu->gpr[ra];
   tmp *= (m_int64_t)(m_int32_t)cpu->gpr[rb];

   cpu->xer &= ~PPC32_XER_OV;

   if (unlikely(tmp != (m_int64_t)(m_int32_t)tmp))
      cpu->xer |= PPC32_XER_OV|PPC32_XER_SO;

   res = (m_uint32_t)tmp;
   ppc32_exec_update_cr0(cpu,res);
   cpu->gpr[rd] = res;
   return(0);
}

/* NAND */
static fastcall int ppc32_exec_NAND(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   cpu->gpr[ra] = ~(cpu->gpr[rs] & cpu->gpr[rb]);
   return(0);
}

/* NAND. */
static fastcall int ppc32_exec_NAND_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t tmp;

   tmp = ~(cpu->gpr[rs] & cpu->gpr[rb]);
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* NEG - Negate */
static fastcall int ppc32_exec_NEG(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);

   cpu->gpr[rd] = ~cpu->gpr[ra] + 1;
   return(0);
}

/* NEG. */
static fastcall int ppc32_exec_NEG_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   register m_uint32_t tmp;

   tmp = ~cpu->gpr[ra] + 1;
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[rd] = tmp;
   return(0);
}

/* NEGO */
static fastcall int ppc32_exec_NEGO(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   register m_uint32_t tmp;

   tmp = ~cpu->gpr[ra] + 1;
   cpu->gpr[rd] = tmp;

   cpu->xer &= ~PPC32_XER_OV;

   if (unlikely(tmp == 0x80000000))
      cpu->xer |= PPC32_XER_OV|PPC32_XER_SO;

   ppc32_exec_update_cr0(cpu,tmp);
   return(0);
}

/* NEGO. */
static fastcall int ppc32_exec_NEGO_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   register m_uint32_t tmp;

   tmp = ~cpu->gpr[ra] + 1;
   cpu->gpr[rd] = tmp;

   cpu->xer &= ~PPC32_XER_OV;

   if (unlikely(tmp == 0x80000000))
      cpu->xer |= PPC32_XER_OV|PPC32_XER_SO;

   ppc32_exec_update_cr0(cpu,tmp);
   return(0);
}

/* NOR */
static fastcall int ppc32_exec_NOR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   cpu->gpr[ra] = ~(cpu->gpr[rs] | cpu->gpr[rb]);
   return(0);
}

/* NOR. */
static fastcall int ppc32_exec_NOR_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t tmp;

   tmp = ~(cpu->gpr[rs] | cpu->gpr[rb]);
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* OR */
static fastcall int ppc32_exec_OR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   cpu->gpr[ra] = cpu->gpr[rs] | cpu->gpr[rb];
   return(0);
}

/* OR. */
static fastcall int ppc32_exec_OR_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t tmp;

   tmp = cpu->gpr[rs] | cpu->gpr[rb];
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* ORC - OR with Complement */
static fastcall int ppc32_exec_ORC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   cpu->gpr[ra] = cpu->gpr[rs] | ~cpu->gpr[rb];
   return(0);
}

/* ORC. */
static fastcall int ppc32_exec_ORC_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t tmp;

   tmp = cpu->gpr[rs] | ~cpu->gpr[rb];
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* ORI - OR Immediate */
static fastcall int ppc32_exec_ORI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);

   cpu->gpr[ra] = cpu->gpr[rs] | imm;
   return(0);
}

/* ORIS - OR Immediate Shifted */
static fastcall int ppc32_exec_ORIS(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);

   cpu->gpr[ra] = cpu->gpr[rs] | (imm << 16);
   return(0);
}

/* RFI - Return From Interrupt */
static fastcall int ppc32_exec_RFI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   //printf("RFI: srr0=0x%8.8x, srr1=0x%8.8x\n",cpu->srr0,cpu->srr1);

   cpu->msr &= ~PPC32_RFI_MSR_MASK;
   cpu->msr |= cpu->srr1 & PPC32_RFI_MSR_MASK;

   cpu->msr &= ~(1 << 13);
   cpu->ia = cpu->srr0 & ~0x03;

   cpu->irq_check = (cpu->msr & PPC32_MSR_EE) && cpu->irq_pending;

   //printf("NEW IA=0x%8.8x, NEW MSR=0x%8.8x\n",cpu->ia,cpu->msr);
   return(1);
}

/* RLWIMI - Rotate Left Word Immediate then Mask Insert */
static fastcall int ppc32_exec_RLWIMI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int sh = bits(insn,11,15);
   int mb = bits(insn,6,10);
   int me = bits(insn,1,5);
   register m_uint32_t r,mask;
   
   r = (cpu->gpr[rs] << sh) | (cpu->gpr[rs] >> (32 - sh));
   mask = ppc32_rotate_mask(mb,me);
   cpu->gpr[ra] = (r & mask) | (cpu->gpr[ra] & ~mask);
   return(0);
}

/* RLWIMI. - Rotate Left Word Immediate then Mask Insert */
static fastcall int ppc32_exec_RLWIMI_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int sh = bits(insn,11,15);
   int mb = bits(insn,6,10);
   int me = bits(insn,1,5);
   register m_uint32_t r,mask,tmp;
   
   r = (cpu->gpr[rs] << sh) | (cpu->gpr[rs] >> (32 - sh));
   mask = ppc32_rotate_mask(mb,me);
   tmp = (r & mask) | (cpu->gpr[ra] & ~mask);
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* RLWINM - Rotate Left Word Immediate AND with Mask */
static fastcall int ppc32_exec_RLWINM(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int sh = bits(insn,11,15);
   int mb = bits(insn,6,10);
   int me = bits(insn,1,5);
   register m_uint32_t r,mask;
   
   r = (cpu->gpr[rs] << sh) | (cpu->gpr[rs] >> (32 - sh));
   mask = ppc32_rotate_mask(mb,me);
   cpu->gpr[ra] = r & mask;
   return(0);
}

/* RLWINM. - Rotate Left Word Immediate AND with Mask */
static fastcall int ppc32_exec_RLWINM_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int sh = bits(insn,11,15);
   int mb = bits(insn,6,10);
   int me = bits(insn,1,5);
   register m_uint32_t r,mask,tmp;
   
   r = (cpu->gpr[rs] << sh) | (cpu->gpr[rs] >> (32 - sh));
   mask = ppc32_rotate_mask(mb,me);
   tmp = r & mask;
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* RLWNM - Rotate Left Word then Mask Insert */
static fastcall int ppc32_exec_RLWNM(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int mb = bits(insn,6,10);
   int me = bits(insn,1,5);
   register m_uint32_t r,sh,mask;

   sh = cpu->gpr[rb] & 0x1f;
   r = (cpu->gpr[rs] << sh) | (cpu->gpr[rs] >> (32 - sh));
   mask = ppc32_rotate_mask(mb,me);
   cpu->gpr[ra] = r & mask;
   return(0);
}

/* RLWNM. - Rotate Left Word then Mask Insert */
static fastcall int ppc32_exec_RLWNM_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int mb = bits(insn,6,10);
   int me = bits(insn,1,5);
   register m_uint32_t r,sh,mask,tmp;

   sh = cpu->gpr[rb] & 0x1f;
   r = (cpu->gpr[rs] << sh) | (cpu->gpr[rs] >> (32 - sh));
   mask = ppc32_rotate_mask(mb,me);
   tmp = r & mask;
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* SC - System Call */
static fastcall int ppc32_exec_SC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   ppc32_trigger_exception(cpu,PPC32_EXC_SYSCALL);
   return(1);
}

/* SLW - Shift Left Word */
static fastcall int ppc32_exec_SLW(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t s;

   s = cpu->gpr[rb] & 0x3f;

   if (likely(!(s & 0x20)))
      cpu->gpr[ra] = cpu->gpr[rs] << s;
   else
      cpu->gpr[ra] = 0;
   
   return(0);
}

/* SLW. */
static fastcall int ppc32_exec_SLW_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t s,tmp;

   s = cpu->gpr[rb] & 0x3f;

   if (likely(!(s & 0x20)))
      tmp = cpu->gpr[rs] << s;
   else
      tmp = 0;
   
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* SRAW - Shift Right Algebraic Word */
static fastcall int ppc32_exec_SRAW(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t s,mask;
   int sh;

   cpu->xer_ca = 0;

   s  = cpu->gpr[rs];
   sh = cpu->gpr[rb];

   if (unlikely(sh & 0x20)) {
      cpu->gpr[ra] = (m_int32_t)s >> 31;
      cpu->xer_ca = cpu->gpr[ra] & 0x1;
      return(0);
   }

   cpu->gpr[ra] = (m_int32_t)s >> sh;
   mask = ~(0xFFFFFFFFU << sh);
   
   if ((s & 0x80000000) && ((s & mask) != 0))
      cpu->xer_ca = 1;

   return(0);
}

/* SRAWI - Shift Right Algebraic Word Immediate */
static fastcall int ppc32_exec_SRAWI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int sh = bits(insn,11,15);
   register m_uint32_t s,mask;

   cpu->xer_ca = 0;

   s = cpu->gpr[rs];
   cpu->gpr[ra] = (m_int32_t)s >> sh;
   mask = ~(0xFFFFFFFFU << sh);
   
   if ((s & 0x80000000) && ((s & mask) != 0))
      cpu->xer_ca = 1;

   return(0);
}

/* SRAWI. */
static fastcall int ppc32_exec_SRAWI_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int sh = bits(insn,11,15);
   register m_uint32_t s,r,mask;

   cpu->xer_ca = 0;

   s = cpu->gpr[rs];
   r = (m_int32_t)s >> sh;
   mask = ~(0xFFFFFFFFU << sh);
   
   if ((s & 0x80000000) && ((s & mask) != 0))
      cpu->xer_ca = 1;

   ppc32_exec_update_cr0(cpu,r);
   cpu->gpr[ra] = r;
   return(0);
}

/* SRW - Shift Right Word */
static fastcall int ppc32_exec_SRW(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t s;

   s = cpu->gpr[rb] & 0x3f;

   if (likely(!(s & 0x20)))
      cpu->gpr[ra] = cpu->gpr[rs] >> s;
   else
      cpu->gpr[ra] = 0;
   
   return(0);
}

/* SRW. */
static fastcall int ppc32_exec_SRW_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t s,tmp;

   s = cpu->gpr[rb] & 0x3f;

   if (likely(!(s & 0x20)))
      tmp = cpu->gpr[rs] >> s;
   else
      tmp = 0;
   
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* STB - Store Byte */
static fastcall int ppc32_exec_STB(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = sign_extend_32(imm,16);
   
   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_STB,vaddr,rs);
   return(0);
}

/* STBU - Store Byte with Update */
static fastcall int ppc32_exec_STBU(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + sign_extend_32(imm,16);
   ppc32_exec_memop(cpu,PPC_MEMOP_STB,vaddr,rs);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* STBUX - Store Byte with Update Indexed */
static fastcall int ppc32_exec_STBUX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + cpu->gpr[rb];
   ppc32_exec_memop(cpu,PPC_MEMOP_STB,vaddr,rs);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* STBX - Store Byte Indexed */
static fastcall int ppc32_exec_STBX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_STB,vaddr,rs);
   return(0);
}

/* STH - Store Half-Word */
static fastcall int ppc32_exec_STH(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = sign_extend_32(imm,16);
   
   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_STH,vaddr,rs);
   return(0);
}

/* STHU - Store Half-Word with Update */
static fastcall int ppc32_exec_STHU(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs  = bits(insn,21,25);
   int ra  = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + sign_extend_32(imm,16);
   ppc32_exec_memop(cpu,PPC_MEMOP_STH,vaddr,rs);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* STHUX - Store Half-Word with Update Indexed */
static fastcall int ppc32_exec_STHUX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + cpu->gpr[rb];
   ppc32_exec_memop(cpu,PPC_MEMOP_STH,vaddr,rs);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* STHX - Store Half-Word Indexed */
static fastcall int ppc32_exec_STHX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_STH,vaddr,rs);
   return(0);
}

/* STMW - Store Multiple Word */
static fastcall int ppc32_exec_STMW(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;
   int r;

   vaddr = sign_extend_32(imm,16);
   
   if (ra != 0)
      vaddr += cpu->gpr[ra];

   for(r=rs;r<=31;r++) {
      ppc32_exec_memop(cpu,PPC_MEMOP_STW,vaddr,r);
      vaddr += sizeof(m_uint32_t);
   }

   return(0);
}

/* STW - Store Word */
static fastcall int ppc32_exec_STW(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = sign_extend_32(imm,16);
   
   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_STW,vaddr,rs);
   return(0);
}

/* STWU - Store Word with Update */
static fastcall int ppc32_exec_STWU(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + sign_extend_32(imm,16);
   ppc32_exec_memop(cpu,PPC_MEMOP_STW,vaddr,rs);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* STWUX - Store Word with Update Indexed */
static fastcall int ppc32_exec_STWUX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + cpu->gpr[rb];
   ppc32_exec_memop(cpu,PPC_MEMOP_STW,vaddr,rs);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* STWX - Store Word Indexed */
static fastcall int ppc32_exec_STWX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_STW,vaddr,rs);
   return(0);
}

/* STWBRX - Store Word Byte-Reverse Indexed */
static fastcall int ppc32_exec_STWBRX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_STWBR,vaddr,rs);
   return(0);
}

/* STWCX. - Store Word Conditional Indexed */
static fastcall int ppc32_exec_STWCX_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   if (cpu->reserve) {
      ppc32_exec_memop(cpu,PPC_MEMOP_STW,vaddr,rs);

      cpu->cr_fields[0] = 1 << PPC32_CR_EQ_BIT;

      if (cpu->xer & PPC32_XER_SO)
         cpu->cr_fields[0] |= 1 << PPC32_CR_SO_BIT;

      cpu->reserve = 0;
   } else {
      cpu->cr_fields[0] = 0;

      if (cpu->xer & PPC32_XER_SO)
         cpu->cr_fields[0] |= 1 << PPC32_CR_SO_BIT;
   }

   return(0);
}

/* STFD - Store Floating-Point Double */
static fastcall int ppc32_exec_STFD(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = sign_extend_32(imm,16);
   
   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_STFD,vaddr,rs);
   return(0);
}

/* STFDU - Store Floating-Point Double with Update */
static fastcall int ppc32_exec_STFDU(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + sign_extend_32(imm,16);
   ppc32_exec_memop(cpu,PPC_MEMOP_STFD,vaddr,rs);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* STFDUX - Store Floating-Point Double with Update Indexed */
static fastcall int ppc32_exec_STFDUX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[ra] + cpu->gpr[rb];
   ppc32_exec_memop(cpu,PPC_MEMOP_STFD,vaddr,rs);
   cpu->gpr[ra] = vaddr;
   return(0);
}

/* STFDX - Store Floating-Point Double Indexed */
static fastcall int ppc32_exec_STFDX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   ppc32_exec_memop(cpu,PPC_MEMOP_STFD,vaddr,rs);
   return(0);
}

/* STSWI - Store String Word Immediate */
static fastcall int ppc32_exec_STSWI(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int nb = bits(insn,11,15);
   m_uint32_t vaddr = 0;
   int r;

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   if (nb == 0)
      nb = 32;

   r = rs - 1;
   cpu->sw_pos = 0;

   while(nb > 0) {
      if (cpu->sw_pos == 0) 
         r = (r + 1) & 0x1F;
      
      ppc32_exec_memop(cpu,PPC_MEMOP_STSW,vaddr,r);
      cpu->sw_pos += 8;

      if (cpu->sw_pos == 32)
         cpu->sw_pos = 0;      

      vaddr++;
      nb--;
   }

   return(0);
}

/* STSWX - Store String Word Indexed */
static fastcall int ppc32_exec_STSWX(cpu_ppc_t *cpu,ppc_insn_t insn)
{  
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;
   int r,nb;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   nb = cpu->xer & PPC32_XER_BC_MASK;
   r = rs - 1;
   cpu->sw_pos = 0;

   while(nb > 0) {
      if (cpu->sw_pos == 0) 
         r = (r + 1) & 0x1F;
      
      ppc32_exec_memop(cpu,PPC_MEMOP_STSW,vaddr,r);
      cpu->sw_pos += 8;

      if (cpu->sw_pos == 32)
         cpu->sw_pos = 0;      

      vaddr++;
      nb--;
   }

   return(0);
}

/* SUBF - Subtract From */
static fastcall int ppc32_exec_SUBF(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   cpu->gpr[rd] = cpu->gpr[rb] - cpu->gpr[ra];
   return(0);
}

/* SUBF. */
static fastcall int ppc32_exec_SUBF_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t tmp;

   tmp = cpu->gpr[rb] - cpu->gpr[ra];
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[rd] = tmp;
   return(0);
}

/* SUBFO - Subtract From with Overflow */
static fastcall int ppc32_exec_SUBFO(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,tmp;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];

   tmp = b - a;
   ppc32_exec_ov_sub(cpu,tmp,b,a);
   cpu->gpr[rd] = tmp;
   return(0);
}

/* SUBFO. */
static fastcall int ppc32_exec_SUBFO_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,tmp;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];

   tmp = b - a;
   ppc32_exec_ov_sub(cpu,tmp,b,a);
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[rd] = tmp;
   return(0);
}

/* SUBFC - Subtract From Carrying */
static fastcall int ppc32_exec_SUBFC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,r,tmp;

   a = ~cpu->gpr[ra];
   b = cpu->gpr[rb];
   
   tmp = a + 1;
   r = b + tmp;

   ppc32_exec_ca_sum(cpu,tmp,a,1);
   if (r < tmp)
      cpu->xer_ca = 1;

   cpu->gpr[rd] = r;
   return(0);
}

/* SUBFC. */
static fastcall int ppc32_exec_SUBFC_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,r,tmp;

   a = ~cpu->gpr[ra];
   b = cpu->gpr[rb];
   
   tmp = a + 1;
   r = b + tmp;

   ppc32_exec_ca_sum(cpu,tmp,a,1);
   if (r < tmp)
      cpu->xer_ca = 1;

   ppc32_exec_update_cr0(cpu,r);
   cpu->gpr[rd] = r;
   return(0);
}

/* SUBFCO - Subtract From with Overflow */
__unused static fastcall int ppc32_exec_SUBFCO(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,tmp;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   tmp = b - a;

   ppc32_exec_ca_sub(cpu,tmp,b,a);
   ppc32_exec_ov_sub(cpu,tmp,b,a);
   cpu->gpr[rd] = tmp;
   return(0);
}

/* SUBFCO. */
__unused static fastcall int ppc32_exec_SUBFCO_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,tmp;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];
   tmp = b - a;

   ppc32_exec_ca_sub(cpu,tmp,b,a);
   ppc32_exec_ov_sub(cpu,tmp,b,a);
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[rd] = tmp;
   return(0);
}

/* SUBFE - Subtract From Carrying */
static fastcall int ppc32_exec_SUBFE(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   register m_uint32_t a,b,r,tmp;
   m_uint32_t carry;

   carry = cpu->xer_ca;

   a = ~cpu->gpr[ra];
   b = cpu->gpr[rb];
   tmp = a + carry;
   r = b + tmp;

   ppc32_exec_ca_sum(cpu,tmp,a,carry);
   if (r < tmp)
      cpu->xer_ca = 1;

   cpu->gpr[rd] = r;
   return(0);
}

/* SUBFIC - Subtract From Immediate Carrying */
static fastcall int ppc32_exec_SUBFIC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   register m_uint32_t a,b,r,tmp;

   a = ~cpu->gpr[ra];
   b = sign_extend_32(imm,16);
   
   tmp = a + 1;
   r = b + tmp;

   ppc32_exec_ca_sum(cpu,tmp,a,1);
   if (r < tmp)
      cpu->xer_ca = 1;

   cpu->gpr[rd] = r;
   return(0);
}

/* SUBFZE - Subtract From Zero extended */
static fastcall int ppc32_exec_SUBFZE(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   register m_uint32_t a,r;
   m_uint32_t carry;

   carry = cpu->xer_ca;

   a = ~cpu->gpr[ra];
   r = a + carry;

   if (r < a)
      cpu->xer_ca = 1;

   cpu->gpr[rd] = r;
   return(0);
}

/* SUBFZE. */
static fastcall int ppc32_exec_SUBFZE_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   register m_uint32_t a,r;
   m_uint32_t carry;

   carry = cpu->xer_ca;

   a = ~cpu->gpr[ra];
   r = a + carry;

   if (r < a)
      cpu->xer_ca = 1;

   ppc32_exec_update_cr0(cpu,r);
   cpu->gpr[rd] = r;
   return(0);
}

/* SYNC - Synchronize */
static fastcall int ppc32_exec_SYNC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   return(0);
}

/* TLBIA - TLB Invalidate All */
static fastcall int ppc32_exec_TLBIA(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   ppc32_mem_invalidate_cache(cpu);
   return(0);
}

/* TLBIE - TLB Invalidate Entry */
static fastcall int ppc32_exec_TLBIE(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   ppc32_mem_invalidate_cache(cpu);
   return(0);
}

/* TLBSYNC - TLB Synchronize */
static fastcall int ppc32_exec_TLBSYNC(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   return(0);
}

/* TW - Trap Word */
static fastcall int ppc32_exec_TW(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int to = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_int32_t a,b;

   a = cpu->gpr[ra];
   b = cpu->gpr[rb];

   if (((a < b)  && (to & 0x10)) ||
       ((a > b)  && (to & 0x08)) ||
       ((a == b) && (to & 0x04)) ||
       (((m_uint32_t)a < (m_uint32_t)b) && (to & 0x02)) ||
       (((m_uint32_t)a > (m_uint32_t)b) && (to & 0x01)))
   {
      ppc32_trigger_exception(cpu,PPC32_EXC_PROG);
      cpu->srr1 |= 1 << 17;
      return(1);
   }

   return(0);
}

/* TWI - Trap Word Immediate */
static fastcall int ppc32_exec_TWI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int to = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_int32_t a,b;

   a = cpu->gpr[ra];
   b = sign_extend(imm,16);

   if (((a < b)  && (to & 0x10)) ||
       ((a > b)  && (to & 0x08)) ||
       ((a == b) && (to & 0x04)) ||
       (((m_uint32_t)a < (m_uint32_t)b) && (to & 0x02)) ||
       (((m_uint32_t)a > (m_uint32_t)b) && (to & 0x01)))
   {
      ppc32_trigger_exception(cpu,PPC32_EXC_PROG);
      cpu->srr1 |= 1 << 17;
      return(1);
   }

   return(0);
}

/* XOR */
static fastcall int ppc32_exec_XOR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   cpu->gpr[ra] = cpu->gpr[rs] ^ cpu->gpr[rb];
   return(0);
}

/* XOR. */
static fastcall int ppc32_exec_XOR_dot(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t tmp;

   tmp = cpu->gpr[rs] ^ cpu->gpr[rb];
   ppc32_exec_update_cr0(cpu,tmp);
   cpu->gpr[ra] = tmp;
   return(0);
}

/* XORI - XOR Immediate */
static fastcall int ppc32_exec_XORI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);

   cpu->gpr[ra] = cpu->gpr[rs] ^ imm;
   return(0);
}

/* XORIS - XOR Immediate Shifted */
static fastcall int ppc32_exec_XORIS(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);

   cpu->gpr[ra] = cpu->gpr[rs] ^ (imm << 16);
   return(0);
}

/* DCCCI - Data Cache Congruence Class Invalidate (PowerPC 405) */
static fastcall int ppc32_exec_DCCCI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   return(0);
}

/* ICCCI - Instruction Cache Congruence Class Invalidate (PowerPC 405) */
static fastcall int ppc32_exec_ICCCI(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   m_uint32_t vaddr;

   vaddr = cpu->gpr[rb];

   if (ra != 0)
      vaddr += cpu->gpr[ra];

   return(0);
}

/* MFDCR - Move From Device Control Register (PowerPC 405) */
static fastcall int ppc32_exec_MFDCR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int UNUSED(rt) = bits(insn,21,25);
   return(0);
}

/* MTDCR - Move To Device Control Register (PowerPC 405) */
static fastcall int ppc32_exec_MTDCR(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int UNUSED(rt) = bits(insn,21,25);
   return(0);
}

/* TLBRE - TLB Read Entry (PowerPC 405) */
static fastcall int ppc32_exec_TLBRE(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int ws = bits(insn,11,15);
   m_uint32_t index;

   index = cpu->gpr[ra] & 0x3F;

   if (ws == 1) {
      cpu->gpr[rt] = cpu->ppc405_tlb[index].tlb_lo;
   } else {
      cpu->gpr[rt] = cpu->ppc405_tlb[index].tlb_hi;
      cpu->ppc405_pid = cpu->ppc405_tlb[index].tid;
   }

   return(0);
}

/* TLBWE - TLB Write Entry (PowerPC 405) */
static fastcall int ppc32_exec_TLBWE(cpu_ppc_t *cpu,ppc_insn_t insn)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int ws = bits(insn,11,15);
   m_uint32_t index;

   index = cpu->gpr[ra] & 0x3F;

   if (ws == 1) {
      cpu->ppc405_tlb[index].tlb_lo = cpu->gpr[rs];
   } else {
      cpu->ppc405_tlb[index].tlb_hi = cpu->gpr[rs];
      cpu->ppc405_tlb[index].tid = cpu->ppc405_pid;
   }

   return(0);
}

/* PowerPC instruction array */
static struct ppc32_insn_exec_tag ppc32_exec_tags[] = {
   { "mflr"    , ppc32_exec_MFLR       , 0xfc1fffff , 0x7c0802a6, 0 },
   { "mtlr"    , ppc32_exec_MTLR       , 0xfc1fffff , 0x7c0803a6, 0 },
   { "mfctr"   , ppc32_exec_MFCTR      , 0xfc1fffff , 0x7c0902a6, 0 },
   { "mtctr"   , ppc32_exec_MTCTR      , 0xfc1fffff , 0x7c0903a6, 0 },
   { "add"     , ppc32_exec_ADD        , 0xfc0007ff , 0x7c000214, 0 },
   { "add."    , ppc32_exec_ADD_dot    , 0xfc0007ff , 0x7c000215, 0 },
   { "addo"    , ppc32_exec_ADDO       , 0xfc0007ff , 0x7c000614, 0 },
   { "addo."   , ppc32_exec_ADDO_dot   , 0xfc0007ff , 0x7c000615, 0 },
   { "addc"    , ppc32_exec_ADDC       , 0xfc0007ff , 0x7c000014, 0 },
   { "addc."   , ppc32_exec_ADDC_dot   , 0xfc0007ff , 0x7c000015, 0 },
   { "addco"   , ppc32_exec_ADDCO      , 0xfc0007ff , 0x7c000414, 0 },
   { "addco."  , ppc32_exec_ADDCO_dot  , 0xfc0007ff , 0x7c000415, 0 },
   { "adde"    , ppc32_exec_ADDE       , 0xfc0007ff , 0x7c000114, 0 },
   { "adde."   , ppc32_exec_ADDE_dot   , 0xfc0007ff , 0x7c000115, 0 },
   { "addeo"   , ppc32_exec_ADDEO      , 0xfc0007ff , 0x7c000514, 0 },
   { "addeo."  , ppc32_exec_ADDEO_dot  , 0xfc0007ff , 0x7c000515, 0 },
   { "addi"    , ppc32_exec_ADDI       , 0xfc000000 , 0x38000000, 0 },
   { "addic"   , ppc32_exec_ADDIC      , 0xfc000000 , 0x30000000, 0 },
   { "addic."  , ppc32_exec_ADDIC_dot  , 0xfc000000 , 0x34000000, 0 },
   { "addis"   , ppc32_exec_ADDIS      , 0xfc000000 , 0x3c000000, 0 },
   { "addme"   , ppc32_exec_ADDME      , 0xfc00ffff , 0x7c0001d4, 0 },
   { "addme."  , ppc32_exec_ADDME_dot  , 0xfc00ffff , 0x7c0001d5, 0 },
   { "addze"   , ppc32_exec_ADDZE      , 0xfc00ffff , 0x7c000194, 0 },
   { "addze."  , ppc32_exec_ADDZE_dot  , 0xfc00ffff , 0x7c000195, 0 },
   { "and"     , ppc32_exec_AND        , 0xfc0007ff , 0x7c000038, 0 },
   { "and."    , ppc32_exec_AND_dot    , 0xfc0007ff , 0x7c000039, 0 },
   { "andc"    , ppc32_exec_ANDC       , 0xfc0007ff , 0x7c000078, 0 },
   { "andc."   , ppc32_exec_ANDC_dot   , 0xfc0007ff , 0x7c000079, 0 },
   { "andi."   , ppc32_exec_ANDI_dot   , 0xfc000000 , 0x70000000, 0 },
   { "andis."  , ppc32_exec_ANDIS_dot  , 0xfc000000 , 0x74000000, 0 },
   { "b"       , ppc32_exec_B          , 0xfc000003 , 0x48000000, 0 },
   { "ba"      , ppc32_exec_BA         , 0xfc000003 , 0x48000002, 0 },
   { "bl"      , ppc32_exec_BL         , 0xfc000003 , 0x48000001, 0 },
   { "bla"     , ppc32_exec_BLA        , 0xfc000003 , 0x48000003, 0 },
   { "bc"      , ppc32_exec_BC         , 0xfc000003 , 0x40000000, 0 },
   { "bca"     , ppc32_exec_BCA        , 0xfc000003 , 0x40000002, 0 },
   { "bcl"     , ppc32_exec_BCL        , 0xfc000003 , 0x40000001, 0 },
   { "bcla"    , ppc32_exec_BCLA       , 0xfc000003 , 0x40000003, 0 },
   { "bclr"    , ppc32_exec_BCLR       , 0xfc00ffff , 0x4c000020, 0 },
   { "bclrl"   , ppc32_exec_BCLRL      , 0xfc00ffff , 0x4c000021, 0 },
   { "bcctr"   , ppc32_exec_BCCTR      , 0xfc00ffff , 0x4c000420, 0 },
   { "bcctrl"  , ppc32_exec_BCCTRL     , 0xfc00ffff , 0x4c000421, 0 },
   { "cmp"     , ppc32_exec_CMP        , 0xfc6007ff , 0x7c000000, 0 },
   { "cmpi"    , ppc32_exec_CMPI       , 0xfc600000 , 0x2c000000, 0 },
   { "cmpl"    , ppc32_exec_CMPL       , 0xfc6007ff , 0x7c000040, 0 },
   { "cmpli"   , ppc32_exec_CMPLI      , 0xfc600000 , 0x28000000, 0 },
   { "cntlzw"  , ppc32_exec_CNTLZW     , 0xfc00ffff , 0x7c000034, 0 },
   { "crand"   , ppc32_exec_CRAND      , 0xfc0007ff , 0x4c000202, 0 },
   { "crandc"  , ppc32_exec_CRANDC     , 0xfc0007ff , 0x4c000102, 0 },
   { "creqv"   , ppc32_exec_CREQV      , 0xfc0007ff , 0x4c000242, 0 },
   { "crnand"  , ppc32_exec_CRNAND     , 0xfc0007ff , 0x4c0001c2, 0 },
   { "crnor"   , ppc32_exec_CRNOR      , 0xfc0007ff , 0x4c000042, 0 },
   { "cror"    , ppc32_exec_CROR       , 0xfc0007ff , 0x4c000382, 0 },
   { "crorc"   , ppc32_exec_CRORC      , 0xfc0007ff , 0x4c000342, 0 },
   { "crxor"   , ppc32_exec_CRXOR      , 0xfc0007ff , 0x4c000182, 0 },
   { "dcbf"    , ppc32_exec_DCBF       , 0xffe007ff , 0x7c0000ac, 0 },
   { "dcbi"    , ppc32_exec_DCBI       , 0xffe007ff , 0x7c0003ac, 0 },
   { "dcbt"    , ppc32_exec_DCBT       , 0xffe007ff , 0x7c00022c, 0 },
   { "dcbst"   , ppc32_exec_DCBST      , 0xffe007ff , 0x7c00006c, 0 },
   { "divw"    , ppc32_exec_DIVW       , 0xfc0007ff , 0x7c0003d6, 0 },
   { "divw."   , ppc32_exec_DIVW_dot   , 0xfc0007ff , 0x7c0003d7, 0 },
   { "divwu"   , ppc32_exec_DIVWU      , 0xfc0007ff , 0x7c000396, 0 },
   { "divwu."  , ppc32_exec_DIVWU_dot  , 0xfc0007ff , 0x7c000397, 0 },
   { "eieio"   , ppc32_exec_EIEIO      , 0xffffffff , 0x7c0006ac, 0 },
   { "eqv"     , ppc32_exec_EQV        , 0xfc0007ff , 0x7c000238, 0 },
   { "extsb"   , ppc32_exec_EXTSB      , 0xfc00ffff , 0x7c000774, 0 },
   { "extsb."  , ppc32_exec_EXTSB_dot  , 0xfc00ffff , 0x7c000775, 0 },
   { "extsh"   , ppc32_exec_EXTSH      , 0xfc00ffff , 0x7c000734, 0 },
   { "extsh."  , ppc32_exec_EXTSH_dot  , 0xfc00ffff , 0x7c000735, 0 },
   { "icbi"    , ppc32_exec_ICBI       , 0xffe007ff , 0x7c0007ac, 0 },
   { "isync"   , ppc32_exec_ISYNC      , 0xffffffff , 0x4c00012c, 0 },
   { "lbz"     , ppc32_exec_LBZ        , 0xfc000000 , 0x88000000, 0 },
   { "lbzu"    , ppc32_exec_LBZU       , 0xfc000000 , 0x8c000000, 0 },
   { "lbzux"   , ppc32_exec_LBZUX      , 0xfc0007ff , 0x7c0000ee, 0 },
   { "lbzx"    , ppc32_exec_LBZX       , 0xfc0007ff , 0x7c0000ae, 0 },
   { "lha"     , ppc32_exec_LHA        , 0xfc000000 , 0xa8000000, 0 },
   { "lhau"    , ppc32_exec_LHAU       , 0xfc000000 , 0xac000000, 0 },
   { "lhaux"   , ppc32_exec_LHAUX      , 0xfc0007ff , 0x7c0002ee, 0 },
   { "lhax"    , ppc32_exec_LHAX       , 0xfc0007ff , 0x7c0002ae, 0 },
   { "lhz"     , ppc32_exec_LHZ        , 0xfc000000 , 0xa0000000, 0 },
   { "lhzu"    , ppc32_exec_LHZU       , 0xfc000000 , 0xa4000000, 0 },
   { "lhzux"   , ppc32_exec_LHZUX      , 0xfc0007ff , 0x7c00026e, 0 },
   { "lhzx"    , ppc32_exec_LHZX       , 0xfc0007ff , 0x7c00022e, 0 },
   { "lmw"     , ppc32_exec_LMW        , 0xfc000000 , 0xb8000000, 0 },
   { "lwbrx"   , ppc32_exec_LWBRX      , 0xfc0007ff , 0x7c00042c, 0 },
   { "lwz"     , ppc32_exec_LWZ        , 0xfc000000 , 0x80000000, 0 },
   { "lwzu"    , ppc32_exec_LWZU       , 0xfc000000 , 0x84000000, 0 },
   { "lwzux"   , ppc32_exec_LWZUX      , 0xfc0007ff , 0x7c00006e, 0 },
   { "lwzx"    , ppc32_exec_LWZX       , 0xfc0007ff , 0x7c00002e, 0 },
   { "lwarx"   , ppc32_exec_LWARX      , 0xfc0007ff , 0x7c000028, 0 },
   { "lfd"     , ppc32_exec_LFD        , 0xfc000000 , 0xc8000000, 0 },
   { "lfdu"    , ppc32_exec_LFDU       , 0xfc000000 , 0xcc000000, 0 },
   { "lfdux"   , ppc32_exec_LFDUX      , 0xfc0007ff , 0x7c0004ee, 0 },
   { "lfdx"    , ppc32_exec_LFDX       , 0xfc0007ff , 0x7c0004ae, 0 },
   { "lswi"    , ppc32_exec_LSWI       , 0xfc0007ff , 0x7c0004aa, 0 },
   { "lswx"    , ppc32_exec_LSWX       , 0xfc0007ff , 0x7c00042a, 0 },
   { "mcrf"    , ppc32_exec_MCRF       , 0xfc63ffff , 0x4c000000, 0 },
   { "mfcr"    , ppc32_exec_MFCR       , 0xfc1fffff , 0x7c000026, 0 },
   { "mfmsr"   , ppc32_exec_MFMSR      , 0xfc1fffff , 0x7c0000a6, 0 },
   { "mfspr"   , ppc32_exec_MFSPR      , 0xfc0007ff , 0x7c0002a6, 0 },
   { "mfsr"    , ppc32_exec_MFSR       , 0xfc10ffff , 0x7c0004a6, 0 },
   { "mfsrin"  , ppc32_exec_MFSRIN     , 0xfc1f07ff , 0x7c000526, 0 },
   { "mftbl"   , ppc32_exec_MFTBL      , 0xfc1ff7ff , 0x7c0c42e6, 0 },
   { "mftbu"   , ppc32_exec_MFTBU      , 0xfc1ff7ff , 0x7c0d42e6, 0 },
   { "mtcrf"   , ppc32_exec_MTCRF      , 0xfc100fff , 0x7c000120, 0 },
   { "mtmsr"   , ppc32_exec_MTMSR      , 0xfc1fffff , 0x7c000124, 0 },
   { "mtspr"   , ppc32_exec_MTSPR      , 0xfc0007ff , 0x7c0003a6, 0 },
   { "mtsr"    , ppc32_exec_MTSR       , 0xfc10ffff , 0x7c0001a4, 0 },
   { "mulhw"   , ppc32_exec_MULHW      , 0xfc0007ff , 0x7c000096, 0 },
   { "mulhw."  , ppc32_exec_MULHW_dot  , 0xfc0007ff , 0x7c000097, 0 },
   { "mulhwu"  , ppc32_exec_MULHWU     , 0xfc0007ff , 0x7c000016, 0 },
   { "mulhwu." , ppc32_exec_MULHWU_dot , 0xfc0007ff , 0x7c000017, 0 },
   { "mulli"   , ppc32_exec_MULLI      , 0xfc000000 , 0x1c000000, 0 },
   { "mullw"   , ppc32_exec_MULLW      , 0xfc0007ff , 0x7c0001d6, 0 },
   { "mullw."  , ppc32_exec_MULLW_dot  , 0xfc0007ff , 0x7c0001d7, 0 },
   { "mullwo"  , ppc32_exec_MULLWO     , 0xfc0007ff , 0x7c0005d6, 0 },
   { "mullwo." , ppc32_exec_MULLWO_dot , 0xfc0007ff , 0x7c0005d7, 0 },
   { "nand"    , ppc32_exec_NAND       , 0xfc0007ff , 0x7c0003b8, 0 },
   { "nand."   , ppc32_exec_NAND_dot   , 0xfc0007ff , 0x7c0003b9, 0 },
   { "neg"     , ppc32_exec_NEG        , 0xfc00ffff , 0x7c0000d0, 0 },
   { "neg."    , ppc32_exec_NEG_dot    , 0xfc00ffff , 0x7c0000d1, 0 },
   { "nego"    , ppc32_exec_NEGO       , 0xfc00ffff , 0x7c0004d0, 0 },
   { "nego."   , ppc32_exec_NEGO_dot   , 0xfc00ffff , 0x7c0004d1, 0 },
   { "nor"     , ppc32_exec_NOR        , 0xfc0007ff , 0x7c0000f8, 0 },
   { "nor."    , ppc32_exec_NOR_dot    , 0xfc0007ff , 0x7c0000f9, 0 },
   { "or"      , ppc32_exec_OR         , 0xfc0007ff , 0x7c000378, 0 },
   { "or."     , ppc32_exec_OR_dot     , 0xfc0007ff , 0x7c000379, 0 },
   { "orc"     , ppc32_exec_ORC        , 0xfc0007ff , 0x7c000338, 0 },
   { "orc."    , ppc32_exec_ORC_dot    , 0xfc0007ff , 0x7c000339, 0 },
   { "ori"     , ppc32_exec_ORI        , 0xfc000000 , 0x60000000, 0 },
   { "oris"    , ppc32_exec_ORIS       , 0xfc000000 , 0x64000000, 0 },
   { "rfi"     , ppc32_exec_RFI        , 0xffffffff , 0x4c000064, 0 },
   { "rlwimi"  , ppc32_exec_RLWIMI     , 0xfc000001 , 0x50000000, 0 },
   { "rlwimi." , ppc32_exec_RLWIMI_dot , 0xfc000001 , 0x50000001, 0 },
   { "rlwinm"  , ppc32_exec_RLWINM     , 0xfc000001 , 0x54000000, 0 },
   { "rlwinm." , ppc32_exec_RLWINM_dot , 0xfc000001 , 0x54000001, 0 },
   { "rlwnm"   , ppc32_exec_RLWNM      , 0xfc000001 , 0x5c000000, 0 },
   { "rlwnm."  , ppc32_exec_RLWNM_dot  , 0xfc000001 , 0x5c000001, 0 },
   { "sc"      , ppc32_exec_SC         , 0xffffffff , 0x44000002, 0 },
   { "slw"     , ppc32_exec_SLW        , 0xfc0007ff , 0x7c000030, 0 },
   { "slw."    , ppc32_exec_SLW_dot    , 0xfc0007ff , 0x7c000031, 0 },
   { "sraw"    , ppc32_exec_SRAW       , 0xfc0007ff , 0x7c000630, 0 },
   { "srawi"   , ppc32_exec_SRAWI      , 0xfc0007ff , 0x7c000670, 0 },
   { "srawi."  , ppc32_exec_SRAWI_dot  , 0xfc0007ff , 0x7c000671, 0 },
   { "srw"     , ppc32_exec_SRW        , 0xfc0007ff , 0x7c000430, 0 },
   { "srw."    , ppc32_exec_SRW_dot    , 0xfc0007ff , 0x7c000431, 0 },
   { "stb"     , ppc32_exec_STB        , 0xfc000000 , 0x98000000, 0 },
   { "stbu"    , ppc32_exec_STBU       , 0xfc000000 , 0x9c000000, 0 },
   { "stbux"   , ppc32_exec_STBUX      , 0xfc0007ff , 0x7c0001ee, 0 },
   { "stbx"    , ppc32_exec_STBX       , 0xfc0007ff , 0x7c0001ae, 0 },
   { "sth"     , ppc32_exec_STH        , 0xfc000000 , 0xb0000000, 0 },
   { "sthu"    , ppc32_exec_STHU       , 0xfc000000 , 0xb4000000, 0 },
   { "sthux"   , ppc32_exec_STHUX      , 0xfc0007ff , 0x7c00036e, 0 },
   { "sthx"    , ppc32_exec_STHX       , 0xfc0007ff , 0x7c00032e, 0 },
   { "stmw"    , ppc32_exec_STMW       , 0xfc000000 , 0xbc000000, 0 },
   { "stw"     , ppc32_exec_STW        , 0xfc000000 , 0x90000000, 0 },
   { "stwu"    , ppc32_exec_STWU       , 0xfc000000 , 0x94000000, 0 },
   { "stwux"   , ppc32_exec_STWUX      , 0xfc0007ff , 0x7c00016e, 0 },
   { "stwx"    , ppc32_exec_STWX       , 0xfc0007ff , 0x7c00012e, 0 },
   { "stwbrx"  , ppc32_exec_STWBRX     , 0xfc0007ff , 0x7c00052c, 0 },
   { "stwcx."  , ppc32_exec_STWCX_dot  , 0xfc0007ff , 0x7c00012d, 0 },
   { "stfd"    , ppc32_exec_STFD       , 0xfc000000 , 0xd8000000, 0 },
   { "stfdu"   , ppc32_exec_STFDU      , 0xfc000000 , 0xdc000000, 0 },
   { "stfdux"  , ppc32_exec_STFDUX     , 0xfc0007ff , 0x7c0005ee, 0 },
   { "stfdx"   , ppc32_exec_STFDX      , 0xfc0007ff , 0x7c0005ae, 0 },
   { "stswi"   , ppc32_exec_STSWI      , 0xfc0007ff , 0x7c0005aa, 0 },
   { "stswx"   , ppc32_exec_STSWX      , 0xfc0007ff , 0x7c00052a, 0 },
   { "subf"    , ppc32_exec_SUBF       , 0xfc0007ff , 0x7c000050, 0 },
   { "subf."   , ppc32_exec_SUBF_dot   , 0xfc0007ff , 0x7c000051, 0 },
   { "subfo"   , ppc32_exec_SUBFO      , 0xfc0007ff , 0x7c000450, 0 },
   { "subfo."  , ppc32_exec_SUBFO_dot  , 0xfc0007ff , 0x7c000451, 0 },
   { "subfc"   , ppc32_exec_SUBFC      , 0xfc0007ff , 0x7c000010, 0 },
   { "subfc."  , ppc32_exec_SUBFC_dot  , 0xfc0007ff , 0x7c000011, 0 },
   //{ "subfco"  , ppc32_exec_SUBFCO     , 0xfc0007ff , 0x7c000410, 0 },
   //{ "subfco." , ppc32_exec_SUBFCO_dot , 0xfc0007ff , 0x7c000411, 0 },
   { "subfe"   , ppc32_exec_SUBFE      , 0xfc0007ff , 0x7c000110, 0 },
   { "subfic"  , ppc32_exec_SUBFIC     , 0xfc000000 , 0x20000000, 0 },
   { "subfze"  , ppc32_exec_SUBFZE     , 0xfc00ffff , 0x7c000190, 0 },
   { "subfze." , ppc32_exec_SUBFZE_dot , 0xfc00ffff , 0x7c000191, 0 },
   { "sync"    , ppc32_exec_SYNC       , 0xffffffff , 0x7c0004ac, 0 },
   { "tlbia"   , ppc32_exec_TLBIA      , 0xffffffff , 0x7c0002e4, 0 },
   { "tlbie"   , ppc32_exec_TLBIE      , 0xffff07ff , 0x7c000264, 0 },
   { "tlbsync" , ppc32_exec_TLBSYNC    , 0xffffffff , 0x7c00046c, 0 },
   { "tw"      , ppc32_exec_TW         , 0xfc0007ff , 0x7c000008, 0 },
   { "twi"     , ppc32_exec_TWI        , 0xfc000000 , 0x0c000000, 0 },
   { "xor"     , ppc32_exec_XOR        , 0xfc0007ff , 0x7c000278, 0 },
   { "xor."    , ppc32_exec_XOR_dot    , 0xfc0007ff , 0x7c000279, 0 },
   { "xori"    , ppc32_exec_XORI       , 0xfc000000 , 0x68000000, 0 },
   { "xoris"   , ppc32_exec_XORIS      , 0xfc000000 , 0x6c000000, 0 },

   /* PowerPC 405 specific instructions */
   { "dccci"   , ppc32_exec_DCCCI      , 0xfc0007ff , 0x7c00038c, 0 },
   { "iccci"   , ppc32_exec_ICCCI      , 0xfc0007ff , 0x7c00078c, 0 },
   { "mfdcr"   , ppc32_exec_MFDCR      , 0xfc0007ff , 0x7c000286, 0 },
   { "mtdcr"   , ppc32_exec_MTDCR      , 0xfc0007ff , 0x7c000386, 0 },
   { "tlbre"   , ppc32_exec_TLBRE      , 0xfc0007ff , 0x7c000764, 0 },
   { "tlbwe"   , ppc32_exec_TLBWE      , 0xfc0007ff , 0x7c0007a4, 0 },

   /* Unknown opcode fallback */
   { "unknown" , ppc32_exec_unknown    , 0x00000000 , 0x00000000, 0 },

   { NULL      , NULL, 0, 0, 0 },
};
