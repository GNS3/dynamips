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
#define PPC_JIT_MAX_CHUNKS  64

/* Size of hash for IA lookup */
#define PPC_JIT_IA_HASH_BITS    17
#define PPC_JIT_IA_HASH_MASK    ((1 << PPC_JIT_IA_HASH_BITS) - 1)
#define PPC_JIT_IA_HASH_SIZE    (1 << PPC_JIT_IA_HASH_BITS)

/* Size of hash for physical lookup */
#define PPC_JIT_PHYS_HASH_BITS  16
#define PPC_JIT_PHYS_HASH_MASK  ((1 << PPC_JIT_PHYS_HASH_BITS) - 1)
#define PPC_JIT_PHYS_HASH_SIZE  (1 << PPC_JIT_PHYS_HASH_BITS)

#define PPC_JIT_TARGET_BITMAP_INDEX(x) (((x) >> 7) & 0x1F)
#define PPC_JIT_TARGET_BITMAP_POS(x)   (((x) >> 2) & 0x1F)

/* Instruction jump patch */
struct ppc32_insn_patch {
   struct ppc32_insn_patch *next;
   u_char *jit_insn;
   m_uint32_t ppc_ia;
};

/* Instruction patch table */
#define PPC32_INSN_PATCH_TABLE_SIZE  32

struct ppc32_jit_patch_table {   
   struct ppc32_jit_patch_table *next;
   struct ppc32_insn_patch patches[PPC32_INSN_PATCH_TABLE_SIZE];
   u_int cur_patch;
};

#define PPC32_JIT_TCB_FLAG_NO_FLUSH  0x2   /* No flushing */

/* PPC32 translated code block */
struct ppc32_jit_tcb {
   u_int flags;
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
   
   /* 1024 instructions per page, one bit per instruction */
   m_uint32_t target_bitmap[32];
   m_uint32_t target_undef_cnt;

#if DEBUG_BLOCK_TIMESTAMP
   m_uint64_t tm_first_use,tm_last_use;
#endif
};

/* PPC instruction recognition */
struct ppc32_insn_tag {
   int (*emit)(cpu_ppc_t *cpu,ppc32_jit_tcb_t *,ppc_insn_t);
   m_uint32_t mask,value;
};

/* Mark the specified IA as a target for further recompiling */
static inline void 
ppc32_jit_tcb_set_target_bit(ppc32_jit_tcb_t *b,m_uint32_t ia)
{
   int index,pos;

   index = PPC_JIT_TARGET_BITMAP_INDEX(ia);
   pos   = PPC_JIT_TARGET_BITMAP_POS(ia);

   b->target_bitmap[index] |= 1 << pos;
}

/* Returns TRUE if the specified IA is in the target bitmap */
static inline int
ppc32_jit_tcb_get_target_bit(ppc32_jit_tcb_t *b,m_uint32_t ia)
{
   int index,pos;

   index = PPC_JIT_TARGET_BITMAP_INDEX(ia);
   pos   = PPC_JIT_TARGET_BITMAP_POS(ia);

   return(b->target_bitmap[index] & (1 << pos));
}

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

/* ======================================================================== */
/* JIT emit operations (generic).                                           */
/* ======================================================================== */

/* Indicate registers modified by ppc32_update_cr() functions */
extern void ppc32_update_cr_set_altered_hreg(cpu_ppc_t *cpu);

/* Set opcode */
static inline void ppc32_op_set(cpu_ppc_t *cpu,jit_op_t *op)
{
   cpu_gen_t *c = cpu->gen;
   *c->jit_op_current = op;
   c->jit_op_current = &op->next;
}

/* EMIT_BASIC_OPCODE */
static inline void ppc32_op_emit_basic_opcode(cpu_ppc_t *cpu,u_int opcode)
{
   jit_op_t *op = jit_op_get(cpu->gen,0,opcode);
   ppc32_op_set(cpu,op);
}

/* Trash the specified host register */
static inline void ppc32_op_emit_alter_host_reg(cpu_ppc_t *cpu,int host_reg)
{
   jit_op_t *op = jit_op_get(cpu->gen,0,JIT_OP_ALTER_HOST_REG);
   op->param[0] = host_reg;
   ppc32_op_set(cpu,op);
}

/* EMIT_INSN_OUTPUT */
static inline jit_op_t *
ppc32_op_emit_insn_output(cpu_ppc_t *cpu,u_int size_index,char *insn_name)
{
   jit_op_t *op = jit_op_get(cpu->gen,size_index,JIT_OP_INSN_OUTPUT);
   op->arg_ptr = NULL;
   op->insn_name = insn_name;
   ppc32_op_set(cpu,op);
   return op;
}

/* EMIT_LOAD_GPR */
static inline 
void ppc32_op_emit_load_gpr(cpu_ppc_t *cpu,int host_reg,int ppc_reg)
{
   jit_op_t *op = jit_op_get(cpu->gen,0,JIT_OP_LOAD_GPR);
   op->param[0] = host_reg;
   op->param[1] = ppc_reg;
   op->param[2] = host_reg;
   ppc32_op_set(cpu,op);
}

