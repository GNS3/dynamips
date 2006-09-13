/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
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

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "cpu.h"
#include "cp0.h"
#include "vm.h"

/* Record a memory access */
void memlog_rec_access(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint64_t data,
                       m_uint32_t op_size,m_uint32_t op_type)
{
   memlog_access_t *acc;

   acc = &cpu->memlog_array[cpu->memlog_pos];
   acc->pc      = cpu->pc;
   acc->vaddr   = vaddr;
   acc->data    = data;
   acc->op_size = op_size;
   acc->op_type = op_type;
   acc->data_valid = (op_type == MTS_WRITE);

   cpu->memlog_pos = (cpu->memlog_pos + 1) & (MEMLOG_COUNT - 1);
}

/* Show the latest memory accesses */
void memlog_dump(cpu_mips_t *cpu)
{
   memlog_access_t *acc;
   char s_data[64];
   u_int i,pos;
   
   for(i=0;i<MEMLOG_COUNT;i++) {
      pos = cpu->memlog_pos + i;
      pos &= (MEMLOG_COUNT-1);
      acc = &cpu->memlog_array[pos];

      if (cpu->pc) {
         if (acc->data_valid)
            snprintf(s_data,sizeof(s_data),"0x%llx",acc->data);
         else
            snprintf(s_data,sizeof(s_data),"XXXXXXXX");

         printf("CPU%u: pc=0x%8.8llx, vaddr=0x%8.8llx, "
                "size=%u, type=%s, data=%s\n",
                cpu->id,acc->pc,acc->vaddr,acc->op_size,
                (acc->op_type == MTS_READ) ? "read " : "write",
                s_data);
      }
   }
}

/* Update the data obtained by a read access */
void memlog_update_read(cpu_mips_t *cpu,m_iptr_t raddr)
{
   memlog_access_t *acc;

   acc = &cpu->memlog_array[(cpu->memlog_pos-1) & (MEMLOG_COUNT-1)];

   if (acc->op_type == MTS_READ) 
   {
      switch(acc->op_size) {
         case 1:
            acc->data = *(m_uint8_t *)raddr;
            break;
         case 2:
            acc->data = vmtoh16(*(m_uint16_t *)raddr);
            break;
         case 4:
            acc->data = vmtoh32(*(m_uint32_t *)raddr);
            break;
         case 8:
            acc->data = vmtoh64(*(m_uint64_t *)raddr);
            break;
      }

      acc->data_valid = TRUE;
   }
}

/* MTS access with special access mask */
void mts_access_special(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint32_t mask,
                        u_int op_code,u_int op_type,u_int op_size,
                        m_uint64_t *data,u_int *exc)
{
   switch(mask) {
      case MTS_ACC_U:
#if DEBUG_MTS_ACC_U
         if (op_type == MTS_READ)
            cpu_log(cpu,"MTS","read  access to undefined address 0x%llx at "
                    "pc=0x%llx (size=%u)\n",vaddr,cpu->pc,op_size);
         else
            cpu_log(cpu,"MTS","write access to undefined address 0x%llx at "
                    "pc=0x%llx, value=0x%8.8llx (size=%u)\n",
                    vaddr,cpu->pc,*data,op_size);
#endif
         if (op_type == MTS_READ)
            *data = 0;
         break;

      case MTS_ACC_T:
         if (op_code != MIPS_MEMOP_LOOKUP) {
#if DEBUG_MTS_ACC_T
            cpu_log(cpu,"MTS","TLB exception for address 0x%llx at pc=0x%llx "
                    "(%s access, size=%u)\n",
                    vaddr,cpu->pc,(op_type == MTS_READ) ? 
                    "read":"write",op_size);
            mips64_dump_regs(cpu);
#if MEMLOG_ENABLE
            memlog_dump(cpu);
#endif
#endif
            cpu->cp0.reg[MIPS_CP0_BADVADDR] = vaddr;

            if (op_type == MTS_READ)
               mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_TLB_LOAD,0);
            else
               mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_TLB_SAVE,0);
         }
         
         *exc = 1;
         break;

      case MTS_ACC_AE:
         if (op_code != MIPS_MEMOP_LOOKUP) {
#if DEBUG_MTS_ACC_AE
            cpu_log(cpu,"MTS","AE exception for address 0x%llx at pc=0x%llx "
                    "(%s access)\n",
                    vaddr,cpu->pc,(op_type == MTS_READ) ? "read":"write");
#endif
            cpu->cp0.reg[MIPS_CP0_BADVADDR] = vaddr;

            if (op_type == MTS_READ)
               mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_ADDR_LOAD,0);
            else
               mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_ADDR_SAVE,0);
         }

         *exc = 1;
         break;
   }
}

/* Allocate an L1 array */
mts32_l1_array_t *mts32_alloc_l1_array(m_iptr_t val)
{
   mts32_l1_array_t *p;
   u_int i;

   if (!(p = malloc(sizeof(mts32_l1_array_t))))
      return NULL;

   for(i=0;i<(1 << MTS32_LEVEL1_BITS);i++)
      p->entry[i] = val;

   return p;
}

/* Allocate an L2 array */
mts32_l2_array_t *mts32_alloc_l2_array(cpu_mips_t *cpu,m_iptr_t val)
{
   mts32_l2_array_t *p;
   u_int i;

   if (cpu->mts32_l2_free_list) {
      p = cpu->mts32_l2_free_list;
      cpu->mts32_l2_free_list = p->next;
   } else {
      if (!(p = m_memalign((1 << MTS_FLAG_BITS),sizeof(*p))))
         return NULL;
   }

   for(i=0;i<(1 << MTS32_LEVEL2_BITS);i++)
      p->entry[i] = val;

   return p;
}

/* Free an L1 array */
void mts32_free_l1_array(mts32_l1_array_t *array)
{
   u_int i;

   if (array != NULL) {
      for(i=0;i<(1<<MTS32_LEVEL1_BITS);i++)
         if (array->entry[i] & MTS_CHAIN_MASK)
            free((void *)(array->entry[i] & ~MTS_CHAIN_MASK));
      
      free(array);
   }
}

/* Free an L2 array */
void mts32_free_l2_array(cpu_mips_t *cpu,mts32_l2_array_t *array)
{
   array->next = cpu->mts32_l2_free_list;
   cpu->mts32_l2_free_list = array;
}

/* Set an L1 entry */
static void mts32_set_l1_data(cpu_mips_t *cpu,m_uint32_t start,m_uint32_t len,
                              m_iptr_t val)
{
   mts32_l1_array_t *p = cpu->mts_l1_ptr;
   m_uint32_t pos;
   m_iptr_t p2;

   while(len > 0) {
      pos = start >> (MTS32_LEVEL2_BITS + MTS32_OFFSET_BITS);

      if (pos >= (1 << MTS32_LEVEL1_BITS))
         break;

      /* free a possible L2 array */
      if (p->entry[pos] & MTS_CHAIN_MASK) {
         p2 = p->entry[pos] & ~MTS_CHAIN_MASK;
         mts32_free_l2_array(cpu,(mts32_l2_array_t *)p2);
      }

      p->entry[pos] = val;
      start += MTS32_LEVEL1_SIZE;
      len -= MTS32_LEVEL1_SIZE;
   }
}

/* Fork an L1 array */
static int mts32_fork_l1_array(cpu_mips_t *cpu,u_int l1_pos)
{
   mts32_l1_array_t *p1;
   mts32_l2_array_t *p2;
   m_iptr_t entry,val;
   u_int i;

   p1 = cpu->mts_l1_ptr;
   entry = p1->entry[l1_pos];
   val = ((entry & MTS_ACC_MASK) != MTS_ACC_OK) ? entry : 0;

   if (!(p2 = mts32_alloc_l2_array(cpu,val)))
      return(-1);

   /* mts32_alloc_l2_array() did the job for us */
   if (!val) {
      for(i=0;i<(1 << MTS32_LEVEL2_BITS);i++)
         p2->entry[i] = entry + (1 << MTS32_OFFSET_BITS);
   }
   
   p1->entry[l1_pos] = (m_iptr_t)p2 | MTS_CHAIN_MASK;
   return(0);
}

/* Set address error on a complete level 1 array */
void mts32_set_l1_ae(cpu_mips_t *cpu)
{
   mts32_l1_array_t *p1 = cpu->mts_l1_ptr;
   u_int i;

   for(i=0;i<(1<<MTS32_LEVEL1_BITS);i++)
      p1->entry[i] = MTS_ACC_AE;
}

