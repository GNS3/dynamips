/*
 * Cisco router simulation platform.
 * Copyright (c) 2008 Christophe Fillot (cf@utc.fr)
 *
 * Translation Sharing Groups.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <assert.h>

#include "device.h"
#include "pci_dev.h"
#include "pci_io.h"
#include "cpu.h"
#include "vm.h"
#include "tcb.h"

#define DEBUG_JIT_FLUSH          0
#define DEBUG_JIT_BUFFER_ADJUST  0
#define DEBUG_JIT_PATCH          0

/* Size of a JIT page */
#define TC_JIT_PAGE_SIZE  32768

/* CPU provisionning */
#ifndef __CYGWIN__
#define TSG_EXEC_AREA_SINGLE_CPU  64
#else
#define TSG_EXEC_AREA_SINGLE_CPU  16
#endif
#define TSG_EXEC_AREA_SHARED      64

/* Minimal number of exec pages to have to satisfy an allocation request */
#define TCB_MIN_EXEC_PAGES  (2 * TCB_DESC_MAX_CHUNKS)

/* Maximum number of translation sharing groups */
#define TSG_MAX_GROUPS  128

/* Hash table to retrieve Translated Code descriptors from their checksums */
#define TC_HASH_BITS   16
#define TC_HASH_SIZE   (1 << TC_HASH_BITS)
#define TC_HASH_MASK   (TC_HASH_SIZE - 1)

typedef struct tsg tsg_t;
struct tsg {
   /* Lock to synchronize multiple CPU accesses */
   pthread_mutex_t lock;
   pthread_mutexattr_t lock_attr;   

   /* Hash table to retrieve Translated Code */
   cpu_tc_t **tc_hash;
   
   /* Free list of TC descriptors */
   cpu_tc_t *tc_free_list;
   
   /* List of CPUs attached to this group */
   cpu_gen_t *cpu_list;

   /* Exec page allocator */
   void *exec_area;
   
   insn_exec_page_t *exec_page_array;
   insn_exec_page_t *exec_page_free_list;

   size_t exec_area_alloc_size;
   u_int exec_page_alloc,exec_page_total;
   u_int exec_area_full;
};

#define TSG_LOCK(g)   pthread_mutex_lock(&(g)->lock)
#define TSG_UNLOCK(g) pthread_mutex_unlock(&(g)->lock)

/* TCB groups */
static tsg_t *tsg_array[TSG_MAX_GROUPS];

/* forward prototype declarations */
int tsg_remove_single_desc(cpu_gen_t *cpu);
static int tc_free(tsg_t *tsg,cpu_tc_t *tc);

/* Create a new exec area */
static int exec_page_create_area(tsg_t *tsg)
{
   size_t area_size,page_count;
   insn_exec_page_t *cp;
   u_char *cp_addr;
   int i;
   
   /* Area already created */
   if (tsg->exec_area != NULL)
      return(0);
      
   /* Allocate an executable area through MMAP */
   area_size = tsg->exec_area_alloc_size * 1048756;
   tsg->exec_area = mmap(NULL,area_size,
                         PROT_EXEC|PROT_READ|PROT_WRITE,
                         MAP_SHARED|MAP_ANONYMOUS,-1,(off_t)0);
  
   if (!tsg->exec_area) {
      perror("exec_page_create_area: mmap");
      goto err_mmap;
   }

   /* Create the page array */  
   page_count = area_size / TC_JIT_PAGE_SIZE;
   tsg->exec_page_array = calloc(page_count,sizeof(insn_exec_page_t));
      
   if (!tsg->exec_page_array)
      goto err_array;
   
   for(i=0,cp_addr=tsg->exec_area;i<page_count;i++) {
      cp = &tsg->exec_page_array[i];

      cp->ptr = cp_addr;
      cp_addr += TC_JIT_PAGE_SIZE;

      tsg->exec_page_total++;

      cp->next = tsg->exec_page_free_list;
      tsg->exec_page_free_list = cp;
   }

   return(0);
   
 err_array:
   munmap(tsg->exec_area,area_size);
 err_mmap:
   return(-1);
}

