/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __AMD64_TRANS_H__
#define __AMD64_TRANS_H__

#include "utils.h"
#include "amd64-codegen.h"
#include "mips64.h"
#include "dynamips.h"
#include "cp0.h"

#define JIT_SUPPORT 1

/* Wrappers to amd64-codegen functions */
#define insn_block_set_patch amd64_patch
#define insn_block_set_jump  amd64_jump_code

/* MIPS instruction array */
extern struct insn_tag mips64_insn_tags[];

/* Push epilog for an amd64 instruction block */
static forced_inline void insn_block_push_epilog(insn_block_t *block)
{
   amd64_ret(block->jit_ptr);
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

   asm volatile ("movq %0,%%r15"::"r"(cpu):"r15");
   jit_code();
}

static inline void amd64_patch(u_char *code,u_char *target)
{
   /* Skip REX */
   if ((code[0] >= 0x40) && (code[0] <= 0x4f))
      code += 1;

   if ((code [0] & 0xf8) == 0xb8) {
      /* amd64_set_reg_template */
      *(m_uint64_t *)(code + 1) = (m_uint64_t)target;
   }
   else if (code [0] == 0x8b) {
      /* mov 0(%rip), %dreg */
      *(m_uint32_t *)(code + 2) = (m_uint32_t)(m_uint64_t)target - 7;
   }
   else if ((code [0] == 0xff) && (code [1] == 0x15)) {
      /* call *<OFFSET>(%rip) */
      *(m_uint32_t *)(code + 2) = ((m_uint32_t)(m_uint64_t)target) - 7;
   }
   else
      x86_patch(code,target);
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
