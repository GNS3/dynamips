/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "cpu.h"
#include "mips64_jit.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

/* MTS access with special access mask */
void mips64_access_special(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint32_t mask,
                           u_int op_code,u_int op_type,u_int op_size,
                           m_uint64_t *data)
{
   switch(mask) {
      case MTS_ACC_U:
         if (op_type == MTS_READ)
            *data = 0;

         if (cpu->gen->undef_mem_handler != NULL) {
            if (cpu->gen->undef_mem_handler(cpu->gen,vaddr,op_size,op_type,
                                            data))
               return;
         }

#if DEBUG_MTS_ACC_U
         if (op_type == MTS_READ)
            cpu_log(cpu->gen,
                    "MTS","read  access to undefined address 0x%llx at "
                    "pc=0x%llx (size=%u)\n",vaddr,cpu->pc,op_size);
         else
            cpu_log(cpu->gen,
                    "MTS","write access to undefined address 0x%llx at "
                    "pc=0x%llx, value=0x%8.8llx (size=%u)\n",
                    vaddr,cpu->pc,*data,op_size);
#endif
         break;

      case MTS_ACC_T:
         if (op_code != MIPS_MEMOP_LOOKUP) {

            /* GR edit */
            /* If the IOS tries to access memory at addr 0x0, it is probably */
            /* a reload. Shut the vm down, otherwise 100% cpu and livespin   */
            if (vaddr == 0) {
              cpu_log(cpu->gen,
                    "MTS","TLB exception suggests RELOAD for address 0x%llx at pc=0x%llx "
                    "(%s access, size=%u)\n",
                    vaddr,cpu->pc,(op_type == MTS_READ) ? 
                    "read":"write",op_size);
              vm_stop(cpu->vm);
            }
            /* GR edit end */

#if DEBUG_MTS_ACC_T
            cpu_log(cpu->gen,
                    "MTS","TLB exception for address 0x%llx at pc=0x%llx "
                    "(%s access, size=%u)\n",
                    vaddr,cpu->pc,(op_type == MTS_READ) ? 
                    "read":"write",op_size);
            mips64_dump_regs(cpu->gen);
#if MEMLOG_ENABLE
            memlog_dump(cpu->gen);
#endif
#endif
      
            cpu->cp0.reg[MIPS_CP0_BADVADDR] = vaddr;

            if (op_type == MTS_READ)
               mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_TLB_LOAD,0);
            else
               mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_TLB_SAVE,0);
            
            cpu_exec_loop_enter(cpu->gen);
         }
         break;

      case MTS_ACC_AE:
         if (op_code != MIPS_MEMOP_LOOKUP) {
#if DEBUG_MTS_ACC_AE
            cpu_log(cpu->gen,
                    "MTS","AE exception for address 0x%llx at pc=0x%llx "
                    "(%s access)\n",
                    vaddr,cpu->pc,(op_type == MTS_READ) ? "read":"write");
#endif
            cpu->cp0.reg[MIPS_CP0_BADVADDR] = vaddr;

            if (op_type == MTS_READ)
               mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_ADDR_LOAD,0);
            else
               mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_ADDR_SAVE,0);

            cpu_exec_loop_enter(cpu->gen);
         }
         break;
   }
}

/* === MTS for 64-bit address space ======================================= */
#define MTS_ADDR_SIZE      64
#define MTS_NAME(name)     mts64_##name
#define MTS_NAME_UP(name)  MTS64_##name
#define MTS_PROTO(name)    mips64_mts64_##name
#define MTS_PROTO_UP(name) MIPS64_MTS64_##name

#include "mips_mts.c"

/* === MTS for 32-bit address space ======================================= */
#define MTS_ADDR_SIZE      32
#define MTS_NAME(name)     mts32_##name
#define MTS_NAME_UP(name)  MTS32_##name
#define MTS_PROTO(name)    mips64_mts32_##name
#define MTS_PROTO_UP(name) MIPS64_MTS32_##name

#include "mips_mts.c"

/* === Specific operations for MTS64 ====================================== */

