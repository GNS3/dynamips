/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * PPC32 JIT compiler.
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

#include "cpu.h"
#include "device.h"
#include "ppc32.h"
#include "ppc32_exec.h"
#include "ppc32_jit.h"
#include "insn_lookup.h"
#include "memory.h"
#include "ptask.h"

#include PPC32_ARCH_INC_FILE

/* Instruction Lookup Table */
static insn_lookup_t *ilt = NULL;

static void *ppc32_jit_get_insn(int index)
{
   return(&ppc32_insn_tags[index]);
}

static int ppc32_jit_chk_lo(struct ppc32_insn_tag *tag,int value)
{
   return((value & tag->mask) == (tag->value & 0xFFFF));
}

static int ppc32_jit_chk_hi(struct ppc32_insn_tag *tag,int value)
{
   return((value & (tag->mask >> 16)) == (tag->value >> 16));
}

/* Initialize instruction lookup table */
void ppc32_jit_create_ilt(void)
{
   int i,count;

   for(i=0,count=0;ppc32_insn_tags[i].emit;i++)
      count++;

   ilt = ilt_create("ppc32j",count,
                    (ilt_get_insn_cbk_t)ppc32_jit_get_insn,
                    (ilt_check_cbk_t)ppc32_jit_chk_lo,
                    (ilt_check_cbk_t)ppc32_jit_chk_hi);
}

/* Initialize the JIT structure */
int ppc32_jit_init(cpu_ppc_t *cpu)
{
   insn_exec_page_t *cp;
   u_char *cp_addr;
   u_int area_size;
   size_t len;
   int i;

   /* JIT mapping for executable pages */
   len = PPC_JIT_IA_HASH_SIZE * sizeof(void *);
   cpu->exec_blk_map = m_memalign(4096,len);
   memset(cpu->exec_blk_map,0,len);

   /* Physical mapping for executable pages */
   len = PPC_JIT_PHYS_HASH_SIZE * sizeof(void *);
   cpu->exec_phys_map = m_memalign(4096,len);
   memset(cpu->exec_phys_map,0,len);

   /* Get area size */
   if (!(area_size = cpu->vm->exec_area_size))
      area_size = PPC_EXEC_AREA_SIZE;

   /* Create executable page area */
   cpu->exec_page_area_size = area_size * 1048576;
   cpu->exec_page_area = mmap(NULL,cpu->exec_page_area_size,
                              PROT_EXEC|PROT_READ|PROT_WRITE,
                              MAP_SHARED|MAP_ANONYMOUS,-1,(off_t)0);

   if (!cpu->exec_page_area) {
      fprintf(stderr,
              "ppc32_jit_init: unable to create exec area (size %lu)\n",
              (u_long)cpu->exec_page_area_size);
      return(-1);
   }

   /* Carve the executable page area */
   cpu->exec_page_count = cpu->exec_page_area_size / PPC_JIT_BUFSIZE;

   cpu->exec_page_array = calloc(cpu->exec_page_count,
                                 sizeof(insn_exec_page_t));
   
   if (!cpu->exec_page_array) {
      fprintf(stderr,"ppc32_jit_init: unable to create exec page array\n");
      return(-1);
   }

   for(i=0,cp_addr=cpu->exec_page_area;i<cpu->exec_page_count;i++) {
      cp = &cpu->exec_page_array[i];

      cp->ptr = cp_addr;
      cp_addr += PPC_JIT_BUFSIZE;

      cp->next = cpu->exec_page_free_list;
      cpu->exec_page_free_list = cp;
   }

   printf("CPU%u: carved JIT exec zone of %lu Mb into %lu pages of %u Kb.\n",
          cpu->gen->id,
          (u_long)(cpu->exec_page_area_size / 1048576),
          (u_long)cpu->exec_page_count,PPC_JIT_BUFSIZE / 1024);
   return(0);
}

/* Flush the JIT */
u_int ppc32_jit_flush(cpu_ppc_t *cpu,u_int threshold)
{
   ppc32_jit_tcb_t *p,*next;
   m_uint32_t ia_hash;
   u_int count = 0;

   if (!threshold)
      threshold = (u_int)(-1);  /* UINT_MAX not defined everywhere */

   for(p=cpu->tcb_list;p;p=next) {
      next = p->next;

      if (p->acc_count <= threshold) {
         ia_hash = ppc32_jit_get_ia_hash(p->start_ia);
         ppc32_jit_tcb_free(cpu,p,TRUE);

         if (cpu->exec_blk_map[ia_hash] == p)
            cpu->exec_blk_map[ia_hash] = NULL;
         count++;
      }
   }

   cpu->compiled_pages -= count;
   return(count);
}

