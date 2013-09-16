/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * JIT operations.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>

#include "cpu.h"
#include "jit_op.h"

u_int jit_op_blk_sizes[JIT_OP_POOL_NR] = {
   0, 32, 64, 128, 256, 384, 512, 1024,
};

/* Get a JIT op (allocate one if necessary) */
jit_op_t *jit_op_get(cpu_gen_t *cpu,int size_index,u_int opcode)
{
   jit_op_t *op;
   size_t len;

   assert(size_index < JIT_OP_POOL_NR);
   op = cpu->jit_op_pool[size_index];

   if (op != NULL) {
      assert(op->ob_size_index == size_index);
      cpu->jit_op_pool[size_index] = op->next;
   } else {
      /* no block found, allocate one */
      len = sizeof(*op) + jit_op_blk_sizes[size_index];

      op = malloc(len);
      assert(op != NULL);
      op->ob_size_index = size_index;
   }

   op->opcode = opcode;
   op->param[0] = op->param[1] = op->param[2] = -1;
   op->next = NULL;
   op->ob_ptr = op->ob_data;
   op->arg_ptr = NULL;
   op->insn_name = NULL;
   return op;
}

/* Release a JIT op */
void jit_op_free(cpu_gen_t *cpu,jit_op_t *op)
{  
   assert(op->ob_size_index < JIT_OP_POOL_NR);
   op->next = cpu->jit_op_pool[op->ob_size_index];
   cpu->jit_op_pool[op->ob_size_index] = op;
}

/* Free a list of JIT ops */
void jit_op_free_list(cpu_gen_t *cpu,jit_op_t *op_list)
{
   jit_op_t *op,*opn;
   
   for(op=op_list;op;op=opn) {
      opn = op->next;
      jit_op_free(cpu,op);
   }
}

/* Initialize JIT op pools for the specified CPU */
int jit_op_init_cpu(cpu_gen_t *cpu)
{
   cpu->jit_op_array = calloc(cpu->jit_op_array_size,sizeof(jit_op_t *));

   if (!cpu->jit_op_array)
      return(-1);

   memset(cpu->jit_op_pool,0,sizeof(cpu->jit_op_pool));
   return(0);
}

/* Free memory used by pools */
void jit_op_free_pools(cpu_gen_t *cpu)
{
   jit_op_t *op,*opn;
   int i;

   for(i=0;i<JIT_OP_POOL_NR;i++) {
      for(op=cpu->jit_op_pool[i];op;op=opn) {
         opn = op->next;
         free(op);
      }
      
      cpu->jit_op_pool[i] = NULL;
   }
}
