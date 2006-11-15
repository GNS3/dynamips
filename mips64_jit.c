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
#include "mips64.h"
#include "mips64_exec.h"
#include "insn_lookup.h"
#include "ptask.h"

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

/* Initialize the JIT structure */
int mips64_jit_init(cpu_mips_t *cpu)
{
   insn_exec_page_t *cp;
   u_char *cp_addr;
   u_int area_size;
   size_t len;
   int i;

   /* Physical mapping for executable pages */
   len = 1048576 * sizeof(void *);
   cpu->exec_phys_map = m_memalign(4096,len);
   memset(cpu->exec_phys_map,0,len);

   /* Get area size */
   if (!(area_size = cpu->vm->exec_area_size))
      area_size = MIPS_EXEC_AREA_SIZE;

   /* Create executable page area */
   cpu->exec_page_area_size = area_size * 1048576;
   cpu->exec_page_area = mmap(NULL,cpu->exec_page_area_size,
                              PROT_EXEC|PROT_READ|PROT_WRITE,
                              MAP_SHARED|MAP_ANONYMOUS,-1,(off_t)0);

   if (!cpu->exec_page_area) {
      fprintf(stderr,
              "mips64_jit_init: unable to create exec area (size %lu)\n",
              (u_long)cpu->exec_page_area_size);
      return(-1);
   }

   /* Carve the executable page area */
   cpu->exec_page_count = cpu->exec_page_area_size / MIPS_JIT_BUFSIZE;

   cpu->exec_page_array = calloc(cpu->exec_page_count,
                                 sizeof(insn_exec_page_t));
   
   if (!cpu->exec_page_array) {
      fprintf(stderr,"mips64_jit_init: unable to create exec page array\n");
      return(-1);
   }

   for(i=0,cp_addr=cpu->exec_page_area;i<cpu->exec_page_count;i++) {
      cp = &cpu->exec_page_array[i];

      cp->ptr = cp_addr;
      cp_addr += MIPS_JIT_BUFSIZE;

      cp->next = cpu->exec_page_free_list;
      cpu->exec_page_free_list = cp;
   }

   printf("CPU%u: carved JIT exec zone of %lu Mb into %lu pages of %u Kb.\n",
          cpu->id,(u_long)(cpu->exec_page_area_size / 1048576),
          (u_long)cpu->exec_page_count,MIPS_JIT_BUFSIZE / 1024);
   return(0);
}

/* Flush the JIT */
u_int mips64_jit_flush(cpu_mips_t *cpu,u_int threshold)
{
   insn_block_t *p,*next;
   u_int count = 0;

   if (!threshold)
      threshold = (u_int)(-1);  /* UINT_MAX not defined everywhere */

   for(p=cpu->insn_block_list;p;p=next) {
      next = p->next;

      if (p->acc_count <= threshold) {
         cpu->exec_phys_map[p->phys_page] = NULL;
         insn_block_free(cpu,p,TRUE);
         count++;
      }
   }

   cpu->compiled_pages -= count;
   return(count);
}

/* Shutdown the JIT */
void mips64_jit_shutdown(cpu_mips_t *cpu)
{   
   insn_block_t *p,*next;

   /* Flush the JIT */
   mips64_jit_flush(cpu,0);

   /* Free the instruction blocks */
   for(p=cpu->insn_block_free_list;p;p=next) {
      next = p->next;
      free(p);
   }

   /* Unmap the executable page area */
   if (cpu->exec_page_area)
      munmap(cpu->exec_page_area,cpu->exec_page_area_size);

   /* Free the exec page array */
   free(cpu->exec_page_array);

   /* Free physical mapping for executable pages */
   free(cpu->exec_phys_map);   
}

/* Allocate an exec page */
static inline insn_exec_page_t *exec_page_alloc(cpu_mips_t *cpu)
{
   insn_exec_page_t *p;
   u_int count;

   /* If the free list is empty, flush JIT */
   if (unlikely(!cpu->exec_page_free_list)) 
   {
      if (cpu->jit_flush_method) {
         cpu_log(cpu,"JIT","flushing data structures (compiled pages=%u)\n",
                 cpu->compiled_pages);
         mips64_jit_flush(cpu,0);
      } else {
         count = mips64_jit_flush(cpu,100);
         cpu_log(cpu,"JIT","partial JIT flush (count=%u)\n",count);

         if (!cpu->exec_page_free_list)
            mips64_jit_flush(cpu,0);
      }
      
      /* Use both methods alternatively */
      cpu->jit_flush_method = 1 - cpu->jit_flush_method;
   }

   if (unlikely(!(p = cpu->exec_page_free_list)))
      return NULL;
   
   cpu->exec_page_free_list = p->next;
   cpu->exec_page_alloc++;
   return p;
}

