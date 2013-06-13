/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * MIPS64 JIT compiler.
 */

#ifndef __MIPS64_JIT_H__
#define __MIPS64_JIT_H__

#include "utils.h"
#include "sbox.h"
#include "tcb.h"

/* Size of hash for virtual address lookup */
#define MIPS_JIT_VIRT_HASH_BITS   16
#define MIPS_JIT_VIRT_HASH_MASK   ((1 << MIPS_JIT_VIRT_HASH_BITS) - 1)
#define MIPS_JIT_VIRT_HASH_SIZE   (1 << MIPS_JIT_VIRT_HASH_BITS)

/* Size of hash for physical lookup */
#define MIPS_JIT_PHYS_HASH_BITS   16
#define MIPS_JIT_PHYS_HASH_MASK   ((1 << MIPS_JIT_PHYS_HASH_BITS) - 1)
#define MIPS_JIT_PHYS_HASH_SIZE   (1 << MIPS_JIT_PHYS_HASH_BITS)

/* MIPS instruction recognition */
struct mips64_insn_tag {
   int (*emit)(cpu_mips_t *cpu,cpu_tc_t *,mips_insn_t);
   m_uint32_t mask,value;
   int delay_slot;
};

/* MIPS jump instruction (for block scan) */
struct mips64_insn_jump {
   char *name;
   m_uint32_t mask,value;
   int offset_bits;
   int relative;
};

/* Get the JIT instruction pointer in a translated block */
static forced_inline 
u_char *mips64_jit_tc_get_host_ptr(cpu_tc_t *tc,m_uint64_t vaddr)
{
   m_uint32_t offset;

   offset = ((m_uint32_t)vaddr & MIPS_MIN_PAGE_IMASK) >> 2;
   return(tc->jit_insn_ptr[offset]);
}

/* Check if the specified address belongs to the specified block */
static forced_inline 
int mips64_jit_tcb_local_addr(cpu_tc_t *tc,m_uint64_t vaddr,
                              u_char **jit_addr)
{
   if ((vaddr & MIPS_MIN_PAGE_MASK) == tc->vaddr) {
      *jit_addr = mips64_jit_tc_get_host_ptr(tc,vaddr);
      return(1);
   }

   return(0);
}

/* Check if PC register matches the compiled block virtual address */
static forced_inline int mips64_jit_tcb_match(cpu_mips_t *cpu,cpu_tb_t *tb)
{
   m_uint64_t vpage;

   vpage = cpu->pc & MIPS_MIN_PAGE_MASK;
   return((tb->vaddr == vpage) && (tb->exec_state == cpu->exec_state));
}

/* Compute the hash index for the specified virtual address */
static forced_inline m_uint32_t mips64_jit_get_virt_hash(m_uint64_t vaddr)
{
   m_uint32_t page_hash;

   page_hash = sbox_u32(vaddr >> MIPS_MIN_PAGE_SHIFT);
   return((page_hash ^ (page_hash >> 12)) & MIPS_JIT_VIRT_HASH_MASK);
}

/* Compute the hash index for the specified physical page */
static forced_inline m_uint32_t mips64_jit_get_phys_hash(m_uint32_t phys_page)
{
   m_uint32_t page_hash;

   page_hash = sbox_u32(phys_page);
   return((page_hash ^ (page_hash >> 12)) & MIPS_JIT_PHYS_HASH_MASK);
}

/* Find a JIT block matching a physical page */
static inline cpu_tb_t *
mips64_jit_find_by_phys_page(cpu_mips_t *cpu,m_uint32_t phys_page)
{
   m_uint32_t page_hash = mips64_jit_get_phys_hash(phys_page);
   cpu_tb_t *tb;
   
   for(tb=cpu->gen->tb_phys_hash[page_hash];tb;tb=tb->phys_next)
      if (tb->phys_page == phys_page)
         return tb;

   return NULL;
}

/* Check if there are pending IRQ */
extern void mips64_check_pending_irq(cpu_tc_t *tc);

/* Initialize instruction lookup table */
void mips64_jit_create_ilt(void);

/* Initialize the JIT structure */
int mips64_jit_init(cpu_mips_t *cpu);

/* Flush the JIT */
u_int mips64_jit_flush(cpu_mips_t *cpu,u_int threshold);

/* Shutdown the JIT */
void mips64_jit_shutdown(cpu_mips_t *cpu);

/* Check if an instruction is in a delay slot or not */
int mips64_jit_is_delay_slot(cpu_tc_t *tc,m_uint64_t pc);

/* Fetch a MIPS instruction and emit corresponding translated code */
struct mips64_insn_tag *mips64_jit_fetch_and_emit(cpu_mips_t *cpu,
                                                  cpu_tc_t *tc,
                                                  int delay_slot);

/* Record a patch to apply in a compiled block */
int mips64_jit_tcb_record_patch(cpu_mips_t *cpu,cpu_tc_t *tc,
                                u_char *jit_ptr,m_uint64_t vaddr);

/* Mark a block as containing self-modifying code */
void mips64_jit_mark_smc(cpu_mips_t *cpu,cpu_tb_t *tb);

/* Free an instruction block */
void mips64_jit_tcb_free(cpu_mips_t *cpu,cpu_tb_t *tb,int list_removal);

/* Execute compiled MIPS code */
void *mips64_jit_run_cpu(cpu_gen_t *cpu);

/* Set the Pointer Counter (PC) register */
void mips64_set_pc(cpu_tc_t *tc,m_uint64_t new_pc);

/* Set the Return Address (RA) register */
void mips64_set_ra(cpu_tc_t *tc,m_uint64_t ret_pc);

/* Single-step operation */
void mips64_emit_single_step(cpu_tc_t *tc,mips_insn_t insn);

/* Virtual Breakpoint */
void mips64_emit_breakpoint(cpu_tc_t *tc);

/* Emit unhandled instruction code */
int mips64_emit_invalid_delay_slot(cpu_tc_t *tc);

/* 
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
void mips64_inc_cp0_count_reg(cpu_tc_t *tc);

/* Increment the number of executed instructions (performance debugging) */
void mips64_inc_perf_counter(cpu_tc_t *tc);

#endif