/* Allocate an exec page */
static insn_exec_page_t *exec_page_alloc(cpu_gen_t *cpu)
{
   tsg_t *tsg = tsg_array[cpu->tsg];
   insn_exec_page_t *p;
   __maybe_unused int count;

   TSG_LOCK(tsg);

   //tcb_desc_check_consistency(tcbg);

   /* 
    * If the free list is empty, try to increase exec area capacity, then
    * flush JIT for the requesting CPU.
    */
   if (unlikely(!(p = tsg->exec_page_free_list))) {
      count = tsg_remove_single_desc(cpu);
#if DEBUG_JIT_FLUSH
      cpu_log(cpu,"JIT","flushed %d TCB\n",count);
#endif

      if (unlikely(!(p = tsg->exec_page_free_list)))
         tsg->exec_area_full = TRUE;
   }

   /* If the area is full, stop allocating pages and free TCB */
   if (tsg->exec_area_full) {
      cpu_jit_tcb_flush_all(cpu);
      
      /* if we get >= 25% of free pages, we can reallocate */
      if (tsg->exec_page_total >= (tsg->exec_page_alloc * 4)) {
         tsg->exec_area_full = FALSE;
      }

      TSG_UNLOCK(tsg);
      return NULL;
   }

   tsg->exec_page_free_list = p->next;
   tsg->exec_page_alloc++;
   TSG_UNLOCK(tsg);
   return p;
}

/* 
 * Free an exec page and returns it to the pool.
 * Note: the lock is already taken when exec_page_free() is called.
 */
static inline void exec_page_free(tsg_t *tcbg,insn_exec_page_t *p)
{
   if (p != NULL) {
      p->next = tcbg->exec_page_free_list;
      tcbg->exec_page_free_list = p;
      tcbg->exec_page_alloc--;
   }
}

/* Allocate a free TCB group */
int tsg_alloc(void)
{
   int i;
   
   for(i=0;i<TSG_MAX_GROUPS;i++)
      if (tsg_array[i] == NULL)
         return(i);
        
   return(-1);
}

/* Create a translation sharing group */
int tsg_create(int id,size_t alloc_size)
{
   tsg_t *tsg;
   
   /* If the group is already initialized, skip it */
   if (tsg_array[id] != NULL)
      return(0);
   
   /* Allocate the holding structure */
   if (!(tsg = malloc(sizeof(*tsg))))
      return(-1);
      
   memset(tsg,0,sizeof(*tsg));
   tsg->exec_area_full = FALSE;
   tsg->exec_area_alloc_size = alloc_size;
   
   /* Create the TC hash table */
   if (!(tsg->tc_hash = calloc(sizeof(cpu_tc_t *),TC_HASH_SIZE)))
      goto err_hash;

   /* Create the exec page area */
   if (exec_page_create_area(tsg) == -1)
      goto err_area;

   tsg_array[id] = tsg;

   pthread_mutexattr_init(&tsg->lock_attr);
   pthread_mutexattr_settype(&tsg->lock_attr,PTHREAD_MUTEX_RECURSIVE);
   pthread_mutex_init(&tsg->lock,&tsg->lock_attr);
   return(0);
   
 err_area:
   free(tsg->tc_hash);
 err_hash:
   free(tsg);
   return(-1);
}

/* Bind a CPU to a TSG - If the group isn't specified, create one */
int tsg_bind_cpu(cpu_gen_t *cpu)
{
   tsg_t *tsg;
   ssize_t alloc_size;

   if (cpu->tsg == -1) {
      cpu->tsg = tsg_alloc();
      
      if (cpu->tsg == -1)
         return(-1);
         
      alloc_size = TSG_EXEC_AREA_SINGLE_CPU;
   } else {
      alloc_size = TSG_EXEC_AREA_SHARED;
   }
      
   if (tsg_create(cpu->tsg,alloc_size) == -1)
      return(-1);
      
   tsg = tsg_array[cpu->tsg];
   M_LIST_ADD(cpu,tsg->cpu_list,tsg);
   return(0);
}

/* Unbind a CPU from a TSG - release all resources used */
int tsg_unbind_cpu(cpu_gen_t *cpu)
{
   tsg_t *tsg = tsg_array[cpu->tsg];
   cpu_tb_t *tb,*next;
   
   if (cpu->tsg == -1)
      return(-1);
   
   /* Free all descriptors in free list */
   for(tb=cpu->tb_free_list;tb;tb=next) {
      next = tb->tb_next;
      free(tb);
   }
   
   /* Free all descriptors currently in use */
   for(tb=cpu->tb_list;tb;tb=next) {
      next = tb->tb_next;
      tb_free(cpu,tb);
      free(tb);
   }
   
   cpu->tb_list = NULL;
   cpu->tb_free_list = NULL;
   
   TSG_LOCK(tsg);
   M_LIST_REMOVE(cpu,tsg);
   TSG_UNLOCK(tsg);
   return(0);
}

