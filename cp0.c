/*
 * Cisco 7200 (Predator) simulation platform.
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

#include "rbtree.h"
#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "cp0.h"

/* MIPS cp0 registers names */
char *mips64_cp0_reg_names[MIPS64_CP0_REG_NR] = {
   "index" , 
   "random", 
   "entry_lo0", 
   "entry_lo1", 
   "context", 
   "pagemask",
   "wired", 
   "cp0_undef_7",
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
   "cp0_undef_21",
   "cp0_undef_22",
   "cp0_undef_23",
   "cp0_undef_24",
   "cp0_undef_25",
   "ecc", 
   "cache_err", 
   "tag_lo", 
   "tag_hi", 
   "err_epc",
   "cp0_undef_31",
};

/* Get cp0 register index given its name */
int cp0_get_reg_index(char *name)
{
   int i;

   for(i=0;i<MIPS64_CP0_REG_NR;i++)
      if (!strcmp(mips64_cp0_reg_names[i],name))
         return(i);

   return(-1);
}

/* Check that we are running in kernel mode */
int cp0_check_kernel_mode(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   u_int cpu_mode;

   cpu_mode = cp0->reg[MIPS_CP0_STATUS] >> MIPS_CP0_STATUS_KSU_SHIFT;
   cpu_mode &= MIPS_CP0_STATUS_KSU_MASK;

   if (cpu_mode != MIPS_CP0_STATUS_KM) {
      /* XXX Branch delay slot */
      mips64_trigger_exception(cpu,MIPS_CP0_CAUSE_ILLOP,0);
      return(1);
   }

   return(0);
}

/* Get a cp0 register */
static inline m_uint64_t cp0_get_reg(cpu_mips_t *cpu,u_int cp0_reg)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint32_t delta;

   switch(cp0_reg) {
      case MIPS_CP0_COUNT:
         delta = cpu->cp0_virt_cmp_reg - cpu->cp0_virt_cnt_reg;
         return(cp0->reg[MIPS_CP0_COMPARE] - (clock_divisor * delta));

      case MIPS_CP0_COMPARE:
         delta = cpu->cp0_virt_cmp_reg - cpu->cp0_virt_cnt_reg;
         return(cp0->reg[MIPS_CP0_COUNT] + (clock_divisor * delta));

      default:
         return(cp0->reg[cp0_reg]);
   }
}

/* Set a cp0 register */
static inline void cp0_set_reg(cpu_mips_t *cpu,u_int cp0_reg,m_uint64_t val)
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
         /* read only register */
         break;

      case MIPS_CP0_WIRED:
         cp0->reg[cp0_reg] = val & MIPS64_TLB_IDX_MASK;
         break;
         
      case MIPS_CP0_COMPARE:
         cp0->reg[MIPS_CP0_CAUSE] &= ~MIPS_CP0_CAUSE_IBIT7;
         mips64_update_irq_flag(cpu);
         cp0->reg[cp0_reg] = val;

         delta = val - cp0->reg[MIPS_CP0_COUNT];
         cpu->cp0_virt_cnt_reg = 0;
         cpu->cp0_virt_cmp_reg = delta / clock_divisor;
         break;

      case MIPS_CP0_COUNT:
         cp0->reg[cp0_reg] = val;

         delta = cp0->reg[MIPS_CP0_COMPARE] - val;
         cpu->cp0_virt_cnt_reg = 0;
         cpu->cp0_virt_cmp_reg = delta / clock_divisor;
         break;

      default:
         cp0->reg[cp0_reg] = val;
   }
}

/* DMFC0 */
fastcall void cp0_exec_dmfc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg)
{
   cpu->gpr[gp_reg] = cp0_get_reg(cpu,cp0_reg);
}

/* DMTC0 */
fastcall void cp0_exec_dmtc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg)
{
   cp0_set_reg(cpu,cp0_reg,cpu->gpr[gp_reg]);
}

/* MFC0 */
fastcall void cp0_exec_mfc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg)
{
   cpu->gpr[gp_reg] = sign_extend(cp0_get_reg(cpu,cp0_reg),32);
}