/* Set an L2 entry */
static int mts32_set_l2_entry(cpu_mips_t *cpu,m_uint64_t vaddr,m_iptr_t val)
{
   m_uint32_t naddr = vaddr & 0xffffffff;
   m_uint32_t l1_pos,l2_pos;
   mts32_l1_array_t *p1;
   mts32_l2_array_t *p2;
   m_iptr_t entry;

   p1 = cpu->mts_l1_ptr;

   l1_pos = naddr >> (MTS32_LEVEL2_BITS + MTS32_OFFSET_BITS);
   l2_pos = (naddr >> MTS32_OFFSET_BITS) & ((1 << MTS32_LEVEL2_BITS) - 1);

   entry = p1->entry[l1_pos];

   if (!(entry & MTS_CHAIN_MASK)) {
      if (mts32_fork_l1_array(cpu,l1_pos) == -1) {
         fprintf(stderr,"mts32_set_l2_entry: unable to fork L1 entry.\n");
         return(-1);
      }

      entry = p1->entry[l1_pos];
   }

   p2 = (mts32_l2_array_t *)(entry & MTS_ADDR_MASK);
   p2->entry[l2_pos] = val;
   return(0);
}

/* Initialize an empty MTS32 subsystem */
int mts32_init_empty(cpu_mips_t *cpu)
{
   if (cpu->state == MIPS_CPU_RUNNING) {
      cpu_log(cpu,"MTS","trying to reset MTS while the CPU is online.\n");
      return(-1);
   }

   mts32_free_l1_array(cpu->mts_l1_ptr);

   /* Allocate a new L1 array */
   cpu->mts_l1_ptr = mts32_alloc_l1_array(0);

   if (!cpu->mts_l1_ptr)
      return(-1);
 
   /* Address Error on complete address space for now */
   mts32_set_l1_ae(cpu);
   return(0);
}

/* Free memory used by MTS32 */
void mts32_shutdown(cpu_mips_t *cpu)
{
   mts32_l2_array_t *array,*next;

   /* Free L1/L2 entries */
   if (cpu->mts_l1_ptr) {
      mts32_free_l1_array(cpu->mts_l1_ptr);
      cpu->mts_l1_ptr = NULL;
   }

   /* Free arrays that are sitting in the free list */
   for(array=cpu->mts32_l2_free_list;array;array=next) {
      next = array->next;
      free(array);
   }

   cpu->mts32_l2_free_list = NULL;
}

/* Map a device at the specified virtual address */
void mts32_map_device(cpu_mips_t *cpu,u_int dev_id,m_uint64_t vaddr,
                      m_uint32_t offset,m_uint32_t len)
{
   struct vdevice *dev;
   m_iptr_t val;

   if (!(dev = dev_get_by_id(cpu->vm,dev_id)) || !dev->phys_len)
      return;

#if DEBUG_MTS_MAP_DEV
   cpu_log(cpu,"MTS32",
           "mapping device %s (offset=0x%x,len=0x%x) at vaddr 0x%llx\n",
           dev->name,offset,len,vaddr);
#endif

   if (!dev->host_addr || (dev->flags & VDEVICE_FLAG_NO_MTS_MMAP))
      val = (dev_id << MTS_DEVID_SHIFT) | MTS_DEV_MASK | MTS_ACC_OK;
   else
      val = dev->host_addr | MTS_ACC_OK;

   val += offset;

   while(len > 0) {
      if (!(vaddr & MTS32_LEVEL1_MASK) && !(len & MTS32_LEVEL1_MASK)) {
         mts32_set_l1_data(cpu,vaddr,MTS32_LEVEL1_SIZE,val);
         vaddr += MTS32_LEVEL1_SIZE;
         val += MTS32_LEVEL1_SIZE;
         len -= MTS32_LEVEL1_SIZE;
      } else {         
         mts32_set_l2_entry(cpu,vaddr,val);
         vaddr += MTS32_LEVEL2_SIZE;
         val += MTS32_LEVEL2_SIZE;
         len -= MTS32_LEVEL2_SIZE;
      }
   }
}

/* Map a physical address to the specified virtual address */
void mts32_map(cpu_mips_t *cpu,m_uint64_t vaddr,
               m_uint64_t paddr,m_uint32_t len,
               int cache_access)
{ 
   struct vdevice *dev,*next_dev;
   m_uint32_t dev_offset,clen;

   while(len > 0)
   {
#if DEBUG_MTS_MAP_VIRT
      cpu_log(cpu,"MTS32",
              "mts32_map: vaddr=0x%llx, paddr=0x%llx, len=0x%x, cache=%d\n",
              vaddr,paddr,len,cache_access);
#endif
      dev = dev_lookup(cpu->vm,paddr,cache_access);
      next_dev = dev_lookup_next(cpu->vm,paddr,dev,cache_access);

      if (next_dev)
         clen = m_min(len,next_dev->phys_addr-paddr);
      else
         clen = len;

      if (!dev) {
         mts32_unmap(cpu,vaddr,clen,MTS_ACC_U);
      } else {
         dev_offset = paddr - dev->phys_addr;
         clen = m_min(clen,dev->phys_len);
         mts32_map_device(cpu,dev->id,vaddr,dev_offset,clen);
      }

      vaddr += clen;
      paddr += clen;
      len -= clen;
   }
}

/* Unmap a memory zone */
void mts32_unmap(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint32_t len,
                 m_uint32_t val)
{
   while(len > 0)
   {
#if DEBUG_MTS_MAP_VIRT
      cpu_log(cpu,"MTS32","mts32_unmap: vaddr=0x%llx, len=0x%x\n",vaddr,len);
#endif
      if (!(vaddr & MTS32_LEVEL1_MASK) && !(len & MTS32_LEVEL1_MASK)) {
         mts32_set_l1_data(cpu,vaddr,MTS32_LEVEL1_SIZE,val);
         vaddr += MTS32_LEVEL1_SIZE;
         len -= MTS32_LEVEL1_SIZE;
      } else {         
         mts32_set_l2_entry(cpu,vaddr,val);
         vaddr += MTS32_LEVEL2_SIZE;
         len -= MTS32_LEVEL2_SIZE;
      }
   }
}

/* Map all devices for kernel mode */
void mts32_km_map_all_dev(cpu_mips_t *cpu)
{
   /* KSEG0: cached accesses */
   mts32_map(cpu,MIPS_KSEG0_BASE,0,MTS_SIZE_512M,TRUE);

   /* KSEG1: uncached accesses */
   mts32_map(cpu,MIPS_KSEG1_BASE,0,MTS_SIZE_512M,FALSE);
}

/* MTS32 raw lookup */
static forced_inline 
void *mts32_raw_lookup(cpu_mips_t *cpu,mts32_l1_array_t *p1,m_uint64_t vaddr)
{    
   m_uint32_t naddr = vaddr & 0xffffffff;
   m_uint32_t l1_pos,l2_pos,shift;
   mts32_l2_array_t *p2;
   m_iptr_t entry,haddr;
   m_uint64_t data;
   u_int dev_id;

   shift = MTS32_LEVEL2_BITS + MTS32_OFFSET_BITS;
   l1_pos = naddr >> shift;
   entry = p1->entry[l1_pos];

   if (unlikely((entry & MTS_ACC_MASK) != MTS_ACC_OK))
      return NULL;

   if (entry & MTS_CHAIN_MASK) {
      p2 = (mts32_l2_array_t *)(entry & MTS_ADDR_MASK);
      l2_pos = (naddr >> MTS32_OFFSET_BITS) & ((1 << MTS32_LEVEL2_BITS) - 1);
      entry = p2->entry[l2_pos];
      shift = MTS32_OFFSET_BITS;
   }

   if (unlikely((entry & MTS_ACC_MASK) != MTS_ACC_OK))
      return NULL;

   /* device access */
   if (unlikely(entry & MTS_DEV_MASK)) {
      dev_id = (entry & MTS_DEVID_MASK) >> MTS_DEVID_SHIFT;
      haddr = (entry & MTS_DEVOFF_MASK);
      haddr += (naddr & ((1 << shift) - 1));
      return(dev_access_fast(cpu,dev_id,haddr,4,MTS_READ,&data));
   }

   haddr = entry & MTS_ADDR_MASK;
   haddr += (naddr & ((1 << shift) - 1));   
   return((void *)haddr);
}