/* Create a JIT chunk */
int tc_alloc_jit_chunk(cpu_gen_t *cpu,cpu_tc_t *tc)
{
   insn_exec_page_t *chunk;
   
   if (tc->jit_chunk_pos >= TC_MAX_CHUNKS) {
      cpu_log(cpu,"JIT","TC 0x%8.8llx: too many chunks.\n",tc->vaddr);
      return(-1);
   }
   
   if (!(chunk = exec_page_alloc(cpu)))
      return(-1);
   
   tc->jit_chunks[tc->jit_chunk_pos++] = chunk;
   tc->jit_buffer = chunk;
   return(0);
}

/* Free JIT chunks allocated for a TC descriptor */
static void tc_free_jit_chunks(tsg_t *tsg,cpu_tc_t *tc)
{
   int i;

   for(i=0;i<tc->jit_chunk_pos;i++) {
      exec_page_free(tsg,tc->jit_chunks[i]);
      tc->jit_chunks[i] = NULL;
   }
}

/* Remove the TC descriptor from the TSG hash table */
static inline void tc_remove_from_hash(cpu_tc_t *tc)
{
   M_LIST_REMOVE(tc,hash);
}

/* 
 * Add a TC descriptor as local to the specified CPU.
 * This occurs when the descriptor has just been created and is not shared.
 * It allows to free pages easily in case of contention. 
 */
static inline void tc_add_cpu_local(cpu_gen_t *cpu,cpu_tc_t *tc)
{   
   M_LIST_ADD(tc,cpu->tc_local_list,sc);
}

/*
 * Remove a TC descriptor from a local list of a CPU. It happens when the TC
 * becomes shared between different virtual CPUs.
 */
static inline void tc_remove_cpu_local(cpu_tc_t *tc)
{
   M_LIST_REMOVE(tc,sc);
}

/* Free a TC descriptor */
static int tc_free(tsg_t *tsg,cpu_tc_t *tc)
{
   TSG_LOCK(tsg);
      
   tc->ref_count--;   
   assert(tc->ref_count >= 0);
      
   if (tc->ref_count == 0) {
      tc->flags &= ~TC_FLAG_VALID;
      
      tc_free_patches(tc);
      
      tc_remove_from_hash(tc);
      tc_remove_cpu_local(tc);
      tc_free_jit_chunks(tsg,tc);
      free(tc->jit_insn_ptr);
      
      tc->sc_next = tsg->tc_free_list;
      tsg->tc_free_list = tc;
      TSG_UNLOCK(tsg);
      return(TRUE);
   }
   
   /* not yet deleted */
   TSG_UNLOCK(tsg);
   return(FALSE);
}

/* Allocate a new TC descriptor */
cpu_tc_t *tc_alloc(cpu_gen_t *cpu,m_uint64_t vaddr,m_uint32_t exec_state)
{
   tsg_t *tsg = tsg_array[cpu->tsg];
   cpu_tc_t *tc;
   size_t len;

   TSG_LOCK(tsg);
   if (tsg->tc_free_list) {
      tc = tsg->tc_free_list;
      tsg->tc_free_list = tc->sc_next;
   } else {
      if (!(tc = malloc(sizeof(*tc))))
         return NULL;
   }
   TSG_UNLOCK(tsg);
   
   memset(tc,0,sizeof(*tc));
   tc->vaddr = vaddr;
   tc->exec_state = exec_state;
   tc->ref_count = 1;
   
   /* 
    * Allocate the array used to convert target code ptr to native code ptr,
    * and create the first JIT buffer.
    */
   len = VM_PAGE_SIZE / sizeof(m_uint32_t);

   if (!(tc->jit_insn_ptr = calloc(len,sizeof(u_char *))) ||
       (tc_alloc_jit_chunk(cpu,tc) == -1))
   {
      tc_free(tsg,tc);
      return NULL;
   }

   tc->jit_ptr = tc->jit_buffer->ptr;
   return tc;
}

