/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * PPC32 JIT compiler.
 */

#ifndef __PPC32_JIT_H__
#define __PPC32_JIT_H__

#include "utils.h"
#include "sbox.h"

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

/* Size of hash for IA lookup */
#define PPC_JIT_IA_HASH_BITS    17
#define PPC_JIT_IA_HASH_MASK    ((1 << PPC_JIT_IA_HASH_BITS) - 1)
#define PPC_JIT_IA_HASH_SIZE    (1 << PPC_JIT_IA_HASH_BITS)

/* Size of hash for physical lookup */
#define PPC_JIT_PHYS_HASH_BITS  16
#define PPC_JIT_PHYS_HASH_MASK  ((1 << PPC_JIT_PHYS_HASH_BITS) - 1)
#define PPC_JIT_PHYS_HASH_SIZE  (1 << PPC_JIT_PHYS_HASH_BITS)

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
   ppc_insn_t *ppc_code;
   u_int ppc_trans_pos;
   u_int jit_chunk_pos;
   u_char *jit_ptr;
   insn_exec_page_t *jit_buffer;
   insn_exec_page_t *jit_chunks[PPC_JIT_MAX_CHUNKS];
   struct ppc32_jit_patch_table *patch_table;
   ppc32_jit_tcb_t *prev,*next;

   m_uint32_t phys_page;
   m_uint32_t phys_hash;
   ppc32_jit_tcb_t **phys_pprev,*phys_next;

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

   offset = (vaddr & PPC32_MIN_PAGE_IMASK) >> 2;
   return(b->jit_insn_ptr[offset]);
}

/* Check if the specified address belongs to the specified block */
static forced_inline 
int ppc32_jit_tcb_local_addr(ppc32_jit_tcb_t *block,m_uint32_t vaddr,
                             u_char **jit_addr)
{
   if ((vaddr & PPC32_MIN_PAGE_MASK) == block->start_ia) {
      *jit_addr = ppc32_jit_tcb_get_host_ptr(block,vaddr);
      return(1);
   }

   return(0);
}

/* Check if PC register matches the compiled block virtual address */
static forced_inline 
int ppc32_jit_tcb_match(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block)
{
   m_uint32_t vpage;

   vpage = cpu->ia & ~PPC32_MIN_PAGE_IMASK;
   return(block->start_ia == vpage);
}

/* Compute the hash index for the specified IA value */
static forced_inline m_uint32_t ppc32_jit_get_ia_hash(m_uint32_t ia)
{
   m_uint32_t page_hash;

   page_hash = sbox_u32(ia >> PPC32_MIN_PAGE_SHIFT);
   return((page_hash ^ (page_hash >> 14)) & PPC_JIT_IA_HASH_MASK);
}

/* Compute the hash index for the specified physical page */
static forced_inline m_uint32_t ppc32_jit_get_phys_hash(m_uint32_t phys_page)
{
   m_uint32_t page_hash;

   page_hash = sbox_u32(phys_page);
   return((page_hash ^ (page_hash >> 12)) & PPC_JIT_PHYS_HASH_MASK);
}

/* Find the JIT block matching a physical page */
static inline ppc32_jit_tcb_t *
ppc32_jit_find_by_phys_page(cpu_ppc_t *cpu,m_uint32_t phys_page)
{
   m_uint32_t page_hash =  ppc32_jit_get_phys_hash(phys_page);
   ppc32_jit_tcb_t *block;
   
   for(block=cpu->exec_phys_map[page_hash];block;block=block->phys_next)
      if (block->phys_page == phys_page)
         return block;

   return NULL;
}

/* Virtual Breakpoint */
void ppc32_emit_breakpoint(ppc32_jit_tcb_t *b);

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
