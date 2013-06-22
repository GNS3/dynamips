/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __MIPS64_MEM_H__
#define __MIPS64_MEM_H__

/* Shutdown the MTS subsystem */
void mips64_mem_shutdown(cpu_mips_t *cpu);

/* Set the address mode */
int mips64_set_addr_mode(cpu_mips_t *cpu,u_int addr_mode);

#endif
