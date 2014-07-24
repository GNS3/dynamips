/*
 * Cisco router simulation platform.
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
#include <fcntl.h>
#include <assert.h>

#include "rbtree.h"
#include "cpu.h"
#include "vm.h"
#include "tcb.h"
#include "mips64_mem.h"
#include "mips64_exec.h"
#include "mips64_jit.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

#include "gdb_proto.h"

/* MIPS general purpose registers names */
char *mips64_gpr_reg_names[MIPS64_GPR_NR] = {
   "zr", "at", "v0", "v1", "a0", "a1", "a2", "a3",
   "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
   "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
   "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra",
};

/* Cacheability and Coherency Attribute */
static int cca_cache_status[8] = {
   1, 1, 0, 1, 0, 1, 0, 0,
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

/* Get cacheability info */
int mips64_cca_cached(m_uint8_t val)
{
   return(cca_cache_status[val & 0x03]);
}

/* Reset a MIPS64 CPU */
int mips64_reset(cpu_mips_t *cpu)
{
   cpu->pc = MIPS_ROM_PC;
   cpu->gpr[MIPS_GPR_SP] = MIPS_ROM_SP;
   cpu->cp0.reg[MIPS_CP0_STATUS] = MIPS_CP0_STATUS_BEV;
   cpu->cp0.reg[MIPS_CP0_CAUSE]  = 0;
   cpu->cp0.reg[MIPS_CP0_CONFIG] = 0x00c08ff0ULL;

   /* Clear the complete TLB */
   memset(&cpu->cp0.tlb,0,MIPS64_TLB_MAX_ENTRIES*sizeof(tlb_entry_t));

   /* Restart the MTS subsystem */
   mips64_set_addr_mode(cpu,32/*64*/);  /* zzz */
   cpu->gen->mts_rebuild(cpu->gen);

   /* Flush JIT structures */
   mips64_jit_flush(cpu,0);
   return(0);
}

/* Initialize a MIPS64 processor */
int mips64_init(cpu_mips_t *cpu)
{
   cpu->addr_bus_mask = 0xFFFFFFFFFFFFFFFFULL;
   cpu->cp0.reg[MIPS_CP0_PRID] = MIPS_PRID_R4600;
   cpu->cp0.tlb_entries = MIPS64_TLB_STD_ENTRIES;

   /* Initialize idle timer */
   cpu->gen->idle_max = 500;
   cpu->gen->idle_sleep_time = 30000;

   /* Timer IRQ parameters (default frequency: 250 Hz <=> 4ms period) */
   cpu->timer_irq_check_itv = 1000;
   cpu->timer_irq_freq      = 250;

   /* Enable fast memory operations */
   cpu->fast_memop = TRUE;

   /* Enable/Disable direct block jump */
   cpu->exec_blk_direct_jump = cpu->vm->exec_blk_direct_jump;

   /* Create the IRQ lock (for non-jit architectures) */
   pthread_mutex_init(&cpu->irq_lock,NULL);

   /* Idle loop mutex and condition */
   pthread_mutex_init(&cpu->gen->idle_mutex,NULL);
   pthread_cond_init(&cpu->gen->idle_cond,NULL);

   /* Set the CPU methods */
   cpu->gen->reg_set =  (void *)mips64_reg_set;
   cpu->gen->reg_dump = (void *)mips64_dump_regs;
   cpu->gen->mmu_dump = (void *)mips64_tlb_dump;
   cpu->gen->mmu_raw_dump = (void *)mips64_tlb_raw_dump;
   cpu->gen->add_breakpoint = (void *)mips64_add_breakpoint;
   cpu->gen->remove_breakpoint = (void *)mips64_remove_breakpoint;
   cpu->gen->set_idle_pc = (void *)mips64_set_idle_pc;
   cpu->gen->get_idling_pc = (void *)mips64_get_idling_pc;

   /* Set the startup parameters */
   mips64_reset(cpu);
   return(0);
}

/* Delete a MIPS64 processor */
void mips64_delete(cpu_mips_t *cpu)
{
   if (cpu) {
      mips64_mem_shutdown(cpu);
      mips64_jit_shutdown(cpu);
   }
}

/* Set the CPU PRID register */
void mips64_set_prid(cpu_mips_t *cpu,m_uint32_t prid)
{
   cpu->cp0.reg[MIPS_CP0_PRID] = prid;

   if ((prid == MIPS_PRID_R7000) || (prid == MIPS_PRID_BCM1250))
      cpu->cp0.tlb_entries = MIPS64_TLB_MAX_ENTRIES;
}

/* Set idle PC value */
void mips64_set_idle_pc(cpu_gen_t *cpu,m_uint64_t addr)
{
   CPU_MIPS64(cpu)->idle_pc = addr;
}

/* Timer IRQ */
void *mips64_timer_irq_run(cpu_mips_t *cpu)
{
   pthread_mutex_t umutex = PTHREAD_MUTEX_INITIALIZER;
   pthread_cond_t ucond = PTHREAD_COND_INITIALIZER;
   struct timespec t_spc;
   m_tmcnt_t expire;
   u_int interval;
   u_int threshold;

   interval = 1000000 / cpu->timer_irq_freq;
   threshold = cpu->timer_irq_freq * 10;
   expire = m_gettime_usec() + interval;

   while(cpu->gen->state != CPU_STATE_HALTED) {
      pthread_mutex_lock(&umutex);
      t_spc.tv_sec = expire / 1000000;
      t_spc.tv_nsec = (expire % 1000000) * 1000;
      pthread_cond_timedwait(&ucond,&umutex,&t_spc);
      pthread_mutex_unlock(&umutex);

      if (likely(!cpu->irq_disable) && 
          likely(cpu->gen->state == CPU_STATE_RUNNING)) 
      {
         cpu->timer_irq_pending++;

         if (unlikely(cpu->timer_irq_pending > threshold)) {
            cpu->timer_irq_pending = 0;
            cpu->timer_drift++;
#if 0
            printf("Timer IRQ not accurate (%u pending IRQ): "
                   "reduce the \"--timer-irq-check-itv\" parameter "
                   "(current value: %u)\n",
                   cpu->timer_irq_pending,cpu->timer_irq_check_itv);
#endif
         }
      }

      expire += interval;
   }

   return NULL;
}

#define IDLE_HASH_SIZE  8192

/* Idle PC hash item */
struct mips64_idle_pc_hash {
   m_uint64_t pc;
   u_int count;
   struct mips64_idle_pc_hash *next;
};

/* Determine an "idling" PC */
int mips64_get_idling_pc(cpu_gen_t *cpu)
{
   cpu_mips_t *mcpu = CPU_MIPS64(cpu);
   struct mips64_idle_pc_hash **pc_hash,*p;
   struct cpu_idle_pc *res;
   u_int h_index;
   m_uint64_t cur_pc;
   int i;

   cpu->idle_pc_prop_count = 0;

   if (mcpu->idle_pc != 0) {
      printf("\nYou already use an idle PC, using the calibration would give "
             "incorrect results.\n");
      return(-1);
   }

   printf("\nPlease wait while gathering statistics...\n");

   pc_hash = calloc(IDLE_HASH_SIZE,sizeof(struct mips64_idle_pc_hash *));

   /* Disable IRQ */
   mcpu->irq_disable = TRUE;

   /* Take 1000 measures, each mesure every 10ms */
   for(i=0;i<1000;i++) {
      cur_pc = mcpu->pc;
      h_index = (cur_pc >> 2) & (IDLE_HASH_SIZE-1);

      for(p=pc_hash[h_index];p;p=p->next)
         if (p->pc == cur_pc) {
            p->count++;
            break;
         }

      if (!p) {
         if ((p = malloc(sizeof(*p)))) {
            p->pc    = cur_pc;
            p->count = 1;
            p->next  = pc_hash[h_index];
            pc_hash[h_index] = p;
         }
      }

      usleep(10000);
   }

   /* Select PCs */
   for(i=0;i<IDLE_HASH_SIZE;i++) {
      for(p=pc_hash[i];p;p=p->next)
         if ((p->count >= 70) && (p->count <= 180)) {
            res = &cpu->idle_pc_prop[cpu->idle_pc_prop_count++];

            res->pc    = p->pc;
            res->count = p->count;

            if (cpu->idle_pc_prop_count >= CPU_IDLE_PC_MAX_RES)
               goto done;
         }
   }

 done:
   /* Set idle PC */
   if (cpu->idle_pc_prop_count) {
      printf("Done. Suggested idling PC:\n");

      for(i=0;i<cpu->idle_pc_prop_count;i++) {
         printf("   0x%llx (count=%u)\n",
                cpu->idle_pc_prop[i].pc,
                cpu->idle_pc_prop[i].count);
      }         

      printf("Restart the emulator with \"--idle-pc=0x%llx\" (for example)\n",
             cpu->idle_pc_prop[0].pc);
   } else {
      printf("Done. No suggestion for idling PC\n");

      for(i=0;i<IDLE_HASH_SIZE;i++)
         for(p=pc_hash[i];p;p=p->next) {
            printf("  0x%16.16llx (%3u)\n",p->pc,p->count);

            if (cpu->idle_pc_prop_count < CPU_IDLE_PC_MAX_RES) {
               res = &cpu->idle_pc_prop[cpu->idle_pc_prop_count++];

               res->pc    = p->pc;
               res->count = p->count;
            }
         }
       
      printf("\n");
   }

   /* Re-enable IRQ */
   mcpu->irq_disable = FALSE;
   return(0);
}

/* Set an IRQ (VM IRQ standard routing) */
void mips64_vm_set_irq(vm_instance_t *vm,u_int irq)
{
   cpu_mips_t *boot_cpu;

   boot_cpu = CPU_MIPS64(vm->boot_cpu);

   if (boot_cpu->irq_disable) {
      boot_cpu->irq_pending = 0;
      return;
   }

   mips64_set_irq(boot_cpu,irq);

   if (boot_cpu->irq_idle_preempt[irq])
      cpu_idle_break_wait(vm->boot_cpu);
}

/* Clear an IRQ (VM IRQ standard routing) */
void mips64_vm_clear_irq(vm_instance_t *vm,u_int irq)
{
   cpu_mips_t *boot_cpu;

   boot_cpu = CPU_MIPS64(vm->boot_cpu);
   mips64_clear_irq(boot_cpu,irq);
}

/* Update the IRQ flag (inline) */
static forced_inline int mips64_update_irq_flag_fast(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint32_t imask,sreg_mask;
   m_uint32_t cause;

   cpu->irq_pending = FALSE;

   cause = cp0->reg[MIPS_CP0_CAUSE] & ~MIPS_CP0_CAUSE_IMASK;
   cp0->reg[MIPS_CP0_CAUSE] = cause | cpu->irq_cause;

   sreg_mask = MIPS_CP0_STATUS_IE|MIPS_CP0_STATUS_EXL|MIPS_CP0_STATUS_ERL;

   if ((cp0->reg[MIPS_CP0_STATUS] & sreg_mask) == MIPS_CP0_STATUS_IE) {
      imask = cp0->reg[MIPS_CP0_STATUS] & MIPS_CP0_STATUS_IMASK;
      if (unlikely(cp0->reg[MIPS_CP0_CAUSE] & imask)) {
         cpu->irq_pending = TRUE;
         return(TRUE);
      }
   }

   return(FALSE);
}

/* Update the IRQ flag */
void mips64_update_irq_flag(cpu_mips_t *cpu)
{
   mips64_update_irq_flag_fast(cpu);
}

/* Generate a general exception */
void mips64_general_exception(cpu_mips_t *cpu,u_int exc_code)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint64_t cause;

   /* First check if a GDB session is present so it can handle the exception */
   switch (exc_code)
   {
       case MIPS_CP0_CAUSE_ILLOP:
       case MIPS_CP0_CAUSE_TLB_LOAD:
       case MIPS_CP0_CAUSE_TLB_SAVE:
       case MIPS_CP0_CAUSE_ADDR_LOAD:
       case MIPS_CP0_CAUSE_ADDR_SAVE:
       case MIPS_CP0_CAUSE_TRAP:
       case MIPS_CP0_CAUSE_BP:
           if ((cpu->vm->gdb_server_running == TRUE))// && (mips64_is_breakpoint_at_pc(cpu)))
           {
              cpu->vm->gdb_ctx->signal = GDB_SIGINT;
              vm_suspend(cpu->vm);
              return;
           }
   }

   /* Update cause register (set BD and ExcCode) */
   cause = cp0->reg[MIPS_CP0_CAUSE] & MIPS_CP0_CAUSE_IMASK;

   if (cpu->bd_slot)
      cause |= MIPS_CP0_CAUSE_BD_SLOT;
   else
      cause &= ~MIPS_CP0_CAUSE_BD_SLOT;

   cause |= (exc_code << MIPS_CP0_CAUSE_SHIFT);
   cp0->reg[MIPS_CP0_CAUSE] = cause;

   /* If EXL bit is 0, set EPC and BadVaddr registers */
   if (likely(!(cp0->reg[MIPS_CP0_STATUS] & MIPS_CP0_STATUS_EXL))) {
      cp0->reg[MIPS_CP0_EPC] = cpu->pc - (cpu->bd_slot << 2);
   }
   
   /* Set EXL bit in status register */
   cp0->reg[MIPS_CP0_STATUS] |= MIPS_CP0_STATUS_EXL;

   /* Use bootstrap vectors ? */
   if (cp0->reg[MIPS_CP0_STATUS] & MIPS_CP0_STATUS_BEV)
      cpu->pc = 0xffffffffbfc00200ULL + 0x180;
   else
      cpu->pc = 0xffffffff80000000ULL + 0x180;

   /* Clear the pending IRQ flag */
   cpu->irq_pending = 0;
}

