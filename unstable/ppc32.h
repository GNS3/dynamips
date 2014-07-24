/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __PPC_32_H__
#define __PPC_32_H__

#include <pthread.h>

#include "utils.h" 
#include "rbtree.h"

/* CPU identifiers */
#define PPC32_PVR_405     0x40110000

/* Number of GPR (general purpose registers) */
#define PPC32_GPR_NR      32

/* Number of registers in FPU */
#define PPC32_FPU_REG_NR  32

/* Minimum page size: 4 Kb */
#define PPC32_MIN_PAGE_SHIFT   12
#define PPC32_MIN_PAGE_SIZE    (1 << PPC32_MIN_PAGE_SHIFT)
#define PPC32_MIN_PAGE_IMASK   (PPC32_MIN_PAGE_SIZE - 1)
#define PPC32_MIN_PAGE_MASK    0xFFFFF000

/* Number of instructions per page */
#define PPC32_INSN_PER_PAGE    (PPC32_MIN_PAGE_SIZE/sizeof(ppc_insn_t))

/* Starting point for ROM */
#define PPC32_ROM_START  0xfff00100
#define PPC32_ROM_SP     0x00006000

/* Special Purpose Registers (SPR) */
#define PPC32_SPR_XER        1
#define PPC32_SPR_LR         8      /* Link Register */
#define PPC32_SPR_CTR        9      /* Count Register */
#define PPC32_SPR_DSISR      18
#define PPC32_SPR_DAR        19
#define PPC32_SPR_DEC        22     /* Decrementer */
#define PPC32_SPR_SDR1       25     /* Page Table Address */
#define PPC32_SPR_SRR0       26
#define PPC32_SPR_SRR1       27
#define PPC32_SPR_TBL_READ   268    /* Time Base Low (read) */
#define PPC32_SPR_TBU_READ   269    /* Time Base Up (read) */
#define PPC32_SPR_SPRG0      272
#define PPC32_SPR_SPRG1      273
#define PPC32_SPR_SPRG2      274
#define PPC32_SPR_SPRG3      275
#define PPC32_SPR_TBL_WRITE  284    /* Time Base Low (write) */
#define PPC32_SPR_TBU_WRITE  285    /* Time Base Up (write) */
#define PPC32_SPR_PVR        287    /* Processor Version Register */
#define PPC32_SPR_HID0       1008
#define PPC32_SPR_HID1       1009

#define PPC405_SPR_PID      945    /* Process Identifier */

/* Exception vectors */
#define PPC32_EXC_SYS_RST   0x00000100   /* System Reset */
#define PPC32_EXC_MC_CHK    0x00000200   /* Machine Check */
#define PPC32_EXC_DSI       0x00000300   /* Data memory access failure */
#define PPC32_EXC_ISI       0x00000400   /* Instruction fetch failure */
#define PPC32_EXC_EXT       0x00000500   /* External Interrupt */
#define PPC32_EXC_ALIGN     0x00000600   /* Alignment */
#define PPC32_EXC_PROG      0x00000700   /* FPU, Illegal instruction, ... */
#define PPC32_EXC_NO_FPU    0x00000800   /* FPU unavailable */
#define PPC32_EXC_DEC       0x00000900   /* Decrementer */
#define PPC32_EXC_SYSCALL   0x00000C00   /* System Call */
#define PPC32_EXC_TRACE     0x00000D00   /* Trace */
#define PPC32_EXC_FPU_HLP   0x00000E00   /* Floating-Point Assist */

/* Condition Register (CR) is accessed through 8 fields of 4 bits */
#define ppc32_get_cr_field(n)  ((n) >> 2)
#define ppc32_get_cr_bit(n)    (~(n) & 0x03)

/* Positions of LT, GT, EQ and SO bits in CR fields */
#define PPC32_CR_LT_BIT  3
#define PPC32_CR_GT_BIT  2
#define PPC32_CR_EQ_BIT  1
#define PPC32_CR_SO_BIT  0

/* CR0 (Condition Register Field 0) bits */
#define PPC32_CR0_LT_BIT    31
#define PPC32_CR0_LT        (1 << PPC32_CR0_LT_BIT)   /* Negative */
#define PPC32_CR0_GT_BIT    30
#define PPC32_CR0_GT        (1 << PPC32_CR0_GT_BIT)   /* Positive */
#define PPC32_CR0_EQ_BIT    29
#define PPC32_CR0_EQ        (1 << PPC32_CR0_EQ_BIT)   /* Zero */
#define PPC32_CR0_SO_BIT    28
#define PPC32_CR0_SO        (1 << PPC32_CR0_SO_BIT)   /* Summary overflow */

