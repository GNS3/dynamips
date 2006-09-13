/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Just an empty JIT template file for architectures not supported by the JIT
 * code.
 */

#ifndef __NOJIT_TRANS_H__
#define __NOJIT_TRANS_H__

#include "utils.h"
#include "mips64.h"
#include "dynamips.h"
#include "cp0.h"

#define JIT_SUPPORT 0

#define insn_block_set_patch(a,b)
#define insn_block_set_jump(a,b)

/* MIPS instruction array */
extern struct insn_tag mips64_insn_tags[];

/* Push epilog for an x86 instruction block */
void insn_block_push_epilog(insn_block_t *block);

/* Execute JIT code */
void insn_block_exec_jit_code(cpu_mips_t *cpu,insn_block_t *block);

/* Set the Pointer Counter (PC) register */
void mips64_set_pc(insn_block_t *b,m_uint64_t new_pc);

/* Set the Return Address (RA) register */
void mips64_set_ra(insn_block_t *b,m_uint64_t ret_pc);

/* Virtual Breakpoint */
void mips64_emit_breakpoint(insn_block_t *b);

/* Emit unhandled instruction code */
int mips64_emit_invalid_delay_slot(insn_block_t *b);

/* 
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
void mips64_inc_cp0_count_reg(insn_block_t *b);

/* Increment the number of executed instructions (performance debugging) */
void mips64_inc_perf_counter(insn_block_t *b);

#endif
