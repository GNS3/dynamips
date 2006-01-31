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

   if (++cpu->memlog_pos == MEMLOG_COUNT)
      cpu->memlog_pos = 0;
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
      if (!(p = m_memalign((1 << MTS32_FLAG_BITS),sizeof(*p))))
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

   for(i=0;i<(1<<MTS32_LEVEL1_BITS);i++)
      if (array->entry[i] & MTS32_CHAIN_MASK)
         free((void *)(array->entry[i] & ~MTS32_CHAIN_MASK));

   free(array);
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

   while(len > 0) {
      pos = start >> (MTS32_LEVEL2_BITS + MTS32_OFFSET_BITS);

      if (pos >= (1 << MTS32_LEVEL1_BITS))
         break;

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
   val = ((entry & MTS32_ACC_MASK) != MTS32_ACC_OK) ? entry : 0;

   if (!(p2 = mts32_alloc_l2_array(cpu,val)))
      return(-1);

   /* mts32_alloc_l2_array() did the job for us */
   if (!val) {
      for(i=0;i<(1 << MTS32_LEVEL2_BITS);i++)
         p2->entry[i] = entry + (1 << MTS32_OFFSET_BITS);
   }
   
   p1->entry[l1_pos] = (m_iptr_t)p2 | MTS32_CHAIN_MASK;
   return(0);
}

/* Set address error on a complete level 1 array */
void mts32_set_l1_ae(cpu_mips_t *cpu)
{
   mts32_l1_array_t *p1 = cpu->mts_l1_ptr;
   u_int i;

   for(i=0;i<(1<<MTS32_LEVEL1_BITS);i++)
      p1->entry[i] = MTS32_ACC_AE;
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

   if (!(entry & MTS32_CHAIN_MASK)) {
      if (mts32_fork_l1_array(cpu,l1_pos) == -1) {
         fprintf(stderr,"mts32_set_l2_entry: unable to fork L1 entry.\n");
         return(-1);
      }

      entry = p1->entry[l1_pos];
   }

   p2 = (mts32_l2_array_t *)(entry & MTS32_ADDR_MASK);
   p2->entry[l2_pos] = val;
   return(0);
}

/* Initialize an MTS array for kernel mode with 32-bit addresses */
int mts32_init_kernel_mode(cpu_mips_t *cpu)
{
   cpu->mts_l1_ptr = mts32_alloc_l1_array(0);

   if (!cpu->mts_l1_ptr)
      return(-1);

   /* Address Error on complete address space for now */
   mts32_set_l1_ae(cpu);

   /* KUSEG: 0x00000000 -> 0x80000000: 2 GB mapped (TLB) */
   mts32_set_l1_data(cpu,MIPS_KUSEG_BASE,MIPS_KUSEG_SIZE,MTS32_ACC_T);

   /* KSEG0: 0x80000000 -> 0xa0000000: 0.5GB mapped (unmapped) */
   mts32_set_l1_data(cpu,MIPS_KSEG0_BASE,MIPS_KSEG0_SIZE,MTS32_ACC_U);

   /* KSEG1: 0xa0000000 -> 0xc0000000: 0.5GB mapped (unmapped) */
   mts32_set_l1_data(cpu,MIPS_KSEG1_BASE,MIPS_KSEG1_SIZE,MTS32_ACC_U);

   /* KSSEG: 0xc0000000 -> 0xe0000000: 0.5GB mapped (TLB) */
   mts32_set_l1_data(cpu,MIPS_KSSEG_BASE,MIPS_KSSEG_SIZE,MTS32_ACC_T);

   /* KSEG3: 0xe0000000 -> 0xffffffff: 0.5GB mapped (TLB) */
   mts32_set_l1_data(cpu,MIPS_KSEG3_BASE,MIPS_KSEG3_SIZE,MTS32_ACC_T);
   
   return(0);
}

