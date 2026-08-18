/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * JIT engine for 32-bit PowerPC architecture
 * Copyright (c) 2006, 2007 Zhe Fang (fangzhe@msn.com)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "cpu.h"
#include "jit_op.h"
#include "ppc32_jit.h"
#include "ppc32_ppc32_trans.h"
#include "memory.h"

/* Macros for CPU structure access */
#define REG_OFFSET(reg)   (OFFSET(cpu_ppc_t,gpr[(reg)]))
#define MEMOP_OFFSET(op)  (OFFSET(cpu_ppc_t,mem_op_fn[(op)]))

/* Scratch slot in the caller stack frame (114 bytes, slots 0..113 are
   owned by the JIT caller; 32 is clear of the ABI-defined slots) */
#define PPC_STACK_SCRATCH_OFFSET 32

#define DECLARE_INSN(name) \
   static int ppc32_emit_##name(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b, \
                                ppc_insn_t insn)

/* ======================================================================== */
/* Low level helpers */

/* Load a 32 bit immediate value */
static forced_inline void ppc32_load_imm(u_char **ptr,u_int reg,
                                         m_uint32_t val)
{
   if (val & 0xffff0000) {
      ppc_lis(*ptr,reg,val >> 16);
      if (val & 0xffff)
         ppc_ori(*ptr,reg,reg,val & 0xffff);
   } else if (val & 0x8000) {
      ppc_lis(*ptr,reg,0);
      ppc_ori(*ptr,reg,reg,val);
   } else {
      ppc_li(*ptr,reg,val);
   }
}

/* Set the Instruction Address (IA) register */
void ppc32_set_ia(u_char **ptr,m_uint32_t new_ia)
{
   ppc32_load_imm(ptr,ppc_r12,new_ia);
   ppc_stw(*ptr,ppc_r12,OFFSET(cpu_ppc_t,ia),ppc_r3);
}

/* Set the Link Register (LR) */
static void ppc32_set_lr(jit_op_t *iop,m_uint32_t new_lr)
{
   ppc32_load_imm(&iop->ob_ptr,ppc_r11,new_lr);
   ppc_stw(iop->ob_ptr,ppc_r11,OFFSET(cpu_ppc_t,lr),ppc_r3);
}

/* Load a GPR into the specified host register */
static forced_inline void ppc32_load_gpr(u_char **ptr,u_int host_reg,
                                         u_int ppc_reg)
{
   ppc_lwz(*ptr,host_reg,REG_OFFSET(ppc_reg),ppc_r3);
}

/* Store contents for a host register into a GPR register */
static forced_inline void ppc32_store_gpr(u_char **ptr,u_int ppc_reg,
                                          u_int host_reg)
{
   ppc_stw(*ptr,host_reg,REG_OFFSET(ppc_reg),ppc_r3);
}

/* Restore the CPU instance pointer */
static forced_inline void ppc32_reload_cpu(u_char **ptr)
{
   ppc_lwz(*ptr,ppc_r3,PPC_STACK_DECREMENTER+PPC_STACK_PARAM_OFFSET,
           ppc_r1);
}

/* Store the host XER[CA] into the guest xer_ca */
static forced_inline void ppc32_update_xer_ca(u_char **ptr)
{
   ppc_mfspr(*ptr,ppc_r12,32);
   ppc_rlwinm(*ptr,ppc_r12,ppc_r12,3,31,31);
   ppc_stw(*ptr,ppc_r12,OFFSET(cpu_ppc_t,xer_ca),ppc_r3);
}

/* Extract the host XER[CA] bit into a host register (0/1) */
static forced_inline void ppc32_get_xer_ca(u_char **ptr,u_int reg)
{
   ppc_mfspr(*ptr,reg,32);
   ppc_rlwinm(*ptr,reg,reg,3,31,31);
}

/* Update a guest CR field from the host CR register */
static forced_inline void ppc32_update_cr_field(u_char **ptr,int field)
{
   m_uint32_t rot = (field << 2) & 31;

   ppc_mfcr(*ptr,ppc_r11);
   /* Keep LT/GT/EQ and shift to right position */
   ppc_rlwinm(*ptr,ppc_r11,ppc_r11,rot,0,2);
   ppc_rlwinm(*ptr,ppc_r11,ppc_r11,4,28,31);

#if 0
   /* Check XER Summary of Overflow and report it */
   ppc_lwz(*ptr,ppc_r12,OFFSET(cpu_ppc_t,xer),ppc_r3);
   ppc_rlwinm(*ptr,ppc_r12,ppc_r12,1,31,31);
   ppc_or(*ptr,ppc_r11,ppc_r11,ppc_r12);
#endif

   /* Store modified CR field */
   ppc_stw(*ptr,ppc_r11,PPC32_CR_FIELD_OFFSET(field),ppc_r3);
}

/* Update CR0 from the host CR register */
static forced_inline void ppc32_update_cr0(u_char **ptr,u_int hreg)
{
   /* Set host CR0 from the instruction result (hreg): the preceding host
      instruction (add, and, extsb, ...) does not update the CR. */
   ppc_cmpwi(*ptr,ppc_cr0,hreg,0);
   ppc32_update_cr_field(ptr,0);
}

/* Load a guest CR bit into the specified host register */
static forced_inline void ppc32_load_cr_bit(u_char **ptr,int rD,int bi)
{
   int field = ppc32_get_cr_field(bi);
   int pos   = ppc32_get_cr_bit(bi);

   ppc_lwz(*ptr,rD,PPC32_CR_FIELD_OFFSET(field),ppc_r3);
   ppc_rlwinm(*ptr,rD,rD,(32 - pos) & 31,31,31);
}

/* CR logical operation helpers */
static forced_inline void ppc32_cr_and (u_char **p,int rD,int rB){ ppc_and (*p,rD,rD,rB); }
static forced_inline void ppc32_cr_andc(u_char **p,int rD,int rB){ ppc_andc(*p,rD,rD,rB); }
static forced_inline void ppc32_cr_eqv (u_char **p,int rD,int rB){ ppc_eqv (*p,rD,rD,rB); }
static forced_inline void ppc32_cr_nand(u_char **p,int rD,int rB){ ppc_nand(*p,rD,rD,rB); }
static forced_inline void ppc32_cr_nor (u_char **p,int rD,int rB){ ppc_nor (*p,rD,rD,rB); }
static forced_inline void ppc32_cr_or  (u_char **p,int rD,int rB){ ppc_or  (*p,rD,rD,rB); }
static forced_inline void ppc32_cr_orc (u_char **p,int rD,int rB){ ppc_orc (*p,rD,rD,rB); }
static forced_inline void ppc32_cr_xor (u_char **p,int rD,int rB){ ppc_xor (*p,rD,rD,rB); }

/* Set a guest CR bit from a host register */
static forced_inline void ppc32_store_cr_bit(u_char **ptr,int rT,int rS,
                                             int bi)
{
   int field = ppc32_get_cr_field(bi);
   int pos   = ppc32_get_cr_bit(bi);

   ppc_lwz(*ptr,rT,PPC32_CR_FIELD_OFFSET(field),ppc_r3);
   ppc_rlwimi(*ptr,rT,rS,pos + 1,pos,pos);
   ppc_stw(*ptr,rT,PPC32_CR_FIELD_OFFSET(field),ppc_r3);
}

/* ======================================================================== */
/* Jump helpers */

