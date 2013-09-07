/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * MIPS64 JIT compiler.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>

#include "sbox.h"
#include "cpu.h"
#include "device.h"
#include "tcb.h"
#include "mips64.h"
#include "mips64_cp0.h"
#include "mips64_exec.h"
#include "mips64_jit.h"
#include "insn_lookup.h"
#include "memory.h"
#include "ptask.h"

#include MIPS64_ARCH_INC_FILE

#define DEBUG_JIT_SHARED  0

#if DEBUG_BLOCK_TIMESTAMP
static volatile m_uint64_t jit_jiffies = 0;
#endif

/* MIPS jump instructions for block scan */
struct mips64_insn_jump mips64_insn_jumps[] = {
   { "b"       , 0xffff0000, 0x10000000, 16, 1 },
   { "bal"     , 0xffff0000, 0x04110000, 16, 1 },
   { "beq"     , 0xfc000000, 0x10000000, 16, 1 },
   { "beql"    , 0xfc000000, 0x50000000, 16, 1 },
   { "bgez"    , 0xfc1f0000, 0x04010000, 16, 1 },
   { "bgezl"   , 0xfc1f0000, 0x04030000, 16, 1 },
   { "bgezal"  , 0xfc1f0000, 0x04110000, 16, 1 },
   { "bgezall" , 0xfc1f0000, 0x04130000, 16, 1 },
   { "bgtz"    , 0xfc1f0000, 0x1c000000, 16, 1 },
   { "bgtzl"   , 0xfc1f0000, 0x5c000000, 16, 1 },
   { "blez"    , 0xfc1f0000, 0x18000000, 16, 1 },
   { "blezl"   , 0xfc1f0000, 0x58000000, 16, 1 },
   { "bltz"    , 0xfc1f0000, 0x04000000, 16, 1 },
   { "bltzl"   , 0xfc1f0000, 0x04020000, 16, 1 },
   { "bltzal"  , 0xfc1f0000, 0x04100000, 16, 1 },
   { "bltzall" , 0xfc1f0000, 0x04120000, 16, 1 },
   { "bne"     , 0xfc000000, 0x14000000, 16, 1 },
   { "bnel"    , 0xfc000000, 0x54000000, 16, 1 },
   { "j"       , 0xfc000000, 0x08000000, 26, 0 },
   { NULL      , 0x00000000, 0x00000000,  0, 0 },
};

/* Instruction Lookup Table */
static insn_lookup_t *ilt = NULL;

static void *mips64_jit_get_insn(int index)
{
   return(&mips64_insn_tags[index]);
}

static int mips64_jit_chk_lo(struct mips64_insn_tag *tag,int value)
{
   return((value & tag->mask) == (tag->value & 0xFFFF));
}

static int mips64_jit_chk_hi(struct mips64_insn_tag *tag,int value)
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
void mips64_jit_create_ilt(void)
{
   int i,count;

   for(i=0,count=0;mips64_insn_tags[i].emit;i++)
      count++;

   ilt = ilt_create("mips64j",count,
                    (ilt_get_insn_cbk_t)mips64_jit_get_insn,
                    (ilt_check_cbk_t)mips64_jit_chk_lo,
                    (ilt_check_cbk_t)mips64_jit_chk_hi);

   atexit(destroy_ilt);
}

/* Initialize the JIT structure */
int mips64_jit_init(cpu_mips_t *cpu)
{
   if (tsg_bind_cpu(cpu->gen) == -1)
      return(-1);
   
   return(cpu_jit_init(cpu->gen,
                       MIPS_JIT_VIRT_HASH_SIZE,
                       MIPS_JIT_PHYS_HASH_SIZE));
}

/* Flush the JIT */
u_int mips64_jit_flush(cpu_mips_t *cpu,u_int threshold)
{
   /* TO FIX / ENHANCE */
   return(tsg_remove_single_desc(cpu->gen));
}

/* Shutdown the JIT */
void mips64_jit_shutdown(cpu_mips_t *cpu)
{
   cpu_jit_shutdown(cpu->gen);
}

/* Find the JIT code emitter for the specified MIPS instruction */
static struct mips64_insn_tag *insn_tag_find(mips_insn_t ins)
{
   struct mips64_insn_tag *tag = NULL;
   int index;

   index = ilt_lookup(ilt,ins);
   tag = mips64_jit_get_insn(index);
   return tag;
}

/* Check if the specified MIPS instruction is a jump */
__unused static struct mips64_insn_jump *insn_jump_find(mips_insn_t ins)
{
   struct mips64_insn_jump *jump = NULL;
   int i;

   for(i=0;mips64_insn_jumps[i].name;i++)
      if ((ins & mips64_insn_jumps[i].mask) == mips64_insn_jumps[i].value) {
         jump = &mips64_insn_jumps[i];
         break;
      }

   return(jump);
}

