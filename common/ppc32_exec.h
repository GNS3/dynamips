/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __PPC32_EXEC_H__
#define __PPC32_EXEC_H__

#include "utils.h"

/* PowerPC instruction recognition */
struct ppc32_insn_exec_tag {
   char *name;
   fastcall int (*exec)(cpu_ppc_t *,ppc_insn_t);
   m_uint32_t mask,value;
   int instr_type;
   m_uint64_t count;
};

/* Get a rotation mask */
static forced_inline m_uint32_t ppc32_rotate_mask(m_uint32_t mb,m_uint32_t me)
{
   m_uint32_t mask;

   mask = (0xFFFFFFFFU >> mb) ^ ((0xFFFFFFFFU >> me) >> 1);
   
   if (me < mb)
      mask = ~mask;
        
   return(mask);
}

/* Initialize instruction lookup table */
void ppc32_exec_create_ilt(void);

/* Dump statistics */
void ppc32_dump_stats(cpu_ppc_t *cpu);

/* Execute a page */
fastcall int ppc32_exec_page(cpu_ppc_t *cpu);

/* Execute a single instruction (external) */
fastcall int ppc32_exec_single_insn_ext(cpu_ppc_t *cpu,ppc_insn_t insn);

/* Execute a single instruction */
int ppc32_exec_single_instruction(cpu_ppc_t *cpu,ppc_insn_t instruction);

/* Fetch an instruction */
int ppc32_exec_fetch(cpu_ppc_t *cpu,m_uint32_t ia,
                                          ppc_insn_t *insn);

#endif
