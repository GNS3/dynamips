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

/* === MTS for 64-bit address space ======================================= */
#define MTS_ADDR_SIZE      64
#define MTS_PROTO(name)    mts64_##name
#define MTS_PROTO_UP(name) MTS64_##name

#include "mips_mts.c"

/* === MTS for 32-bit address space ======================================= */
#define MTS_ADDR_SIZE      32
#define MTS_PROTO(name)    mts32_##name
#define MTS_PROTO_UP(name) MTS32_##name

#include "mips_mts.c"

/* === Specific operations for MTS64 ====================================== */

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
   entry = cpu->mts_cache[hash_bucket];
   zone = vaddr >> 40;

#if DEBUG_MTS_STATS
   cpu->mts_misses++;
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
      cpu->mts_cache[hash_bucket] = entry;
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
      entry->pself = (mts64_entry_t **)&cpu->mts_cache[hash_bucket];
      entry->next  = cpu->mts_rmap[map.tlb_index];
      entry->pprev = (mts64_entry_t **)&cpu->mts_rmap[map.tlb_index];
      if (entry->next)
         entry->next->pprev = &entry->next;
      cpu->mts_rmap[map.tlb_index] = entry;
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
   entry = cpu->mts_cache[hash_bucket];

#if DEBUG_MTS_STATS
   cpu->mts_lookups++;
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

