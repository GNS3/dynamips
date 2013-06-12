/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __CP0_H__
#define __CP0_H__

#include "utils.h" 

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
int mips64_cp0_tlb_lookup(cpu_mips_t *cpu,m_uint64_t vaddr,mts_map_t *res);

/* Map all TLB entries into the MTS */
void mips64_cp0_map_all_tlb_to_mts(cpu_mips_t *cpu);

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