/* MTS64 slow lookup */
static mts64_entry_t *
mips64_mts64_slow_lookup(cpu_mips_t *cpu,m_uint64_t vaddr,
                         u_int op_code,u_int op_size,
                         u_int op_type,m_uint64_t *data,
                         mts64_entry_t *alt_entry)
{
   m_uint32_t hash_bucket,zone,sub_zone,cca;
   mts64_entry_t *entry;
   mts_map_t map;

   map.tlb_index = -1;
   hash_bucket = MTS64_HASH(vaddr);
   entry = &cpu->mts_u.mts64_cache[hash_bucket];
   zone = vaddr >> 40;

#if DEBUG_MTS_STATS
   cpu->mts_misses++;
#endif

   switch(zone) {
      case 0x000000:   /* xkuseg */
      case 0x400000:   /* xksseg */
      case 0xc00000:   /* xkseg */
         /* trigger TLB exception if no matching entry found */
         if (!mips64_cp0_tlb_lookup(cpu,vaddr,&map))
            goto err_tlb;

         if (!(entry = mips64_mts64_map(cpu,op_type,&map,entry,alt_entry)))
            goto err_undef;

         return(entry);

      case 0xffffff:
         sub_zone  = (vaddr >> 29) & 0x7FF;

         switch(sub_zone) {
            case 0x7fc:   /* ckseg0 */
               map.vaddr  = vaddr & MIPS_MIN_PAGE_MASK;               
               map.paddr  = map.vaddr - 0xFFFFFFFF80000000ULL;
               map.offset = vaddr & MIPS_MIN_PAGE_IMASK;
               map.cached = TRUE;

               if (!(entry = mips64_mts64_map(cpu,op_type,&map,
                                              entry,alt_entry)))
                  goto err_undef;

               return(entry);

            case 0x7fd:   /* ckseg1 */
               map.vaddr  = vaddr & MIPS_MIN_PAGE_MASK;
               map.paddr  = map.vaddr - 0xFFFFFFFFA0000000ULL;
               map.offset = vaddr & MIPS_MIN_PAGE_IMASK;
               map.cached = FALSE;

               if (!(entry = mips64_mts64_map(cpu,op_type,&map,
                                              entry,alt_entry)))
                  goto err_undef;
               
               return(entry);

            case 0x7fe:   /* cksseg */
            case 0x7ff:   /* ckseg3 */
               /* trigger TLB exception if no matching entry found */
               if (!mips64_cp0_tlb_lookup(cpu,vaddr,&map))
                  goto err_tlb;

               if (!(entry = mips64_mts64_map(cpu,op_type,
                                              &map,entry,alt_entry)))
                  goto err_undef;

               return(entry);

            default:
               /* Invalid zone: generate Address Error (AE) exception */
               goto err_address;
         }
         break;
   
      /* xkphys */
      case 0x800000:
      case 0x880000:
      case 0x900000:
      case 0x980000:
      case 0xa00000:
      case 0xa80000:
      case 0xb00000:
      case 0xb80000:
         cca = (vaddr >> MIPS64_XKPHYS_CCA_SHIFT) & 0x03;
         map.cached = mips64_cca_cached(cca);
         map.vaddr  = vaddr & MIPS_MIN_PAGE_MASK;
         map.paddr  = (vaddr & MIPS64_XKPHYS_PHYS_MASK);
         map.paddr  &= MIPS_MIN_PAGE_MASK;
         map.offset = vaddr & MIPS_MIN_PAGE_IMASK;

         if (!(entry = mips64_mts64_map(cpu,op_type,&map,entry,alt_entry)))
            goto err_undef;

         return(entry);

      default:
         /* Invalid zone: generate Address Error (AE) exception */
         goto err_address;
   }

 err_undef:
   mips64_access_special(cpu,vaddr,MTS_ACC_U,op_code,op_type,op_size,data);
   return NULL;
 err_address:
   mips64_access_special(cpu,vaddr,MTS_ACC_AE,op_code,op_type,op_size,data);
   return NULL;
 err_tlb:
   mips64_access_special(cpu,vaddr,MTS_ACC_T,op_code,op_type,op_size,data);
   return NULL;
}

