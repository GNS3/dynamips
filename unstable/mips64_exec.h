/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __MIPS64_EXEC_H__
#define __MIPS64_EXEC_H__

#include "utils.h"

/* MIPS instruction recognition */
struct mips64_insn_exec_tag {
   char *name;
   fastcall int (*exec)(cpu_mips_t *,mips_insn_t);
   m_uint32_t mask,value;
   int delay_slot;
   int instr_type;
   m_uint64_t count;
};

/* Initialize instruction lookup table */
void mips64_exec_create_ilt(void);

/* Dump statistics */
void mips64_dump_stats(cpu_mips_t *cpu);

/* Dump an instruction */
int mips64_dump_insn(char *buffer,size_t buf_size,size_t insn_name_size,
                     m_uint64_t pc,mips_insn_t instruction);

/* Dump an instruction block */
void mips64_dump_insn_block(cpu_mips_t *cpu,m_uint64_t pc,u_int count,
                            size_t insn_name_size);

/* Single-step execution */
fastcall void mips64_exec_single_step(cpu_mips_t *cpu,mips_insn_t instruction);

/* Execute a page */
fastcall int mips64_exec_page(cpu_mips_t *cpu);

/* Run MIPS code in step-by-step mode */
void *mips64_exec_run_cpu(cpu_gen_t *cpu);

/* Fetch an instruction */
/*static forced_inline*/ int mips64_exec_fetch(cpu_mips_t *cpu,m_uint64_t pc,
                                           mips_insn_t *insn);

                                           /* Execute a single instruction */
/*static forced_inline*/ int 
mips64_exec_single_instruction(cpu_mips_t *cpu,mips_insn_t instruction);

#endif
