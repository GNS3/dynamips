/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * MIPS64 JIT compiler.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>

#include ARCH_INC_FILE

#include "rbtree.h"
#include "cp0.h"
#include "memory.h"
#include "cpu.h"
#include "device.h"
#include "mips64_exec.h"
#include "insn_lookup.h"
#include "ptask.h"

#define ibh_index(x) (((x) ^ ((x) >> 16)) & 0xfff)

extern rbtree_tree *sym_tree;

#if DEBUG_BLOCK_TIMESTAMP
static volatile m_uint64_t jit_jiffies = 0;
#endif

/* MIPS jump instructions for block scan */
struct insn_jump mips64_insn_jumps[] = {
   { "b"       , 0xffff0000, 0x10000000, 16, 1 },
   { "bal"     , 0xffff0000, 0x04110000, 16, 1 },
   { "beq"     , 0xfc000000, 0x10000000, 16, 1 },
   { "beql"    , 0xfc000000, 0x50000000, 16, 1 },
   { "bgez"    , 0xfc1f0000, 0x04010000, 16, 1 },
   { "bgezl"   , 0xfc1f0000, 0x04030000, 16, 1 },
   { "bgezal"  , 0xfc1f0000, 0x04110000, 16, 1 },
   { "bgezall" , 0xfc1f0000, 0x04130000, 16, 1 },
   { "bgtz"    , 0xfc1f0000, 0x1c000000, 16, 1 },
   { "bgtzl"   , 0xfc1f0000, 0x5c000000, 16, 1 },
   { "blez"    , 0xfc1f0000, 0x18000000, 16, 1 },
   { "blezl"   , 0xfc1f0000, 0x58000000, 16, 1 },
   { "bltz"    , 0xfc1f0000, 0x04000000, 16, 1 },
   { "bltzl"   , 0xfc1f0000, 0x04020000, 16, 1 },
   { "bltzal"  , 0xfc1f0000, 0x04100000, 16, 1 },
   { "bltzall" , 0xfc1f0000, 0x04120000, 16, 1 },
   { "bne"     , 0xfc000000, 0x14000000, 16, 1 },
   { "bnel"    , 0xfc000000, 0x54000000, 16, 1 },
   { "j"       , 0xfc000000, 0x08000000, 26, 0 },
   { NULL      , 0x00000000, 0x00000000,  0, 0 },
};

/* Instruction Lookup Table */
static insn_lookup_t *ilt = NULL;

static void *mips64_jit_get_insn(int index)
{
   return(&mips64_insn_tags[index]);
}

static int mips64_jit_chk_lo(struct insn_tag *tag,int value)
{
   return((value & tag->mask) == (tag->value & 0xFFFF));
}

static int mips64_jit_chk_hi(struct insn_tag *tag,int value)
{
   return((value & (tag->mask >> 16)) == (tag->value >> 16));
}

/* Initialize instruction lookup table */
void mips64_jit_create_ilt(void)
{
   int i,count;

   for(i=0,count=0;mips64_insn_tags[i].emit;i++)
      count++;

   ilt = ilt_create(count,
                    (ilt_get_insn_cbk_t)mips64_jit_get_insn,
                    (ilt_check_cbk_t)mips64_jit_chk_lo,
                    (ilt_check_cbk_t)mips64_jit_chk_hi);
}

/* Insert specified address in instruction block hash table */
void mips64_jit_add_hash_addr(cpu_mips_t *cpu,m_uint64_t addr)
{
   struct insn_block *block;
   m_uint16_t index;
   
   block = insn_block_locate_fast(cpu,addr);

   if (likely(block != NULL)) {
      index = ibh_index(addr >> 2);
      cpu->insn_block_hash[index] = block;
   }
}

/* Remove specified block from instruction block hash table */
void mips64_jit_remove_hash_block(cpu_mips_t *cpu,struct insn_block *block)
{
   int index;

   for(index=0;index<4096;index++) {
      if (cpu->insn_block_hash[index] == block)
         cpu->insn_block_hash[index] = NULL;
   }
}

/* Dump instruction block hash table */
void mips64_jit_dump_hash(cpu_mips_t *cpu)
{
   struct insn_block *block;
   int i;

   for(i=0;i<4096/*65536*/;i++) {
      block = cpu->insn_block_hash[i];
      if (!block)
         m_log("IBHASH","Index %d: no block\n",i);
      else {
         m_log("IBHASH","Index %d: Block 0x%llx, count=%llu\n",
               i,block->start_pc,block->acc_count);
      }
   }
}