/* Generate a general exception that updates BadVaddr */
void mips64_gen_exception_badva(cpu_mips_t *cpu,u_int exc_code,
                                m_uint64_t bad_vaddr)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint64_t cause;
   
   /* Update cause register (set BD and ExcCode) */
   cause = cp0->reg[MIPS_CP0_CAUSE] & MIPS_CP0_CAUSE_IMASK;

   if (cpu->bd_slot)
      cause |= MIPS_CP0_CAUSE_BD_SLOT;
   else
      cause &= ~MIPS_CP0_CAUSE_BD_SLOT;

   cause |= (exc_code << MIPS_CP0_CAUSE_SHIFT);
   cp0->reg[MIPS_CP0_CAUSE] = cause;

   /* If EXL bit is 0, set EPC and BadVaddr registers */
   if (likely(!(cp0->reg[MIPS_CP0_STATUS] & MIPS_CP0_STATUS_EXL))) {
      cp0->reg[MIPS_CP0_EPC] = cpu->pc - (cpu->bd_slot << 2);
      cp0->reg[MIPS_CP0_BADVADDR] = bad_vaddr;
   }
   
   /* Set EXL bit in status register */
   cp0->reg[MIPS_CP0_STATUS] |= MIPS_CP0_STATUS_EXL;

   /* Use bootstrap vectors ? */
   if (cp0->reg[MIPS_CP0_STATUS] & MIPS_CP0_STATUS_BEV)
      cpu->pc = 0xffffffffbfc00200ULL + 0x180;
   else
      cpu->pc = 0xffffffff80000000ULL + 0x180;

   /* Clear the pending IRQ flag */
   cpu->irq_pending = 0;
}