/* Map a device at the specified virtual address */
void mts32_map_device(cpu_mips_t *cpu,u_int dev_id,m_uint64_t vaddr,
                      m_uint32_t offset,m_uint32_t len)
{
   struct vdevice *dev;
   m_iptr_t val;

   if (!(dev = dev_get_by_id(cpu,dev_id)))
      return;

   if (!dev->phys_len)
      return;

#if DEBUG_MTS_MAP_DEV
   m_log("MTS32","mapping device %s (offset=0x%x,len=0x%x) at vaddr 0x%llx\n",
         dev->name,offset,len,vaddr);
#endif

   if (!dev->host_addr || (dev->flags & VDEVICE_FLAG_NO_MTS_MMAP))
      val = (dev_id << MTS32_DEVID_SHIFT) | MTS32_DEV_MASK | MTS32_ACC_OK;
   else
      val = dev->host_addr | MTS32_ACC_OK;

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

/* Map all devices for kernel mode */
void mts32_km_map_all_dev(cpu_mips_t *cpu)
{
   struct vdevice *dev;
   m_uint32_t len;
   u_int dev_id;

   for(dev_id=0;dev_id<MIPS64_DEVICE_MAX;dev_id++) 
   {
      if (!(dev = dev_get_by_id(cpu,dev_id)))
         continue;

      /* we map only devices present in the first 0.5 GB of physical memory */
      if (dev->phys_addr >= MTS_SIZE_512M)
         continue;

      if ((dev->phys_addr + dev->phys_len) >= MTS_SIZE_512M)
         len = MTS_SIZE_512M - dev->phys_addr;
      else
         len = dev->phys_len;
      
      mts32_map_device(cpu,dev_id,MIPS_KSEG0_BASE+dev->phys_addr,0,len);
      mts32_map_device(cpu,dev_id,MIPS_KSEG1_BASE+dev->phys_addr,0,len);
   }
}

/* Map a physical address to the specified virtual address */
void mts32_map_phys_addr(cpu_mips_t *cpu,m_uint64_t vaddr,
                         m_uint64_t paddr,m_uint32_t len)
{
   m_uint32_t dev_offset,dev_len;
   struct vdevice *dev;
   u_int dev_id;

   while(len > 0)
   {
#if DEBUG_MTS_MAP_VIRT
      m_log("MTS32",
            "mts32_map_phys_addr: vaddr=0x%llx, paddr=0x%llx, len=0x%x\n",
            vaddr,paddr,len);
#endif
      dev = dev_lookup(cpu,paddr,&dev_id);

      if (!dev) {
         mts32_set_l2_entry(cpu,vaddr,MTS32_ACC_U);
         dev_len = MTS32_LEVEL2_SIZE;
         goto next;
      }

      dev_offset = paddr - dev->phys_addr;
      dev_len = dev->phys_len - dev_offset;

      if (len <= dev_len) {
         mts32_map_device(cpu,dev_id,vaddr,dev_offset,len);
         break;
      }

      mts32_map_device(cpu,dev_id,vaddr,dev_offset,dev_len);

   next:
      vaddr += dev_len;
      paddr += dev_len;
      len -= dev_len;
   }
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

   if (entry & MTS32_CHAIN_MASK) {
      p2 = (mts32_l2_array_t *)(entry & MTS32_ADDR_MASK);
      l2_pos = (naddr >> MTS32_OFFSET_BITS) & ((1 << MTS32_LEVEL2_BITS) - 1);
      entry = p2->entry[l2_pos];
      shift = MTS32_OFFSET_BITS;
   }
   
   /* device access */
   if (unlikely(entry & MTS32_DEV_MASK)) {
      dev_id = (entry & MTS32_DEVID_MASK) >> MTS32_DEVID_SHIFT;
      haddr = (entry & MTS32_DEVOFF_MASK);
      haddr |= (naddr & ((1 << shift) - 1));
      return(dev_access(cpu,dev_id,haddr,MTS_READ,4,&data));
   }

   haddr = entry & MTS32_ADDR_MASK;
   haddr += (naddr & ((1 << shift) - 1));   
   return((void *)haddr);
}

/* MTS32 access with special access mask */
static forced_inline void 
mts32_access_special(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint32_t mask,
                     u_int op_type,u_int op_size,m_uint64_t *data,u_int *exc)
{
   switch(mask) {
      case MTS32_ACC_U:
#if DEBUG_MTS_ACC_U
         if (op_type == MTS_READ)
            m_log("MTS32","read  access to undefined address 0x%llx at "
                  "pc=0x%llx (size=%u)\n",vaddr,cpu->pc,op_size);
         else
            m_log("MTS32","write access to undefined address 0x%llx at "
                  "pc=0x%llx, value=0x%8.8llx (size=%u)\n",
                  vaddr,cpu->pc,*data,op_size);
#endif
         if (op_type == MTS_READ)
            *data = 0;
         break;

      case MTS32_ACC_T:
         if ((vaddr & 0xF000000000000000ULL) == 0x9000000000000000ULL)
            break;

#if DEBUG_MTS_ACC_T
         m_log("MTS32","TLB exception for address 0x%llx at pc=0x%llx "
               "(%s access)\n",
               vaddr,cpu->pc,(op_type == MTS_READ) ? "read":"write");

         if (op_type == MTS_WRITE) {
            m_log("MTS32","tlb_exception: tried to write value 0x%llx\n",
                  *data);
            mips64_dump_regs(cpu);
#if MEMLOG_ENABLE
            memlog_dump(cpu);
#endif
         }
#endif
         cpu->cp0.reg[MIPS_CP0_BADVADDR] = vaddr;
            
         if (op_type == MTS_READ)
            mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_TLB_LOAD,0);
         else
            mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_TLB_SAVE,0);
         
         *exc = 1;
         break;

      case MTS32_ACC_AE:
#if DEBUG_MTS_ACC_AE
         m_log("MTS32","AE exception for address 0x%llx at pc=0x%llx "
               "(%s access)\n",
               vaddr,cpu->pc,(op_type == MTS_READ) ? "read":"write");
#endif            
         cpu->cp0.reg[MIPS_CP0_BADVADDR] = vaddr;

         if (op_type == MTS_READ)
            mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_ADDR_LOAD,0);
         else
            mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_ADDR_SAVE,0);
         
         *exc = 1;
         break;
   }
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

   if (unlikely((mask = (entry & MTS32_ACC_MASK)) != MTS32_ACC_OK)) {
      mts32_access_special(cpu,vaddr,mask,op_type,op_size,data,exc);
      return NULL;
   }

   /* do we have a level 2 entry ? */
   if (entry & MTS32_CHAIN_MASK) {
      p2 = (mts32_l2_array_t *)(entry & MTS32_ADDR_MASK);
      l2_pos = (naddr >> MTS32_OFFSET_BITS) & ((1 << MTS32_LEVEL2_BITS) - 1);
      entry = p2->entry[l2_pos];
      shift = MTS32_OFFSET_BITS;
   }

   if (unlikely((mask = (entry & MTS32_ACC_MASK)) != MTS32_ACC_OK)) {
      mts32_access_special(cpu,vaddr,mask,op_type,op_size,data,exc);
      return NULL;
   }

   /* device access */
   if (unlikely(entry & MTS32_DEV_MASK)) {
      dev_id = (entry & MTS32_DEVID_MASK) >> MTS32_DEVID_SHIFT;
      haddr = (entry & MTS32_DEVOFF_MASK);
      haddr |= (naddr & ((1 << shift) - 1));

#if DEBUG_MTS_DEV
      m_log("MTS32","device access: vaddr=0x%llx, pc=0x%llx\n",vaddr,cpu->pc);
#endif
      return(dev_access(cpu,dev_id,haddr,op_size,op_type,data));
   }

   /* raw memory access */
   haddr = entry & MTS32_ADDR_MASK;
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

