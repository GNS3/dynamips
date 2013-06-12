/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __CP0_H__
#define __CP0_H__

#include "utils.h" 

#define TLB_ZONE_ADD 0
#define TLB_ZONE_DELETE 1

/* Update the Context register with a faulty address */
static inline 
void mips64_cp0_update_context_reg(cpu_mips_t *cpu,m_uint64_t addr)
{
   m_uint64_t badvpn2;
   
   badvpn2 = addr & MIPS_CP0_CONTEXT_VPN2_MASK;
   badvpn2 <<= MIPS_CP0_CONTEXT_BADVPN2_SHIFT;
   
   cpu->cp0.reg[MIPS_CP0_CONTEXT] &= ~MIPS_CP0_CONTEXT_BADVPN2_MASK;
   cpu->cp0.reg[MIPS_CP0_CONTEXT] |= badvpn2;
}

/* Update the XContext register with a faulty address */
static inline 
void mips64_cp0_update_xcontext_reg(cpu_mips_t *cpu,m_uint64_t addr)
{
   m_uint64_t rbadvpn2;
   
   rbadvpn2 = addr & MIPS_CP0_XCONTEXT_VPN2_MASK;
   rbadvpn2 <<= MIPS_CP0_XCONTEXT_BADVPN2_SHIFT;
   rbadvpn2 |= ((addr >> 62) & 0x03) << MIPS_CP0_XCONTEXT_R_SHIFT;
   
   cpu->cp0.reg[MIPS_CP0_XCONTEXT] &= ~MIPS_CP0_XCONTEXT_RBADVPN2_MASK;
   cpu->cp0.reg[MIPS_CP0_XCONTEXT] |= rbadvpn2;
}

/* Get the CPU operating mode (User,Supervisor or Kernel) */
static forced_inline u_int mips64_cp0_get_mode(cpu_mips_t *cpu)
{  
   mips_cp0_t *cp0 = &cpu->cp0;
   u_int cpu_mode;

   cpu_mode = cp0->reg[MIPS_CP0_STATUS] >> MIPS_CP0_STATUS_KSU_SHIFT;
   cpu_mode &= MIPS_CP0_STATUS_KSU_MASK;
   return(cpu_mode);
}

/* Get the VPN2 mask */
static forced_inline m_uint64_t mips64_cp0_get_vpn2_mask(cpu_mips_t *cpu)
{
   if (cpu->addr_mode == 64)
      return(MIPS_TLB_VPN2_MASK_64);
   else
      return(MIPS_TLB_VPN2_MASK_32);
}

/* CP0 register names */
extern char *mips64_cp0_reg_names[];

/* Get cp0 register index given its name */
int mips64_cp0_get_reg_index(char *name);

/* Get the CPU operating mode (User,Supervisor or Kernel) */
u_int mips64_cp0_get_mode(cpu_mips_t *cpu);

/* Get a cp0 register */
m_uint64_t mips64_cp0_get_reg(cpu_mips_t *cpu,u_int cp0_reg);

/* DMFC0 */
fastcall void mips64_cp0_exec_dmfc0(cpu_mips_t *cpu,u_int gp_reg,
                                    u_int cp0_reg);

/* DMTC0 */
fastcall void mips64_cp0_exec_dmtc0(cpu_mips_t *cpu,u_int gp_reg,
                                    u_int cp0_reg);

/* MFC0 */
fastcall void mips64_cp0_exec_mfc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg);

/* MTC0 */
fastcall void mips64_cp0_exec_mtc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg);

/* CFC0 */
fastcall void mips64_cp0_exec_cfc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg);

/* CTC0 */
fastcall void mips64_cp0_exec_ctc0(cpu_mips_t *cpu,u_int gp_reg,u_int cp0_reg);

/* TLB lookup */
int mips64_cp0_tlb_lookup(cpu_mips_t *cpu,m_uint64_t vaddr,u_int op_type,
                          mts_map_t *res);

/* TLBP: Probe a TLB entry */
fastcall void mips64_cp0_exec_tlbp(cpu_mips_t *cpu);

/* TLBR: Read Indexed TLB entry */
fastcall void mips64_cp0_exec_tlbr(cpu_mips_t *cpu);

/* TLBWI: Write Indexed TLB entry */
fastcall void mips64_cp0_exec_tlbwi(cpu_mips_t *cpu);

/* TLBWR: Write Random TLB entry */
fastcall void mips64_cp0_exec_tlbwr(cpu_mips_t *cpu);

/* Raw dump of the TLB */
void mips64_tlb_raw_dump(cpu_gen_t *cpu);

/* Dump the specified TLB entry */
void mips64_tlb_dump_entry(cpu_mips_t *cpu,u_int index);

/* Human-Readable dump of the TLB */
void mips64_tlb_dump(cpu_gen_t *cpu);

#endif
