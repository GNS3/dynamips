/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Template code for MTS.
 */

#define MTS_ENTRY  MTS_NAME(entry_t)
#define MTS_CACHE(cpu)  ( cpu->mts_u. MTS_NAME(cache) )

/* Forward declarations */
static forced_inline void *MTS_PROTO(access)(cpu_mips_t *cpu,m_uint64_t vaddr,
                                             u_int op_code,u_int op_size,
                                             u_int op_type,m_uint64_t *data);

static fastcall int MTS_PROTO(translate)(cpu_mips_t *cpu,m_uint64_t vaddr,
                                         m_uint32_t *phys_page);

/* Initialize the MTS subsystem for the specified CPU */
int MTS_PROTO(init)(cpu_mips_t *cpu)
{
   size_t len;

   /* Initialize the cache entries to 0 (empty) */
   len = MTS_NAME_UP(HASH_SIZE) * sizeof(MTS_ENTRY);
   if (!(MTS_CACHE(cpu) = malloc(len)))
      return(-1);

   memset(MTS_CACHE(cpu),0xFF,len);
   cpu->mts_lookups = 0;
   cpu->mts_misses  = 0;
   return(0);
}

/* Free memory used by MTS */
void MTS_PROTO(shutdown)(cpu_mips_t *cpu)
{
   /* Free the cache itself */
   free(MTS_CACHE(cpu));
   MTS_CACHE(cpu) = NULL;
}

/* Show MTS detailed information (debugging only!) */
void MTS_PROTO(show_stats)(cpu_gen_t *gen_cpu)
{
   cpu_mips_t *cpu = CPU_MIPS64(gen_cpu);
#if DEBUG_MTS_MAP_VIRT
   MTS_ENTRY *entry;
   u_int i,count;
#endif

   printf("\nCPU%u: MTS%d statistics:\n",cpu->gen->id,MTS_ADDR_SIZE);

#if DEBUG_MTS_MAP_VIRT
   /* Valid hash entries */
   for(count=0,i=0;i<MTS_NAME_UP(HASH_SIZE);i++) {
      entry = &(MTS_CACHE(cpu)[i]);

      if (!(entry->gvpa & MTS_INV_ENTRY_MASK)) {
         printf("    %4u: vaddr=0x%8.8llx, paddr=0x%8.8llx, hpa=%p\n",
                i,(m_uint64_t)entry->gvpa,(m_uint64_t)entry->gppa,
                (void *)entry->hpa);
         count++;
      }
   }

   printf("   %u/%u valid hash entries.\n",count,MTS_NAME_UP(HASH_SIZE));
#endif

   printf("   Total lookups: %llu, misses: %llu, efficiency: %g%%\n",
          cpu->mts_lookups, cpu->mts_misses,
          100 - ((double)(cpu->mts_misses*100)/
                 (double)cpu->mts_lookups));
}

/* Invalidate the complete MTS cache */
void MTS_PROTO(invalidate_cache)(cpu_mips_t *cpu)
{
   size_t len;

   len = MTS_NAME_UP(HASH_SIZE) * sizeof(MTS_ENTRY);
   memset(MTS_CACHE(cpu),0xFF,len);
}

/* 
 * MTS mapping.
 *
 * It is NOT inlined since it triggers a GCC bug on my config (x86, GCC 3.3.5)
 */
