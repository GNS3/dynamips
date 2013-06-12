/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * MIPS Coprocessor 0 (System Coprocessor) implementation.
 * We don't use the JIT here, since there is no high performance needed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "device.h"
#include "mips64.h"
#include "mips64_cp0.h"
#include "dynamips.h"
#include "memory.h"

/* MIPS cp0 registers names */
char *mips64_cp0_reg_names[MIPS64_CP0_REG_NR] = {
   "index" , 
   "random", 
   "entry_lo0", 
   "entry_lo1", 
   "context", 
   "pagemask",
   "wired", 
   "info",
   "badvaddr", 
   "count", 
   "entry_hi", 
   "compare", 
   "status", 
   "cause",
   "epc", 
   "prid", 
   "config", 
   "ll_addr", 
   "watch_lo", 
   "watch_hi", 
   "xcontext",
   "cp0_r21",
   "cp0_r22",
   "cp0_r23",
   "cp0_r24",
   "cp0_r25",
   "ecc", 
   "cache_err", 
   "tag_lo", 
   "tag_hi", 
   "err_epc",
   "cp0_r31",
};

/* Get cp0 register index given its name */
int mips64_cp0_get_reg_index(char *name)
{
   int i;

   for(i=0;i<MIPS64_CP0_REG_NR;i++)
      if (!strcmp(mips64_cp0_reg_names[i],name))
         return(i);

   return(-1);
}

/* Check that we are running in kernel mode */
int mips64_cp0_check_kernel_mode(cpu_mips_t *cpu)
{
   u_int cpu_mode;

   cpu_mode = mips64_cp0_get_mode(cpu);

   if (cpu_mode != MIPS_CP0_STATUS_KM) {
      mips64_general_exception(cpu,MIPS_CP0_CAUSE_ILLOP);
      return(1);
   }

   return(0);
}

/* Get value of random register */
static inline u_int mips64_cp0_get_random_reg(cpu_mips_t *cpu)
{
   u_int wired;

   /* We use the virtual count register as a basic "random" value */
   wired = cpu->cp0.reg[MIPS_CP0_WIRED];
   return(wired + (cpu->cp0_virt_cnt_reg % (cpu->cp0.tlb_entries - wired)));
}

/* Get a cp0 register (fast version) */
static inline m_uint64_t mips64_cp0_get_reg_fast(cpu_mips_t *cpu,u_int cp0_reg)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint32_t delta,res;

   switch(cp0_reg) {
      case MIPS_CP0_COUNT:
         delta = cpu->cp0_virt_cmp_reg - cpu->cp0_virt_cnt_reg;
         res = (m_uint32_t)cp0->reg[MIPS_CP0_COMPARE];
         res -= cpu->vm->clock_divisor * delta;
         return(sign_extend(res,32));

#if 1
      case MIPS_CP0_COMPARE:
         return(sign_extend(cp0->reg[MIPS_CP0_COMPARE],32));
#else
      /* really useful and logical ? */
      case MIPS_CP0_COMPARE:
         delta = cpu->cp0_virt_cmp_reg - cpu->cp0_virt_cnt_reg;
         res = (m_uint32_t)cp0->reg[MIPS_CP0_COUNT];
         res += (cpu->vm->clock_divisor * delta);
         return(res);
#endif
      case MIPS_CP0_INFO:
         return(MIPS64_R7000_TLB64_ENABLE);

      case MIPS_CP0_RANDOM:        
         return(mips64_cp0_get_random_reg(cpu));

      default:
         return(cp0->reg[cp0_reg]);
   }
}

/* Get a cp0 register */
m_uint64_t mips64_cp0_get_reg(cpu_mips_t *cpu,u_int cp0_reg)
{
   return(mips64_cp0_get_reg_fast(cpu,cp0_reg));
}

