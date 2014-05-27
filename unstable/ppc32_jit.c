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

/* Destroy instruction lookup table */
static void destroy_ilt(void)
{
   assert(ilt);
   ilt_destroy(ilt);
   ilt = NULL;
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

   atexit(destroy_ilt);
}

/* Initialize the JIT structure */
int ppc32_jit_init(cpu_ppc_t *cpu)
{
   insn_exec_page_t *cp;
   u_char *cp_addr;
   u_int area_size;
   size_t len;
   int i;

   /* Virtual address mapping for TCB */
   len = PPC_JIT_VIRT_HASH_SIZE * sizeof(void *);
   cpu->tcb_virt_hash = m_memalign(4096,len);
   memset(cpu->tcb_virt_hash,0,len);

   /* Physical address mapping for TCB */
   len = PPC_JIT_PHYS_HASH_SIZE * sizeof(void *);
   cpu->tcb_phys_hash = m_memalign(4096,len);
   memset(cpu->tcb_phys_hash,0,len);

   /* Get area size */
   if (!(area_size = cpu->vm->exec_area_size))
      area_size = PPC_EXEC_AREA_SIZE;

   /* Create executable page area */
   cpu->exec_page_area_size = area_size * 1048576;
   cpu->exec_page_area = memzone_map_exec_area(cpu->exec_page_area_size);

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
   m_uint32_t hv;
   u_int count = 0;

   if (!threshold)
      threshold = (u_int)(-1);  /* UINT_MAX not defined everywhere */

   for(p=cpu->tcb_list;p;p=next) {
      next = p->next;

      /* flushing not allowed */
      if (p->flags & PPC32_JIT_TCB_FLAG_NO_FLUSH)
         continue;

      if (p->acc_count <= threshold) {
         hv = ppc32_jit_get_virt_hash(p->start_ia);
         ppc32_jit_tcb_free(cpu,p,TRUE);

         if (cpu->tcb_virt_hash[hv] == p)
            cpu->tcb_virt_hash[hv] = NULL;
            
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
      memzone_unmap(cpu->exec_page_area,cpu->exec_page_area_size);

   /* Free the exec page array */
   free(cpu->exec_page_array);

   /* Free virtual and physical hash tables */
   free(cpu->tcb_virt_hash);
   free(cpu->tcb_phys_hash);
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
static forced_inline ppc_insn_t insn_fetch(ppc32_jit_tcb_t *tcb)
{
   return(vmtoh32(tcb->ppc_code[tcb->ppc_trans_pos]));
}

#define DEBUG_HREG  0

/* Show register allocation status */
_unused static void ppc32_jit_show_hreg_status(cpu_ppc_t *cpu)
{    
   struct hreg_map *map;

   printf("PPC32-JIT: reg status for insn '%s'\n",cpu->jit_hreg_seq_name);

   for(map=cpu->hreg_map_list;map;map=map->next) {
      switch(map->flags) {
         case 0:
            printf("   hreg %d is free, mapped to vreg %d\n",
                   map->hreg,map->vreg);
            break;
         case HREG_FLAG_ALLOC_LOCKED:
            printf("   hreg %d is locked, mapped to vreg %d\n",
                   map->hreg,map->vreg);
            break;
         case HREG_FLAG_ALLOC_FORCED:
            printf("   hreg %d is in forced alloc\n",map->hreg);
            break;
      }
   }
}

/* Extract an host reg mapping from the register list */
static void ppc32_jit_extract_hreg(cpu_ppc_t *cpu,struct hreg_map *map)
{
   if (map->prev != NULL)
      map->prev->next = map->next;
   else
      cpu->hreg_map_list = map->next;

   if (map->next != NULL)
      map->next->prev = map->prev;
   else
      cpu->hreg_lru = map->prev;
}

/* Insert a reg map as head of list (as MRU element) */
void ppc32_jit_insert_hreg_mru(cpu_ppc_t *cpu,struct hreg_map *map)
{
   map->next = cpu->hreg_map_list;
   map->prev = NULL;

   if (map->next == NULL) {
      cpu->hreg_lru = map;
   } else {
      map->next->prev = map;
   }

   cpu->hreg_map_list = map;
}

/* Start register allocation sequence */
void ppc32_jit_start_hreg_seq(cpu_ppc_t *cpu,char *insn)
{
   struct hreg_map *map;

#if DEBUG_HREG
   printf("Starting hreg_seq insn='%s'\n",insn);
#endif

   /* Reset the allocation state of all host registers */
   for(map=cpu->hreg_map_list;map;map=map->next)
      map->flags = 0;

   /* Save the instruction name for debugging/error analysis */
   cpu->jit_hreg_seq_name = insn;
}

/* Close register allocation sequence */
void ppc32_jit_close_hreg_seq(cpu_ppc_t *cpu)
{
#if DEBUG_HREG
   ppc32_show_hreg_status(cpu);
#endif
}

/* Find a free host register to use */
static struct hreg_map *ppc32_jit_get_free_hreg(cpu_ppc_t *cpu)
{
   struct hreg_map *map,*oldest_free = NULL;

   for(map=cpu->hreg_lru;map;map=map->prev) {
      if ((map->vreg == -1) && (map->flags == 0))
         return map;

      if ((map->flags == 0) && !oldest_free)
         oldest_free = map;
   }

   if (!oldest_free) {
      fprintf(stderr,
              "ppc32_get_free_hreg: unable to find free reg for insn %s\n",
              cpu->jit_hreg_seq_name);
   }

   return oldest_free;
}

/* Allocate an host register */
int ppc32_jit_alloc_hreg(cpu_ppc_t *cpu,int ppc_reg)
{
   struct hreg_map *map;
   int hreg;

   /* 
    * If PPC reg is invalid, the caller requested for a temporary register.
    */
   if (ppc_reg == -1) {
      if ((map = ppc32_jit_get_free_hreg(cpu)) == NULL)
         return(-1);

      /* Allocate the register and invalidate its PPC mapping if present */
      map->flags = HREG_FLAG_ALLOC_LOCKED;

      if (map->vreg != -1) {
         cpu->ppc_reg_map[map->vreg] = -1;
         map->vreg = -1;
      }

      return(map->hreg);
   }

   hreg = cpu->ppc_reg_map[ppc_reg];

   /* 
    * If the PPC register is already mapped to an host register, re-use this
    * mapping and put this as MRU mapping.
    */
   if (hreg != -1) {
      map = &cpu->hreg_map[hreg];
   } else {
      /* 
       * This PPC register has no mapping to host register. Find a free
       * register.
       */
      if ((map = ppc32_jit_get_free_hreg(cpu)) == NULL)
         return(-1);

      /* Remove the old PPC mapping if present */
      if (map->vreg != -1)
         cpu->ppc_reg_map[map->vreg] = -1;
      
      /* Establish the new mapping */
      cpu->ppc_reg_map[ppc_reg] = map->hreg;
      map->vreg = ppc_reg;
   }

   /* Prevent this register from further allocation in this instruction */
   map->flags = HREG_FLAG_ALLOC_LOCKED;
   ppc32_jit_extract_hreg(cpu,map);
   ppc32_jit_insert_hreg_mru(cpu,map);
   return(map->hreg);
}

/* Force allocation of an host register */
int ppc32_jit_alloc_hreg_forced(cpu_ppc_t *cpu,int hreg)
{
   int ppc_reg;

   ppc_reg = cpu->hreg_map[hreg].vreg;

   /* Check that this register is not already allocated */
   if (cpu->hreg_map[hreg].flags != 0) {
      fprintf(stderr,"ppc32_alloc_hreg_forced: trying to force allocation "
              "of hreg %d (insn %s)\n",
              hreg,cpu->jit_hreg_seq_name);
      return(-1);
   }

   cpu->hreg_map[hreg].flags = HREG_FLAG_ALLOC_FORCED;
   cpu->hreg_map[hreg].vreg  = -1;

   if (ppc_reg != -1)
      cpu->ppc_reg_map[ppc_reg] = -1;

   return(0);
}

/* Emit a breakpoint if necessary */
#if BREAKPOINT_ENABLE
static void insn_emit_breakpoint(cpu_ppc_t *cpu,ppc32_jit_tcb_t *tcb)
{
   m_uint32_t ia;
   int i;

   ia = tcb->start_ia + ((tcb->ppc_trans_pos-1)<<2);

   for(i=0;i<PPC32_MAX_BREAKPOINTS;i++)
      if (ia == cpu->breakpoints[i]) {
         ppc32_emit_breakpoint(cpu,tcb);
         break;
      }
}
#endif /* BREAKPOINT_ENABLE */

/* Fetch a PowerPC instruction and emit corresponding translated code */
struct ppc32_insn_tag *ppc32_jit_fetch_and_emit(cpu_ppc_t *cpu,
                                                ppc32_jit_tcb_t *tcb)
{
   struct ppc32_insn_tag *tag;
   ppc_insn_t code;

   code = insn_fetch(tcb);
   tag = insn_tag_find(code);
   assert(tag);

   tag->emit(cpu,tcb,code);
   return tag;
}

/* Add end of JIT block */
_unused static void ppc32_jit_tcb_add_end(ppc32_jit_tcb_t *tcb)
{
   ppc32_set_ia(&tcb->jit_ptr,tcb->start_ia+(tcb->ppc_trans_pos<<2));
   ppc32_jit_tcb_push_epilog(&tcb->jit_ptr);
}

/* Record a patch to apply in a compiled block */
int ppc32_jit_tcb_record_patch(ppc32_jit_tcb_t *tcb,jit_op_t *iop,
                               u_char *jit_ptr,m_uint32_t vaddr)
{
   struct ppc32_jit_patch_table *ipt = tcb->patch_table;
   struct ppc32_insn_patch *patch;

   /* pc must be 32-bit aligned */
   if (vaddr & 0x03) {
      fprintf(stderr,
              "TCB 0x%8.8x: trying to record an invalid IA (0x%8.8x)\n",
              tcb->start_ia,vaddr);
      return(-1);
   }

   if (!ipt || (ipt->cur_patch >= PPC32_INSN_PATCH_TABLE_SIZE))
   {
      /* full table or no table, create a new one */
      ipt = malloc(sizeof(*ipt));
      if (!ipt) {
         fprintf(stderr,"Block 0x%8.8x: unable to create patch table.\n",
                 tcb->start_ia);
         return(-1);
      }

      memset(ipt,0,sizeof(*ipt));
      ipt->next = tcb->patch_table;
      tcb->patch_table = ipt;
   }

#if DEBUG_BLOCK_PATCH
   printf("TCB 0x%8.8x: recording patch [JIT:%p->ppc:0x%8.8x], "
          "MTP=%d\n",tcb->start_ia,jit_ptr,vaddr,tcb->ppc_trans_pos);
#endif

   patch = &ipt->patches[ipt->cur_patch];
   patch->jit_insn = jit_ptr;
   patch->ppc_ia = vaddr;
   ipt->cur_patch++; 

   patch->next = iop->arg_ptr;
   iop->arg_ptr = patch;
   return(0);
}

/* Apply patches for a JIT instruction block */
static int ppc32_jit_tcb_apply_patches(cpu_ppc_t *cpu,
                                       ppc32_jit_tcb_t *tcb,
                                       jit_op_t *iop)
{
   struct ppc32_insn_patch *patch;
   u_char *jit_ptr,*jit_dst;
   u_int pos;

   for(patch=iop->arg_ptr;patch;patch=patch->next) {
      jit_ptr = (patch->jit_insn - iop->ob_data) + iop->ob_final;

      pos = (patch->ppc_ia & PPC32_MIN_PAGE_IMASK) >> 2;
      jit_dst = tcb->jit_insn_ptr[pos];

      if (jit_dst) {
#if DEBUG_BLOCK_PATCH       
         printf("TCB 0x%8.8x: applying patch "
                "[JIT:%p->ppc:0x%8.8x=JIT:%p, ]\n",
                tcb->start_ia,patch->jit_insn,patch->ppc_ia,jit_dst);
#endif
         ppc32_jit_tcb_set_patch(jit_ptr,jit_dst);
      } else {
         printf("TCB 0x%8.8x: null dst for patch!\n",tcb->start_ia);
      }
   }
   
   return(0);
}

/* Free the patch table */
static void ppc32_jit_tcb_free_patches(ppc32_jit_tcb_t *tcb)
{
   struct ppc32_jit_patch_table *p,*next;

   for(p=tcb->patch_table;p;p=next) {
      next = p->next;
      free(p);
   }

   tcb->patch_table = NULL;
}

/* Adjust the JIT buffer if its size is not sufficient */
static int ppc32_jit_tcb_adjust_buffer(cpu_ppc_t *cpu,ppc32_jit_tcb_t *tcb)
{
   insn_exec_page_t *new_buffer;

   if ((tcb->jit_ptr - tcb->jit_buffer->ptr) <= (PPC_JIT_BUFSIZE - 512))
      return(0);

#if DEBUG_BLOCK_CHUNK  
   printf("TCB 0x%8.8x: adjusting JIT buffer...\n",tcb->start_ia);
#endif

   if (tcb->jit_chunk_pos >= PPC_JIT_MAX_CHUNKS) {
      fprintf(stderr,"TCB 0x%8.8x: too many JIT chunks.\n",tcb->start_ia);
      return(-1);
   }

   if (!(new_buffer = exec_page_alloc(cpu)))
      return(-1);

   /* record the new exec page */
   tcb->jit_chunks[tcb->jit_chunk_pos++] = tcb->jit_buffer;
   tcb->jit_buffer = new_buffer;

   /* jump to the new exec page (link) */
   ppc32_jit_tcb_set_jump(tcb->jit_ptr,new_buffer->ptr);
   tcb->jit_ptr = new_buffer->ptr;
   return(0);
}

/* Allocate an instruction block */
static inline ppc32_jit_tcb_t *ppc32_jit_tcb_alloc(cpu_ppc_t *cpu)
{
   ppc32_jit_tcb_t *tcb;

   if (cpu->tcb_free_list) {
      tcb = cpu->tcb_free_list;
      cpu->tcb_free_list = tcb->next;
   } else {
      if (!(tcb = malloc(sizeof(*tcb))))
         return NULL;
   }

   memset(tcb,0,sizeof(*tcb));
   return tcb;
}

/* Free the code chunks */
static void ppc32_jit_tcb_free_code_chunks(cpu_ppc_t *cpu,ppc32_jit_tcb_t *tcb)
{
   int i;

   /* Free code pages */
   for(i=0;i<PPC_JIT_MAX_CHUNKS;i++) {
      exec_page_free(cpu,tcb->jit_chunks[i]);
      tcb->jit_chunks[i] = NULL;
   }

   /* Free the current JIT buffer */
   exec_page_free(cpu,tcb->jit_buffer);
   tcb->jit_buffer = NULL;
}

/* Free the generated code stuff */
static void ppc32_jit_flush_gen_code(cpu_ppc_t *cpu,ppc32_jit_tcb_t *tcb)
{
   /* Free the patch tables */
   ppc32_jit_tcb_free_patches(tcb);

   /* Free code pages */
   ppc32_jit_tcb_free_code_chunks(cpu,tcb);

   /* Free the PowerPC-to-native code mapping */
   free(tcb->jit_insn_ptr);
   tcb->jit_insn_ptr = NULL;
}

/* Mark a block as containing self-modifying code */
void ppc32_jit_mark_smc(cpu_ppc_t *cpu,ppc32_jit_tcb_t *tcb)
{
   if (tcb->flags & PPC32_JIT_TCB_FLAG_SMC)
      return; /* already done */

   tcb->flags |= PPC32_JIT_TCB_FLAG_SMC;
   ppc32_jit_flush_gen_code(cpu,tcb);
}

/* Free an instruction block */
void ppc32_jit_tcb_free(cpu_ppc_t *cpu,ppc32_jit_tcb_t *tcb,int list_removal)
{
   if (tcb != NULL) {
      if (list_removal) {
         /* Remove the block from the linked list */
         if (tcb->next)
            tcb->next->prev = tcb->prev;
         else
            cpu->tcb_last = tcb->prev;

         if (tcb->prev)
            tcb->prev->next = tcb->next;
         else
            cpu->tcb_list = tcb->next;

         /* Remove the block from the physical mapping hash table */
         if (tcb->phys_pprev) {
            if (tcb->phys_next)
               tcb->phys_next->phys_pprev = tcb->phys_pprev;
            
            *(tcb->phys_pprev) = tcb->phys_next;
            
            tcb->phys_pprev = NULL;
            tcb->phys_next = NULL;
         }
      }

      /* Free generated code information */
      ppc32_jit_flush_gen_code(cpu,tcb);
      
      tcb->next = cpu->tcb_free_list;
      cpu->tcb_free_list = tcb;
   }
}

/* Create an instruction block */
static ppc32_jit_tcb_t *
ppc32_jit_tcb_create(cpu_ppc_t *cpu,m_uint32_t vaddr,m_uint32_t exec_state)
{
   ppc32_jit_tcb_t *tcb = NULL;
   m_uint32_t phys_page;
   ppc_insn_t *ppc_code;

   /* 
    * Get the powerpc code address from the host point of view.
    * If there is an error (TLB,...), we return directly to the main loop.
    */
   ppc_code = cpu->mem_op_ifetch(cpu,vaddr);

   if (unlikely(cpu->translate(cpu,cpu->ia,PPC32_MTS_ICACHE,&phys_page)))
      return NULL;

   if (!(tcb = ppc32_jit_tcb_alloc(cpu)))
      goto err_block_alloc;

   tcb->start_ia   = vaddr;
   tcb->exec_state = exec_state;
   tcb->phys_page  = phys_page;
   tcb->phys_hash  = ppc32_jit_get_phys_hash(phys_page);   

   /* Allocate the first JIT buffer */
   if (!(tcb->jit_buffer = exec_page_alloc(cpu)))
      goto err_jit_alloc;

   tcb->jit_ptr = tcb->jit_buffer->ptr;
   tcb->ppc_code = ppc_code;

   if (!tcb->ppc_code) {
      fprintf(stderr,"%% No memory map for code execution at 0x%8.8x\n",
              tcb->start_ia);
      goto err_lookup;
   }

#if DEBUG_BLOCK_TIMESTAMP
   tcb->tm_first_use = tcb->tm_last_use = jit_jiffies;
#endif
   return tcb;

 err_lookup:
 err_jit_alloc:
   ppc32_jit_tcb_free(cpu,tcb,FALSE);
 err_block_alloc:
   fprintf(stderr,"%% Unable to create instruction block for vaddr=0x%8.8x\n", 
           vaddr);
   return NULL;
}

/* ======================================================================== */

/* Dump a JIT opcode */
static void ppc32_op_dump_opcode(jit_op_t *op)
{
   switch(op->opcode) {
      case JIT_OP_BRANCH_TARGET:
         printf("branch_target");
         break;
      case JIT_OP_BRANCH_JUMP:
         printf("branch_jump");
         break;
      case JIT_OP_EOB:
         printf("eob");
         break;
      case JIT_OP_LOAD_GPR:
         printf("load_gpr(%d,$%d,r:%d)",
                op->param[0],op->param[1],op->param[2]);
         break;
      case JIT_OP_STORE_GPR:
         printf("store_gpr(%d,$%d,r:%d)",
                op->param[0],op->param[1],op->param[2]);
         break;
      case JIT_OP_ALTER_HOST_REG:
         printf("alter_host_reg(%d)",op->param[0]);
         break;
      case JIT_OP_UPDATE_FLAGS:
         printf("update_flags(%d,%s)",
                op->param[0],(op->param[1] ? "signed" : "unsigned"));
         break;
      case JIT_OP_REQUIRE_FLAGS:
         printf("require_flags(%d)",op->param[0]);
         break;
      case JIT_OP_TRASH_FLAGS:
         printf("trash_flags(%d)",op->param[0]);
         break;
      case JIT_OP_INSN_OUTPUT:
         printf("insn_out(\"%s\")",op->insn_name);
         break;
      case JIT_OP_SET_HOST_REG_IMM32:
         printf("set_host_reg_imm32(%d,0x%8.8x)",op->param[0],op->param[1]);
         break;
      default:
         printf("op(%u)",op->opcode);
   }
}

/* Dump JIT operations (debugging) */
_unused static void ppc32_op_dump(cpu_gen_t *cpu,ppc32_jit_tcb_t *tcb)
{
   m_uint32_t ia = tcb->start_ia;
   jit_op_t *op;
   int i;

   printf("PPC32-JIT: dump of page 0x%8.8x\n",ia);

   for(i=0;i<PPC32_INSN_PER_PAGE;i++,ia+=sizeof(ppc_insn_t)) {
      printf("  0x%8.8x: ", ia);

      for(op=cpu->jit_op_array[i];op;op=op->next) {
         ppc32_op_dump_opcode(op);
         printf(" ");
      }

      printf("\n");
   }

   printf("\n");
}

/* PPC register mapping */
typedef struct {
   int host_reg;
   jit_op_t *last_store;
   m_uint32_t last_store_ia;
}ppc_reg_map_t;

/* Clear register mapping (with PPC register) */
static void ppc32_clear_ppc_reg_map(ppc_reg_map_t *ppc_map,int *host_map,
                                    int reg)
{
   int i,hreg;

   if (reg == JIT_OP_ALL_REGS) {
      for(i=0;i<PPC32_GPR_NR;i++) {
         ppc_map[i].host_reg = JIT_OP_INV_REG;
         ppc_map[i].last_store = NULL;
      }

      for(i=0;i<JIT_HOST_NREG;i++)
         host_map[i] = JIT_OP_INV_REG;
   } else {
      hreg = ppc_map[reg].host_reg;

      if (hreg != JIT_OP_INV_REG)
         host_map[hreg] = JIT_OP_INV_REG;
      
      ppc_map[reg].host_reg = JIT_OP_INV_REG;
      ppc_map[reg].last_store = NULL;
   }
}

/* Clear register mapping (with host register) */
static void ppc32_clear_host_reg_map(ppc_reg_map_t *ppc_map,int *host_map,
                                     int reg)
{
   int ppc_reg;

   if (host_map[reg] != JIT_OP_INV_REG) {
      ppc_reg = host_map[reg];

      ppc_map[ppc_reg].host_reg = JIT_OP_INV_REG;
      ppc_map[ppc_reg].last_store = NULL;
      host_map[reg] = JIT_OP_INV_REG;
   }
}

/* Dump register mapping */
static void ppc32_dump_reg_map(ppc_reg_map_t *map_array,int *host_map)
{
   int i;

   printf("PPC32-JIT: current register mapping:\n");

   for(i=0;i<PPC32_GPR_NR;i++)
      printf("  ppc reg %2.2d: %d\n",i,map_array[i].host_reg);

   printf("\n");

   for(i=0;i<JIT_HOST_NREG;i++)
      printf("  hreg %d: %d\n",i,host_map[i]);

   printf("\n");
}

/* Check register mapping consistency */
_maybe_used static int ppc32_check_reg_map(ppc_reg_map_t *map_array,int *host_map)
{
   ppc_reg_map_t *map;
   int i;

   for(i=0;i<PPC32_GPR_NR;i++) {
      map = &map_array[i];

      if ((map->host_reg != JIT_OP_INV_REG) && (host_map[map->host_reg] != i)) 
         goto error;
   }

   for(i=0;i<JIT_HOST_NREG;i++) {
      if ((host_map[i] != JIT_OP_INV_REG) && 
          (map_array[host_map[i]].host_reg != i))
         goto error;
   }

   return(0);

 error:
   printf("PPC32_JIT: inconsistency in register mapping.\n");
   ppc32_dump_reg_map(map_array,host_map);
   exit(1);
}

/* Optimize JIT operations */
static void ppc32_op_optimize(cpu_gen_t *cpu,ppc32_jit_tcb_t *tcb)
{
   ppc_reg_map_t ppc_map[PPC32_GPR_NR],*map;
   int reg,host_map[JIT_HOST_NREG];
   jit_op_t *op,*opx,*last_cr_update[8];
   m_uint32_t cur_ia;
   int i,j;

   ppc32_clear_ppc_reg_map(ppc_map,host_map,JIT_OP_ALL_REGS);

   for(i=0;i<8;i++)
      last_cr_update[i] = NULL;

   for(i=0;i<PPC32_INSN_PER_PAGE;i++) {
      for(op=cpu->jit_op_array[i];op;op=op->next) 
      {
         //ppc32_check_reg_map(ppc_map,host_map);
         cur_ia = tcb->start_ia + (i << 2);

         switch(op->opcode) {
            /* Clear mapping if end of block or branch target */
            case JIT_OP_BRANCH_TARGET:
            case JIT_OP_EOB:
               ppc32_clear_ppc_reg_map(ppc_map,host_map,JIT_OP_ALL_REGS);

               for(j=0;j<8;j++)
                  last_cr_update[j] = NULL;
               break;

            /* Branch jump: clear "store" operation status */
            case JIT_OP_BRANCH_JUMP:
               for(j=0;j<PPC32_GPR_NR;j++)
                  ppc_map[j].last_store = NULL;

               for(j=0;j<8;j++)
                  last_cr_update[j] = NULL;
               break;

            /* Alteration of a specific host register */
            case JIT_OP_ALTER_HOST_REG:
               reg = op->param[0];

               if (reg != JIT_OP_ALL_REGS) {
                  if (host_map[reg] != JIT_OP_INV_REG)
                     ppc32_clear_ppc_reg_map(ppc_map,host_map,host_map[reg]);
               } else {
                  ppc32_clear_ppc_reg_map(ppc_map,host_map,JIT_OP_ALL_REGS);
               }
               break;

            /* Save reg mapping and last operation */
            case JIT_OP_STORE_GPR:
               reg = op->param[0];
               map = &ppc_map[op->param[1]];

               /* clear old mapping */
               if (reg != map->host_reg) {
                  ppc32_clear_host_reg_map(ppc_map,host_map,reg);
                  ppc32_clear_ppc_reg_map(ppc_map,host_map,op->param[1]);
               }
               
               /* cancel previous store op for this PPC register */
               if (map->last_store) {
                  map->last_store->param[0] = JIT_OP_INV_REG;
                  map->last_store = NULL;
               }

               map->host_reg = reg;
               map->last_store = op;
               map->last_store_ia = cur_ia;
               host_map[reg] = op->param[1];
               break;

            /* Load reg: check if can avoid it */
            case JIT_OP_LOAD_GPR:
               reg = op->param[0];
               map = &ppc_map[op->param[1]];

               if (reg == map->host_reg) {
                  /* Cancel this load */
                  op->param[0] = JIT_OP_INV_REG;
               } else {
                  /* clear old mapping */
                  ppc32_clear_host_reg_map(ppc_map,host_map,reg);
                  ppc32_clear_ppc_reg_map(ppc_map,host_map,op->param[1]);

                  /* Save this reg mapping */
                  map->host_reg = op->param[0];
                  map->last_store = NULL;
                  host_map[op->param[0]] = op->param[1];
               }
               break;

            /* Trash flags */
            case JIT_OP_TRASH_FLAGS:
               for(j=0;j<8;j++)
                  last_cr_update[j] = NULL;
               break;

            /* Flags required */
            case JIT_OP_REQUIRE_FLAGS:
               if (op->param[0] != JIT_OP_PPC_ALL_FLAGS) {
                  last_cr_update[op->param[0]] = NULL;
               } else {
                  for(j=0;j<8;j++)
                     last_cr_update[j] = NULL;
               }
               break;

            /* Update flags */
            case JIT_OP_UPDATE_FLAGS:
               opx = last_cr_update[op->param[0]];

               if (opx != NULL)
                  opx->param[0] = JIT_OP_INV_REG;

               last_cr_update[op->param[0]] = op;
               break;
         }
      }
   }
}

/* Generate the JIT code for the specified JIT op list */
static void ppc32_op_gen_list(ppc32_jit_tcb_t *tcb,int ipos,jit_op_t *op_list,
                              u_char *jit_start)
{
   jit_op_t *op;

   for(op=op_list;op;op=op->next) {
      switch(op->opcode) {
         case JIT_OP_INSN_OUTPUT:
            ppc32_op_insn_output(tcb,op);
            break;
         case JIT_OP_LOAD_GPR:
            ppc32_op_load_gpr(tcb,op);
            break;
         case JIT_OP_STORE_GPR:
            ppc32_op_store_gpr(tcb,op);
            break;
         case JIT_OP_UPDATE_FLAGS:
            ppc32_op_update_flags(tcb,op);
            break;
         case JIT_OP_BRANCH_TARGET:
            tcb->jit_insn_ptr[ipos] = jit_start;
            break;
         case JIT_OP_MOVE_HOST_REG:
            ppc32_op_move_host_reg(tcb,op);
            break;
         case JIT_OP_SET_HOST_REG_IMM32:
            ppc32_op_set_host_reg_imm32(tcb,op);
            break;
      }
   }
}

/* Opcode emit start */
static inline void ppc32_op_emit_start(cpu_ppc_t *cpu,ppc32_jit_tcb_t *tcb)
{
   cpu_gen_t *c = cpu->gen;
   jit_op_t *op;

   if (c->jit_op_array[tcb->ppc_trans_pos] == NULL)
      c->jit_op_current = &c->jit_op_array[tcb->ppc_trans_pos];
   else {
      for(op=c->jit_op_array[tcb->ppc_trans_pos];op;op=op->next)
         c->jit_op_current = &op->next;
   }
}

/* Generate the JIT code for the current page, given an op list */
static int ppc32_op_gen_page(cpu_ppc_t *cpu,ppc32_jit_tcb_t *tcb)
{   
   struct ppc32_insn_tag *tag;
   cpu_gen_t *gcpu = cpu->gen;
   jit_op_t *iop;
   m_uint32_t cur_ia;
   u_char *jit_ptr;
   int i;

   /* Generate JIT opcodes */
   for(tcb->ppc_trans_pos=0;
       tcb->ppc_trans_pos<PPC32_INSN_PER_PAGE;
       tcb->ppc_trans_pos++) 
   {
      ppc32_op_emit_start(cpu,tcb);

      cur_ia = tcb->start_ia + (tcb->ppc_trans_pos << 2);

      if (ppc32_jit_tcb_get_target_bit(tcb,cur_ia))
         ppc32_op_emit_basic_opcode(cpu,JIT_OP_BRANCH_TARGET);

#if DEBUG_INSN_PERF_CNT
      ppc32_inc_perf_counter(cpu);
#endif
#if BREAKPOINT_ENABLE
      if (cpu->breakpoints_enabled)
         insn_emit_breakpoint(cpu,tcb);
#endif

      if (unlikely(!(tag = ppc32_jit_fetch_and_emit(cpu,tcb)))) {
         fprintf(stderr,"ppc32_op_gen_page: unable to fetch instruction.\n");
         return(-1);
      }
   }

   /* 
    * Mark the first instruction as a potential target, as well as the 
    * current IA value.
    */
   ppc32_op_emit_branch_target(cpu,tcb,tcb->start_ia);
   ppc32_op_emit_branch_target(cpu,tcb,cpu->ia);

   /* Optimize condition register and general registers */
   ppc32_op_optimize(gcpu,tcb);

   /* Generate JIT code for each instruction in page */
   for(i=0;i<PPC32_INSN_PER_PAGE;i++) 
   {
      jit_ptr = tcb->jit_ptr;

      /* Generate output code */
      ppc32_op_gen_list(tcb,i,gcpu->jit_op_array[i],jit_ptr);

      /* Adjust the JIT buffer if its size is not sufficient */
      ppc32_jit_tcb_adjust_buffer(cpu,tcb);
   }

   /* Apply patches and free opcodes */
   for(i=0;i<PPC32_INSN_PER_PAGE;i++) {
      for(iop=gcpu->jit_op_array[i];iop;iop=iop->next)
         if (iop->opcode == JIT_OP_INSN_OUTPUT)
            ppc32_jit_tcb_apply_patches(cpu,tcb,iop);

      jit_op_free_list(gcpu,gcpu->jit_op_array[i]);
      gcpu->jit_op_array[i] = NULL;
   }

   /* Add end of page (returns to caller) */
   ppc32_set_page_jump(cpu,tcb);

   /* Free patch tables */
   ppc32_jit_tcb_free_patches(tcb);
   return(0);
}

/* ======================================================================== */

/* Compile a PowerPC instruction page */
static ppc32_jit_tcb_t *
ppc32_jit_tcb_compile(cpu_ppc_t *cpu,m_uint32_t vaddr,m_uint32_t exec_state)
{  
   ppc32_jit_tcb_t *tcb;
   m_uint32_t page_addr;

   page_addr = vaddr & PPC32_MIN_PAGE_MASK;

   if (unlikely(!(tcb = ppc32_jit_tcb_create(cpu,page_addr,exec_state)))) {
      fprintf(stderr,"insn_page_compile: unable to create JIT block.\n");
      return NULL;
   }

   /* Allocate the array used to convert PPC code ptr to native code ptr */
   if (!(tcb->jit_insn_ptr = calloc(PPC32_INSN_PER_PAGE,sizeof(u_char *)))) {
      fprintf(stderr,"insn_page_compile: unable to create JIT mappings.\n");
      goto error;
   }

   /* Compile the page */
   if (ppc32_op_gen_page(cpu,tcb) == -1) {
      fprintf(stderr,"insn_page_compile: unable to compile page.\n");
      goto error;
   }
   
   /* Add the block to the linked list */
   tcb->next = cpu->tcb_list;
   tcb->prev = NULL;

   if (cpu->tcb_list)
      cpu->tcb_list->prev = tcb;
   else
      cpu->tcb_last = tcb;

   cpu->tcb_list = tcb;
   
   /* Add the block to the physical mapping hash table */
   tcb->phys_next = cpu->tcb_phys_hash[tcb->phys_hash];
   tcb->phys_pprev = &cpu->tcb_phys_hash[tcb->phys_hash];

   if (cpu->tcb_phys_hash[tcb->phys_hash] != NULL)
      cpu->tcb_phys_hash[tcb->phys_hash]->phys_pprev = &tcb->phys_next;

   cpu->tcb_phys_hash[tcb->phys_hash] = tcb;

   cpu->compiled_pages++;
   return tcb;

 error:
   ppc32_jit_tcb_free(cpu,tcb,FALSE);
   return NULL;
}

/* Recompile a page */
int ppc32_jit_tcb_recompile(cpu_ppc_t *cpu,ppc32_jit_tcb_t *tcb)
{
#if 0
   printf("PPC32-JIT: recompiling page 0x%8.8x\n",tcb->start_ia);
#endif

   /* Free old code chunks */
   ppc32_jit_tcb_free_code_chunks(cpu,tcb);

   /* Reset code ptr array */
   memset(tcb->jit_insn_ptr,0,PPC32_INSN_PER_PAGE * sizeof(u_char *));

   /* Allocate the first JIT buffer */
   if (!(tcb->jit_buffer = exec_page_alloc(cpu)))
      return(-1);

   /* Disable flushing to avoid dangling pointers */
   tcb->flags |= PPC32_JIT_TCB_FLAG_NO_FLUSH;

   /* Recompile the page */
   if (ppc32_op_gen_page(cpu,tcb) == -1) {
      fprintf(stderr,"insn_page_compile: unable to recompile page.\n");
      return(-1);
   }

   tcb->flags &= ~PPC32_JIT_TCB_FLAG_NO_FLUSH;
   tcb->target_undef_cnt = 0;
   return(0);
}

/* Run a compiled PowerPC instruction block */
static forced_inline
void ppc32_jit_tcb_run(cpu_ppc_t *cpu,ppc32_jit_tcb_t *tcb)
{
   if (unlikely(cpu->ia & 0x03)) {
      fprintf(stderr,"ppc32_jit_tcb_run: Invalid IA 0x%8.8x.\n",cpu->ia);
      ppc32_dump_regs(cpu->gen);
      ppc32_dump_mmu(cpu->gen);
      cpu_stop(cpu->gen);
      return;
   }

   /* Execute JIT compiled code */
   ppc32_jit_tcb_exec(cpu,tcb);
}

/* Execute compiled PowerPC code */
void *ppc32_jit_run_cpu(cpu_gen_t *gen)
{    
   cpu_ppc_t *cpu = CPU_PPC32(gen);
   pthread_t timer_irq_thread;
   ppc32_jit_tcb_t *tcb;
   m_uint32_t hv,hp;
   m_uint32_t phys_page;
   int timer_irq_check = 0;

   ppc32_jit_init_hreg_mapping(cpu);

   if (pthread_create(&timer_irq_thread,NULL,(void *)ppc32_timer_irq_run,cpu)) 
   {
      fprintf(stderr,
              "VM '%s': unable to create Timer IRQ thread for CPU%u.\n",
              cpu->vm->name,gen->id);
      cpu_stop(cpu->gen);
      return NULL;
   }

   gen->cpu_thread_running = TRUE;
   cpu_exec_loop_set(gen);

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
      hv = ppc32_jit_get_virt_hash(cpu->ia);
      tcb = cpu->tcb_virt_hash[hv];

      if (unlikely(!tcb) || unlikely(!ppc32_jit_tcb_match(cpu,tcb))) 
      {
         /* slow lookup: try to find the page by physical address */
         cpu->translate(cpu,cpu->ia,PPC32_MTS_ICACHE,&phys_page);
         hp = ppc32_jit_get_phys_hash(phys_page);

         for(tcb=cpu->tcb_phys_hash[hp];tcb;tcb=tcb->phys_next)
            if (ppc32_jit_tcb_match(cpu,tcb))
               goto tcb_found;

         /* the tcb doesn't exist, compile the page */
         tcb = ppc32_jit_tcb_compile(cpu,cpu->ia,cpu->exec_state);

         if (unlikely(!tcb)) {
            fprintf(stderr,
                    "VM '%s': unable to compile block for CPU%u IA=0x%8.8x\n",
                    cpu->vm->name,gen->id,cpu->ia);
            cpu_stop(gen);
            break;
         }

        tcb_found:
         /* update the virtual hash table */
         cpu->tcb_virt_hash[hv] = tcb;
      }

#if DEBUG_BLOCK_TIMESTAMP
      tcb->tm_last_use = jit_jiffies++;
#endif
      tcb->acc_count++;
      
      if (unlikely(tcb->flags & PPC32_JIT_TCB_FLAG_SMC))
         ppc32_exec_page(cpu);
      else
         ppc32_jit_tcb_run(cpu,tcb);
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