/* MTS32 access */
static forced_inline void *mts32_access(cpu_mips_t *cpu,m_uint64_t vaddr,
                                        u_int op_code,u_int op_size,
                                        u_int op_type,m_uint64_t *data,
                                        u_int *exc)
{   
   m_uint32_t naddr = vaddr & 0xffffffff;
   m_uint32_t l1_pos,l2_pos,mask,shift;
   mts32_l1_array_t *p1 = cpu->mts_l1_ptr;
   mts32_l2_array_t *p2;
   m_iptr_t entry,haddr;
   u_int dev_id;

#if MEMLOG_ENABLE
   /* Record the memory access */
   memlog_rec_access(cpu,vaddr,*data,op_size,op_type);
#endif

   *exc = 0;
   shift = MTS32_LEVEL2_BITS + MTS32_OFFSET_BITS;
   l1_pos = naddr >> shift;
   entry = p1->entry[l1_pos];

   if (unlikely((mask = (entry & MTS_ACC_MASK)) != MTS_ACC_OK)) {
      mts_access_special(cpu,vaddr,mask,op_code,op_type,op_size,data,exc);
      return NULL;
   }

   /* do we have a level 2 entry ? */
   if (entry & MTS_CHAIN_MASK) {
      p2 = (mts32_l2_array_t *)(entry & MTS_ADDR_MASK);
      l2_pos = (naddr >> MTS32_OFFSET_BITS) & ((1 << MTS32_LEVEL2_BITS) - 1);
      entry = p2->entry[l2_pos];
      shift = MTS32_OFFSET_BITS;

      if (unlikely((mask = (entry & MTS_ACC_MASK)) != MTS_ACC_OK)) {
         mts_access_special(cpu,vaddr,mask,op_code,op_type,op_size,data,exc);
         return NULL;
      }
   }

   /* device access */
   if (unlikely(entry & MTS_DEV_MASK)) {
      dev_id = (entry & MTS_DEVID_MASK) >> MTS_DEVID_SHIFT;
      haddr = (entry & MTS_DEVOFF_MASK);
      haddr += (naddr & ((1 << shift) - 1));

#if DEBUG_MTS_DEV
      cpu_log(cpu,"MTS32",
              "device access: vaddr=0x%llx, pc=0x%llx, dev_offset=0x%x\n",
              vaddr,cpu->pc,haddr);
#endif
      return(dev_access_fast(cpu,dev_id,haddr,op_size,op_type,data));
   }

   /* raw memory access */
   haddr = entry & MTS_ADDR_MASK;
   haddr += (naddr & ((1 << shift) - 1));
#if MEMLOG_ENABLE
   memlog_update_read(cpu,haddr);
#endif
   return((void *)haddr);
}

/* Memory lookup */
void *mts32_lookup(cpu_mips_t *cpu,m_uint64_t vaddr)
{
   return(mts32_raw_lookup(cpu,cpu->mts_l1_ptr,vaddr));
}

/* Initialize the MTS64 subsystem for the specified CPU */
int mts64_init(cpu_mips_t *cpu)
{
   size_t len;

   /* Initialize the cache entries to 0 (empty) */
   len = MTS64_HASH_SIZE * sizeof(mts64_entry_t *);
   if (!(cpu->mts64_cache = malloc(len)))
      return(-1);

   memset(cpu->mts64_cache,0,len);
   cpu->mts64_lookups = 0;
   cpu->mts64_misses  = 0;

   /* Reset the TLB reverse map (used for selective invalidations) */
   memset(cpu->mts64_rmap,0,(cpu->cp0.tlb_entries * sizeof(mts64_entry_t *)));
   return(0);
}

/* Free memory used by MTS64 */
void mts64_shutdown(cpu_mips_t *cpu)
{
   mts64_chunk_t *chunk,*next;
   int i;

   /* Reset the reverse map */
   for(i=0;i<cpu->cp0.tlb_entries;i++)
      cpu->mts64_rmap[i] = NULL;

   /* Free the cache itself */
   free(cpu->mts64_cache);
   cpu->mts64_cache = NULL;

   /* Free the chunks */
   for(chunk=cpu->mts64_chunk_list;chunk;chunk=next) {
      next = chunk->next;
      free(chunk);
   }

   for(chunk=cpu->mts64_chunk_free_list;chunk;chunk=next) {
      next = chunk->next;
      free(chunk);
   }
   
   cpu->mts64_chunk_list = cpu->mts64_chunk_free_list = NULL;
   cpu->mts64_entry_free_list = NULL;
}

/* Show MTS64 detailed information (debugging only!) */
void mts64_show_stats(cpu_mips_t *cpu)
{
   mts64_chunk_t *chunk;
#if DEBUG_MTS_MAP_VIRT
   mts64_entry_t *entry;
   u_int i;
#endif
   u_int count;

   printf("\nCPU%u: MTS64 statistics:\n",cpu->id);

   printf("   Total lookups: %llu, misses: %llu, efficiency: %g%%\n",
          cpu->mts64_lookups, cpu->mts64_misses,
          100 - ((double)(cpu->mts64_misses*100)/
                 (double)cpu->mts64_lookups));

#if DEBUG_MTS_MAP_VIRT
   /* Valid hash entries */
   for(count=0,i=0;i<MTS64_HASH_SIZE;i++) {
      if ((entry = cpu->mts64_cache[i]) != NULL) {
         printf("    %4u: entry=%p, start=0x%16.16llx, "
                "len=0x%8.8x (%6u bytes), action=0x%llx\n",
                i,entry,entry->start,entry->mask,entry->mask+1,
                (m_uint64_t)entry->action);
         count++;
      }
   }

   printf("   %u/%u valid hash entries.\n",count,MTS64_HASH_SIZE);
#endif

   /* Number of chunks */
   for(count=0,chunk=cpu->mts64_chunk_list;chunk;chunk=chunk->next)
      count++;

   printf("   Number of chunks: %u\n",count);

#if DEBUG_MTS_MAP_VIRT
   /* Reverse map */
   for(i=0;i<MIPS64_TLB_ENTRIES;i++) {
      for(count=0,entry=cpu->mts64_rmap[i];entry;entry=entry->next)
         count++;

      if (count > 0)
         printf("   tlb_rmap[%u]: %u entries\n",i,count);
   }
#endif
}

/* Allocate a new chunk */
static int mts64_alloc_chunk(cpu_mips_t *cpu)
{
   mts64_chunk_t *chunk;

   /* Try the free list first, then use standard allocation procedure */
   if ((chunk = cpu->mts64_chunk_free_list) != NULL) {
      cpu->mts64_chunk_free_list = chunk->next;
   } else {
      if (!(chunk = malloc(sizeof(*chunk))))
         return(-1);
   }

   chunk->count = 0;
   chunk->next = cpu->mts64_chunk_list;
   cpu->mts64_chunk_list = chunk;
   return(0);
}

/* Allocate a new entry */
static mts64_entry_t *mts64_alloc_entry(cpu_mips_t *cpu)
{
   mts64_chunk_t *chunk = cpu->mts64_chunk_list;
   mts64_entry_t *entry;

   /* First, try to allocate the entry from the free list */
   if ((entry = cpu->mts64_entry_free_list) != NULL) {
      cpu->mts64_entry_free_list = cpu->mts64_entry_free_list->next;
      return entry;
   }

   /* A new chunk is required */
   if (!chunk || (chunk->count == MTS64_CHUNK_SIZE)) {
      if (mts64_alloc_chunk(cpu) == -1)
         return NULL;

      chunk = cpu->mts64_chunk_list;
   }

   entry = &chunk->entry[chunk->count];
   chunk->count++;
   return entry;
}

/* Invalidate the complete MTS64 cache */
void mts64_invalidate_cache(cpu_mips_t *cpu)
{
   mts64_chunk_t *chunk;
   size_t len;
   u_int i;

   len = MTS64_HASH_SIZE * sizeof(mts64_entry_t *);
   memset(cpu->mts64_cache,0,len);
 
   /* Move all chunks to the free list */
   while((chunk = cpu->mts64_chunk_list) != NULL) {
      cpu->mts64_chunk_list = chunk->next;
      chunk->next = cpu->mts64_chunk_free_list;
      cpu->mts64_chunk_free_list = chunk;
   }

   /* Reset the free list of entries (since they are located in chunks) */
   cpu->mts64_entry_free_list = NULL;

   /* Reset the reverse map */
   for(i=0;i<cpu->cp0.tlb_entries;i++)
      cpu->mts64_rmap[i] = NULL;
}