/* Find the JIT code emitter for the specified MIPS instruction */
struct insn_tag *insn_tag_find(mips_insn_t ins)
{
   struct insn_tag *tag = NULL;
   int index;

   index = ilt_lookup(ilt,ins);
   tag = mips64_jit_get_insn(index);
   return tag;
}

/* Check if the specified MIPS instruction is a jump */
struct insn_jump *insn_jump_find(mips_insn_t ins)
{
   struct insn_jump *jump = NULL;
   int i;

   for(i=0;mips64_insn_jumps[i].name;i++)
      if ((ins & mips64_insn_jumps[i].mask) == mips64_insn_jumps[i].value) {
         jump = &mips64_insn_jumps[i];
         break;
      }

   return(jump);
}

/* Fetch a MIPS instruction */
static inline mips_insn_t insn_fetch(insn_block_t *b)
{
   return(vmtoh32(b->mips_code[b->mips_trans_pos]));
}

/* Emit a breakpoint if necessary */
static void insn_emit_breakpoint(cpu_mips_t *cpu,insn_block_t *b)
{
   m_uint64_t pc;
   int i;

   pc = b->start_pc+((b->mips_trans_pos-1)<<2);

   for(i=0;i<MIPS64_MAX_BREAKPOINTS;i++)
      if (pc == cpu->breakpoints[i]) {
         mips64_emit_breakpoint(b);
         break;
      }
}

/* Fetch a MIPS instruction and emit corresponding translated code */
struct insn_tag *insn_fetch_and_emit(cpu_mips_t *cpu,insn_block_t *block,
                                     int delay_slot)
{
   struct insn_tag *tag;
   mips_insn_t code;

   code = insn_fetch(block);
   tag = insn_tag_find(code);
   assert(tag);
   
   if (delay_slot && !tag->delay_slot) {
      fprintf(stderr,"%% Invalid instruction 0x%8.8x in delay slot.\n",code);
      return NULL;
   }

   if (!delay_slot) {
      block->jit_insn_ptr[block->mips_trans_pos] = block->jit_ptr;
   }

   if (delay_slot != 2)
      block->mips_trans_pos++;

   mips64_inc_cp0_count_reg(block);

   if (!delay_slot)
      mips64_check_pending_irq(block);

#if BREAKPOINT_ENABLE
   if (cpu->breakpoints_enabled)
      insn_emit_breakpoint(cpu,block);
#endif

   tag->emit(cpu,block,code);
   return tag;
}

/* 
 * This is a special case of MIPS instruction emitting, when this fucking
 * GCC compiler has been able to optimize a lot and to branch in a delay
 * slot.
 */
u_char *insn_special_emit(cpu_mips_t *cpu,insn_block_t *block,m_uint64_t vaddr)
{   
   struct insn_tag *tag;
   mips_insn_t code;
   u_int offset;
   u_char *cptr;
   
   /* the caller will patch to this address */
   cptr = block->jit_ptr;

   offset = (vaddr - block->start_pc) >> 2;
   block->jit_insn_ptr[offset] = cptr;
   code = vmtoh32(block->mips_code[offset]);
   tag = insn_tag_find(code);
   assert(tag);

   tag->emit(cpu,block,code);
   mips64_set_pc(block,vaddr+4);
   insn_block_push_epilog(block);
   return cptr;
}

/* Add end of JIT block */
void insn_block_add_end(insn_block_t *b)
{
   mips64_set_pc(b,b->start_pc+(b->mips_trans_pos<<2));
   insn_block_push_epilog(b);
}

/* Create a instruction block */
insn_block_t *insn_block_create(cpu_mips_t *cpu,m_uint64_t vaddr)
{
   insn_block_t *block = NULL;

   if (!(block = malloc(sizeof(*block))))
      goto error;

   memset(block,0,sizeof(*block));
   block->start_pc = vaddr;
   block->jit_bufsize = MIPS_JIT_BUFSIZE;

   if (!(block->jit_buffer = malloc(block->jit_bufsize)))
      goto error;

   block->jit_ptr = block->jit_buffer;
   block->mips_code = cpu->mem_op_lookup(cpu,block->start_pc);

   if (!block->mips_code) {
      fprintf(stderr,"%% No memory map for code execution at 0x%llx\n",
              block->start_pc);
      goto error;
   }

#if DEBUG_BLOCK_TIMESTAMP
   block->tm_first_use = block->tm_last_use = jit_jiffies;
#endif
   return block;

 error:
   free(block);
   fprintf(stderr,"%% Unable to create instruction block for vaddr=0x%llx\n", 
           vaddr);
   return NULL;
}

