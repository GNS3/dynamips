/*
 * Cisco router simulation platform.
 * Copyright (c) 2008 Christophe Fillot (cf@utc.fr)
 *
 * Translation Sharing Groups.
 */

#ifndef __TSG_H__
#define __TSG_H__

#include "utils.h"
#include "vm.h"

/* Checksum type */
typedef m_uint64_t tsg_checksum_t;

/* Instruction jump patch */
struct insn_patch {
   struct insn_patch *next;
   u_char *jit_insn;
   m_uint64_t vaddr;
};

/* Instruction patch table */
#define INSN_PATCH_TABLE_SIZE  32

struct insn_patch_table {   
   struct insn_patch_table *next;
   struct insn_patch patches[INSN_PATCH_TABLE_SIZE];
   u_int cur_patch;
};

/* Flags for CPU Tranlation Blocks (TB) */
#define TB_FLAG_SMC      0x01  /* Self-modifying code */
#define TB_FLAG_RECOMP   0x02  /* Page being recompiled */
#define TB_FLAG_NOJIT    0x04  /* Page not supported for JIT */
#define TB_FLAG_VALID    0x08

/* Don't use translated code to execute the page */
#define TB_FLAG_NOTRANS  (TB_FLAG_SMC|TB_FLAG_RECOMP|TB_FLAG_NOJIT)

/* CPU Translation Block */
struct cpu_tb {
   u_int flags;
   m_uint64_t vaddr;
   m_uint32_t exec_state;
   tsg_checksum_t checksum;

   m_uint64_t acc_count;   
   void *target_code;

   cpu_tb_t **tb_pprev,*tb_next;
   
   /* Translated Code (can be shared among multiple CPUs) */
   cpu_tc_t *tc;
   cpu_tb_t **tb_dl_pprev,*tb_dl_next;
   
   /* Virtual page hash */
   m_uint32_t virt_hash;
   
   /* Physical page information */
   m_uint32_t phys_page;
   m_uint32_t phys_hash;
   cpu_tb_t **phys_pprev,*phys_next;

#if DEBUG_BLOCK_TIMESTAMP
   m_uint64_t tm_first_use,tm_last_use;
#endif
};

/* Maximum exec pages per TC descriptor */ 
#define TC_MAX_CHUNKS  32

/* TC descriptor flags */
#define TC_FLAG_REMOVAL  0x01  /* Descriptor marked for removal */
#define TC_FLAG_VALID    0x02

/* CPU Translated Code */
struct cpu_tc {
   tsg_checksum_t checksum;
   m_uint64_t vaddr;
   m_uint32_t exec_state;
   u_int flags;

   /* Temporarily used during the translation */   
   void *target_code;

   u_char **jit_insn_ptr;
   u_int jit_chunk_pos;
   insn_exec_page_t *jit_chunks[TC_MAX_CHUNKS];
   
   /* Current JIT buffer */
   insn_exec_page_t *jit_buffer;
   u_char *jit_ptr;
   
   /* Patch table */
   struct insn_patch_table *patch_table;

   /* Translation position in target code */
   u_int trans_pos;
   
   /* 1024 instructions per page, one bit per instruction */
   m_uint32_t target_bitmap[32];
   m_uint32_t target_undef_cnt;

   /* Reference count */
   int ref_count;
   
   /* TB list referring to this translated code / exec pages */
   cpu_tb_t *tb_list;
   
   /* Linked list for hash table referencing */
   cpu_tc_t **hash_pprev,*hash_next;
   
   /* Linked list for single-CPU referencement (ref_count=1) */
   cpu_tc_t **sc_pprev,*sc_next;
};

#define TC_TARGET_BITMAP_INDEX(x) (((x) >> 7) & 0x1F)
#define TC_TARGET_BITMAP_POS(x)   (((x) >> 2) & 0x1F)

/* Mark the specified vaddr as a target for further recompiling */
static inline void tc_set_target_bit(cpu_tc_t *tc,m_uint32_t vaddr)
{
   int index,pos;

   index = TC_TARGET_BITMAP_INDEX(vaddr);
   pos   = TC_TARGET_BITMAP_POS(vaddr);

   tc->target_bitmap[index] |= 1 << pos;
}

/* Returns TRUE if the specified vaddr is in the target bitmap */
static inline int tc_get_target_bit(cpu_tc_t *tc,m_uint32_t vaddr)
{
   int index,pos;

   index = TC_TARGET_BITMAP_INDEX(vaddr);
   pos   = TC_TARGET_BITMAP_POS(vaddr);

   return(tc->target_bitmap[index] & (1 << pos));
}

