/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Template code for MTS.
 */

#define MTS_ENTRY  MTS_PROTO(entry_t)
#define MTS_CHUNK  MTS_PROTO(chunk_t)

/* Forward declarations */
static forced_inline void *MTS_PROTO(access)(cpu_mips_t *cpu,m_uint64_t vaddr,
                                             u_int op_code,u_int op_size,
                                             u_int op_type,m_uint64_t *data,
                                             u_int *exc);

static fastcall int MTS_PROTO(translate)(cpu_mips_t *cpu,m_uint64_t vaddr,
                                         m_uint32_t *phys_page);

/* Initialize the MTS subsystem for the specified CPU */
int MTS_PROTO(init)(cpu_mips_t *cpu)
{
   size_t len;

   /* Initialize the cache entries to 0 (empty) */
   len = MTS_PROTO_UP(HASH_SIZE) * sizeof(MTS_ENTRY *);
   if (!(cpu->mts_cache = malloc(len)))
      return(-1);

   memset(cpu->mts_cache,0,len);
   cpu->mts_lookups = 0;
   cpu->mts_misses  = 0;

   /* Reset the TLB reverse map (used for selective invalidations) */
   memset(cpu->mts_rmap,0,(cpu->cp0.tlb_entries * sizeof(MTS_ENTRY *)));
   return(0);
}

/* Free memory used by MTS */
void MTS_PROTO(shutdown)(cpu_mips_t *cpu)
{
   MTS_CHUNK *chunk,*next;
   int i;

   /* Reset the reverse map */
   for(i=0;i<cpu->cp0.tlb_entries;i++)
      cpu->mts_rmap[i] = NULL;

   /* Free the cache itself */
   free(cpu->mts_cache);
   cpu->mts_cache = NULL;

   /* Free the chunks */
   for(chunk=cpu->mts_chunk_list;chunk;chunk=next) {
      next = chunk->next;
      free(chunk);
   }

   for(chunk=cpu->mts_chunk_free_list;chunk;chunk=next) {
      next = chunk->next;
      free(chunk);
   }
   
   cpu->mts_chunk_list = cpu->mts_chunk_free_list = NULL;
   cpu->mts_entry_free_list = NULL;
}

/* Show MTS detailed information (debugging only!) */
void MTS_PROTO(show_stats)(cpu_mips_t *cpu)
{
   MTS_CHUNK *chunk;
#if DEBUG_MTS_MAP_VIRT
   MTS_ENTRY *entry;
   u_int i;
#endif
   u_int count;

   printf("\nCPU%u: MTS%d statistics:\n",cpu->id,MTS_ADDR_SIZE);

   printf("   Total lookups: %llu, misses: %llu, efficiency: %g%%\n",
          cpu->mts_lookups, cpu->mts_misses,
          100 - ((double)(cpu->mts_misses*100)/
                 (double)cpu->mts_lookups));

#if DEBUG_MTS_MAP_VIRT
   /* Valid hash entries */
   for(count=0,i=0;i<MTS_PROTO_UP(HASH_SIZE);i++) {
      if ((entry = cpu->mts_cache[i]) != NULL) {
         printf("    %4u: entry=%p, start=0x%16.16llx, "
                "len=0x%8.8x, action=0x%8.8llx\n",
                i,entry,(m_uint64_t)entry->start,entry->mask,
                (m_uint64_t)entry->action);
         count++;
      }
   }

   printf("   %u/%u valid hash entries.\n",count,MTS_PROTO_UP(HASH_SIZE));
#endif

   /* Number of chunks */
   for(count=0,chunk=cpu->mts_chunk_list;chunk;chunk=chunk->next)
      count++;

   printf("   Number of chunks: %u\n",count);

#if DEBUG_MTS_MAP_VIRT
   /* Reverse map */
   for(i=0;i<MIPS64_TLB_MAX_ENTRIES;i++) {
      for(count=0,entry=cpu->mts_rmap[i];entry;entry=entry->next)
         count++;

      if (count > 0)
         printf("   tlb_rmap[%u]: %u entries\n",i,count);
   }
#endif
}