/* Record a patch to apply in a compiled block */
int insn_block_record_patch(insn_block_t *block,u_char *jit_ptr,
                            m_uint64_t vaddr)
{
   struct insn_patch_table *ipt = block->patch_table;
   struct insn_patch *patch;

   /* pc must be 32-bit aligned */
   if (vaddr & 0x03) {
      fprintf(stderr,"Block 0x%8.8llx: trying to record an invalid PC "
              "(0x%8.8llx) - mips_trans_pos=%d.\n",
              block->start_pc,vaddr,block->mips_trans_pos);
      return(-1);
   }

   if (!ipt || (ipt->cur_patch >= INSN_PATCH_TABLE_SIZE))
   {
      /* full table or no table, create a new one */
      ipt = malloc(sizeof(*ipt));
      if (!ipt) {
         fprintf(stderr,"%% Unable to create patch table.\n");
         return(-1);
      }

      memset(ipt,0,sizeof(*ipt));
      ipt->next = block->patch_table;
      block->patch_table = ipt;
   }

#if DEBUG_BLOCK_PATCH
   printf("Block 0x%8.8llx: recording patch [JIT:%p->mips:0x%8.8llx], "
          "MTP=%d\n",block->start_pc,jit_ptr,vaddr,block->mips_trans_pos);
#endif

   patch = &ipt->patches[ipt->cur_patch];
   patch->jit_insn = jit_ptr;
   patch->mips_pc = vaddr;
   ipt->cur_patch++;   
   return(0);
}

/* Apply all patches */
int insn_block_apply_patches(cpu_mips_t *cpu,insn_block_t *block)
{
   struct insn_patch_table *ipt;
   struct insn_patch *patch;
   u_char *jit_dst;
   int i;

   for(ipt=block->patch_table;ipt;ipt=ipt->next)
      for(i=0;i<ipt->cur_patch;i++) 
      {
         patch = &ipt->patches[i];
         jit_dst = insn_block_get_jit_ptr(block,patch->mips_pc);

         if (!jit_dst) {
#if DEBUG_BLOCK_PATCH
            printf("Block 0x%8.8llx: trying to apply a null patch "
                   "(PC=0x%llx)\n",
                   block->start_pc,patch->mips_pc);
#endif
            jit_dst = insn_special_emit(cpu,block,patch->mips_pc);
         }

         insn_block_set_patch(patch->jit_insn,jit_dst);

#if DEBUG_BLOCK_PATCH
         printf("Block 0x%8.8llx: applying patch "
                "[JIT:%p->mips:0x%8.8llx=JIT:%p]\n",
                block->start_pc,patch->jit_insn,patch->mips_pc,jit_dst);
#endif
      }

   return(0);
}

/* Adjust the JIT buffer if its size is not sufficient */
int insn_block_adjust_jit_buffer(insn_block_t *block)
{
   u_char *new_ptr;

   if ((block->jit_ptr - block->jit_buffer) <= (block->jit_bufsize - 512))
      return(0);

#if DEBUG_BLOCK_CHUNK  
   printf("Block 0x%llx: adjusting JIT buffer...\n",block->start_pc);
#endif

   if (block->jit_chunk_pos >= INSN_MAX_CHUNKS) {
      fprintf(stderr,"Block 0x%llx: too many JIT chunks.\n",block->start_pc);
      return(-1);
   }

   /* save the current JIT block */
   block->jit_chunks[block->jit_chunk_pos++] = block->jit_buffer;

   if (!(new_ptr = malloc(block->jit_bufsize)))
      return(-1);

   /* jump to the new block */
   insn_block_set_jump(block->jit_ptr,new_ptr);
   block->jit_ptr = block->jit_buffer = new_ptr;
   return(0);
}

