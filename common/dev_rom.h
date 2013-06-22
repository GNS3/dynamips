/*
 * Cisco Router Simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEV_ROM_H__
#define __DEV_ROM_H__

#include <sys/types.h>
#include "utils.h"
#include "cpu.h"
#include "device.h"
#include "net_io.h"
#include "vm.h"

/* MIPS64 ROM */
extern m_uint8_t mips64_microcode[];
extern ssize_t mips64_microcode_len;

/* PPC32 ROM */
extern m_uint8_t ppc32_microcode[];
extern ssize_t ppc32_microcode_len;

/* Initialize a ROM zone */
int dev_rom_init(vm_instance_t *vm,char *name,m_uint64_t paddr,m_uint32_t len,
                 m_uint8_t *rom_data,ssize_t rom_data_size);

#endif