static no_inline MTS_ENTRY *
MTS_PROTO(map)(cpu_mips_t *cpu,u_int op_type,mts_map_t *map,
               MTS_ENTRY *entry,MTS_ENTRY *alt_entry)
{
   cpu_tb_t *tb;
   struct vdevice *dev;
   m_uint32_t offset;
   m_iptr_t host_ptr;
   m_uint32_t exec_flag = 0;
   int cow;

   if (!(dev = dev_lookup(cpu->vm,map->paddr,map->cached)))
      return NULL;

   if (cpu->gen->tb_phys_hash != NULL) {      
      tb = mips64_jit_find_by_phys_page(cpu,map->paddr >> VM_PAGE_SHIFT);

      if ((tb != NULL) && !(tb->flags & TB_FLAG_SMC))
         exec_flag = MTS_FLAG_EXEC;
   }

   if (dev->flags & VDEVICE_FLAG_SPARSE) {
      host_ptr = dev_sparse_get_host_addr(cpu->vm,dev,map->paddr,op_type,&cow);

      entry->gvpa  = map->vaddr;
      entry->gppa  = map->paddr;
      entry->hpa   = host_ptr;
      entry->flags = (cow) ? MTS_FLAG_COW : 0;
      entry->flags |= exec_flag | map->flags;
      return entry;
   }

   if (!dev->host_addr || (dev->flags & VDEVICE_FLAG_NO_MTS_MMAP)) {
      offset = (map->paddr + map->offset) - dev->phys_addr;

      /* device entries are never stored in virtual TLB */
      alt_entry->hpa   = (dev->id << MTS_DEVID_SHIFT) + offset;
      alt_entry->flags = MTS_FLAG_DEV | map->flags;
      return alt_entry;
   }

   entry->gvpa  = map->vaddr;
   entry->gppa  = map->paddr;
   entry->hpa   = dev->host_addr + (map->paddr - dev->phys_addr);
   entry->flags = exec_flag | map->flags;
   return entry;
}

/* Invalidate TCB related to a physical page marked as executable */
static void MTS_PROTO(invalidate_tcb)(cpu_mips_t *cpu,MTS_ENTRY *entry)
{
   m_uint32_t hp,phys_page,pc_phys_page;

   if (cpu->gen->tb_phys_hash == NULL)
      return;

   phys_page = entry->gppa >> VM_PAGE_SHIFT;
   hp = mips64_jit_get_phys_hash(phys_page);
 
   cpu->translate(cpu,cpu->pc,&pc_phys_page);

   entry->flags &= ~MTS_FLAG_EXEC;
   cpu_jit_write_on_exec_page(cpu->gen,phys_page,hp,pc_phys_page);
}

/* MTS lookup */
static void *MTS_PROTO(lookup)(cpu_mips_t *cpu,m_uint64_t vaddr)
{
   m_uint64_t data;
   return(MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LOOKUP,4,MTS_READ,&data));
}

/* MTS instruction fetch */
static void *MTS_PROTO(ifetch)(cpu_mips_t *cpu,m_uint64_t vaddr)
{
   m_uint64_t data;
   return(MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_IFETCH,4,MTS_READ,&data));
}

/* === MIPS Memory Operations ============================================= */

/* LB: Load Byte */
fastcall void MTS_PROTO(lb)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LB,1,MTS_READ,&data);
   if (likely(haddr != NULL)) data = *(m_uint8_t *)haddr;
   cpu->gpr[reg] = sign_extend(data,8);
}

/* LBU: Load Byte Unsigned */
fastcall void MTS_PROTO(lbu)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LBU,1,MTS_READ,&data);
   if (likely(haddr != NULL)) data = *(m_uint8_t *)haddr;
   cpu->gpr[reg] = data & 0xff;
}

/* LH: Load Half-Word */
fastcall void MTS_PROTO(lh)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LH,2,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh16(*(m_uint16_t *)haddr);
   cpu->gpr[reg] = sign_extend(data,16);
}

/* LHU: Load Half-Word Unsigned */
fastcall void MTS_PROTO(lhu)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LHU,2,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh16(*(m_uint16_t *)haddr);
   cpu->gpr[reg] = data & 0xffff;
}

/* LW: Load Word */
fastcall void MTS_PROTO(lw)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LW,4,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);
   cpu->gpr[reg] = sign_extend(data,32);
}

/* LWU: Load Word Unsigned */
fastcall void MTS_PROTO(lwu)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LWU,4,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);
   cpu->gpr[reg] = data & 0xffffffff;
}

/* LD: Load Double-Word */
fastcall void MTS_PROTO(ld)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LD,8,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);
   cpu->gpr[reg] = data;
}

/* SB: Store Byte */
fastcall void MTS_PROTO(sb)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   data = cpu->gpr[reg] & 0xff;
   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SB,1,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint8_t *)haddr = data;
}