/* Scan an instruction block to determine its length */
int insn_block_scan(insn_block_t *block)
{
   struct insn_jump *jump;
   m_uint64_t cur_pc,last_jrra_pc;
   m_uint64_t next_pc,max_next_pc;
   m_int64_t offset;
   mips_insn_t code;
   size_t len;

   last_jrra_pc = max_next_pc = 0;

   for(block->mips_code_len = 0, cur_pc = block->start_pc;
       block->mips_code_len < MIPS_MAX_BLOCK_INSN;
       block->mips_code_len++, cur_pc += 4)
   {
      code = vmtoh32(block->mips_code[block->mips_code_len]);

#if DEBUG_BLOCK_SCAN
      printf("insn_block_scan: cur_pc = 0x%llx, instruction = 0x%8.8x\n",
             cur_pc,code);
#endif     
 
      /* jr ra = jump to return address */
      if (code == MIPS_INSN_JR_RA) {
         last_jrra_pc = cur_pc;

         if (last_jrra_pc >= max_next_pc) {
            block->mips_code_len++;  /* account delay slot */
            break;
         }

         continue;
      }

      /* check if we have a jump instruction */
      if (!(jump = insn_jump_find(code)))
         continue;

#if DEBUG_BLOCK_SCAN
      printf("insn_block_scan: jump \"%s\" detected (max_next_pc=0x%llx)\n",
             jump->name, max_next_pc);
#endif

      /* we have a jump, compute next pc */
      offset = (code & ((1 << jump->offset_bits) - 1)) << 2;

      if (jump->relative) {
         next_pc = cur_pc + 4 + sign_extend(offset,jump->offset_bits+2);
      } else {
         next_pc = (cur_pc & ~((1 << (jump->offset_bits + 2)) - 1)) | offset;
      }

      if (next_pc > max_next_pc)
         max_next_pc = next_pc;

#if DEBUG_BLOCK_SCAN
      printf("insn_block_scan: next_pc=0x%llx, max_next_pc=0x%llx\n",
             next_pc,max_next_pc);
#endif
   }

   block->mips_code_len++;
   block->end_pc = block->start_pc + (block->mips_code_len * 4);
   
   len = block->mips_code_len * sizeof(u_char *);

   if (!(block->jit_insn_ptr = malloc(len))) {
      fprintf(stderr,"insn_block_scan: unable to create JIT/mips mapping.\n");
      return(-1);
   }

   memset(block->jit_insn_ptr,0,len);

#if DEBUG_BLOCK_SCAN
   printf("insn_block_scan: start_pc = 0x%llx, end_pc = 0x%llx\n",
          block->start_pc, block->end_pc);
#endif

   return(0);
}

/* Compile a MIPS instruction block */
static inline int insn_block_compile(cpu_mips_t *cpu,insn_block_t *block)
{  
   struct insn_tag *tag;

   block->mips_trans_pos = 0;

   while(block->mips_trans_pos < block->mips_code_len)
   {
      if (!(tag = insn_fetch_and_emit(cpu,block,0)))
         return(-1);

#if DEBUG_BLOCK_COMPILE
      printf("Block 0x%8.8llx: emitted tag 0x%8.8x/0x%8.8x\n",
             block->start_pc,tag->mask,tag->value);
#endif

      insn_block_adjust_jit_buffer(block);
   }

   insn_block_add_end(block);
   insn_block_apply_patches(cpu,block);
   return(0);
}

/* Compile a MIPS instruction block */
static insn_block_t *insn_block_scan_and_compile(cpu_mips_t *cpu,
                                                 m_uint64_t vaddr)
{
   insn_block_t *block;
   m_uint16_t index;

   if (!(block = insn_block_create(cpu,vaddr)))
      return NULL;

   if ((insn_block_scan(block) == -1) || (insn_block_compile(cpu,block) == -1))
      return NULL;

   rbtree_insert(cpu->insn_block_tree,block,block);
   
   index = ibh_index(vaddr >> 2);
   cpu->insn_block_hash[index] = block;
   return block;
}