/* LB: Load Byte */
fastcall u_int mts32_lb(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_LB,1,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = *(m_uint8_t *)haddr;
   cpu->gpr[reg] = sign_extend(data,8);
   return(exc);
}

/* LBU: Load Byte Unsigned */
fastcall u_int mts32_lbu(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_LBU,1,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = *(m_uint8_t *)haddr;
   cpu->gpr[reg] = data & 0xff;
   return(exc);
}

/* LH: Load Half-Word */
fastcall u_int mts32_lh(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_LH,2,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh16(*(m_uint16_t *)haddr);
   cpu->gpr[reg] = sign_extend(data,16);
   return(exc);
}

/* LHU: Load Half-Word Unsigned */
fastcall u_int mts32_lhu(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_LHU,2,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh16(*(m_uint16_t *)haddr);
   cpu->gpr[reg] = data & 0xffff;
   return(exc);
}

/* LW: Load Word */
fastcall u_int mts32_lw(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_LW,4,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);
   cpu->gpr[reg] = sign_extend(data,32);
   return(exc);
}

/* LWU: Load Word Unsigned */
fastcall u_int mts32_lwu(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_LWU,4,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);
   cpu->gpr[reg] = data & 0xffffffff;
   return(exc);
}

/* LD: Load Double-Word */
fastcall u_int mts32_ld(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_LD,8,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);
   cpu->gpr[reg] = data;   
   return(exc);
}