/* Free an exec page and returns it to the pool */
static inline void exec_page_free(cpu_mips_t *cpu,insn_exec_page_t *p)
{
   if (p) {
      p->next = cpu->exec_page_free_list;
      cpu->exec_page_free_list = p;
      cpu->exec_page_alloc--;
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
static forced_inline mips_insn_t insn_fetch(insn_block_t *b)
{
   return(vmtoh32(b->mips_code[b->mips_trans_pos]));
}

/* Emit a breakpoint if necessary */
#if BREAKPOINT_ENABLE
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
#endif /* BREAKPOINT_ENABLE */

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
      mips64_emit_invalid_delay_slot(block);
      return NULL;
   }

   if (!delay_slot) {
      block->jit_insn_ptr[block->mips_trans_pos] = block->jit_ptr;
   }

   if (delay_slot != 2)
      block->mips_trans_pos++;

#if DEBUG_INSN_PERF_CNT
   mips64_inc_perf_counter(block);
#endif

   if (!delay_slot) {
      /* Check for IRQs + Increment count register before jumps */
      if (!tag->delay_slot) {
         mips64_inc_cp0_count_reg(block);
         mips64_check_pending_irq(block);
      }
   }

#if BREAKPOINT_ENABLE
   if (cpu->breakpoints_enabled)
      insn_emit_breakpoint(cpu,block);
#endif

   tag->emit(cpu,block,code);
   return tag;
}

/* Add end of JIT block */
void insn_block_add_end(insn_block_t *b)
{
   mips64_set_pc(b,b->start_pc+(b->mips_trans_pos<<2));
   insn_block_push_epilog(b);
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

         if (jit_dst) {
#if DEBUG_BLOCK_PATCH
            printf("Block 0x%8.8llx: applying patch "
                   "[JIT:%p->mips:0x%8.8llx=JIT:%p]\n",
                   block->start_pc,patch->jit_insn,patch->mips_pc,jit_dst);
#endif
            insn_block_set_patch(patch->jit_insn,jit_dst);
         }
      }

   return(0);
}

/* Free the patch table */
static void insn_block_free_patches(insn_block_t *block)
{
   struct insn_patch_table *p,*next;

   for(p=block->patch_table;p;p=next) {
      next = p->next;
      free(p);
   }

   block->patch_table = NULL;
}

/* Adjust the JIT buffer if its size is not sufficient */
int insn_block_adjust_jit_buffer(cpu_mips_t *cpu,insn_block_t *block)
{
   insn_exec_page_t *new_buffer;

   if ((block->jit_ptr - block->jit_buffer->ptr) <= (MIPS_JIT_BUFSIZE - 512))
      return(0);

#if DEBUG_BLOCK_CHUNK  
   printf("Block 0x%llx: adjusting JIT buffer...\n",block->start_pc);
#endif

   if (block->jit_chunk_pos >= INSN_MAX_CHUNKS) {
      fprintf(stderr,"Block 0x%llx: too many JIT chunks.\n",block->start_pc);
      return(-1);
   }

   if (!(new_buffer = exec_page_alloc(cpu)))
      return(-1);

   /* record the new exec page */
   block->jit_chunks[block->jit_chunk_pos++] = block->jit_buffer;
   block->jit_buffer = new_buffer;

   /* jump to the new exec page (link) */
   insn_block_set_jump(block->jit_ptr,new_buffer->ptr);
   block->jit_ptr = new_buffer->ptr;
   return(0);
}

/* Allocate an instruction block */
static inline insn_block_t *insn_block_alloc(cpu_mips_t *cpu)
{
   insn_block_t *p;

   if (cpu->insn_block_free_list) {
      p = cpu->insn_block_free_list;
      cpu->insn_block_free_list = p->next;
   } else {
      if (!(p = malloc(sizeof(*p))))
         return NULL;
   }

   memset(p,0,sizeof(*p));
   return p;
}

/* Free an instruction block */
void insn_block_free(cpu_mips_t *cpu,insn_block_t *block,int list_removal)
{
   int i;

   if (block) {
      if (list_removal) {
         /* Remove the block from the linked list */
         if (block->next)
            block->next->prev = block->prev;
         else
            cpu->insn_block_last = block->prev;

         if (block->prev)
            block->prev->next = block->next;
         else
            cpu->insn_block_list = block->next;
      }

      /* Free the patch tables */
      insn_block_free_patches(block);

      /* Free code pages */
      for(i=0;i<INSN_MAX_CHUNKS;i++)
         exec_page_free(cpu,block->jit_chunks[i]);

      /* Free the current JIT buffer */
      exec_page_free(cpu,block->jit_buffer);

      /* Free the MIPS-to-native code mapping */
      free(block->jit_insn_ptr);

      /* Make the block return to the free list */
      block->next = cpu->insn_block_free_list;
      cpu->insn_block_free_list = block;
   }
}