/* Generate a TLB/XTLB miss exception */
void mips64_tlb_miss_exception(cpu_mips_t *cpu,u_int exc_code,
                               m_uint64_t bad_vaddr)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint64_t cause,vector;
   
   /* Update cause register (set BD and ExcCode) */
   cause = cp0->reg[MIPS_CP0_CAUSE] & MIPS_CP0_CAUSE_IMASK;

   if (cpu->bd_slot)
      cause |= MIPS_CP0_CAUSE_BD_SLOT;
   else
      cause &= ~MIPS_CP0_CAUSE_BD_SLOT;

   cause |= (exc_code << MIPS_CP0_CAUSE_SHIFT);
   cp0->reg[MIPS_CP0_CAUSE] = cause;

   /* If EXL bit is 0, set EPC and BadVaddr registers */
   if (likely(!(cp0->reg[MIPS_CP0_STATUS] & MIPS_CP0_STATUS_EXL))) {
      cp0->reg[MIPS_CP0_EPC] = cpu->pc - (cpu->bd_slot << 2);
      cp0->reg[MIPS_CP0_BADVADDR] = bad_vaddr;
      
      /* 
       * determine if TLB or XTLB exception, based on the current
       * addressing mode.
       */
      if (cpu->addr_mode == 64) {
         vector = 0x080;
      } else {
         vector = 0x000;
      }
   } else {
      /* nested: handled as a general exception */
      vector = 0x180;
   }

   /* Set EXL bit in status register */
   cp0->reg[MIPS_CP0_STATUS] |= MIPS_CP0_STATUS_EXL;

   /* Use bootstrap vectors ? */
   if (cp0->reg[MIPS_CP0_STATUS] & MIPS_CP0_STATUS_BEV)
      cpu->pc = 0xffffffffbfc00200ULL + vector;
   else
      cpu->pc = 0xffffffff80000000ULL + vector;

   /* Clear the pending IRQ flag */
   cpu->irq_pending = 0;
}

