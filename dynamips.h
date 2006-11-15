/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DYNAMIPS_H__
#define __DYNAMIPS_H__

#include <libelf.h>

#include "utils.h"

/* Debugging flags */
#define DEBUG_BLOCK_SCAN       0
#define DEBUG_BLOCK_COMPILE    0
#define DEBUG_BLOCK_PATCH      0
#define DEBUG_BLOCK_CHUNK      0
#define DEBUG_BLOCK_TIMESTAMP  0   /* block timestamping (little overhead) */
#define DEBUG_SYM_TREE         0   /* use symbol tree (slow) */
#define DEBUG_MTS_MAP_DEV      0
#define DEBUG_MTS_MAP_VIRT     0
#define DEBUG_MTS_ACC_U        1   /* undefined memory */
#define DEBUG_MTS_ACC_T        1   /* tlb exception */
#define DEBUG_MTS_ACC_AE       1   /* address error exception */
#define DEBUG_MTS_DEV          0   /* debugging for device access */
#define DEBUG_MTS_STATS        1   /* MTS64 cache performance */
#define DEBUG_INSN_PERF_CNT    0   /* Instruction performance counter */
#define DEBUG_BLOCK_PERF_CNT   0   /* Block performance counter */
#define DEBUG_TLB_ACTIVITY     0 
#define DEBUG_SYSCALL          0
#define DEBUG_CACHE            0
#define DEBUG_JR0              0   /* Debug register jumps to 0 */

/* Feature flags */
#define MEMLOG_ENABLE          0   /* Memlogger (MTS ASM must be disabled) */
#define BREAKPOINT_ENABLE      0   /* Virtual Breakpoints */
#define NJM_STATS_ENABLE       1   /* Non-JIT mode stats (little overhead) */
#define MTSASM_ENABLE          1   /* Optimized-assembly MTS */

/* Size of executable page area (in Mb) */
#ifndef __CYGWIN__
#define MIPS_EXEC_AREA_SIZE  64
#else
#define MIPS_EXEC_AREA_SIZE  16
#endif

/* Buffer size for JIT code generation */
#define MIPS_JIT_BUFSIZE  32768

/* Maximum number of X86 chunks */
#define INSN_MAX_CHUNKS   32

/* Translated block function pointer */
typedef m_uint64_t (*insn_tblock_fptr)(void);

/* Instruction jump patch */
struct insn_patch {
   u_char *jit_insn;
   m_uint64_t mips_pc;
};

/* Instruction patch table */
#define INSN_PATCH_TABLE_SIZE  32

struct insn_patch_table {
   struct insn_patch patches[INSN_PATCH_TABLE_SIZE];
   u_int cur_patch;
   struct insn_patch_table *next;
};

/* Exec page */
struct insn_exec_page {
   u_char *ptr;
   insn_exec_page_t *next;
};

/* Instruction block */
struct insn_block {
   m_uint64_t start_pc;
   u_char **jit_insn_ptr;
   m_uint64_t acc_count;
   m_uint32_t phys_page;
   mips_insn_t *mips_code;
   u_int mips_trans_pos;
   u_int jit_chunk_pos;
   u_char *jit_ptr;
   insn_exec_page_t *jit_buffer;
   insn_exec_page_t *jit_chunks[INSN_MAX_CHUNKS];
   struct insn_patch_table *patch_table;
   insn_block_t *prev,*next;
#if DEBUG_BLOCK_TIMESTAMP
   m_uint64_t tm_first_use,tm_last_use;
#endif
};

/* MIPS instruction recognition */
struct insn_tag {
   int (*emit)(cpu_mips_t *cpu,struct insn_block *,mips_insn_t);
   m_uint32_t mask,value;
   int delay_slot;
};

/* MIPS jump instruction (for block scan) */
struct insn_jump {
   char *name;
   m_uint32_t mask,value;
   int offset_bits;
   int relative;
};

/* Symbol */
struct symbol {
   m_uint64_t addr;
   char name[0];
};

/* ROM identification tag */
#define ROM_ID  0x1e94b3df

/* Global log file */
extern FILE *log_file;

/* Software version */
extern const char *sw_version;

/* Get the JIT instruction pointer in a compiled block */
static forced_inline 
u_char *insn_block_get_jit_ptr(struct insn_block *block,m_uint64_t vaddr)
{
   m_uint64_t offset;

   offset = (vaddr - block->start_pc) >> 2;
   return(block->jit_insn_ptr[offset]);
}

/* Check if there are pending IRQ */
extern void mips64_check_pending_irq(struct insn_block *b);

/* Initialize instruction lookup table */
void mips64_jit_create_ilt(void);

/* Initialize the JIT structure */
int mips64_jit_init(cpu_mips_t *cpu);

/* Flush the JIT */
u_int mips64_jit_flush(cpu_mips_t *cpu,u_int threshold);

/* Shutdown the JIT */
void mips64_jit_shutdown(cpu_mips_t *cpu);

/* Find the JIT code emitter for the specified MIPS instruction */
struct insn_tag *insn_tag_find(mips_insn_t ins);

/* Check if the specified MIPS instruction is a jump */
struct insn_jump *insn_jump_find(mips_insn_t ins);

/* Fetch a MIPS instruction and emit corresponding x86 translated code */
struct insn_tag *insn_fetch_and_emit(cpu_mips_t *cpu,struct insn_block *block,
                                     int delay_slot);

/* Record a patch to apply in a compiled block */
int insn_block_record_patch(struct insn_block *block,u_char *x86_ptr,
                            m_uint64_t vaddr);

/* Free an instruction block */
void insn_block_free(cpu_mips_t *cpu,insn_block_t *block,int list_removal);

/* Tree comparison function */
int insn_block_cmp(m_uint64_t *vaddr,struct insn_block *b);

/* Check if the specified address belongs to the specified block */
int insn_block_local_addr(struct insn_block *block,m_uint64_t vaddr,
                          u_char **x86_addr);

/* Execute a compiled MIPS code */
void *insn_block_execute(cpu_mips_t *cpu);

/* Dump the instruction block tree */
void insn_block_dump_tree(cpu_mips_t *cpu);

/* Delete all objects */
void dynamips_reset(void);

#endif