/* Create an instruction block */
static insn_block_t *insn_block_create(cpu_mips_t *cpu,m_uint64_t vaddr)
{
   insn_block_t *block = NULL;

   if (!(block = insn_block_alloc(cpu)))
      goto err_block_alloc;

   block->start_pc = vaddr;

   /* Allocate the first JIT buffer */
   if (!(block->jit_buffer = exec_page_alloc(cpu)))
      goto err_jit_alloc;

   block->jit_ptr = block->jit_buffer->ptr;
   block->mips_code = cpu->mem_op_lookup(cpu,block->start_pc);

   if (!block->mips_code) {
      fprintf(stderr,"%% No memory map for code execution at 0x%llx\n",
              block->start_pc);
      goto err_lookup;
   }

#if DEBUG_BLOCK_TIMESTAMP
   block->tm_first_use = block->tm_last_use = jit_jiffies;
#endif
   return block;

 err_lookup:
 err_jit_alloc:
   insn_block_free(cpu,block,FALSE);
 err_block_alloc:
   fprintf(stderr,"%% Unable to create instruction block for vaddr=0x%llx\n", 
           vaddr);
   return NULL;
}

/* Compile a MIPS instruction page */
static inline insn_block_t *insn_page_compile(cpu_mips_t *cpu,m_uint64_t vaddr)
{  
   m_uint64_t page_addr;
   insn_block_t *block;
   struct insn_tag *tag;
   size_t len;

   page_addr = vaddr & ~(m_uint64_t)MIPS_MIN_PAGE_IMASK;

   if (unlikely(!(block = insn_block_create(cpu,page_addr)))) {
      fprintf(stderr,"insn_page_compile: unable to create JIT block.\n");
      return NULL;
   }

   /* Allocate the array used to convert MIPS code ptr to native code ptr */
   len = MIPS_MIN_PAGE_SIZE / sizeof(mips_insn_t);

   if (!(block->jit_insn_ptr = calloc(len,sizeof(u_char *)))) {
      fprintf(stderr,"insn_page_compile: unable to create JIT mappings.\n");
      goto error;
   }

   /* Emit native code for each instruction */
   block->mips_trans_pos = 0;

   while(block->mips_trans_pos < (MIPS_MIN_PAGE_SIZE/sizeof(mips_insn_t)))
   {
      if (unlikely(!(tag = insn_fetch_and_emit(cpu,block,0)))) {
         fprintf(stderr,"insn_page_compile: unable to fetch instruction.\n");
         goto error;
      }

#if DEBUG_BLOCK_COMPILE
      printf("Page 0x%8.8llx: emitted tag 0x%8.8x/0x%8.8x\n",
             block->start_pc,tag->mask,tag->value);
#endif

      insn_block_adjust_jit_buffer(cpu,block);
   }

   insn_block_add_end(block);
   insn_block_apply_patches(cpu,block);
   insn_block_free_patches(block);

   /* Add the block to the linked list */
   block->next = cpu->insn_block_list;
   block->prev = NULL;

   if (cpu->insn_block_list)
      cpu->insn_block_list->prev = block;
   else
      cpu->insn_block_last = block;

   cpu->insn_block_list = block;
   
   cpu->compiled_pages++;
   return block;

 error:
   insn_block_free(cpu,block,FALSE);
   return NULL;
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
      cpu_stop(cpu);
      return;
   }

#if DEBUG_SYM_TREE
   if (cpu->sym_trace && cpu->sym_tree)
   {
      if ((sym = mips64_sym_lookup(cpu,cpu->pc)) != NULL) {
         cpu_log(cpu,"insn_block_run(start)",
                 "%s (PC=0x%llx) RA = 0x%llx\na0=0x%llx, "
                 "a1=0x%llx, a2=0x%llx, a3=0x%llx\n",
                 sym->name, cpu->pc, cpu->gpr[MIPS_GPR_RA],
                 cpu->gpr[MIPS_GPR_A0], cpu->gpr[MIPS_GPR_A1],
                 cpu->gpr[MIPS_GPR_A2], cpu->gpr[MIPS_GPR_A3]);
         mark = TRUE;
      }
   }
