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

#endif