/* SB: Store Byte */
fastcall u_int mts32_sb(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   data = cpu->gpr[reg] & 0xff;
   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_SB,1,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint8_t *)haddr = data;
   return(exc);
}

/* SH: Store Half-Word */
fastcall u_int mts32_sh(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   data = cpu->gpr[reg] & 0xffff;
   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_SH,2,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint16_t *)haddr = htovm16(data);
   return(exc);
}

/* SW: Store Word */
fastcall u_int mts32_sw(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   data = cpu->gpr[reg] & 0xffffffff;
   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_SW,4,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
   return(exc);
}

/* SD: Store Double-Word */
fastcall u_int mts32_sd(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   data = cpu->gpr[reg];
   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_SD,8,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
   return(exc);
}

/* LD: Load Double-Word To Coprocessor 1 */
fastcall u_int mts32_ldc1(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_LD,8,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);
   cpu->fpu.reg[reg] = data;
   return(exc);
}

/* LWL: Load Word Left */
fastcall u_int mts32_lwl(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x03);
   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_LWL,4,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);

   m_shift = (vaddr & 0x03) << 3;
   r_mask = (1ULL << m_shift) - 1;
   data <<= m_shift;

   cpu->gpr[reg] &= r_mask;
   cpu->gpr[reg] |= data;
   cpu->gpr[reg] = sign_extend(cpu->gpr[reg],32);
   return(exc);
}

/* LWR: Load Word Right */
fastcall u_int mts32_lwr(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x03);
   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_LWR,4,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);

   m_shift = ((vaddr & 0x03) + 1) << 3;
   r_mask = (1ULL << m_shift) - 1;
   
   data = sign_extend(data >> (32 - m_shift),32);
   r_mask = sign_extend(r_mask,32);

   cpu->gpr[reg] &= ~r_mask;
   cpu->gpr[reg] |= data;
   return(exc);
}

/* LDL: Load Double-Word Left */
fastcall u_int mts32_ldl(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x07);
   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_LDL,8,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);

   m_shift = (vaddr & 0x07) << 3;
   r_mask = (1ULL << m_shift) - 1;
   data <<= m_shift;

   cpu->gpr[reg] &= r_mask;
   cpu->gpr[reg] |= data;
   return(exc);
}

/* LDR: Load Double-Word Right */
fastcall u_int mts32_ldr(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t r_mask,naddr;
   m_uint64_t data;
   u_int m_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x07);
   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_LDL,8,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);

   m_shift = ((vaddr & 0x07) + 1) << 3;
   r_mask = (1ULL << m_shift) - 1;
   data >>= (64 - m_shift);

   cpu->gpr[reg] &= ~r_mask;
   cpu->gpr[reg] |= data;
   return(exc);
}

/* SWL: Store Word Left */
fastcall u_int mts32_swl(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x03ULL);
   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_LW,4,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);

   r_shift = (vaddr & 0x03) << 3;
   d_mask = 0xffffffff >> r_shift;

   data &= ~d_mask;
   data |= (cpu->gpr[reg] & 0xffffffff) >> r_shift;

   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_SWL,4,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
   return(exc);
}

/* SWR: Store Word Right */
fastcall u_int mts32_swr(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x03);
   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_LW,4,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);

   if (exc) return(exc);

   r_shift = ((vaddr & 0x03) + 1) << 3;
   d_mask = 0xffffffff >> r_shift;

   data &= d_mask;
   data |= (cpu->gpr[reg] << (32 - r_shift)) & 0xffffffff;

   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_SWR,4,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
   return(exc);
}