/* Fetch a MIPS instruction */
static forced_inline mips_insn_t insn_fetch(cpu_tc_t *tc)
{
   return(vmtoh32(((mips_insn_t *)tc->target_code)[tc->trans_pos]));
}

/* Emit a breakpoint if necessary */
#if BREAKPOINT_ENABLE
static void insn_emit_breakpoint(cpu_mips_t *cpu,cpu_tc_t *tc)
{
   m_uint64_t pc;
   int i;

   pc = tc->vaddr + ((tc->trans_pos-1) << 2);

   for(i=0;i<MIPS64_MAX_BREAKPOINTS;i++)
      if (pc == cpu->breakpoints[i]) {
         mips64_emit_breakpoint(tc);
         break;
      }
}
#endif /* BREAKPOINT_ENABLE */

/* Check if an instruction is in a delay slot or not */
int mips64_jit_is_delay_slot(cpu_tc_t *tc,m_uint64_t pc)
{   
   struct mips64_insn_tag *tag;
   m_uint32_t offset,insn;

   offset = (pc - tc->vaddr) >> 2;

   if (!offset)
      return(FALSE);

   /* Fetch the previous instruction to determine if it is a jump */
   insn = vmtoh32(((m_uint32_t *)tc->target_code)[offset-1]);
   tag = insn_tag_find(insn);
   assert(tag != NULL);
   return(!tag->delay_slot);
}

/* Fetch a MIPS instruction and emit corresponding translated code */
struct mips64_insn_tag *mips64_jit_fetch_and_emit(cpu_mips_t *cpu,
                                                  cpu_tc_t *tc,
                                                  int delay_slot)
{
   struct mips64_insn_tag *tag;
   mips_insn_t code;

   code = insn_fetch(tc);
   tag = insn_tag_find(code);
   assert(tag);

   /* Branch-delay slot is in another page: slow exec */
   if ((tc->trans_pos == (MIPS_INSN_PER_PAGE-1)) && !tag->delay_slot) {
      tc->jit_insn_ptr[tc->trans_pos] = tc->jit_ptr;

      mips64_set_pc(tc,tc->vaddr + (tc->trans_pos << 2));
      mips64_emit_single_step(tc,code); 
      mips64_jit_tcb_push_epilog(tc);
      tc->trans_pos++;
      return tag;
   }

   if (delay_slot && !tag->delay_slot) {
      mips64_emit_invalid_delay_slot(tc);
      return NULL;
   }

   if (!delay_slot)
      tc->jit_insn_ptr[tc->trans_pos] = tc->jit_ptr;

   if (delay_slot != 2)
      tc->trans_pos++;

#if DEBUG_INSN_PERF_CNT
   mips64_inc_perf_counter(tc);
#endif

   if (!delay_slot) {
      /* Check for IRQs + Increment count register before jumps */
      if (!tag->delay_slot) {
         mips64_inc_cp0_count_reg(tc);
         mips64_check_pending_irq(tc);
      }
   }

#if BREAKPOINT_ENABLE
   if (cpu->breakpoints_enabled)
      insn_emit_breakpoint(cpu,tc);
#endif

   tag->emit(cpu,tc,code);
   return tag;
}

/* Add end of JIT block */
static void mips64_jit_tcb_add_end(cpu_tc_t *tc)
{
   mips64_set_pc(tc,tc->vaddr+(tc->trans_pos<<2));
   mips64_jit_tcb_push_epilog(tc);
}

/* Record a patch to apply in a compiled block */
int mips64_jit_tcb_record_patch(cpu_mips_t *cpu,cpu_tc_t *tc,
                                u_char *jit_ptr,m_uint64_t vaddr)
{
   struct insn_patch *patch;
   
   patch = tc_record_patch(cpu->gen,tc,jit_ptr,vaddr);
   return((patch != NULL) ? 0 : -1);
}

/* Apply all patches */
static int mips64_jit_tcb_apply_patches(cpu_mips_t *cpu,cpu_tc_t *tc)
{
   tc_apply_patches(tc,mips64_jit_tcb_set_patch);
   return(0);
}

/* Adjust the JIT buffer if its size is not sufficient */
static int mips64_jit_tcb_adjust_buffer(cpu_mips_t *cpu,cpu_tc_t *tc)
{
   return(tc_adjust_jit_buffer(cpu->gen,tc,mips64_jit_tcb_set_jump));
}