/* Invalidate partially the MTS64 cache, given a TLB entry index */
void mts64_invalidate_tlb_entry(cpu_mips_t *cpu,u_int tlb_index)
{
   mts64_entry_t *entry;

   for(entry=cpu->mts64_rmap[tlb_index];entry;entry=entry->next) {
      *(entry->pself) = NULL;
      if (!entry->next) {
         entry->next = cpu->mts64_entry_free_list;
         break;
      }
   }

   cpu->mts64_entry_free_list = cpu->mts64_rmap[tlb_index];
   cpu->mts64_rmap[tlb_index] = NULL;
} 

/* 
 * MTS64 mapping.
 *
 * It is NOT inlined since it triggers a GCC bug on my config (x86, GCC 3.3.5)
 */
static no_inline int mts64_map(cpu_mips_t *cpu,m_uint64_t vaddr,mts_map_t *map,
                               mts64_entry_t *entry)
{
   struct vdevice *dev;
   m_uint64_t lk_addr;
   m_uint32_t poffset;

   lk_addr = map->paddr + (vaddr - map->vaddr);

   if (!(dev = dev_lookup(cpu->vm,lk_addr,map->cached)))
      return(FALSE);

   if (map->paddr > dev->phys_addr) {
      poffset = map->paddr - dev->phys_addr;
      entry->start     = map->vaddr;
      entry->phys_page = map->paddr >> MIPS_MIN_PAGE_SHIFT;
      entry->mask      = ~((m_min(map->len,dev->phys_len - poffset)) - 1);
      entry->action    = poffset;
   } else {
      poffset = dev->phys_addr - map->paddr;
      entry->start     = map->vaddr + poffset;
      entry->phys_page = (map->paddr + poffset) >> MIPS_MIN_PAGE_SHIFT;
      entry->mask      = ~((m_min(map->len - poffset,dev->phys_len)) - 1);
      entry->action    = 0;
   }

   if (!dev->host_addr || (dev->flags & VDEVICE_FLAG_NO_MTS_MMAP))
      entry->action += (dev->id << MTS_DEVID_SHIFT) | MTS_DEV_MASK;
   else
      entry->action += dev->host_addr;

   return(TRUE);
}

/* MTS64 slow lookup */
static forced_inline 
mts64_entry_t *mts64_slow_lookup(cpu_mips_t *cpu,m_uint64_t vaddr,
                                 u_int op_code,u_int op_size,
                                 u_int op_type,m_uint64_t *data,
                                 u_int *exc)
{
   m_uint32_t hash_bucket,zone,sub_zone,cca;
   mts64_entry_t *entry,new_entry;
   mts_map_t map;

   map.tlb_index = -1;
   hash_bucket = MTS64_HASH(vaddr);
   entry = cpu->mts64_cache[hash_bucket];
   zone = vaddr >> 40;

#if DEBUG_MTS_STATS
   cpu->mts64_misses++;
#endif

   switch(zone) {
      case 0x000000:   /* xkuseg */
      case 0x400000:   /* xksseg */
      case 0xc00000:   /* xkseg */
         /* trigger TLB exception if no matching entry found */
         if (!cp0_tlb_lookup(cpu,vaddr,&map))
            goto err_tlb;

         if (!mts64_map(cpu,vaddr,&map,&new_entry))
            goto err_undef;
         break;

      case 0xffffff:
         sub_zone  = (vaddr >> 29) & 0x7FF;

         switch(sub_zone) {
            case 0x7fc:   /* ckseg0 */
               map.vaddr  = sign_extend(MIPS_KSEG0_BASE,32);
               map.paddr  = 0;
               map.len    = MIPS_KSEG0_SIZE;
               map.cached = TRUE;
               if (!mts64_map(cpu,vaddr,&map,&new_entry))
                  goto err_undef;
               break;

            case 0x7fd:   /* ckseg1 */
               map.vaddr  = sign_extend(MIPS_KSEG1_BASE,32);
               map.paddr  = 0;
               map.len    = MIPS_KSEG1_SIZE;
               map.cached = FALSE;
               if (!mts64_map(cpu,vaddr,&map,&new_entry))
                  goto err_undef;
               break;

            case 0x7fe:   /* cksseg */
            case 0x7ff:   /* ckseg3 */
               /* trigger TLB exception if no matching entry found */
               if (!cp0_tlb_lookup(cpu,vaddr,&map))
                  goto err_tlb;

               if (!mts64_map(cpu,vaddr,&map,&new_entry))
                  goto err_undef;
               break;

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
         map.vaddr  = vaddr & MIPS64_XKPHYS_ZONE_MASK;
         map.paddr  = 0;
         map.len    = MIPS64_XKPHYS_PHYS_SIZE;
         if (!mts64_map(cpu,vaddr,&map,&new_entry))
            goto err_undef;
         break;

      default:
         /* Invalid zone: generate Address Error (AE) exception */
         goto err_address;
   }

   /* Get a new entry if necessary */
   if (!entry) {
      entry = mts64_alloc_entry(cpu);
      entry->pself = entry->pprev = NULL;
      entry->next = NULL;

      /* Store the entry in hash table for future use */
      cpu->mts64_cache[hash_bucket] = entry;
   } else {
      /* Remove the entry from the reverse map list */
      if (entry->pprev) {
         if (entry->next)
            entry->next->pprev = entry->pprev;

         *(entry->pprev) = entry->next;
      }
   }

   /* Add this entry to the reverse map list */
   if (map.tlb_index != -1) {
      entry->pself = &cpu->mts64_cache[hash_bucket];
      entry->next  = cpu->mts64_rmap[map.tlb_index];
      entry->pprev = &cpu->mts64_rmap[map.tlb_index];
      if (entry->next)
         entry->next->pprev = &entry->next;
      cpu->mts64_rmap[map.tlb_index] = entry;
   }

   /* Fill the new entry or replace the previous */
   entry->phys_page = new_entry.phys_page;
   entry->start  = new_entry.start;
   entry->mask   = new_entry.mask;
   entry->action = new_entry.action;
   return entry;

 err_undef:
   mts_access_special(cpu,vaddr,MTS_ACC_U,op_code,op_type,op_size,data,exc);
   return NULL;
 err_address:
   mts_access_special(cpu,vaddr,MTS_ACC_AE,op_code,op_type,op_size,data,exc);
   return NULL;
 err_tlb:
   mts_access_special(cpu,vaddr,MTS_ACC_T,op_code,op_type,op_size,data,exc);
   return NULL;
}
     
/* MTS64 access */
static forced_inline void *mts64_access(cpu_mips_t *cpu,m_uint64_t vaddr,
                                        u_int op_code,u_int op_size,
                                        u_int op_type,m_uint64_t *data,
                                        u_int *exc)
{
   m_uint32_t hash_bucket;
   mts64_entry_t *entry;
   m_iptr_t haddr;
   u_int dev_id;

#if MEMLOG_ENABLE
   /* Record the memory access */
   memlog_rec_access(cpu,vaddr,*data,op_size,op_type);
#endif

   *exc = 0;
   hash_bucket = MTS64_HASH(vaddr);
   entry = cpu->mts64_cache[hash_bucket];

#if DEBUG_MTS_STATS
   cpu->mts64_lookups++;
#endif

   /* Slow lookup if nothing found in cache */
   if (unlikely((!entry) || 
       unlikely((vaddr & sign_extend(entry->mask,32)) != entry->start))) 
   {
      entry = mts64_slow_lookup(cpu,vaddr,op_code,op_size,op_type,data,exc);
      if (!entry) return NULL;
   }

   /* Device access */
   if (unlikely(entry->action & MTS_DEV_MASK)) {
      dev_id = (entry->action & MTS_DEVID_MASK) >> MTS_DEVID_SHIFT;
      haddr = entry->action & MTS_DEVOFF_MASK;
      haddr += vaddr - entry->start;

#if DEBUG_MTS_DEV
      cpu_log(cpu,"MTS64",
              "device access: vaddr=0x%llx, pc=0x%llx, dev_offset=0x%x\n",
              vaddr,cpu->pc,haddr);
#endif
      return(dev_access_fast(cpu,dev_id,haddr,op_size,op_type,data));
   }

   /* Raw memory access */
   haddr = entry->action & MTS_ADDR_MASK;
   haddr += vaddr - entry->start;
#if MEMLOG_ENABLE
   memlog_update_read(cpu,haddr);
#endif
   return((void *)haddr);
}

/* MTS64 lookup */
static void *mts64_lookup(cpu_mips_t *cpu,m_uint64_t vaddr)
{
   m_uint64_t data;
   u_int exc;
   return(mts64_access(cpu,vaddr,MIPS_MEMOP_LOOKUP,4,MTS_READ,&data,&exc));
}

/* MTS64 virtual address to physical page translation */
static fastcall int mts64_translate(cpu_mips_t *cpu,m_uint64_t vaddr,
                                    m_uint32_t *phys_page)
{   
   m_uint32_t hash_bucket,offset;
   mts64_entry_t *entry;
   m_uint64_t data = 0;
   u_int exc = 0;
   
   hash_bucket = MTS64_HASH(vaddr);
   entry = cpu->mts64_cache[hash_bucket];

   /* Slow lookup if nothing found in cache */
   if (unlikely((!entry) || 
       unlikely((vaddr & sign_extend(entry->mask,32)) != entry->start))) 
   {
      entry = mts64_slow_lookup(cpu,vaddr,MIPS_MEMOP_LOOKUP,4,MTS_READ,
                                &data,&exc);
      if (!entry)
         return(-1);
   }

   offset = vaddr - entry->start;
   *phys_page = entry->phys_page + (offset >> MIPS_MIN_PAGE_SHIFT);
   return(0);
}

/* === MIPS Memory Operations ============================================= */

/* Macro for MTS access (32 or 64 bit) */
#define MTS_ACCESS(X) mts##X##_access
#define MTS_MEMOP(op) MTS_##op(32) MTS_##op(64)

/* LB: Load Byte */
#define MTS_LB(X) \
fastcall u_int mts##X##_lb(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)       \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_LB,1,MTS_READ,&data,&exc);     \
   if (likely(haddr != NULL)) data = *(m_uint8_t *)haddr;                    \
   if (likely(!exc)) cpu->gpr[reg] = sign_extend(data,8);                    \
   return(exc);                                                              \
}                                                                            \