/* Prepare a TLB exception */
void mips64_prepare_tlb_exception(cpu_mips_t *cpu,m_uint64_t vaddr)
{
   m_uint64_t vpn2,mask;

   /* Update CP0 context and xcontext registers */
   mips64_cp0_update_context_reg(cpu,vaddr);
   mips64_cp0_update_xcontext_reg(cpu,vaddr);
            
   /* EntryHi also contains the VPN address */
   mask = mips64_cp0_get_vpn2_mask(cpu);
   vpn2 = vaddr & mask;
   cpu->cp0.reg[MIPS_CP0_TLB_HI] &= ~mask;
   cpu->cp0.reg[MIPS_CP0_TLB_HI] |= vpn2;
}

/*
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
fastcall void mips64_exec_inc_cp0_cnt(cpu_mips_t *cpu)
{
   cpu->cp0_virt_cnt_reg++;

#if 0 /* TIMER_IRQ */
   mips_cp0_t *cp0 = &cpu->cp0;

   if (unlikely((cpu->cp0_virt_cnt_reg == cpu->cp0_virt_cmp_reg))) {
      cp0->reg[MIPS_CP0_COUNT] = (m_uint32_t)cp0->reg[MIPS_CP0_COMPARE];
      mips64_set_irq(cpu,7);
      mips64_update_irq_flag_fast(cpu);
   }
