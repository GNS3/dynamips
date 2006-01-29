/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __MIPS64_EXEC_H__
#define __MIPS64_EXEC_H__

#include "utils.h"

/* MIPS instruction recognition */
struct insn_exec_tag {
   char *name;
   int (*exec)(cpu_mips_t *,mips_insn_t);
   m_uint32_t mask,value;
   int delay_slot;
   int instr_type;
   m_uint64_t count;
};

/* Initialize instruction lookup table */
void mips64_exec_create_ilt(void);

/* Dump statistics */
void mips64_dump_stats(cpu_mips_t *cpu);

/* Run MIPS code in step-by-step mode */
void *mips64_exec_run_cpu(cpu_mips_t *cpu);

#endif