/* Try a direct jump to another compiled page (block lookup optimization) */
static void ppc32_try_direct_far_jump(cpu_ppc_t *cpu,jit_op_t *iop,
                                      m_uint32_t new_ia)
{
   m_uint32_t new_page,ia_hash,ia_offset;
   u_char *test1,*test2,*test3,*test4;

   /* Indicate that we clobber r4..r7 */
   ppc32_op_emit_alter_host_reg(cpu,ppc_r4);
   ppc32_op_emit_alter_host_reg(cpu,ppc_r5);
   ppc32_op_emit_alter_host_reg(cpu,ppc_r6);
   ppc32_op_emit_alter_host_reg(cpu,ppc_r7);

   new_page = new_ia & PPC32_MIN_PAGE_MASK;
   ia_offset = (new_ia & PPC32_MIN_PAGE_IMASK) >> 2;
   ia_hash = ppc32_jit_get_virt_hash(new_ia);

   /* r4 = target page */
   ppc32_load_imm(&iop->ob_ptr,ppc_r4,new_page);

   /* r5 = tcb_virt_hash[ia_hash] */
   ppc_lwz(iop->ob_ptr,ppc_r5,OFFSET(cpu_ppc_t,tcb_virt_hash),ppc_r3);
   ppc32_load_imm(&iop->ob_ptr,ppc_r6,ia_hash * sizeof(void *));
   ppc_lwzx(iop->ob_ptr,ppc_r5,ppc_r5,ppc_r6);

   /* no JIT block found ? */
   ppc_cmpwi(iop->ob_ptr,ppc_cr0,ppc_r5,0);
   test1 = iop->ob_ptr;
   ppc_bc(iop->ob_ptr,PPC_BR_TRUE_UNLIKELY,
          ppc_crbf(ppc_cr0,PPC_BR_EQ),0);

   /* Check block PC */
   ppc_lwz(iop->ob_ptr,ppc_r6,OFFSET(ppc32_jit_tcb_t,start_ia),ppc_r5);
   ppc_cmpw(iop->ob_ptr,ppc_cr0,ppc_r4,ppc_r6);
   test2 = iop->ob_ptr;
   ppc_bc(iop->ob_ptr,PPC_BR_FALSE_UNLIKELY,
          ppc_crbf(ppc_cr0,PPC_BR_EQ),0);

   /* Jump to the code */
   ppc_lwz(iop->ob_ptr,ppc_r6,
           OFFSET(ppc32_jit_tcb_t,jit_insn_ptr),ppc_r5);
   ppc_cmpwi(iop->ob_ptr,ppc_cr0,ppc_r6,0);
   test3 = iop->ob_ptr;
   ppc_bc(iop->ob_ptr,PPC_BR_TRUE_UNLIKELY,
          ppc_crbf(ppc_cr0,PPC_BR_EQ),0);

   ppc_lwz(iop->ob_ptr,ppc_r7,ia_offset * sizeof(void *),ppc_r6);
   ppc_cmpwi(iop->ob_ptr,ppc_cr0,ppc_r7,0);
   test4 = iop->ob_ptr;
   ppc_bc(iop->ob_ptr,PPC_BR_TRUE_UNLIKELY,
          ppc_crbf(ppc_cr0,PPC_BR_EQ),0);
   ppc_mtctr(iop->ob_ptr,ppc_r7);
   ppc_bctr(iop->ob_ptr);

   /* Returns to caller... */
   ppc_patch(test1,iop->ob_ptr);
   ppc_patch(test2,iop->ob_ptr);
   ppc_patch(test3,iop->ob_ptr);
   ppc_patch(test4,iop->ob_ptr);

   ppc32_set_ia(&iop->ob_ptr,new_ia);
   ppc32_jit_tcb_push_epilog(&iop->ob_ptr);
}

/* Set Jump */
static void ppc32_set_jump(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,jit_op_t *iop,
                           m_uint32_t new_ia)
{
   u_char *jump_ptr;

   if (ppc32_jit_tcb_local_addr(b,new_ia,&jump_ptr)) {
      /* Same page: slots for ppc32_jit_tcb_set_patch() */
      ppc32_jit_tcb_record_patch(b,iop,iop->ob_ptr,new_ia);
      ppc_b(iop->ob_ptr,0);
      ppc_nop(iop->ob_ptr);
      ppc_nop(iop->ob_ptr);
      ppc_nop(iop->ob_ptr);
   } else {
      if (cpu->exec_blk_direct_jump) {
         /* Block lookup optimization */
         ppc32_try_direct_far_jump(cpu,iop,new_ia);
      } else {
         ppc32_set_ia(&iop->ob_ptr,new_ia);
         ppc32_jit_tcb_push_epilog(&iop->ob_ptr);
      }
   }
}

/* Jump to the next page */
void ppc32_set_page_jump(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b)
{
   jit_op_t *iop,*op_list = NULL;
   int i;

   cpu->gen->jit_op_current = &op_list;

   iop = ppc32_op_emit_insn_output(cpu,4,"set_page_jump");
   ppc32_set_jump(cpu,b,iop,b->start_ia + PPC32_MIN_PAGE_SIZE);
   ppc32_op_insn_output(b,iop);

   jit_op_free_list(cpu->gen,op_list);
   cpu->gen->jit_op_current = NULL;

   /* The page is complete: make all emitted code visible */
   for(i=0;i<b->jit_chunk_pos;i++)
      mono_ppc_flush_icache(b->jit_chunks[i]->ptr,PPC_JIT_BUFSIZE);
   mono_ppc_flush_icache(b->jit_buffer->ptr,
                         b->jit_ptr - b->jit_buffer->ptr);
}

/* Basic C call */
static forced_inline void ppc32_emit_basic_c_call(u_char **ptr,void *f)
{
   ppc_emit_jump_code(*ptr,(u_char *)f,1);
   ppc32_reload_cpu(ptr);
}

/* Emit a simple call to a C function without any parameter */
static void ppc32_emit_c_call(ppc32_jit_tcb_t *b,jit_op_t *iop,void *f)
{
   ppc32_set_ia(&iop->ob_ptr,b->start_ia+(b->ppc_trans_pos << 2));
   ppc32_emit_basic_c_call(&iop->ob_ptr,f);
}

/* ======================================================================== */
/* JIT operations (specific to target CPU). */

/* INSN_OUTPUT */
void ppc32_op_insn_output(ppc32_jit_tcb_t *b,jit_op_t *op)
{
   op->ob_final = b->jit_ptr;
   memcpy(b->jit_ptr,op->ob_data,op->ob_ptr - op->ob_data);
   b->jit_ptr += op->ob_ptr - op->ob_data;

   if ((op->ob_ptr - op->ob_data) >= jit_op_blk_sizes[op->ob_size_index]) {
      printf("ppc32_op_insn_output: FAILURE: count=%d, size=%d\n",
             op->ob_ptr - op->ob_data, jit_op_blk_sizes[op->ob_size_index]);
   }
}

/* LOAD_GPR: p[0] = %host_reg, p[1] = %ppc_reg */
void ppc32_op_load_gpr(ppc32_jit_tcb_t *b,jit_op_t *op)
{
   if (op->param[0] != JIT_OP_INV_REG)
      ppc32_load_gpr(&b->jit_ptr,op->param[0],op->param[1]);
}

/* STORE_GPR: p[0] = %host_reg, p[1] = %ppc_reg */
void ppc32_op_store_gpr(ppc32_jit_tcb_t *b,jit_op_t *op)
{
   if (op->param[0] != JIT_OP_INV_REG)
      ppc32_store_gpr(&b->jit_ptr,op->param[1],op->param[0]);
}

/* UPDATE_FLAGS: p[0] = cr_field, p[1] = is_signed */
void ppc32_op_update_flags(ppc32_jit_tcb_t *b,jit_op_t *op)
{
   if (op->param[0] != JIT_OP_INV_REG) {
      if (op->param[0] == 0)
         ppc32_update_cr0(&b->jit_ptr,ppc_r4);
      else
         ppc32_update_cr_field(&b->jit_ptr,op->param[0]);
   }
}

/* MOVE_HOST_REG: p[0] = %host_dst_reg, p[1] = %host_src_reg */
void ppc32_op_move_host_reg(ppc32_jit_tcb_t *b,jit_op_t *op)
{
   if ((op->param[0] != JIT_OP_INV_REG) && (op->param[1] != JIT_OP_INV_REG))
      ppc_mr(b->jit_ptr,op->param[0],op->param[1]);
}

/* SET_HOST_REG_IMM32: p[0] = %host_reg, p[1] = imm32 */
void ppc32_op_set_host_reg_imm32(ppc32_jit_tcb_t *b,jit_op_t *op)
{
   if (op->param[0] != JIT_OP_INV_REG)
      ppc32_load_imm(&b->jit_ptr,op->param[0],op->param[1]);
}

/* Indicate registers modified by ppc32_update_cr() functions */
void ppc32_update_cr_set_altered_hreg(cpu_ppc_t *cpu)
{
   /* r11 and r12 are modified by ppc32_update_cr*() */
   ppc32_op_emit_alter_host_reg(cpu,ppc_r11);
   ppc32_op_emit_alter_host_reg(cpu,ppc_r12);
}

/* Initialize register mapping */
void ppc32_jit_init_hreg_mapping(cpu_ppc_t *cpu)
{
   /* Volatile, caller-saved host registers available for caching guest
      registers; r3 is the cpu pointer and r12 is the scratch register. */
   int avail_hregs[] = { ppc_r4, ppc_r5, ppc_r6, ppc_r7, ppc_r8,
                         ppc_r9, ppc_r10, ppc_r11, -1 };
   struct hreg_map *map;
   int i,hreg;

   cpu->hreg_map_list = cpu->hreg_lru = NULL;

   /* Add the available registers to the map list */
   for(i=0;avail_hregs[i]!=-1;i++) {
      hreg = avail_hregs[i];
      map = &cpu->hreg_map[hreg];

      /* Initialize mapping. At the beginning, no PPC reg is mapped */
      map->flags = 0;
      map->hreg  = hreg;
      map->vreg  = -1;
      ppc32_jit_insert_hreg_mru(cpu,map);
   }

   /* Clear PPC registers mapping */
   for(i=0;i<PPC32_GPR_NR;i++)
      cpu->ppc_reg_map[i] = -1;
}

/* Allocate a specific temp register */
_Unused static int ppc32_jit_get_tmp_hreg(cpu_ppc_t *cpu)
{
   return(ppc_r12);
}

/* ======================================================================== */
/* Memory operations */

