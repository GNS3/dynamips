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
#define DEBUG_INSN_ITRACE      0
#define DEBUG_SYM_TREE         0   /* use symbol tree (slow) */
#define DEBUG_MTS_MAP_DEV      0
#define DEBUG_MTS_MAP_VIRT     0
#define DEBUG_MTS_ACC_U        1   /* undefined memory */
#define DEBUG_MTS_ACC_T        1   /* tlb exception */
#define DEBUG_MTS_ACC_AE       1   /* address error exception */
#define DEBUG_MTS_DEV          0   /* debugging for device access */
#define DEBUG_TLB_ACTIVITY     0 
#define DEBUG_SYSCALL          0
#define DEBUG_CACHE            0
#define MEMLOG_ENABLE          0   /* Memlogger */
#define BREAKPOINT_ENABLE      0   /* Virtual Breakpoints */
#define NJM_STATS_ENABLE       1   /* Non-JIT mode stats (little overhead) */

/* Maximum of 512 instructions per block */
#define MIPS_MAX_BLOCK_INSN   512

/* Buffer size for JIT code generation */
#define MIPS_JIT_BUFSIZE  16384

/* Maximum number of X86 chunks */
#define INSN_MAX_CHUNKS   32

/* MIPS instruction */
typedef m_uint32_t mips_insn_t;

/* Translated block function pointer */
typedef m_uint64_t (*insn_tblock_fptr)(void);

/* Instruction jump patch */
struct insn_patch {
   u_char *jit_insn;
   m_uint64_t mips_pc;
};

/* Instruction patch table */
#define INSN_PATCH_TABLE_SIZE  16

struct insn_patch_table {
   struct insn_patch patches[INSN_PATCH_TABLE_SIZE];
   u_int cur_patch;
   struct insn_patch_table *next;
};

/* Instruction block */
typedef struct insn_block insn_block_t;
struct insn_block {
   m_uint64_t start_pc,end_pc;
   mips_insn_t *mips_code;
   u_int mips_code_len;
   u_int mips_trans_pos;
   u_int jit_chunk_pos;
   u_char **jit_insn_ptr;
   size_t jit_bufsize;
   u_char *jit_buffer,*jit_ptr;
   u_char *jit_chunks[INSN_MAX_CHUNKS];
   struct insn_patch_table *patch_table;
   m_uint64_t acc_count;
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

#define IOSEMU_ID  0x781a321f

/* RAM, ROM and NVRAM size, ELF entry point */
extern char *rom_filename;
extern u_int ram_size;
extern u_int rom_size;
extern u_int nvram_size;
extern u_int conf_reg;
extern u_int clock_divisor;
extern m_uint32_t ios_entry_point;

extern int insn_itrace;
extern int jit_use;
extern FILE *log_file;
extern volatile int vm_running;

/* Console and AUX ports VTTY type and parameters */
extern int vtty_con_type,vtty_aux_type;
extern int vtty_con_tcp_port,vtty_aux_tcp_port;

/* Locate an instruction block given a MIPS virtual address */
static forced_inline 
struct insn_block *insn_block_locate(cpu_mips_t *cpu,m_uint64_t vaddr)
{
   return(rbtree_lookup(cpu->insn_block_tree,&vaddr));
}

/* Locate an instruction block given a MIPS virtual address */
static forced_inline 
struct insn_block *insn_block_locate_fast(cpu_mips_t *cpu,m_uint64_t vaddr)
{
   rbtree_tree *tree;
   register rbtree_node *node,*nil;
   struct insn_block *b;

   tree = cpu->insn_block_tree;
   node = tree->root;
   nil = &tree->nil;

   while(node != nil)
   {
      b = (struct insn_block *)node->key;

      if (vaddr < b->start_pc) {
         node = node->left;
         continue;
      }

      if (vaddr >= b->end_pc) {
         node = node->right;
         continue;
      }

      return b;
   }

   return(NULL);
}

/* Get the JIT instruction pointer in a compiled block */
static forced_inline 
u_char *insn_block_get_jit_ptr(struct insn_block *block,m_uint64_t vaddr)
{
   m_uint64_t offset;

   offset = (vaddr - block->start_pc) >> 2;
   return(block->jit_insn_ptr[offset]);
}

/* Get the JIT instruction ptr for a virtual address (NULL if not found) */
static forced_inline u_char *insn_get_jit_ptr(cpu_mips_t *cpu,m_uint64_t vaddr,
                                              struct insn_block **block)
{
   if ((*block = insn_block_locate_fast(cpu,vaddr)) == NULL)
      return NULL;
 
   return(insn_block_get_jit_ptr(*block,vaddr));
}

/* Check if there are pending IRQ */
extern void mips64_check_pending_irq(struct insn_block *b);

/* Initialize instruction lookup table */
void mips64_jit_create_ilt(void);

/* Find the JIT code emitter for the specified MIPS instruction */
struct insn_tag *insn_tag_find(mips_insn_t ins);

/* Check if the specified MIPS instruction is a jump */
struct insn_jump *insn_jump_find(mips_insn_t ins);

/* Fetch a MIPS instruction and emit corresponding x86 translated code */
struct insn_tag *insn_fetch_and_emit(cpu_mips_t *cpu,struct insn_block *block,
                                     int delay_slot);

/* Create a instruction block */
struct insn_block *insn_block_create(cpu_mips_t *cpu,m_uint64_t vaddr);

/* Record a patch to apply in a compiled block */
int insn_block_record_patch(struct insn_block *block,u_char *x86_ptr,
                            m_uint64_t vaddr);

/* Tree comparison function */
int insn_block_cmp(m_uint64_t *vaddr,struct insn_block *b);

/* Check if the specified address belongs to the specified block */
int insn_block_local_addr(struct insn_block *block,m_uint64_t vaddr,
                          u_char **x86_addr);

/* Execute a compiled MIPS code */
void *insn_block_execute(cpu_mips_t *cpu);

/* Dump the instruction block tree */
void insn_block_dump_tree(cpu_mips_t *cpu);

#endif