/* Produce translated code for a page. If this fails, use non-compiled mode */
static cpu_tc_t *mips64_jit_tcb_translate(cpu_mips_t *cpu,cpu_tb_t *tb)
{
   struct mips64_insn_tag *tag;
   cpu_tc_t *tc;
   
   /* The page is not shared, we have to compile it */
   tc = tc_alloc(cpu->gen,tb->vaddr,tb->exec_state);
   
   if (tc == NULL)
      return NULL;
   
   tc->target_code = tb->target_code;
   tc->trans_pos   = 0;
   
   /* Emit native code for each instruction */
   while(tc->trans_pos < MIPS_INSN_PER_PAGE)
   {
      if (unlikely(!(tag = mips64_jit_fetch_and_emit(cpu,tc,0)))) {
         cpu_log(cpu->gen,"JIT",
                 "unable to fetch instruction (VA=0x%8.8llx,exec_state=%u).\n",
                 tb->vaddr,tb->exec_state);
         return NULL;
      }

#if DEBUG_BLOCK_COMPILE
      cpu_log(cpu->gen,"JIT","Page 0x%8.8llx: emitted tag 0x%8.8x/0x%8.8x\n",
             tb->vaddr,tag->mask,tag->value);
#endif

      if (mips64_jit_tcb_adjust_buffer(cpu,tc) == -1)
         return NULL;
   }

   mips64_jit_tcb_add_end(tc);
   mips64_jit_tcb_apply_patches(cpu,tc);
   tc_free_patches(tc);
   tc->target_code = NULL;
   return tc;
}

/* Compile a MIPS instruction page */
static cpu_tb_t *
mips64_jit_tcb_compile(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint32_t exec_state)
{
   cpu_tb_t *tb;
   cpu_tc_t *tc;
   m_uint64_t page_addr;
   mips_insn_t *mips_code;
   m_uint32_t phys_page;

   page_addr = vaddr & MIPS_MIN_PAGE_MASK;

   /* 
    * Get the mips code address from the host point of view.
    * If there is an error (TLB,...), we return directly to the main loop.
    */
   mips_code = cpu->mem_op_ifetch(cpu,page_addr);

   if (unlikely(cpu->translate(cpu,page_addr,&phys_page)))
      return NULL;

   /* Create a new translation block */
   if (!(tb = tb_alloc(cpu->gen,page_addr,exec_state)))
      return NULL;
      
   tb->vaddr       = page_addr;
   tb->exec_state  = exec_state;
   tb->phys_page   = phys_page;
   tb->phys_hash   = mips64_jit_get_phys_hash(phys_page);   
   tb->virt_hash   = mips64_jit_get_virt_hash(page_addr);
   tb->target_code = mips_code;
   tb->checksum    = tsg_checksum_page(tb->target_code,VM_PAGE_SIZE);

   /* Check if we can share this page with another virtual CPU */
   if (tc_find_shared(cpu->gen,tb) == TSG_LOOKUP_SHARED) {
#if DEBUG_JIT_SHARED
      cpu_log(cpu->gen,"JIT","Page 0x%8.8llx is shared (ref_count=%u)\n",
              tb->vaddr,tb->tc->ref_count);
#endif
      return tb;
   }

   /* The page is not shared, we have to compile it */
   tc = mips64_jit_tcb_translate(cpu,tb);
   
   if (tc != NULL) {
      tc->target_code = tb->target_code;
      tc->trans_pos   = 0;
 
      tb_enable(cpu->gen,tb);
      tc_register(cpu->gen,tb,tc);
   } else {
      tb->flags |= TB_FLAG_NOJIT;
      tb_enable(cpu->gen,tb);
   }
   
   return tb;
}

/* Run a compiled MIPS instruction block */
static forced_inline 
void mips64_jit_tcb_run(cpu_mips_t *cpu,cpu_tb_t *tb)
{
#if DEBUG_SYM_TREE
   struct symbol *sym = NULL;
   int mark = FALSE;
#endif

   if (unlikely(cpu->pc & 0x03)) {
      fprintf(stderr,"mips64_jit_tcb_run: Invalid PC 0x%llx.\n",cpu->pc);
      mips64_dump_regs(cpu->gen);
      mips64_tlb_dump(cpu->gen);
      cpu_stop(cpu->gen);
      return;
   }

#if DEBUG_SYM_TREE
   if (cpu->sym_trace && cpu->sym_tree)
   {
      if ((sym = mips64_sym_lookup(cpu,cpu->pc)) != NULL) {
         cpu_log(cpu,"mips64_jit_tcb_run(start)",
                 "%s (PC=0x%llx) RA = 0x%llx\na0=0x%llx, "
                 "a1=0x%llx, a2=0x%llx, a3=0x%llx\n",
                 sym->name, cpu->pc, cpu->gpr[MIPS_GPR_RA],
                 cpu->gpr[MIPS_GPR_A0], cpu->gpr[MIPS_GPR_A1],
                 cpu->gpr[MIPS_GPR_A2], cpu->gpr[MIPS_GPR_A3]);
         mark = TRUE;
      }
   }
#endif

   /* Execute JIT compiled code */
   mips64_jit_tcb_exec(cpu,tb);

#if DEBUG_SYM_TREE
   if (mark) {
      cpu_log(cpu,"mips64_jit_tcb_run(end)","%s, v0 = 0x%llx\n",
              sym->name,cpu->gpr[MIPS_GPR_V0]);
   }
#endif
}