/* Allocate a new chunk */
static int MTS_PROTO(alloc_chunk)(cpu_mips_t *cpu)
{
   MTS_CHUNK *chunk;

   /* Try the free list first, then use standard allocation procedure */
   if ((chunk = cpu->mts_chunk_free_list) != NULL) {
      cpu->mts_chunk_free_list = chunk->next;
   } else {
      if (!(chunk = malloc(sizeof(*chunk))))
         return(-1);
   }

   chunk->count = 0;
   chunk->next = cpu->mts_chunk_list;
   cpu->mts_chunk_list = chunk;
   return(0);
}

/* Allocate a new entry */
static MTS_ENTRY *MTS_PROTO(alloc_entry)(cpu_mips_t *cpu)
{
   MTS_CHUNK *chunk = cpu->mts_chunk_list;
   MTS_ENTRY *entry;

   /* First, try to allocate the entry from the free list */
   if ((entry = cpu->mts_entry_free_list) != NULL) {
      cpu->mts_entry_free_list = ((MTS_ENTRY *)cpu->mts_entry_free_list)->next;
      return entry;
   }

   /* A new chunk is required */
   if (!chunk || (chunk->count == MTS_PROTO_UP(CHUNK_SIZE))) {
      if (MTS_PROTO(alloc_chunk)(cpu) == -1)
         return NULL;

      chunk = cpu->mts_chunk_list;
   }

   entry = &chunk->entry[chunk->count];
   chunk->count++;
   return entry;
}

/* Invalidate the complete MTS cache */
void MTS_PROTO(invalidate_cache)(cpu_mips_t *cpu)
{
   MTS_CHUNK *chunk;
   size_t len;
   u_int i;

   len = MTS_PROTO_UP(HASH_SIZE) * sizeof(MTS_ENTRY *);
   memset(cpu->mts_cache,0,len);
 
   /* Move all chunks to the free list */
   while((chunk = cpu->mts_chunk_list) != NULL) {
      cpu->mts_chunk_list = chunk->next;
      chunk->next = cpu->mts_chunk_free_list;
      cpu->mts_chunk_free_list = chunk;
   }

   /* Reset the free list of entries (since they are located in chunks) */
   cpu->mts_entry_free_list = NULL;

   /* Reset the reverse map */
   for(i=0;i<cpu->cp0.tlb_entries;i++)
      cpu->mts_rmap[i] = NULL;
}

/* Invalidate partially the MTS cache, given a TLB entry index */
void MTS_PROTO(invalidate_tlb_entry)(cpu_mips_t *cpu,u_int tlb_index)
{
   MTS_ENTRY *entry;

   for(entry=cpu->mts_rmap[tlb_index];entry;entry=entry->next) {
      *(entry->pself) = NULL;
      if (!entry->next) {
         entry->next = cpu->mts_entry_free_list;
         break;
      }
   }

   cpu->mts_entry_free_list = cpu->mts_rmap[tlb_index];
   cpu->mts_rmap[tlb_index] = NULL;
} 

/* 
 * MTS mapping.
 *
 * It is NOT inlined since it triggers a GCC bug on my config (x86, GCC 3.3.5)
 */
static no_inline int MTS_PROTO(map)(cpu_mips_t *cpu,m_uint64_t vaddr,
                                    mts_map_t *map,MTS_ENTRY *entry)
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

/* MTS lookup */
static void *MTS_PROTO(lookup)(cpu_mips_t *cpu,m_uint64_t vaddr)
{
   m_uint64_t data;
   u_int exc;
   return(MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LOOKUP,4,MTS_READ,
                            &data,&exc));
}

/* === MIPS Memory Operations ============================================= */

/* LB: Load Byte */
fastcall u_int MTS_PROTO(lb)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LB,1,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = *(m_uint8_t *)haddr;
   if (likely(!exc)) cpu->gpr[reg] = sign_extend(data,8);
   return(exc);
}

/* LBU: Load Byte Unsigned */
fastcall u_int MTS_PROTO(lbu)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LBU,1,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = *(m_uint8_t *)haddr;
   if (likely(!exc)) cpu->gpr[reg] = data & 0xff;
   return(exc);
}

/* LH: Load Half-Word */
fastcall u_int MTS_PROTO(lh)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LH,2,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh16(*(m_uint16_t *)haddr);
   if (likely(!exc)) cpu->gpr[reg] = sign_extend(data,16);
   return(exc);
}

