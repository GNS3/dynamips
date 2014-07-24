/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __MIPS_64_H__
#define __MIPS_64_H__

#include <pthread.h>

#include "utils.h" 
#include "rbtree.h"

/* 
 * MIPS General Purpose Registers 
 */
#define MIPS_GPR_ZERO        0             /*  zero  */
#define MIPS_GPR_AT          1             /*  at  */
#define MIPS_GPR_V0          2             /*  v0  */
#define MIPS_GPR_V1          3             /*  v1  */
#define MIPS_GPR_A0          4             /*  a0  */
#define MIPS_GPR_A1          5             /*  a1  */
#define MIPS_GPR_A2          6             /*  a2  */
#define MIPS_GPR_A3          7             /*  a3  */
#define MIPS_GPR_T0          8             /*  t0  */
#define MIPS_GPR_T1          9             /*  t1  */
#define MIPS_GPR_T2          10            /*  t2  */
#define MIPS_GPR_T3          11            /*  t3  */
#define MIPS_GPR_T4          12            /*  t4  */
#define MIPS_GPR_T5          13            /*  t5  */
#define MIPS_GPR_T6          14            /*  t6  */
#define MIPS_GPR_T7          15            /*  t7  */
#define MIPS_GPR_S0          16            /*  s0  */
#define MIPS_GPR_S1          17            /*  s1  */
#define MIPS_GPR_S2          18            /*  s2  */
#define MIPS_GPR_S3          19            /*  s3  */
#define MIPS_GPR_S4          20            /*  s4  */
#define MIPS_GPR_S5          21            /*  s5  */
#define MIPS_GPR_S6          22            /*  s6  */
#define MIPS_GPR_S7          23            /*  s7  */
#define MIPS_GPR_T8          24            /*  t8  */
#define MIPS_GPR_T9          25            /*  t9  */
#define MIPS_GPR_K0          26            /*  k0  */
#define MIPS_GPR_K1          27            /*  k1  */
#define MIPS_GPR_GP          28            /*  gp  */
#define MIPS_GPR_SP          29            /*  sp  */
#define MIPS_GPR_FP          30            /*  fp  */
#define MIPS_GPR_RA          31            /*  ra  */

/*
 * Coprocessor 0 (System Coprocessor) Register definitions
 */
#define MIPS_CP0_INDEX       0             /* TLB Index           */
#define MIPS_CP0_RANDOM      1             /* TLB Random          */
#define MIPS_CP0_TLB_LO_0    2             /* TLB Entry Lo0       */
#define MIPS_CP0_TLB_LO_1    3             /* TLB Entry Lo1       */
#define MIPS_CP0_CONTEXT     4             /* Kernel PTE pointer  */
#define MIPS_CP0_PAGEMASK    5             /* TLB Page Mask       */
#define MIPS_CP0_WIRED       6             /* TLB Wired           */
#define MIPS_CP0_INFO        7             /* Info (RM7000)       */
#define MIPS_CP0_BADVADDR    8             /* Bad Virtual Address */
#define MIPS_CP0_COUNT       9             /* Count               */
#define MIPS_CP0_TLB_HI      10            /* TLB Entry Hi        */
#define MIPS_CP0_COMPARE     11            /* Timer Compare       */
#define MIPS_CP0_STATUS      12            /* Status              */
#define MIPS_CP0_CAUSE       13            /* Cause               */
#define MIPS_CP0_EPC         14            /* Exception PC        */
#define MIPS_CP0_PRID        15            /* Proc Rev ID         */
#define MIPS_CP0_CONFIG      16            /* Configuration       */
#define MIPS_CP0_LLADDR      17            /* Load/Link address   */
#define MIPS_CP0_WATCHLO     18            /* Low Watch address   */
#define MIPS_CP0_WATCHHI     19            /* High Watch address  */
#define MIPS_CP0_XCONTEXT    20            /* Extended context    */
#define MIPS_CP0_ECC         26            /* ECC and parity      */
#define MIPS_CP0_CACHERR     27            /* Cache Err/Status    */
#define MIPS_CP0_TAGLO       28            /* Cache Tag Lo        */
#define MIPS_CP0_TAGHI       29            /* Cache Tag Hi        */
#define MIPS_CP0_ERR_EPC     30            /* Error exception PC  */

/*
 * CP0 Set 1 Registers (R7000)
 */
#define MIPS_CP0_S1_CONFIG     16          /* Configuration Register */
#define MIPS_CP0_S1_IPLLO      18          /* Priority level for IRQ [7:0] */
#define MIPS_CP0_S1_IPLHI      19          /* Priority level for IRQ [15:8] */
#define MIPS_CP0_S1_INTCTL     20          /* Interrupt Control */
#define MIPS_CP0_S1_DERRADDR0  26          /* Imprecise Error Address */
#define MIPS_CP0_S1_DERRADDR1  27          /* Imprecise Error Address */

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