/* Memory operation */
static void ppc32_emit_memop(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                             int op,int base,int offset,int target,int update)
{
   m_uint32_t val = sign_extend(offset,16);
   jit_op_t *iop;

   /*
    * Since an exception can be triggered, clear JIT state. This allows
    * to use branch target tag (we can directly branch on this instruction).
    */
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_BRANCH_TARGET);
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);

   iop = ppc32_op_emit_insn_output(cpu,5,"memop");

   /* Save PC for exception handling */
   ppc32_set_ia(&iop->ob_ptr,b->start_ia+(b->ppc_trans_pos << 2));

   /* r4 = GPR[base] + sign-extended offset */
   ppc32_load_imm(&iop->ob_ptr,ppc_r4,val);
   if (update || (base != 0)) {
      ppc32_load_gpr(&iop->ob_ptr,ppc_r5,base);
      ppc_add(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   }

   /* Keep the effective address for the update */
   if (update)
      ppc_stw(iop->ob_ptr,ppc_r4,PPC_STACK_SCRATCH_OFFSET,ppc_r1);

   /* Call memory function */
   ppc_lwz(iop->ob_ptr,ppc_r12,MEMOP_OFFSET(op),ppc_r3);
   ppc_mtlr(iop->ob_ptr,ppc_r12);
   ppc_li(iop->ob_ptr,ppc_r5,target);
   ppc_blrl(iop->ob_ptr);

   ppc32_reload_cpu(&iop->ob_ptr);

   if (update) {
      ppc_lwz(iop->ob_ptr,ppc_r5,PPC_STACK_SCRATCH_OFFSET,ppc_r1);
      ppc32_store_gpr(&iop->ob_ptr,base,ppc_r5);
   }
}

/* Memory operation (indexed) */
static void ppc32_emit_memop_idx(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                                 int op,int ra,int rb,int target,int update)
{
   jit_op_t *iop;

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_BRANCH_TARGET);
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);

   iop = ppc32_op_emit_insn_output(cpu,5,"memop_idx");

   /* Save PC for exception handling */
   ppc32_set_ia(&iop->ob_ptr,b->start_ia+(b->ppc_trans_pos << 2));

   /* r4 = GPR[rb] + GPR[ra] */
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rb);
   if (update || (ra != 0)) {
      ppc32_load_gpr(&iop->ob_ptr,ppc_r5,ra);
      ppc_add(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   }

   if (update)
      ppc_stw(iop->ob_ptr,ppc_r4,PPC_STACK_SCRATCH_OFFSET,ppc_r1);

   ppc_lwz(iop->ob_ptr,ppc_r12,MEMOP_OFFSET(op),ppc_r3);
   ppc_mtlr(iop->ob_ptr,ppc_r12);
   ppc_li(iop->ob_ptr,ppc_r5,target);
   ppc_blrl(iop->ob_ptr);

   ppc32_reload_cpu(&iop->ob_ptr);

   if (update) {
      ppc_lwz(iop->ob_ptr,ppc_r5,PPC_STACK_SCRATCH_OFFSET,ppc_r1);
      ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r5);
   }
}

typedef void (*memop_fast_access)(jit_op_t *iop,int target);

/* Fast LBZ */
static void ppc32_memop_fast_lbz(jit_op_t *iop,int target)
{
   ppc_lbzx(iop->ob_ptr,ppc_r10,ppc_r8,ppc_r9);
   ppc32_store_gpr(&iop->ob_ptr,target,ppc_r10);
}

/* Fast LHZ */
static void ppc32_memop_fast_lhz(jit_op_t *iop,int target)
{
   ppc_lhzx(iop->ob_ptr,ppc_r10,ppc_r8,ppc_r9);
   ppc32_store_gpr(&iop->ob_ptr,target,ppc_r10);
}

/* Fast LHA */
static void ppc32_memop_fast_lha(jit_op_t *iop,int target)
{
   ppc_lhax(iop->ob_ptr,ppc_r10,ppc_r8,ppc_r9);
   ppc32_store_gpr(&iop->ob_ptr,target,ppc_r10);
}

/* Fast LWZ */
static void ppc32_memop_fast_lwz(jit_op_t *iop,int target)
{
   ppc_lwzx(iop->ob_ptr,ppc_r10,ppc_r8,ppc_r9);
   ppc32_store_gpr(&iop->ob_ptr,target,ppc_r10);
}

/* Fast STB */
static void ppc32_memop_fast_stb(jit_op_t *iop,int target)
{
   ppc32_load_gpr(&iop->ob_ptr,ppc_r10,target);
   ppc_stbx(iop->ob_ptr,ppc_r10,ppc_r8,ppc_r9);
}

/* Fast STH */
static void ppc32_memop_fast_sth(jit_op_t *iop,int target)
{
   ppc32_load_gpr(&iop->ob_ptr,ppc_r10,target);
   ppc_sthx(iop->ob_ptr,ppc_r10,ppc_r8,ppc_r9);
}

/* Fast STW */
static void ppc32_memop_fast_stw(jit_op_t *iop,int target)
{
   ppc32_load_gpr(&iop->ob_ptr,ppc_r10,target);
   ppc_stwx(iop->ob_ptr,ppc_r10,ppc_r8,ppc_r9);
}

/* Fast memory operation */
static void ppc32_emit_memop_fast(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                                  int write_op,int opcode,
                                  int base,int offset,int target,
                                  memop_fast_access op_handler)
{
   m_uint32_t val = sign_extend(offset,16);
   u_char *test1,*test2,*p_exit;
   jit_op_t *iop;

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_BRANCH_TARGET);
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);

   iop = ppc32_op_emit_insn_output(cpu,5,"memop_fast");

   test2 = NULL;

   /* r4 = GPR[base] + sign-extended offset */
   if (val != 0) {
      ppc32_load_imm(&iop->ob_ptr,ppc_r4,val);
      if (base != 0) {
         ppc32_load_gpr(&iop->ob_ptr,ppc_r5,base);
         ppc_add(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
      }
   } else {
      if (base != 0)
         ppc32_load_gpr(&iop->ob_ptr,ppc_r4,base);
      else
         ppc_li(iop->ob_ptr,ppc_r4,0);
   }

   /* r5 = mts32 hash */
   ppc_srwi(iop->ob_ptr,ppc_r5,ppc_r4,MTS32_HASH_SHIFT1);
   ppc_srwi(iop->ob_ptr,ppc_r6,ppc_r4,MTS32_HASH_SHIFT2);
   ppc_xor(iop->ob_ptr,ppc_r5,ppc_r5,ppc_r6);
   ppc_andid(iop->ob_ptr,ppc_r5,ppc_r5,MTS32_HASH_MASK);

   /* r6 = mts32_entry */
   ppc_lwz(iop->ob_ptr,ppc_r6,
           OFFSET(cpu_ppc_t,mts_cache)+(PPC32_MTS_DCACHE*sizeof(void *)),
           ppc_r3);
   ppc_mulli(iop->ob_ptr,ppc_r5,ppc_r5,sizeof(mts32_entry_t));
   ppc_add(iop->ob_ptr,ppc_r6,ppc_r6,ppc_r5);

   /* Compare virtual page address (r5 = vpage) */
   ppc_rlwinm(iop->ob_ptr,ppc_r5,ppc_r4,0,0,19);
   ppc_lwz(iop->ob_ptr,ppc_r7,OFFSET(mts32_entry_t,gvpa),ppc_r6);
   ppc_cmpw(iop->ob_ptr,ppc_cr0,ppc_r5,ppc_r7);
   test1 = iop->ob_ptr;
   ppc_bc(iop->ob_ptr,PPC_BR_FALSE_UNLIKELY,
          ppc_crbf(ppc_cr0,PPC_BR_EQ),0);

   /* Test if we are writing to a COW/EXEC page */
   if (write_op) {
      ppc_lwz(iop->ob_ptr,ppc_r7,OFFSET(mts32_entry_t,flags),ppc_r6);
      ppc_andid(iop->ob_ptr,ppc_r7,ppc_r7,MTS_FLAG_COW|MTS_FLAG_EXEC);
      ppc_cmpwi(iop->ob_ptr,ppc_cr0,ppc_r7,0);
      test2 = iop->ob_ptr;
      ppc_bc(iop->ob_ptr,PPC_BR_FALSE_UNLIKELY,
             ppc_crbf(ppc_cr0,PPC_BR_EQ),0);
   }

   /* r8 = Host Page Address, r9 = offset in page */
   ppc_lwz(iop->ob_ptr,ppc_r8,OFFSET(mts32_entry_t,hpa),ppc_r6);
   ppc_rlwinm(iop->ob_ptr,ppc_r9,ppc_r4,0,20,31);

   /* Memory access */
   op_handler(iop,target);

   p_exit = iop->ob_ptr;
   ppc_b(iop->ob_ptr,0);

   /* === Slow lookup === */
   ppc_patch(test1,iop->ob_ptr);
   if (test2)
      ppc_patch(test2,iop->ob_ptr);

   /* Save PC for exception handling */
   ppc32_set_ia(&iop->ob_ptr,b->start_ia+(b->ppc_trans_pos << 2));

   /* mem_op_fn[opcode](cpu, r4, target) */
   ppc_lwz(iop->ob_ptr,ppc_r12,MEMOP_OFFSET(opcode),ppc_r3);
   ppc_mtlr(iop->ob_ptr,ppc_r12);
   ppc_li(iop->ob_ptr,ppc_r5,target);
   ppc_blrl(iop->ob_ptr);

   ppc32_reload_cpu(&iop->ob_ptr);

   ppc_patch(p_exit,iop->ob_ptr);
}