/* Run a compiled MIPS instruction block */
static forced_inline void insn_block_run(cpu_mips_t *cpu,insn_block_t *block)
{
#if DEBUG_SYM_TREE
   struct symbol *sym = NULL;
   int mark = FALSE;
#endif

   if (unlikely(cpu->pc & 0x03)) {
      fprintf(stderr,"insn_block_run: Invalid PC 0x%llx.\n",cpu->pc);
      mips64_dump_regs(cpu);
      tlb_dump(cpu);
      exit(EXIT_FAILURE);
   }

#if DEBUG_SYM_TREE
   if (sym_tree)
   {
      if ((sym = sym_lookup(cpu->pc)) != NULL) {
         fprintf(log_file,
                 "function_run: %s (PC=0x%llx) "
                 "RA = 0x%llx\na0=0x%llx, "
                 "a1=0x%llx, a2=0x%llx, a3=0x%llx\n",
                 sym->name, cpu->pc, cpu->gpr[MIPS_GPR_RA],
                 cpu->gpr[MIPS_GPR_A0], cpu->gpr[MIPS_GPR_A1],
                 cpu->gpr[MIPS_GPR_A2], cpu->gpr[MIPS_GPR_A3]);
         mark = TRUE;
      }
   }
#endif

#if DEBUG_INSN_ITRACE
   if (insn_itrace) {
      fprintf(log_file,
              "block_run(S): PC = 0x%llx [start_pc=0x%llx,end_pc=0x%llx] "
              "RA = 0x%llx\na0=0x%llx, a1=0x%llx, a2=0x%llx, a3=0x%llx\n",
              cpu->pc,block->start_pc,block->end_pc,
              cpu->gpr[MIPS_GPR_RA],
              cpu->gpr[MIPS_GPR_A0],
              cpu->gpr[MIPS_GPR_A1],
              cpu->gpr[MIPS_GPR_A2],
              cpu->gpr[MIPS_GPR_A3]);
   }
#endif

   /* Execute JIT compiled code */
   insn_block_exec_jit_code(cpu,block);

#if DEBUG_SYM_TREE
   if (mark) {
      fprintf(log_file,"function_end: %s, v0 = 0x%llx\n",
              sym->name,cpu->gpr[MIPS_GPR_V0]);
   }
#endif

#if DEBUG_INSN_ITRACE
   if (insn_itrace) {
      fprintf(log_file,"block_run(E): PC = 0x%llx, v0 = 0x%llx\n",
              cpu->pc, cpu->gpr[MIPS_GPR_V0]);
   }
#endif
}

/* Tree comparison function */
int insn_block_cmp(m_uint64_t *vaddr,insn_block_t *b)
{
   if (*vaddr < b->start_pc)
      return(-1);

   if (*vaddr >= b->end_pc)
      return(1);

   return(0);
}

/* Check if the specified address belongs to the specified block */
int insn_block_local_addr(insn_block_t *block,m_uint64_t vaddr,
                          u_char **jit_addr)
{
   if ((vaddr >= block->start_pc) && (vaddr < block->end_pc)) {
      *jit_addr = insn_block_get_jit_ptr(block,vaddr);
      return(1);
   }
   return(0);
}

/* Execute a compiled MIPS code */
void *insn_block_execute(cpu_mips_t *cpu)
{
   insn_block_t *block;
   m_uint32_t index;

 start_cpu:
   for(;;) {
      if (unlikely(!cpu->pc) || unlikely(cpu->state != MIPS_CPU_RUNNING))
         break;

      index = ibh_index(cpu->pc >> 2);
      block = cpu->insn_block_hash[index];
      cpu->hash_lookups++;

      if (unlikely(!block || (cpu->pc < block->start_pc) || 
                   (cpu->pc >= block->end_pc)))
      {
         block = insn_block_locate_fast(cpu,cpu->pc);

         if (!block) {
            block = insn_block_scan_and_compile(cpu,cpu->pc);
            if (unlikely(!block)) {
               cpu->pc = 0;
               break;
            }
         }

         cpu->insn_block_hash[index] = block;
         cpu->hash_misses++;
      }

#if DEBUG_BLOCK_TIMESTAMP
      block->tm_last_use = jit_jiffies++;
#endif
      block->acc_count++;
      insn_block_run(cpu,block);
   }
   
   if (!cpu->pc) {
      cpu_stop(cpu);
      m_log("CPU","CPU%u has PC=0, halting CPU.\n",cpu->id);
   }

   /* check regularly if the CPU has been restarted */
   do {
      if (cpu->state == MIPS_CPU_RUNNING)
         goto start_cpu;
      
      usleep(200000);
   }while(1);

   return NULL;
}

/* Dump an instruction block */
void insn_block_dump_tree_node(insn_block_t *b,void *empty,m_tmcnt_t *ct)
{
#if DEBUG_BLOCK_TIMESTAMP
   m_uint64_t deltaT = jit_jiffies - b->tm_last_use;

   m_log("RBTREE","Block 0x%llx (end=0x%llx): count=%10llu, deltaT=%12llu\n",
         b->start_pc,b->end_pc,b->acc_count,deltaT);
#else
   m_log("RBTREE","Block 0x%llx (end=0x%llx): count=%llu\n",
         b->start_pc,b->end_pc,b->acc_count);
#endif
}

/* Dump the instruction block tree */
void insn_block_dump_tree(cpu_mips_t *cpu)
{
   m_log("RBTREE","Height: %d\n",rbtree_height(cpu->insn_block_tree));

   rbtree_foreach(cpu->insn_block_tree,
                  (tree_fforeach)insn_block_dump_tree_node,
                  NULL);
}
