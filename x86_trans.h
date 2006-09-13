/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __X86_TRANS_H__
#define __X86_TRANS_H__

#include "utils.h"
#include "x86-codegen.h"
#include "mips64.h"
#include "dynamips.h"
#include "cp0.h"
#include "mips64_exec.h"

#define JIT_SUPPORT 1

/* Manipulate bitmasks atomically */
static forced_inline void atomic_or(m_uint32_t *v,m_uint32_t m)
{
   __asm__ __volatile__("lock; orl %1,%0":"=m"(*v):"ir"(m),"m"(*v));
}

static forced_inline void atomic_and(m_uint32_t *v,m_uint32_t m)
{
   __asm__ __volatile__("lock; andl %1,%0":"=m"(*v):"ir"(m),"m"(*v));
}

/* Wrappers to x86-codegen functions */
#define insn_block_set_patch x86_patch
#define insn_block_set_jump  x86_jump_code

/* MIPS instruction array */
extern struct insn_tag mips64_insn_tags[];

/* Push epilog for an x86 instruction block */
static forced_inline void insn_block_push_epilog(insn_block_t *block)
{
   x86_ret(block->jit_ptr);
}

/* Execute JIT code */
static forced_inline
void insn_block_exec_jit_code(cpu_mips_t *cpu,insn_block_t *block)
{
   insn_tblock_fptr jit_code;
   m_uint32_t offset;

   offset = (cpu->pc & MIPS_MIN_PAGE_IMASK) >> 2;
   jit_code = (insn_tblock_fptr)block->jit_insn_ptr[offset];

   if (unlikely(!jit_code)) {
      mips64_exec_single_step(cpu,vmtoh32(block->mips_code[offset]));
      return;
   }

   asm volatile ("movl %0,%%edi"::"r"(cpu):
                 "esi","edi","eax","ebx","ecx","edx");
   jit_code();
}

/* Set the Pointer Counter (PC) register */
void mips64_set_pc(insn_block_t *b,m_uint64_t new_pc);

/* Set the Return Address (RA) register */
void mips64_set_ra(insn_block_t *b,m_uint64_t ret_pc);

/* Virtual Breakpoint */
void mips64_emit_breakpoint(insn_block_t *b);

/* Emit unhandled instruction code */
int mips64_emit_invalid_delay_slot(insn_block_t *b);

/* 
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
void mips64_inc_cp0_count_reg(insn_block_t *b);

/* Increment the number of executed instructions (performance debugging) */
void mips64_inc_perf_counter(insn_block_t *b);

#endif
