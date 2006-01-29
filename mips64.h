/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __MIPS_64_H__
#define __MIPS_64_H__

#include <pthread.h>

#include "utils.h" 
#include "rbtree.h"

/* MIPS General Purpose Registers */
#define MIPS_GPR_ZERO           0               /*  zero  */
#define MIPS_GPR_AT             1               /*  at  */
#define MIPS_GPR_V0             2               /*  v0  */
#define MIPS_GPR_V1             3               /*  v1  */
#define MIPS_GPR_A0             4               /*  a0  */
#define MIPS_GPR_A1             5               /*  a1  */
#define MIPS_GPR_A2             6               /*  a2  */
#define MIPS_GPR_A3             7               /*  a3  */
#define MIPS_GPR_T0             8               /*  t0  */
#define MIPS_GPR_T1             9               /*  t1  */
#define MIPS_GPR_T2             10              /*  t2  */
#define MIPS_GPR_T3             11              /*  t3  */
#define MIPS_GPR_T4             12              /*  t4  */
#define MIPS_GPR_T5             13              /*  t5  */
#define MIPS_GPR_T6             14              /*  t6  */
#define MIPS_GPR_T7             15              /*  t7  */
#define MIPS_GPR_S0             16              /*  s0  */
#define MIPS_GPR_S1             17              /*  s1  */
#define MIPS_GPR_S2             18              /*  s2  */
#define MIPS_GPR_S3             19              /*  s3  */
#define MIPS_GPR_S4             20              /*  s4  */
#define MIPS_GPR_S5             21              /*  s5  */
#define MIPS_GPR_S6             22              /*  s6  */
#define MIPS_GPR_S7             23              /*  s7  */
#define MIPS_GPR_T8             24              /*  t8  */
#define MIPS_GPR_T9             25              /*  t9  */
#define MIPS_GPR_K0             26              /*  k0  */
#define MIPS_GPR_K1             27              /*  k1  */
#define MIPS_GPR_GP             28              /*  gp  */
#define MIPS_GPR_SP             29              /*  sp  */
#define MIPS_GPR_FP             30              /*  fp  */
#define MIPS_GPR_RA             31              /*  ra  */

/*
 * Coprocessor 0 (System Coprocessor) Register definitions
 */
#define MIPS_CP0_INDEX      0             /* TLB Index           */
#define MIPS_CP0_RANDOM     1             /* TLB Random          */
#define MIPS_CP0_TLB_LO_0   2             /* TLB Entry Lo0       */
#define MIPS_CP0_TLB_LO_1   3             /* TLB Entry Lo1       */
#define MIPS_CP0_CONTEXT    4             /* Kernel PTE pointer  */
#define MIPS_CP0_PAGEMASK   5             /* TLB Page Mask       */
#define MIPS_CP0_WIRED      6             /* TLB Wired           */
#define MIPS_CP0_BADVADDR   8             /* Bad Virtual Address */
#define MIPS_CP0_COUNT      9             /* Count               */
#define MIPS_CP0_TLB_HI     10            /* TLB Entry Hi        */
#define MIPS_CP0_COMPARE    11            /* Timer Compare       */
#define MIPS_CP0_STATUS     12            /* Status              */
#define MIPS_CP0_CAUSE      13            /* Cause               */
#define MIPS_CP0_EPC        14            /* Exception PC        */
#define MIPS_CP0_PRID       15            /* Proc Rev ID         */
#define MIPS_CP0_CONFIG     16            /* Configuration       */
#define MIPS_CP0_LLADDR     17            /* Load/Link address   */
#define MIPS_CP0_WATCHLO    18            /* Low Watch address   */
#define MIPS_CP0_WATCHHI    19            /* High Watch address  */
#define MIPS_CP0_XCONTEXT   20            /* Extended context    */
#define MIPS_CP0_ECC        26            /* ECC and parity      */
#define MIPS_CP0_CACHERR    27            /* Cache Err/Status    */
#define MIPS_CP0_TAGLO      28            /* Cache Tag Lo        */
#define MIPS_CP0_TAGHI      29            /* Cache Tag Hi        */
#define MIPS_CP0_ERR_EPC    30            /* Error exception PC  */

