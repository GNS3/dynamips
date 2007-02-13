/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __PPC32_MEM_H__
#define __PPC32_MEM_H__

/* Initialize the MTS subsystem for the specified CPU */
int ppc32_mem_init(cpu_ppc_t *cpu);

/* Free memory used by MTS */
void ppc32_mem_shutdown(cpu_ppc_t *cpu);

/* Invalidate the MTS caches (instruction and data) */
void ppc32_mem_invalidate_cache(cpu_ppc_t *cpu);

/* Set a BAT register */
int ppc32_set_bat(cpu_ppc_t *cpu,struct ppc32_bat_prog *bp);

/* Load BAT registers from a BAT array */
void ppc32_load_bat_array(cpu_ppc_t *cpu,struct ppc32_bat_prog *bp);

/* Get the host address for SDR1 */
int ppc32_set_sdr1(cpu_ppc_t *cpu,m_uint32_t sdr1);

/* Initialize the page table */
int ppc32_init_page_table(cpu_ppc_t *cpu);

/* Map a page */
int ppc32_map_page(cpu_ppc_t *cpu,u_int vsid,m_uint32_t vaddr,m_uint64_t paddr,
                   u_int wimg,u_int pp);

/* Map a memory zone */
int ppc32_map_zone(cpu_ppc_t *cpu,u_int vsid,m_uint32_t vaddr,m_uint64_t paddr,
                   m_uint32_t size,u_int wimg,u_int pp);

/* Get a BAT SPR */
m_uint32_t ppc32_get_bat_spr(cpu_ppc_t *cpu,u_int spr);

/* Set a BAT SPR */
void ppc32_set_bat_spr(cpu_ppc_t *cpu,u_int spr,m_uint32_t val);

/* Initialize memory access vectors */
void ppc32_init_memop_vectors(cpu_ppc_t *cpu);

/* Restart the memory subsystem */
int ppc32_mem_restart(cpu_ppc_t *cpu);

#endif
