/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * JIT engine for 32-bit PowerPC architecture
 * Copyright (c) 2006, 2007 Zhe Fang (fangzhe@msn.com)
 */

#ifndef __MIPS64_PPC32_TRANS_H__
#define __MIPS64_PPC32_TRANS_H__

#include "utils.h"
#include "cpu.h"
#include "mips64_exec.h"
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

/* Made from ppc_patch in ppc-codegen.h */
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

/* Here's a hack, see comments in mips64_set_jump for more info */
//#define mips64_jit_tcb_set_patch ppc_patch
#define mips64_jit_tcb_set_patch(a,b) ppc_emit_jump_code(a,b,0)
#define mips64_jit_tcb_set_jump(a,b)  ppc_emit_jump_code(a,b,0)

/* MIPS instruction array */
extern struct mips64_insn_tag mips64_insn_tags[];

#define PPC_STACK_DECREMENTER 114

/* Push epilog for a ppc instruction block */
static forced_inline void mips64_jit_tcb_push_epilog(mips64_jit_tcb_t *b)
{
   /* Restore link register */
   ppc_lwz(b->jit_ptr,ppc_r0,PPC_STACK_DECREMENTER+PPC_RET_ADDR_OFFSET,ppc_r1);
   ppc_mtlr(b->jit_ptr,ppc_r0);
   ppc_blr(b->jit_ptr);
}

/* Execute JIT code */
static forced_inline
void mips64_jit_tcb_exec(cpu_mips_t *cpu,mips64_jit_tcb_t *block)
{
   register insn_tblock_fptr jit_code __asm__("r12");
   m_uint32_t offset;

   offset = (cpu->pc & MIPS_MIN_PAGE_IMASK) >> 2;
   jit_code = (insn_tblock_fptr)block->jit_insn_ptr[offset];

   if (unlikely(!jit_code)) {
      mips64_exec_single_step(cpu,vmtoh32(block->mips_code[offset]));
      return;
   }

   /* Same as C call of jit_code(cpu_mips_t *cpu) except establish and
      destroy caller's stack frame here.
      r0, r3 - r10, r11, r12, ctr, xer, cr0 - cr5, cr6 - cr7 are volatile
      according to the ABI.
      CPU instance pointer passed through r3, also preserved onto stack.
    */
   __asm__ __volatile__(
                        "mtlr   r12                   \n"
                        "mr      r3,   %1             \n"
                        "lis     r0, hi16(jit_ret)    \n"
                        "ori     r0, r0, lo16(jit_ret)\n"
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