#endif
}

/* Trigger the Timer IRQ */
fastcall void mips64_trigger_timer_irq(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;

   cpu->timer_irq_count++;

   cp0->reg[MIPS_CP0_COUNT] = (m_uint32_t)cp0->reg[MIPS_CP0_COMPARE];
   mips64_set_irq(cpu,7);
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
   mips64_general_exception(cpu,MIPS_CP0_CAUSE_SYSCALL);
}

/* Execute BREAK instruction */
fastcall void mips64_exec_break(cpu_mips_t *cpu,u_int code)
{
   cpu_log(cpu->gen,"MIPS64","BREAK instruction (code=%u)\n",code);
   mips64_general_exception(cpu,MIPS_CP0_CAUSE_BP);
}

/* Trigger a Trap Exception */
fastcall void mips64_trigger_trap_exception(cpu_mips_t *cpu)
{  
   cpu_log(cpu->gen,"MIPS64","TRAP exception\n");
   mips64_general_exception(cpu,MIPS_CP0_CAUSE_TRAP);
}

/* Trigger IRQs */
fastcall void mips64_trigger_irq(cpu_mips_t *cpu)
{
   if (unlikely(cpu->irq_disable)) {
      cpu->irq_pending = 0;
      return;
   }

   cpu->irq_count++;
   if (mips64_update_irq_flag_fast(cpu))
      mips64_general_exception(cpu,MIPS_CP0_CAUSE_INTERRUPT);
   else
      cpu->irq_fp_count++;
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
   cpu_log(cpu->gen,"BREAKPOINT",
           "Virtual breakpoint reached at PC=0x%llx\n",cpu->pc);

   printf("[[[ Virtual Breakpoint reached at PC=0x%llx RA=0x%llx]]]\n",
          cpu->pc,cpu->gpr[MIPS_GPR_RA]);

   mips64_dump_regs(cpu->gen);
   memlog_dump(cpu->gen);
}

