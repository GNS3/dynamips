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
#define ppc32_jit_tcb_set_patch(a,b) (void)(a); (void)(b)
#define ppc32_jit_tcb_set_jump(a,b)  (void)(a); (void)(b)

/* PPC instruction array */
extern struct ppc32_insn_tag ppc32_insn_tags[];

/* Push epilog for an x86 instruction block */
void ppc32_jit_tcb_push_epilog(u_char **ptr);

/* Execute JIT code */
void ppc32_jit_tcb_exec(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block);

#endif