/* LBU: Load Byte Unsigned */
#define MTS_LBU(X) \
fastcall u_int mts##X##_lbu(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)      \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_LBU,1,MTS_READ,&data,&exc);    \
   if (likely(haddr != NULL)) data = *(m_uint8_t *)haddr;                    \
   if (likely(!exc)) cpu->gpr[reg] = data & 0xff;                            \
   return(exc);                                                              \
}

/* LH: Load Half-Word */
#define MTS_LH(X) \
fastcall u_int mts##X##_lh(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)       \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_LH,2,MTS_READ,&data,&exc);     \
   if (likely(haddr != NULL)) data = vmtoh16(*(m_uint16_t *)haddr);          \
   if (likely(!exc)) cpu->gpr[reg] = sign_extend(data,16);                   \
   return(exc);                                                              \
}

/* LHU: Load Half-Word Unsigned */
#define MTS_LHU(X) \
fastcall u_int mts##X##_lhu(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)      \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_LHU,2,MTS_READ,&data,&exc);    \
   if (likely(haddr != NULL)) data = vmtoh16(*(m_uint16_t *)haddr);          \
   if (likely(!exc)) cpu->gpr[reg] = data & 0xffff;                          \
   return(exc);                                                              \
}

/* LW: Load Word */
#define MTS_LW(X) \
fastcall u_int mts##X##_lw(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)       \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_LW,4,MTS_READ,&data,&exc);     \
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);          \
   if (likely(!exc)) cpu->gpr[reg] = sign_extend(data,32);                   \
   return(exc);                                                              \
}

/* LWU: Load Word Unsigned */
#define MTS_LWU(X) \
fastcall u_int mts##X##_lwu(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)      \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_LWU,4,MTS_READ,&data,&exc);    \
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);          \
   if (likely(!exc)) cpu->gpr[reg] = data & 0xffffffff;                      \
   return(exc);                                                              \
}

/* LD: Load Double-Word */
#define MTS_LD(X) \
fastcall u_int mts##X##_ld(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)       \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_LD,8,MTS_READ,&data,&exc);     \
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);          \
   if (likely(!exc)) cpu->gpr[reg] = data;                                   \
   return(exc);                                                              \
}

/* SB: Store Byte */
#define MTS_SB(X) \
fastcall u_int mts##X##_sb(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)       \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   data = cpu->gpr[reg] & 0xff;                                              \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_SB,1,MTS_WRITE,&data,&exc);    \
   if (likely(haddr != NULL)) *(m_uint8_t *)haddr = data;                    \
   return(exc);                                                              \
}

/* SH: Store Half-Word */
#define MTS_SH(X) \
fastcall u_int mts##X##_sh(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)       \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   data = cpu->gpr[reg] & 0xffff;                                            \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_SH,2,MTS_WRITE,&data,&exc);    \
   if (likely(haddr != NULL)) *(m_uint16_t *)haddr = htovm16(data);          \
   return(exc);                                                              \
}

/* SW: Store Word */
#define MTS_SW(X) \
fastcall u_int mts##X##_sw(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)       \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   data = cpu->gpr[reg] & 0xffffffff;                                        \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_SW,4,MTS_WRITE,&data,&exc);    \
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);          \
   return(exc);                                                              \
}

/* SD: Store Double-Word */
#define MTS_SD(X) \
fastcall u_int mts##X##_sd(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)       \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   data = cpu->gpr[reg];                                                     \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_SD,8,MTS_WRITE,&data,&exc);    \
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);          \
   return(exc);                                                              \
}

/* LDC1: Load Double-Word To Coprocessor 1 */
#define MTS_LDC1(X) \
fastcall u_int mts##X##_ldc1(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)     \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_LDC1,8,MTS_READ,&data,&exc);   \
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);          \
   if (likely(!exc)) cpu->fpu.reg[reg] = data;                               \
   return(exc);                                                              \
}

/* LWL: Load Word Left */
#define MTS_LWL(X) \
fastcall u_int mts##X##_lwl(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)      \
{                                                                            \
   m_uint64_t r_mask,naddr;                                                  \
   m_uint64_t data;                                                          \
   u_int m_shift;                                                            \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   naddr = vaddr & ~(0x03);                                                  \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_LWL,4,MTS_READ,&data,&exc);    \
                                                                             \
   if (likely(haddr != NULL))                                                \
      data = vmtoh32(*(m_uint32_t *)haddr);                                  \
                                                                             \
   if (likely(!exc)) {                                                       \
      m_shift = (vaddr & 0x03) << 3;                                         \
      r_mask = (1ULL << m_shift) - 1;                                        \
      data <<= m_shift;                                                      \
                                                                             \
      cpu->gpr[reg] &= r_mask;                                               \
      cpu->gpr[reg] |= data;                                                 \
      cpu->gpr[reg] = sign_extend(cpu->gpr[reg],32);                         \
   }                                                                         \
   return(exc);                                                              \
}

/* LWR: Load Word Right */
#define MTS_LWR(X) \
fastcall u_int mts##X##_lwr(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)      \
{                                                                            \
   m_uint64_t r_mask,naddr;                                                  \
   m_uint64_t data;                                                          \
   u_int m_shift;                                                            \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   naddr = vaddr & ~(0x03);                                                  \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_LWR,4,MTS_READ,&data,&exc);    \
                                                                             \
   if (likely(haddr != NULL))                                                \
      data = vmtoh32(*(m_uint32_t *)haddr);                                  \
                                                                             \
   if (likely(!exc)) {                                                       \
      m_shift = ((vaddr & 0x03) + 1) << 3;                                   \
      r_mask = (1ULL << m_shift) - 1;                                        \
                                                                             \
      data = sign_extend(data >> (32 - m_shift),32);                         \
      r_mask = sign_extend(r_mask,32);                                       \
                                                                             \
      cpu->gpr[reg] &= ~r_mask;                                              \
      cpu->gpr[reg] |= data;                                                 \
   }                                                                         \
   return(exc);                                                              \
}

/* LDL: Load Double-Word Left */
#define MTS_LDL(X) \
fastcall u_int mts##X##_ldl(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)      \
{                                                                            \
   m_uint64_t r_mask,naddr;                                                  \
   m_uint64_t data;                                                          \
   u_int m_shift;                                                            \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   naddr = vaddr & ~(0x07);                                                  \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_LDL,8,MTS_READ,&data,&exc);    \
                                                                             \
   if (likely(haddr != NULL))                                                \
      data = vmtoh64(*(m_uint64_t *)haddr);                                  \
                                                                             \
   if (likely(!exc)) {                                                       \
      m_shift = (vaddr & 0x07) << 3;                                         \
      r_mask = (1ULL << m_shift) - 1;                                        \
      data <<= m_shift;                                                      \
                                                                             \
      cpu->gpr[reg] &= r_mask;                                               \
      cpu->gpr[reg] |= data;                                                 \
   }                                                                         \
   return(exc);                                                              \
}

