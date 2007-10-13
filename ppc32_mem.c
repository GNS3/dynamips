/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * PowerPC MMU.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "ppc32_jit.h"

#define DEBUG_ICBI  0

/* Memory access with special access mask */
void ppc32_access_special(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int cid,
                          m_uint32_t mask,u_int op_code,u_int op_type,
                          u_int op_size,m_uint64_t *data)
{
   switch(mask) {
      case MTS_ACC_T:
         if (op_code != PPC_MEMOP_LOOKUP) {
#if DEBUG_MTS_ACC_T
            cpu_log(cpu->gen,
                    "MTS","MMU exception for address 0x%8.8x at ia=0x%8.8x "
                    "(%s access, size=%u)\n",
                    vaddr,cpu->ia,(op_type == MTS_READ) ? 
                    "read":"write",op_size);
            //ppc32_dump_regs(cpu->gen);
#if MEMLOG_ENABLE
            memlog_dump(cpu->gen);
#endif
#endif
            if (cid == PPC32_MTS_DCACHE) {
               cpu->dsisr = PPC32_DSISR_NOTRANS;

               if (op_type == MTS_WRITE)
                  cpu->dsisr |= PPC32_DSISR_STORE;

               cpu->dar = vaddr;
               ppc32_trigger_exception(cpu,PPC32_EXC_DSI);
               cpu_exec_loop_enter(cpu->gen);
            }
         }        
         break;

      case MTS_ACC_U:
         if (op_type == MTS_READ)
            *data = 0;

         if (cpu->gen->undef_mem_handler != NULL) {
            if (cpu->gen->undef_mem_handler(cpu->gen,(m_uint64_t)vaddr,
                                            op_size,op_type,data))
               return;
         }

#if DEBUG_MTS_ACC_U
         if (op_type == MTS_READ)
            cpu_log(cpu->gen,
                    "MTS","read  access to undefined address 0x%8.8x at "
                    "ia=0x%8.8x (size=%u)\n",vaddr,cpu->ia,op_size);
         else
            cpu_log(cpu->gen,
                    "MTS","write access to undefined address 0x%8.8x at "
                    "ia=0x%8.8x, value=0x%8.8llx (size=%u)\n",
                    vaddr,cpu->ia,*data,op_size);
#endif
         break;
   }
}

/* Initialize the MTS subsystem for the specified CPU */
int ppc32_mem_init(cpu_ppc_t *cpu)
{
   size_t len;

   /* Initialize the cache entries to 0 (empty) */
   len = MTS32_HASH_SIZE * sizeof(mts32_entry_t);

   if (!(cpu->mts_cache[PPC32_MTS_ICACHE] = malloc(len)))
      return(-1);

   if (!(cpu->mts_cache[PPC32_MTS_DCACHE] = malloc(len)))
      return(-1);

   memset(cpu->mts_cache[PPC32_MTS_ICACHE],0xFF,len);
   memset(cpu->mts_cache[PPC32_MTS_DCACHE],0xFF,len);

   cpu->mts_lookups = 0;
   cpu->mts_misses  = 0;
   return(0);
}

/* Free memory used by MTS */
void ppc32_mem_shutdown(cpu_ppc_t *cpu)
{
   if (cpu != NULL) {
      /* Free the caches themselves */
      free(cpu->mts_cache[PPC32_MTS_ICACHE]);
      free(cpu->mts_cache[PPC32_MTS_DCACHE]);
      cpu->mts_cache[PPC32_MTS_ICACHE] = NULL;
      cpu->mts_cache[PPC32_MTS_DCACHE] = NULL;
   }
}