/*
 * CP0 Status Register
 */
#define MIPS_CP0_STATUS_CU0        0x10000000
#define MIPS_CP0_STATUS_CU1        0x20000000
#define MIPS_CP0_STATUS_BEV        0x00400000
#define MIPS_CP0_STATUS_TS         0x00200000
#define MIPS_CP0_STATUS_SR         0x00100000
#define MIPS_CP0_STATUS_CH         0x00040000
#define MIPS_CP0_STATUS_CE         0x00020000
#define MIPS_CP0_STATUS_DE         0x00010000
#define MIPS_CP0_STATUS_RP         0x08000000
#define MIPS_CP0_STATUS_FR         0x04000000
#define MIPS_CP0_STATUS_RE         0x02000000
#define MIPS_CP0_STATUS_KX         0x00000080
#define MIPS_CP0_STATUS_SX         0x00000040
#define MIPS_CP0_STATUS_UX         0x00000020
#define MIPS_CP0_STATUS_KSU        0x00000018
#define MIPS_CP0_STATUS_ERL        0x00000004
#define MIPS_CP0_STATUS_EXL        0x00000002
#define MIPS_CP0_STATUS_IE         0x00000001
#define MIPS_CP0_STATUS_IMASK8     0x00000000
#define MIPS_CP0_STATUS_IMASK7     0x00008000
#define MIPS_CP0_STATUS_IMASK6     0x00004000
#define MIPS_CP0_STATUS_IMASK5     0x00002000
#define MIPS_CP0_STATUS_IMASK4     0x00001000
#define MIPS_CP0_STATUS_IMASK3     0x00000800
#define MIPS_CP0_STATUS_IMASK2     0x00000400
#define MIPS_CP0_STATUS_IMASK1     0x00000200
#define MIPS_CP0_STATUS_IMASK0     0x00000100

#define MIPS_CP0_STATUS_DS_MASK    0x00770000
#define MIPS_CP0_STATUS_CU_MASK    0xF0000000
#define MIPS_CP0_STATUS_IMASK      0x0000FF00

/* Addressing mode: Kernel, Supervisor and User */
#define MIPS_CP0_STATUS_KSU_SHIFT  0x03
#define MIPS_CP0_STATUS_KSU_MASK   0x03

#define MIPS_CP0_STATUS_KM  0x00
#define MIPS_CP0_STATUS_SM  0x01
#define MIPS_CP0_STATUS_UM  0x10


/*
 * CP0 Cause register
 */
#define MIPS_CP0_CAUSE_BD_SLOT        0x80000000

#define MIPS_CP0_CAUSE_MASK           0x0000007C
#define MIPS_CP0_CAUSE_CEMASK         0x30000000
#define MIPS_CP0_CAUSE_IMASK          0x0000FF00

#define MIPS_CP0_CAUSE_SHIFT          2
#define MIPS_CP0_CAUSE_CESHIFT        28
#define MIPS_CP0_CAUSE_ISHIFT         8

#define MIPS_CP0_CAUSE_INTERRUPT      0
#define MIPS_CP0_CAUSE_TLB_MOD        1
#define MIPS_CP0_CAUSE_TLB_LOAD       2
#define MIPS_CP0_CAUSE_TLB_SAVE       3
#define MIPS_CP0_CAUSE_ADDR_LOAD      4    /* ADEL */
#define MIPS_CP0_CAUSE_ADDR_SAVE      5    /* ADES */
#define MIPS_CP0_CAUSE_BUS_INSTR      6
#define MIPS_CP0_CAUSE_BUS_DATA       7
#define MIPS_CP0_CAUSE_SYSCALL        8
#define MIPS_CP0_CAUSE_BP             9
#define MIPS_CP0_CAUSE_ILLOP          10
#define MIPS_CP0_CAUSE_CP_UNUSABLE    11
#define MIPS_CP0_CAUSE_OVFLW          12
#define MIPS_CP0_CAUSE_TRAP           13
#define MIPS_CP0_CAUSE_VC_INSTR       14   /* Virtual Coherency */
#define MIPS_CP0_CAUSE_FPE            15
#define MIPS_CP0_CAUSE_WATCH          23
#define MIPS_CP0_CAUSE_VC_DATA        31   /* Virtual Coherency */