/* Compute a checksum on a page */
tsg_checksum_t tsg_checksum_page(void *page,ssize_t size)
{
   tsg_checksum_t cksum = 0;
   m_uint64_t *ptr = page;
   
   while(size > 0) {
      cksum ^= *ptr;
      ptr++;
      size -= sizeof(m_uint64_t);
   }
   return(cksum);
}

/* Compute a hash on the specified checksum */
static inline u_int tsg_cksum_hash(tsg_checksum_t cksum)
{
   tsg_checksum_t tmp;
   
   tmp = cksum ^ (cksum >> 17) ^ (cksum >> 23);
   return((u_int)(tmp & TC_HASH_MASK));
}

/* Compare physical pages */
static inline int tb_compare_page(cpu_tb_t *tb1,cpu_tb_t *tb2)
{
   if (tb1->target_code == tb2->target_code)
      return(0);
   
   return(memcmp(tb1->target_code,tb2->target_code,VM_PAGE_SIZE));
}

/* Compare 2 TBs */
static forced_inline int tb_compare(cpu_tb_t *tb1,cpu_tb_t *tb2)
{
   return((tb1->vaddr == tb2->vaddr) &&
          (tb1->exec_state == tb2->exec_state));
}

/* Try to find a TC descriptor to share generated code */
int tc_find_shared(cpu_gen_t *cpu,cpu_tb_t *tb)
{
   tsg_t *tsg = tsg_array[cpu->tsg];
   cpu_tb_t *p;
   cpu_tc_t *tc;
   u_int hash_bucket;
      
   TSG_LOCK(tsg);   

   assert(tb->target_code != NULL);

   hash_bucket = tsg_cksum_hash(tb->checksum);
   for(tc=tsg->tc_hash[hash_bucket];tc;tc=tc->hash_next) 
   {
      assert(tc->flags & TC_FLAG_VALID);
      
      if (tc->checksum == tb->checksum) {
         for(p=tc->tb_list;p;p=p->tb_dl_next) {
            //assert(p->flags & TCB_FLAG_VALID);
            
            if (!(p->flags & TB_FLAG_VALID)) {
               tb_dump(tb);
               abort();
            }
            
            if (tb_compare(tb,p) && !tb_compare_page(tb,p))
            {
               /* matching page, we can share the code */
               tc->ref_count++;
               tb->tc = tc;
               tc_remove_cpu_local(tc);
               M_LIST_ADD(tb,tc->tb_list,tb_dl);               
               tb_enable(cpu,tb);
               
               TSG_UNLOCK(tsg);
               return(TSG_LOOKUP_SHARED);
            }
         }
      }
   }
   
   /* A new TCB descriptor must be created */
   TSG_UNLOCK(tsg);   
   return(TSG_LOOKUP_NEW);
}

/* Register a newly compiled TCB descriptor */
void tc_register(cpu_gen_t *cpu,cpu_tb_t *tb,cpu_tc_t *tc)
{
   tsg_t *tsg = tsg_array[cpu->tsg];
   u_int hash_bucket = tsg_cksum_hash(tb->checksum);
   
   tb->tc = tc;
   tc->checksum = tb->checksum;

   TSG_LOCK(tsg);   
   tc_add_cpu_local(cpu,tc);
   M_LIST_ADD(tb,tc->tb_list,tb_dl);
   M_LIST_ADD(tc,tsg->tc_hash[hash_bucket],hash);
   tc->flags |= TC_FLAG_VALID;
   TSG_UNLOCK(tsg);
}

/* Remove all TC descriptors belonging to a single CPU (ie not shared) */
int tsg_remove_single_desc(cpu_gen_t *cpu)
{
   cpu_tc_t *tc,*next;
   int count = 0;
   
   for(tc=cpu->tc_local_list;tc;tc=next) {      
      next = tc->sc_next;

      assert(tc->ref_count == 1);
      assert(tc->tb_list->tb_dl_next == NULL);

      tb_free(cpu,tc->tb_list);      
      count++;
   }
   
   cpu->tc_local_list = NULL;
   return(count);
}

