/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __PPC32_AMD64_TRANS_H__
#define __PPC32_AMD64_TRANS_H__

#include "utils.h"
#include "amd64-codegen.h"
#include "cpu.h"
#include "dynamips.h"
#include "ppc32_exec.h"

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

/* Wrappers to amd64-codegen functions */
#define ppc32_jit_tcb_set_patch amd64_patch
#define ppc32_jit_tcb_set_jump  amd64_jump_code

/* PPC instruction array */
extern struct ppc32_insn_tag ppc32_insn_tags[];

/* Push epilog for an x86 instruction block */
static forced_inline void ppc32_jit_tcb_push_epilog(u_char **ptr)
{
   amd64_ret(*ptr);
}

/* Execute JIT code */
static forced_inline
void ppc32_jit_tcb_exec(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block)
{
   insn_tblock_fptr jit_code;
   m_uint32_t offset;

   offset = (cpu->ia & PPC32_MIN_PAGE_IMASK) >> 2;
   jit_code = (insn_tblock_fptr)block->jit_insn_ptr[offset];

   if (unlikely(!jit_code)) {
      ppc32_jit_tcb_set_target_bit(block,cpu->ia);

      if (++block->target_undef_cnt == 16) {
         ppc32_jit_tcb_recompile(cpu,block);
         jit_code = (insn_tblock_fptr)block->jit_insn_ptr[offset];
      } else {
         ppc32_exec_page(cpu);
         return;
      }
   }

   asm volatile ("movq %0,%%r15"::"r"(cpu):
                 "r13","r14","r15","rax","rbx","rcx","rdx","rdi","rsi");
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

#endif