/* MTC0 */
fastcall void cp0_exec_mtc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg)
{
   cp0_set_reg(cpu,cp0_reg,cpu->gpr[gp_reg] & 0xffffffff);
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

/* 
 * Map a TLB entry into the MTS.
 *
 * We apply the physical address bus masking here.
 */
void cp0_map_tlb_to_mts(cpu_mips_t *cpu,u_int index)
{
   m_uint64_t v0_addr,v1_addr,p0_addr,p1_addr;
   m_uint32_t page_size;
   tlb_entry_t *entry;

   entry = &cpu->cp0.tlb[index];

   page_size = get_page_size(entry->mask);
   v0_addr = entry->hi & MIPS_TLB_VPN2_MASK;
   v1_addr = v0_addr + page_size;

   if (entry->lo0 & MIPS_TLB_V_MASK) {
      p0_addr = (entry->lo0 & MIPS_TLB_PFN_MASK) << 6;
      mts32_map_phys_addr(cpu,v0_addr,p0_addr & cpu->addr_bus_mask,page_size);
   }

   if (entry->lo1 & MIPS_TLB_V_MASK) {
      p1_addr = (entry->lo1 & MIPS_TLB_PFN_MASK) << 6;
      mts32_map_phys_addr(cpu,v1_addr,p1_addr & cpu->addr_bus_mask,page_size);
   }
}

/* Map all TLB entries into the MTS */
void cp0_map_all_tlb_to_mts(cpu_mips_t *cpu)
{   
   u_int i;

   for(i=0;i<MIPS64_TLB_ENTRIES;i++)
      cp0_map_tlb_to_mts(cpu,i);
}

/* TLBP: Probe a TLB entry */
fastcall void cp0_exec_tlbp(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   m_uint64_t hi_reg,asid,vpn2;
   tlb_entry_t *entry;
   u_int i;
  
   hi_reg = cp0->reg[MIPS_CP0_TLB_HI];
   asid = hi_reg & MIPS_TLB_ASID_MASK;
   vpn2 = hi_reg & MIPS_TLB_VPN2_MASK;

   cp0->reg[MIPS_CP0_INDEX] = 0xffffffff80000000ULL;
   
   for(i=0;i<MIPS64_TLB_ENTRIES;i++) {
      entry = &cp0->tlb[i];

      if (((entry->hi & MIPS_TLB_VPN2_MASK) == vpn2) &&
          ((entry->hi & MIPS_TLB_G_MASK) || 
           ((entry->hi & MIPS_TLB_ASID_MASK) == asid)))
      {
         cp0->reg[MIPS_CP0_INDEX] = i;
         tlb_dump(cpu);
      }
   }
}

/* TLBR: Read Indexed TLB entry */
fastcall void cp0_exec_tlbr(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   tlb_entry_t *entry;
   u_int index;

   index = cp0->reg[MIPS_CP0_INDEX];

#if DEBUG_TLB_ACTIVITY
   printf("CP0_TLBR: reading entry %u.\n",index);
#endif

   if (index < MIPS64_TLB_ENTRIES) 
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

/* TLBWI: Write Indexed TLB entry */
fastcall void cp0_exec_tlbwi(cpu_mips_t *cpu)
{
   mips_cp0_t *cp0 = &cpu->cp0;
   tlb_entry_t *entry;
   u_int index;

   index = cp0->reg[MIPS_CP0_INDEX];

#if DEBUG_TLB_ACTIVITY
   printf("CP0_TLBWI: writing entry %u.\n",index);
#endif

   if (index < MIPS64_TLB_ENTRIES) 
   {     
      entry = &cp0->tlb[index];

      entry->mask = cp0->reg[MIPS_CP0_PAGEMASK] & MIPS_TLB_PAGE_MASK;
      entry->hi   = cp0->reg[MIPS_CP0_TLB_HI] & ~entry->mask;
      entry->hi   &= MIPS_CP0_HI_SAFE_MASK;         /* clear G bit */
      entry->lo0  = cp0->reg[MIPS_CP0_TLB_LO_0];
      entry->lo1  = cp0->reg[MIPS_CP0_TLB_LO_1];

      /* if G bit is set in lo0 and lo1, set it in hi */
      if ((entry->lo0 & entry->lo1) & MIPS_CP0_LO_G_MASK)
         entry->hi |= MIPS_TLB_G_MASK;

      /* Clear G bit in TLB lo0 and lo1 */
      entry->lo0 &= ~MIPS_CP0_LO_G_MASK;
      entry->lo1 &= ~MIPS_CP0_LO_G_MASK;

      cp0_map_tlb_to_mts(cpu,index);

#if DEBUG_TLB_ACTIVITY
      tlb_dump_entry(cpu,index);
#endif
   }
}

/* Raw dump of the TLB */
void tlb_raw_dump(cpu_mips_t *cpu)
{
   tlb_entry_t *entry;
   u_int i;

   printf("TLB dump:\n");

   for(i=0;i<MIPS64_TLB_ENTRIES;i++) {
      entry = &cpu->cp0.tlb[i];
      printf(" %2d: mask=0x%16.16llx hi=0x%16.16llx "
             "lo0=0x%16.16llx lo1=0x%16.16llx\n",
             i, entry->mask, entry->hi, entry->lo0, entry->lo1);
   }

   printf("\n");
}

/* Dump the specified TLB entry */
void tlb_dump_entry(cpu_mips_t *cpu,u_int index)
{
   tlb_entry_t *entry;
   char buffer[256];

   entry = &cpu->cp0.tlb[index];

   /* virtual Address */
   printf(" %2d: vaddr=0x%8.8llx ", index, entry->hi & MIPS_TLB_VPN2_MASK);

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
void tlb_dump(cpu_mips_t *cpu)
{
   u_int i;

   printf("TLB dump:\n");

   for(i=0;i<MIPS64_TLB_ENTRIES;i++) 
      tlb_dump_entry(cpu,i);
   
   printf("\n");
}
