/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __PPC32_NOJIT_TRANS_H__
#define __PPC32_NOJIT_TRANS_H__

#include "utils.h"
#include "x86-codegen.h"
#include "cpu.h"
#include "ppc32_exec.h"
#include "dynamips.h"

#define JIT_SUPPORT 0

/* Wrappers to x86-codegen functions */
#define ppc32_jit_tcb_set_patch(a,b)
#define ppc32_jit_tcb_set_jump(a,b)

/* PPC instruction array */
extern struct ppc32_insn_tag ppc32_insn_tags[];

/* Virtual Breakpoint */
void ppc32_emit_breakpoint(ppc32_jit_tcb_t *b);

/* Push epilog for an x86 instruction block */
void ppc32_jit_tcb_push_epilog(ppc32_jit_tcb_t *block);

/* Execute JIT code */
void ppc32_jit_tcb_exec(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block);

/* Set the Instruction Address (IA) register */
void ppc32_set_ia(ppc32_jit_tcb_t *b,m_uint32_t new_ia);

/* Set the Link Register (LR) */
void ppc32_set_lr(ppc32_jit_tcb_t *b,m_uint32_t new_lr);

/* Increment the number of executed instructions (performance debugging) */
void ppc32_inc_perf_counter(ppc32_jit_tcb_t *b);

#endif