/* Show MTS detailed information (debugging only!) */
void ppc32_mem_show_stats(cpu_gen_t *gen_cpu)
{
   cpu_ppc_t *cpu = CPU_PPC32(gen_cpu);
#if DEBUG_MTS_MAP_VIRT
   mts32_entry_t *entry;
   u_int i,count;
#endif

   printf("\nCPU%u: MTS statistics:\n",cpu->gen->id);

#if DEBUG_MTS_MAP_VIRT
   printf("Instruction cache:\n");
   
   /* Valid hash entries for Instruction Cache */
   for(count=0,i=0;i<MTS32_HASH_SIZE;i++) {
      entry = &cpu->mts_cache[PPC32_MTS_ICACHE][i];

      if (!(entry->gvpa & MTS_INV_ENTRY_MASK)) {
         printf("    %4u: vaddr=0x%8.8x, paddr=0x%8.8x, hpa=%p\n",
                i,entry->gvpa,entry->gppa,(void *)entry->hpa);
         count++;
      }
   }

   printf("   %u/%u valid hash entries for icache.\n",count,MTS32_HASH_SIZE);


   printf("Data cache:\n");
   
   /* Valid hash entries for Instruction Cache */
   for(count=0,i=0;i<MTS32_HASH_SIZE;i++) {
      entry = &cpu->mts_cache[PPC32_MTS_DCACHE][i];

      if (!(entry->gvpa & MTS_INV_ENTRY_MASK)) {
         printf("    %4u: vaddr=0x%8.8x, paddr=0x%8.8x, hpa=%p\n",
                i,entry->gvpa,entry->gppa,(void *)entry->hpa);
         count++;
      }
   }

   printf("   %u/%u valid hash entries for dcache.\n",count,MTS32_HASH_SIZE);
#endif

   printf("\n   Total lookups: %llu, misses: %llu, efficiency: %g%%\n",
          cpu->mts_lookups, cpu->mts_misses,
          100 - ((double)(cpu->mts_misses*100)/
                 (double)cpu->mts_lookups));
}

/* Invalidate the MTS caches (instruction and data) */
void ppc32_mem_invalidate_cache(cpu_ppc_t *cpu)
{
   size_t len;

   len = MTS32_HASH_SIZE * sizeof(mts32_entry_t);
   memset(cpu->mts_cache[PPC32_MTS_ICACHE],0xFF,len);
   memset(cpu->mts_cache[PPC32_MTS_DCACHE],0xFF,len);
}

/* 
 * MTS mapping.
 *
 * It is NOT inlined since it triggers a GCC bug on my config (x86, GCC 3.3.5)
 */
static no_inline struct mts32_entry *
ppc32_mem_map(cpu_ppc_t *cpu,u_int op_type,mts_map_t *map,
              mts32_entry_t *entry,mts32_entry_t *alt_entry)
{
   ppc32_jit_tcb_t *block;
   struct vdevice *dev;
   m_uint32_t offset;
   m_iptr_t host_ptr;
   m_uint32_t exec_flag = 0;
   int cow;

   if (!(dev = dev_lookup(cpu->vm,map->paddr+map->offset,map->cached)))
      return NULL;

   if (cpu->exec_phys_map) {
      block = ppc32_jit_find_by_phys_page(cpu,map->paddr >> VM_PAGE_SHIFT);

      if (block)
         exec_flag = MTS_FLAG_EXEC;
   }

   if (dev->flags & VDEVICE_FLAG_SPARSE) {
      host_ptr = dev_sparse_get_host_addr(cpu->vm,dev,map->paddr,op_type,&cow);

      entry->gvpa  = map->vaddr;
      entry->gppa  = map->paddr;
      entry->hpa   = host_ptr;
      entry->flags = (cow) ? MTS_FLAG_COW : 0;
      entry->flags |= exec_flag;
      return entry;
   }

   if (!dev->host_addr || (dev->flags & VDEVICE_FLAG_NO_MTS_MMAP)) {
      offset = (map->paddr + map->offset) - dev->phys_addr;

      /* device entries are never stored in virtual TLB */
      alt_entry->gppa  = dev->id;
      alt_entry->hpa   = offset;
      alt_entry->flags = MTS_FLAG_DEV;
      return alt_entry;
   }

   entry->gvpa  = map->vaddr;
   entry->gppa  = map->paddr;
   entry->hpa   = dev->host_addr + (map->paddr - dev->phys_addr);
   entry->flags = exec_flag;
   return entry;
}

/* BAT lookup */
static forced_inline int ppc32_bat_lookup(cpu_ppc_t *cpu,m_uint32_t vaddr,
                                          u_int cid,mts_map_t *map)
{
   m_uint32_t bepi,mask,bl,pr,ubat;
   int i;
   
   pr = (cpu->msr & PPC32_MSR_PR) >> PPC32_MSR_PR_SHIFT;
   pr = ((~pr << 1) | pr) & 0x03;

   for(i=0;i<PPC32_BAT_NR;i++) {
      ubat = cpu->bat[cid][i].reg[0];

      if (!(ubat & pr))
         continue;

      //bl = (ubat & PPC32_UBAT_BL_MASK) >> PPC32_UBAT_BL_SHIFT;
      bl = (ubat & PPC32_UBAT_XBL_MASK) >> PPC32_UBAT_XBL_SHIFT;
      
      mask = ~bl << PPC32_BAT_ADDR_SHIFT;
      bepi = ubat & PPC32_UBAT_BEPI_MASK;

      if (bepi == (vaddr & mask)) {
         map->vaddr = vaddr & PPC32_MIN_PAGE_MASK;
         map->paddr = cpu->bat[cid][i].reg[1] & PPC32_LBAT_BRPN_MASK;
         map->paddr += map->vaddr - bepi;
         map->offset = vaddr & PPC32_MIN_PAGE_IMASK;
         map->cached = FALSE;
         return(TRUE);
      }
   }

   return(FALSE);
}