/* XER register */
#define PPC32_XER_SO_BIT    31
#define PPC32_XER_SO        (1 << PPC32_XER_SO_BIT) /* Summary Overflow */
#define PPC32_XER_OV        0x40000000              /* Overflow */
#define PPC32_XER_CA_BIT    29
#define PPC32_XER_CA        (1 << PPC32_XER_CA_BIT) /* Carry */
#define PPC32_XER_BC_MASK   0x0000007F              /* Byte cnt (lswx/stswx) */

/* MSR (Machine State Register) */
#define PPC32_MSR_POW_MASK  0x00060000   /* Power Management */
#define PPC32_MSR_ILE       0x00010000   /* Exception Little-Endian Mode */
#define PPC32_MSR_EE        0x00008000   /* External Interrupt Enable */
#define PPC32_MSR_PR        0x00004000   /* Privilege Level (0=supervisor) */
#define PPC32_MSR_PR_SHIFT  14
#define PPC32_MSR_FP        0x00002000   /* Floating-Point Available */
#define PPC32_MSR_ME        0x00001000   /* Machine Check Enable */
#define PPC32_MSR_FE0       0x00000800   /* Floating-Point Exception Mode 0 */
#define PPC32_MSR_SE        0x00000400   /* Single-step trace enable */
#define PPC32_MSR_BE        0x00000200   /* Branch Trace Enable */
#define PPC32_MSR_FE1       0x00000100   /* Floating-Point Exception Mode 1 */
#define PPC32_MSR_IP        0x00000040   /* Exception Prefix */
#define PPC32_MSR_IR        0x00000020   /* Instruction address translation */
#define PPC32_MSR_DR        0x00000010   /* Data address translation */
#define PPC32_MSR_RI        0x00000002   /* Recoverable Exception */
#define PPC32_MSR_LE        0x00000001   /* Little-Endian mode enable */

#define PPC32_RFI_MSR_MASK  0x87c0ff73
#define PPC32_EXC_SRR1_MASK 0x0000ff73
#define PPC32_EXC_MSR_MASK  0x0006ef32

/* Number of BAT registers (8 for PowerPC 7448) */
#define PPC32_BAT_NR  8

/* Number of segment registers */
#define PPC32_SR_NR   16

/* Upper BAT register */
#define PPC32_UBAT_BEPI_MASK   0xFFFE0000  /* Block Effective Page Index */
#define PPC32_UBAT_BEPI_SHIFT  17
#define PPC32_UBAT_BL_MASK     0x00001FFC  /* Block Length */
#define PPC32_UBAT_BL_SHIFT    2
#define PPC32_UBAT_XBL_MASK    0x0001FFFC  /* Block Length */
#define PPC32_UBAT_XBL_SHIFT   2
#define PPC32_UBAT_VS          0x00000002  /* Supervisor mode valid bit */
#define PPC32_UBAT_VP          0x00000001  /* User mode valid bit */
#define PPC32_UBAT_PROT_MASK   (PPC32_UBAT_VS|PPC32_UBAT_VP)

/* Lower BAT register */
#define PPC32_LBAT_BRPN_MASK   0xFFFE0000  /* Physical address */
#define PPC32_LBAT_BRPN_SHIFT  17
#define PPC32_LBAT_WIMG_MASK   0x00000078  /* Memory/cache access mode bits */
#define PPC32_LBAT_PP_MASK     0x00000003  /* Protection bits */

#define PPC32_BAT_ADDR_SHIFT   17

/* Segment Descriptor */
#define PPC32_SD_T          0x80000000
#define PPC32_SD_KS         0x40000000   /* Supervisor-state protection key */
#define PPC32_SD_KP         0x20000000   /* User-state protection key */
#define PPC32_SD_N          0x10000000   /* No-execute protection bit */
#define PPC32_SD_VSID_MASK  0x00FFFFFF   /* Virtual Segment ID */

/* SDR1 Register */
#define PPC32_SDR1_HTABORG_MASK  0xFFFF0000  /* Physical base address */
#define PPC32_SDR1_HTABEXT_MASK  0x0000E000  /* Extended base address */
#define PPC32_SDR1_HTABMASK      0x000001FF  /* Mask for page table address */
#define PPC32_SDR1_HTMEXT_MASK   0x00001FFF  /* Extended mask */

/* Page Table Entry (PTE) size: 64-bits */
#define PPC32_PTE_SIZE   8