/* LDR: Load Double-Word Right */
#define MTS_LDR(X) \
fastcall u_int mts##X##_ldr(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)      \
{                                                                            \
   m_uint64_t r_mask,naddr;                                                  \
   m_uint64_t data;                                                          \
   u_int m_shift;                                                            \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   naddr = vaddr & ~(0x07);                                                  \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_LDR,8,MTS_READ,&data,&exc);    \
                                                                             \
   if (likely(haddr != NULL))                                                \
      data = vmtoh64(*(m_uint64_t *)haddr);                                  \
                                                                             \
   if (likely(!exc)) {                                                       \
      m_shift = ((vaddr & 0x07) + 1) << 3;                                   \
      r_mask = (1ULL << m_shift) - 1;                                        \
      data >>= (64 - m_shift);                                               \
                                                                             \
      cpu->gpr[reg] &= ~r_mask;                                              \
      cpu->gpr[reg] |= data;                                                 \
   }                                                                         \
   return(exc);                                                              \
}

/* SWL: Store Word Left */
#define MTS_SWL(X) \
fastcall u_int mts##X##_swl(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)      \
{                                                                            \
   m_uint64_t d_mask,naddr;                                                  \
   m_uint64_t data;                                                          \
   u_int r_shift;                                                            \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   naddr = vaddr & ~(0x03ULL);                                               \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_SWL,4,MTS_READ,&data,&exc);    \
   if (unlikely(exc)) return(exc);                                           \
                                                                             \
   if (likely(haddr != NULL))                                                \
      data = vmtoh32(*(m_uint32_t *)haddr);                                  \
                                                                             \
   r_shift = (vaddr & 0x03) << 3;                                            \
   d_mask = 0xffffffff >> r_shift;                                           \
                                                                             \
   data &= ~d_mask;                                                          \
   data |= (cpu->gpr[reg] & 0xffffffff) >> r_shift;                          \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_SWL,4,MTS_WRITE,&data,&exc);   \
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);          \
   return(exc);                                                              \
}

/* SWR: Store Word Right */
#define MTS_SWR(X) \
fastcall u_int mts##X##_swr(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)      \
{                                                                            \
   m_uint64_t d_mask,naddr;                                                  \
   m_uint64_t data;                                                          \
   u_int r_shift;                                                            \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   naddr = vaddr & ~(0x03);                                                  \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_SWR,4,MTS_READ,&data,&exc);    \
   if (unlikely(exc)) return(exc);                                           \
                                                                             \
   if (likely(haddr != NULL))                                                \
      data = vmtoh32(*(m_uint32_t *)haddr);                                  \
                                                                             \
   r_shift = ((vaddr & 0x03) + 1) << 3;                                      \
   d_mask = 0xffffffff >> r_shift;                                           \
                                                                             \
   data &= d_mask;                                                           \
   data |= (cpu->gpr[reg] << (32 - r_shift)) & 0xffffffff;                   \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_SWR,4,MTS_WRITE,&data,&exc);   \
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);          \
   return(exc);                                                              \
}

/* SDL: Store Double-Word Left */
#define MTS_SDL(X) \
fastcall u_int mts##X##_sdl(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)      \
{                                                                            \
   m_uint64_t d_mask,naddr;                                                  \
   m_uint64_t data;                                                          \
   u_int r_shift;                                                            \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   naddr = vaddr & ~(0x07);                                                  \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_SDL,8,MTS_READ,&data,&exc);    \
   if (unlikely(exc)) return(exc);                                           \
                                                                             \
   if (likely(haddr != NULL))                                                \
      data = vmtoh64(*(m_uint64_t *)haddr);                                  \
                                                                             \
   r_shift = (vaddr & 0x07) << 3;                                            \
   d_mask = 0xffffffffffffffffULL >> r_shift;                                \
                                                                             \
   data &= ~d_mask;                                                          \
   data |= cpu->gpr[reg] >> r_shift;                                         \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_SDL,8,MTS_WRITE,&data,&exc);   \
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);          \
   return(exc);                                                              \
}

/* SDR: Store Double-Word Right */
#define MTS_SDR(X) \
fastcall u_int mts##X##_sdr(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)      \
{                                                                            \
   m_uint64_t d_mask,naddr;                                                  \
   m_uint64_t data;                                                          \
   u_int r_shift;                                                            \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   naddr = vaddr & ~(0x07);                                                  \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_SDR,8,MTS_READ,&data,&exc);    \
   if (unlikely(exc)) return(exc);                                           \
                                                                             \
   if (likely(haddr != NULL))                                                \
      data = vmtoh64(*(m_uint64_t *)haddr);                                  \
                                                                             \
   r_shift = ((vaddr & 0x07) + 1) << 3;                                      \
   d_mask = 0xffffffffffffffffULL >> r_shift;                                \
                                                                             \
   data &= d_mask;                                                           \
   data |= cpu->gpr[reg] << (64 - r_shift);                                  \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,naddr,MIPS_MEMOP_SDR,8,MTS_WRITE,&data,&exc);   \
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);          \
   return(exc);                                                              \
}

/* LL: Load Linked */
#define MTS_LL(X) \
fastcall u_int mts##X##_ll(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)       \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_LL,4,MTS_READ,&data,&exc);     \
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);          \
                                                                             \
   if (likely(!exc)) {                                                       \
      cpu->gpr[reg] = sign_extend(data,32);                                  \
      cpu->ll_bit = 1;                                                       \
   }                                                                         \
                                                                             \
   return(exc);                                                              \
}

/* SC: Store Conditional */
#define MTS_SC(X) \
fastcall u_int mts##X##_sc(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)       \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc = 0;                                                            \
                                                                             \
   if (cpu->ll_bit) {                                                        \
      data = cpu->gpr[reg] & 0xffffffff;                                     \
      haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_SC,4,MTS_WRITE,&data,&exc); \
      if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);       \
   }                                                                         \
                                                                             \
   if (likely(!exc))                                                         \
      cpu->gpr[reg] = cpu->ll_bit;                                           \
   return(exc);                                                              \
}

/* SDC1: Store Double-Word from Coprocessor 1 */
#define MTS_SDC1(X) \
fastcall u_int mts##X##_sdc1(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)     \
{                                                                            \
   m_uint64_t data;                                                          \
   void *haddr;                                                              \
   u_int exc;                                                                \
                                                                             \
   data = cpu->fpu.reg[reg];                                                 \
   haddr = MTS_ACCESS(X)(cpu,vaddr,MIPS_MEMOP_SDC1,8,MTS_WRITE,&data,&exc);  \
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);          \
   return(exc);                                                              \
}

/* CACHE: Cache operation */
fastcall u_int mts_cache(cpu_mips_t *cpu,m_uint64_t vaddr,u_int op)
{
   struct insn_block *block;
   m_uint32_t phys_page;

#if DEBUG_CACHE
   cpu_log(cpu,"MTS","CACHE: PC=0x%llx, vaddr=0x%llx, cache=%u, code=%u\n",
           cpu->pc, vaddr, op & 0x3, op >> 2);
#endif

   if (!cpu->translate(cpu,vaddr,&phys_page)) {
      if ((phys_page < 1048576) && cpu->exec_phys_map) {
         block = cpu->exec_phys_map[phys_page];

         if (block) {
            if ((cpu->pc < block->start_pc) || 
                ((cpu->pc - block->start_pc) >= MIPS_MIN_PAGE_SIZE))
            {
#if DEBUG_CACHE
               cpu_log(cpu,"MTS",
                       "CACHE: removing compiled page at 0x%llx, pc=0x%llx\n",
                       block->start_pc,cpu->pc);
#endif
               cpu->exec_phys_map[phys_page] = NULL;
               insn_block_free(cpu,block,TRUE);
            }
            else
            {
#if DEBUG_CACHE
               cpu_log(cpu,"MTS",
                       "CACHE: trying to remove page 0x%llx with pc=0x%llx\n",
                       block->start_pc,cpu->pc);
#endif
            }
         }
      }
   }

   return(0);
}

/* 
 * "Instanciation" of MIPS Memory Operations.
 */