/* Shutdown the JIT */
void ppc32_jit_shutdown(cpu_ppc_t *cpu)
{   
   ppc32_jit_tcb_t *p,*next;

   /* Flush the JIT */
   ppc32_jit_flush(cpu,0);

   /* Free the instruction blocks */
   for(p=cpu->tcb_free_list;p;p=next) {
      next = p->next;
      free(p);
   }

   /* Unmap the executable page area */
   if (cpu->exec_page_area)
      munmap(cpu->exec_page_area,cpu->exec_page_area_size);

   /* Free the exec page array */
   free(cpu->exec_page_array);

   /* Free JIT block mapping */
   free(cpu->exec_blk_map);   

   /* Free physical mapping for executable pages */
   free(cpu->exec_phys_map);
}

/* Allocate an exec page */
static inline insn_exec_page_t *exec_page_alloc(cpu_ppc_t *cpu)
{
   insn_exec_page_t *p;
   u_int count;

   /* If the free list is empty, flush JIT */
   if (unlikely(!cpu->exec_page_free_list)) 
   {
      if (cpu->jit_flush_method) {
         cpu_log(cpu->gen,
                 "JIT","flushing data structures (compiled pages=%u)\n",
                 cpu->compiled_pages);
         ppc32_jit_flush(cpu,0);
      } else {
         count = ppc32_jit_flush(cpu,100);
         cpu_log(cpu->gen,"JIT","partial JIT flush (count=%u)\n",count);

         if (!cpu->exec_page_free_list)
            ppc32_jit_flush(cpu,0);
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
static inline void exec_page_free(cpu_ppc_t *cpu,insn_exec_page_t *p)
{
   if (p) {
      p->next = cpu->exec_page_free_list;
      cpu->exec_page_free_list = p;
      cpu->exec_page_alloc--;
   }
}

/* Find the JIT code emitter for the specified PowerPC instruction */
static struct ppc32_insn_tag *insn_tag_find(ppc_insn_t ins)
{
   struct ppc32_insn_tag *tag = NULL;
   int index;

   index = ilt_lookup(ilt,ins);
   tag = ppc32_jit_get_insn(index);
   return tag;
}

/* Fetch a PowerPC instruction */
static forced_inline ppc_insn_t insn_fetch(ppc32_jit_tcb_t *b)
{
   return(vmtoh32(b->ppc_code[b->ppc_trans_pos]));
}

/* Emit a breakpoint if necessary */
#if BREAKPOINT_ENABLE
static void insn_emit_breakpoint(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b)
{
   m_uint32_t ia;
   int i;

   ia = b->start_ia+((b->ppc_trans_pos-1)<<2);

   for(i=0;i<PPC32_MAX_BREAKPOINTS;i++)
      if (ia == cpu->breakpoints[i]) {
         ppc32_emit_breakpoint(b);
         break;
      }
}
#endif /* BREAKPOINT_ENABLE */

/* Fetch a PowerPC instruction and emit corresponding translated code */
struct ppc32_insn_tag *ppc32_jit_fetch_and_emit(cpu_ppc_t *cpu,
                                                ppc32_jit_tcb_t *block)
{
   struct ppc32_insn_tag *tag;
   ppc_insn_t code;

   code = insn_fetch(block);
   tag = insn_tag_find(code);
   assert(tag);
   
   block->jit_insn_ptr[block->ppc_trans_pos] = block->jit_ptr;
   block->ppc_trans_pos++;

#if DEBUG_INSN_PERF_CNT
   ppc32_inc_perf_counter(block);
#endif

#if BREAKPOINT_ENABLE
   if (cpu->breakpoints_enabled)
      insn_emit_breakpoint(cpu,block);
#endif

   tag->emit(cpu,block,code);
   return tag;
}

/* Add end of JIT block */
static void ppc32_jit_tcb_add_end(ppc32_jit_tcb_t *b)
{
   ppc32_set_ia(b,b->start_ia+(b->ppc_trans_pos<<2));
   ppc32_jit_tcb_push_epilog(b);
}

/* Record a patch to apply in a compiled block */
int ppc32_jit_tcb_record_patch(ppc32_jit_tcb_t *block,u_char *jit_ptr,
                               m_uint32_t vaddr)
{
   struct ppc32_jit_patch_table *ipt = block->patch_table;
   struct ppc32_insn_patch *patch;

   /* pc must be 32-bit aligned */
   if (vaddr & 0x03) {
      fprintf(stderr,"Block 0x%8.8x: trying to record an invalid IA "
              "(0x%8.8x) - ppc_trans_pos=%d.\n",
              block->start_ia,vaddr,block->ppc_trans_pos);
      return(-1);
   }

   if (!ipt || (ipt->cur_patch >= PPC32_INSN_PATCH_TABLE_SIZE))
   {
      /* full table or no table, create a new one */
      ipt = malloc(sizeof(*ipt));
      if (!ipt) {
         fprintf(stderr,"Block 0x%8.8x: unable to create patch table.\n",
                 block->start_ia);
         return(-1);
      }

      memset(ipt,0,sizeof(*ipt));
      ipt->next = block->patch_table;
      block->patch_table = ipt;
   }

#if DEBUG_BLOCK_PATCH
   printf("Block 0x%8.8x: recording patch [JIT:%p->ppc:0x%8.8x], "
          "MTP=%d\n",block->start_ia,jit_ptr,vaddr,block->ppc_trans_pos);
#endif

   patch = &ipt->patches[ipt->cur_patch];
   patch->jit_insn = jit_ptr;
   patch->ppc_ia = vaddr;
   ipt->cur_patch++;   
   return(0);
}

/* Apply all patches */
static int ppc32_jit_tcb_apply_patches(cpu_ppc_t *cpu,
                                        ppc32_jit_tcb_t *block)
{
   struct ppc32_jit_patch_table *ipt;
   struct ppc32_insn_patch *patch;
   u_char *jit_dst;
   int i;

   for(ipt=block->patch_table;ipt;ipt=ipt->next)
      for(i=0;i<ipt->cur_patch;i++) 
      {
         patch = &ipt->patches[i];
         jit_dst = ppc32_jit_tcb_get_host_ptr(block,patch->ppc_ia);

         if (jit_dst) {
#if DEBUG_BLOCK_PATCH
            printf("Block 0x%8.8x: applying patch "
                   "[JIT:%p->ppc:0x%8.8x=JIT:%p]\n",
                   block->start_ia,patch->jit_insn,patch->ppc_ia,jit_dst);
#endif
            ppc32_jit_tcb_set_patch(patch->jit_insn,jit_dst);
         }
      }

   return(0);
}

/* Free the patch table */
static void ppc32_jit_tcb_free_patches(ppc32_jit_tcb_t *block)
{
   struct ppc32_jit_patch_table *p,*next;

   for(p=block->patch_table;p;p=next) {
      next = p->next;
      free(p);
   }

   block->patch_table = NULL;
}

/* Adjust the JIT buffer if its size is not sufficient */
static int ppc32_jit_tcb_adjust_buffer(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block)
{
   insn_exec_page_t *new_buffer;

   if ((block->jit_ptr - block->jit_buffer->ptr) <= (PPC_JIT_BUFSIZE - 512))
      return(0);

#if DEBUG_BLOCK_CHUNK  
   printf("Block 0x%8.8x: adjusting JIT buffer...\n",block->start_ia);
#endif

   if (block->jit_chunk_pos >= PPC_JIT_MAX_CHUNKS) {
      fprintf(stderr,"Block 0x%8.8x: too many JIT chunks.\n",block->start_ia);
      return(-1);
   }

   if (!(new_buffer = exec_page_alloc(cpu)))
      return(-1);

   /* record the new exec page */
   block->jit_chunks[block->jit_chunk_pos++] = block->jit_buffer;
   block->jit_buffer = new_buffer;

   /* jump to the new exec page (link) */
   ppc32_jit_tcb_set_jump(block->jit_ptr,new_buffer->ptr);
   block->jit_ptr = new_buffer->ptr;
   return(0);
}

/* Allocate an instruction block */
static inline ppc32_jit_tcb_t *ppc32_jit_tcb_alloc(cpu_ppc_t *cpu)
{
   ppc32_jit_tcb_t *p;

   if (cpu->tcb_free_list) {
      p = cpu->tcb_free_list;
      cpu->tcb_free_list = p->next;
   } else {
      if (!(p = malloc(sizeof(*p))))
         return NULL;
   }

   memset(p,0,sizeof(*p));
   return p;
}

/* Free an instruction block */
void ppc32_jit_tcb_free(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block,
                         int list_removal)
{
   int i;

   if (block) {
      if (list_removal) {
         /* Remove the block from the linked list */
         if (block->next)
            block->next->prev = block->prev;
         else
            cpu->tcb_last = block->prev;

         if (block->prev)
            block->prev->next = block->next;
         else
            cpu->tcb_list = block->next;

         /* Remove the block from the physical mapping hash table */
         if (block->phys_pprev) {
            if (block->phys_next)
               block->phys_next->phys_pprev = block->phys_pprev;
            
            *(block->phys_pprev) = block->phys_next;
            
            block->phys_pprev = NULL;
            block->phys_next = NULL;
         }
      }

      /* Free the patch tables */
      ppc32_jit_tcb_free_patches(block);

      /* Free code pages */
      for(i=0;i<PPC_JIT_MAX_CHUNKS;i++)
         exec_page_free(cpu,block->jit_chunks[i]);

      /* Free the current JIT buffer */
      exec_page_free(cpu,block->jit_buffer);

      /* Free the PowerPC-to-native code mapping */
      free(block->jit_insn_ptr);
      
      block->next = cpu->tcb_free_list;
      cpu->tcb_free_list = block;
   }
}

/* Create an instruction block */
static ppc32_jit_tcb_t *ppc32_jit_tcb_create(cpu_ppc_t *cpu,m_uint32_t vaddr)
{
   ppc32_jit_tcb_t *block = NULL;
   m_uint32_t phys_page;

   if (unlikely(cpu->translate(cpu,cpu->ia,PPC32_MTS_ICACHE,&phys_page)))
      return NULL;

   if (!(block = ppc32_jit_tcb_alloc(cpu)))
      goto err_block_alloc;

   block->start_ia = vaddr;
   block->phys_page = phys_page;
   block->phys_hash = ppc32_jit_get_phys_hash(phys_page);   

   /* Allocate the first JIT buffer */
   if (!(block->jit_buffer = exec_page_alloc(cpu)))
      goto err_jit_alloc;

   block->jit_ptr = block->jit_buffer->ptr;
   block->ppc_code = cpu->mem_op_lookup(cpu,block->start_ia,PPC32_MTS_ICACHE);

   if (!block->ppc_code) {
      fprintf(stderr,"%% No memory map for code execution at 0x%8.8x\n",
              block->start_ia);
      goto err_lookup;
   }

#if DEBUG_BLOCK_TIMESTAMP
   block->tm_first_use = block->tm_last_use = jit_jiffies;
#endif
   return block;

 err_lookup:
 err_jit_alloc:
   ppc32_jit_tcb_free(cpu,block,FALSE);
 err_block_alloc:
   fprintf(stderr,"%% Unable to create instruction block for vaddr=0x%8.8x\n", 
           vaddr);
   return NULL;
}

/* Compile a PowerPC instruction page */
static inline 
ppc32_jit_tcb_t *ppc32_jit_tcb_compile(cpu_ppc_t *cpu,m_uint32_t vaddr)
{  
   ppc32_jit_tcb_t *block;
   struct ppc32_insn_tag *tag;
   m_uint32_t page_addr;
   size_t len;

   page_addr = vaddr & ~PPC32_MIN_PAGE_IMASK;

   if (unlikely(!(block = ppc32_jit_tcb_create(cpu,page_addr)))) {
      fprintf(stderr,"insn_page_compile: unable to create JIT block.\n");
      return NULL;
   }

   /* Allocate the array used to convert PPC code ptr to native code ptr */
   len = PPC32_MIN_PAGE_SIZE / sizeof(ppc_insn_t);

   if (!(block->jit_insn_ptr = calloc(len,sizeof(u_char *)))) {
      fprintf(stderr,"insn_page_compile: unable to create JIT mappings.\n");
      goto error;
   }

   /* Emit native code for each instruction */
   block->ppc_trans_pos = 0;

   while(block->ppc_trans_pos < (PPC32_MIN_PAGE_SIZE/sizeof(ppc_insn_t)))
   {
      if (unlikely(!(tag = ppc32_jit_fetch_and_emit(cpu,block)))) {
         fprintf(stderr,"insn_page_compile: unable to fetch instruction.\n");
         goto error;
      }

#if DEBUG_BLOCK_COMPILE
      printf("Page 0x%8.8x: emitted tag 0x%8.8x/0x%8.8x\n",
             block->start_ia,tag->mask,tag->value);
#endif

      ppc32_jit_tcb_adjust_buffer(cpu,block);
   }

   ppc32_jit_tcb_add_end(block);
   ppc32_jit_tcb_apply_patches(cpu,block);
   ppc32_jit_tcb_free_patches(block);

   /* Add the block to the linked list */
   block->next = cpu->tcb_list;
   block->prev = NULL;

   if (cpu->tcb_list)
      cpu->tcb_list->prev = block;
   else
      cpu->tcb_last = block;

   cpu->tcb_list = block;
   
   /* Add the block to the physical mapping hash table */
   block->phys_next = cpu->exec_phys_map[block->phys_hash];
   block->phys_pprev = &cpu->exec_phys_map[block->phys_hash];

   if (cpu->exec_phys_map[block->phys_hash] != NULL)
      cpu->exec_phys_map[block->phys_hash]->phys_pprev = &block->phys_next;

   cpu->exec_phys_map[block->phys_hash] = block;

   cpu->compiled_pages++;
   return block;

 error:
   ppc32_jit_tcb_free(cpu,block,FALSE);
   return NULL;
}

/* Run a compiled PowerPC instruction block */
static forced_inline 
void ppc32_jit_tcb_run(cpu_ppc_t *cpu,ppc32_jit_tcb_t *block)
{
   if (unlikely(cpu->ia & 0x03)) {
      fprintf(stderr,"ppc32_jit_tcb_run: Invalid IA 0x%8.8x.\n",cpu->ia);
      ppc32_dump_regs(cpu->gen);
      ppc32_dump_mmu(cpu->gen);
      cpu_stop(cpu->gen);
      return;
   }

   /* Execute JIT compiled code */
   ppc32_jit_tcb_exec(cpu,block);
}

/* Execute compiled PowerPC code */
void *ppc32_jit_run_cpu(cpu_gen_t *gen)
{    
   cpu_ppc_t *cpu = CPU_PPC32(gen);
   pthread_t timer_irq_thread;
   ppc32_jit_tcb_t *block;
   m_uint32_t ia_hash;
   int timer_irq_check = 0;

   if (pthread_create(&timer_irq_thread,NULL,(void *)ppc32_timer_irq_run,cpu)) 
   {
      fprintf(stderr,
              "VM '%s': unable to create Timer IRQ thread for CPU%u.\n",
              cpu->vm->name,gen->id);
      cpu_stop(cpu->gen);
      return NULL;
   }

   gen->cpu_thread_running = TRUE;

 start_cpu:   
   gen->idle_count = 0;

   for(;;) {
      if (unlikely(gen->state != CPU_STATE_RUNNING))
         break;

#if DEBUG_BLOCK_PERF_CNT
      cpu->perf_counter++;
#endif
      /* Handle virtual idle loop */
      if (unlikely(cpu->ia == cpu->idle_pc)) {
         if (++gen->idle_count == gen->idle_max) {
            cpu_idle_loop(gen);
            gen->idle_count = 0;
         }
      }

      /* Handle the virtual CPU clock */
      if (++timer_irq_check == cpu->timer_irq_check_itv) {
         timer_irq_check = 0;

         if (cpu->timer_irq_pending && !cpu->irq_disable &&
             (cpu->msr & PPC32_MSR_EE))
         {
            cpu->timer_irq_armed = 0;
            cpu->timer_irq_pending--;

            vm_set_irq(cpu->vm,0);
         }
      }

      /* Check IRQs */
      if (unlikely(cpu->irq_check))
         ppc32_trigger_irq(cpu);

      /* Get the JIT block corresponding to IA register */
      ia_hash = ppc32_jit_get_ia_hash(cpu->ia);
      block = cpu->exec_blk_map[ia_hash];

      /* No block found, compile the page */
      if (unlikely(!block) || unlikely(!ppc32_jit_tcb_match(cpu,block))) 
      {
         if (block != NULL) {
            ppc32_jit_tcb_free(cpu,block,TRUE);
            cpu->exec_blk_map[ia_hash] = NULL;
         }

         block = ppc32_jit_tcb_compile(cpu,cpu->ia);

         if (unlikely(!block)) {
            fprintf(stderr,
                    "VM '%s': unable to compile block for CPU%u IA=0x%8.8x\n",
                    cpu->vm->name,gen->id,cpu->ia);
            cpu_stop(gen);
            break;
         }

         cpu->exec_blk_map[ia_hash] = block;
      }

#if DEBUG_BLOCK_TIMESTAMP
      block->tm_last_use = jit_jiffies++;
#endif
      block->acc_count++;
      ppc32_jit_tcb_run(cpu,block);
   }
      
   if (!cpu->ia) {
      cpu_stop(gen);
      cpu_log(gen,"JIT","IA=0, halting CPU.\n");
   }

   /* Check regularly if the CPU has been restarted */
   while(gen->cpu_thread_running) {
      gen->seq_state++;

      switch(gen->state) {
         case CPU_STATE_RUNNING:
            gen->state = CPU_STATE_RUNNING;
            goto start_cpu;

         case CPU_STATE_HALTED:
            gen->cpu_thread_running = FALSE;
            pthread_join(timer_irq_thread,NULL);
            break;
      }
      
      /* CPU is paused */
      usleep(200000);
   }

   return NULL;
}