/* MTS64 virtual address to physical page translation */
static fastcall int mts64_translate(cpu_mips_t *cpu,m_uint64_t vaddr,
                                    m_uint32_t *phys_page)
{   
   m_uint32_t hash_bucket,offset;
   mts64_entry_t *entry;
   m_uint64_t data = 0;
   u_int exc = 0;
   
   hash_bucket = MTS64_HASH(vaddr);
   entry = cpu->mts_cache[hash_bucket];

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

/* === Specific operations for MTS32 ====================================== */

/* MTS32 slow lookup */
static forced_inline 
mts32_entry_t *mts32_slow_lookup(cpu_mips_t *cpu,m_uint64_t vaddr,
                                 u_int op_code,u_int op_size,
                                 u_int op_type,m_uint64_t *data,
                                 u_int *exc)
{
   m_uint32_t hash_bucket,zone;
   mts32_entry_t *entry,new_entry;
   mts_map_t map;

   map.tlb_index = -1;
   hash_bucket = MTS32_HASH(vaddr);
   entry = cpu->mts_cache[hash_bucket];
   zone = (vaddr >> 29) & 0x7;

#if DEBUG_MTS_STATS
   cpu->mts_misses++;
#endif

   switch(zone) {
      case 0x00 ... 0x03:   /* kuseg */
         /* trigger TLB exception if no matching entry found */
         if (!cp0_tlb_lookup(cpu,vaddr,&map))
            goto err_tlb;

         if (!mts32_map(cpu,vaddr,&map,&new_entry))
            goto err_undef;
         break;

      case 0x04:   /* kseg0 */
         map.vaddr  = sign_extend(MIPS_KSEG0_BASE,32);
         map.paddr  = 0;
         map.len    = MIPS_KSEG0_SIZE;
         map.cached = TRUE;
         if (!mts32_map(cpu,vaddr,&map,&new_entry))
            goto err_undef;
         break;

      case 0x05:   /* kseg1 */
         map.vaddr  = sign_extend(MIPS_KSEG1_BASE,32);
         map.paddr  = 0;
         map.len    = MIPS_KSEG1_SIZE;
         map.cached = FALSE;
         if (!mts32_map(cpu,vaddr,&map,&new_entry))
            goto err_undef;
         break;

      case 0x06:   /* ksseg */
      case 0x07:   /* kseg3 */
         /* trigger TLB exception if no matching entry found */
         if (!cp0_tlb_lookup(cpu,vaddr,&map))
            goto err_tlb;

         if (!mts32_map(cpu,vaddr,&map,&new_entry))
            goto err_undef;
         break;
   }

   /* Get a new entry if necessary */
   if (!entry) {
      entry = mts32_alloc_entry(cpu);
      entry->pself = entry->pprev = NULL;
      entry->next = NULL;

      /* Store the entry in hash table for future use */
      cpu->mts_cache[hash_bucket] = entry;
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
      entry->pself = (mts32_entry_t **)&cpu->mts_cache[hash_bucket];
      entry->next  = cpu->mts_rmap[map.tlb_index];
      entry->pprev = (mts32_entry_t **)&cpu->mts_rmap[map.tlb_index];
      if (entry->next)
         entry->next->pprev = &entry->next;
      cpu->mts_rmap[map.tlb_index] = entry;
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

/* MTS32 access */
static forced_inline void *mts32_access(cpu_mips_t *cpu,m_uint64_t vaddr,
                                        u_int op_code,u_int op_size,
                                        u_int op_type,m_uint64_t *data,
                                        u_int *exc)
{
   m_uint32_t hash_bucket;
   mts32_entry_t *entry;
   m_iptr_t haddr;
   u_int dev_id;

#if MEMLOG_ENABLE
   /* Record the memory access */
   memlog_rec_access(cpu,vaddr,*data,op_size,op_type);
#endif

   *exc = 0;
   hash_bucket = MTS32_HASH(vaddr);
   entry = cpu->mts_cache[hash_bucket];

#if DEBUG_MTS_STATS
   cpu->mts_lookups++;
#endif

   /* Slow lookup if nothing found in cache */
   if (unlikely((!entry) || unlikely((vaddr & entry->mask) != entry->start))) {
      entry = mts32_slow_lookup(cpu,vaddr,op_code,op_size,op_type,data,exc);
      if (!entry) return NULL;
   }

   /* Device access */
   if (unlikely(entry->action & MTS_DEV_MASK)) {
      dev_id = (entry->action & MTS_DEVID_MASK) >> MTS_DEVID_SHIFT;
      haddr = entry->action & MTS_DEVOFF_MASK;
      haddr += (m_uint32_t)vaddr - entry->start;

#if DEBUG_MTS_DEV
      cpu_log(cpu,"MTS32",
              "device access: vaddr=0x%llx, pc=0x%llx, dev_offset=0x%x\n",
              vaddr,cpu->pc,haddr);
#endif
      return(dev_access_fast(cpu,dev_id,haddr,op_size,op_type,data));
   }

   /* Raw memory access */
   haddr = entry->action & MTS_ADDR_MASK;
   haddr += (m_uint32_t)vaddr - entry->start;
#if MEMLOG_ENABLE
   memlog_update_read(cpu,haddr);
#endif
   return((void *)haddr);
}

/* MTS32 virtual address to physical page translation */
static fastcall int mts32_translate(cpu_mips_t *cpu,m_uint64_t vaddr,
                                    m_uint32_t *phys_page)
{   
   m_uint32_t hash_bucket,offset;
   mts32_entry_t *entry;
   m_uint64_t data = 0;
   u_int exc = 0;
   
   hash_bucket = MTS32_HASH(vaddr);
   entry = cpu->mts_cache[hash_bucket];

   /* Slow lookup if nothing found in cache */
   if (unlikely((!entry) || unlikely((vaddr & entry->mask) != entry->start))) {
      entry = mts32_slow_lookup(cpu,vaddr,MIPS_MEMOP_LOOKUP,4,MTS_READ,
                                &data,&exc);
      if (!entry)
         return(-1);
   }

   offset = vaddr - entry->start;
   *phys_page = entry->phys_page + (offset >> MIPS_MIN_PAGE_SHIFT);
   return(0);
}

/* ======================================================================== */

/* Shutdown MTS subsystem */
void mts_shutdown(cpu_mips_t *cpu)
{
   if (cpu->mts_shutdown != NULL)
      cpu->mts_shutdown(cpu);
}

/* Set the address mode */
int mts_set_addr_mode(cpu_mips_t *cpu,u_int addr_mode)
{
   if (cpu->addr_mode != addr_mode) {
      mts_shutdown(cpu);
      
      switch(addr_mode) {
         case 32:
            mts32_init(cpu);
            mts32_init_memop_vectors(cpu);
            break;
         case 64:
            mts64_init(cpu);
            mts64_init_memop_vectors(cpu);
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

/* === Operations on physical memory ====================================== */

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