MTS_MEMOP(LB)
MTS_MEMOP(LBU)
MTS_MEMOP(LH)
MTS_MEMOP(LHU)
MTS_MEMOP(LW)
MTS_MEMOP(LWU)
MTS_MEMOP(LD)
MTS_MEMOP(SB)
MTS_MEMOP(SH)
MTS_MEMOP(SW)
MTS_MEMOP(SD)
MTS_MEMOP(LDC1)
MTS_MEMOP(LWL)
MTS_MEMOP(LWR)
MTS_MEMOP(LDL)
MTS_MEMOP(LDR)
MTS_MEMOP(SWL)
MTS_MEMOP(SWR)
MTS_MEMOP(SDL)
MTS_MEMOP(SDR)
MTS_MEMOP(LL)
MTS_MEMOP(SC)
MTS_MEMOP(SDC1)

/* Fast assembly routines */
#ifdef FAST_ASM
extern fastcall u_int mts32_lw_asm(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg);
#endif

/* === MTS32 Cache Management ============================================= */

/* MTS32 map/unmap/rebuild "API" functions */
void mts32_api_map(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint64_t paddr,
                   m_uint32_t len,int cache_access,int tlb_index)
{
   mts32_map(cpu,vaddr,paddr,len,cache_access);
}

void mts32_api_unmap(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint32_t len,
                     m_uint32_t val,int tlb_index)
{
   mts32_unmap(cpu,vaddr,len,val);             
}

void mts32_api_rebuild(cpu_mips_t *cpu)
{
   u_int cpu_mode;

   cpu_mode = cp0_get_mode(cpu);

   /* The complete address space gives AE (address error) */
   if (mts32_init_empty(cpu) == -1)
      return;

   /* USEG: 0x00000000 -> 0x80000000: 2 GB mapped (TLB) */
   mts32_unmap(cpu,MIPS_KUSEG_BASE,MIPS_KUSEG_SIZE,MTS_ACC_T);

   /* If the CPU is in Kernel Mode, activate KSEG segments */
   if (cpu_mode == MIPS_CP0_STATUS_KM) {
      /* KSEG0 / KSEG1 : physical memory */
      mts32_km_map_all_dev(cpu);
    
      /* KSSEG: 0xc0000000 -> 0xe0000000: 0.5GB mapped (TLB) */
      mts32_unmap(cpu,MIPS_KSSEG_BASE,MIPS_KSSEG_SIZE,MTS_ACC_T);

      /* KSEG3: 0xe0000000 -> 0xffffffff: 0.5GB mapped (TLB) */
      mts32_unmap(cpu,MIPS_KSEG3_BASE,MIPS_KSEG3_SIZE,MTS_ACC_T);
   } else {
      if (cpu_mode == MIPS_CP0_STATUS_SM) {
         /* SSEG: 0xc0000000 -> 0xe0000000: 0.5GB mapped (TLB) */
         mts32_unmap(cpu,MIPS_KSSEG_BASE,MIPS_KSSEG_SIZE,MTS_ACC_T);
      }
   }

   /* Map all TLB entries */
   cp0_map_all_tlb_to_mts(cpu);
}

/* === MTS64 Cache Management ============================================= */

/* MTS64 map/unmap/rebuild "API" functions */
void mts64_api_map(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint64_t paddr,
                   m_uint32_t len,int cache_access,int tlb_index)
{
   /* nothing to do, the cache will be filled on-the-fly */
}

void mts64_api_unmap(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint32_t len,
                     m_uint32_t val,int tlb_index)
{
   /* Invalidate the TLB entry or the full cache if no index is specified */
   if (tlb_index != -1)
      mts64_invalidate_tlb_entry(cpu,tlb_index);
   else
      mts64_invalidate_cache(cpu);
}

void mts64_api_rebuild(cpu_mips_t *cpu)
{
   mts64_invalidate_cache(cpu);
}

/* ======================================================================== */

/* Initialize memory 32-bit access vectors */
void mts32_init_memop_vectors(cpu_mips_t *cpu)
{
   /* XXX TODO:
    *  - LD/SD forbidden in Supervisor/User modes with 32-bit addresses.
    */

   /* API vectors */
   cpu->mts_map     = mts32_api_map;
   cpu->mts_unmap   = mts32_api_unmap;
   cpu->mts_rebuild = mts32_api_rebuild;

   /* memory lookup operation */
   cpu->mem_op_lookup = mts32_lookup;

   /* Load Operations */
   cpu->mem_op_fn[MIPS_MEMOP_LB] = mts32_lb;
   cpu->mem_op_fn[MIPS_MEMOP_LBU] = mts32_lbu;
   cpu->mem_op_fn[MIPS_MEMOP_LH] = mts32_lh;
   cpu->mem_op_fn[MIPS_MEMOP_LHU] = mts32_lhu;
   cpu->mem_op_fn[MIPS_MEMOP_LW] = mts32_lw;
   cpu->mem_op_fn[MIPS_MEMOP_LWU] = mts32_lwu;
   cpu->mem_op_fn[MIPS_MEMOP_LD] = mts32_ld;
   cpu->mem_op_fn[MIPS_MEMOP_LDL] = mts32_ldl;
   cpu->mem_op_fn[MIPS_MEMOP_LDR] = mts32_ldr;

   /* Store Operations */
   cpu->mem_op_fn[MIPS_MEMOP_SB] = mts32_sb;
   cpu->mem_op_fn[MIPS_MEMOP_SH] = mts32_sh;
   cpu->mem_op_fn[MIPS_MEMOP_SW] = mts32_sw;
   cpu->mem_op_fn[MIPS_MEMOP_SD] = mts32_sd;

   /* Load Left/Right operations */
   cpu->mem_op_fn[MIPS_MEMOP_LWL] = mts32_lwl;
   cpu->mem_op_fn[MIPS_MEMOP_LWR] = mts32_lwr;
   cpu->mem_op_fn[MIPS_MEMOP_LDL] = mts32_ldl;
   cpu->mem_op_fn[MIPS_MEMOP_LDR] = mts32_ldr;

   /* Store Left/Right operations */
   cpu->mem_op_fn[MIPS_MEMOP_SWL] = mts32_swl;
   cpu->mem_op_fn[MIPS_MEMOP_SWR] = mts32_swr;
   cpu->mem_op_fn[MIPS_MEMOP_SDL] = mts32_sdl;
   cpu->mem_op_fn[MIPS_MEMOP_SDR] = mts32_sdr;

   /* LL/SC - Load Linked / Store Conditional */
   cpu->mem_op_fn[MIPS_MEMOP_LL] = mts32_ll;
   cpu->mem_op_fn[MIPS_MEMOP_SC] = mts32_sc;

   /* Coprocessor 1 memory access functions */
   cpu->mem_op_fn[MIPS_MEMOP_LDC1] = mts32_ldc1;
   cpu->mem_op_fn[MIPS_MEMOP_SDC1] = mts32_sdc1;

   /* Cache Operation */
   cpu->mem_op_fn[MIPS_MEMOP_CACHE] = mts_cache;

#if defined(FAST_ASM) && MTSASM_ENABLE
   if (cpu->vm->jit_use)
      cpu->mem_op_fn[MIPS_MEMOP_LW] = mts32_lw_asm;
#endif
}

