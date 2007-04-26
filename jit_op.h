/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 */

#ifndef __JIT_OP_H__
#define __JIT_OP_H__

#include "utils.h"

/* Number of JIT pools */
#define JIT_OP_POOL_NR  8

/* Invalid register in op */
#define JIT_OP_INV_REG  -1

/* All flags */
#define JIT_OP_PPC_ALL_FLAGS  -1

/* All registers */
#define JIT_OP_ALL_REGS  -1

/* JIT opcodes */
enum {
   JIT_OP_INVALID = 0,
   JIT_OP_INSN_OUTPUT,
   JIT_OP_BRANCH_TARGET,
   JIT_OP_BRANCH_JUMP,
   JIT_OP_EOB,
   JIT_OP_LOAD_GPR,
   JIT_OP_STORE_GPR,
   JIT_OP_UPDATE_FLAGS,
   JIT_OP_REQUIRE_FLAGS,
   JIT_OP_TRASH_FLAGS,
   JIT_OP_ALTER_HOST_REG,
   JIT_OP_MOVE_HOST_REG,
   JIT_OP_SET_HOST_REG_IMM32,
};

/* JIT operation */
struct jit_op {
   u_int opcode;
   int param[3];
   void *arg_ptr;
   char *insn_name;
   struct jit_op *next;
   
   /* JIT output buffer */
   u_int ob_size_index;
   u_char *ob_final;
   u_char *ob_ptr;
   u_char ob_data[0];
};

extern u_int jit_op_blk_sizes[];

/* Find a specific opcode in a JIT op list */
static inline jit_op_t *jit_op_find_opcode(jit_op_t *op_list,u_int opcode)
{
   jit_op_t *op;

   for(op=op_list;op;op=op->next)
      if (op->opcode == opcode)
         return op;

   return NULL;
}

/* Get a JIT op (allocate one if necessary) */
jit_op_t *jit_op_get(cpu_gen_t *cpu,int size_index,u_int opcode);

/* Release a JIT op */
void jit_op_free(cpu_gen_t *cpu,jit_op_t *op);

/* Free a list of JIT ops */
void jit_op_free_list(cpu_gen_t *cpu,jit_op_t *op_list);

/* Initialize JIT op pools for the specified CPU */
int jit_op_init_cpu(cpu_gen_t *cpu);

/* Free memory used by pools */
void jit_op_free_pools(cpu_gen_t *cpu);

#endif
