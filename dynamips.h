/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DYNAMIPS_H__
#define __DYNAMIPS_H__

#include <libelf.h>

#include "utils.h"

/* Debugging flags */
#define DEBUG_BLOCK_SCAN       0
#define DEBUG_BLOCK_COMPILE    0
#define DEBUG_BLOCK_PATCH      0
#define DEBUG_BLOCK_CHUNK      0
#define DEBUG_BLOCK_TIMESTAMP  0   /* block timestamping (little overhead) */
#define DEBUG_SYM_TREE         0   /* use symbol tree (slow) */
#define DEBUG_MTS_MAP_DEV      0
#define DEBUG_MTS_MAP_VIRT     1
#define DEBUG_MTS_ACC_U        1   /* undefined memory */
#define DEBUG_MTS_ACC_T        1   /* tlb exception */
#define DEBUG_MTS_ACC_AE       1   /* address error exception */
#define DEBUG_MTS_DEV          0   /* debugging for device access */
#define DEBUG_MTS_STATS        1   /* MTS cache performance */
#define DEBUG_INSN_PERF_CNT    0   /* Instruction performance counter */
#define DEBUG_BLOCK_PERF_CNT   0   /* Block performance counter */
#define DEBUG_DEV_PERF_CNT     1   /* Device performance counter */
#define DEBUG_TLB_ACTIVITY     0 
#define DEBUG_SYSCALL          0
#define DEBUG_CACHE            0
#define DEBUG_JR0              0   /* Debug register jumps to 0 */

/* Feature flags */
#define MEMLOG_ENABLE          0   /* Memlogger (fast memop must be off) */
#define BREAKPOINT_ENABLE      1   /* Virtual Breakpoints */
#define NJM_STATS_ENABLE       1   /* Non-JIT mode stats (little overhead) */

/* Symbol */
struct symbol {
   m_uint64_t addr;
   char name[0];
};

/* ROM identification tag */
#define ROM_ID  0x1e94b3df

/* Global log file */
extern FILE *log_file;

/* Software version */
extern const char *sw_version;

/* Software version specific tag */
extern const char *sw_version_tag;

/* Command Line long options */
#define OPT_DISK0_SIZE  0x100
#define OPT_DISK1_SIZE  0x101
#define OPT_EXEC_AREA   0x102
#define OPT_IDLE_PC     0x103
#define OPT_TIMER_ITV   0x104
#define OPT_VM_DEBUG    0x105
#define OPT_IOMEM_SIZE  0x106
#define OPT_SPARSE_MEM  0x107

/* Delete all objects */
void dynamips_reset(void);

#endif