/* SH: Store Half-Word */
fastcall void MTS_PROTO(sh)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   data = cpu->gpr[reg] & 0xffff;
   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SH,2,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint16_t *)haddr = htovm16(data);
}

/* SW: Store Word */
fastcall void MTS_PROTO(sw)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   data = cpu->gpr[reg] & 0xffffffff;
   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SW,4,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
}

/* SD: Store Double-Word */
fastcall void MTS_PROTO(sd)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   data = cpu->gpr[reg];
   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SD,8,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
}

/* LDC1: Load Double-Word To Coprocessor 1 */
fastcall void MTS_PROTO(ldc1)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LDC1,8,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);
   cpu->fpu.reg[reg] = data;
}

/* LWL: Load Word Left */
fastcall void MTS_PROTO(lwl)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;

   naddr = vaddr & ~(0x03);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_LWL,4,MTS_READ,&data);

   if (likely(haddr != NULL))
      data = vmtoh32(*(m_uint32_t *)haddr);

   m_shift = (vaddr & 0x03) << 3;
   r_mask = (1ULL << m_shift) - 1;
   data <<= m_shift;

   cpu->gpr[reg] &= r_mask;
   cpu->gpr[reg] |= data;
   cpu->gpr[reg] = sign_extend(cpu->gpr[reg],32);
}

/* LWR: Load Word Right */
fastcall void MTS_PROTO(lwr)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;

   naddr = vaddr & ~(0x03);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_LWR,4,MTS_READ,&data);

   if (likely(haddr != NULL))
      data = vmtoh32(*(m_uint32_t *)haddr);

   m_shift = ((vaddr & 0x03) + 1) << 3;
   r_mask = (1ULL << m_shift) - 1;

   data = sign_extend(data >> (32 - m_shift),32);
   r_mask = sign_extend(r_mask,32);

   cpu->gpr[reg] &= ~r_mask;
   cpu->gpr[reg] |= data;
}

/* LDL: Load Double-Word Left */
fastcall void MTS_PROTO(ldl)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;

   naddr = vaddr & ~(0x07);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_LDL,8,MTS_READ,&data);

   if (likely(haddr != NULL))
      data = vmtoh64(*(m_uint64_t *)haddr);

   m_shift = (vaddr & 0x07) << 3;
   r_mask = (1ULL << m_shift) - 1;
   data <<= m_shift;

   cpu->gpr[reg] &= r_mask;
   cpu->gpr[reg] |= data;
}

/* LDR: Load Double-Word Right */
fastcall void MTS_PROTO(ldr)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;

   naddr = vaddr & ~(0x07);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_LDR,8,MTS_READ,&data);

   if (likely(haddr != NULL))
      data = vmtoh64(*(m_uint64_t *)haddr);

   m_shift = ((vaddr & 0x07) + 1) << 3;
   r_mask = (1ULL << m_shift) - 1;
   data >>= (64 - m_shift);
   
   cpu->gpr[reg] &= ~r_mask;
   cpu->gpr[reg] |= data;
}

/* SWL: Store Word Left */
fastcall void MTS_PROTO(swl)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;

   naddr = vaddr & ~(0x03ULL);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SWL,4,MTS_READ,&data);

   if (likely(haddr != NULL))
      data = vmtoh32(*(m_uint32_t *)haddr);

   r_shift = (vaddr & 0x03) << 3;
   d_mask = 0xffffffff >> r_shift;

   data &= ~d_mask;
   data |= (cpu->gpr[reg] & 0xffffffff) >> r_shift;

   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SWL,4,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
}

/* SWR: Store Word Right */
fastcall void MTS_PROTO(swr)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;

   naddr = vaddr & ~(0x03);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SWR,4,MTS_READ,&data);

   if (likely(haddr != NULL))
      data = vmtoh32(*(m_uint32_t *)haddr);

   r_shift = ((vaddr & 0x03) + 1) << 3;
   d_mask = 0xffffffff >> r_shift;

   data &= d_mask;
   data |= (cpu->gpr[reg] << (32 - r_shift)) & 0xffffffff;

   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SWR,4,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
}