/* ======================================================================== */
/* Fallback: unhandled instruction, breakpoint, perf counter */

/* Emit unhandled instruction code */
static int ppc32_emit_unknown(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                              ppc_insn_t opcode)
{
   u_char *test1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,5,"unknown");

   /* Update IA */
   ppc32_set_ia(&iop->ob_ptr,b->start_ia+(b->ppc_trans_pos << 2));

   /* Fallback to non-JIT mode: res = ppc32_exec_single_insn_ext(cpu, insn) */
   ppc32_load_imm(&iop->ob_ptr,ppc_r4,opcode);
   ppc_emit_jump_code(iop->ob_ptr,(u_char *)ppc32_exec_single_insn_ext,1);

   /* if (res != 0) exit the block */
   ppc_cmpwi(iop->ob_ptr,ppc_cr0,ppc_r3,0);
   test1 = iop->ob_ptr;
   ppc_bc(iop->ob_ptr,PPC_BR_TRUE_UNLIKELY,
          ppc_crbf(ppc_cr0,PPC_BR_EQ),0);
   ppc32_jit_tcb_push_epilog(&iop->ob_ptr);
   ppc_patch(test1,iop->ob_ptr);
   ppc32_reload_cpu(&iop->ob_ptr);

   /* Signal this as an EOB to reset JIT state */
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   return(0);
}

/* Virtual Breakpoint */
void ppc32_emit_breakpoint(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b)
{
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"breakpoint");
   ppc32_emit_c_call(b,iop,ppc32_run_breakpoint);

   /* Signal this as an EOB to reset JIT state */
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
}

/* Increment the number of executed instructions (performance debugging) */
void ppc32_inc_perf_counter(cpu_ppc_t *cpu)
{
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,1,"perf_cnt");
   ppc_lwz(iop->ob_ptr,ppc_r12,OFFSET(cpu_ppc_t,perf_counter),ppc_r3);
   ppc_addi(iop->ob_ptr,ppc_r12,ppc_r12,1);
   ppc_stw(iop->ob_ptr,ppc_r12,OFFSET(cpu_ppc_t,perf_counter),ppc_r3);
}

/* ======================================================================== */
/* Instruction emitters */

/* BLR - Branch to Link Register */
DECLARE_INSN(BLR)
{
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"blr");

   ppc_lwz(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,lr),ppc_r3);
   ppc_stw(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,ia),ppc_r3);

   /* set the return address */
   if (insn & 1)
      ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));

   ppc32_jit_tcb_push_epilog(&iop->ob_ptr);
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1) << 2));
   return(0);
}

/* BCTR - Branch to Count Register */
DECLARE_INSN(BCTR)
{
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"bctr");

   ppc_lwz(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,ctr),ppc_r3);
   ppc_stw(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,ia),ppc_r3);

   if (insn & 1)
      ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));

   ppc32_jit_tcb_push_epilog(&iop->ob_ptr);
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1) << 2));
   return(0);
}

/* BCLR - Branch Conditional to Link register */
DECLARE_INSN(BCLR)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   jit_op_t *iop;
   u_char *jump_ptr;
   int cond,ctr;

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_BRANCH_JUMP);
   iop = ppc32_op_emit_insn_output(cpu,5,"bclr");

   cond = (bo >> 3) & 0x1;
   ctr  = (bo >> 1) & 0x1;

   /* Set the return address */
   if (insn & 1) {
      ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));
      ppc32_op_emit_branch_target(cpu,b,
                                  b->start_ia+((b->ppc_trans_pos+1) << 2));
   }

   /* r4 = condition (0/1), same logic as ppc32_check_cond() */
   ppc_li(iop->ob_ptr,ppc_r4,1);
   if (!(bo & 0x04)) {
      ppc_lwz(iop->ob_ptr,ppc_r5,OFFSET(cpu_ppc_t,ctr),ppc_r3);
      ppc_addic(iop->ob_ptr,ppc_r5,ppc_r5,-1);
      ppc_stw(iop->ob_ptr,ppc_r5,OFFSET(cpu_ppc_t,ctr),ppc_r3);
      ppc_cmpwi(iop->ob_ptr,ppc_cr0,ppc_r5,0);
      ppc_mfcr(iop->ob_ptr,ppc_r6);
      ppc_rlwinm(iop->ob_ptr,ppc_r6,ppc_r6,3,31,31);
      ppc_xori(iop->ob_ptr,ppc_r6,ppc_r6,1 ^ ctr);
      ppc_mr(iop->ob_ptr,ppc_r4,ppc_r6);
   }
   if (!((bo >> 4) & 0x01)) {
      ppc32_load_cr_bit(&iop->ob_ptr,ppc_r7,bi);
      ppc_xori(iop->ob_ptr,ppc_r7,ppc_r7,1 ^ cond);
      ppc_and(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r7);
   }

   ppc_cmpwi(iop->ob_ptr,ppc_cr0,ppc_r4,0);
   jump_ptr = iop->ob_ptr;
   ppc_bc(iop->ob_ptr,PPC_BR_TRUE_UNLIKELY,
          ppc_crbf(ppc_cr0,PPC_BR_EQ),0);

   /* Branch to LR */
   ppc_lwz(iop->ob_ptr,ppc_r5,OFFSET(cpu_ppc_t,lr),ppc_r3);
   ppc_rlwinm(iop->ob_ptr,ppc_r5,ppc_r5,0,0,29);
   ppc_stw(iop->ob_ptr,ppc_r5,OFFSET(cpu_ppc_t,ia),ppc_r3);
   ppc32_jit_tcb_push_epilog(&iop->ob_ptr);

   ppc_patch(jump_ptr,iop->ob_ptr);
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   return(0);
}

/* B - Branch */
DECLARE_INSN(B)
{
   m_uint32_t offset = bits(insn,2,25);
   m_uint32_t new_ia;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"b");

   /* compute the new ia */
   new_ia = b->start_ia + (b->ppc_trans_pos << 2);
   new_ia += sign_extend(offset << 2,26);

   if (insn & 1)
      ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));

   ppc32_set_jump(cpu,b,iop,new_ia);

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   ppc32_op_emit_branch_target(cpu,b,new_ia);
   ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1) << 2));
   return(0);
}

/* BA - Branch Absolute */
DECLARE_INSN(BA)
{
   m_uint32_t offset = bits(insn,2,25);
   m_uint32_t new_ia;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"ba");

   new_ia = sign_extend(offset << 2,26);

   if (insn & 1)
      ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));

   ppc32_set_jump(cpu,b,iop,new_ia);

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   ppc32_op_emit_branch_target(cpu,b,new_ia);
   ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1) << 2));
   return(0);
}

/* BL - Branch and Link */
DECLARE_INSN(BL)
{
   m_uint32_t offset = bits(insn,2,25);
   m_uint32_t new_ia;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"bl");

   new_ia = b->start_ia + (b->ppc_trans_pos << 2);
   new_ia += sign_extend(offset << 2,26);

   ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));
   ppc32_set_jump(cpu,b,iop,new_ia);

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   ppc32_op_emit_branch_target(cpu,b,new_ia);
   ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1) << 2));
   return(0);
}

/* BLA - Branch and Link Absolute */
DECLARE_INSN(BLA)
{
   m_uint32_t offset = bits(insn,2,25);
   m_uint32_t new_ia;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"bla");

   new_ia = sign_extend(offset << 2,26);

   ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));
   ppc32_set_jump(cpu,b,iop,new_ia);

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   ppc32_op_emit_branch_target(cpu,b,new_ia);
   ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1) << 2));
   return(0);
}