/* MTS64 access */
static forced_inline
void *mips64_mts64_access(cpu_mips_t *cpu,m_uint64_t vaddr,
                          u_int op_code,u_int op_size,
                          u_int op_type,m_uint64_t *data)
{   
   mts64_entry_t *entry,alt_entry;
   m_uint32_t hash_bucket;
   m_iptr_t haddr;
   u_int dev_id;
   int cow;

#if MEMLOG_ENABLE
   /* Record the memory access */
   memlog_rec_access(cpu->gen,vaddr,*data,op_size,op_type);
#endif
   
   hash_bucket = MTS64_HASH(vaddr);
   entry = &cpu->mts_u.mts64_cache[hash_bucket];

#if DEBUG_MTS_STATS
   cpu->mts_lookups++;
#endif

   /* Copy-On-Write for sparse device ? */
   cow = (op_type == MTS_WRITE) && (entry->flags & MTS_FLAG_COW);

   /* Slow lookup if nothing found in cache */
   if (unlikely(((vaddr & MIPS_MIN_PAGE_MASK) != entry->gvpa) || cow)) {
      entry = mips64_mts64_slow_lookup(cpu,vaddr,op_code,op_size,op_type,
                                       data,&alt_entry);
      if (!entry) 
         return NULL;

      if (entry->flags & MTS_FLAG_DEV) {
         dev_id = (entry->hpa & MTS_DEVID_MASK) >> MTS_DEVID_SHIFT;
         haddr  = entry->hpa & MTS_DEVOFF_MASK;
         return(dev_access_fast(cpu->gen,dev_id,haddr,op_size,op_type,data));
      }
   }

   /* Raw memory access */
   haddr = entry->hpa + (vaddr & MIPS_MIN_PAGE_IMASK);
#if MEMLOG_ENABLE
   memlog_update_read(cpu->gen,haddr);
#endif
   return((void *)haddr);
}

/* MTS64 virtual address to physical page translation */
static fastcall int mips64_mts64_translate(cpu_mips_t *cpu,m_uint64_t vaddr,
                                           m_uint32_t *phys_page)
{   
   mts64_entry_t *entry,alt_entry;
   m_uint32_t hash_bucket;
   m_uint64_t data = 0;
   
   hash_bucket = MTS64_HASH(vaddr);
   entry = &cpu->mts_u.mts64_cache[hash_bucket];

   /* Slow lookup if nothing found in cache */
   if (unlikely((vaddr & MIPS_MIN_PAGE_MASK) != entry->gvpa)) {
      entry = mips64_mts64_slow_lookup(cpu,vaddr,MIPS_MEMOP_LOOKUP,4,MTS_READ,
                                       &data,&alt_entry);
      if (!entry)
         return(-1);
   }

   *phys_page = entry->gppa >> MIPS_MIN_PAGE_SHIFT;
   return(0);
}

/* === Specific operations for MTS32 ====================================== */

/* MTS32 slow lookup */
static mts32_entry_t *
mips64_mts32_slow_lookup(cpu_mips_t *cpu,m_uint64_t vaddr,
                         u_int op_code,u_int op_size,
                         u_int op_type,m_uint64_t *data,
                         mts32_entry_t *alt_entry)
{
   m_uint32_t hash_bucket,zone;
   mts32_entry_t *entry;
   mts_map_t map;

   map.tlb_index = -1;
   hash_bucket = MTS32_HASH(vaddr);
   entry = &cpu->mts_u.mts32_cache[hash_bucket];
   zone = (vaddr >> 29) & 0x7;

#if DEBUG_MTS_STATS
   cpu->mts_misses++;
#endif

   switch(zone) {
      case 0x00 ... 0x03:   /* kuseg */
         /* trigger TLB exception if no matching entry found */
         if (!mips64_cp0_tlb_lookup(cpu,vaddr,&map))
            goto err_tlb;

         if (!(entry = mips64_mts32_map(cpu,op_type,&map,entry,alt_entry)))
            goto err_undef;

         return(entry);

      case 0x04:   /* kseg0 */
         map.vaddr  = vaddr & MIPS_MIN_PAGE_MASK;
         map.paddr  = map.vaddr - 0xFFFFFFFF80000000ULL;
         map.offset = vaddr & MIPS_MIN_PAGE_IMASK;
         map.cached = TRUE;

         if (!(entry = mips64_mts32_map(cpu,op_type,&map,entry,alt_entry)))
            goto err_undef;

         return(entry);

      case 0x05:   /* kseg1 */
         map.vaddr  = vaddr & MIPS_MIN_PAGE_MASK;
         map.paddr  = map.vaddr - 0xFFFFFFFFA0000000ULL;
         map.offset = vaddr & MIPS_MIN_PAGE_IMASK;
         map.cached = FALSE;

         if (!(entry = mips64_mts32_map(cpu,op_type,&map,entry,alt_entry)))
            goto err_undef;

         return(entry);

      case 0x06:   /* ksseg */
      case 0x07:   /* kseg3 */
         /* trigger TLB exception if no matching entry found */
         if (!mips64_cp0_tlb_lookup(cpu,vaddr,&map))
            goto err_tlb;

         if (!(entry = mips64_mts32_map(cpu,op_type,&map,entry,alt_entry)))
            goto err_undef;

         return(entry);
   }

 err_undef:
   mips64_access_special(cpu,vaddr,MTS_ACC_U,op_code,op_type,op_size,data);
   return NULL;
#if 0
 err_address:
   mips64_access_special(cpu,vaddr,MTS_ACC_AE,op_code,op_type,op_size,data);
   return NULL;
#endif
 err_tlb:
   mips64_access_special(cpu,vaddr,MTS_ACC_T,op_code,op_type,op_size,data);
   return NULL;
}