/* Set a cp0 register */
static inline void mips64_cp0_set_reg(cpu_mips_t *cpu,u_int cp0_reg,
                                      m_uint64_t val)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint32_t delta;

   switch(cp0_reg) {
      case MIPS_CP0_STATUS:
      case MIPS_CP0_CAUSE:
         cp0->reg[cp0_reg] = val;
         mips64_update_irq_flag(cpu);
         break;
         
      case MIPS_CP0_PAGEMASK:
         cp0->reg[cp0_reg] = val & MIPS_TLB_PAGE_MASK;
         break;
         
      case MIPS_CP0_COMPARE:
         mips64_clear_irq(cpu,7);
         mips64_update_irq_flag(cpu);
         cp0->reg[cp0_reg] = val;

         delta = val - cp0->reg[MIPS_CP0_COUNT];
         cpu->cp0_virt_cnt_reg = 0;
         cpu->cp0_virt_cmp_reg = delta / cpu->vm->clock_divisor;
         break;

      case MIPS_CP0_COUNT:
         cp0->reg[cp0_reg] = val;

         delta = cp0->reg[MIPS_CP0_COMPARE] - val;
         cpu->cp0_virt_cnt_reg = 0;
         cpu->cp0_virt_cmp_reg = delta / cpu->vm->clock_divisor;
         break;

     case MIPS_CP0_TLB_HI:
         cp0->reg[cp0_reg] = val & MIPS_CP0_HI_SAFE_MASK;
         break;

      case MIPS_CP0_TLB_LO_0:
      case MIPS_CP0_TLB_LO_1:
         cp0->reg[cp0_reg] = val & MIPS_CP0_LO_SAFE_MASK;
         break;

      case MIPS_CP0_RANDOM:
      case MIPS_CP0_PRID:
      case MIPS_CP0_CONFIG:
         /* read only registers */
         break;

      case MIPS_CP0_WIRED:
         cp0->reg[cp0_reg] = val & MIPS64_TLB_IDX_MASK;
         break;

      default:
         cp0->reg[cp0_reg] = val;
   }
}

/* Get a cp0 "set 1" register (R7000) */
m_uint64_t mips64_cp0_s1_get_reg(cpu_mips_t *cpu,u_int cp0_s1_reg)
{
   switch(cp0_s1_reg) {
      case MIPS_CP0_S1_CONFIG:
         return(0x7F << 25);

      case MIPS_CP0_S1_IPLLO:
         return(cpu->cp0.ipl_lo);

      case MIPS_CP0_S1_IPLHI:
         return(cpu->cp0.ipl_hi);

      case MIPS_CP0_S1_INTCTL:
         return(cpu->cp0.int_ctl);

      case MIPS_CP0_S1_DERRADDR0:
         return(cpu->cp0.derraddr0);

      case MIPS_CP0_S1_DERRADDR1:
         return(cpu->cp0.derraddr1);

      default:
         /* undefined register */
         cpu_log(cpu->gen,"CP0_S1","trying to read unknown register %u\n",
                 cp0_s1_reg);
         return(0);
   }
}

/* Set a cp0 "set 1" register (R7000) */
static inline void mips64_cp0_s1_set_reg(cpu_mips_t *cpu,u_int cp0_s1_reg,
                                         m_uint64_t val)
{
   mips_cp0_t *cp0 = &cpu->cp0;

   switch(cp0_s1_reg) {
      case MIPS_CP0_S1_IPLLO:
         cp0->ipl_lo = val;
         break;

      case MIPS_CP0_S1_IPLHI:
         cp0->ipl_hi = val;
         break;

      case MIPS_CP0_S1_INTCTL:
         cp0->int_ctl = val;
         break;

      case MIPS_CP0_S1_DERRADDR0:
         cp0->derraddr0 = val;
         break;
         
      case MIPS_CP0_S1_DERRADDR1:
         cp0->derraddr1 = val;
         break;

      default:
         cpu_log(cpu->gen,
                 "CP0_S1","trying to set unknown register %u (val=0x%x)\n",
                 cp0_s1_reg,val);
   }
}

/* DMFC0 */
fastcall void mips64_cp0_exec_dmfc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg)
{
   cpu->gpr[gp_reg] = mips64_cp0_get_reg_fast(cpu,cp0_reg);
}

/* DMTC0 */
fastcall void mips64_cp0_exec_dmtc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg)
{
   mips64_cp0_set_reg(cpu,cp0_reg,cpu->gpr[gp_reg]);
}

/* MFC0 */
fastcall void mips64_cp0_exec_mfc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg)
{
   cpu->gpr[gp_reg] = sign_extend(mips64_cp0_get_reg_fast(cpu,cp0_reg),32);
}

/* MTC0 */
fastcall void mips64_cp0_exec_mtc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg)
{
   mips64_cp0_set_reg(cpu,cp0_reg,cpu->gpr[gp_reg] & 0xffffffff);
}

/* CFC0 */
fastcall void mips64_cp0_exec_cfc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg)
{
   cpu->gpr[gp_reg] = sign_extend(mips64_cp0_s1_get_reg(cpu,cp0_reg),32);
}