/* BC - Branch Conditional */
static int ppc32_emit_bc_generic(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                                 ppc_insn_t insn,char *name)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   int bd = bits(insn,2,15);
   jit_op_t *iop;
   u_int cr_field,cr_bit;
   m_uint32_t new_ia;
   u_char *jump_ptr;
   int local_jump;
   int cond,ctr;

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_BRANCH_JUMP);
   iop = ppc32_op_emit_insn_output(cpu,5,name);

   cond = (bo >> 3) & 0x1;
   ctr  = (bo >> 1) & 0x1;

   /* Set the return address */
   if (insn & 1) {
      ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));
      ppc32_op_emit_branch_target(cpu,b,
                                  b->start_ia+((b->ppc_trans_pos+1) << 2));
   }

   /* Compute the new ia */
   new_ia = sign_extend_32(bd << 2,16);
   if (!(insn & 0x02))
      new_ia += b->start_ia + (b->ppc_trans_pos << 2);

   /* r4 = condition (0/1), same logic as ppc32_check_cond() */
   ppc_li(iop->ob_ptr,ppc_r4,1);
   if (!(bo & 0x04)) {
      ppc_lwz(iop->ob_ptr,ppc_r5,OFFSET(cpu_ppc_t,ctr),ppc_r3);
      ppc_addic(iop->ob_ptr,ppc_r5,ppc_r5,-1);
      ppc_stw(iop->ob_ptr,ppc_r5,OFFSET(cpu_ppc_t,ctr),ppc_r3);
      ppc_cmpwi(iop->ob_ptr,ppc_cr0,ppc_r5,0);
      ppc_mfcr(iop->ob_ptr,ppc_r6);
      ppc_rlwinm(iop->ob_ptr,ppc_r6,ppc_r6,3,31,31);
      ppc_xori(iop->ob_ptr,ppc_r6,ppc_r6,1 ^ ctr);
      ppc_mr(iop->ob_ptr,ppc_r4,ppc_r6);
   }
   if (!((bo >> 4) & 0x01)) {
      cr_field = ppc32_get_cr_field(bi);
      cr_bit   = ppc32_get_cr_bit(bi);

      ppc32_op_emit_require_flags(cpu,cr_field);

      ppc32_load_cr_bit(&iop->ob_ptr,ppc_r7,bi);
      ppc_xori(iop->ob_ptr,ppc_r7,ppc_r7,1 ^ cond);
      ppc_and(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r7);
   }

   ppc_cmpwi(iop->ob_ptr,ppc_cr0,ppc_r4,0);

   local_jump = ppc32_jit_tcb_local_addr(b,new_ia,&jump_ptr);

   /* if !cond -> skip the taken path (patched once its size is known) */
   jump_ptr = iop->ob_ptr;
   ppc_bc(iop->ob_ptr,PPC_BR_TRUE_UNLIKELY,
          ppc_crbf(ppc_cr0,PPC_BR_EQ),0);

   if (local_jump) {
      /* Same page: slots for ppc32_jit_tcb_set_patch() */
      ppc32_jit_tcb_record_patch(b,iop,iop->ob_ptr,new_ia);
      ppc_b(iop->ob_ptr,0);
      ppc_nop(iop->ob_ptr);
      ppc_nop(iop->ob_ptr);
      ppc_nop(iop->ob_ptr);
   } else {
      if (cpu->exec_blk_direct_jump)
         ppc32_try_direct_far_jump(cpu,iop,new_ia);
      else {
         ppc32_set_ia(&iop->ob_ptr,new_ia);
         ppc32_jit_tcb_push_epilog(&iop->ob_ptr);
      }
   }
   ppc_patch(jump_ptr,iop->ob_ptr);

   ppc32_op_emit_branch_target(cpu,b,new_ia);
   return(0);
}

/* BC - Branch Conditional */
DECLARE_INSN(BC)
{
   return(ppc32_emit_bc_generic(cpu,b,insn,"bc"));
}

/* BC - Branch Conditional (Condition Check only) */
DECLARE_INSN(BCC)
{
   return(ppc32_emit_bc_generic(cpu,b,insn,"bcc"));
}

/* MFLR - Move From Link Register */
DECLARE_INSN(MFLR)
{
   int rt = bits(insn,21,25);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"mflr");
   ppc_lwz(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,lr),ppc_r3);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   return(0);
}

/* MTLR - Move To Link Register */
DECLARE_INSN(MTLR)
{
   int rs = bits(insn,21,25);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"mtlr");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_stw(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,lr),ppc_r3);
   return(0);
}

/* MFCTR - Move From Counter Register */
DECLARE_INSN(MFCTR)
{
   int rt = bits(insn,21,25);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"mfctr");
   ppc_lwz(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,ctr),ppc_r3);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   return(0);
}

/* MTCTR - Move To Counter Register */
DECLARE_INSN(MTCTR)
{
   int rs = bits(insn,21,25);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"mtctr");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_stw(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,ctr),ppc_r3);
   return(0);
}

/* MFTBL - Move from Time Base (Lo) */
DECLARE_INSN(MFTBL)
{
   int rd = bits(insn,21,25);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"mftbl");

   /* cpu->tb += 50 */
   ppc_lwz(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,tb),ppc_r3);
   ppc_lwz(iop->ob_ptr,ppc_r5,OFFSET(cpu_ppc_t,tb)+4,ppc_r3);
   ppc_addic(iop->ob_ptr,ppc_r5,ppc_r5,50);
   ppc_addze(iop->ob_ptr,ppc_r4,ppc_r4);
   ppc_stw(iop->ob_ptr,ppc_r5,OFFSET(cpu_ppc_t,tb)+4,ppc_r3);
   ppc_stw(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,tb),ppc_r3);

   ppc32_store_gpr(&iop->ob_ptr,rd,ppc_r5);
   return(0);
}

/* MFTBU - Move from Time Base (Up) */
DECLARE_INSN(MFTBU)
{
   int rd = bits(insn,21,25);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"mftbu");
   ppc_lwz(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,tb),ppc_r3);
   ppc32_store_gpr(&iop->ob_ptr,rd,ppc_r4);
   return(0);
}

/* ADD */
DECLARE_INSN(ADD)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"add");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_add(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* ADDC */
DECLARE_INSN(ADDC)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"addc");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_addc(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   ppc32_update_xer_ca(&iop->ob_ptr);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* ADDE - Add Extended */
DECLARE_INSN(ADDE)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"adde");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_lwz(iop->ob_ptr,ppc_r6,OFFSET(cpu_ppc_t,xer_ca),ppc_r3);
   /* r7 = rA + CA, capture carry1 */
   ppc_addc(iop->ob_ptr,ppc_r7,ppc_r4,ppc_r6);
   ppc32_get_xer_ca(&iop->ob_ptr,ppc_r8);
   /* r4 = r7 + rB, CA_out = carry1 | carry2 */
   ppc_addc(iop->ob_ptr,ppc_r4,ppc_r7,ppc_r5);
   ppc32_get_xer_ca(&iop->ob_ptr,ppc_r9);
   ppc_or(iop->ob_ptr,ppc_r9,ppc_r9,ppc_r8);
   ppc_stw(iop->ob_ptr,ppc_r9,OFFSET(cpu_ppc_t,xer_ca),ppc_r3);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* ADDI - ADD Immediate */
DECLARE_INSN(ADDI)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int si = sign_extend(bits(insn,0,15),16);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"addi");
   if (ra != 0)
      ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   else
      ppc_li(iop->ob_ptr,ppc_r4,0);
   ppc_addi(iop->ob_ptr,ppc_r4,ppc_r4,si);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   return(0);
}

/* ADDIC - ADD Immediate with Carry */
DECLARE_INSN(ADDIC)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int si = sign_extend(bits(insn,0,15),16);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"addic");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc_addic(iop->ob_ptr,ppc_r4,ppc_r4,si);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   ppc32_update_xer_ca(&iop->ob_ptr);
   return(0);
}

/* ADDIC. */
DECLARE_INSN(ADDIC_dot)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int si = sign_extend(bits(insn,0,15),16);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"addic.");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc_addic(iop->ob_ptr,ppc_r4,ppc_r4,si);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   ppc32_update_xer_ca(&iop->ob_ptr);
   ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* ADDIS - ADD Immediate Shifted */
DECLARE_INSN(ADDIS)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int si = sign_extend(bits(insn,0,15),16);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"addis");
   if (ra != 0)
      ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   else
      ppc_li(iop->ob_ptr,ppc_r4,0);
   ppc_addis(iop->ob_ptr,ppc_r4,ppc_r4,si);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   return(0);
}

/* ADDZE */
DECLARE_INSN(ADDZE)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"addze");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc_lwz(iop->ob_ptr,ppc_r5,OFFSET(cpu_ppc_t,xer_ca),ppc_r3);
   ppc_addc(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   ppc32_update_xer_ca(&iop->ob_ptr);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* ADDME - Add to Minus One Extended */
DECLARE_INSN(ADDME)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"addme");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc_lwz(iop->ob_ptr,ppc_r5,OFFSET(cpu_ppc_t,xer_ca),ppc_r3);
   /* r6 = rA + CA, capture carry1 */
   ppc_addc(iop->ob_ptr,ppc_r6,ppc_r4,ppc_r5);
   ppc32_get_xer_ca(&iop->ob_ptr,ppc_r7);
   /* r4 = r6 - 1, CA_out = carry1 | carry2 */
   ppc_addic(iop->ob_ptr,ppc_r4,ppc_r6,-1);
   ppc32_get_xer_ca(&iop->ob_ptr,ppc_r8);
   ppc_or(iop->ob_ptr,ppc_r8,ppc_r8,ppc_r7);
   ppc_stw(iop->ob_ptr,ppc_r8,OFFSET(cpu_ppc_t,xer_ca),ppc_r3);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* AND */
DECLARE_INSN(AND)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"and");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_and(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* ANDC */
DECLARE_INSN(ANDC)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"andc");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_andc(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* AND Immediate */
DECLARE_INSN(ANDI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int ui = bits(insn,0,15);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"andi.");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_andid(iop->ob_ptr,ppc_r4,ppc_r4,ui);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* AND Immediate Shifted */
DECLARE_INSN(ANDIS)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int ui = bits(insn,0,15);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"andis.");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_andisd(iop->ob_ptr,ppc_r4,ppc_r4,ui);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* CMP - Compare */
DECLARE_INSN(CMP)
{
   int bf = bits(insn,23,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"cmp");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_cmpw(iop->ob_ptr,bf,ppc_r4,ppc_r5);
   ppc32_update_cr_field(&iop->ob_ptr,bf);
   return(0);
}