/* SDL: Store Double-Word Left */
fastcall u_int mts32_sdl(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x07);
   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_LD,8,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);

   if (exc) return(exc);

   r_shift = (vaddr & 0x07) << 3;
   d_mask = 0xffffffffffffffffULL >> r_shift;

   data &= ~d_mask;
   data |= cpu->gpr[reg] >> r_shift;

   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_SDL,8,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
   return(exc);
}

/* SDR: Store Double-Word Right */
fastcall u_int mts32_sdr(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t d_mask,naddr;
   m_uint64_t data;
   u_int r_shift;
   void *haddr;
   u_int exc;

   naddr = vaddr & ~(0x07);
   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_LD,8,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh64(*(m_uint64_t *)haddr);

   if (exc) return(exc);

   r_shift = ((vaddr & 0x07) + 1) << 3;
   d_mask = 0xffffffffffffffffULL >> r_shift;

   data &= d_mask;
   data |= cpu->gpr[reg] << (64 - r_shift);

   haddr = mts32_access(cpu,naddr,MIPS_MEMOP_SDR,8,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
   return(exc);
}

/* LL: Load Linked */
fastcall u_int mts32_ll(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_LL,4,MTS_READ,&data,&exc);
   if (likely(haddr != NULL)) data = vmtoh32(*(m_uint32_t *)haddr);

   if (!exc) {
      cpu->gpr[reg] = sign_extend(data,32);
      cpu->ll_bit = 1;
   }

   return(exc);
}

/* SC: Store Conditional */
fastcall u_int mts32_sc(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc = 0;
   
   if (cpu->ll_bit) {
      data = cpu->gpr[reg] & 0xffffffff;
      haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_SW,4,MTS_WRITE,&data,&exc);
      if (likely(haddr != NULL)) *(m_uint32_t *)haddr = htovm32(data);
   }

   if (!exc)
      cpu->gpr[reg] = cpu->ll_bit;
   return(exc);
}

/* SDC1: Store Double-Word from Coprocessor 1 */
fastcall u_int mts32_sdc1(cpu_mips_t *cpu,m_uint64_t vaddr,u_int reg)
{
   m_uint64_t data;
   void *haddr;
   u_int exc;

   data = cpu->fpu.reg[reg];
   haddr = mts32_access(cpu,vaddr,MIPS_MEMOP_SDC1,8,MTS_WRITE,&data,&exc);
   if (likely(haddr != NULL)) *(m_uint64_t *)haddr = htovm64(data);
   return(exc);
}

/* CACHE: Cache operation */
fastcall u_int mts32_cache(cpu_mips_t *cpu,m_uint64_t vaddr,u_int op)
{
   struct insn_block *b;

#if DEBUG_CACHE
   m_log("MTS32","CACHE: PC=0x%llx, vaddr = 0x%llx, op = %u\n",
         cpu->pc, vaddr, op);
#endif

   b = insn_block_locate(cpu,vaddr);

   if (b) {
#if DEBUG_CACHE
      m_log("MTS32","CACHE: Removing compiled block at start=0x%llx "
            "(end=0x%llx)\n",b->start_pc,b->end_pc);
#endif
      rbtree_remove(cpu->insn_block_tree,&vaddr);
      mips64_jit_remove_hash_block(cpu,b);
   }

   return(0);
}

/* Initialize memory access vectors */
void mts_init_memop_vectors(cpu_mips_t *cpu)
{
   /* XXX TODO:
    *  - MTS32 or MTS64. 
    *  - LD/SD forbidden in Supervisor/User modes with 32-bit addresses.
    */

   /* Generic Memory operation */
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
   cpu->mem_op_fn[MIPS_MEMOP_CACHE] = mts32_cache;
}

/* Copy a memory block from VM physical RAM to real host */
void physmem_copy_from_vm(cpu_mips_t *cpu,void *real_buffer,
                          m_uint64_t paddr,size_t len)
{
   struct vdevice *vm_ram;
   u_char *ptr;

   if ((vm_ram = dev_lookup(cpu,paddr,NULL)) != NULL) {
      assert(vm_ram->host_addr != 0);
      ptr = (u_char *)vm_ram->host_addr + (paddr - vm_ram->phys_addr);
      memcpy(real_buffer,ptr,len);
   }
}

