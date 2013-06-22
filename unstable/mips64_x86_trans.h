/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __MIPS64_X86_TRANS_H__
#define __MIPS64_X86_TRANS_H__

#include "utils.h"
#include "x86-codegen.h"
#include "cpu.h"
#include "mips64_exec.h"
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
#define mips64_jit_tcb_set_patch x86_patch_fn
#define mips64_jit_tcb_set_jump  x86_jump_code_fn

/* MIPS instruction array */
extern struct mips64_insn_tag mips64_insn_tags[];

/* Push epilog for an x86 instruction block */
static forced_inline void mips64_jit_tcb_push_epilog(cpu_tc_t *tc)
{
   x86_ret(tc->jit_ptr);
}

/* Execute JIT code */
static forced_inline void mips64_jit_tcb_exec(cpu_mips_t *cpu,cpu_tb_t *tb)
{
   insn_tblock_fptr jit_code;
   m_uint32_t offset,*iarray;

   offset = (cpu->pc & MIPS_MIN_PAGE_IMASK) >> 2;
   jit_code = (insn_tblock_fptr)tb->tc->jit_insn_ptr[offset];

   if (unlikely(!jit_code)) {
      iarray = (m_uint32_t *)tb->target_code;
      mips64_exec_single_step(cpu,vmtoh32(iarray[offset]));
      return;
   }

   asm volatile ("movl %0,%%edi"::"r"(cpu):
                 "esi","edi","eax","ebx","ecx","edx");
   jit_code();
}

#endif