/* Memory slow lookup */
static mts32_entry_t *ppc32_slow_lookup(cpu_ppc_t *cpu,m_uint32_t vaddr,
                                        u_int cid,u_int op_code,u_int op_size,
                                        u_int op_type,m_uint64_t *data,
                                        mts32_entry_t *alt_entry)
{
   m_uint32_t hash_bucket,segment,vsid;
   m_uint32_t hash,tmp,pteg_offset,pte_key,key,pte2;
   mts32_entry_t *entry;
   m_uint8_t *pte_haddr;
   m_uint64_t paddr;
   mts_map_t map;
   int i;

#if DEBUG_MTS_STATS
   cpu->mts_misses++;
#endif

   hash_bucket = MTS32_HASH(vaddr);
   entry = &cpu->mts_cache[cid][hash_bucket];

   /* No translation - cover the 4GB space */
   if (((cid == PPC32_MTS_ICACHE) && !(cpu->msr & PPC32_MSR_IR)) ||
       ((cid == PPC32_MTS_DCACHE) && !(cpu->msr & PPC32_MSR_DR)))
   {
      map.vaddr  = vaddr & PPC32_MIN_PAGE_MASK;
      map.paddr  = vaddr & PPC32_MIN_PAGE_MASK;
      map.offset = vaddr & PPC32_MIN_PAGE_IMASK;
      map.cached = FALSE;

      if (!(entry = ppc32_mem_map(cpu,op_type,&map,entry,alt_entry)))
         goto err_undef;

      return entry;
   }

   /* Walk through the BAT registers */
   if (ppc32_bat_lookup(cpu,vaddr,cid,&map)) {
      if (!(entry = ppc32_mem_map(cpu,op_type,&map,entry,alt_entry)))
         goto err_undef;

      return entry;
   }

   if (unlikely(!cpu->sdr1))
      goto no_pte;

   /* Get the virtual segment identifier */
   segment = vaddr >> 28;
   vsid = cpu->sr[segment] & PPC32_SD_VSID_MASK;

   /* Compute the first hash value */
   hash =  (vaddr >> PPC32_MIN_PAGE_SHIFT) & 0xFFFF;
   hash ^= vsid;
   hash &= 0x7FFFFF;

   tmp = (hash >> 10) & (cpu->sdr1 & PPC32_SDR1_HTMEXT_MASK);
   pteg_offset = (hash & 0x3FF) << 6;
   pteg_offset |= tmp << 16;
   pte_haddr = cpu->sdr1_hptr + pteg_offset;
   
   pte_key = 0x80000000 | (vsid << 7);
   pte_key |= (vaddr >> 22) & 0x3F;

   for(i=0;i<8;i++,pte_haddr+=PPC32_PTE_SIZE) {
      key = vmtoh32(*(m_uint32_t *)pte_haddr);

      if (key == pte_key)
         goto pte_lookup_done;
   }

   /* Secondary hash value */
   hash = (~hash) & 0x7FFFFF;

   tmp = (hash >> 10) & (cpu->sdr1 & PPC32_SDR1_HTMEXT_MASK);
   pteg_offset = (hash & 0x3FF) << 6;
   pteg_offset |= tmp << 16;
   pte_haddr = cpu->sdr1_hptr + pteg_offset;
   
   pte_key = 0x80000040 | (vsid << 7);
   pte_key |= (vaddr >> 22) & 0x3F;

   for(i=0;i<8;i++,pte_haddr+=PPC32_PTE_SIZE) {
      key = vmtoh32(*(m_uint32_t *)pte_haddr);

      if (key == pte_key)
         goto pte_lookup_done;
   }
   
 no_pte:
   /* No matching PTE for this virtual address */
   ppc32_access_special(cpu,vaddr,cid,MTS_ACC_T,op_code,op_type,op_size,data);
   return NULL;

 pte_lookup_done:
   pte2  = vmtoh32(*(m_uint32_t *)(pte_haddr + sizeof(m_uint32_t)));
   paddr =   pte2 & PPC32_PTEL_RPN_MASK;
   paddr |= (pte2 & PPC32_PTEL_XPN_MASK) << (33 - PPC32_PTEL_XPN_SHIFT);
   paddr |= (pte2 & PPC32_PTEL_X_MASK) << (32 - PPC32_PTEL_X_SHIFT);

   map.vaddr  = vaddr & ~PPC32_MIN_PAGE_IMASK;
   map.paddr  = paddr;
   map.offset = vaddr & PPC32_MIN_PAGE_IMASK;
   map.cached = FALSE;
   
   if ((entry = ppc32_mem_map(cpu,op_type,&map,entry,alt_entry)))
      return entry;

 err_undef:
   ppc32_access_special(cpu,vaddr,cid,MTS_ACC_U,op_code,op_type,op_size,data);
   return NULL;
}