#define MIPS_CP0_CAUSE_IBIT7     0x00008000
#define MIPS_CP0_CAUSE_IBIT6     0x00004000
#define MIPS_CP0_CAUSE_IBIT5     0x00002000
#define MIPS_CP0_CAUSE_IBIT4     0x00001000
#define MIPS_CP0_CAUSE_IBIT3     0x00000800
#define MIPS_CP0_CAUSE_IBIT2     0x00000400
#define MIPS_CP0_CAUSE_IBIT1     0x00000200
#define MIPS_CP0_CAUSE_IBIT0     0x00000100


/* TLB masks and shifts */
#define MIPS_TLB_PAGE_MASK     0x01ffe000
#define MIPS_TLB_PAGE_SHIFT    13
#define MIPS_TLB_VPN2_MASK     0xffffffffffffe000ULL
#define MIPS_TLB_PFN_MASK      0x3fffffc0
#define MIPS_TLB_ASID_MASK     0x000000ff       /* "asid" in EntryHi */
#define MIPS_TLB_G_MASK        0x00001000       /* "Global" in EntryHi */
#define MIPS_TLB_V_MASK        0x2              /* "Valid" in EntryLo */
#define MIPS_TLB_D_MASK        0x4              /* "Dirty" in EntryLo */

#define MIPS_CP0_LO_G_MASK     0x00000001     /* "Global" in Lo0/1 reg */
#define MIPS_CP0_HI_SAFE_MASK  0xffffe0ff     /* Safety mask for Hi reg */
#define MIPS_CP0_LO_SAFE_MASK  0x7fffffff     /* Safety mask for Lo reg */

/* MIPS "jr ra" instruction */
#define MIPS_INSN_JR_RA   0x03e00008

/* Minimum page size: 4 Kb */
#define MIPS_MIN_PAGE_SIZE  (1 << 12)

/* Addressing mode: Kernel, Supervisor and User */
#define MIPS_MODE_KERNEL  00

/* Segments in 32-bit User mode */
#define MIPS_USEG_BASE    0x00000000
#define MIPS_USEG_SIZE    0x80000000

/* Segments in 32-bit Supervisor mode */
#define MIPS_SUSEG_BASE   0x00000000
#define MIPS_SUSEG_SIZE   0x80000000
#define MIPS_SSEG_BASE    0xc0000000
#define MIPS_SSEG_SIZE    0x20000000

/* Segments in 32-bit Kernel mode */
#define MIPS_KUSEG_BASE   0x00000000
#define MIPS_KUSEG_SIZE   0x80000000

#define MIPS_KSEG0_BASE   0x80000000
#define MIPS_KSEG0_SIZE   0x20000000

#define MIPS_KSEG1_BASE   0xa0000000
#define MIPS_KSEG1_SIZE   0x20000000

#define MIPS_KSSEG_BASE   0xc0000000
#define MIPS_KSSEG_SIZE   0x20000000

#define MIPS_KSEG3_BASE   0xe0000000
#define MIPS_KSEG3_SIZE   0x20000000

/* Macros for CPU structure access */
#define REG_OFFSET(reg)       (OFFSET(cpu_mips_t,gpr[(reg)]))
#define CP0_REG_OFFSET(c0reg) (OFFSET(cpu_mips_t,cp0.reg[(c0reg)]))
#define MEMOP_OFFSET(op)      (OFFSET(cpu_mips_t,mem_op_fn[(op)]))