/* CP0 Context register */
#define MIPS_CP0_CONTEXT_VPN2_MASK       0xffffe000ULL  /* applied to addr */
#define MIPS_CP0_CONTEXT_BADVPN2_MASK    0x7fffffULL
#define MIPS_CP0_CONTEXT_BADVPN2_SHIFT   4

/* CP0 XContext register */
#define MIPS_CP0_XCONTEXT_VPN2_MASK      0xffffffe000ULL
#define MIPS_CP0_XCONTEXT_RBADVPN2_MASK  0x1ffffffffULL
#define MIPS_CP0_XCONTEXT_BADVPN2_SHIFT  4
#define MIPS_CP0_XCONTEXT_R_SHIFT        31

/* TLB masks and shifts */
#define MIPS_TLB_PAGE_MASK     0x01ffe000ULL
#define MIPS_TLB_PAGE_SHIFT    13
#define MIPS_TLB_VPN2_MASK_32  0xffffe000ULL
#define MIPS_TLB_VPN2_MASK_64  0xc00000ffffffe000ULL
#define MIPS_TLB_PFN_MASK      0x3fffffc0ULL
#define MIPS_TLB_ASID_MASK     0x000000ff     /* "asid" in EntryHi */
#define MIPS_TLB_G_MASK        0x00001000ULL  /* "Global" in EntryHi */
#define MIPS_TLB_V_MASK        0x2ULL         /* "Valid" in EntryLo */
#define MIPS_TLB_D_MASK        0x4ULL         /* "Dirty" in EntryLo */
#define MIPS_TLB_C_MASK        0x38ULL        /* Page Coherency Attribute */
#define MIPS_TLB_C_SHIFT       3

#define MIPS_CP0_LO_G_MASK     0x00000001ULL  /* "Global" in Lo0/1 reg */
#define MIPS_CP0_LO_SAFE_MASK  0x3fffffffULL  /* Safety mask for Lo reg */
#define MIPS_CP0_HI_SAFE_MASK  0xc00000ffffffe0ffULL  /* Same for EntryHi */

/* results for TLB lookups */
enum {
   MIPS_TLB_LOOKUP_OK = 0,     /* Entry found */
   MIPS_TLB_LOOKUP_INVALID,    /* Invalid entry found */
   MIPS_TLB_LOOKUP_MISS,       /* No matching entry found */
   MIPS_TLB_LOOKUP_MOD,        /* Read-only */
};

/* Exceptions vectors */
enum {
   MIPS_EXCVECT_RST = 0,          /* Soft Reset, Reset, NMI */
   MIPS_EXCVECT_TLB_REFILL,       /* TLB Refill (32-bit) */
   MIPS_EXCVECT_XTLB_REFILL,      /* TLB Refill (64-bit) */
   MIPS_EXCVECT_CACHE_ERR,        /* Cache Error */
   MIPS_EXCVECT_INT_IV0,          /* Interrupt, IV=0 */
   MIPS_EXCVECT_INT_IV1,          /* Interrupt, IV=1 */
   MIPS_EXCVECT_OTHERS,           /* Other exceptions */
};

/* MIPS "jr ra" instruction */
#define MIPS_INSN_JR_RA        0x03e00008

/* Minimum page size: 4 Kb */
#define MIPS_MIN_PAGE_SHIFT    12
#define MIPS_MIN_PAGE_SIZE     (1 << MIPS_MIN_PAGE_SHIFT)
#define MIPS_MIN_PAGE_IMASK    (MIPS_MIN_PAGE_SIZE - 1)
#define MIPS_MIN_PAGE_MASK     0xfffffffffffff000ULL

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

/* xkphys mask (36-bit physical address) */
#define MIPS64_XKPHYS_ZONE_MASK    0xF800000000000000ULL
#define MIPS64_XKPHYS_PHYS_SIZE    (1ULL << 36)
#define MIPS64_XKPHYS_PHYS_MASK    (MIPS64_XKPHYS_PHYS_SIZE - 1)
#define MIPS64_XKPHYS_CCA_SHIFT    59

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
#define MIPS64_TLB_STD_ENTRIES  48
#define MIPS64_TLB_MAX_ENTRIES  64
#define MIPS64_TLB_IDX_MASK     0x3f   /* 6 bits */

/* Enable the 64 TLB entries for R7000 CPU */
#define MIPS64_R7000_TLB64_ENABLE   0x20000000

/* Number of instructions per page */
#define MIPS_INSN_PER_PAGE (MIPS_MIN_PAGE_SIZE/sizeof(mips_insn_t))