/* Memory access */
static inline void *ppc32_mem_access(cpu_ppc_t *cpu,m_uint32_t vaddr,
                                     u_int cid,u_int op_code,u_int op_size,
                                     u_int op_type,m_uint64_t *data)
{   
   mts32_entry_t *entry,alt_entry;
   ppc32_jit_tcb_t *block;
   m_uint32_t hash_bucket;
   m_uint32_t phys_page;
   m_uint32_t ia_hash;
   m_iptr_t haddr;
   u_int dev_id;
   int cow;

#if MEMLOG_ENABLE
   /* Record the memory access */
   memlog_rec_access(cpu->gen,vaddr,*data,op_size,op_type);
#endif

   hash_bucket = MTS32_HASH(vaddr);
   entry = &cpu->mts_cache[cid][hash_bucket];

#if DEBUG_MTS_STATS
   cpu->mts_lookups++;
#endif

   /* Copy-On-Write for sparse device ? */
   cow = (op_type == MTS_WRITE) && (entry->flags & MTS_FLAG_COW);

   /* Slow lookup if nothing found in cache */
   if (unlikely(((vaddr & PPC32_MIN_PAGE_MASK) != entry->gvpa) || cow)) {
      entry = cpu->mts_slow_lookup(cpu,vaddr,cid,op_code,op_size,op_type,
                                   data,&alt_entry);
      if (!entry)
         return NULL;

      if (entry->flags & MTS_FLAG_DEV) {
         dev_id = entry->gppa;
         haddr  = entry->hpa;
         return(dev_access_fast(cpu->gen,dev_id,haddr,op_size,op_type,data));
      }
   }

   /* Invalidate JIT code for written pages */
   if ((op_type == MTS_WRITE) && (entry->flags & MTS_FLAG_EXEC)) {
      if (cpu->exec_phys_map) {
         phys_page = entry->gppa >> VM_PAGE_SHIFT;

         if (vaddr >= PPC32_EXC_SYS_RST) {
            block = ppc32_jit_find_by_phys_page(cpu,phys_page);

            if (block != NULL) {
               //printf("Invalidation of block 0x%8.8x\n",block->start_ia);
               ia_hash = ppc32_jit_get_ia_hash(block->start_ia);
               ppc32_jit_tcb_free(cpu,block,TRUE);

               if (cpu->exec_blk_map[ia_hash] == block)
                  cpu->exec_blk_map[ia_hash] = NULL;

               entry->flags &= ~MTS_FLAG_EXEC;
            }
         }
      }
   }

   /* Raw memory access */
   haddr = entry->hpa + (vaddr & PPC32_MIN_PAGE_IMASK);
#if MEMLOG_ENABLE
   memlog_update_read(cpu->gen,haddr);
#endif
   return((void *)haddr);
}

/* Memory data access */
#define PPC32_MEM_DACCESS(cpu,vaddr,op_code,op_size,op_type,data) \
   ppc32_mem_access((cpu),(vaddr),PPC32_MTS_DCACHE,(op_code),(op_size),\
                    (op_type),(data))

/* Virtual address to physical page translation */
static fastcall int ppc32_translate(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int cid,
                                    m_uint32_t *phys_page)
{   
   mts32_entry_t *entry,alt_entry;
   m_uint32_t hash_bucket;
   m_uint64_t data = 0;
   
   hash_bucket = MTS32_HASH(vaddr);
   entry = &cpu->mts_cache[cid][hash_bucket];

   /* Slow lookup if nothing found in cache */
   if (unlikely(((m_uint32_t)vaddr & PPC32_MIN_PAGE_MASK) != entry->gvpa)) {
      entry = cpu->mts_slow_lookup(cpu,vaddr,cid,PPC_MEMOP_LOOKUP,4,MTS_READ,
                                   &data,&alt_entry);
      if (!entry)
         return(-1);
   }

   *phys_page = entry->gppa >> PPC32_MIN_PAGE_SHIFT;
   return(0);
}