/* PTE entry (Up and Lo) */
#define PPC32_PTEU_V           0x80000000    /* Valid entry */
#define PPC32_PTEU_VSID_MASK   0x7FFFFF80    /* Virtual Segment ID */
#define PPC32_PTEU_VSID_SHIFT  7 
#define PPC32_PTEU_H           0x00000040    /* Hash function */
#define PPC32_PTEU_API_MASK    0x0000003F    /* Abbreviated Page index */
#define PPC32_PTEL_RPN_MASK    0xFFFFF000    /* Physical Page Number */
#define PPC32_PTEL_XPN_MASK    0x00000C00    /* Extended Page Number (0-2) */
#define PPC32_PTEL_XPN_SHIFT   9
#define PPC32_PTEL_R           0x00000100    /* Referenced bit */
#define PPC32_PTEL_C           0x00000080    /* Changed bit */
#define PPC32_PTEL_WIMG_MASK   0x00000078    /* Mem/cache access mode bits */
#define PPC32_PTEL_WIMG_SHIFT  3
#define PPC32_PTEL_X_MASK      0x00000004    /* Extended Page Number (3) */
#define PPC32_PTEL_X_SHIFT     2
#define PPC32_PTEL_PP_MASK     0x00000003    /* Page Protection bits */

/* DSISR register */
#define PPC32_DSISR_NOTRANS    0x40000000    /* No valid translation */
#define PPC32_DSISR_STORE      0x02000000    /* Store operation */

/* PowerPC 405 TLB definitions */
#define PPC405_TLBHI_EPN_MASK    0xFFFFFC00    /* Effective Page Number */
#define PPC405_TLBHI_SIZE_MASK   0x00000380    /* Page Size */
#define PPC405_TLBHI_SIZE_SHIFT  7
#define PPC405_TLBHI_V           0x00000040    /* Valid TLB entry */
#define PPC405_TLBHI_E           0x00000020    /* Endianness */
#define PPC405_TLBHI_U0          0x00000010    /* User-Defined Attribute */

#define PPC405_TLBLO_RPN_MASK    0xFFFFFC00    /* Real Page Number */
#define PPC405_TLBLO_EX          0x00000200    /* Execute Enable */
#define PPC405_TLBLO_WR          0x00000100    /* Write Enable */
#define PPC405_TLBLO_ZSEL_MASK   0x000000F0    /* Zone Select */
#define PPC405_TLBLO_ZSEL_SHIFT  4
#define PPC405_TLBLO_W           0x00000008    /* Write-Through */
#define PPC405_TLBLO_I           0x00000004    /* Caching Inhibited */
#define PPC405_TLBLO_M           0x00000002    /* Memory Coherent */
#define PPC405_TLBLO_G           0x00000001    /* Guarded */

/* Number of TLB entries for PPC405 */
#define PPC405_TLB_ENTRIES   64

struct ppc405_tlb_entry {
   m_uint32_t tlb_hi,tlb_lo,tid;
};

/* Memory operations */
enum {
   PPC_MEMOP_LOOKUP = 0,
   PPC_MEMOP_IFETCH,

   /* Load operations */
   PPC_MEMOP_LBZ,
   PPC_MEMOP_LHZ,
   PPC_MEMOP_LWZ,

   /* Load operation with sign-extend */
   PPC_MEMOP_LHA,

   /* Store operations */
   PPC_MEMOP_STB,
   PPC_MEMOP_STH,
   PPC_MEMOP_STW,

   /* Byte-Reversed operations */
   PPC_MEMOP_LWBR,
   PPC_MEMOP_STWBR,

   /* String operations */
   PPC_MEMOP_LSW,
   PPC_MEMOP_STSW,

   /* FPU operations */
   PPC_MEMOP_LFD,
   PPC_MEMOP_STFD,

   /* ICBI - Instruction Cache Block Invalidate */
   PPC_MEMOP_ICBI,

   PPC_MEMOP_MAX,
};

/* PowerPC CPU type */
typedef struct cpu_ppc cpu_ppc_t;

/* Memory operation function prototype */
typedef fastcall void (*ppc_memop_fn)(cpu_ppc_t *cpu,m_uint32_t vaddr,
                                      u_int reg);

/* BAT type indexes */
enum {
   PPC32_IBAT_IDX = 0,
   PPC32_DBAT_IDX,
};

/* BAT register */
struct ppc32_bat_reg {
   m_uint32_t reg[2];
};

/* BAT register programming */
struct ppc32_bat_prog {
   int type,index;
   m_uint32_t hi,lo;
};

/* MTS Instruction Cache and Data Cache */
#define PPC32_MTS_ICACHE  PPC32_IBAT_IDX
#define PPC32_MTS_DCACHE  PPC32_DBAT_IDX