/* Copy a memory block to VM physical RAM from real host */
void physmem_copy_to_vm(cpu_mips_t *cpu,void *real_buffer,
                        m_uint64_t paddr,size_t len)
{
   struct vdevice *vm_ram;
   u_char *ptr;

   if ((vm_ram = dev_lookup(cpu,paddr,NULL)) != NULL) {
      assert(vm_ram->host_addr != 0);
      ptr = (u_char *)vm_ram->host_addr + (paddr - vm_ram->phys_addr);
      memcpy(ptr,real_buffer,len);
   }
}

/* Copy a 32-bit word from the VM physical RAM to real host */
m_uint32_t physmem_copy_u32_from_vm(cpu_mips_t *cpu,m_uint64_t paddr)
{
   struct vdevice *dev;
   m_uint32_t offset;
   m_uint64_t tmp;
   void *ptr;

   if (unlikely((dev = dev_lookup(cpu,paddr,NULL)) == NULL))
      return(0);

   offset = paddr - dev->phys_addr;

   if ((dev->host_addr != 0) && !(dev->flags & VDEVICE_FLAG_NO_MTS_MMAP))
      ptr = (u_char *)dev->host_addr + offset;
   else {
      ptr = dev->handler(cpu,dev,offset,4,MTS_READ,&tmp);
      if (!ptr) return(tmp);
   }
   
   return(vmtoh32(*(m_uint32_t *)ptr));
}

/* Copy a 32-bit word to the VM physical RAM from real host */
void physmem_copy_u32_to_vm(cpu_mips_t *cpu,m_uint64_t paddr,m_uint32_t val)
{
   struct vdevice *dev;
   m_uint32_t offset;
   m_uint64_t tmp;
   void *ptr;

   if (unlikely((dev = dev_lookup(cpu,paddr,NULL)) == NULL))
      return;

   offset = paddr - dev->phys_addr;

   if ((dev->host_addr != 0) && !(dev->flags & VDEVICE_FLAG_NO_MTS_MMAP))
      ptr = (u_char *)dev->host_addr + offset;
   else {
      tmp = val;
      ptr = dev->handler(cpu,dev,offset,4,MTS_READ,&tmp);
      if (!ptr) return;
   }
   
   *(m_uint32_t *)ptr = htovm32(val);
}

/* DMA transfer operation */
void physmem_dma_transfer(cpu_mips_t *cpu,m_uint64_t src,m_uint64_t dst,
                          size_t len)
{
   struct vdevice *src_dev,*dst_dev;
   u_char *sptr,*dptr;

   src_dev = dev_lookup(cpu,src,NULL);
   dst_dev = dev_lookup(cpu,dst,NULL);

   if ((src_dev != NULL) && (dst_dev != NULL)) {
      assert(src_dev->host_addr != 0);
      assert(dst_dev->host_addr != 0);
      
      sptr = (u_char *)src_dev->host_addr + (src - src_dev->phys_addr);
      dptr = (u_char *)dst_dev->host_addr + (dst - dst_dev->phys_addr);
      memcpy(dptr,sptr,len);
   }
}

/* strlen in VM physical memory */
size_t physmem_strlen(cpu_mips_t *cpu,m_uint64_t paddr)
{
   struct vdevice *vm_ram;
   size_t len = 0;
   char *ptr;
   
   if ((vm_ram = dev_lookup(cpu,paddr,NULL)) != NULL) {
      ptr = (char *)vm_ram->host_addr + (paddr - vm_ram->phys_addr);
      len = strlen(ptr);
   }

   return(len);
}

/* Physical memory dump (32-bit words) */
void physmem_dump_vm(cpu_mips_t *cpu,m_uint64_t paddr,m_uint32_t u32_count)
{
   m_uint32_t i;

   for(i=0;i<u32_count;i++) {
      if ((i & 3) == 0)
         fprintf(log_file,"\n0x%8.8llx: ",paddr);

      fprintf(log_file,"0x%8.8x ",physmem_copy_u32_from_vm(cpu,paddr+(i<<2)));
   }
   
   fprintf(log_file,"\n");
}