#endif

   /* Execute JIT compiled code */
   insn_block_exec_jit_code(cpu,block);

#if DEBUG_SYM_TREE
   if (mark) {
      cpu_log(cpu,"insn_block_run(end)","%s, v0 = 0x%llx\n",
              sym->name,cpu->gpr[MIPS_GPR_V0]);
   }
#endif
}

/* Check if the specified address belongs to the specified block */
int insn_block_local_addr(insn_block_t *block,m_uint64_t vaddr,
                          u_char **jit_addr)
{
   if ((vaddr >= block->start_pc) && 
       ((vaddr - block->start_pc) < MIPS_MIN_PAGE_SIZE))
   {
      *jit_addr = insn_block_get_jit_ptr(block,vaddr);
      return(1);
   }

   return(0);
}

/* Check if PC register matches the compiled block virtual address */
static forced_inline int insn_block_match(cpu_mips_t *cpu,insn_block_t *block)
{
   m_uint64_t vpage;

   vpage = cpu->pc & ~(m_uint64_t)MIPS_MIN_PAGE_IMASK;
   return(block->start_pc == vpage);
}

/* Execute a compiled MIPS code */
void *insn_block_execute(cpu_mips_t *cpu)
{  
   pthread_t timer_irq_thread;
   insn_block_t *block;
   m_uint32_t phys_page;
   int timer_irq_check = 0;

   if (pthread_create(&timer_irq_thread,NULL,
                      (void *)mips64_timer_irq_run,cpu)) 
   {
      fprintf(stderr,
              "VM '%s': unable to create Timer IRQ thread for CPU%u.\n",
              cpu->vm->name,cpu->id);
      cpu_stop(cpu);
      return NULL;
   }

   cpu->cpu_thread_running = TRUE;

 start_cpu:   
   cpu->idle_count = 0;

   for(;;) {
      if (unlikely(cpu->state != MIPS_CPU_RUNNING))
         break;

#if DEBUG_BLOCK_PERF_CNT
      cpu->perf_counter++;
#endif
      /* Handle virtual idle loop */
      if (unlikely(cpu->pc == cpu->idle_pc)) {
         if (++cpu->idle_count == cpu->idle_max) {
            mips64_idle_loop(cpu);
            cpu->idle_count = 0;
         }
      }

      /* Handle the virtual CPU clock */
      if (++timer_irq_check == cpu->timer_irq_check_itv) {
         timer_irq_check = 0;

         if (cpu->timer_irq_pending && !cpu->irq_disable) {
            mips64_trigger_timer_irq(cpu);
            mips64_trigger_irq(cpu);
            cpu->timer_irq_pending--;
         }
      }

      /* Get the physical page address corresponding to PC register */
      if (unlikely(cpu->translate(cpu,cpu->pc,&phys_page))) {
         fprintf(stderr,"VM '%s': no physical page for CPU%u PC=0x%llx\n",
                 cpu->vm->name,cpu->id,cpu->pc);
         cpu_stop(cpu);
         break;
      }

      block = cpu->exec_phys_map[phys_page];

      /* No block found, compile the page */
      if (unlikely(!block) || unlikely(!insn_block_match(cpu,block))) 
      {
         if (block != NULL) {
            insn_block_free(cpu,block,TRUE);
            cpu->exec_phys_map[phys_page] = NULL;
         }

         block = insn_page_compile(cpu,cpu->pc);
         if (unlikely(!block)) {
            fprintf(stderr,
                    "VM '%s': unable to compile block for CPU%u PC=0x%llx\n",
                    cpu->vm->name,cpu->id,cpu->pc);
            cpu_stop(cpu);
            break;
         }

         block->phys_page = phys_page;
         cpu->exec_phys_map[phys_page] = block;
      }

#if DEBUG_BLOCK_TIMESTAMP
      block->tm_last_use = jit_jiffies++;
#endif
      block->acc_count++;
      insn_block_run(cpu,block);
   }
      
   if (!cpu->pc) {
      cpu_stop(cpu);
      cpu_log(cpu,"JIT","PC=0, halting CPU.\n");
   }

   /* Check regularly if the CPU has been restarted */
   while(cpu->cpu_thread_running) {
      cpu->seq_state++;

      switch(cpu->state) {
         case MIPS_CPU_RUNNING:
            cpu->state = MIPS_CPU_RUNNING;
            goto start_cpu;

         case MIPS_CPU_HALTED:
            cpu->cpu_thread_running = FALSE;
            pthread_join(timer_irq_thread,NULL);
            break;
      }
      
      /* CPU is paused */
      usleep(200000);
   }

   return NULL;
}
