/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Just an empty JIT template file for architectures not supported by the JIT
 * code.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "cpu.h"
#include "ppc32_jit.h"
#include "ppc32_nojit_trans.h"

#define EMPTY(func) func { \
   fprintf(stderr,"This function should not be called: "#func"\n"); \
   abort(); \
}

EMPTY(void ppc32_jit_tcb_push_epilog(ppc32_jit_tcb_t *block));
EMPTY(void ppc32_jit_tcb_exec(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block));
EMPTY(void ppc32_set_ia(ppc32_jit_tcb_t *b,m_uint32_t new_ia));
EMPTY(void ppc32_set_lr(ppc32_jit_tcb_t *b,m_uint32_t new_lr));
EMPTY(void ppc32_inc_perf_counter(ppc32_jit_tcb_t *b));

/* PowerPC instruction array */
struct ppc32_insn_tag ppc32_insn_tags[] = {
   { NULL, 0, 0 },
};