/* Virtual address lookup */
static void *ppc32_mem_lookup(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int cid)
{
   m_uint64_t data;
   return(ppc32_mem_access(cpu,vaddr,cid,PPC_MEMOP_LOOKUP,4,MTS_READ,&data));
}

/* Set a BAT register */
int ppc32_set_bat(cpu_ppc_t *cpu,struct ppc32_bat_prog *bp)
{
   struct ppc32_bat_reg *bat;

   if ((bp->type != PPC32_IBAT_IDX) && (bp->type != PPC32_DBAT_IDX))
      return(-1);

   if (bp->index >= PPC32_BAT_NR)
      return(-1);

   bat = &cpu->bat[bp->type][bp->index];
   bat->reg[0] = bp->hi;
   bat->reg[1] = bp->lo;
   return(0);
}

/* Load BAT registers from a BAT array */
void ppc32_load_bat_array(cpu_ppc_t *cpu,struct ppc32_bat_prog *bp)
{
   while(bp->index != -1) {
      ppc32_set_bat(cpu,bp);
      bp++;
   }
}

/* Get the host address for SDR1 */
int ppc32_set_sdr1(cpu_ppc_t *cpu,m_uint32_t sdr1)
{
   struct vdevice *dev;
   m_uint64_t pt_addr;

   cpu->sdr1 = sdr1;
   pt_addr  = sdr1 & PPC32_SDR1_HTABORG_MASK;
   pt_addr |= ((m_uint64_t)(sdr1 & PPC32_SDR1_HTABEXT_MASK) << 20);

   if (!(dev = dev_lookup(cpu->vm,pt_addr,TRUE))) {
      fprintf(stderr,"ppc32_set_sdr1: unable to find haddr for SDR1=0x%8.8x\n",
              sdr1);
      return(-1);
   }

   cpu->sdr1_hptr = (char *)dev->host_addr + (pt_addr - dev->phys_addr);
   return(0);
}

/* Initialize the page table */
int ppc32_init_page_table(cpu_ppc_t *cpu)
{
   m_uint32_t pt_size;

   if (!cpu->sdr1_hptr)
      return(-1);

   pt_size = (1 + (cpu->sdr1 & PPC32_SDR1_HTMEXT_MASK)) << 16;
   memset(cpu->sdr1_hptr,0,pt_size);
   return(0);
}

/* Map a page */
int ppc32_map_page(cpu_ppc_t *cpu,u_int vsid,m_uint32_t vaddr,m_uint64_t paddr,
                   u_int wimg,u_int pp)
{   
   m_uint32_t hash,tmp,pteg_offset,key;
   m_uint8_t *pte_haddr;
   int i;

   /* Compute the first hash value */
   hash =  (vaddr >> PPC32_MIN_PAGE_SHIFT) & 0xFFFF;
   hash ^= vsid;
   hash &= 0x7FFFFF;

   tmp = (hash >> 10) & (cpu->sdr1 & PPC32_SDR1_HTMEXT_MASK);
   pteg_offset = (hash & 0x3FF) << 6;
   pteg_offset |= tmp << 16;
   pte_haddr = cpu->sdr1_hptr + pteg_offset;
   
   for(i=0;i<8;i++,pte_haddr+=PPC32_PTE_SIZE) {
      key = vmtoh32(*(m_uint32_t *)pte_haddr);
      
      if (!(key & PPC32_PTEU_V)) {
         hash = 0;
         goto free_pte_found;
      }
   }

   /* Secondary hash value */
   hash = (~hash) & 0x7FFFFF;

   tmp = (hash >> 10) & (cpu->sdr1 & PPC32_SDR1_HTMEXT_MASK);
   pteg_offset = (hash & 0x3FF) << 6;
   pteg_offset |= tmp << 16;
   pte_haddr = cpu->sdr1_hptr + pteg_offset;

   for(i=0;i<8;i++,pte_haddr+=PPC32_PTE_SIZE) {
      key = vmtoh32(*(m_uint32_t *)pte_haddr);
      
      if (!(key & PPC32_PTEU_V)) {
         hash = PPC32_PTEU_H;
         goto free_pte_found;
      }
   }

   /* No free PTE found */
   return(-1);

 free_pte_found:
   tmp = PPC32_PTEU_V | (vsid << PPC32_PTEU_VSID_SHIFT) | hash;
   tmp |= (vaddr >> 22) & 0x3F;
   *(m_uint32_t *)pte_haddr = htovm32(tmp);

   tmp = vaddr & PPC32_PTEL_RPN_MASK;
   tmp |= (vaddr >> (32 - PPC32_PTEL_X_SHIFT)) & PPC32_PTEL_X_MASK;
   tmp |= (vaddr >> (33 - PPC32_PTEL_XPN_SHIFT)) & PPC32_PTEL_XPN_MASK;

   tmp |= (wimg << PPC32_PTEL_WIMG_SHIFT) + pp;
   *(m_uint32_t *)(pte_haddr+sizeof(m_uint32_t)) = htovm32(tmp);
   return(0);
}