/* CTC0 */
fastcall void mips64_cp0_exec_ctc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg)
{
   mips64_cp0_s1_set_reg(cpu,cp0_reg,cpu->gpr[gp_reg] & 0xffffffff);
}

/* Get the page size corresponding to a page mask */
static inline m_uint32_t get_page_size(m_uint32_t page_mask)
{
   return((page_mask + 0x2000) >> 1);
}

/* Write page size in buffer */
static char *get_page_size_str(char *buffer,size_t len,m_uint32_t page_mask)
{
   m_uint32_t page_size;

   page_size = get_page_size(page_mask);
   
   /* Mb ? */
   if (page_size >= (1024*1024))
      snprintf(buffer,len,"%uMB",page_size >> 20);
   else
      snprintf(buffer,len,"%uKB",page_size >> 10);

   return buffer;
}

/* Execute a callback for the specified entry */
static inline void mips64_cp0_tlb_callback(cpu_mips_t *cpu,tlb_entry_t *entry,
                                           int action)
{
   m_uint64_t vaddr,paddr0,paddr1;
   m_uint32_t psize;

   vaddr = entry->hi & mips64_cp0_get_vpn2_mask(cpu);
   psize = get_page_size(entry->mask);

   if (entry->lo0 & MIPS_TLB_V_MASK) {
      paddr0 = (entry->lo0 & MIPS_TLB_PFN_MASK) << 6;
      
      /*printf("TLB: vaddr=0x%8.8llx -> paddr0=0x%10.10llx (size=0x%8.8x), "
             "action=%s\n",
             vaddr,paddr0,psize,
             (action == 0) ? "ADD" : "DELETE");*/
   }

   if (entry->lo1 & MIPS_TLB_V_MASK) {
      paddr1 = (entry->lo1 & MIPS_TLB_PFN_MASK) << 6;

      /*printf("TLB: vaddr=0x%8.8llx -> paddr1=0x%10.10llx (size=0x%8.8x), "
             "action=%s\n",
             vaddr,paddr1,psize,
             (action == 0) ? "ADD" : "DELETE");*/
   }
}

/* TLB lookup */
int mips64_cp0_tlb_lookup(cpu_mips_t *cpu,m_uint64_t vaddr,
                          u_int op_type,mts_map_t *res)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint64_t vpn_addr,vpn2_mask;
   m_uint64_t page_mask,hi_addr;
   m_uint32_t page_size,pca;
   tlb_entry_t *entry;
   u_int asid;
   int i;

   vpn2_mask = mips64_cp0_get_vpn2_mask(cpu);
   vpn_addr = vaddr & vpn2_mask;

   asid = cp0->reg[MIPS_CP0_TLB_HI] & MIPS_TLB_ASID_MASK;

   for(i=0;i<cp0->tlb_entries;i++) {
      entry = &cp0->tlb[i];

      page_mask = ~entry->mask;
      hi_addr = entry->hi & vpn2_mask & page_mask;

      if (((vpn_addr & page_mask) == hi_addr) &&
          ((entry->hi & MIPS_TLB_G_MASK) ||
           ((entry->hi & MIPS_TLB_ASID_MASK) == asid)))
      {
         page_size = get_page_size(entry->mask);

         if ((vaddr & page_size) == 0) {
            /* Even Page */
            if (entry->lo0 & MIPS_TLB_V_MASK) 
            {
               /* Check write protection */
               if ((op_type == MTS_WRITE) && !(entry->lo0 & MIPS_TLB_D_MASK))
                  return MIPS_TLB_LOOKUP_MOD;
               
               res->flags = 0;
               res->vaddr = vaddr & MIPS_MIN_PAGE_MASK;
               res->paddr = (entry->lo0 & MIPS_TLB_PFN_MASK) << 6;
               res->paddr += (res->vaddr & (page_size-1));
               res->paddr &= cpu->addr_bus_mask;

               res->offset = vaddr & MIPS_MIN_PAGE_IMASK;

               pca = (entry->lo0 & MIPS_TLB_C_MASK);
               pca >>= MIPS_TLB_C_SHIFT;
               res->cached = mips64_cca_cached(pca);
               
               if (!(entry->lo0 & MIPS_TLB_D_MASK))
                  res->flags |= MTS_FLAG_RO;
               
               return(MIPS_TLB_LOOKUP_OK);
            }
         } else {
            /* Odd Page */
            if (entry->lo1 & MIPS_TLB_V_MASK) 
            {
               /* Check write protection */
               if ((op_type == MTS_WRITE) && !(entry->lo1 & MIPS_TLB_D_MASK))
                  return MIPS_TLB_LOOKUP_MOD;

               res->flags = 0;
               res->vaddr = vaddr & MIPS_MIN_PAGE_MASK;
               res->paddr = (entry->lo1 & MIPS_TLB_PFN_MASK) << 6;
               res->paddr += (res->vaddr & (page_size-1));
               res->paddr &= cpu->addr_bus_mask;

               res->offset = vaddr & MIPS_MIN_PAGE_IMASK;

               pca = (entry->lo1 & MIPS_TLB_C_MASK);
               pca >>= MIPS_TLB_C_SHIFT;
               res->cached = mips64_cca_cached(pca);     
                                             
               if (!(entry->lo0 & MIPS_TLB_D_MASK))
                  res->flags |= MTS_FLAG_RO;
                         
               return(MIPS_TLB_LOOKUP_OK);
            }
         }

         /* Invalid entry */
         return(MIPS_TLB_LOOKUP_INVALID);
      }
   }

   /* No matching entry */
   return(MIPS_TLB_LOOKUP_MISS);
}

