/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __PPC32_AMD64_TRANS_H__
#define __PPC32_AMD64_TRANS_H__

#include <assert.h>
#include "utils.h"
#include "arm64-codegen.h"
#include "cpu.h"
#include "dynamips.h"
#include "ppc32_exec.h"

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

/* Wrappers to amd64-codegen functions */
#define ppc32_jit_tcb_set_patch arm64_patch
#define ppc32_jit_tcb_set_jump  arm64_jump_code

/* PPC instruction array */
extern struct ppc32_insn_tag ppc32_insn_tags[];

/* Push epilog for an x86 instruction block */
static forced_inline void ppc32_jit_tcb_push_epilog(u_char **ptr)
{
   arm64_ret(*ptr);
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
   m_int64_t disp14_pcrel = ((m_int64_t)target - (m_int64_t)code) / 4 & 0x7FFF;
   m_int64_t disp19_pcrel = ((m_int64_t)target - (m_int64_t)code) / 4 & 0xFFFFF;
   m_int64_t disp26_pcrel = ((m_int64_t)target - (m_int64_t)code) / 4 & 0x3FFFFFF;
   /* cbz */
   if ((insn & 0x7E000000) == 0x34000000)
      *(m_uint32_t*)code = (insn & 0xFF00001F) | (disp19_pcrel << 5);
   /* b.cond */
   else if ((insn & 0xFF000000) == 0x54000000)
      *(m_uint32_t*)code = (insn & 0xFF00000F) | (disp19_pcrel << 5);
   /* b */
   else if ((insn & 0xFC000000) == 0x14000000)
      *(m_uint32_t*)code = (insn & 0xFC000000) | disp26_pcrel;
   /* tbz */
   else if ((insn & 0x7E000000) == 0x36000000)
      *(m_uint32_t*)code = (insn & 0xFFF8001F) | (disp14_pcrel << 5);
   else {
      fprintf(stderr, "!!! %x\n", insn);
      assert(0);
   }
   __builtin___clear_cache(code, code+4);
}

#endif
