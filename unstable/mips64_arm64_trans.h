/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __MIPS64_ARM64_TRANS_H__
#define __MIPS64_ARM64_TRANS_H__

#include <assert.h>
#include "utils.h"
#include "arm64-codegen.h"
#include "cpu.h"
#include "dynamips.h"
#include "mips64_exec.h"

#define JIT_SUPPORT 1

/* Manipulate bitmasks atomically */
static forced_inline void atomic_or(m_uint32_t *v,m_uint32_t m)
{
   __atomic_or_fetch(v, m, __ATOMIC_RELAXED);
}

static forced_inline void atomic_and(m_uint32_t *v,m_uint32_t m)
{
   __atomic_and_fetch(v, m, __ATOMIC_RELAXED);
}

/* Wrappers to arm64-codegen functions */
#define mips64_jit_tcb_set_patch arm64_patch
#define mips64_jit_tcb_set_jump  arm64_jump_code_fn

/* MIPS instruction array */
extern struct mips64_insn_tag mips64_insn_tags[];

/* Push epilog for an arm64 instruction block */
static forced_inline void mips64_jit_tcb_push_epilog(cpu_tc_t *tc)
{
   arm64_ret(tc->jit_ptr);
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
   pthread_jit_write_protect_np(1);
   asm volatile ("mov x28,%0\n\t"
                 "blr %1"
                 ::"r"(cpu), "r"(jit_code):
                  "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
                  "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x19", "x20",
                  "x21", "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x30", "memory");
   pthread_jit_write_protect_np(0);
}

static inline void arm64_patch(u_char *code,u_char *target)
{
   m_uint32_t insn = *(m_uint32_t*)code;
   m_int64_t disp19_pcrel = ((m_int64_t)target - (m_int64_t)code) / 4 & 0xFFFFF;
   m_int64_t disp26_pcrel = ((m_int64_t)target - (m_int64_t)code) / 4 & 0x3FFFFFF;
   /* cbz */
   if ((insn & 0xFE000000) == 0xB4000000)
      *(m_uint32_t*)code = (insn & 0xFF00000F) | (disp19_pcrel << 5);
   /* b.cond */
   else if ((insn & 0xFF000000) == 0x54000000)
      *(m_uint32_t*)code = (insn & 0xFF00000F) | (disp19_pcrel << 5);
   /* b */
   else if ((insn & 0xFC000000) == 0x14000000)
      *(m_uint32_t*)code = (insn & 0xFC000000) | disp26_pcrel;
   else {
      fprintf(stderr, "!!! %x\n", insn);
      assert(0);
   }
}

#endif