/* MTS32 access */
static forced_inline
void *mips64_mts32_access(cpu_mips_t *cpu,m_uint64_t vaddr,
                          u_int op_code,u_int op_size,
                          u_int op_type,m_uint64_t *data)
{
   mts32_entry_t *entry,alt_entry;
   m_uint32_t hash_bucket;
   m_iptr_t haddr;
   u_int dev_id;
   int cow;

#if MEMLOG_ENABLE
   /* Record the memory access */
   memlog_rec_access(cpu->gen,vaddr,*data,op_size,op_type);
#endif

   hash_bucket = MTS32_HASH(vaddr);
   entry = &cpu->mts_u.mts32_cache[hash_bucket];

#if DEBUG_MTS_STATS
   cpu->mts_lookups++;
#endif

   /* Copy-On-Write for sparse device ? */
   cow = (op_type == MTS_WRITE) && (entry->flags & MTS_FLAG_COW);

   /* Slow lookup if nothing found in cache */
   if (unlikely((((m_uint32_t)vaddr & MIPS_MIN_PAGE_MASK) != entry->gvpa) || 
                cow))
   {
      entry = mips64_mts32_slow_lookup(cpu,vaddr,op_code,op_size,op_type,
                                       data,&alt_entry);
      if (!entry) 
         return NULL;

      if (entry->flags & MTS_FLAG_DEV) {
         dev_id = (entry->hpa & MTS_DEVID_MASK) >> MTS_DEVID_SHIFT;
         haddr  = entry->hpa & MTS_DEVOFF_MASK;
         return(dev_access_fast(cpu->gen,dev_id,haddr,op_size,op_type,data));
      }
   }

   /* Raw memory access */
   haddr = entry->hpa + (vaddr & MIPS_MIN_PAGE_IMASK);
#if MEMLOG_ENABLE
   memlog_update_read(cpu->gen,haddr);
#endif
   return((void *)haddr);
}

/* MTS32 virtual address to physical page translation */
static fastcall int mips64_mts32_translate(cpu_mips_t *cpu,m_uint64_t vaddr,
                                           m_uint32_t *phys_page)
{     
   mts32_entry_t *entry,alt_entry;
   m_uint32_t hash_bucket;
   m_uint64_t data = 0;
   
   hash_bucket = MTS32_HASH(vaddr);
   entry = &cpu->mts_u.mts32_cache[hash_bucket];

   /* Slow lookup if nothing found in cache */
   if (unlikely(((m_uint32_t)vaddr & MIPS_MIN_PAGE_MASK) != entry->gvpa)) {
      entry = mips64_mts32_slow_lookup(cpu,vaddr,MIPS_MEMOP_LOOKUP,4,MTS_READ,
                                       &data,&alt_entry);
      if (!entry)
         return(-1);
   }

   *phys_page = entry->gppa >> MIPS_MIN_PAGE_SHIFT;
   return(0);
}

/* ======================================================================== */

/* Shutdown MTS subsystem */
void mips64_mem_shutdown(cpu_mips_t *cpu)
{
   if (cpu->mts_shutdown != NULL)
      cpu->mts_shutdown(cpu);
}

/* Set the address mode */
int mips64_set_addr_mode(cpu_mips_t *cpu,u_int addr_mode)
{
   if (cpu->addr_mode != addr_mode) {
      mips64_mem_shutdown(cpu);
      
      switch(addr_mode) {
         case 32:
            mips64_mts32_init(cpu);
            mips64_mts32_init_memop_vectors(cpu);
            break;
         case 64:
            mips64_mts64_init(cpu);
            mips64_mts64_init_memop_vectors(cpu);
            break;
         default:
            fprintf(stderr,
                    "mts_set_addr_mode: internal error (addr_mode=%u)\n",
                    addr_mode);
            exit(EXIT_FAILURE);
      }
   }

   return(0);
}