/* FPU Coprocessor definition */
typedef struct {
   m_uint64_t reg[PPC32_FPU_REG_NR];
}ppc_fpu_t;

/* Maximum number of breakpoints */
#define PPC32_MAX_BREAKPOINTS  20

/* PowerPC CPU definition */
struct cpu_ppc {
   /* Execution state */
   m_uint32_t exec_state;

   /* Instruction address */
   m_uint32_t ia;
   
   /* Last successfull instruction address */
   m_uint32_t ia_prev;

   /* General Purpose registers */
   m_uint32_t gpr[PPC32_GPR_NR];

   /* Pending IRQ */
   volatile m_uint32_t irq_pending,irq_check;

   /* XER, Condition Register, Link Register, Count Register */
   m_uint32_t xer,lr,ctr,reserve;
   m_uint32_t xer_ca;

   /* Condition Register (CR) fields */
   u_int cr_fields[8];

   /* MTS caches (Instruction+Data) */
   mts32_entry_t *mts_cache[2];

   /* Code page translation cache and physical page mapping */
   ppc32_jit_tcb_t **tcb_virt_hash,**tcb_phys_hash;

   /* Virtual address to physical page translation */
   fastcall int (*translate)(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int cid,
                             m_uint32_t *phys_page);

   /* Memory access functions */
   ppc_memop_fn mem_op_fn[PPC_MEMOP_MAX];

   /* Memory lookup function (to load ELF image,...) and Instruction fetch */
   void *(*mem_op_lookup)(cpu_ppc_t *cpu,m_uint32_t vaddr,u_int cid);
   void *(*mem_op_ifetch)(cpu_ppc_t *cpu,m_uint32_t vaddr);

   /* MTS slow lookup function */
   mts32_entry_t *(*mts_slow_lookup)(cpu_ppc_t *cpu,m_uint32_t vaddr,
                                     u_int cid,u_int op_code,u_int op_size,
                                     u_int op_type,m_uint64_t *data,
                                     mts32_entry_t *alt_entry);

   /* IRQ counters */
   m_uint64_t irq_count,timer_irq_count,irq_fp_count;
   pthread_mutex_t irq_lock;

   /* Current and free lists of translated code blocks */
   ppc32_jit_tcb_t *tcb_list,*tcb_last,*tcb_free_list;

   /* Executable page area */
   void *exec_page_area;
   size_t exec_page_area_size;
   size_t exec_page_count,exec_page_alloc;
   insn_exec_page_t *exec_page_free_list;
   insn_exec_page_t *exec_page_array;

   /* Idle PC value */
   volatile m_uint32_t idle_pc;

   /* Timer IRQs */
   volatile u_int timer_irq_pending,timer_irq_armed;
   u_int timer_irq_freq;
   u_int timer_irq_check_itv;
   u_int timer_drift;

   /* IRQ disable flag */
   volatile u_int irq_disable;

   /* IBAT (Instruction) and DBAT (Data) registers */
   struct ppc32_bat_reg bat[2][PPC32_BAT_NR];
   
   /* Segment registers */
   m_uint32_t sr[PPC32_SR_NR];

   /* Page Table Address */
   m_uint32_t sdr1;
   void *sdr1_hptr;

   /* MSR (Machine state register) */
   m_uint32_t msr;

   /* Interrupt Registers (SRR0/SRR1) */
   m_uint32_t srr0,srr1,dsisr,dar;

   /* SPRG registers */
   m_uint32_t sprg[4];

   /* PVR (Processor Version Register) */
   m_uint32_t pvr;

   /* Time-Base register */
   m_uint64_t tb;

   /* Decrementer */
   m_uint32_t dec;

   /* Hardware Implementation Dependent Registers */
   m_uint32_t hid0,hid1;

   /* String instruction position (lswi/stswi) */
   u_int sw_pos;

   /* PowerPC 405 TLB */
   struct ppc405_tlb_entry ppc405_tlb[PPC405_TLB_ENTRIES];
   m_uint32_t ppc405_pid;

   /* MPC860 IMMR register */
   m_uint32_t mpc860_immr;

   /* FPU */
   ppc_fpu_t fpu;

   /* Generic CPU instance pointer */
   cpu_gen_t *gen;

   /* VM instance */
   vm_instance_t *vm;
   
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

   /* Current exec page (non-JIT) info */
   m_uint64_t njm_exec_page;
   mips_insn_t *njm_exec_ptr;

   /* Performance counter (non-JIT) */
   m_uint32_t perf_counter;

