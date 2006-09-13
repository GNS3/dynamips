/*
 * Cisco 7200 (Predator) simulation platform.
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

#include "nojit_trans.h"

/* Set an IRQ */
void mips64_set_irq(cpu_mips_t *cpu,m_uint8_t irq)
{
   m_uint32_t m;
   m = (1 << (irq + MIPS_CP0_CAUSE_ISHIFT)) & MIPS_CP0_CAUSE_IMASK;
   MIPS64_IRQ_LOCK(cpu);
   cpu->irq_cause |= m;
   MIPS64_IRQ_UNLOCK(cpu);
}

/* Clear an IRQ */
void mips64_clear_irq(cpu_mips_t *cpu,m_uint8_t irq)
{
   m_uint32_t m;

   m = (1 << (irq + MIPS_CP0_CAUSE_ISHIFT)) & MIPS_CP0_CAUSE_IMASK;
   MIPS64_IRQ_LOCK(cpu);
   cpu->irq_cause &= ~m;
   MIPS64_IRQ_UNLOCK(cpu);

   if (!cpu->irq_cause)
      cpu->irq_pending = 0;
}

#define EMPTY(func) func { \
   fprintf(stderr,"This function should not be called: "#func"\n"); \
   abort(); \
}

EMPTY(void insn_block_push_epilog(insn_block_t *block));
EMPTY(void insn_block_exec_jit_code(cpu_mips_t *cpu,insn_block_t *block));
EMPTY(void mips64_set_pc(insn_block_t *b,m_uint64_t new_pc));
EMPTY(void mips64_set_ra(insn_block_t *b,m_uint64_t ret_pc));
EMPTY(void mips64_emit_breakpoint(insn_block_t *b));
EMPTY(void mips64_emit_invalid_delay_slot(insn_block_t *b));
EMPTY(void mips64_inc_cp0_count_reg(insn_block_t *b));
EMPTY(void mips64_check_pending_irq(insn_block_t *b));
EMPTY(void mips64_inc_perf_counter(insn_block_t *b));

/* MIPS instruction array */
struct insn_tag mips64_insn_tags[] = {
   { NULL, 0, 0, 0 },
};