/* SDL: Store Double-Word Left */
fastcall void MTS_PROTO(sdl)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;

   naddr = vaddr & ~(0x07);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SDL,8,MTS_READ,&data);

   if (likely(haddr != NULL))
      data = vmtoh64(*(m_uint64_t *)haddr);

   r_shift = (vaddr & 0x07) << 3;
   d_mask = 0xffffffffffffffffULL >> r_shift;

   data &= ~d_mask;
   data |= cpu->gpr[reg] >> r_shift;

   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SDL,8,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
}

/* SDR: Store Double-Word Right */
fastcall void MTS_PROTO(sdr)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;

   naddr = vaddr & ~(0x07);
   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SDR,8,MTS_READ,&data);

   if (likely(haddr != NULL))
      data = vmtoh64(*(m_uint64_t *)haddr);

   r_shift = ((vaddr & 0x07) + 1) << 3;
   d_mask = 0xffffffffffffffffULL >> r_shift;

   data &= d_mask;
   data |= cpu->gpr[reg] << (64 - r_shift);

   haddr = MTS_PROTO(access)(cpu,naddr,MIPS_MEMOP_SDR,8,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
}

/* LL: Load Linked */
fastcall void MTS_PROTO(ll)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_LL,4,MTS_READ,&data);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);

   cpu->gpr[reg] = sign_extend(data,32);
   cpu->ll_bit = 1;
}

/* SC: Store Conditional */
fastcall void MTS_PROTO(sc)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   if (cpu->ll_bit) {
      data = cpu->gpr[reg] & 0xffffffff;
      haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SC,4,MTS_WRITE,&data);
      if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
   }

   cpu->gpr[reg] = cpu->ll_bit;
}

/* SDC1: Store Double-Word from Coprocessor 1 */
fastcall void MTS_PROTO(sdc1)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;

   data = cpu->fpu.reg[reg];
   haddr = MTS_PROTO(access)(cpu,vaddr,MIPS_MEMOP_SDC1,8,MTS_WRITE,&data);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
}

/* CACHE: Cache operation */
fastcall void MTS_PROTO(cache)(cpu_mips_t *cpu,m_uint64_t vaddr,u_int op)
{
#if DEBUG_CACHE
   cpu_log(cpu->gen,
           "MTS","CACHE: PC=0x%llx, vaddr=0x%llx, cache=%u, code=%u\n",
           cpu->pc, vaddr, op & 0x3, op >> 2);
#endif
}

/* === MTS Cache Management ============================================= */

void MTS_PROTO(api_rebuild)(cpu_gen_t *cpu)
{
   MTS_PROTO(invalidate_cache)(CPU_MIPS64(cpu));
}

/* ======================================================================== */

/* Initialize memory access vectors */
void MTS_PROTO(init_memop_vectors)(cpu_mips_t *cpu)
{
   /* XXX TODO:
    *  - LD/SD forbidden in Supervisor/User modes with 32-bit addresses.
    */

   cpu->addr_mode = MTS_ADDR_SIZE;

   /* Memory lookup and Instruction fetch operations */
   cpu->mem_op_lookup = MTS_PROTO(lookup);
   cpu->mem_op_ifetch = MTS_PROTO(ifetch);

   /* Translation operation */
   cpu->translate = MTS_PROTO(translate);

   /* Invalidate and Shutdown operations */
   cpu->mts_invalidate = MTS_PROTO(invalidate_cache);
   cpu->mts_shutdown = MTS_PROTO(shutdown);

   /* Rebuild MTS data structures */
   cpu->gen->mts_rebuild = MTS_PROTO(api_rebuild);

   /* Show statistics */
   cpu->gen->mts_show_stats = MTS_PROTO(show_stats);

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
#undef MTS_NAME
#undef MTS_NAME_UP
#undef MTS_PROTO
#undef MTS_PROTO_UP
#undef MTS_ENTRY
#undef MTS_CHUNK
