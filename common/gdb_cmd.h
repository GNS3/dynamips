#ifndef __GDB_CMD_H__
#define __GDB_CMD_H__

//#include "ppc32_exec.h"
//#include "mips64_exec.h"

#include "gdb_utils.h"
//#include "utils.h"

//typedef struct cpu_gen cpu_gen_t;

// Memory related commands
int gdb_cmd_read_mem(gdb_debug_context_t* ctx);

void gdb_cmd_write_mem(gdb_debug_context_t *ctx);

// CPU related commands
void gdb_cmd_get_cpu_reg_mips64(gdb_debug_context_t* ctx);

void gdb_cmd_get_cpu_reg_ppc32(gdb_debug_context_t* ctx);

void gdb_cmd_get_cpu_regs(gdb_debug_context_t* ctx);

void get_cpu_regs_ppc32(gdb_debug_context_t* ctx);

void get_cpu_regs_mips64(gdb_debug_context_t* ctx);

void gdb_cmd_set_cpu_regs(gdb_debug_context_t* ctx);

void gdb_cmd_set_cpu_reg_mips64(gdb_debug_context_t* ctx);

void gdb_cmd_set_cpu_reg_ppc32(gdb_debug_context_t* ctx);

// Program control related commands

void gdb_cmd_proc_continue(gdb_debug_context_t *ctx, m_uint64_t address, int stepping);

void gdb_cmd_proc_step(gdb_debug_context_t *ctx);

void gdb_ppc32_exec_single_step_cpu(cpu_gen_t *gen);

void gdb_mips64_exec_single_step_cpu(cpu_gen_t *gen);

// Breakpoint and watchpoint related commands

void gdb_cmd_insert_breakpoint(gdb_debug_context_t* ctx);

void gdb_cmd_remove_breakpoint(gdb_debug_context_t* ctx);


/*
 * Define a structure used for communication to a GDB client
 */
typedef struct mips64_comm_context_t_ {
    int regs[32];                       /* CPU registers */
    int sr;                             /* status register */
    int lo;                             /* LO */
    int hi;                             /* HI */
    int bad;                            /* BadVaddr */
    int cause;                          /* Cause */
    int pc;                             /* PC */
    int fp;                             /* Psuedo frame pointer */
} mips64_comm_context_t;

/*
 * Define a structure used for communication to a GDB client
 */
typedef struct ppc_comm_context_t_ {
    int regs[32];                       /* CPU registers */
    int fpr[32];                        /* Floating Point registers */
    int sr;                             /* status register */
    int ia ;                            /* PC */
    int lo;                             /* LO */
    int lr;                             /* link register */
    int xer_ca;                         /*  */
    int xer;                            /* */
    int fp;                             /* Psuedo frame pointer */
} ppc_comm_context_t;

/*
 * The number of registers/bytes used for the communication
 * between the client and server
 */
#define PPC32_COMM_REGS               (PPC32_COMM_REGBYTES/4)
#define PPC32_COMM_REGBYTES           (sizeof(ppc_comm_context_t))

#define MIPS64_COMM_REGS              39
#define MIPS64_COMM_REGBYTES          (MIPS64_COMM_REGS*4)

/*
 * GDB breakpoint/watchpoint types
 */
#define GDB_BREAKPOINT_SW        0
#define GDB_BREAKPOINT_HW        1
#define GDB_WATCHPOINT_WRITE     2
#define GDB_WATCHPOINT_READ      3
#define GDB_WATCHPOINT_ACCESS    4

#endif