/* LHU: Load Half-Word Unsigned */
fastcall u_int MTS_PROTO(lhu)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LHU,2,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh16(*(m_uint16_t *)haddr);
   if (likely(!exc)) cpu->gpr[reg] = data & 0xffff;
   return(exc);
}

/* LW: Load Word */
fastcall u_int MTS_PROTO(lw)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LW,4,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);
   if (likely(!exc)) cpu->gpr[reg] = sign_extend(data,32);
   return(exc);
}

/* LWU: Load Word Unsigned */
fastcall u_int MTS_PROTO(lwu)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LWU,4,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);
   if (likely(!exc)) cpu->gpr[reg] = data & 0xffffffff;
   return(exc);
}

/* LD: Load Double-Word */
fastcall u_int MTS_PROTO(ld)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LD,8,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);
   if (likely(!exc)) cpu->gpr[reg] = data;
   return(exc);
}

/* SB: Store Byte */
fastcall u_int MTS_PROTO(sb)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   data = cpu->gpr[reg] & 0xff;
   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SB,1,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint8_t *)haddr = data;
   return(exc);
}

/* SH: Store Half-Word */
fastcall u_int MTS_PROTO(sh)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   data = cpu->gpr[reg] & 0xffff;
   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SH,2,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint16_t *)haddr = htovm16(data);
   return(exc);
}

/* SW: Store Word */
fastcall u_int MTS_PROTO(sw)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   data = cpu->gpr[reg] & 0xffffffff;
   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SW,4,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
   return(exc);
}

/* SD: Store Double-Word */
fastcall u_int MTS_PROTO(sd)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   data = cpu->gpr[reg];
   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SD,8,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
   return(exc);
}

/* LDC1: Load Double-Word To Coprocessor 1 */
fastcall u_int MTS_PROTO(ldc1)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LDC1,8,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);
   if (likely(!exc)) cpu->fpu.reg[reg] = data;
   return(exc);
}

/* LWL: Load Word Left */
fastcall u_int MTS_PROTO(lwl)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x03);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_LWL,4,MTS_READ,&data,&exc);

   if (likely(haddr != NULL))
      data = vmtoh32(*(m_uint32_t *)haddr);

   if (likely(!exc)) {
      m_shift = (vaddr & 0x03) << 3;
      r_mask = (1ULL << m_shift) - 1;
      data <<= m_shift;

      cpu->gpr[reg] &= r_mask;
      cpu->gpr[reg] |= data;
      cpu->gpr[reg] = sign_extend(cpu->gpr[reg],32);
   }
   return(exc);
}

/* LWR: Load Word Right */
fastcall u_int MTS_PROTO(lwr)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x03);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_LWR,4,MTS_READ,&data,&exc);

   if (likely(haddr != NULL))
      data = vmtoh32(*(m_uint32_t *)haddr);

   if (likely(!exc)) {
      m_shift = ((vaddr & 0x03) + 1) << 3;
      r_mask = (1ULL << m_shift) - 1;

      data = sign_extend(data >> (32 - m_shift),32);
      r_mask = sign_extend(r_mask,32);

      cpu->gpr[reg] &= ~r_mask;
      cpu->gpr[reg] |= data;
   }
   return(exc);
}

/* LDL: Load Double-Word Left */
fastcall u_int MTS_PROTO(ldl)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x07);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_LDL,8,MTS_READ,&data,&exc);

   if (likely(haddr != NULL))
      data = vmtoh64(*(m_uint64_t *)haddr);

   if (likely(!exc)) {
      m_shift = (vaddr & 0x07) << 3;
      r_mask = (1ULL << m_shift) - 1;
      data <<= m_shift;

      cpu->gpr[reg] &= r_mask;
      cpu->gpr[reg] |= data;
   }
   return(exc);
}

/* LDR: Load Double-Word Right */
fastcall u_int MTS_PROTO(ldr)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x07);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_LDR,8,MTS_READ,&data,&exc);

   if (likely(haddr != NULL))
      data = vmtoh64(*(m_uint64_t *)haddr);

   if (likely(!exc)) {
      m_shift = ((vaddr & 0x07) + 1) << 3;
      r_mask = (1ULL << m_shift) - 1;
      data >>= (64 - m_shift);

      cpu->gpr[reg] &= ~r_mask;
      cpu->gpr[reg] |= data;
   }
   return(exc);
}