/* MIPS CPU Identifiers */
#define MIPS_PRID_R4600    0x00002012
#define MIPS_PRID_R4700    0x00002112
#define MIPS_PRID_R5000    0x00002312
#define MIPS_PRID_R7000    0x00002721
#define MIPS_PRID_R527x    0x00002812
#define MIPS_PRID_BCM1250  0x00040102

/* Memory operations */
enum {
   MIPS_MEMOP_LOOKUP = 0,
   MIPS_MEMOP_IFETCH,

   MIPS_MEMOP_LB,
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

/* Maximum number of breakpoints */
#define MIPS64_MAX_BREAKPOINTS  20

/* MIPS CPU type */
typedef struct cpu_mips cpu_mips_t;

/* Memory operation function prototype */
typedef fastcall void (*mips_memop_fn)(cpu_mips_t *cpu,m_uint64_t vaddr,
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
   tlb_entry_t tlb[MIPS64_TLB_MAX_ENTRIES];
   
   /* Number of TLB entries */
   u_int tlb_entries;

   /* Extensions for R7000 CP0 Set1 */
   m_uint32_t ipl_lo,ipl_hi,int_ctl;
   m_uint32_t derraddr0,derraddr1;
}mips_cp0_t;

/* FPU Coprocessor (CP1) definition */
typedef struct {
   m_uint64_t reg[MIPS64_CP1_REG_NR];
}mips_cp1_t;

/* MIPS CPU definition */
struct cpu_mips {
   /* MTS32/MTS64 caches */
   union {
      mts32_entry_t *mts32_cache;
      mts64_entry_t *mts64_cache;
   }mts_u;

   /* Virtual version of CP0 Compare Register */
   m_uint32_t cp0_virt_cnt_reg,cp0_virt_cmp_reg;

   /* General Purpose Registers, Pointer Counter, LO/HI, IRQ */
   m_uint32_t irq_pending,irq_cause,ll_bit;
   m_uint64_t pc,gpr[MIPS64_GPR_NR];
   m_uint64_t lo,hi,ret_pc;
   m_uint32_t exec_state;
   u_int bd_slot;
   
   /* Virtual address to physical page translation */
   fastcall int (*translate)(cpu_mips_t *cpu,m_uint64_t vaddr,
                             m_uint32_t *phys_page);

   /* Memory access functions */
   mips_memop_fn mem_op_fn[MIPS_MEMOP_MAX];

   /* Memory lookup function (to load ELF image,...) and instruction fetch */
   void *(*mem_op_lookup)(cpu_mips_t *cpu,m_uint64_t vaddr);
   void *(*mem_op_ifetch)(cpu_mips_t *cpu,m_uint64_t vaddr);

   /* System coprocessor (CP0) */
   mips_cp0_t cp0;
   
   /* FPU (CP1) */
   mips_cp1_t fpu;

   /* Address bus mask for physical addresses */
   m_uint64_t addr_bus_mask;

   /* IRQ counters and cause */
   m_uint64_t irq_count,timer_irq_count,irq_fp_count;
   pthread_mutex_t irq_lock;

   /* Current and free lists of translated code blocks */
   mips64_jit_tcb_t *tcb_list,*tcb_last,*tcb_free_list;

   /* Executable page area */
   void *exec_page_area;
   size_t exec_page_area_size;
   size_t exec_page_count,exec_page_alloc;
   insn_exec_page_t *exec_page_free_list;
   insn_exec_page_t *exec_page_array;

   /* Idle PC value */
   volatile m_uint64_t idle_pc;

   /* Timer IRQs */
   volatile u_int timer_irq_pending;
   u_int timer_irq_freq;
   u_int timer_irq_check_itv;
   u_int timer_drift;

   /* IRQ disable flag */
   volatile u_int irq_disable;

   /* IRQ idling preemption */
   u_int irq_idle_preempt[8];

   /* Generic CPU instance pointer */
   cpu_gen_t *gen;

   /* VM instance */
   vm_instance_t *vm;

   /* non-JIT mode instruction counter */
   m_uint64_t insn_exec_count;

   /* MTS invalidate/shutdown operations */
   void (*mts_invalidate)(cpu_mips_t *cpu);
   void (*mts_shutdown)(cpu_mips_t *cpu);

   /* MTS cache statistics */
   m_uint64_t mts_misses,mts_lookups;

   /* JIT flush method */
   u_int jit_flush_method;

   /* Number of compiled pages */
   u_int compiled_pages;

   /* Fast memory operations use */
   u_int fast_memop;

   /* Direct block jump */
   u_int exec_blk_direct_jump;

   /* Address mode (32 or 64 bits) */
   u_int addr_mode;

   /* Current exec page (non-JIT) info */
   m_uint64_t njm_exec_page;
   mips_insn_t *njm_exec_ptr;

   /* Performance counter (number of instructions executed by CPU) */
   m_uint32_t perf_counter;