/* TLBP: Probe a TLB entry */
fastcall void mips64_cp0_exec_tlbp(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint64_t hi_reg,asid;
   m_uint64_t vpn2,vpn2_mask;
   m_uint64_t page_mask;
   tlb_entry_t *entry;
   int i;
  
   vpn2_mask = mips64_cp0_get_vpn2_mask(cpu);
   hi_reg = cp0->reg[MIPS_CP0_TLB_HI];
   asid = hi_reg & MIPS_TLB_ASID_MASK;
   vpn2 = hi_reg & vpn2_mask;

   cp0->reg[MIPS_CP0_INDEX] = 0xffffffff80000000ULL;
   
   for(i=0;i<cp0->tlb_entries;i++) {
      entry = &cp0->tlb[i];
      page_mask = ~entry->mask;

      if (((entry->hi & vpn2_mask & page_mask) == (vpn2 & page_mask)) &&
          ((entry->hi & MIPS_TLB_G_MASK) || 
           ((entry->hi & MIPS_TLB_ASID_MASK) == asid)))
      {
         cp0->reg[MIPS_CP0_INDEX] = i;
#if DEBUG_TLB_ACTIVITY
         printf("CPU: CP0_TLBP returned %u\n",i);
         tlb_dump(cpu);
#endif
      }
   }
}

/* TLBR: Read Indexed TLB entry */
fastcall void mips64_cp0_exec_tlbr(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   tlb_entry_t *entry;
   u_int index;

   index = cp0->reg[MIPS_CP0_INDEX];

#if DEBUG_TLB_ACTIVITY
   cpu_log(cpu,"TLB","CP0_TLBR: reading entry %u.\n",index);
#endif

   if (index < cp0->tlb_entries)
   {
      entry = &cp0->tlb[index];

      cp0->reg[MIPS_CP0_PAGEMASK] = entry->mask;
      cp0->reg[MIPS_CP0_TLB_HI]   = entry->hi;
      cp0->reg[MIPS_CP0_TLB_LO_0] = entry->lo0;
      cp0->reg[MIPS_CP0_TLB_LO_1] = entry->lo1;

      /* 
       * The G bit must be reported in both Lo0 and Lo1 registers,
       * and cleared in Hi register.
       */
      if (entry->hi & MIPS_TLB_G_MASK) {
         cp0->reg[MIPS_CP0_TLB_LO_0] |= MIPS_CP0_LO_G_MASK;
         cp0->reg[MIPS_CP0_TLB_LO_1] |= MIPS_CP0_LO_G_MASK;
         cp0->reg[MIPS_CP0_TLB_HI] &= ~MIPS_TLB_G_MASK;
      }
   }
}

/* TLBW: Write a TLB entry */
static inline void mips64_cp0_exec_tlbw(cpu_mips_t *cpu,u_int index)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   tlb_entry_t *entry;

#if DEBUG_TLB_ACTIVITY
   cpu_log(cpu,"TLB","CP0_TLBWI: writing entry %u "
           "[mask=0x%8.8llx,hi=0x%8.8llx,lo0=0x%8.8llx,lo1=0x%8.8llx]\n",
           index,cp0->reg[MIPS_CP0_PAGEMASK],cp0->reg[MIPS_CP0_TLB_HI],
           cp0->reg[MIPS_CP0_TLB_LO_0],cp0->reg[MIPS_CP0_TLB_LO_1]);