/* SWL: Store Word Left */
fastcall u_int MTS_PROTO(swl)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x03ULL);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SWL,4,MTS_READ,&data,&exc);
   if (unlikely(exc)) return(exc);

   if (likely(haddr != NULL))
      data = vmtoh32(*(m_uint32_t *)haddr);

   r_shift = (vaddr & 0x03) << 3;
   d_mask = 0xffffffff >> r_shift;

   data &= ~d_mask;
   data |= (cpu->gpr[reg] & 0xffffffff) >> r_shift;

   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SWL,4,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
   return(exc);
}

/* SWR: Store Word Right */
fastcall u_int MTS_PROTO(swr)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x03);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SWR,4,MTS_READ,&data,&exc);
   if (unlikely(exc)) return(exc);

   if (likely(haddr != NULL))
      data = vmtoh32(*(m_uint32_t *)haddr);

   r_shift = ((vaddr & 0x03) + 1) << 3;
   d_mask = 0xffffffff >> r_shift;

   data &= d_mask;
   data |= (cpu->gpr[reg] << (32 - r_shift)) & 0xffffffff;

   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SWR,4,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
   return(exc);
}

/* SDL: Store Double-Word Left */
fastcall u_int MTS_PROTO(sdl)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x07);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SDL,8,MTS_READ,&data,&exc);
   if (unlikely(exc)) return(exc);

   if (likely(haddr != NULL))
      data = vmtoh64(*(m_uint64_t *)haddr);

   r_shift = (vaddr & 0x07) << 3;
   d_mask = 0xffffffffffffffffULL >> r_shift;

   data &= ~d_mask;
   data |= cpu->gpr[reg] >> r_shift;

   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SDL,8,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
   return(exc);
}

/* SDR: Store Double-Word Right */
fastcall u_int MTS_PROTO(sdr)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x07);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SDR,8,MTS_READ,&data,&exc);
   if (unlikely(exc)) return(exc);

   if (likely(haddr != NULL))
      data = vmtoh64(*(m_uint64_t *)haddr);

   r_shift = ((vaddr & 0x07) + 1) << 3;
   d_mask = 0xffffffffffffffffULL >> r_shift;

   data &= d_mask;
   data |= cpu->gpr[reg] << (64 - r_shift);

   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SDR,8,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
   return(exc);
}

/* LL: Load Linked */
fastcall u_int MTS_PROTO(ll)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LL,4,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);

   if (likely(!exc)) {
      cpu->gpr[reg] = sign_extend(data,32);
      cpu->ll_bit = 1;
   }

   return(exc);
}

/* SC: Store Conditional */
fastcall u_int MTS_PROTO(sc)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc = 0;

   if (cpu->ll_bit) {
      data = cpu->gpr[reg] & 0xffffffff;
      haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SC,4,MTS_WRITE,
                                &data,&exc);
      if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
   }

   if (likely(!exc))
      cpu->gpr[reg] = cpu->ll_bit;
   return(exc);
}

/* SDC1: Store Double-Word from Coprocessor 1 */
fastcall u_int MTS_PROTO(sdc1)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   data = cpu->fpu.reg[reg];
   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SDC1,8,MTS_WRITE,
                             &data,&exc);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
   return(exc);
}

/* CACHE: Cache operation */
fastcall u_int MTS_PROTO(cache)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int op)
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

/* === MTS Cache Management ============================================= */

/* MTS map/unmap/rebuild "API" functions */
void MTS_PROTO(api_map)(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint64_t paddr,
                        m_uint32_t len,int cache_access,int tlb_index)
{
   /* nothing to do, the cache will be filled on-the-fly */
}

void MTS_PROTO(api_unmap)(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint32_t len,
                          m_uint32_t val,int tlb_index)
{
   /* Invalidate the TLB entry or the full cache if no index is specified */
   if (tlb_index != -1)
      MTS_PROTO(invalidate_tlb_entry)(cpu,tlb_index);
   else
      MTS_PROTO(invalidate_cache)(cpu);
}