/* Dump a TCB */
void tb_dump(cpu_tb_t *tb)
{
   printf("TB 0x%8.8llx:\n",tb->vaddr);
   printf("  - flags       : 0x%4.4x\n",tb->flags);
   printf("  - checksum    : 0x%16.16llx\n",tb->checksum);
   printf("  - target_code : %p\n",tb->target_code);
   printf("  - virt_hash   : 0x%8.8x\n",tb->virt_hash);
   printf("  - phys_hash   : 0x%8.8x\n",tb->phys_hash);
   printf("  - phys_page   : 0x%8.8x\n",tb->phys_page);
   printf("  - tc          : %p\n",tb->tc);
   printf("  - tb_LIST     : (%p,%p)\n",tb->tb_pprev,tb->tb_next);
   printf("  - tcb_dl_LIST : (%p,%p)\n",tb->tb_dl_pprev,tb->tb_dl_next);
   printf("  - phys_LIST   : (%p,%p)\n",tb->phys_pprev,tb->phys_next);
}

/* Dump a TCB descriptor */
void tc_dump(cpu_tc_t *tc)
{
   printf("TC 0x%8.8llx:\n",tc->vaddr);
   printf("  - flags      : 0x%4.4x\n",tc->flags);
   printf("  - checksum   : 0x%8.8llx\n",tc->checksum);
   printf("  - ref_count  : %u\n",tc->ref_count);
   printf("  - tb_list    : %p\n",tc->tb_list);
   printf("  - hash_LIST  : (%p,%p)\n",tc->hash_pprev,tc->hash_next);
   printf("  - sc_LIST    : (%p,%p)\n",tc->sc_pprev,tc->sc_next);    
}

/* Consistency check */
int tc_check_consistency(cpu_gen_t *cpu)
{
   tsg_t *tsg = tsg_array[cpu->tsg];
   cpu_tb_t *tb;
   cpu_tc_t *tc;
   int i,err=0;
   
   TSG_LOCK(tsg);

   for(i=0;i<TC_HASH_SIZE;i++) {
      for(tc=tsg->tc_hash[i];tc;tc=tc->hash_next) {
         if (!(tc->flags & TC_FLAG_VALID)) {
            cpu_log(cpu,"JIT",
                    "consistency error: TC 0x%8.8llx (flags=0x%x)\n",
                    tc->vaddr,tc->flags);
            tc_dump(tc);
            err++;
         }
            
         for(tb=tc->tb_list;tb;tb=tb->tb_dl_next) {
            if (!(tb->flags & TB_FLAG_VALID)) {
               cpu_log(cpu,"JIT",
                       "consistency error: TB 0x%8.8llx (flags=0x%x)\n",
                       tb->vaddr,tb->flags);
               err++;
               
               tb_dump(tb);
            }
         }
      }
   }

   TSG_UNLOCK(tsg);
   
   if (err > 0) {
      printf("TSG %d: internal consistency error (%d pb detected)\n",
             cpu->tsg,err);
   }
      
   return(err);
}


/* Statistics: compute number of shared pages in a translation group */
static int tsg_get_stats(tsg_t *tsg,struct tsg_stats *s)
{
   cpu_tc_t *tc;
   int i;
   
   s->shared_tc = s->total_tc = 0;
   s->shared_pages = 0; 
   
   if (!tsg)
      return(-1);

   for(i=0;i<TC_HASH_SIZE;i++) {
      for(tc=tsg->tc_hash[i];tc;tc=tc->hash_next) {
         if (tc->ref_count > 1) {
            s->shared_pages += tc->jit_chunk_pos;
            s->shared_tc++;
         }

         s->total_tc++;
      }
   }
   
   return(0);
}

/* Show statistics about all translation groups */
void tsg_show_stats(void)
{
   struct tsg_stats s;
   int i;

   printf("\nTSG statistics:\n\n");

   printf("  ID   Shared TC     Total TC     Alloc.Pages  Shared Pages   Total Pages\n");

   for(i=0;i<TSG_MAX_GROUPS;i++) {
      if (!tsg_get_stats(tsg_array[i],&s)) {
         printf(" %3d     %8u     %8u       %8u      %8u      %8u\n",
                i,s.shared_tc,s.total_tc,
                tsg_array[i]->exec_page_alloc,
                s.shared_pages,
                tsg_array[i]->exec_page_total);
      }
   }

   printf("\n");
}