#endif

   if (index < cp0->tlb_entries)
   {
      entry = &cp0->tlb[index];

      mips64_cp0_tlb_callback(cpu,entry,TLB_ZONE_ADD);

      entry->mask = cp0->reg[MIPS_CP0_PAGEMASK] & MIPS_TLB_PAGE_MASK;
      entry->hi   = cp0->reg[MIPS_CP0_TLB_HI];
      entry->lo0  = cp0->reg[MIPS_CP0_TLB_LO_0];
      entry->lo1  = cp0->reg[MIPS_CP0_TLB_LO_1];

      /* if G bit is set in lo0 and lo1, set it in hi */
      if ((entry->lo0 & entry->lo1) & MIPS_CP0_LO_G_MASK)
         entry->hi |= MIPS_TLB_G_MASK;
      else
         entry->hi &= ~MIPS_TLB_G_MASK;

      /* Clear G bit in TLB lo0 and lo1 */
      entry->lo0 &= ~MIPS_CP0_LO_G_MASK;
      entry->lo1 &= ~MIPS_CP0_LO_G_MASK;

      /* Inform the MTS subsystem */
      cpu->mts_invalidate(cpu);

      mips64_cp0_tlb_callback(cpu,entry,TLB_ZONE_DELETE);

#if DEBUG_TLB_ACTIVITY
      mips64_tlb_dump_entry(cpu,index);
#endif
   }
}

/* TLBWI: Write Indexed TLB entry */
fastcall void mips64_cp0_exec_tlbwi(cpu_mips_t *cpu)
{
   mips64_cp0_exec_tlbw(cpu,cpu->cp0.reg[MIPS_CP0_INDEX]);
}

/* TLBWR: Write Random TLB entry */
fastcall void mips64_cp0_exec_tlbwr(cpu_mips_t *cpu)
{
   mips64_cp0_exec_tlbw(cpu,mips64_cp0_get_random_reg(cpu));
}

/* Raw dump of the TLB */
void mips64_tlb_raw_dump(cpu_gen_t *cpu)
{
   cpu_mips_t *mcpu = CPU_MIPS64(cpu);
   tlb_entry_t *entry;
   u_int i;

   printf("TLB dump:\n");

   for(i=0;i<mcpu->cp0.tlb_entries;i++) {
      entry = &mcpu->cp0.tlb[i];
      printf(" %2d: mask=0x%16.16llx hi=0x%16.16llx "
             "lo0=0x%16.16llx lo1=0x%16.16llx\n",
             i, entry->mask, entry->hi, entry->lo0, entry->lo1);
   }

   printf("\n");
}

/* Dump the specified TLB entry */
void mips64_tlb_dump_entry(cpu_mips_t *cpu,u_int index)
{
   tlb_entry_t *entry;
   char buffer[256];

   entry = &cpu->cp0.tlb[index];

   /* virtual Address */
   printf(" %2d: vaddr=0x%8.8llx ", 
          index, entry->hi & mips64_cp0_get_vpn2_mask(cpu));

   /* global or ASID */
   if (entry->hi & MIPS_TLB_G_MASK)
      printf("(global)    ");
   else
      printf("(asid 0x%2.2llx) ",entry->hi & MIPS_TLB_ASID_MASK);

   /* 1st page: Lo0 */
   printf("p0=");

   if (entry->lo0 & MIPS_TLB_V_MASK)
      printf("0x%9.9llx",(entry->lo0 & MIPS_TLB_PFN_MASK) << 6);
   else
      printf("(invalid)  ");            
   
   printf(" %c ",(entry->lo0 & MIPS_TLB_D_MASK) ? 'D' : ' ');
   
   /* 2nd page: Lo1 */
   printf("p1=");

   if (entry->lo1 & MIPS_TLB_V_MASK)
      printf("0x%9.9llx",(entry->lo1 & MIPS_TLB_PFN_MASK) << 6);
   else
      printf("(invalid)  ");            

   printf(" %c ",(entry->lo1 & MIPS_TLB_D_MASK) ? 'D' : ' ');

   /* page size */
   printf(" (%s)\n",get_page_size_str(buffer,sizeof(buffer),entry->mask));
}

/* Human-Readable dump of the TLB */
void mips64_tlb_dump(cpu_gen_t *cpu)
{
   cpu_mips_t *mcpu = CPU_MIPS64(cpu);
   u_int i;

   printf("TLB dump:\n");

   for(i=0;i<mcpu->cp0.tlb_entries;i++) 
      mips64_tlb_dump_entry(mcpu,i);
   
   printf("\n");
}