/* CMPI - Compare Immediate */
DECLARE_INSN(CMPI)
{
   int bf = bits(insn,23,25);
   int ra = bits(insn,16,20);
   int si = sign_extend(bits(insn,0,15),16);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"cmpi");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc_cmpwi(iop->ob_ptr,bf,ppc_r4,si);
   ppc32_update_cr_field(&iop->ob_ptr,bf);
   return(0);
}

/* CMPL - Compare Logical */
DECLARE_INSN(CMPL)
{
   int bf = bits(insn,23,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"cmpl");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_cmplw(iop->ob_ptr,bf,ppc_r4,ppc_r5);
   ppc32_update_cr_field(&iop->ob_ptr,bf);
   return(0);
}

/* CMPLI - Compare Immediate */
DECLARE_INSN(CMPLI)
{
   int bf = bits(insn,23,25);
   int ra = bits(insn,16,20);
   int ui = bits(insn,0,15);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"cmpli");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc_cmplwi(iop->ob_ptr,bf,ppc_r4,ui);
   ppc32_update_cr_field(&iop->ob_ptr,bf);
   return(0);
}

/* CR logical operation */
static int ppc32_emit_cr_logical(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                                 ppc_insn_t insn,char *name,
                                 void (*op)(u_char **,int,int))
{
   int bd = bits(insn,21,25);
   int ba = bits(insn,16,20);
   int bb = bits(insn,11,15);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,name);
   ppc32_op_emit_require_flags(cpu,ppc32_get_cr_field(ba));
   ppc32_op_emit_require_flags(cpu,ppc32_get_cr_field(bb));
   ppc32_op_emit_require_flags(cpu,ppc32_get_cr_field(bd));
   ppc32_load_cr_bit(&iop->ob_ptr,ppc_r4,ba);
   ppc32_load_cr_bit(&iop->ob_ptr,ppc_r5,bb);
   op(&iop->ob_ptr,ppc_r4,ppc_r5);
   ppc32_store_cr_bit(&iop->ob_ptr,ppc_r6,ppc_r4,bd);
   return(0);
}

/* CRAND - Condition Register AND */
DECLARE_INSN(CRAND)  { return(ppc32_emit_cr_logical(cpu,b,insn,"crand",ppc32_cr_and)); }
/* CRANDC - Condition Register AND with Complement */
DECLARE_INSN(CRANDC) { return(ppc32_emit_cr_logical(cpu,b,insn,"crandc",ppc32_cr_andc)); }
/* CREQV - Condition Register EQV */
DECLARE_INSN(CREQV)  { return(ppc32_emit_cr_logical(cpu,b,insn,"creqv",ppc32_cr_eqv)); }
/* CRNAND - Condition Register NAND */
DECLARE_INSN(CRNAND) { return(ppc32_emit_cr_logical(cpu,b,insn,"crnand",ppc32_cr_nand)); }
/* CRNOR - Condition Register NOR */
DECLARE_INSN(CRNOR)  { return(ppc32_emit_cr_logical(cpu,b,insn,"crnor",ppc32_cr_nor)); }
/* CROR - Condition Register OR */
DECLARE_INSN(CROR)   { return(ppc32_emit_cr_logical(cpu,b,insn,"cror",ppc32_cr_or)); }
/* CRORC - Condition Register OR with Complement */
DECLARE_INSN(CRORC)  { return(ppc32_emit_cr_logical(cpu,b,insn,"crorc",ppc32_cr_orc)); }
/* CRXOR - Condition Register XOR */
DECLARE_INSN(CRXOR)  { return(ppc32_emit_cr_logical(cpu,b,insn,"crxor",ppc32_cr_xor)); }

/* DIVWU - Divide Word Unsigned */
DECLARE_INSN(DIVWU)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"divwu");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_divwu(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* DIVW - Divide Word */
DECLARE_INSN(DIVW)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"divw");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_divw(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* EQV */
DECLARE_INSN(EQV)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"eqv");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_eqv(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* EXTSB - Extend Sign Byte */
DECLARE_INSN(EXTSB)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"extsb");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_extsb(iop->ob_ptr,ppc_r4,ppc_r4);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* EXTSH - Extend Sign Word */
DECLARE_INSN(EXTSH)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"extsh");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_extsh(iop->ob_ptr,ppc_r4,ppc_r4);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* MCRF - Move Condition Register Field */
DECLARE_INSN(MCRF)
{
   int bf = bits(insn,23,25);
   int bfa = bits(insn,18,20);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"mcrf");
   ppc_lwz(iop->ob_ptr,ppc_r4,PPC32_CR_FIELD_OFFSET(bfa),ppc_r3);
   ppc_stw(iop->ob_ptr,ppc_r4,PPC32_CR_FIELD_OFFSET(bf),ppc_r3);
   return(0);
}

/* MFCR - Move from Condition Register */
DECLARE_INSN(MFCR)
{
   int rt = bits(insn,21,25);
   int i;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,5,"mfcr");
   ppc_li(iop->ob_ptr,ppc_r4,0);
   for(i=0;i<8;i++) {
      ppc_lwz(iop->ob_ptr,ppc_r5,PPC32_CR_FIELD_OFFSET(i),ppc_r3);
      ppc_rlwimi(iop->ob_ptr,ppc_r4,ppc_r5,28 - (i << 2),
                 i << 2,(i << 2) + 3);
   }
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   return(0);
}

/* MFMSR - Move from Machine State Register */
DECLARE_INSN(MFMSR)
{
   int rt = bits(insn,21,25);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"mfmsr");
   ppc_lwz(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,msr),ppc_r3);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   return(0);
}

/* MFSR - Move From Segment Register */
DECLARE_INSN(MFSR)
{
   int rt = bits(insn,21,25);
   int sr = bits(insn,16,19);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"mfsr");
   ppc_lwz(iop->ob_ptr,ppc_r4,OFFSET(cpu_ppc_t,sr)+(sr << 2),ppc_r3);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   return(0);
}

/* MTCRF - Move to Condition Register Fields */
DECLARE_INSN(MTCRF)
{
   int rs = bits(insn,21,25);
   int fxm = bits(insn,12,19);
   int i;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,5,"mtcrf");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   for(i=0;i<8;i++) {
      if (fxm & (1 << (7 - i))) {
         ppc_rlwinm(iop->ob_ptr,ppc_r5,ppc_r4,((i << 2) + 4) & 31,28,31);
         ppc_stw(iop->ob_ptr,ppc_r5,PPC32_CR_FIELD_OFFSET(i),ppc_r3);
      }
   }
   return(0);
}

/* MULHW - Multiply High Word */
DECLARE_INSN(MULHW)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"mulhw");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_mulhw(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* MULHWU - Multiply High Word Unsigned */
DECLARE_INSN(MULHWU)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"mulhwu");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_mulhwu(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* MULLI - Multiply Low Immediate */
DECLARE_INSN(MULLI)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int si = sign_extend(bits(insn,0,15),16);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"mulli");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc_mulli(iop->ob_ptr,ppc_r4,ppc_r4,si);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   return(0);
}

/* MULLW - Multiply Low Word */
DECLARE_INSN(MULLW)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"mullw");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_mullw(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* NAND */
DECLARE_INSN(NAND)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"nand");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_nand(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* NEG */
DECLARE_INSN(NEG)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"neg");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc_neg(iop->ob_ptr,ppc_r4,ppc_r4);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* NOR */
DECLARE_INSN(NOR)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"nor");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_nor(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* OR */
DECLARE_INSN(OR)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"or");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_or(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* OR with Complement */
DECLARE_INSN(ORC)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"orc");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_orc(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* OR Immediate */
DECLARE_INSN(ORI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int ui = bits(insn,0,15);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"ori");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_ori(iop->ob_ptr,ppc_r4,ppc_r4,ui);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   return(0);
}

/* OR Immediate Shifted */
DECLARE_INSN(ORIS)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int ui = bits(insn,0,15);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"oris");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_oris(iop->ob_ptr,ppc_r4,ppc_r4,ui);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   return(0);
}

/* RLWIMI - Rotate Left Word Immediate then Mask Insert */
DECLARE_INSN(RLWIMI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int sh = bits(insn,11,15);
   int mb = bits(insn,6,10);
   int me = bits(insn,1,5);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"rlwimi");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,ra);
   ppc_rlwimi(iop->ob_ptr,ppc_r5,ppc_r4,sh,mb,me);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r5);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* RLWINM - Rotate Left Word Immediate AND with Mask */
DECLARE_INSN(RLWINM)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int sh = bits(insn,11,15);
   int mb = bits(insn,6,10);
   int me = bits(insn,1,5);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"rlwinm");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_rlwinm(iop->ob_ptr,ppc_r4,ppc_r4,sh,mb,me);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* RLWNM - Rotate Left Word then Mask Insert */
DECLARE_INSN(RLWNM)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int mb = bits(insn,6,10);
   int me = bits(insn,1,5);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"rlwnm");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_rlwnm(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5,mb,me);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* Shift Left Word */
