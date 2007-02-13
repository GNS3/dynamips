/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Just an empty JIT template file for architectures not supported by the JIT
 * code.
 */

#ifndef __MIPS64_NOJIT_TRANS_H__
#define __MIPS64_NOJIT_TRANS_H__

#include "utils.h"
#include "cpu.h"
#include "dynamips.h"

#define JIT_SUPPORT 0

#define mips64_jit_tcb_set_patch(a,b)
#define mips64_jit_tcb_set_jump(a,b)

/* MIPS instruction array */
extern struct mips64_insn_tag mips64_insn_tags[];

/* Push epilog for an x86 instruction block */
void mips64_jit_tcb_push_epilog(mips64_jit_tcb_t *block);

/* Execute JIT code */
void mips64_jit_tcb_exec(cpu_mips_t *cpu,mips64_jit_tcb_t *block);

/* Set the Pointer Counter (PC) register */
void mips64_set_pc(mips64_jit_tcb_t *b,m_uint64_t new_pc);

/* Set the Return Address (RA) register */
void mips64_set_ra(mips64_jit_tcb_t *b,m_uint64_t ret_pc);

/* Virtual Breakpoint */
void mips64_emit_breakpoint(mips64_jit_tcb_t *b);

/* Emit unhandled instruction code */
void mips64_emit_invalid_delay_slot(mips64_jit_tcb_t *b);

/* 
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
void mips64_inc_cp0_count_reg(mips64_jit_tcb_t *b);

/* Increment the number of executed instructions (performance debugging) */
void mips64_inc_perf_counter(mips64_jit_tcb_t *b);

#endif