   /* Breakpoints */
   m_uint64_t breakpoints[MIPS64_MAX_BREAKPOINTS];
   u_int breakpoints_enabled;

   /* Symtrace */
   int sym_trace;
   rbtree_tree *sym_tree;

   /* XXX */
   cpu_tb_t *current_tb;
};

#define MIPS64_IRQ_LOCK(cpu)   pthread_mutex_lock(&(cpu)->irq_lock)
#define MIPS64_IRQ_UNLOCK(cpu) pthread_mutex_unlock(&(cpu)->irq_lock)

/* Register names */
extern char *mips64_gpr_reg_names[];

/* Get cacheability info */
int mips64_cca_cached(m_uint8_t val);

/* Reset a MIPS64 CPU */
int mips64_reset(cpu_mips_t *cpu);

/* Initialize a MIPS64 processor */
int mips64_init(cpu_mips_t *cpu);

/* Delete a MIPS64 processor */
void mips64_delete(cpu_mips_t *cpu);

/* Set the CPU PRID register */
void mips64_set_prid(cpu_mips_t *cpu,m_uint32_t prid);

/* Set idle PC value */
void mips64_set_idle_pc(cpu_gen_t *cpu,m_uint64_t addr);

/* Timer IRQ */
void *mips64_timer_irq_run(cpu_mips_t *cpu);

/* Determine an "idling" PC */
int mips64_get_idling_pc(cpu_gen_t *cpu);

/* Set an IRQ (VM IRQ standard routing) */
void mips64_vm_set_irq(vm_instance_t *vm,u_int irq);

/* Clear an IRQ (VM IRQ standard routing) */
void mips64_vm_clear_irq(vm_instance_t *vm,u_int irq);

/* Update the IRQ flag */
void mips64_update_irq_flag(cpu_mips_t *cpu);

/* Generate a general exception */
void mips64_general_exception(cpu_mips_t *cpu,u_int exc_code);

/* Generate a general exception that updates BadVaddr */
void mips64_gen_exception_badva(cpu_mips_t *cpu,u_int exc_code,
                                m_uint64_t bad_vaddr);

/* Generate a TLB/XTLB exception */
void mips64_tlb_miss_exception(cpu_mips_t *cpu,u_int exc_code,
                               m_uint64_t bad_vaddr);

/* Prepare a TLB exception */
void mips64_prepare_tlb_exception(cpu_mips_t *cpu,m_uint64_t vaddr);

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
fastcall void mips64_trigger_trap_exception(cpu_mips_t *cpu);

/* Trigger IRQs */
fastcall void mips64_trigger_irq(cpu_mips_t *cpu);

/* Set an IRQ */
void mips64_set_irq(cpu_mips_t *cpu,m_uint8_t irq);

/* Clear an IRQ */
void mips64_clear_irq(cpu_mips_t *cpu,m_uint8_t irq);

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

/* Add a virtual breakpoint */
int mips64_add_breakpoint(cpu_gen_t *cpu,m_uint64_t pc);

/* Remove a virtual breakpoint */
void mips64_remove_breakpoint(cpu_gen_t *cpu,m_uint64_t pc);

/* Return is the current PC points to a breakpoint */
int mips64_is_breakpoint_at_pc(cpu_mips_t *cpu);

/* Debugging for register-jump to address 0 */
fastcall void mips64_debug_jr0(cpu_mips_t *cpu);

/* Set a register */
void mips64_reg_set(cpu_gen_t *cpu,u_int reg,m_uint64_t val);

/* Dump registers of a MIPS64 processor */
void mips64_dump_regs(cpu_gen_t *cpu);

/* Dump a memory block */
void mips64_dump_memory(cpu_mips_t *cpu,m_uint64_t vaddr,u_int count);

/* Dump the stack */
void mips64_dump_stack(cpu_mips_t *cpu,u_int count);

/* Save the CPU state into a file */
int mips64_save_state(cpu_mips_t *cpu,char *filename);

/* Load a raw image into the simulated memory */
int mips64_load_raw_image(cpu_mips_t *cpu,char *filename,m_uint64_t vaddr);

/* Load an ELF image into the simulated memory */
int mips64_load_elf_image(cpu_mips_t *cpu,char *filename,int skip_load,
                          m_uint32_t *entry_point);

/* Symbol lookup */
struct symbol *mips64_sym_lookup(cpu_mips_t *cpu,m_uint64_t addr);

/* Insert a new symbol */
struct symbol *mips64_sym_insert(cpu_mips_t *cpu,char *name,m_uint64_t addr);

/* Create the symbol tree */
int mips64_sym_create_tree(cpu_mips_t *cpu);

/* Load a symbol file */
int mips64_sym_load_file(cpu_mips_t *cpu,char *filename);

#endif
