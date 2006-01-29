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

#define JIT_SUPPORT 1

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

   jit_code = (insn_tblock_fptr)insn_block_get_jit_ptr(block,cpu->pc);

   if (unlikely(!jit_code)) {
      fprintf(stderr,"insn_block_run: null JIT handler for PC 0x%llx\n",
              cpu->pc);
      mips64_dump_regs(cpu);
      tlb_dump(cpu);
      exit(1);
   }

   asm volatile ("movl %0,%%edi"::"r"(cpu):"edi","eax","ebx","ecx","edx");
   jit_code();
}

/* Set the Pointer Counter (PC) register */
void mips64_set_pc(insn_block_t *b,m_uint64_t new_pc);

/* Set the Return Address (RA) register */
void mips64_set_ra(insn_block_t *b,m_uint64_t ret_pc);

/* Virtual Breakpoint */
void mips64_emit_breakpoint(insn_block_t *b);

/* 
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
void mips64_inc_cp0_count_reg(insn_block_t *b);

#endif