/* Initial Program Counter and Stack pointer for ROM */
#define MIPS_ROM_PC  0xffffffffbfc00000ULL
#define MIPS_ROM_SP  0xffffffff80004000ULL

/* Number of GPR (general purpose registers) */
#define MIPS64_GPR_NR  32

/* Number of registers in CP0 */
#define MIPS64_CP0_REG_NR   32

/* Number of registers in CP1 */
#define MIPS64_CP1_REG_NR   32

/* Number of TLB entries */
#define MIPS64_TLB_ENTRIES    64
#define MIPS64_TLB_IDX_MASK   0x3f   /* 6 bits */

/* Virtual CPU states */
enum {
   MIPS_CPU_RUNNING = 0,
   MIPS_CPU_HALTED,
   MIPS_CPU_SUSPENDED,
};

/* Memory operations */
enum {
   MIPS_MEMOP_LB = 0,
   MIPS_MEMOP_LBU,
   MIPS_MEMOP_LH,
   MIPS_MEMOP_LHU,
   MIPS_MEMOP_LW,
   MIPS_MEMOP_LWU,
   MIPS_MEMOP_LD,
   MIPS_MEMOP_SB,
   MIPS_MEMOP_SH,
   MIPS_MEMOP_SW,
   MIPS_MEMOP_SD, 

   MIPS_MEMOP_LWL,
   MIPS_MEMOP_LWR,
   MIPS_MEMOP_LDL,
   MIPS_MEMOP_LDR,
   MIPS_MEMOP_SWL,
   MIPS_MEMOP_SWR,
   MIPS_MEMOP_SDL,
   MIPS_MEMOP_SDR,

   MIPS_MEMOP_LL,
   MIPS_MEMOP_SC,

   MIPS_MEMOP_LDC1,
   MIPS_MEMOP_SDC1,

   MIPS_MEMOP_CACHE,

   MIPS_MEMOP_MAX,
};

/* 6 bits are reserved for device ID (see the memory subsystem) */
#define MIPS64_DEVICE_MAX  (1 << 6)

/* Number of recorded memory accesses (power of two) */
#define MEMLOG_COUNT   16

/* Maximum number of breakpoints */
#define MIPS64_MAX_BREAKPOINTS  8

typedef struct memlog_access memlog_access_t;
struct memlog_access {
   m_uint64_t pc;
   m_uint64_t vaddr;
   m_uint64_t data;
   m_uint32_t data_valid;
   m_uint32_t op_size;
   m_uint32_t op_type;
};

/* MIPS CPU type */
typedef struct cpu_mips cpu_mips_t;

/* Memory operation function prototype */
typedef fastcall u_int (*mips_memop_fn)(cpu_mips_t *cpu,m_uint64_t vaddr,
                                        u_int reg);

/* TLB entry definition */
typedef struct {
   m_uint64_t mask;
   m_uint64_t hi;
   m_uint64_t lo0;
   m_uint64_t lo1;
}tlb_entry_t;

/* System Coprocessor (CP0) definition */
typedef struct {
   m_uint64_t reg[MIPS64_CP0_REG_NR];
   tlb_entry_t tlb[MIPS64_TLB_ENTRIES];
}mips_cp0_t;

/* FPU Coprocessor (CP1) definition */
typedef struct {
   m_uint64_t reg[MIPS64_CP1_REG_NR];
}mips_cp1_t;

/* CPU definition */
struct cpu_mips {
   m_uint64_t gpr[MIPS64_GPR_NR];
   m_uint64_t pc;
   m_uint64_t lo,hi;
   m_uint32_t irq_pending;

   /* Red-Black tree to locate PC */
   rbtree_tree *insn_block_tree;

   /* MTS 1st level array */
   void *mts_l1_ptr;

   /* MTS32 array free list */
   void *mts32_l2_free_list;
   