/* Initialize memory 64-bit access vectors */
void mts64_init_memop_vectors(cpu_mips_t *cpu)
{   
   /* API vectors */
   cpu->mts_map     = mts64_api_map;
   cpu->mts_unmap   = mts64_api_unmap;
   cpu->mts_rebuild = mts64_api_rebuild;

   /* memory lookup operation */
   cpu->mem_op_lookup = mts64_lookup;

   cpu->translate = mts64_translate;

   /* Load Operations */
   cpu->mem_op_fn[MIPS_MEMOP_LB] = mts64_lb;
   cpu->mem_op_fn[MIPS_MEMOP_LBU] = mts64_lbu;
   cpu->mem_op_fn[MIPS_MEMOP_LH] = mts64_lh;
   cpu->mem_op_fn[MIPS_MEMOP_LHU] = mts64_lhu;
   cpu->mem_op_fn[MIPS_MEMOP_LW] = mts64_lw;
   cpu->mem_op_fn[MIPS_MEMOP_LWU] = mts64_lwu;
   cpu->mem_op_fn[MIPS_MEMOP_LD] = mts64_ld;
   cpu->mem_op_fn[MIPS_MEMOP_LDL] = mts64_ldl;
   cpu->mem_op_fn[MIPS_MEMOP_LDR] = mts64_ldr;

   /* Store Operations */
   cpu->mem_op_fn[MIPS_MEMOP_SB] = mts64_sb;
   cpu->mem_op_fn[MIPS_MEMOP_SH] = mts64_sh;
   cpu->mem_op_fn[MIPS_MEMOP_SW] = mts64_sw;
   cpu->mem_op_fn[MIPS_MEMOP_SD] = mts64_sd;

   /* Load Left/Right operations */
   cpu->mem_op_fn[MIPS_MEMOP_LWL] = mts64_lwl;
   cpu->mem_op_fn[MIPS_MEMOP_LWR] = mts64_lwr;
   cpu->mem_op_fn[MIPS_MEMOP_LDL] = mts64_ldl;
   cpu->mem_op_fn[MIPS_MEMOP_LDR] = mts64_ldr;

   /* Store Left/Right operations */
   cpu->mem_op_fn[MIPS_MEMOP_SWL] = mts64_swl;
   cpu->mem_op_fn[MIPS_MEMOP_SWR] = mts64_swr;
   cpu->mem_op_fn[MIPS_MEMOP_SDL] = mts64_sdl;
   cpu->mem_op_fn[MIPS_MEMOP_SDR] = mts64_sdr;

   /* LL/SC - Load Linked / Store Conditional */
   cpu->mem_op_fn[MIPS_MEMOP_LL] = mts64_ll;
   cpu->mem_op_fn[MIPS_MEMOP_SC] = mts64_sc;

   /* Coprocessor 1 memory access functions */
   cpu->mem_op_fn[MIPS_MEMOP_LDC1] = mts64_ldc1;
   cpu->mem_op_fn[MIPS_MEMOP_SDC1] = mts64_sdc1;

   /* Cache Operation */
   cpu->mem_op_fn[MIPS_MEMOP_CACHE] = mts_cache;
}

/* Initialize memory access vectors */
void mts_init_memop_vectors(cpu_mips_t *cpu)
{
   /* TEST */
   mts64_init_memop_vectors(cpu);
}

/* Shutdown MTS subsystem */
void mts_shutdown(cpu_mips_t *cpu)
{
   mts32_shutdown(cpu);
   mts64_shutdown(cpu);
}

/* Copy a memory block from VM physical RAM to real host */
void physmem_copy_from_vm(vm_instance_t *vm,void *real_buffer,
                          m_uint64_t paddr,size_t len)
{
   struct vdevice *vm_ram;
   u_char *ptr;

   if ((vm_ram = dev_lookup(vm,paddr,FALSE)) != NULL) {
      assert(vm_ram->host_addr != 0);
      ptr = (u_char *)vm_ram->host_addr + (paddr - vm_ram->phys_addr);
      memcpy(real_buffer,ptr,len);
   }
}

/* Copy a memory block to VM physical RAM from real host */
void physmem_copy_to_vm(vm_instance_t *vm,void *real_buffer,
                        m_uint64_t paddr,size_t len)
{
   struct vdevice *vm_ram;
   u_char *ptr;

   if ((vm_ram = dev_lookup(vm,paddr,FALSE)) != NULL) {
      assert(vm_ram->host_addr != 0);
      ptr = (u_char *)vm_ram->host_addr + (paddr - vm_ram->phys_addr);
      memcpy(ptr,real_buffer,len);
   }
}

/* Copy a 32-bit word from the VM physical RAM to real host */
m_uint32_t physmem_copy_u32_from_vm(vm_instance_t *vm,m_uint64_t paddr)
{
   struct vdevice *dev;
   m_uint32_t offset;
   m_uint64_t tmp;
   void *ptr;

   if (unlikely((dev = dev_lookup(vm,paddr,FALSE)) == NULL))
      return(0);

   offset = paddr - dev->phys_addr;

   if ((dev->host_addr != 0) && !(dev->flags & VDEVICE_FLAG_NO_MTS_MMAP))
      ptr = (u_char *)dev->host_addr + offset;
   else {
      ptr = dev->handler(vm->boot_cpu,dev,offset,4,MTS_READ,&tmp);
      if (!ptr) return(tmp);
   }
   
   return(vmtoh32(*(m_uint32_t *)ptr));
}

/* Copy a 32-bit word to the VM physical RAM from real host */
void physmem_copy_u32_to_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t val)
{
   struct vdevice *dev;
   m_uint32_t offset;
   m_uint64_t tmp;
   void *ptr;

   if (unlikely((dev = dev_lookup(vm,paddr,FALSE)) == NULL))
      return;

   offset = paddr - dev->phys_addr;

   if ((dev->host_addr != 0) && !(dev->flags & VDEVICE_FLAG_NO_MTS_MMAP))
      ptr = (u_char *)dev->host_addr + offset;
   else {
      tmp = val;
      ptr = dev->handler(vm->boot_cpu,dev,offset,4,MTS_WRITE,&tmp);
      if (!ptr) return;
   }
   
   *(m_uint32_t *)ptr = htovm32(val);
}

/* Copy a 16-bit word from the VM physical RAM to real host */
m_uint16_t physmem_copy_u16_from_vm(vm_instance_t *vm,m_uint64_t paddr)
{
   struct vdevice *dev;
   m_uint32_t offset;
   m_uint64_t tmp;
   void *ptr;

   if (unlikely((dev = dev_lookup(vm,paddr,FALSE)) == NULL))
      return(0);

   offset = paddr - dev->phys_addr;

   if ((dev->host_addr != 0) && !(dev->flags & VDEVICE_FLAG_NO_MTS_MMAP))
      ptr = (u_char *)dev->host_addr + offset;
   else {
      ptr = dev->handler(vm->boot_cpu,dev,offset,2,MTS_READ,&tmp);
      if (!ptr) return(tmp);
   }
   
   return(vmtoh16(*(m_uint16_t *)ptr));
}

/* Copy a 16-bit word to the VM physical RAM from real host */
void physmem_copy_u16_to_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint16_t val)
{
   struct vdevice *dev;
   m_uint32_t offset;
   m_uint64_t tmp;
   void *ptr;

   if (unlikely((dev = dev_lookup(vm,paddr,FALSE)) == NULL))
      return;

   offset = paddr - dev->phys_addr;

   if ((dev->host_addr != 0) && !(dev->flags & VDEVICE_FLAG_NO_MTS_MMAP))
      ptr = (u_char *)dev->host_addr + offset;
   else {
      tmp = val;
      ptr = dev->handler(vm->boot_cpu,dev,offset,2,MTS_WRITE,&tmp);
      if (!ptr) return;
   }
   
   *(m_uint16_t *)ptr = htovm16(val);
}

/* DMA transfer operation */
void physmem_dma_transfer(vm_instance_t *vm,m_uint64_t src,m_uint64_t dst,
                          size_t len)
{
   struct vdevice *src_dev,*dst_dev;
   u_char *sptr,*dptr;

   src_dev = dev_lookup(vm,src,FALSE);
   dst_dev = dev_lookup(vm,dst,FALSE);

   if ((src_dev != NULL) && (dst_dev != NULL)) {
      assert(src_dev->host_addr != 0);
      assert(dst_dev->host_addr != 0);
      
      sptr = (u_char *)src_dev->host_addr + (src - src_dev->phys_addr);
      dptr = (u_char *)dst_dev->host_addr + (dst - dst_dev->phys_addr);
      memcpy(dptr,sptr,len);
   } else {
      vm_log(vm,"DMA","unable to transfer from 0x%llx to 0x%llx (len=%lu)\n",
             src,dst,(u_long)len);
   }
}

/* strlen in VM physical memory */
size_t physmem_strlen(vm_instance_t *vm,m_uint64_t paddr)
{
   struct vdevice *vm_ram;
   size_t len = 0;
   char *ptr;
   
   if ((vm_ram = dev_lookup(vm,paddr,TRUE)) != NULL) {
      ptr = (char *)vm_ram->host_addr + (paddr - vm_ram->phys_addr);
      len = strlen(ptr);
   }

   return(len);
}

/* Physical memory dump (32-bit words) */
void physmem_dump_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t u32_count)
{
   m_uint32_t i;

   for(i=0;i<u32_count;i++) {
      vm_log(vm,"physmem_dump","0x%8.8llx: 0x%8.8x\n",
             paddr+(i<<2),physmem_copy_u32_from_vm(vm,paddr+(i<<2)));
   }
}
