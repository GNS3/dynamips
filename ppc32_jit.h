/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * PPC32 JIT compiler.
 */

#ifndef __PPC32_JIT_H__
#define __PPC32_JIT_H__

#include "utils.h"

/* Size of executable page area (in Mb) */
#ifndef __CYGWIN__
#define PPC_EXEC_AREA_SIZE  64
#else
#define PPC_EXEC_AREA_SIZE  16
#endif

/* Buffer size for JIT code generation */
#define PPC_JIT_BUFSIZE     32768

/* Maximum number of X86 chunks */
#define PPC_JIT_MAX_CHUNKS  32

/* Instruction jump patch */
struct ppc32_insn_patch {
   u_char *jit_insn;
   m_uint64_t ppc_ia;
};

/* Instruction patch table */
#define PPC32_INSN_PATCH_TABLE_SIZE  32

struct ppc32_jit_patch_table {
   struct ppc32_insn_patch patches[PPC32_INSN_PATCH_TABLE_SIZE];
   u_int cur_patch;
   struct ppc32_jit_patch_table *next;
};

/* PPC32 translated code block */
struct ppc32_jit_tcb {
   m_uint32_t start_ia;
   u_char **jit_insn_ptr;
   m_uint64_t acc_count;
   m_uint32_t phys_page;
   ppc_insn_t *ppc_code;
   u_int ppc_trans_pos;
   u_int jit_chunk_pos;
   u_char *jit_ptr;
   insn_exec_page_t *jit_buffer;
   insn_exec_page_t *jit_chunks[PPC_JIT_MAX_CHUNKS];
   struct ppc32_jit_patch_table *patch_table;
   ppc32_jit_tcb_t *prev,*next;
#if DEBUG_BLOCK_TIMESTAMP
   m_uint64_t tm_first_use,tm_last_use;
#endif
};

/* PPC instruction recognition */
struct ppc32_insn_tag {
   int (*emit)(cpu_ppc_t *cpu,ppc32_jit_tcb_t *,ppc_insn_t);
   m_uint32_t mask,value;
};

/* Get the JIT instruction pointer in a translated block */
static forced_inline 
u_char *ppc32_jit_tcb_get_host_ptr(ppc32_jit_tcb_t *b,m_uint32_t vaddr)
{
   m_uint32_t offset;

   offset = (vaddr - b->start_ia) >> 2;
   return(b->jit_insn_ptr[offset]);
}

/* Initialize instruction lookup table */
void ppc32_jit_create_ilt(void);

/* Initialize the JIT structure */
int ppc32_jit_init(cpu_ppc_t *cpu);

/* Flush the JIT */
u_int ppc32_jit_flush(cpu_ppc_t *cpu,u_int threshold);

/* Shutdown the JIT */
void ppc32_jit_shutdown(cpu_ppc_t *cpu);

/* Fetch a PowerPC instruction and emit corresponding translated code */
struct ppc32_insn_tag *ppc32_jit_fetch_and_emit(cpu_ppc_t *cpu,
                                                ppc32_jit_tcb_t *block);

/* Record a patch to apply in a compiled block */
int ppc32_jit_tcb_record_patch(ppc32_jit_tcb_t *block,u_char *jit_ptr,
                               m_uint32_t vaddr);

/* Free an instruction block */
void ppc32_jit_tcb_free(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block,
                        int list_removal);

/* Check if the specified address belongs to the specified block */
int ppc32_jit_tcb_local_addr(ppc32_jit_tcb_t *block,m_uint32_t vaddr,
                             u_char **jit_addr);

/* Execute compiled PowerPC code */
void *ppc32_jit_run_cpu(cpu_gen_t *gen);

#endif