/* Map a memory zone */
int ppc32_map_zone(cpu_ppc_t *cpu,u_int vsid,m_uint32_t vaddr,m_uint64_t paddr,
                   m_uint32_t size,u_int wimg,u_int pp)
{
   while(size > 0) {
      if (ppc32_map_page(cpu,vsid,vaddr,paddr,wimg,pp) == -1)
         return(-1);

      size  -= PPC32_MIN_PAGE_SIZE;
      vaddr += PPC32_MIN_PAGE_SIZE;
      paddr += PPC32_MIN_PAGE_SIZE;
   }

   return(0);
}

/* PowerPC 405 TLB masks */
static m_uint32_t ppc405_tlb_masks[8] = {
   0xFFFFFC00, 0xFFFFF000, 0xFFFFC000, 0xFFFF0000,
   0xFFFC0000, 0xFFF00000, 0xFFC00000, 0xFF000000,
};

/* PowerPC 405 slow lookup */
static mts32_entry_t *ppc405_slow_lookup(cpu_ppc_t *cpu,m_uint32_t vaddr,
                                         u_int cid,u_int op_code,u_int op_size,
                                         u_int op_type,m_uint64_t *data,
                                         mts32_entry_t *alt_entry)
{
   struct ppc405_tlb_entry *tlb_entry;
   m_uint32_t hash_bucket,mask;
   m_uint32_t page_size;
   mts32_entry_t *entry;
   mts_map_t map;
   int i;

#if DEBUG_MTS_STATS
   cpu->mts_misses++;
#endif

   hash_bucket = MTS32_HASH(vaddr);
   entry = &cpu->mts_cache[cid][hash_bucket];

   /* No translation - cover the 4GB space */
   if (((cid == PPC32_MTS_ICACHE) && !(cpu->msr & PPC32_MSR_IR)) ||
       ((cid == PPC32_MTS_DCACHE) && !(cpu->msr & PPC32_MSR_DR)))
   {
      map.vaddr  = vaddr & PPC32_MIN_PAGE_MASK;
      map.paddr  = vaddr & PPC32_MIN_PAGE_MASK;
      map.cached = FALSE;

      if (!(entry = ppc32_mem_map(cpu,op_type,&map,entry,alt_entry)))
         goto err_undef;

      return entry;
   }

   /* Walk through the unified TLB */
   for(i=0;i<PPC405_TLB_ENTRIES;i++)
   {
      tlb_entry = &cpu->ppc405_tlb[i];

      /* We want a valid entry with TID = PID */
      if (!(tlb_entry->tlb_hi & PPC405_TLBHI_V) ||
          (tlb_entry->tid != cpu->ppc405_pid))
         continue;

      /* Get the address mask corresponding to this entry */
      page_size = tlb_entry->tlb_hi & PPC405_TLBHI_SIZE_MASK;
      page_size >>= PPC405_TLBHI_SIZE_SHIFT;
      mask = ppc405_tlb_masks[page_size];

      /* Matching entry ? */
      if ((vaddr & mask) == (tlb_entry->tlb_hi & mask)) {
         map.vaddr  = vaddr & mask;
         map.paddr  = tlb_entry->tlb_lo & mask;
         map.cached = FALSE;

         if (!(entry = ppc32_mem_map(cpu,op_type,&map,entry,alt_entry)))
            goto err_undef;

         return entry;
      }
   }

   /* No matching TLB entry for this virtual address */
   ppc32_access_special(cpu,vaddr,cid,MTS_ACC_T,op_code,op_type,op_size,data);
   return NULL;

 err_undef:
   ppc32_access_special(cpu,vaddr,cid,MTS_ACC_U,op_code,op_type,op_size,data);
   return NULL;
}

/* Dump a PowerPC 405 TLB entry */
static void ppc405_dump_tlb_entry(cpu_ppc_t *cpu,u_int index)
{
   struct ppc405_tlb_entry *entry;

   entry = &cpu->ppc405_tlb[index];

   printf(" %2d: hi=0x%8.8x lo=0x%8.8x tid=0x%2.2x\n",
          index,entry->tlb_hi,entry->tlb_lo,entry->tid);
}