/* Adjust the JIT buffer if its size is not sufficient */
int tc_adjust_jit_buffer(cpu_gen_t *cpu,cpu_tc_t *tc,
                         void (*set_jump)(u_char **insn,u_char *dst))
{
   assert((tc->jit_ptr - tc->jit_buffer->ptr) < TC_JIT_PAGE_SIZE);
   
   if ((tc->jit_ptr - tc->jit_buffer->ptr) <= (TC_JIT_PAGE_SIZE - 512))
      return(0);

#if DEBUG_JIT_BUFFER_ADJUST  
   cpu_log(cpu,"JIT",
           "TC 0x%8.8llx: adjusting JIT buffer (cur=%p,start=%p,delta=%u)\n",
           tc->vaddr,tc->jit_ptr,tc->jit_buffer->ptr,
           TC_JIT_PAGE_SIZE - (tc->jit_ptr-tc->jit_buffer->ptr));
#endif

   /* 
    * If we cannot allocate a new chunk, free the complete descriptor and 
    * return an error so that the caller uses non-JIT mode for this TCB.
    */
   if (tc_alloc_jit_chunk(cpu,tc) == -1) {
      tc_free(tsg_array[cpu->tsg],tc);
      return(-1);
   }

   /* jump to the new exec page (link) */
   set_jump(&tc->jit_ptr,tc->jit_buffer->ptr);
   tc->jit_ptr = tc->jit_buffer->ptr;
   return(0);
}

/* Record a patch to apply in a compiled block */
struct insn_patch *tc_record_patch(cpu_gen_t *cpu,cpu_tc_t *tc,
                                   u_char *jit_ptr,m_uint64_t vaddr)
{
   struct insn_patch_table *ipt = tc->patch_table;
   struct insn_patch *patch;

   /* vaddr must be 32-bit aligned */
   if (vaddr & 0x03) {
      cpu_log(cpu,"JIT",
              "TC 0x%8.8llx: trying to record an invalid vaddr (0x%8.8llx)\n",
              tc->vaddr,vaddr);
      return NULL;
   }

   if (!ipt || (ipt->cur_patch >= INSN_PATCH_TABLE_SIZE))
   {
      /* full table or no table, create a new one */
      ipt = malloc(sizeof(*ipt));
      if (!ipt) {
         cpu_log(cpu,"JIT","TC 0x%8.8llx: unable to create patch table.\n",
                 tc->vaddr);
         return NULL;
      }

      memset(ipt,0,sizeof(*ipt));
      ipt->next = tc->patch_table;
      tc->patch_table = ipt;
   }

#if DEBUG_JIT_PATCH
   printf("TC 0x%8.8llx: recording patch [host %p -> target:0x%8.8llx], "
          "TP=%d\n",tc->vaddr,jit_ptr,vaddr,tc->trans_pos);
#endif

   patch = &ipt->patches[ipt->cur_patch];
   patch->jit_insn = jit_ptr;
   patch->vaddr    = vaddr;
   ipt->cur_patch++;
   
   return patch;
}

/* Apply all patches */
int tc_apply_patches(cpu_tc_t *tc,void (*set_patch)(u_char *insn,u_char *dst))
{
   struct insn_patch_table *ipt;
   struct insn_patch *patch;
   u_char *jit_dst;
   int i;

   for(ipt=tc->patch_table;ipt;ipt=ipt->next)
      for(i=0;i<ipt->cur_patch;i++) 
      {
         patch = &ipt->patches[i];
         jit_dst = tc_get_host_ptr(tc,patch->vaddr);

         if (jit_dst) {
#if DEBUG_JIT_PATCH
            printf("TC 0x%8.8llx: applying patch "
                   "[host %p -> target 0x%8.8llx=JIT:%p]\n",
                   tc->vaddr,patch->jit_insn,patch->vaddr,jit_dst);
#endif
            set_patch(patch->jit_insn,jit_dst);
         }
      }

   return(0);
}

/* Free the patch table */
void tc_free_patches(cpu_tc_t *tc)
{
   struct insn_patch_table *p,*next;

   for(p=tc->patch_table;p;p=next) {
      next = p->next;
      free(p);
   }

   tc->patch_table = NULL;
}

/* Initialize the JIT structures of a CPU */
int cpu_jit_init(cpu_gen_t *cpu,size_t virt_hash_size,size_t phys_hash_size)
{
   size_t len;

   /* Virtual address mapping for TCB */
   len = virt_hash_size * sizeof(void *);
   cpu->tb_virt_hash = m_memalign(4096,len);
   memset(cpu->tb_virt_hash,0,len);

   /* Physical address mapping for TCB */
   len = phys_hash_size * sizeof(void *);
   cpu->tb_phys_hash = m_memalign(4096,len);
   memset(cpu->tb_phys_hash,0,len);
   
   return(0);
}