/* Check if cufrrent PC has a breakpoint set */
int mips64_is_breakpoint_at_pc(cpu_mips_t *cpu)
{
   m_uint64_t pc = cpu->pc;
   int i;

   for(i=0; i < MIPS64_MAX_BREAKPOINTS; i++)
   {
      if (pc == cpu->breakpoints[i]) {
         return TRUE;
      }
   }
   return FALSE;
}

/* Add a virtual breakpoint */
int mips64_add_breakpoint(cpu_gen_t *cpu,m_uint64_t pc)
{
   cpu_mips_t *mcpu = CPU_MIPS64(cpu);
   int i;

   for(i=0;i<MIPS64_MAX_BREAKPOINTS;i++)
      if (!mcpu->breakpoints[i])
         break;

   if (i == MIPS64_MAX_BREAKPOINTS)
      return(-1);

   mcpu->breakpoints[i] = pc;
   mcpu->breakpoints_enabled = TRUE;
   return(0);
}

/* Remove a virtual breakpoint */
void mips64_remove_breakpoint(cpu_gen_t *cpu,m_uint64_t pc)
{   
   cpu_mips_t *mcpu = CPU_MIPS64(cpu);
   int i,j;

   for(i=0;i<MIPS64_MAX_BREAKPOINTS;i++)
      if (mcpu->breakpoints[i] == pc)
      {
         for(j=i;j<MIPS64_MAX_BREAKPOINTS-1;j++)
            mcpu->breakpoints[j] = mcpu->breakpoints[j+1];

         mcpu->breakpoints[MIPS64_MAX_BREAKPOINTS-1] = 0;
      }

   for(i=0;i<MIPS64_MAX_BREAKPOINTS;i++)
      if (mcpu->breakpoints[i] != 0)
         return;

   mcpu->breakpoints_enabled = FALSE;
}

/* Debugging for register-jump to address 0 */
fastcall void mips64_debug_jr0(cpu_mips_t *cpu)
{
   printf("MIPS64: cpu %p jumping to address 0...\n",cpu);
   mips64_dump_regs(cpu->gen);
}

/* Set a register */
void mips64_reg_set(cpu_gen_t *cpu,u_int reg,m_uint64_t val)
{
   if (reg < MIPS64_GPR_NR)
      CPU_MIPS64(cpu)->gpr[reg] = val;
}