/* Dump the PowerPC 405 TLB */
static void ppc405_dump_tlb(cpu_gen_t *cpu)
{
   cpu_ppc_t *pcpu = CPU_PPC32(cpu);
   u_int i;

   for(i=0;i<PPC405_TLB_ENTRIES;i++)
      ppc405_dump_tlb_entry(pcpu,i);
}

/* === PPC Memory Operations ============================================= */

/* LBZ: Load Byte Zero */
fastcall void ppc32_lbz(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_LBZ,1,MTS_READ,&data);
   if (likely(haddr != NULL)) data = *(m_uint8_t *)haddr;
   cpu->gpr[reg] = data & 0xFF;
}

/* LHZ: Load Half-Word Zero */
fastcall void ppc32_lhz(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_LHZ,2,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh16(*(m_uint16_t *)haddr);
   cpu->gpr[reg] = data & 0xFFFF;
}

/* LWZ: Load Word Zero */
fastcall void ppc32_lwz(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_LWZ,4,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);
   cpu->gpr[reg] = data;
}

/* LWBR: Load Word Byte Reverse */
fastcall void ppc32_lwbr(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_LWBR,4,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);
   cpu->gpr[reg] = swap32(data);
}

/* LHA: Load Half-Word Algebraic */
fastcall void ppc32_lha(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_LHZ,2,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh16(*(m_uint16_t *)haddr);
   cpu->gpr[reg] = sign_extend_32(data,16);
}

/* STB: Store Byte */
fastcall void ppc32_stb(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   data = cpu->gpr[reg] & 0xff;
   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_STB,1,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint8_t *)haddr = data;
}

/* STH: Store Half-Word */
fastcall void ppc32_sth(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   data = cpu->gpr[reg] & 0xffff;
   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_STH,2,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint16_t *)haddr = htovm16(data);
}

/* STW: Store Word */
fastcall void ppc32_stw(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   data = cpu->gpr[reg];
   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_STW,4,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
}

/* STWBR: Store Word Byte Reversed */
fastcall void ppc32_stwbr(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   data = swap32(cpu->gpr[reg]);
   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_STWBR,4,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
}

/* LSW: Load String Word */
fastcall void ppc32_lsw(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_LSW,1,MTS_READ,&data);
   if (likely(haddr != NULL)) data = *(m_uint8_t *)haddr;
   cpu->gpr[reg] |= (data & 0xFF) << (24 - cpu->sw_pos);
}

/* STW: Store String Word */
fastcall void ppc32_stsw(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   data = (cpu->gpr[reg] >> (24 - cpu->sw_pos)) & 0xFF;
   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_STSW,1,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint8_t *)haddr = data;
}

/* LFD: Load Floating-Point Double */
fastcall void ppc32_lfd(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_LWZ,8,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);
   cpu->fpu.reg[reg] = data;
}