/* Get the JIT instruction pointer in a translated code */
static forced_inline u_char *tc_get_host_ptr(cpu_tc_t *tc,m_uint64_t vaddr)
{
   m_uint32_t offset;

   offset = (vaddr & VM_PAGE_IMASK) >> 2;
   return(tc->jit_insn_ptr[offset]);
}

/* Get the JIT instruction pointer in a translated block */
static forced_inline u_char *tb_get_host_ptr(cpu_tb_t *tb,m_uint64_t vaddr)
{
   return(tc_get_host_ptr(tb->tc,vaddr));
}

/* Lookup return codes */
#define TSG_LOOKUP_NEW     0
#define TSG_LOOKUP_SHARED  1

/* Translation sharing group statistics */
struct tsg_stats {
   u_int total_tc;
   u_int shared_tc;
   u_int shared_pages;
};

enum {
   CPU_JIT_DISABLE_CPU = 0,
   CPU_JIT_ENABLE_CPU,
};

/* Allocate a new TCB descriptor */
cpu_tc_t *tc_alloc(cpu_gen_t *cpu,m_uint64_t vaddr,m_uint32_t exec_state);

/* Bind a CPU to a TSG - If the group isn't specified, create one */
int tsg_bind_cpu(cpu_gen_t *cpu);

/* Unbind a CPU from a TSG - release all resources used */
int tsg_unbind_cpu(cpu_gen_t *cpu);

/* Create a JIT chunk */
int tc_alloc_jit_chunk(cpu_gen_t *cpu,cpu_tc_t *tc);

/* Allocate a new TC descriptor */
cpu_tc_t *tc_alloc(cpu_gen_t *cpu,m_uint64_t vaddr,m_uint32_t exec_state);

/* Compute a checksum on a page */
tsg_checksum_t tsg_checksum_page(void *page,ssize_t size);

/* Try to find a TC descriptor to share generated code */
int tc_find_shared(cpu_gen_t *cpu,cpu_tb_t *tb);

/* Register a newly compiled TCB descriptor */
void tc_register(cpu_gen_t *cpu,cpu_tb_t *tb,cpu_tc_t *tc);

/* Remove all TC descriptors belonging to a single CPU (ie not shared) */
int tsg_remove_single_desc(cpu_gen_t *cpu);

/* Dump a TCB */
void tb_dump(cpu_tb_t *tb);

/* Dump a TCB descriptor */
void tc_dump(cpu_tc_t *tc);

/* Show statistics about all translation groups */
void tsg_show_stats(void);

/* Adjust the JIT buffer if its size is not sufficient */
int tc_adjust_jit_buffer(cpu_gen_t *cpu,cpu_tc_t *tc,
                         void (*set_jump)(u_char **insn,u_char *dst));

/* Record a patch to apply in a compiled block */
struct insn_patch *tc_record_patch(cpu_gen_t *cpu,cpu_tc_t *tc,
                                   u_char *jit_ptr,m_uint64_t vaddr);

/* Apply all patches */
int tc_apply_patches(cpu_tc_t *tc,void (*set_patch)(u_char *insn,u_char *dst));

/* Free the patch table */
void tc_free_patches(cpu_tc_t *tc);

/* Initialize the JIT structures of a CPU */
int cpu_jit_init(cpu_gen_t *cpu,size_t virt_hash_size,size_t phys_hash_size);

/* Shutdown the JIT structures of a CPU */
void cpu_jit_shutdown(cpu_gen_t *cpu);

/* Allocate a new Translation Block */
cpu_tb_t *tb_alloc(cpu_gen_t *cpu,m_uint64_t vaddr,u_int exec_state);

/* Free a Translation Block */
void tb_free(cpu_gen_t *cpu,cpu_tb_t *tb);

/* Enable a Tranlsation Block  */
void tb_enable(cpu_gen_t *cpu,cpu_tb_t *tb);

/* Flush all TCB of a virtual CPU */
void cpu_jit_tcb_flush_all(cpu_gen_t *cpu);

/* Mark a TB as containing self-modifying code */
void tb_mark_smc(cpu_gen_t *cpu,cpu_tb_t *tb);

/* Handle write access on an executable page */
void cpu_jit_write_on_exec_page(cpu_gen_t *cpu,
                                m_uint32_t wr_phys_page,
                                m_uint32_t wr_hp,
                                m_uint32_t ip_phys_page);

#endif