/* Execute compiled MIPS code */
void *mips64_jit_run_cpu(cpu_gen_t *gen)
{    
   cpu_mips_t *cpu = CPU_MIPS64(gen);
   pthread_t timer_irq_thread;
   cpu_tb_t *tb;
   m_uint32_t hv,hp;
   m_uint32_t phys_page;
   int timer_irq_check = 0;

   if (pthread_create(&timer_irq_thread,NULL,
                      (void *)mips64_timer_irq_run,cpu)) 
   {
      fprintf(stderr,
              "VM '%s': unable to create Timer IRQ thread for CPU%u.\n",
              cpu->vm->name,gen->id);
      cpu_stop(cpu->gen);
      return NULL;
   }

   gen->cpu_thread_running = TRUE;
   cpu_exec_loop_set(gen);
   
 start_cpu:   
   gen->idle_count = 0;

   for(;;) {
      if (unlikely(gen->state != CPU_STATE_RUNNING)) {
         /* 
          * We are paused/halted, so free the TCB/TCD in order to allow
          * reallocation of exec pages for other vCPUs.
          */
         cpu_jit_tcb_flush_all(cpu->gen);
         break;
      }
      
#if DEBUG_BLOCK_PERF_CNT
      cpu->perf_counter++;
#endif
      /* Handle virtual idle loop */
      if (unlikely(cpu->pc == cpu->idle_pc)) {
         if (++gen->idle_count == gen->idle_max) {
            cpu_idle_loop(gen);
            gen->idle_count = 0;
         }
      }

      /* Handle the virtual CPU clock */
      if (++timer_irq_check == cpu->timer_irq_check_itv) {
         timer_irq_check = 0;

         if (cpu->timer_irq_pending && !cpu->irq_disable) {
            mips64_trigger_timer_irq(cpu);
            mips64_trigger_irq(cpu);
            cpu->timer_irq_pending--;
         }
      }

      /* Get the JIT block corresponding to PC register */
      hv = mips64_jit_get_virt_hash(cpu->pc);
      tb = gen->tb_virt_hash[hv];

      if (unlikely(!tb) || unlikely(!mips64_jit_tcb_match(cpu,tb))) 
      {
         /* slow lookup: try to find the page by physical address */
         cpu->translate(cpu,cpu->pc,&phys_page);
         hp = mips64_jit_get_phys_hash(phys_page);

         for(tb=gen->tb_phys_hash[hp];tb;tb=tb->phys_next)
            if (mips64_jit_tcb_match(cpu,tb))
               goto tb_found;

         /* the TB doesn't exist, compile the page */
         tb = mips64_jit_tcb_compile(cpu,cpu->pc,cpu->exec_state);

         if (unlikely(!tb)) {
            fprintf(stderr,
                    "VM '%s': unable to compile block for CPU%u PC=0x%llx\n",
                    cpu->vm->name,gen->id,cpu->pc);
            cpu_stop(gen);
            break;
         }

        tb_found:
         /* update the virtual hash table */
         gen->tb_virt_hash[hv] = tb;
      }

#if DEBUG_BLOCK_TIMESTAMP
      tb->tm_last_use = jit_jiffies++;
#endif
      tb->acc_count++;
 
      cpu->current_tb = tb;

      if (unlikely(tb->flags & TB_FLAG_NOTRANS))
         mips64_exec_page(cpu);
      else
         mips64_jit_tcb_run(cpu,tb);
   }

   /* Check regularly if the CPU has been restarted */
   while(gen->cpu_thread_running) {
      gen->seq_state++;

      switch(gen->state) {
         case CPU_STATE_RUNNING:
            printf("VM %s: starting CPU!\n",cpu->vm->name);
            goto start_cpu;

         case CPU_STATE_HALTED:
            gen->cpu_thread_running = FALSE;
            pthread_join(timer_irq_thread,NULL);
            return NULL;
      }
      
      /* CPU is paused */
      usleep(200000);
   }

   return NULL;
}