/* STFD: Store Floating-Point Double */
fastcall void ppc32_stfd(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   data = cpu->fpu.reg[reg];
   haddr = PPC32_MEM_DACCESS(cpu,vaddr,PPC_MEMOP_STW,8,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
}

/* ICBI: Instruction Cache Block Invalidate */
fastcall void ppc32_icbi(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int op)
{
   ppc32_jit_tcb_t *block;
   m_uint32_t phys_page;

#if DEBUG_ICBI
   cpu_log(cpu->gen,"MTS","ICBI: ia=0x%8.8x, vaddr=0x%8.8x\n",cpu->ia,vaddr);
#endif

   if (!cpu->translate(cpu,vaddr,PPC32_MTS_ICACHE,&phys_page)) {
      if (cpu->exec_phys_map) {
         block = ppc32_jit_find_by_phys_page(cpu,phys_page);

         if (block && (block->start_ia == (vaddr & PPC32_MIN_PAGE_MASK))) {
#if DEBUG_ICBI
            cpu_log(cpu->gen,"MTS",
                    "ICBI: removing compiled page at 0x%8.8x, pc=0x%8.8x\n",
                    block->start_ia,cpu->ia);
#endif
            ppc32_jit_tcb_free(cpu,block,TRUE);
            cpu->exec_blk_map[ppc32_jit_get_ia_hash(vaddr)] = NULL;
         }
         else
         {
#if DEBUG_ICBI
            cpu_log(cpu->gen,"MTS",
                    "ICBI: trying to remove page 0x%llx with pc=0x%llx\n",
                    block->start_ia,cpu->ia);
#endif
         }
      }
   }
}

/* ======================================================================== */

/* Get a BAT register pointer given a SPR index */
static inline m_uint32_t *ppc32_get_bat_spr_ptr(cpu_ppc_t *cpu,u_int spr)
{
   m_uint32_t spr_cat,cid,index;

   spr_cat = spr >> 5;
   if ((spr_cat != 0x10) && (spr_cat != 0x11))
      return NULL;

   cid   = (spr >> 3) & 0x1;
   index = (spr >> 1) & 0x3;

   if (spr & 0x20)
      index += 4;

   //printf("GET_BAT_SPR: SPR=%u => cid=%u, index=%u\n",spr,cid,index);

   return(&cpu->bat[cid][index].reg[spr & 0x1]);
}

/* Get a BAT SPR */
m_uint32_t ppc32_get_bat_spr(cpu_ppc_t *cpu,u_int spr)
{
   m_uint32_t *p;

   if (!(p = ppc32_get_bat_spr_ptr(cpu,spr)))
      return(0);

   return(*p);
}

/* Set a BAT SPR */
void ppc32_set_bat_spr(cpu_ppc_t *cpu,u_int spr,m_uint32_t val)
{
   m_uint32_t *p;

   if ((p = ppc32_get_bat_spr_ptr(cpu,spr))) {
      *p = val;
      ppc32_mem_invalidate_cache(cpu);
   }
}

/* ======================================================================== */

/* Rebuild MTS data structures */
static void ppc32_mem_rebuild_mts(cpu_gen_t *gen_cpu)
{
   ppc32_mem_invalidate_cache(CPU_PPC32(gen_cpu));
}

/* Initialize memory access vectors */
void ppc32_init_memop_vectors(cpu_ppc_t *cpu)
{
   /* MTS slow lookup */
   cpu->mts_slow_lookup = ppc32_slow_lookup;

   /* MTS rebuild */
   cpu->gen->mts_rebuild = ppc32_mem_rebuild_mts;

   /* MTS statistics */
   cpu->gen->mts_show_stats = ppc32_mem_show_stats;

   /* Memory lookup operation */
   cpu->mem_op_lookup = ppc32_mem_lookup;

   /* Translation operation */
   cpu->translate = ppc32_translate;

   /* Load Operations */
   cpu->mem_op_fn[PPC_MEMOP_LBZ] = ppc32_lbz;
   cpu->mem_op_fn[PPC_MEMOP_LHZ] = ppc32_lhz;
   cpu->mem_op_fn[PPC_MEMOP_LWZ] = ppc32_lwz;

   /* Load Operation with sign-extension */
   cpu->mem_op_fn[PPC_MEMOP_LHA] = ppc32_lha;

   /* Store Operations */
   cpu->mem_op_fn[PPC_MEMOP_STB] = ppc32_stb;
   cpu->mem_op_fn[PPC_MEMOP_STH] = ppc32_sth;
   cpu->mem_op_fn[PPC_MEMOP_STW] = ppc32_stw;

   /* Byte-Reversed operations */
   cpu->mem_op_fn[PPC_MEMOP_LWBR] = ppc32_lwbr;
   cpu->mem_op_fn[PPC_MEMOP_STWBR] = ppc32_stwbr;

   /* String operations */
   cpu->mem_op_fn[PPC_MEMOP_LSW] = ppc32_lsw;
   cpu->mem_op_fn[PPC_MEMOP_STSW] = ppc32_stsw;

   /* FPU operations */
   cpu->mem_op_fn[PPC_MEMOP_LFD] = ppc32_lfd;
   cpu->mem_op_fn[PPC_MEMOP_STFD] = ppc32_stfd;

   /* ICBI - Instruction Cache Block Invalidate */
   cpu->mem_op_fn[PPC_MEMOP_ICBI] = ppc32_icbi;
}

/* Restart the memory subsystem */
int ppc32_mem_restart(cpu_ppc_t *cpu)
{
   m_uint32_t family;

   ppc32_mem_shutdown(cpu);      
   ppc32_mem_init(cpu);
   ppc32_init_memop_vectors(cpu);

   /* Override the MTS lookup vector depending on the cpu type */
   family = cpu->pvr & 0xFFFF0000;

   if (family == PPC32_PVR_405) {
      cpu->mts_slow_lookup   = ppc405_slow_lookup;
      cpu->gen->mmu_dump     = ppc405_dump_tlb;
      cpu->gen->mmu_raw_dump = ppc405_dump_tlb;
   }

   return(0);
}
