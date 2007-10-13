/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __CPU_H__
#define __CPU_H__

#include <pthread.h>
#include <setjmp.h>
#include "utils.h"
#include "jit_op.h"

#include "mips64.h"
#include "mips64_cp0.h"
#include "ppc32.h"

/* Possible CPU types */
enum {
   CPU_TYPE_MIPS64 = 1,
   CPU_TYPE_PPC32,
};

/* Virtual CPU states */
enum {
   CPU_STATE_RUNNING = 0,
   CPU_STATE_HALTED,
   CPU_STATE_SUSPENDED,
};

/* Maximum results for idle pc */
#define CPU_IDLE_PC_MAX_RES  10

/* Idle PC proposed value */
struct cpu_idle_pc {
   m_uint64_t pc;
   u_int count;
};

/* Number of recorded memory accesses (power of two) */
#define MEMLOG_COUNT   16

typedef struct memlog_access memlog_access_t;
struct memlog_access {
   m_uint64_t iaddr;
   m_uint64_t vaddr;
   m_uint64_t data;
   m_uint32_t data_valid;
   m_uint32_t op_size;
   m_uint32_t op_type;
};

/* Undefined memory access handler */
typedef int (*cpu_undefined_mem_handler_t)(cpu_gen_t *cpu,m_uint64_t vaddr,
                                           u_int op_size,u_int op_type,
                                           m_uint64_t *data);

/* Generic CPU definition */
struct cpu_gen {
   /* CPU type and identifier for MP systems */
   u_int type,id;

   /* CPU states */
   volatile u_int state,prev_state;
   volatile m_uint64_t seq_state;

   /* Thread running this CPU */
   pthread_t cpu_thread;
   volatile int cpu_thread_running;

   /* Exception restore point */
   jmp_buf exec_loop_env;

   /* "Idle" loop management */
   u_int idle_count,idle_max,idle_sleep_time;
   pthread_mutex_t idle_mutex;
   pthread_cond_t idle_cond;

   /* VM instance */
   vm_instance_t *vm;

   /* Next CPU in group */
   cpu_gen_t *next;

   /* Idle PC proposal */
   struct cpu_idle_pc idle_pc_prop[CPU_IDLE_PC_MAX_RES];
   u_int idle_pc_prop_count;

   /* Specific CPU part */
   union {
      cpu_mips_t mips64_cpu;
      cpu_ppc_t ppc32_cpu;
   }sp;

   /* Methods */
   void (*reg_set)(cpu_gen_t *cpu,u_int reg_index,m_uint64_t val);
   void (*reg_dump)(cpu_gen_t *cpu);
   void (*mmu_dump)(cpu_gen_t *cpu);
   void (*mmu_raw_dump)(cpu_gen_t *cpu);
   void (*add_breakpoint)(cpu_gen_t *cpu,m_uint64_t addr);
   void (*remove_breakpoint)(cpu_gen_t *cpu,m_uint64_t addr);
   void (*set_idle_pc)(cpu_gen_t *cpu,m_uint64_t addr);
   void (*get_idling_pc)(cpu_gen_t *cpu);   
   void (*mts_rebuild)(cpu_gen_t *cpu);
   void (*mts_show_stats)(cpu_gen_t *cpu);

   cpu_undefined_mem_handler_t undef_mem_handler;

   /* Memory access log for fault debugging */
   u_int memlog_pos;
   memlog_access_t memlog_array[MEMLOG_COUNT];

   /* Statistics */
   m_uint64_t dev_access_counter;

   /* JIT op array for current compiled page */
   u_int jit_op_array_size;
   jit_op_t **jit_op_array;
   jit_op_t **jit_op_current;
   
   /* JIT op pool */
   jit_op_t *jit_op_pool[JIT_OP_POOL_NR];
};

/* CPU group definition */
typedef struct cpu_group cpu_group_t;
struct cpu_group {
   char *name;
   cpu_gen_t *cpu_list;
   void *priv_data;
};

#define CPU_MIPS64(cpu) (&(cpu)->sp.mips64_cpu)
#define CPU_PPC32(cpu)  (&(cpu)->sp.ppc32_cpu)

/* Get CPU instruction pointer */
static forced_inline m_uint64_t cpu_get_pc(cpu_gen_t *cpu)
{
   switch(cpu->type) {
      case CPU_TYPE_MIPS64:
         return(CPU_MIPS64(cpu)->pc);
      case CPU_TYPE_PPC32:
         return((m_uint64_t)CPU_PPC32(cpu)->ia);
      default:
         return(0);
   }
}

/* Get CPU performance counter */
static forced_inline m_uint32_t cpu_get_perf_counter(cpu_gen_t *cpu)
{
   switch(cpu->type) {
      case CPU_TYPE_MIPS64:
         return(CPU_MIPS64(cpu)->perf_counter);
      case CPU_TYPE_PPC32:
         return(CPU_PPC32(cpu)->perf_counter);
      default:
         return(0);
   }
}

/* Find a CPU in a group given its ID */
cpu_gen_t *cpu_group_find_id(cpu_group_t *group,u_int id);

/* Find the highest CPU ID in a CPU group */
int cpu_group_find_highest_id(cpu_group_t *group,u_int *highest_id);

/* Add a CPU in a CPU group */
int cpu_group_add(cpu_group_t *group,cpu_gen_t *cpu);

/* Create a new CPU group */
cpu_group_t *cpu_group_create(char *name);

/* Delete a CPU group */
void cpu_group_delete(cpu_group_t *group);

/* Rebuild the MTS subsystem for a CPU group */
int cpu_group_rebuild_mts(cpu_group_t *group);

/* Log a message for a CPU */
void cpu_log(cpu_gen_t *cpu,char *module,char *format,...);

/* Create a new CPU */
cpu_gen_t *cpu_create(vm_instance_t *vm,u_int type,u_int id);

/* Delete a CPU */
void cpu_delete(cpu_gen_t *cpu);

/* Start a CPU */
void cpu_start(cpu_gen_t *cpu);

/* Stop a CPU */
void cpu_stop(cpu_gen_t *cpu);

/* Start all CPUs of a CPU group */
void cpu_group_start_all_cpu(cpu_group_t *group);

/* Stop all CPUs of a CPU group */
void cpu_group_stop_all_cpu(cpu_group_t *group);

/* Set a state of all CPUs of a CPU group */
void cpu_group_set_state(cpu_group_t *group,u_int state);

/* Synchronize on CPUs (all CPUs must be inactive) */
int cpu_group_sync_state(cpu_group_t *group);

/* Save state of all CPUs */
int cpu_group_save_state(cpu_group_t *group);

/* Restore state of all CPUs */
int cpu_group_restore_state(cpu_group_t *group);

/* Virtual idle loop */
void cpu_idle_loop(cpu_gen_t *cpu);

/* Break idle wait state */
void cpu_idle_break_wait(cpu_gen_t *cpu);

/* Returns to the CPU exec loop */
static inline void cpu_exec_loop_enter(cpu_gen_t *cpu)
{
   longjmp(cpu->exec_loop_env,1);
}

/* Set the exec loop entry point */
#define cpu_exec_loop_set(cpu) setjmp((cpu)->exec_loop_env)

#endif