   /* non-JIT mode instruction counter */
   m_uint64_t insn_exec_count;

   /* Breakpoints */
   m_uint32_t breakpoints[PPC32_MAX_BREAKPOINTS];
   u_int breakpoints_enabled;

   /* JIT host register allocation */
   char *jit_hreg_seq_name;
   int ppc_reg_map[PPC32_GPR_NR];
   struct hreg_map *hreg_map_list,*hreg_lru;
   struct hreg_map hreg_map[JIT_HOST_NREG];
};

#define PPC32_CR_FIELD_OFFSET(f) \
   (OFFSET(cpu_ppc_t,cr_fields)+((f) * sizeof(u_int)))

/* Get the full CR register */
static forced_inline m_uint32_t ppc32_get_cr(cpu_ppc_t *cpu)
{
   m_uint32_t cr = 0;
   int i;

   for(i=0;i<8;i++)
      cr |= cpu->cr_fields[i] << (28 - (i << 2));

   return(cr);
}

/* Set the CR fields given a CR value */
static forced_inline void ppc32_set_cr(cpu_ppc_t *cpu,m_uint32_t cr)
{
   int i;

   for(i=0;i<8;i++)
      cpu->cr_fields[i] = (cr >> (28 - (i << 2))) & 0x0F;
}

/* Get a CR bit */
static forced_inline m_uint32_t ppc32_read_cr_bit(cpu_ppc_t *cpu,u_int bit)
{
   m_uint32_t res;

   res = cpu->cr_fields[ppc32_get_cr_field(bit)] >> ppc32_get_cr_bit(bit);
   return(res & 0x01);
}

/* Set a CR bit */
static forced_inline void ppc32_set_cr_bit(cpu_ppc_t *cpu,u_int bit)
{
   cpu->cr_fields[ppc32_get_cr_field(bit)] |= 1 << ppc32_get_cr_bit(bit);
}

/* Clear a CR bit */
static forced_inline void ppc32_clear_cr_bit(cpu_ppc_t *cpu,u_int bit)
{
   cpu->cr_fields[ppc32_get_cr_field(bit)] &= ~(1 << ppc32_get_cr_bit(bit));
}

/* Reset a PowerPC CPU */
int ppc32_reset(cpu_ppc_t *cpu);

/* Initialize a PowerPC processor */
int ppc32_init(cpu_ppc_t *cpu);

/* Delete a PowerPC processor */
void ppc32_delete(cpu_ppc_t *cpu);

/* Set the processor version register (PVR) */
void ppc32_set_pvr(cpu_ppc_t *cpu,m_uint32_t pvr);

/* Set idle PC value */
void ppc32_set_idle_pc(cpu_gen_t *cpu,m_uint64_t addr);

/* Timer IRQ */
void *ppc32_timer_irq_run(cpu_ppc_t *cpu);

/* Determine an "idling" PC */
int ppc32_get_idling_pc(cpu_gen_t *cpu);

/* Generate an exception */
void ppc32_trigger_exception(cpu_ppc_t *cpu,u_int exc_vector);

/* Trigger the decrementer exception */
void ppc32_trigger_timer_irq(cpu_ppc_t *cpu);

/* Trigger IRQs */
fastcall void ppc32_trigger_irq(cpu_ppc_t *cpu);

/* Virtual breakpoint */
fastcall void ppc32_run_breakpoint(cpu_ppc_t *cpu);

/* Add a virtual breakpoint */
int ppc32_add_breakpoint(cpu_gen_t *cpu,m_uint64_t ia);

/* Remove a virtual breakpoint */
void ppc32_remove_breakpoint(cpu_gen_t *cpu,m_uint64_t ia);

/* Return a boolean indicating wheather the current EPC has a breakpoint */
int ppc32_is_breakpoint_at_pc(cpu_ppc_t *cpu);

/* Set a register */
void ppc32_reg_set(cpu_gen_t *cpu,u_int reg,m_uint64_t val);

/* Dump registers of a PowerPC processor */
void ppc32_dump_regs(cpu_gen_t *cpu);

/* Dump MMU registers */
void ppc32_dump_mmu(cpu_gen_t *cpu);

/* Load a raw image into the simulated memory */
int ppc32_load_raw_image(cpu_ppc_t *cpu,char *filename,m_uint32_t vaddr);

/* Load an ELF image into the simulated memory */
int ppc32_load_elf_image(cpu_ppc_t *cpu,char *filename,int skip_load,
                         m_uint32_t *entry_point);

/* Run PowerPC code in step-by-step mode */
void *ppc32_exec_run_cpu(cpu_gen_t *gen);

#endif