DECLARE_INSN(SLW)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"slw");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_slw(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* SRAWI - Shift Right Algebraic Word Immediate */
DECLARE_INSN(SRAWI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int sh = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"srawi");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_srawi(iop->ob_ptr,ppc_r4,ppc_r4,sh);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   ppc32_update_xer_ca(&iop->ob_ptr);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* Shift Right Word */
DECLARE_INSN(SRW)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"srw");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_srw(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* SUBF - Subtract From */
DECLARE_INSN(SUBF)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"subf");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_subf(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* SUBFC - Subtract From Carrying */
DECLARE_INSN(SUBFC)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"subfc");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_subfc(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   ppc32_update_xer_ca(&iop->ob_ptr);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* SUBFE - Subtract From Extended */
DECLARE_INSN(SUBFE)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,4,"subfe");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_lwz(iop->ob_ptr,ppc_r6,OFFSET(cpu_ppc_t,xer_ca),ppc_r3);
   /* r7 = ¬rA + CA, capture carry1 */
   ppc_nor(iop->ob_ptr,ppc_r7,ppc_r4,ppc_r4);
   ppc_addc(iop->ob_ptr,ppc_r7,ppc_r7,ppc_r6);
   ppc32_get_xer_ca(&iop->ob_ptr,ppc_r8);
   /* r4 = r7 + rB, CA_out = carry1 | carry2 */
   ppc_addc(iop->ob_ptr,ppc_r4,ppc_r7,ppc_r5);
   ppc32_get_xer_ca(&iop->ob_ptr,ppc_r9);
   ppc_or(iop->ob_ptr,ppc_r9,ppc_r9,ppc_r8);
   ppc_stw(iop->ob_ptr,ppc_r9,OFFSET(cpu_ppc_t,xer_ca),ppc_r3);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* SUBFIC - Subtract From Immediate Carrying */
DECLARE_INSN(SUBFIC)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int si = sign_extend(bits(insn,0,15),16);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"subfic");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,ra);
   ppc_subfic(iop->ob_ptr,ppc_r4,ppc_r4,si);
   ppc32_store_gpr(&iop->ob_ptr,rt,ppc_r4);
   ppc32_update_xer_ca(&iop->ob_ptr);
   return(0);
}

/* SYNC - Synchronize */
DECLARE_INSN(SYNC)
{
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,1,"sync");
   ppc_sync(iop->ob_ptr);
   return(0);
}

/* XOR */
DECLARE_INSN(XOR)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int rc = insn & 1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"xor");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc32_load_gpr(&iop->ob_ptr,ppc_r5,rb);
   ppc_xor(iop->ob_ptr,ppc_r4,ppc_r4,ppc_r5);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   if (rc)
      ppc32_update_cr0(&iop->ob_ptr,ppc_r4);
   return(0);
}

/* XORI - XOR Immediate */
DECLARE_INSN(XORI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int ui = bits(insn,0,15);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"xori");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_xori(iop->ob_ptr,ppc_r4,ppc_r4,ui);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   return(0);
}

/* XORIS - XOR Immediate Shifted */
DECLARE_INSN(XORIS)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int ui = bits(insn,0,15);
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"xoris");
   ppc32_load_gpr(&iop->ob_ptr,ppc_r4,rs);
   ppc_xoris(iop->ob_ptr,ppc_r4,ppc_r4,ui);
   ppc32_store_gpr(&iop->ob_ptr,ra,ppc_r4);
   return(0);
}

/* ======================================================================== */
/* Load / store instructions                                                */
/* ======================================================================== */

/* LBZ - Load Byte and Zero */
DECLARE_INSN(LBZ)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LBZ,ra,d,rt,
                         ppc32_memop_fast_lbz);
   return(0);
}

/* LBZU - Load Byte and Zero with Update */
DECLARE_INSN(LBZU)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop(cpu,b,PPC_MEMOP_LBZ,ra,d,rt,TRUE);
   return(0);
}

/* LBZUX - Load Byte and Zero with Update Indexed */
DECLARE_INSN(LBZUX)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_LBZ,ra,rb,rt,TRUE);
   return(0);
}

/* LBZX - Load Byte and Zero Indexed */
DECLARE_INSN(LBZX)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_LBZ,ra,rb,rt,FALSE);
   return(0);
}

/* LHA - Load Half-Word Algebraic */
DECLARE_INSN(LHA)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LHA,ra,d,rt,
                         ppc32_memop_fast_lha);
   return(0);
}

/* LHAU - Load Half-Word Algebraic with Update */
DECLARE_INSN(LHAU)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop(cpu,b,PPC_MEMOP_LHA,ra,d,rt,TRUE);
   return(0);
}

/* LHAUX - Load Half-Word Algebraic with Update Indexed */
DECLARE_INSN(LHAUX)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_LHA,ra,rb,rt,TRUE);
   return(0);
}

/* LHAX - Load Half-Word Algebraic Indexed */
DECLARE_INSN(LHAX)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_LHA,ra,rb,rt,FALSE);
   return(0);
}

/* LHZ - Load Half-Word and Zero */
DECLARE_INSN(LHZ)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LHZ,ra,d,rt,
                         ppc32_memop_fast_lhz);
   return(0);
}

/* LHZU - Load Half-Word and Zero with Update */
DECLARE_INSN(LHZU)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop(cpu,b,PPC_MEMOP_LHZ,ra,d,rt,TRUE);
   return(0);
}

/* LHZUX - Load Half-Word and Zero with Update Indexed */
DECLARE_INSN(LHZUX)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_LHZ,ra,rb,rt,TRUE);
   return(0);
}

/* LHZX - Load Half-Word and Zero Indexed */
DECLARE_INSN(LHZX)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_LHZ,ra,rb,rt,FALSE);
   return(0);
}

/* LWZ - Load Word and Zero */
DECLARE_INSN(LWZ)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LWZ,ra,d,rt,
                         ppc32_memop_fast_lwz);
   return(0);
}

/* LWZU - Load Word and Zero with Update */
DECLARE_INSN(LWZU)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop(cpu,b,PPC_MEMOP_LWZ,ra,d,rt,TRUE);
   return(0);
}

/* LWZUX - Load Word and Zero with Update Indexed */
DECLARE_INSN(LWZUX)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_LWZ,ra,rb,rt,TRUE);
   return(0);
}

/* LWZX - Load Word and Zero Indexed */
DECLARE_INSN(LWZX)
{
   int rt = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_LWZ,ra,rb,rt,FALSE);
   return(0);
}

/* STB - Store Byte */
DECLARE_INSN(STB)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop_fast(cpu,b,1,PPC_MEMOP_STB,ra,d,rs,
                         ppc32_memop_fast_stb);
   return(0);
}

/* STBU - Store Byte with Update */
DECLARE_INSN(STBU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop(cpu,b,PPC_MEMOP_STB,ra,d,rs,TRUE);
   return(0);
}

/* STBUX - Store Byte with Update Indexed */
DECLARE_INSN(STBUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_STB,ra,rb,rs,TRUE);
   return(0);
}

/* STBUX - Store Byte Indexed */
DECLARE_INSN(STBX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_STB,ra,rb,rs,FALSE);
   return(0);
}

/* STH - Store Half-Word */
DECLARE_INSN(STH)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop_fast(cpu,b,1,PPC_MEMOP_STH,ra,d,rs,
                         ppc32_memop_fast_sth);
   return(0);
}

/* STHU - Store Half-Word with Update */
DECLARE_INSN(STHU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop(cpu,b,PPC_MEMOP_STH,ra,d,rs,TRUE);
   return(0);
}

/* STHUX - Store Half-Word with Update Indexed */
DECLARE_INSN(STHUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_STH,ra,rb,rs,TRUE);
   return(0);
}

/* STHUX - Store Half-Word Indexed */
DECLARE_INSN(STHX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_STH,ra,rb,rs,FALSE);
   return(0);
}

/* STW - Store Word */
DECLARE_INSN(STW)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop_fast(cpu,b,1,PPC_MEMOP_STW,ra,d,rs,
                         ppc32_memop_fast_stw);
   return(0);
}

/* STWU - Store Word with Update */
DECLARE_INSN(STWU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int d  = sign_extend(bits(insn,0,15),16);
   ppc32_emit_memop(cpu,b,PPC_MEMOP_STW,ra,d,rs,TRUE);
   return(0);
}

/* STWUX - Store Word with Update Indexed */
DECLARE_INSN(STWUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_STW,ra,rb,rs,TRUE);
   return(0);
}

/* STWUX - Store Word Indexed */
DECLARE_INSN(STWX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   ppc32_emit_memop_idx(cpu,b,PPC_MEMOP_STW,ra,rb,rs,FALSE);
   return(0);
}

/* ======================================================================== */
/* Instruction lookup table                                                 */
/* ======================================================================== */