/* EMIT_STORE_GPR */
static inline 
void ppc32_op_emit_store_gpr(cpu_ppc_t *cpu,int ppc_reg,int host_reg)
{
   jit_op_t *op = jit_op_get(cpu->gen,0,JIT_OP_STORE_GPR);
   op->param[0] = host_reg;
   op->param[1] = ppc_reg;
   op->param[2] = host_reg;
   ppc32_op_set(cpu,op);
}

/* EMIT_UPDATE_FLAGS */
static inline 
void ppc32_op_emit_update_flags(cpu_ppc_t *cpu,int field,int is_signed)
{
   jit_op_t *op = jit_op_get(cpu->gen,0,JIT_OP_UPDATE_FLAGS);

   op->param[0] = field;
   op->param[1] = is_signed;

   ppc32_op_set(cpu,op);
   ppc32_update_cr_set_altered_hreg(cpu);
}

/* EMIT_REQUIRE_FLAGS */
static inline void ppc32_op_emit_require_flags(cpu_ppc_t *cpu,int field)
{
   jit_op_t *op = jit_op_get(cpu->gen,0,JIT_OP_REQUIRE_FLAGS);
   op->param[0] = field;
   ppc32_op_set(cpu,op);
}

/* EMIT_BRANCH_TARGET */
static inline void ppc32_op_emit_branch_target(cpu_ppc_t *cpu,
                                               ppc32_jit_tcb_t *b,
                                               m_uint32_t ia)
{
   if ((ia & PPC32_MIN_PAGE_MASK) == b->start_ia) {
      cpu_gen_t *c = cpu->gen;
      jit_op_t *op = jit_op_get(c,0,JIT_OP_BRANCH_TARGET);
      u_int pos = (ia & PPC32_MIN_PAGE_IMASK) >> 2;

      /* Insert in head */
      op->next = c->jit_op_array[pos];
      c->jit_op_array[pos] = op;
   }
}

/* EMIT_SET_HOST_REG_IMM32 */
static inline void 
ppc32_op_emit_set_host_reg_imm32(cpu_ppc_t *cpu,int reg,m_uint32_t val)
{
   jit_op_t *op = jit_op_get(cpu->gen,0,JIT_OP_SET_HOST_REG_IMM32);
   op->param[0] = reg;
   op->param[1] = val;
   ppc32_op_set(cpu,op);
}

/* ======================================================================== */
/* JIT operations with implementations specific to target CPU */
void ppc32_op_insn_output(ppc32_jit_tcb_t *b,jit_op_t *op);
void ppc32_op_load_gpr(ppc32_jit_tcb_t *b,jit_op_t *op);
void ppc32_op_store_gpr(ppc32_jit_tcb_t *b,jit_op_t *op);
void ppc32_op_update_flags(ppc32_jit_tcb_t *b,jit_op_t *op);
void ppc32_op_move_host_reg(ppc32_jit_tcb_t *b,jit_op_t *op);
void ppc32_op_set_host_reg_imm32(ppc32_jit_tcb_t *b,jit_op_t *op);

/* Set the Instruction Address (IA) register */
void ppc32_set_ia(u_char **ptr,m_uint32_t new_ia);

/* Jump to the next page */
void ppc32_set_page_jump(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b);

/* Increment the number of executed instructions (performance debugging) */
void ppc32_inc_perf_counter(cpu_ppc_t *cpu);

/* ======================================================================== */

/* Virtual Breakpoint */
void ppc32_emit_breakpoint(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b);

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
int ppc32_jit_tcb_record_patch(ppc32_jit_tcb_t *block,jit_op_t *iop,
                               u_char *jit_ptr,m_uint32_t vaddr);

/* Free an instruction block */
void ppc32_jit_tcb_free(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block,
                        int list_removal);

/* Check if the specified address belongs to the specified block */
int ppc32_jit_tcb_local_addr(ppc32_jit_tcb_t *block,m_uint32_t vaddr,
                             u_char **jit_addr);

/* Recompile a page */
int ppc32_jit_tcb_recompile(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block);

/* Execute compiled PowerPC code */
void *ppc32_jit_run_cpu(cpu_gen_t *gen);

/* Start register allocation sequence */
void ppc32_jit_start_hreg_seq(cpu_ppc_t *cpu,char *insn);

/* Close register allocation sequence */
void ppc32_jit_close_hreg_seq(cpu_ppc_t *cpu);

/* Insert a reg map as head of list (as MRU element) */
void ppc32_jit_insert_hreg_mru(cpu_ppc_t *cpu,struct hreg_map *map);

/* Allocate an host register */
int ppc32_jit_alloc_hreg(cpu_ppc_t *cpu,int ppc_reg);

/* Force allocation of an host register */
int ppc32_jit_alloc_hreg_forced(cpu_ppc_t *cpu,int hreg);

/* Initialize register mapping */
void ppc32_jit_init_hreg_mapping(cpu_ppc_t *cpu);

#endif
