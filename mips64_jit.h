/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * MIPS64 JIT compiler.
 */

#ifndef __MIPS64_JIT_H__
#define __MIPS64_JIT_H__

#include "utils.h"

/* Size of executable page area (in Mb) */
#ifndef __CYGWIN__
#define MIPS_EXEC_AREA_SIZE  64
#else
#define MIPS_EXEC_AREA_SIZE  16
#endif

/* Buffer size for JIT code generation */
#define MIPS_JIT_BUFSIZE     32768

/* Maximum number of X86 chunks */
#define MIPS_JIT_MAX_CHUNKS  32

/* Instruction jump patch */
struct mips64_insn_patch {
   u_char *jit_insn;
   m_uint64_t mips_pc;
};

/* Instruction patch table */
#define MIPS64_INSN_PATCH_TABLE_SIZE  32

struct mips64_jit_patch_table {
   struct mips64_insn_patch patches[MIPS64_INSN_PATCH_TABLE_SIZE];
   u_int cur_patch;
   struct mips64_jit_patch_table *next;
};

/* MIPS64 translated code block */
struct mips64_jit_tcb {
   m_uint64_t start_pc;
   u_char **jit_insn_ptr;
   m_uint64_t acc_count;
   m_uint32_t phys_page;
   mips_insn_t *mips_code;
   u_int mips_trans_pos;
   u_int jit_chunk_pos;
   u_char *jit_ptr;
   insn_exec_page_t *jit_buffer;
   insn_exec_page_t *jit_chunks[MIPS_JIT_MAX_CHUNKS];
   struct mips64_jit_patch_table *patch_table;
   mips64_jit_tcb_t *prev,*next;
#if DEBUG_BLOCK_TIMESTAMP
   m_uint64_t tm_first_use,tm_last_use;
#endif
};

/* MIPS instruction recognition */
struct mips64_insn_tag {
   int (*emit)(cpu_mips_t *cpu,mips64_jit_tcb_t *,mips_insn_t);
   m_uint32_t mask,value;
   int delay_slot;
};

/* MIPS jump instruction (for block scan) */
struct mips64_insn_jump {
   char *name;
   m_uint32_t mask,value;
   int offset_bits;
   int relative;
};

/* Get the JIT instruction pointer in a translated block */
static forced_inline 
u_char *mips64_jit_tcb_get_host_ptr(mips64_jit_tcb_t *b,m_uint64_t vaddr)
{
   m_uint64_t offset;

   offset = (vaddr - b->start_pc) >> 2;
   return(b->jit_insn_ptr[offset]);
}

/* Check if there are pending IRQ */
extern void mips64_check_pending_irq(mips64_jit_tcb_t *b);

/* Initialize instruction lookup table */
void mips64_jit_create_ilt(void);

/* Initialize the JIT structure */
int mips64_jit_init(cpu_mips_t *cpu);

/* Flush the JIT */
u_int mips64_jit_flush(cpu_mips_t *cpu,u_int threshold);

/* Shutdown the JIT */
void mips64_jit_shutdown(cpu_mips_t *cpu);

/* Fetch a MIPS instruction and emit corresponding x86 translated code */
struct mips64_insn_tag *mips64_jit_fetch_and_emit(cpu_mips_t *cpu,
                                                  mips64_jit_tcb_t *block,
                                                  int delay_slot);

/* Record a patch to apply in a compiled block */
int mips64_jit_tcb_record_patch(mips64_jit_tcb_t *block,u_char *x86_ptr,
                                m_uint64_t vaddr);

/* Free an instruction block */
void mips64_jit_tcb_free(cpu_mips_t *cpu,mips64_jit_tcb_t *block,
                         int list_removal);

/* Check if the specified address belongs to the specified block */
int mips64_jit_tcb_local_addr(mips64_jit_tcb_t *block,m_uint64_t vaddr,
                              u_char **haddr);

/* Execute compiled MIPS code */
void *mips64_jit_run_cpu(cpu_gen_t *cpu);

/* Set the Pointer Counter (PC) register */
void mips64_set_pc(mips64_jit_tcb_t *b,m_uint64_t new_pc);

/* Set the Return Address (RA) register */
void mips64_set_ra(mips64_jit_tcb_t *b,m_uint64_t ret_pc);

/* Single-step operation */
void mips64_emit_single_step(mips64_jit_tcb_t *b,mips_insn_t insn);

/* Virtual Breakpoint */
void mips64_emit_breakpoint(mips64_jit_tcb_t *b);

/* Emit unhandled instruction code */
int mips64_emit_invalid_delay_slot(mips64_jit_tcb_t *b);

/* 
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
void mips64_inc_cp0_count_reg(mips64_jit_tcb_t *b);

/* Increment the number of executed instructions (performance debugging) */
void mips64_inc_perf_counter(mips64_jit_tcb_t *b);

#endif