void MTS_PROTO(api_rebuild)(cpu_mips_t *cpu)
{
   MTS_PROTO(invalidate_cache)(cpu);
}

/* ======================================================================== */

/* Initialize memory access vectors */
void MTS_PROTO(init_memop_vectors)(cpu_mips_t *cpu)
{
   /* XXX TODO:
    *  - LD/SD forbidden in Supervisor/User modes with 32-bit addresses.
    */

   cpu->addr_mode = MTS_ADDR_SIZE;

   /* API vectors */
   cpu->mts_map     = MTS_PROTO(api_map);
   cpu->mts_unmap   = MTS_PROTO(api_unmap);
   cpu->mts_rebuild = MTS_PROTO(api_rebuild);

   /* memory lookup operation */
   cpu->mem_op_lookup = MTS_PROTO(lookup);

   /* Translation operation */
   cpu->translate = MTS_PROTO(translate);

   /* Shutdown operation */
   cpu->mts_shutdown = MTS_PROTO(shutdown);

   /* Show statistics */
   cpu->mts_show_stats = MTS_PROTO(show_stats);

   /* Load Operations */
   cpu->mem_op_fn[MIPS_MEMOP_LB] = MTS_PROTO(lb);
   cpu->mem_op_fn[MIPS_MEMOP_LBU] = MTS_PROTO(lbu);
   cpu->mem_op_fn[MIPS_MEMOP_LH] = MTS_PROTO(lh);
   cpu->mem_op_fn[MIPS_MEMOP_LHU] = MTS_PROTO(lhu);
   cpu->mem_op_fn[MIPS_MEMOP_LW] = MTS_PROTO(lw);
   cpu->mem_op_fn[MIPS_MEMOP_LWU] = MTS_PROTO(lwu);
   cpu->mem_op_fn[MIPS_MEMOP_LD] = MTS_PROTO(ld);
   cpu->mem_op_fn[MIPS_MEMOP_LDL] = MTS_PROTO(ldl);
   cpu->mem_op_fn[MIPS_MEMOP_LDR] = MTS_PROTO(ldr);

   /* Store Operations */
   cpu->mem_op_fn[MIPS_MEMOP_SB] = MTS_PROTO(sb);
   cpu->mem_op_fn[MIPS_MEMOP_SH] = MTS_PROTO(sh);
   cpu->mem_op_fn[MIPS_MEMOP_SW] = MTS_PROTO(sw);
   cpu->mem_op_fn[MIPS_MEMOP_SD] = MTS_PROTO(sd);

   /* Load Left/Right operations */
   cpu->mem_op_fn[MIPS_MEMOP_LWL] = MTS_PROTO(lwl);
   cpu->mem_op_fn[MIPS_MEMOP_LWR] = MTS_PROTO(lwr);
   cpu->mem_op_fn[MIPS_MEMOP_LDL] = MTS_PROTO(ldl);
   cpu->mem_op_fn[MIPS_MEMOP_LDR] = MTS_PROTO(ldr);

   /* Store Left/Right operations */
   cpu->mem_op_fn[MIPS_MEMOP_SWL] = MTS_PROTO(swl);
   cpu->mem_op_fn[MIPS_MEMOP_SWR] = MTS_PROTO(swr);
   cpu->mem_op_fn[MIPS_MEMOP_SDL] = MTS_PROTO(sdl);
   cpu->mem_op_fn[MIPS_MEMOP_SDR] = MTS_PROTO(sdr);

   /* LL/SC - Load Linked / Store Conditional */
   cpu->mem_op_fn[MIPS_MEMOP_LL] = MTS_PROTO(ll);
   cpu->mem_op_fn[MIPS_MEMOP_SC] = MTS_PROTO(sc);

   /* Coprocessor 1 memory access functions */
   cpu->mem_op_fn[MIPS_MEMOP_LDC1] = MTS_PROTO(ldc1);
   cpu->mem_op_fn[MIPS_MEMOP_SDC1] = MTS_PROTO(sdc1);

   /* Cache Operation */
   cpu->mem_op_fn[MIPS_MEMOP_CACHE] = MTS_PROTO(cache);
}

#undef MTS_ADDR_SIZE
#undef MTS_PROTO
#undef MTS_PROTO_UP
#undef MTS_ENTRY
#undef MTS_CHUNK
