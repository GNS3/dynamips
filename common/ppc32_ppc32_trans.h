/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * JIT engine for 32-bit PowerPC architecture
 * Copyright (c) 2006, 2007 Zhe Fang (fangzhe@msn.com)
 */

#ifndef __PPC32_PPC32_TRANS_H__
#define __PPC32_PPC32_TRANS_H__

#include "utils.h"
#include "cpu.h"
#include "ppc32_exec.h"
#include "dynamips.h"
#include "ppc-codegen.h"

#define JIT_SUPPORT 1

/* Manipulate bitmasks synchronically */
static forced_inline void atomic_or(m_uint32_t *v,m_uint32_t m)
{
   __asm__ __volatile__("lwarx  r0, 0, %0    \n"
                        "or     r0, r0, %1 \n"
                        "stwcx. r0, 0, %0    \n"
                        "bne-   $-12":"+p"(v): "r"(m): "r0", "memory");
}

static forced_inline void atomic_and(m_uint32_t *v,m_uint32_t m)
{
   __asm__ __volatile__("lwarx  r0, 0, %0    \n"
                        "and    r0, r0, %1 \n"
                        "stwcx. r0, 0, %0    \n"
                        "bne-   $-12":"+p"(v): "r"(m): "r0", "memory");
}

/* Emit a jump (or call with LK) to a possibly far target */
#define ppc_emit_jump_code(code,target,lk)	do {\
 \
		/* prefer relative branches, they are more position independent (e.g. for AOT compilation). */\
		if ((target) - (code) >= 0){ \
			if ((target) - (code) <= 33554431){ \
				ppc_emit32 ((code), (18 << 26) | ((target) - (code)) | lk); \
				break; \
			} \
		} else { \
			/* diff between 0 and -33554432 */ \
			if ((target) - (code) >= -33554432){ \
				ppc_emit32 ((code), (18 << 26) | (((target) - (code)) & ~0xfc000000) | lk); \
				break; \
			} \
		} \
 \
		if ((long)(target) >= 0){ \
			if ((long)(target) <= 33554431){ \
				ppc_emit32 ((code), (18 << 26) | (unsigned int)(target) | 2 | lk); \
				break; \
			} \
		} else { \
			if ((long)(target) >= -33554432){ \
				ppc_emit32 ((code), (18 << 26) | ((unsigned int)(target) & ~0xfc000000) | 2 | lk); \
				break; \
			} \
		} \
 \
		/* The last way... */ \
		ppc_lis ((code), ppc_r12, (unsigned int)(target) >> 16); \
		ppc_ori ((code), ppc_r12, ppc_r12, (unsigned int)(target) & 0xffff); \
		ppc_mtlr((code), ppc_r12); \
		ppc_bclrx((code), PPC_BR_ALWAYS, 0, lk); \
} while (0)

/* Patch a previously emitted branch slot (b or bc) in place */
static inline void ppc32_jit_tcb_set_patch(u_char *instp,u_char *target)
{
   long offset = (long)target - (long)instp;

   if ((offset & ~0x1ffffffL) == 0 || (offset & ~0x1ffffffL) == ~0x1ffffffL) {
      /* Near target: patch the 4-byte branch slot in place. */
      ppc_patch(instp,target);
   } else {
      /* Far target (e.g. the branch target slot landed in another JIT
         chunk, more than 32MB away): the branch emitters reserve 3 more
         instructions after the 4-byte patch slot, forming a 16-byte area
         that we fill with a lis/ori/mtlr/blr trampoline.  r12 is scratch
         in this backend and the host LR is only set by the block epilogue,
         so clobbering them here is safe. */
      ppc_lis(instp,ppc_r12,(unsigned int)((unsigned long)target) >> 16);
      ppc_ori(instp,ppc_r12,ppc_r12,
              (unsigned int)((unsigned long)target) & 0xffff);
      ppc_mtlr(instp,ppc_r12);
      ppc_bclrx(instp,PPC_BR_ALWAYS,0,0);
   }
   /* Flush a safe window so the icache sees the new code. */
   mono_ppc_flush_icache(instp - 16,48);
}

/* Emit a jump at instp (used when switching to a new JIT buffer) */
static inline void ppc32_jit_tcb_set_jump(u_char *instp,u_char *target)
{
   ppc_emit_jump_code(instp,target,0);
   mono_ppc_flush_icache(instp,16);
}

/* PPC instruction array */
extern struct ppc32_insn_tag ppc32_insn_tags[];

#define PPC_STACK_DECREMENTER 114

/* Push epilog for a PPC instruction block */
static forced_inline void ppc32_jit_tcb_push_epilog(u_char **ptr)
{
   /* Restore link register from the caller frame and return */
   ppc_lwz(*ptr,ppc_r0,PPC_STACK_DECREMENTER+PPC_RET_ADDR_OFFSET,ppc_r1);
   ppc_mtlr(*ptr,ppc_r0);
   ppc_blr(*ptr);
}

/* Execute JIT code */
static forced_inline
void ppc32_jit_tcb_exec(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block)
{
   register insn_tblock_fptr jit_code __asm__("r12");
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

   /* Same as C call of jit_code(cpu_ppc_t *cpu) except establish and
      destroy caller's stack frame here.
      r0, r3 - r10, r11, r12, ctr, xer, cr0 - cr5, cr6 - cr7 are volatile
      according to the ABI.
      CPU instance pointer passed through r3, also preserved onto stack.
    */
   __asm__ __volatile__(
                        "mtlr   r12                   \n"
                        "mr      r3,   %1             \n"
                        "lis     r0, jit_ret@h        \n"
                        "ori     r0, r0, jit_ret@l    \n"
                        "stw     r3,   %2(r1)         \n"
                        "stw     r0,   %3(r1)         \n"
                        "stwu    r1,   %4(r1)         \n"
                        "blr                          \n"
                        "jit_ret:                     \n"
                        "lwz     r1,    0(r1)         \n"
                        :"+r"(jit_code):"r"(cpu),"i"(PPC_STACK_PARAM_OFFSET),
                        "i"(PPC_RET_ADDR_OFFSET),"i"(-PPC_STACK_DECREMENTER)
                        :"r0","r3","r4","r5","r6","r7","r8","r9","r10",
                        "r11",/*"r12",*/"lr","ctr","xer","cr0","cr1","cr5",
                        "cr6","cr7","memory");
}

#endif