   /* Memory lookup function (to load ELF image,...) */
   void *(*mem_op_lookup)(cpu_mips_t *cpu,m_uint64_t vaddr);
   
   /* Memory access functions */
   mips_memop_fn mem_op_fn[MIPS_MEMOP_MAX];

   /* LL/SC information */
   u_int ll_bit;

   /* Virtual version of CP0 Compare Register */
   m_uint32_t cp0_virt_cmp_reg;
   m_uint32_t cp0_virt_cnt_reg;

   /* Address bus mask */
   m_uint64_t addr_bus_mask;

   /* System coprocessor (CP0) */
   mips_cp0_t cp0;
   
   /* FPU (CP1) */
   mips_cp1_t fpu;

   /* CPU identifier for MP systems and CPU state */
   u_int id,state;

   /* Next CPU in group */
   cpu_mips_t *next;

   /* Memory mapped devices */
   struct vdevice *dev_array[MIPS64_DEVICE_MAX];

   /* Thread running this CPU */
   pthread_t cpu_thread;

   /* Memory access log for fault debugging */
   u_int memlog_pos;
   memlog_access_t memlog_array[MEMLOG_COUNT];

   /* Breakpoints */
   m_uint64_t breakpoints[MIPS64_MAX_BREAKPOINTS];
   u_int breakpoint_enabled;

   /* non-JIT mode instruction counter */
   m_uint64_t insn_exec_count;
};

/* Register names */
extern char *mips64_gpr_reg_names[];

/* Initialize a MIPS64 processor */
void mips64_init(cpu_mips_t *cpu);

/* Update the IRQ flag */
void mips64_update_irq_flag(cpu_mips_t *cpu);

/* Generate an exception */
void mips64_trigger_exception(cpu_mips_t *cpu,u_int exc_code,int bd_slot);

/*
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
fastcall void mips64_exec_inc_cp0_cnt(cpu_mips_t *cpu);

/* Trigger the Timer IRQ */
fastcall void mips64_trigger_timer_irq(cpu_mips_t *cpu);

/* Execute ERET instruction */
fastcall void mips64_exec_eret(cpu_mips_t *cpu);

/* Execute SYSCALL instruction */
fastcall void mips64_exec_syscall(cpu_mips_t *cpu);

/* Execute BREAK instruction */
fastcall void mips64_exec_break(cpu_mips_t *cpu,u_int code);

/* Trigger a Trap Exception */
asmlinkage void mips64_trigger_trap_exception(cpu_mips_t *cpu);

/* Trigger IRQs */
fastcall void mips64_trigger_irq(cpu_mips_t *cpu);

/* Set an IRQ */
void mips64_set_irq(cpu_mips_t *cpu,u_int irq);

/* Clear an IRQ */
void mips64_clear_irq(cpu_mips_t *cpu,u_int irq);

/* DMFC1 */
fastcall void mips64_exec_dmfc1(cpu_mips_t *cpu,u_int gp_reg,u_int cp1_reg);

/* DMTC1 */
fastcall void mips64_exec_dmtc1(cpu_mips_t *cpu,u_int gp_reg,u_int cp1_reg);

/* MFC1 */
fastcall void mips64_exec_mfc1(cpu_mips_t *cpu,u_int gp_reg,u_int cp1_reg);

/* MTC1 */
fastcall void mips64_exec_mtc1(cpu_mips_t *cpu,u_int gp_reg,u_int cp1_reg);

/* Virtual breakpoint */
fastcall void mips64_run_breakpoint(cpu_mips_t *cpu);

/* Dump registers of a MIPS64 processor */
void mips64_dump_regs(cpu_mips_t *cpu);

/* Dump a memory block */
void mips64_dump_memory(cpu_mips_t *cpu,m_uint64_t vaddr,u_int count);

/* Dump the stack */
void mips64_dump_stack(cpu_mips_t *cpu,u_int count);

/* Save the CPU state into a file */
int mips64_save_state(cpu_mips_t *cpu,char *filename);

#endif