struct ppc32_insn_tag ppc32_insn_tags[] = {
   { ppc32_emit_BLR        , 0xfffffffe , 0x4e800020 },
   { ppc32_emit_BCTR       , 0xfffffffe , 0x4e800420 },
   { ppc32_emit_MFLR       , 0xfc1fffff , 0x7c0802a6 },
   { ppc32_emit_MTLR       , 0xfc1fffff , 0x7c0803a6 },
   { ppc32_emit_MFCTR      , 0xfc1fffff , 0x7c0902a6 },
   { ppc32_emit_MTCTR      , 0xfc1fffff , 0x7c0903a6 },
   { ppc32_emit_MFTBL      , 0xfc1ff7ff , 0x7c0c42e6 },
   { ppc32_emit_MFTBU      , 0xfc1ff7ff , 0x7c0d42e6 },
   { ppc32_emit_ADD        , 0xfc0007fe , 0x7c000214 },
   { ppc32_emit_ADDC       , 0xfc0007fe , 0x7c000014 },
   { ppc32_emit_ADDE       , 0xfc0007fe , 0x7c000114 },
   { ppc32_emit_ADDI       , 0xfc000000 , 0x38000000 },
   { ppc32_emit_ADDIC      , 0xfc000000 , 0x30000000 },
   { ppc32_emit_ADDIC_dot  , 0xfc000000 , 0x34000000 },
   { ppc32_emit_ADDIS      , 0xfc000000 , 0x3c000000 },
   { ppc32_emit_ADDZE      , 0xfc00fffe , 0x7c000194 },
   { ppc32_emit_ADDME      , 0xfc00fffe , 0x7c0001d4 },
   { ppc32_emit_AND        , 0xfc0007fe , 0x7c000038 },
   { ppc32_emit_ANDC       , 0xfc0007fe , 0x7c000078 },
   { ppc32_emit_ANDI       , 0xfc000000 , 0x70000000 },
   { ppc32_emit_ANDIS      , 0xfc000000 , 0x74000000 },
   { ppc32_emit_B          , 0xfc000003 , 0x48000000 },
   { ppc32_emit_BA         , 0xfc000003 , 0x48000002 },
   { ppc32_emit_BL         , 0xfc000003 , 0x48000001 },
   { ppc32_emit_BLA        , 0xfc000003 , 0x48000003 },
   { ppc32_emit_BCC        , 0xfe800000 , 0x40800000 },
   { ppc32_emit_BC         , 0xfc000000 , 0x40000000 },
   { ppc32_emit_BCLR       , 0xfc00fffe , 0x4c000020 },
   { ppc32_emit_CMP        , 0xfc6007ff , 0x7c000000 },
   { ppc32_emit_CMPI       , 0xfc600000 , 0x2c000000 },
   { ppc32_emit_CMPL       , 0xfc6007ff , 0x7c000040 },
   { ppc32_emit_CMPLI      , 0xfc600000 , 0x28000000 },
   { ppc32_emit_CRAND      , 0xfc0007ff , 0x4c000202 },
   { ppc32_emit_CRANDC     , 0xfc0007ff , 0x4c000102 },
   { ppc32_emit_CREQV      , 0xfc0007ff , 0x4c000242 },
   { ppc32_emit_CRNAND     , 0xfc0007ff , 0x4c0001c2 },
   { ppc32_emit_CRNOR      , 0xfc0007ff , 0x4c000042 },
   { ppc32_emit_CROR       , 0xfc0007ff , 0x4c000382 },
   { ppc32_emit_CRORC      , 0xfc0007ff , 0x4c000342 },
   { ppc32_emit_CRXOR      , 0xfc0007ff , 0x4c000182 },
   { ppc32_emit_DIVWU      , 0xfc0007fe , 0x7c000396 },
   { ppc32_emit_DIVW       , 0xfc0007fe , 0x7c0003d6 },
   { ppc32_emit_EQV        , 0xfc0007fe , 0x7c000238 },
   { ppc32_emit_EXTSB      , 0xfc00fffe , 0x7c000774 },
   { ppc32_emit_EXTSH      , 0xfc00fffe , 0x7c000734 },
   { ppc32_emit_LBZ        , 0xfc000000 , 0x88000000 },
   { ppc32_emit_LBZU       , 0xfc000000 , 0x8c000000 },
   { ppc32_emit_LBZUX      , 0xfc0007ff , 0x7c0000ee },
   { ppc32_emit_LBZX       , 0xfc0007ff , 0x7c0000ae },
   { ppc32_emit_LHA        , 0xfc000000 , 0xa8000000 },
   { ppc32_emit_LHAU       , 0xfc000000 , 0xac000000 },
   { ppc32_emit_LHAUX      , 0xfc0007ff , 0x7c0002ee },
   { ppc32_emit_LHAX       , 0xfc0007ff , 0x7c0002ae },
   { ppc32_emit_LHZ        , 0xfc000000 , 0xa0000000 },
   { ppc32_emit_LHZU       , 0xfc000000 , 0xa4000000 },
   { ppc32_emit_LHZUX      , 0xfc0007ff , 0x7c00026e },
   { ppc32_emit_LHZX       , 0xfc0007ff , 0x7c00022e },
   { ppc32_emit_LWZ        , 0xfc000000 , 0x80000000 },
   { ppc32_emit_LWZU       , 0xfc000000 , 0x84000000 },
   { ppc32_emit_LWZUX      , 0xfc0007ff , 0x7c00006e },
   { ppc32_emit_LWZX       , 0xfc0007ff , 0x7c00002e },
   { ppc32_emit_MCRF       , 0xfc63ffff , 0x4c000000 },
   { ppc32_emit_MFCR       , 0xfc1fffff , 0x7c000026 },
   { ppc32_emit_MFMSR      , 0xfc1fffff , 0x7c0000a6 },
   { ppc32_emit_MFSR       , 0xfc10ffff , 0x7c0004a6 },
   { ppc32_emit_MTCRF      , 0xfc100fff , 0x7c000120 },
   { ppc32_emit_MULHW      , 0xfc0007fe , 0x7c000096 },
   { ppc32_emit_MULHWU     , 0xfc0007fe , 0x7c000016 },
   { ppc32_emit_MULLI      , 0xfc000000 , 0x1c000000 },
   { ppc32_emit_MULLW      , 0xfc0007fe , 0x7c0001d6 },
   { ppc32_emit_NAND       , 0xfc0007fe , 0x7c0003b8 },
   { ppc32_emit_NEG        , 0xfc00fffe , 0x7c0000d0 },
   { ppc32_emit_NOR        , 0xfc0007fe , 0x7c0000f8 },
   { ppc32_emit_OR         , 0xfc0007fe , 0x7c000378 },
   { ppc32_emit_ORC        , 0xfc0007fe , 0x7c000338 },
   { ppc32_emit_ORI        , 0xfc000000 , 0x60000000 },
   { ppc32_emit_ORIS       , 0xfc000000 , 0x64000000 },
   { ppc32_emit_RLWIMI     , 0xfc000000 , 0x50000000 },
   { ppc32_emit_RLWINM     , 0xfc000000 , 0x54000000 },
   { ppc32_emit_RLWNM      , 0xfc000000 , 0x5c000000 },
   { ppc32_emit_SLW        , 0xfc0007fe , 0x7c000030 },
   { ppc32_emit_SRAWI      , 0xfc0007fe , 0x7c000670 },
   { ppc32_emit_SRW        , 0xfc0007fe , 0x7c000430 },
   { ppc32_emit_STB        , 0xfc000000 , 0x98000000 },
   { ppc32_emit_STBU       , 0xfc000000 , 0x9c000000 },
   { ppc32_emit_STBUX      , 0xfc0007ff , 0x7c0001ee },
   { ppc32_emit_STBX       , 0xfc0007ff , 0x7c0001ae },
   { ppc32_emit_STH        , 0xfc000000 , 0xb0000000 },
   { ppc32_emit_STHU       , 0xfc000000 , 0xb4000000 },
   { ppc32_emit_STHUX      , 0xfc0007ff , 0x7c00036e },
   { ppc32_emit_STHX       , 0xfc0007ff , 0x7c00032e },
   { ppc32_emit_STW        , 0xfc000000 , 0x90000000 },
   { ppc32_emit_STWU       , 0xfc000000 , 0x94000000 },
   { ppc32_emit_STWUX      , 0xfc0007ff , 0x7c00016e },
   { ppc32_emit_STWX       , 0xfc0007ff , 0x7c00012e },
   { ppc32_emit_SUBF       , 0xfc0007fe , 0x7c000050 },
   { ppc32_emit_SUBFC      , 0xfc0007fe , 0x7c000010 },
   { ppc32_emit_SUBFE      , 0xfc0007fe , 0x7c000110 },
   { ppc32_emit_SUBFIC     , 0xfc000000 , 0x20000000 },
   { ppc32_emit_SYNC       , 0xffffffff , 0x7c0004ac },
   { ppc32_emit_XOR        , 0xfc0007fe , 0x7c000278 },
   { ppc32_emit_XORI       , 0xfc000000 , 0x68000000 },
   { ppc32_emit_XORIS      , 0xfc000000 , 0x6c000000 },
   { ppc32_emit_unknown    , 0x00000000 , 0x00000000 },
   { NULL                  , 0x00000000 , 0x00000000 },
};