/* Shutdown the JIT structures of a CPU */
void cpu_jit_shutdown(cpu_gen_t *cpu)
{
   tsg_unbind_cpu(cpu);
   
   /* Free virtual and physical hash tables */
   free(cpu->tb_virt_hash);
   free(cpu->tb_phys_hash);
   
   cpu->tb_virt_hash = NULL;
   cpu->tb_phys_hash = NULL;
}

/* Allocate a new TB */
cpu_tb_t *tb_alloc(cpu_gen_t *cpu,m_uint64_t vaddr,u_int exec_state)
{
   cpu_tb_t *tb;

   if (cpu->tb_free_list) {
      tb = cpu->tb_free_list;
      cpu->tb_free_list = tb->tb_next;
   } else {
      if (!(tb = malloc(sizeof(*tb))))
         return NULL;
   }

   memset(tb,0,sizeof(*tb));
   tb->vaddr = vaddr;
   tb->exec_state = exec_state;
   return tb;
}

/* Free a Translation Block */
void tb_free(cpu_gen_t *cpu,cpu_tb_t *tb)
{   
   tsg_t *tsg = tsg_array[cpu->tsg];

   /* Remove this TB from the TB list bound to a TC descriptor */   
   TSG_LOCK(tsg);
   M_LIST_REMOVE(tb,tb_dl);
   TSG_UNLOCK(tsg);

   /* Release the TC descriptor first */
   if (tb->tc != NULL)
      tc_free(tsg,tb->tc);

   /* Remove the block from the CPU TCB list */
   M_LIST_REMOVE(tb,tb);

   /* Remove the block from the CPU physical mapping hash table */
   M_LIST_REMOVE(tb,phys);

   /* Invalidate the entry in hash table for virtual addresses */
   if (cpu->tb_virt_hash[tb->virt_hash] == tb)
      cpu->tb_virt_hash[tb->virt_hash] = NULL;

   /* Make the block return to the free list */
   memset(tb,0,sizeof(*tb));
   tb->tb_next = cpu->tb_free_list;
   cpu->tb_free_list = tb;
}

/* Enable a Tranlsation Block  */
void tb_enable(cpu_gen_t *cpu,cpu_tb_t *tb)
{
   M_LIST_ADD(tb,cpu->tb_list,tb);
   M_LIST_ADD(tb,cpu->tb_phys_hash[tb->phys_hash],phys);
   tb->flags |= TB_FLAG_VALID;
}

/* Flush all TCB of a virtual CPU */
void cpu_jit_tcb_flush_all(cpu_gen_t *cpu)
{
   cpu_tb_t *tb,*next;
   
   for(tb=cpu->tb_list;tb;tb=next) {
      next = tb->tb_next;
      tb_free(cpu,tb);
   }

   assert(cpu->tb_list == NULL);
}

/* Mark a TB as containing self-modifying code */
void tb_mark_smc(cpu_gen_t *cpu,cpu_tb_t *tb)
{
   if (tb->flags & TB_FLAG_SMC)
      return; /* already done */

   tb->flags |= TB_FLAG_SMC;

   if (tb->tc != NULL) {
      tc_free(tsg_array[cpu->tsg],tb->tc);
      tb->tc = NULL;
   }
}

/* Handle write access on an executable page */
void cpu_jit_write_on_exec_page(cpu_gen_t *cpu,
                                m_uint32_t wr_phys_page,
                                m_uint32_t wr_hp,
                                m_uint32_t ip_phys_page)
{
   cpu_tb_t *tb,**tbp,*tb_next;
     
   if (wr_phys_page != ip_phys_page) {
      /* Clear all TCB matching the physical page being modified */
      for(tbp=&cpu->tb_phys_hash[wr_hp];*tbp;)
         if ((*tbp)->phys_page == wr_phys_page) {
            tb_next = (*tbp)->phys_next;
            tb_free(cpu,(*tbp));
            *tbp = tb_next;
         } else {
            tbp = &(*tbp)->phys_next;
         }
   } else {
      /* Self-modifying page */
      for(tb=cpu->tb_phys_hash[wr_hp];tb;tb=tb->phys_next)
         if (tb->phys_page == wr_phys_page)
            tb_mark_smc(cpu,tb);

      cpu_exec_loop_enter(cpu);
   }
}
