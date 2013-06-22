/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __PPC32_X86_TRANS_H__
#define __PPC32_X86_TRANS_H__

#include "utils.h"
#include "x86-codegen.h"
#include "cpu.h"
#include "ppc32_exec.h"
#include "dynamips.h"

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
#define ppc32_jit_tcb_set_patch x86_patch
#define ppc32_jit_tcb_set_jump  x86_jump_code

/* PPC instruction array */
extern struct ppc32_insn_tag ppc32_insn_tags[];

/* Push epilog for an x86 instruction block */
static forced_inline void ppc32_jit_tcb_push_epilog(u_char **ptr)
{
   x86_ret(*ptr);
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

   asm volatile ("movl %0,%%edi"::"r"(cpu):
                 "esi","edi","eax","ebx","ecx","edx");
   jit_code();
}

#endif
