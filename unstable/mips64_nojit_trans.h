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

static inline void mips64_jit_tcb_set_patch(u_char *code,u_char *target) {}
static inline void mips64_jit_tcb_set_jump(u_char **instp,u_char *target) {}

/* MIPS instruction array */
extern struct mips64_insn_tag mips64_insn_tags[];

/* Push epilog for an x86 instruction block */
void mips64_jit_tcb_push_epilog(cpu_tc_t *tc);

/* Execute JIT code */
void mips64_jit_tcb_exec(cpu_mips_t *cpu,cpu_tb_t *tb);

#endif