/* Dump registers of a MIPS64 processor */
void mips64_dump_regs(cpu_gen_t *cpu)
{ 
   cpu_mips_t *mcpu = CPU_MIPS64(cpu);
   mips_insn_t *ptr,insn;
   char buffer[80];
   int i;

   printf("MIPS64 Registers:\n");

   for(i=0;i<MIPS64_GPR_NR/2;i++) {
      printf("  %s ($%2d) = 0x%16.16llx   %s ($%2d) = 0x%16.16llx\n",
             mips64_gpr_reg_names[i*2], i*2, mcpu->gpr[i*2],
             mips64_gpr_reg_names[(i*2)+1], (i*2)+1, mcpu->gpr[(i*2)+1]);
   }

   printf("  lo = 0x%16.16llx, hi = 0x%16.16llx\n", mcpu->lo, mcpu->hi);
   printf("  pc = 0x%16.16llx, ll_bit = %u\n", mcpu->pc, mcpu->ll_bit);

   /* Fetch the current instruction */ 
   ptr = mcpu->mem_op_lookup(mcpu,mcpu->pc);
   if (ptr) {
      insn = vmtoh32(*ptr);

      if (mips64_dump_insn(buffer,sizeof(buffer),1,mcpu->pc,insn) != -1)
         printf("  Instruction: %s\n",buffer);
   }

   printf("\nCP0 Registers:\n");

   for(i=0;i<MIPS64_CP0_REG_NR/2;i++) {
      printf("  %-10s ($%2d) = 0x%16.16llx   %-10s ($%2d) = 0x%16.16llx\n",
             mips64_cp0_reg_names[i*2], i*2, 
             mips64_cp0_get_reg(mcpu,i*2),
             mips64_cp0_reg_names[(i*2)+1], (i*2)+1,
             mips64_cp0_get_reg(mcpu,(i*2)+1));
   }

   printf("\n  IRQ count: %llu, IRQ false positives: %llu, "
          "IRQ Pending: %u\n",
          mcpu->irq_count,mcpu->irq_fp_count,mcpu->irq_pending);

   printf("  Timer IRQ count: %llu, pending: %u, timer drift: %u\n\n",
          mcpu->timer_irq_count,mcpu->timer_irq_pending,mcpu->timer_drift);

   printf("  Device access count: %llu\n",cpu->dev_access_counter);
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
   for(i=0;i<cpu->cp0.tlb_entries;i++) {
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
      if ((index = mips64_cp0_get_reg_index(buffer)) != -1) {
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

   mips64_dump_regs(cpu->gen);
   mips64_tlb_dump(cpu->gen);

   fclose(fd);
   return(0);
}

/* Load a raw image into the simulated memory */
int mips64_load_raw_image(cpu_mips_t *cpu,char *filename,m_uint64_t vaddr)
{   
   struct stat file_info;
   size_t len,clen;
   m_uint32_t remain;
   void *haddr;
   FILE *bfd;

   if (!(bfd = fopen(filename,"r"))) {
      perror("fopen");
      return(-1);
   }

   if (fstat(fileno(bfd),&file_info) == -1) {
      perror("stat");
      return(-1);
   }

   len = file_info.st_size;

   printf("Loading RAW file '%s' at virtual address 0x%llx (size=%lu)\n",
          filename,vaddr,(u_long)len);

   while(len > 0)
   {
      haddr = cpu->mem_op_lookup(cpu,vaddr);
   
      if (!haddr) {
         fprintf(stderr,"load_raw_image: invalid load address 0x%llx\n",
                 vaddr);
         return(-1);
      }

      if (len > MIPS_MIN_PAGE_SIZE)
         clen = MIPS_MIN_PAGE_SIZE;
      else
         clen = len;

      remain = MIPS_MIN_PAGE_SIZE;
      remain -= (vaddr - (vaddr & MIPS_MIN_PAGE_MASK));
      
      clen = m_min(clen,remain);

      if (fread((u_char *)haddr,clen,1,bfd) != 1)
         break;
      
      vaddr += clen;
      len -= clen;
   }
   
   fclose(bfd);
   return(0);
}

/* Load an ELF image into the simulated memory */
int mips64_load_elf_image(cpu_mips_t *cpu,char *filename,int skip_load,
                          m_uint32_t *entry_point)
{
   m_uint64_t vaddr;
   m_uint32_t remain;
   void *haddr;
   Elf32_Ehdr *ehdr;
   Elf32_Shdr *shdr;
   Elf_Scn *scn;
   Elf *img_elf;
   size_t len,clen;
   _maybe_used char *name;
   int i,fd;
   FILE *bfd;

   if (!filename)
      return(-1);

#ifdef __CYGWIN__
   fd = open(filename,O_RDONLY|O_BINARY);
#else
   fd = open(filename,O_RDONLY);
#endif

   if (fd == -1) {
      perror("load_elf_image: open");
      return(-1);
   }

   if (elf_version(EV_CURRENT) == EV_NONE) {
      fprintf(stderr,"load_elf_image: library out of date\n");
      return(-1);
   }

   if (!(img_elf = elf_begin(fd,ELF_C_READ,NULL))) {
      fprintf(stderr,"load_elf_image: elf_begin: %s\n",
              elf_errmsg(elf_errno()));
      return(-1);
   }

   if (!(ehdr = elf32_getehdr(img_elf))) {
      fprintf(stderr,"load_elf_image: invalid ELF file\n");
      return(-1);
   }

   printf("Loading ELF file '%s'...\n",filename);
   bfd = fdopen(fd,"rb");

   if (!bfd) {
      perror("load_elf_image: fdopen");
      return(-1);
   }

   if (!skip_load) {
      for(i=0;i<ehdr->e_shnum;i++) {
         scn = elf_getscn(img_elf,i);

         shdr = elf32_getshdr(scn);
         name = elf_strptr(img_elf, ehdr->e_shstrndx, (size_t)shdr->sh_name);
         len  = shdr->sh_size;

         if (!(shdr->sh_flags & SHF_ALLOC) || !len)
            continue;

         fseek(bfd,shdr->sh_offset,SEEK_SET);
         vaddr = sign_extend(shdr->sh_addr,32);

         if (cpu->vm->debug_level > 0) {
            printf("   * Adding section at virtual address 0x%8.8llx "
                   "(len=0x%8.8lx)\n",vaddr & 0xFFFFFFFF,(u_long)len);
         }
         
         while(len > 0)
         {
            haddr = cpu->mem_op_lookup(cpu,vaddr);
   
            if (!haddr) {
               fprintf(stderr,"load_elf_image: invalid load address 0x%llx\n",
                       vaddr);
               return(-1);
            }

            if (len > MIPS_MIN_PAGE_SIZE)
               clen = MIPS_MIN_PAGE_SIZE;
            else
               clen = len;

            remain = PPC32_MIN_PAGE_SIZE;
            remain -= (vaddr - (vaddr & PPC32_MIN_PAGE_MASK));

            clen = m_min(clen,remain);

            if (fread((u_char *)haddr,clen,1,bfd) < 1)
               break;

            vaddr += clen;
            len -= clen;
         }
      }
   } else {
      printf("ELF loading skipped, using a ghost RAM file.\n");
   }

   printf("ELF entry point: 0x%x\n",ehdr->e_entry);

   if (entry_point)
      *entry_point = ehdr->e_entry;

   elf_end(img_elf);
   fclose(bfd);
   return(0);
}

/* Symbol lookup */
struct symbol *mips64_sym_lookup(cpu_mips_t *cpu,m_uint64_t addr)
{
   return(rbtree_lookup(cpu->sym_tree,&addr));
}

/* Insert a new symbol */
struct symbol *mips64_sym_insert(cpu_mips_t *cpu,char *name,m_uint64_t addr)
{
   struct symbol *sym;
   size_t len;

   if (!cpu->sym_tree)
      return NULL;

   len = strlen(name);

   if (!(sym = malloc(len+1+sizeof(*sym))))
      return NULL;
   
   memcpy(sym->name,name,len+1);
   sym->addr = addr;

   if (rbtree_insert(cpu->sym_tree,sym,sym) == -1) {
      free(sym);
      return NULL;
   }

   return sym;
}

/* Symbol comparison function */
static int mips64_sym_compare(m_uint64_t *a1,struct symbol *sym)
{
   if (*a1 > sym->addr)
      return(1);

   if (*a1 < sym->addr)
      return(-1);

   return(0);
}

/* Create the symbol tree */
int mips64_sym_create_tree(cpu_mips_t *cpu)
{
   cpu->sym_tree = rbtree_create((tree_fcompare)mips64_sym_compare,NULL);
   return(cpu->sym_tree ? 0 : -1);
}

/* Load a symbol file */
int mips64_sym_load_file(cpu_mips_t *cpu,char *filename)
{
   char buffer[4096],func_name[128];
   m_uint64_t addr;
   char sym_type;
   FILE *fd;

   if (!cpu->sym_tree && (mips64_sym_create_tree(cpu) == -1)) {
      fprintf(stderr,"CPU%u: Unable to create symbol tree.\n",cpu->gen->id);
      return(-1);
   }

   if (!(fd = fopen(filename,"r"))) {
      perror("load_sym_file: fopen");
      return(-1);
   }

   while(!feof(fd)) {
      fgets(buffer,sizeof(buffer),fd);

      if (sscanf(buffer,"%llx %c %s",&addr,&sym_type,func_name) == 3) {
         mips64_sym_insert(cpu,func_name,addr);
      }
   }

   fclose(fd);
   return(0);
}
