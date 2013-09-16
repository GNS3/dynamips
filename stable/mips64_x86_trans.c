/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "cpu.h"
#include "mips64_jit.h"
#include "mips64_x86_trans.h"
#include "mips64_cp0.h"
#include "memory.h"

/* Macros for CPU structure access */
#define REG_OFFSET(reg)       (OFFSET(cpu_mips_t,gpr[(reg)]))
#define CP0_REG_OFFSET(c0reg) (OFFSET(cpu_mips_t,cp0.reg[(c0reg)]))
#define MEMOP_OFFSET(op)      (OFFSET(cpu_mips_t,mem_op_fn[(op)]))

#define DECLARE_INSN(name) \
   static int mips64_emit_##name(cpu_mips_t *cpu,mips64_jit_tcb_t *b, \
                                 mips_insn_t insn)

/* Set an IRQ */
void mips64_set_irq(cpu_mips_t *cpu,m_uint8_t irq)
{
   m_uint32_t m;
   m = (1 << (irq + MIPS_CP0_CAUSE_ISHIFT)) & MIPS_CP0_CAUSE_IMASK;
   atomic_or(&cpu->irq_cause,m);
}

/* Clear an IRQ */
void mips64_clear_irq(cpu_mips_t *cpu,m_uint8_t irq)
{
   m_uint32_t m;
   m = (1 << (irq + MIPS_CP0_CAUSE_ISHIFT)) & MIPS_CP0_CAUSE_IMASK;
   atomic_and(&cpu->irq_cause,~m);

   if (!cpu->irq_cause)
      cpu->irq_pending = 0;
}

/* Load a 64 bit immediate value */
static inline void mips64_load_imm(mips64_jit_tcb_t *b,
                                   u_int hi_reg,u_int lo_reg,
                                   m_uint64_t value)
{
   m_uint32_t hi_val = value >> 32;
   m_uint32_t lo_val = value & 0xffffffff;

   if (lo_val)
      x86_mov_reg_imm(b->jit_ptr,lo_reg,lo_val);
   else
      x86_alu_reg_reg(b->jit_ptr,X86_XOR,lo_reg,lo_reg);
   
   if (hi_val)
      x86_mov_reg_imm(b->jit_ptr,hi_reg,hi_val);
   else
      x86_alu_reg_reg(b->jit_ptr,X86_XOR,hi_reg,hi_reg);
}

/* Set the Pointer Counter (PC) register */
void mips64_set_pc(mips64_jit_tcb_t *b,m_uint64_t new_pc)
{
   x86_mov_membase_imm(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc),
                       new_pc & 0xFFFFFFFF,4);
   x86_mov_membase_imm(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc)+4,
                       new_pc >> 32,4);
}

/* Set the Return Address (RA) register */
void mips64_set_ra(mips64_jit_tcb_t *b,m_uint64_t ret_pc)
{
   mips64_load_imm(b,X86_EDX,X86_EAX,ret_pc);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(MIPS_GPR_RA),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(MIPS_GPR_RA)+4,X86_EDX,4);
}

/* 
 * Try to branch directly to the specified JIT block without returning to 
 * main loop.
 */
static void mips64_try_direct_far_jump(cpu_mips_t *cpu,mips64_jit_tcb_t *b,
                                       m_uint64_t new_pc)
{
   m_uint64_t new_page;
   m_uint32_t pc_hash,pc_offset;
   u_char *test1,*test2,*test3,*test4;

   new_page = new_pc & MIPS_MIN_PAGE_MASK;
   pc_offset = (new_pc & MIPS_MIN_PAGE_IMASK) >> 2;
   pc_hash = mips64_jit_get_pc_hash(new_pc);

   /* Get JIT block info in %edx */
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,
                       X86_EDI,OFFSET(cpu_mips_t,exec_blk_map),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,X86_EBX,pc_hash*sizeof(void *),4);

   /* no JIT block found ? */
   x86_test_reg_reg(b->jit_ptr,X86_EDX,X86_EDX);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_Z, 0, 1);

   /* Check block PC (lower 32-bits first) */
   x86_mov_reg_imm(b->jit_ptr,X86_EAX,(m_uint32_t)new_page);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_EAX,X86_EDX,
                       OFFSET(mips64_jit_tcb_t,start_pc));
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* Check higher bits... */
   x86_mov_reg_imm(b->jit_ptr,X86_ECX,new_page >> 32);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_ECX,X86_EDX,
                       OFFSET(mips64_jit_tcb_t,start_pc)+4);
   test3 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* Jump to the code */
   x86_mov_reg_membase(b->jit_ptr,X86_ESI,
                       X86_EDX,OFFSET(mips64_jit_tcb_t,jit_insn_ptr),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,
                       X86_ESI,pc_offset * sizeof(void *),4);
   
   x86_test_reg_reg(b->jit_ptr,X86_EBX,X86_EBX);
   test4 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_Z, 0, 1);
   x86_jump_reg(b->jit_ptr,X86_EBX);

   /* Returns to caller... */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);
   x86_patch(test4,b->jit_ptr);

   mips64_set_pc(b,new_pc);
   mips64_jit_tcb_push_epilog(b);
}

/* Set Jump */
static void mips64_set_jump(cpu_mips_t *cpu,mips64_jit_tcb_t *b,
                            m_uint64_t new_pc,int local_jump)
{      
   int return_to_caller = FALSE;
   u_char *jump_ptr;

   if (cpu->sym_trace && !local_jump)
      return_to_caller = TRUE;

   if (!return_to_caller && mips64_jit_tcb_local_addr(b,new_pc,&jump_ptr)) {
      if (jump_ptr) {
         x86_jump_code(b->jit_ptr,jump_ptr);
      } else {
         /* Never jump directly to code in a delay slot */
         if (mips64_jit_is_delay_slot(b,new_pc)) {
            mips64_set_pc(b,new_pc);
            mips64_jit_tcb_push_epilog(b);
            return;
         }

         mips64_jit_tcb_record_patch(b,b->jit_ptr,new_pc);
         x86_jump32(b->jit_ptr,0);
      }
   } else {
      if (cpu->exec_blk_direct_jump) {
         /* Block lookup optimization */
         mips64_try_direct_far_jump(cpu,b,new_pc);
      } else {
         mips64_set_pc(b,new_pc);
         mips64_jit_tcb_push_epilog(b);
      }
   }
}

/* Basic C call */
static forced_inline void mips64_emit_basic_c_call(mips64_jit_tcb_t *b,void *f)
{
   x86_mov_reg_imm(b->jit_ptr,X86_EBX,f);
   x86_call_reg(b->jit_ptr,X86_EBX);
}

/* Emit a simple call to a C function without any parameter */
static void mips64_emit_c_call(mips64_jit_tcb_t *b,void *f)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   mips64_emit_basic_c_call(b,f);
}

/* Single-step operation */
void mips64_emit_single_step(mips64_jit_tcb_t *b,mips_insn_t insn)
{
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   x86_mov_reg_imm(b->jit_ptr,X86_EDX,insn);
   mips64_emit_basic_c_call(b,mips64_exec_single_step);
}

/* Fast memory operation prototype */
typedef void (*memop_fast_access)(mips64_jit_tcb_t *b,int target);

/* Fast LW */
static void mips64_memop_fast_lw(mips64_jit_tcb_t *b,int target)
{
   x86_mov_reg_memindex(b->jit_ptr,X86_EAX,X86_EAX,0,X86_EBX,0,4);
   x86_bswap(b->jit_ptr,X86_EAX);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(target),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(target)+4,X86_EDX,4);
}

/* Fast SW */
static void mips64_memop_fast_sw(mips64_jit_tcb_t *b,int target)
{
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,X86_EDI,REG_OFFSET(target),4);
   x86_bswap(b->jit_ptr,X86_EDX);
   x86_mov_memindex_reg(b->jit_ptr,X86_EAX,0,X86_EBX,0,X86_EDX,4);
}

/* Fast memory operation (64-bit) */
static void mips64_emit_memop_fast64(mips64_jit_tcb_t *b,int write_op,
                                     int opcode,int base,int offset,
                                     int target,int keep_ll_bit,
                                     memop_fast_access op_handler)
{
   m_uint64_t val = sign_extend(offset,16);
   u_char *test1,*test2,*test3,*p_exit;

   test3 = NULL;

   /* ECX:EBX = sign-extended offset */
   mips64_load_imm(b,X86_ECX,X86_EBX,val);

   /* ECX:EBX = GPR[base] + sign-extended offset */
   x86_alu_reg_membase(b->jit_ptr,X86_ADD,X86_EBX,X86_EDI,REG_OFFSET(base));
   x86_alu_reg_membase(b->jit_ptr,X86_ADC,X86_ECX,X86_EDI,REG_OFFSET(base)+4);

   /* EAX = mts32_entry index */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EBX,4);
   x86_shift_reg_imm(b->jit_ptr,X86_SHR,X86_EAX,MTS32_HASH_SHIFT);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_EAX,MTS32_HASH_MASK);

   /* EDX = mts32_entry */
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,
                       X86_EDI,OFFSET(cpu_mips_t,mts_u.mts64_cache),
                       4);
   x86_shift_reg_imm(b->jit_ptr,X86_SHL,X86_EAX,5);
   x86_alu_reg_reg(b->jit_ptr,X86_ADD,X86_EDX,X86_EAX);

   /* Compare virtual page address (ESI = vpage) */
   x86_mov_reg_reg(b->jit_ptr,X86_ESI,X86_EBX,4);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_ESI,MIPS_MIN_PAGE_MASK);

   /* Compare the high part of the vaddr */
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_ECX,X86_EDX,
                       OFFSET(mts64_entry_t,gvpa)+4);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* Compare the low part of the vaddr */
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_ESI,X86_EDX,
                       OFFSET(mts64_entry_t,gvpa));
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* Test if we are writing to a COW page */
   if (write_op) {
      x86_test_membase_imm(b->jit_ptr,X86_EDX,OFFSET(mts64_entry_t,flags),
                           MTS_FLAG_COW);
      test3 = b->jit_ptr;
      x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);
   }

   /* EBX = offset in page, EAX = Host Page Address */
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_EBX,MIPS_MIN_PAGE_IMASK);
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDX,OFFSET(mts64_entry_t,hpa),4);

   /* Memory access */
   op_handler(b,target);
 
   p_exit = b->jit_ptr;
   x86_jump8(b->jit_ptr,0);

   /* === Slow lookup === */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);
   if (test3)
      x86_patch(test3,b->jit_ptr);

   /* Update PC (ECX:EBX = vaddr) */
   x86_mov_reg_reg(b->jit_ptr,X86_ESI,X86_EBX,4);
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   x86_mov_reg_reg(b->jit_ptr,X86_EDX,X86_ESI,4);

   /* EBX = target register */
   x86_mov_reg_imm(b->jit_ptr,X86_EBX,target);

   /* EAX = CPU instance pointer */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);

   /* 
    * Push parameters on stack and call memory function.
    * Keep the stack aligned on a 16-byte boundary for Darwin/x86.
    */
   x86_alu_reg_imm(b->jit_ptr,X86_SUB,X86_ESP,8);
   x86_push_reg(b->jit_ptr,X86_EBX);
   x86_call_membase(b->jit_ptr,X86_EDI,MEMOP_OFFSET(opcode));
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);

   x86_patch(p_exit,b->jit_ptr);
}

/* Fast memory operation (32-bit) */
static void mips64_emit_memop_fast32(mips64_jit_tcb_t *b,int write_op,
                                     int opcode,int base,int offset,
                                     int target,int keep_ll_bit,
                                     memop_fast_access op_handler)
{
   m_uint32_t val = sign_extend(offset,16);
   u_char *test1,*test2,*p_exit;

   test2 = NULL;

   /* EBX = sign-extended offset */
   x86_mov_reg_imm(b->jit_ptr,X86_EBX,val);

   /* EBX = GPR[base] + sign-extended offset */
   x86_alu_reg_membase(b->jit_ptr,X86_ADD,X86_EBX,X86_EDI,REG_OFFSET(base));

   /* EAX = mts32_entry index */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EBX,4);
   x86_shift_reg_imm(b->jit_ptr,X86_SHR,X86_EAX,MTS32_HASH_SHIFT);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_EAX,MTS32_HASH_MASK);

   /* EDX = mts32_entry */
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,
                       X86_EDI,OFFSET(cpu_mips_t,mts_u.mts32_cache),
                       4);
   x86_shift_reg_imm(b->jit_ptr,X86_SHL,X86_EAX,4);
   x86_alu_reg_reg(b->jit_ptr,X86_ADD,X86_EDX,X86_EAX);

   /* Compare virtual page address (ESI = vpage) */
   x86_mov_reg_reg(b->jit_ptr,X86_ESI,X86_EBX,4);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_ESI,PPC32_MIN_PAGE_MASK);

   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_ESI,X86_EDX,
                       OFFSET(mts32_entry_t,gvpa));
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* Test if we are writing to a COW page */
   if (write_op) {
      x86_test_membase_imm(b->jit_ptr,X86_EDX,OFFSET(mts32_entry_t,flags),
                           MTS_FLAG_COW);
      test2 = b->jit_ptr;
      x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);
   }

   /* EBX = offset in page, EAX = Host Page Address */
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_EBX,PPC32_MIN_PAGE_IMASK);
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDX,OFFSET(mts32_entry_t,hpa),4);

   /* Memory access */
   op_handler(b,target);
 
   p_exit = b->jit_ptr;
   x86_jump8(b->jit_ptr,0);

   /* === Slow lookup === */
   x86_patch(test1,b->jit_ptr);
   if (test2)
      x86_patch(test2,b->jit_ptr);

   /* Update PC (EBX = vaddr) */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   /* Sign-extend virtual address and put vaddr in ECX:EDX */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EBX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_reg_reg(b->jit_ptr,X86_ECX,X86_EDX,4);
   x86_mov_reg_reg(b->jit_ptr,X86_EDX,X86_EAX,4);

   /* EBX = target register */
   x86_mov_reg_imm(b->jit_ptr,X86_EBX,target);

   /* EAX = CPU instance pointer */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);

   /* 
    * Push parameters on stack and call memory function.
    * Keep the stack aligned on a 16-byte boundary for Darwin/x86.
    */
   x86_alu_reg_imm(b->jit_ptr,X86_SUB,X86_ESP,8);
   x86_push_reg(b->jit_ptr,X86_EBX);
   x86_call_membase(b->jit_ptr,X86_EDI,MEMOP_OFFSET(opcode));
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);

   x86_patch(p_exit,b->jit_ptr);
}

/* Fast memory operation */
static void mips64_emit_memop_fast(cpu_mips_t *cpu,mips64_jit_tcb_t *b,
                                   int write_op,int opcode,
                                   int base,int offset,
                                   int target,int keep_ll_bit,
                                   memop_fast_access op_handler)
{
   switch(cpu->addr_mode) {
      case 32:
         mips64_emit_memop_fast32(b,write_op,opcode,base,offset,target,
                                  keep_ll_bit,op_handler);
         break;
      case 64:
         mips64_emit_memop_fast64(b,write_op,opcode,base,offset,target,
                                  keep_ll_bit,op_handler);
         break;
   }
}

/* Memory operation */
static void mips64_emit_memop(mips64_jit_tcb_t *b,int op,int base,int offset,
                              int target,int keep_ll_bit)
{
   m_uint64_t val = sign_extend(offset,16);

   /* Save PC for exception handling */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   if (!keep_ll_bit) {
      x86_clear_reg(b->jit_ptr,X86_EAX);
      x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,ll_bit),
                          X86_EAX,4);
   }

   /* ECX:EDX = sign-extended offset */
   mips64_load_imm(b,X86_ECX,X86_EDX,val);

   /* ECX:EDX = GPR[base] + sign-extended offset */
   x86_alu_reg_membase(b->jit_ptr,X86_ADD,X86_EDX,X86_EDI,REG_OFFSET(base));
   x86_alu_reg_membase(b->jit_ptr,X86_ADC,X86_ECX,X86_EDI,REG_OFFSET(base)+4);

   /* EBX = target register */
   x86_mov_reg_imm(b->jit_ptr,X86_EBX,target);
   
   /* EAX = CPU instance pointer */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);

   /* 
    * Push parameters on stack and call memory function.
    * Keep the stack aligned on a 16-byte boundary for Darwin/x86.
    */
   x86_alu_reg_imm(b->jit_ptr,X86_SUB,X86_ESP,8);
   x86_push_reg(b->jit_ptr,X86_EBX);
   x86_call_membase(b->jit_ptr,X86_EDI,MEMOP_OFFSET(op));
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);
}

/* Coprocessor Register transfert operation */
static void mips64_emit_cp_xfr_op(mips64_jit_tcb_t *b,int rt,int rd,void *f)
{
   /* update pc */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   /* cp0 register */
   x86_mov_reg_imm(b->jit_ptr,X86_ECX,rd);

   /* gpr */
   x86_mov_reg_imm(b->jit_ptr,X86_EDX,rt);

   /* cpu instance */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);

   mips64_emit_basic_c_call(b,f);
}

/* Virtual Breakpoint */
void mips64_emit_breakpoint(mips64_jit_tcb_t *b)
{
   x86_alu_reg_imm(b->jit_ptr,X86_SUB,X86_ESP,12);
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_c_call(b,mips64_run_breakpoint);
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);
}

/* Unknown opcode handler */
static asmlinkage void mips64_unknown_opcode(cpu_mips_t *cpu,m_uint32_t opcode)
{
   printf("MIPS64: unhandled opcode 0x%8.8x at 0x%llx (ra=0x%llx)\n",
          opcode,cpu->pc,cpu->gpr[MIPS_GPR_RA]);

   mips64_dump_regs(cpu->gen);
}

/* Emit unhandled instruction code */
static int mips64_emit_unknown(cpu_mips_t *cpu,mips64_jit_tcb_t *b,
                               mips_insn_t opcode)
{   
   x86_mov_reg_imm(b->jit_ptr,X86_EAX,opcode);
   x86_alu_reg_imm(b->jit_ptr,X86_SUB,X86_ESP,4);
   x86_push_reg(b->jit_ptr,X86_EAX);
   x86_push_reg(b->jit_ptr,X86_EDI);
   mips64_emit_c_call(b,mips64_unknown_opcode);
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);
   return(0);
}

/* Invalid delay slot handler */
static fastcall void mips64_invalid_delay_slot(cpu_mips_t *cpu)
{
   printf("MIPS64: invalid instruction in delay slot at 0x%llx (ra=0x%llx)\n",
          cpu->pc,cpu->gpr[MIPS_GPR_RA]);

   mips64_dump_regs(cpu->gen);

   /* Halt the virtual CPU */
   cpu->pc = 0;
}

/* Emit unhandled instruction code */
int mips64_emit_invalid_delay_slot(mips64_jit_tcb_t *b)
{   
   x86_alu_reg_imm(b->jit_ptr,X86_SUB,X86_ESP,12);
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_c_call(b,mips64_invalid_delay_slot);
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);
   
   mips64_jit_tcb_push_epilog(b);
   return(0);
}

/* 
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
void mips64_inc_cp0_count_reg(mips64_jit_tcb_t *b)
{
   x86_inc_membase(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,cp0_virt_cnt_reg));
}

/* Check if there are pending IRQ */
void mips64_check_pending_irq(mips64_jit_tcb_t *b)
{
   u_char *test1;

   /* Check the pending IRQ flag */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,
                       X86_EDI,OFFSET(cpu_mips_t,irq_pending),4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_Z, 0, 1);

   /* Save PC */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   /* Trigger the IRQ */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_trigger_irq);
   mips64_jit_tcb_push_epilog(b);

   x86_patch(test1,b->jit_ptr);
}

/* Increment the number of executed instructions (performance debugging) */
void mips64_inc_perf_counter(mips64_jit_tcb_t *b)
{
   x86_inc_membase(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,perf_counter));
}

/* ADD */
DECLARE_INSN(ADD)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   /* TODO: Exception handling */

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_membase(b->jit_ptr,X86_ADD,X86_EAX,X86_EDI,REG_OFFSET(rt));
   
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* ADDI */
DECLARE_INSN(ADDI)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);

   /* TODO: Exception handling */

   x86_mov_reg_imm(b->jit_ptr,X86_EAX,val & 0xffffffff);
   x86_alu_reg_membase(b->jit_ptr,X86_ADD,X86_EAX,X86_EDI,REG_OFFSET(rs));

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt)+4,X86_EDX,4);
   return(0);
}

/* ADDIU */
DECLARE_INSN(ADDIU)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);

   x86_mov_reg_imm(b->jit_ptr,X86_EAX,val & 0xffffffff);
   x86_alu_reg_membase(b->jit_ptr,X86_ADD,X86_EAX,X86_EDI,REG_OFFSET(rs));

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt)+4,X86_EDX,4);
   return(0);
}

/* ADDU */
DECLARE_INSN(ADDU)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_membase(b->jit_ptr,X86_ADD,X86_EAX,X86_EDI,REG_OFFSET(rt));
   
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* AND */
DECLARE_INSN(AND)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_alu_reg_membase(b->jit_ptr,X86_AND,X86_EAX,X86_EDI,REG_OFFSET(rt));
   x86_alu_reg_membase(b->jit_ptr,X86_AND,X86_EBX,X86_EDI,REG_OFFSET(rt)+4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* ANDI */
DECLARE_INSN(ANDI)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   x86_mov_reg_imm(b->jit_ptr,X86_EAX,imm);
   x86_alu_reg_reg(b->jit_ptr,X86_XOR,X86_EBX,X86_EBX);

   x86_alu_reg_membase(b->jit_ptr,X86_AND,X86_EAX,X86_EDI,REG_OFFSET(rs));

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt)+4,X86_EBX,4);

   return(0);
}

/* B (Branch, virtual instruction) */
DECLARE_INSN(B)
{
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);
   return(0);
}

/* BAL (Branch and Link, virtual instruction) */
DECLARE_INSN(BAL)
{
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* set the return address (instruction after the delay slot) */
   mips64_set_ra(b,b->start_pc + ((b->mips_trans_pos + 1) << 2));

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,0);
   return(0);
}

/* BEQ (Branch On Equal) */
DECLARE_INSN(BEQ)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int offset = bits(insn,0,15);
   u_char *test1,*test2;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);
   
   /* 
    * compare gpr[rs] and gpr[rt]. 
    * compare the low 32 bits first (higher probability).
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_EAX,X86_EDI,REG_OFFSET(rt));
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NE, 0, 1);

   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_EBX,X86_EDI,REG_OFFSET(rt)+4);
   test2 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NE, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BEQL (Branch On Equal Likely) */
DECLARE_INSN(BEQL)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int offset = bits(insn,0,15);
   u_char *test1,*test2;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* 
    * compare gpr[rs] and gpr[rt]. 
    * compare the low 32 bits first (higher probability).
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_EAX,X86_EDI,REG_OFFSET(rt));
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NE, 0, 1);

   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_EBX,X86_EDI,REG_OFFSET(rt)+4);
   test2 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NE, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);
   return(0);
}

/* BEQZ (Branch On Equal Zero - optimization) */
DECLARE_INSN(BEQZ)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1,*test2;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);
   
   /* 
    * compare gpr[rs] with 0.
    * compare the low 32 bits first (higher probability).
    */
   x86_alu_membase_imm(b->jit_ptr,X86_CMP,X86_EDI,REG_OFFSET(rs),0);
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NE, 0, 1);

   x86_alu_membase_imm(b->jit_ptr,X86_CMP,X86_EDI,REG_OFFSET(rs)+4,0);
   test2 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NE, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BNEZ (Branch On Not Equal Zero - optimization) */
DECLARE_INSN(BNEZ)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1,*test2;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);
   
   /* 
    * compare gpr[rs] with 0.
    * compare the low 32 bits first (higher probability).
    */
   x86_alu_membase_imm(b->jit_ptr,X86_CMP,X86_EDI,REG_OFFSET(rs),0);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   x86_alu_membase_imm(b->jit_ptr,X86_CMP,X86_EDI,REG_OFFSET(rs)+4,0);
   test2 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_E, 0, 1);

   x86_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test2,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGEZ (Branch On Greater or Equal Than Zero) */
DECLARE_INSN(BGEZ)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* If sign bit is set, don't take the branch */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGEZAL (Branch On Greater or Equal Than Zero And Link) */
DECLARE_INSN(BGEZAL)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* set the return address (instruction after the delay slot) */
   mips64_set_ra(b,b->start_pc + ((b->mips_trans_pos + 1) << 2));

   /* If sign bit is set, don't take the branch */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGEZALL (Branch On Greater or Equal Than Zero and Link Likely) */
DECLARE_INSN(BGEZALL)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* set the return address (instruction after the delay slot) */
   mips64_set_ra(b,b->start_pc + ((b->mips_trans_pos + 1) << 2));

   /* if sign bit is set, don't take the branch */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   return(0);
}

/* BGEZL (Branch On Greater or Equal Than Zero Likely) */
DECLARE_INSN(BGEZL)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* if sign bit is set, don't take the branch */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   return(0);
}

/* BGTZ (Branch On Greater Than Zero) */
DECLARE_INSN(BGTZ)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1,*test2,*test3;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* 
    * test the hi word of gpr[rs]
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_S, 0, 1);
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* test the lo word of gpr[rs] (here hi word = 0) */
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs),4);
   x86_test_reg_reg(b->jit_ptr,X86_EBX,X86_EBX);
   test3 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_Z, 0, 1);

   /* here, we take the branch */
   x86_patch(test2,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGTZL (Branch On Greater Than Zero Likely) */
DECLARE_INSN(BGTZL)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1,*test2,*test3;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* 
    * test the hi word of gpr[rs]
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_S, 0, 1);
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* test the lo word of gpr[rs] (here hi word = 0) */
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs),4);
   x86_test_reg_reg(b->jit_ptr,X86_EBX,X86_EBX);
   test3 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_Z, 0, 1);

   /* here, we take the branch */
   x86_patch(test2,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);
   return(0);
}

/* BLEZ (Branch On Less or Equal Than Zero) */
DECLARE_INSN(BLEZ)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1,*test2,*test3;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* 
    * test the hi word of gpr[rs]
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_S, 0, 1);
   test2 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* test the lo word of gpr[rs] (here hi word = 0) */
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs),4);
   x86_test_reg_reg(b->jit_ptr,X86_EBX,X86_EBX);
   test3 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* here, we take the branch */
   x86_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test2,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BLEZL (Branch On Less or Equal Than Zero Likely) */
DECLARE_INSN(BLEZL)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1,*test2,*test3;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* 
    * test the hi word of gpr[rs]
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_S, 0, 1);
   test2 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* test the lo word of gpr[rs] (here hi word = 0) */
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs),4);
   x86_test_reg_reg(b->jit_ptr,X86_EBX,X86_EBX);
   test3 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* here, we take the branch */
   x86_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test2,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);
   return(0);
}

/* BLTZ (Branch On Less Than Zero) */
DECLARE_INSN(BLTZ)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* 
    * test the sign bit of gpr[rs], if set, take the branch.
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BLTZAL (Branch On Less Than Zero And Link) */
DECLARE_INSN(BLTZAL)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* set the return address (instruction after the delay slot) */
   mips64_set_ra(b,b->start_pc + ((b->mips_trans_pos + 1) << 2));

   /* 
    * test the sign bit of gpr[rs], if set, take the branch.
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BLTZALL (Branch On Less Than Zero And Link Likely) */
DECLARE_INSN(BLTZALL)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* set the return address (instruction after the delay slot) */
   mips64_set_ra(b,b->start_pc + ((b->mips_trans_pos + 1) << 2));

   /* 
    * test the sign bit of gpr[rs], if set, take the branch.
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   return(0);
}

/* BLTZL (Branch On Less Than Zero Likely) */
DECLARE_INSN(BLTZL)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* 
    * test the sign bit of gpr[rs], if set, take the branch.
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   return(0);
}

/* BNE (Branch On Not Equal) */
DECLARE_INSN(BNE)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int offset = bits(insn,0,15);
   u_char *test1,*test2;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* 
    * compare gpr[rs] and gpr[rt]. 
    * compare the low 32 bits first (higher probability).
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_EAX,X86_EDI,REG_OFFSET(rt));
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_EBX,X86_EDI,REG_OFFSET(rt)+4);
   test2 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_E, 0, 1);

   x86_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test2,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BNEL (Branch On Not Equal Likely) */
DECLARE_INSN(BNEL)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int offset = bits(insn,0,15);
   u_char *test1,*test2;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* 
    * compare gpr[rs] and gpr[rt]. 
    * compare the low 32 bits first (higher probability).
    */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_EAX,X86_EDI,REG_OFFSET(rt));
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_EBX,X86_EDI,REG_OFFSET(rt)+4);
   test2 = b->jit_ptr;
   x86_branch32(b->jit_ptr, X86_CC_E, 0, 1);

   x86_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test2,b->jit_ptr);
   return(0);
}

/* BREAK */
DECLARE_INSN(BREAK)
{	
   u_int code = bits(insn,6,25);

   x86_alu_reg_imm(b->jit_ptr,X86_SUB,X86_ESP,12);
   x86_mov_reg_imm(b->jit_ptr,X86_EDX,code);
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_exec_break);
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);

   mips64_jit_tcb_push_epilog(b);
   return(0);
}

/* CACHE */
DECLARE_INSN(CACHE)
{        
   int base   = bits(insn,21,25);
   int op     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_CACHE,base,offset,op,FALSE);
   return(0);
}

/* CFC0 */
DECLARE_INSN(CFC0)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_cp0_exec_cfc0);
   return(0);
}

/* CTC0 */
DECLARE_INSN(CTC0)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_cp0_exec_ctc0);
   return(0);
}

/* DADDIU */
DECLARE_INSN(DADDIU)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);
   
   mips64_load_imm(b,X86_EBX,X86_EAX,val);

   x86_alu_reg_membase(b->jit_ptr,X86_ADD,X86_EAX,X86_EDI,REG_OFFSET(rs));
   x86_alu_reg_membase(b->jit_ptr,X86_ADC,X86_EBX,X86_EDI,REG_OFFSET(rs)+4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt)+4,X86_EBX,4);

   return(0);
}

/* DADDU: rd = rs + rt */
DECLARE_INSN(DADDU)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_alu_reg_membase(b->jit_ptr,X86_ADD,X86_EAX,X86_EDI,REG_OFFSET(rt));
   x86_alu_reg_membase(b->jit_ptr,X86_ADC,X86_EBX,X86_EDI,REG_OFFSET(rt)+4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* DIV */
DECLARE_INSN(DIV)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   /* eax = gpr[rs] */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_cdq(b->jit_ptr);
   /* ebx = gpr[rt] */
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rt),4);

   /* eax = quotient (LO), edx = remainder (HI) */
   x86_div_reg(b->jit_ptr,X86_EBX,1);

   /* store LO */
   x86_mov_reg_reg(b->jit_ptr,X86_ECX,X86_EDX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo)+4,X86_EDX,4);

   /* store HI */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_ECX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi)+4,X86_EDX,4);
   return(0);
}

/* DIVU */
DECLARE_INSN(DIVU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   /* eax = gpr[rs] */
   x86_clear_reg(b->jit_ptr,X86_EDX);
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   /* ebx = gpr[rt] */
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rt),4);

   /* eax = quotient (LO), edx = remainder (HI) */
   x86_div_reg(b->jit_ptr,X86_EBX,0);

   /* store LO */
   x86_mov_reg_reg(b->jit_ptr,X86_ECX,X86_EDX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo)+4,X86_EDX,4);

   /* store HI */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_ECX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi)+4,X86_EDX,4);
   return(0);
}

/* DMFC0 */
DECLARE_INSN(DMFC0)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_cp0_exec_dmfc0);
   return(0);
}

/* DMFC1 */
DECLARE_INSN(DMFC1)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_exec_dmfc1);
   return(0);
}

/* DMTC0 */
DECLARE_INSN(DMTC0)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_cp0_exec_dmtc0);
   return(0);
}

/* DMTC1 */
DECLARE_INSN(DMTC1)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_exec_dmtc1);
   return(0);
}

/* DSLL */
DECLARE_INSN(DSLL)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);
   
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rt)+4,4);

   x86_shld_reg_imm(b->jit_ptr,X86_EBX,X86_EAX,sa);
   x86_shift_reg_imm(b->jit_ptr,X86_SHL,X86_EAX,sa);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* DSLL32 */
DECLARE_INSN(DSLL32)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);
   
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_shift_reg_imm(b->jit_ptr,X86_SHL,X86_EAX,sa);
   x86_clear_reg(b->jit_ptr,X86_EDX);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EDX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EAX,4);
   return(0);
}

/* DSLLV */
DECLARE_INSN(DSLLV)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rt)+4,4);

   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_ECX,0x3f);

   x86_shld_reg(b->jit_ptr,X86_EBX,X86_EAX);
   x86_shift_reg(b->jit_ptr,X86_SHL,X86_EAX);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* DSRA */
DECLARE_INSN(DSRA)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);
   
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rt)+4,4);

   x86_shrd_reg_imm(b->jit_ptr,X86_EAX,X86_EBX,sa);
   x86_shift_reg_imm(b->jit_ptr,X86_SAR,X86_EBX,sa);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* DSRA32 */
DECLARE_INSN(DSRA32)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);
   
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt)+4,4);
   x86_shift_reg_imm(b->jit_ptr,X86_SAR,X86_EAX,sa);
   x86_cdq(b->jit_ptr);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* DSRAV */
DECLARE_INSN(DSRAV)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rt)+4,4);

   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_ECX,0x3f);

   x86_shrd_reg(b->jit_ptr,X86_EAX,X86_EBX);
   x86_shift_reg(b->jit_ptr,X86_SAR,X86_EBX);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* DSRL */
DECLARE_INSN(DSRL)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);
   
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rt)+4,4);

   x86_shrd_reg_imm(b->jit_ptr,X86_EAX,X86_EBX,sa);
   x86_shift_reg_imm(b->jit_ptr,X86_SHR,X86_EBX,sa);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* DSRL32 */
DECLARE_INSN(DSRL32)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);
   
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt)+4,4);
   x86_shift_reg_imm(b->jit_ptr,X86_SHR,X86_EAX,sa);
   x86_clear_reg(b->jit_ptr,X86_EDX);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* DSRLV */
DECLARE_INSN(DSRLV)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rt)+4,4);

   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_ECX,0x3f);

   x86_shrd_reg(b->jit_ptr,X86_EAX,X86_EBX);
   x86_shift_reg(b->jit_ptr,X86_SHR,X86_EBX);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* DSUBU: rd = rs - rt */
DECLARE_INSN(DSUBU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_alu_reg_membase(b->jit_ptr,X86_SUB,X86_EAX,X86_EDI,REG_OFFSET(rt));
   x86_alu_reg_membase(b->jit_ptr,X86_SBB,X86_EBX,X86_EDI,REG_OFFSET(rt)+4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* ERET */
DECLARE_INSN(ERET)
{
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_exec_eret);
   mips64_jit_tcb_push_epilog(b);
   return(0);
}

/* J (Jump) */
DECLARE_INSN(J)
{
   u_int instr_index = bits(insn,0,25);
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc &= ~((1 << 28) - 1);
   new_pc |= instr_index << 2;

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);
   return(0);
}

/* JAL (Jump And Link) */
DECLARE_INSN(JAL)
{
   u_int instr_index = bits(insn,0,25);
   m_uint64_t new_pc,ret_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc &= ~((1 << 28) - 1);
   new_pc |= instr_index << 2;

   /* set the return address (instruction after the delay slot) */
   ret_pc = b->start_pc + ((b->mips_trans_pos + 1) << 2);
   mips64_set_ra(b,ret_pc);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,0);
   return(0);
}

/* JALR (Jump and Link Register) */
DECLARE_INSN(JALR)
{
   int rs = bits(insn,21,25);
   int rd = bits(insn,11,15);
   m_uint64_t ret_pc;

   /* set the return pc (instruction after the delay slot) in GPR[rd] */
   ret_pc = b->start_pc + ((b->mips_trans_pos + 1) << 2);
   mips64_load_imm(b,X86_EBX,X86_EAX,ret_pc);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   /* get the new pc */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,ret_pc),X86_ECX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,ret_pc)+4,
                       X86_EDX,4);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,
                       X86_EDI,OFFSET(cpu_mips_t,ret_pc),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,
                       X86_EDI,OFFSET(cpu_mips_t,ret_pc)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc),X86_ECX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc)+4,X86_EDX,4);

   /* returns to the caller which will determine the next path */
   mips64_jit_tcb_push_epilog(b);
   return(0);
}

/* JR (Jump Register) */
DECLARE_INSN(JR)
{	
   int rs = bits(insn,21,25);

   /* get the new pc */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,ret_pc),X86_ECX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,ret_pc)+4,
                       X86_EDX,4);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,
                       X86_EDI,OFFSET(cpu_mips_t,ret_pc),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,
                       X86_EDI,OFFSET(cpu_mips_t,ret_pc)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc),X86_ECX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc)+4,X86_EDX,4);

   /* returns to the caller which will determine the next path */
   mips64_jit_tcb_push_epilog(b);
   return(0);
}

/* LB (Load Byte) */
DECLARE_INSN(LB)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LB,base,offset,rt,TRUE);
   return(0);
}

/* LBU (Load Byte Unsigned) */
DECLARE_INSN(LBU)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LBU,base,offset,rt,TRUE);
   return(0);
}

/* LD (Load Double-Word) */
DECLARE_INSN(LD)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LD,base,offset,rt,TRUE);
   return(0);
}

/* LDC1 (Load Double-Word to Coprocessor 1) */
DECLARE_INSN(LDC1)
{
   int base   = bits(insn,21,25);
   int ft     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LDC1,base,offset,ft,TRUE);
   return(0);
}

/* LDL (Load Double-Word Left) */
DECLARE_INSN(LDL)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LDL,base,offset,rt,TRUE);
   return(0);
}

/* LDR (Load Double-Word Right) */
DECLARE_INSN(LDR)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LDR,base,offset,rt,TRUE);
   return(0);
}

/* LH (Load Half-Word) */
DECLARE_INSN(LH)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LH,base,offset,rt,TRUE);
   return(0);
}

/* LHU (Load Half-Word Unsigned) */
DECLARE_INSN(LHU)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LHU,base,offset,rt,TRUE);
   return(0);
}

/* LI (virtual) */
DECLARE_INSN(LI)
{
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);

   x86_mov_membase_imm(b->jit_ptr,X86_EDI,REG_OFFSET(rt),val & 0xffffffff,4);
   x86_mov_membase_imm(b->jit_ptr,X86_EDI,REG_OFFSET(rt)+4,val >> 32,4);
   return(0);
}

/* LL (Load Linked) */
DECLARE_INSN(LL)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LL,base,offset,rt,TRUE);
   return(0);
}

/* LUI */
DECLARE_INSN(LUI)
{
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16) << 16;

   mips64_load_imm(b,X86_EBX,X86_EAX,val);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt)+4,X86_EBX,4);

   return(0);
}

/* LW (Load Word) */
DECLARE_INSN(LW)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,0,MIPS_MEMOP_LW,base,offset,rt,TRUE,
                             mips64_memop_fast_lw);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_LW,base,offset,rt,TRUE);
   }
   return(0);
}

/* LWL (Load Word Left) */
DECLARE_INSN(LWL)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LWL,base,offset,rt,TRUE);
   return(0);
}

/* LWR (Load Word Right) */
DECLARE_INSN(LWR)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LWR,base,offset,rt,TRUE);
   return(0);
}

/* LWU (Load Word Unsigned) */
DECLARE_INSN(LWU)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LWU,base,offset,rt,TRUE);
   return(0);
}

/* MFC0 */
DECLARE_INSN(MFC0)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_cp0_exec_mfc0);
   return(0);
}

/* MFC1 */
DECLARE_INSN(MFC1)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_exec_mfc1);
   return(0);
}

/* MFHI */
DECLARE_INSN(MFHI)
{
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,OFFSET(cpu_mips_t,hi),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,OFFSET(cpu_mips_t,hi)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* MFLO */
DECLARE_INSN(MFLO)
{
   int rd = bits(insn,11,15);

   if (!rd) return(0);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,OFFSET(cpu_mips_t,lo),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,OFFSET(cpu_mips_t,lo)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* MOVE (virtual instruction, real: ADDU) */
DECLARE_INSN(MOVE)
{	
   int rs = bits(insn,21,25);
   int rd = bits(insn,11,15);
   
   if (rs != 0) {
      x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);   
      x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
      x86_cdq(b->jit_ptr);
      x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   } else {
      x86_alu_reg_reg(b->jit_ptr,X86_XOR,X86_EBX,X86_EBX);
      x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EBX,4);
      x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);
   }

   return(0);
}

/* MTC0 */
DECLARE_INSN(MTC0)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_cp0_exec_mtc0);
   return(0);
}

/* MTC1 */
DECLARE_INSN(MTC1)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_exec_mtc1);
   return(0);
}

/* MTHI */
DECLARE_INSN(MTHI)
{
   int rs = bits(insn,21,25);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi)+4,X86_EBX,4);

   return(0);
}

/* MTLO */
DECLARE_INSN(MTLO)
{
   int rs = bits(insn,21,25);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo)+4,X86_EBX,4);

   return(0);
}

/* MUL */
DECLARE_INSN(MUL)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rt),4);

   x86_mul_reg(b->jit_ptr,X86_EBX,1);

   /* store result in gpr[rd] */
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* MULT */
DECLARE_INSN(MULT)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rt),4);

   x86_mul_reg(b->jit_ptr,X86_EBX,1);

   /* store LO */
   x86_mov_reg_reg(b->jit_ptr,X86_ECX,X86_EDX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo)+4,X86_EDX,4);

   /* store HI */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_ECX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi)+4,X86_EDX,4);
   return(0);
}

/* MULTU */
DECLARE_INSN(MULTU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rt),4);

   x86_mul_reg(b->jit_ptr,X86_EBX,0);

   /* store LO */
   x86_mov_reg_reg(b->jit_ptr,X86_ECX,X86_EDX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo)+4,X86_EDX,4);

   /* store HI */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_ECX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi)+4,X86_EDX,4);
   return(0);
}

/* NOP */
DECLARE_INSN(NOP)
{
   //x86_nop(b->jit_ptr);
   return(0);
}

/* NOR */
DECLARE_INSN(NOR)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_alu_reg_membase(b->jit_ptr,X86_OR,X86_EAX,X86_EDI,REG_OFFSET(rt));
   x86_alu_reg_membase(b->jit_ptr,X86_OR,X86_EBX,X86_EDI,REG_OFFSET(rt)+4);

   x86_not_reg(b->jit_ptr,X86_EAX);
   x86_not_reg(b->jit_ptr,X86_EBX);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* OR */
DECLARE_INSN(OR)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_alu_reg_membase(b->jit_ptr,X86_OR,X86_EAX,X86_EDI,REG_OFFSET(rt));
   x86_alu_reg_membase(b->jit_ptr,X86_OR,X86_EBX,X86_EDI,REG_OFFSET(rt)+4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* ORI */
DECLARE_INSN(ORI)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = imm;

   x86_mov_reg_imm(b->jit_ptr,X86_EAX,val & 0xffff);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_alu_reg_membase(b->jit_ptr,X86_OR,X86_EAX,X86_EDI,REG_OFFSET(rs));

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt)+4,X86_EBX,4);

   return(0);
}

/* PREF */
DECLARE_INSN(PREF)
{
   x86_nop(b->jit_ptr);
   return(0);
}

/* PREFI */
DECLARE_INSN(PREFI)
{
   x86_nop(b->jit_ptr);
   return(0);
}

/* SB (Store Byte) */
DECLARE_INSN(SB)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SB,base,offset,rt,FALSE);
   return(0);
}

/* SC (Store Conditional) */
DECLARE_INSN(SC)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SC,base,offset,rt,TRUE);
   return(0);
}

/* SD (Store Double-Word) */
DECLARE_INSN(SD)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SD,base,offset,rt,FALSE);
   return(0);
}

/* SDL (Store Double-Word Left) */
DECLARE_INSN(SDL)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SDL,base,offset,rt,FALSE);
   return(0);
}

/* SDR (Store Double-Word Right) */
DECLARE_INSN(SDR)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SDR,base,offset,rt,FALSE);
   return(0);
}

/* SDC1 (Store Double-Word from Coprocessor 1) */
DECLARE_INSN(SDC1)
{
   int base   = bits(insn,21,25);
   int ft     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SDC1,base,offset,ft,FALSE);
   return(0);
}

/* SH (Store Half-Word) */
DECLARE_INSN(SH)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SH,base,offset,rt,FALSE);
   return(0);
}

/* SLL */
DECLARE_INSN(SLL)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_shift_reg_imm(b->jit_ptr,X86_SHL,X86_EAX,sa);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* SLLV */
DECLARE_INSN(SLLV)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_ECX,0x1f);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_shift_reg(b->jit_ptr,X86_SHL,X86_EAX);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* SLT */
DECLARE_INSN(SLT)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   u_char *test1,*test2,*test3;

   /* edx:eax = gpr[rt] */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,X86_EDI,REG_OFFSET(rt)+4,4);

   /* ebx:ecx = gpr[rs] */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   /* we set rd to 1 when gpr[rs] < gpr[rt] */
   x86_clear_reg(b->jit_ptr,X86_ESI);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_ESI,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_ESI,4);

   /* rs(high) > rt(high) => end */
   x86_alu_reg_reg(b->jit_ptr,X86_CMP,X86_EBX,X86_EDX);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_GT, 0, 1);
   
   /* rs(high) < rt(high) => set rd to 1 */
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_LT, 0, 1);
   
   /* rs(high) == rt(high), rs(low) >= rt(low) => end */
   x86_alu_reg_reg(b->jit_ptr,X86_CMP,X86_ECX,X86_EAX);
   test3 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_AE, 0, 1);
   
   /* set rd to 1 */
   x86_patch(test2,b->jit_ptr);
   x86_inc_membase(b->jit_ptr,X86_EDI,REG_OFFSET(rd));

   /* end */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);
   return(0);
}

/* SLTI */
DECLARE_INSN(SLTI)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);
   u_char *test1,*test2,*test3;

   /* we set rt to 1 when gpr[rs] < val, rt to 0 when gpr[rs] >= val */

   /* edx:eax = val */
   mips64_load_imm(b,X86_EDX,X86_EAX,val);

   /* ebx:ecx = gpr[rs] */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   /* we set rt to 1 when gpr[rs] < val */
   x86_clear_reg(b->jit_ptr,X86_ESI);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt),X86_ESI,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt)+4,X86_ESI,4);

   /* rs(high) > val(high) => end */
   x86_alu_reg_reg(b->jit_ptr,X86_CMP,X86_EBX,X86_EDX);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_GT, 0, 1);
   
   /* rs(high) < val(high) => set rt to 1 */
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_LT, 0, 1);
   
   /* rs(high) == val(high), rs(low) >= val(low) => set rt to 0 */
   x86_alu_reg_reg(b->jit_ptr,X86_CMP,X86_ECX,X86_EAX);
   test3 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_AE, 0, 1);
   
   /* set rt to 1 */
   x86_patch(test2,b->jit_ptr);
   x86_inc_membase(b->jit_ptr,X86_EDI,REG_OFFSET(rt));

   /* end */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);   

   return(0);
}

/* SLTIU */
DECLARE_INSN(SLTIU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);
   u_char *test1,*test2,*test3;

   /* edx:eax = val */
   mips64_load_imm(b,X86_EDX,X86_EAX,val);

   /* ebx:ecx = gpr[rs] */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   /* we set rt to 1 when gpr[rs] < val */
   x86_clear_reg(b->jit_ptr,X86_ESI);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt),X86_ESI,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt)+4,X86_ESI,4);

   /* rs(high) > val(high) => end */
   x86_alu_reg_reg(b->jit_ptr,X86_CMP,X86_EBX,X86_EDX);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_A, 0, 0);
   
   /* rs(high) < val(high) => set rt to 1 */
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_B, 0, 0);
   
   /* rs(high) == val(high), rs(low) >= val(low) => end */
   x86_alu_reg_reg(b->jit_ptr,X86_CMP,X86_ECX,X86_EAX);
   test3 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_AE, 0, 1);
   
   /* set rt to 1 */
   x86_patch(test2,b->jit_ptr);
   x86_inc_membase(b->jit_ptr,X86_EDI,REG_OFFSET(rt));

   /* end */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);
   return(0);
}

/* SLTU */
DECLARE_INSN(SLTU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   u_char *test1,*test2,*test3;

   /* edx:eax = gpr[rt] */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,X86_EDI,REG_OFFSET(rt)+4,4);

   /* ebx:ecx = gpr[rs] */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   /* we set rd to 1 when gpr[rs] < gpr[rt] */
   x86_clear_reg(b->jit_ptr,X86_ESI);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_ESI,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_ESI,4);

   /* rs(high) > rt(high) => end */
   x86_alu_reg_reg(b->jit_ptr,X86_CMP,X86_EBX,X86_EDX);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_A, 0, 0);
   
   /* rs(high) < rt(high) => set rd to 1 */
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_B, 0, 0);
   
   /* rs(high) == rt(high), rs(low) >= rt(low) => end */
   x86_alu_reg_reg(b->jit_ptr,X86_CMP,X86_ECX,X86_EAX);
   test3 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_AE, 0, 1);
   
   /* set rd to 1 */
   x86_patch(test2,b->jit_ptr);
   x86_inc_membase(b->jit_ptr,X86_EDI,REG_OFFSET(rd));

   /* end */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);
   return(0);
}

/* SRA */
DECLARE_INSN(SRA)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_shift_reg_imm(b->jit_ptr,X86_SAR,X86_EAX,sa);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* SRAV */
DECLARE_INSN(SRAV)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_ECX,0x1f);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_shift_reg(b->jit_ptr,X86_SAR,X86_EAX);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* SRL */
DECLARE_INSN(SRL)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_shift_reg_imm(b->jit_ptr,X86_SHR,X86_EAX,sa);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_clear_reg(b->jit_ptr,X86_EDX);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* SRLV */
DECLARE_INSN(SRLV)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_ECX,0x1f);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rt),4);
   x86_shift_reg(b->jit_ptr,X86_SHR,X86_EAX);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_clear_reg(b->jit_ptr,X86_EDX);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* SUB */
DECLARE_INSN(SUB)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   
   /* TODO: Exception handling */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_membase(b->jit_ptr,X86_SUB,X86_EAX,X86_EDI,REG_OFFSET(rt));
   
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* SUBU */
DECLARE_INSN(SUBU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_membase(b->jit_ptr,X86_SUB,X86_EAX,X86_EDI,REG_OFFSET(rt));
   
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EDX,4);
   return(0);
}

/* SW (Store Word) */
DECLARE_INSN(SW)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,1,MIPS_MEMOP_SW,base,offset,rt,FALSE,
                             mips64_memop_fast_sw);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_SW,base,offset,rt,FALSE);
   }
   return(0);
}

/* SWL (Store Word Left) */
DECLARE_INSN(SWL)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SWL,base,offset,rt,FALSE);
   return(0);
}

/* SWR (Store Word Right) */
DECLARE_INSN(SWR)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SWR,base,offset,rt,FALSE);
   return(0);
}

/* SYNC */
DECLARE_INSN(SYNC)
{
   return(0);
}

/* SYSCALL */
DECLARE_INSN(SYSCALL)
{
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_exec_syscall);

   mips64_jit_tcb_push_epilog(b);
   return(0);
}

/* TEQ (Trap If Equal) */
DECLARE_INSN(TEQ)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   u_char *test1,*test2;

   /* Compare low part */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_ECX,X86_EDI,REG_OFFSET(rt));
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* Compare high part */
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_EBX,X86_EDI,REG_OFFSET(rt)+4);
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* Generate trap exception */
   x86_alu_reg_imm(b->jit_ptr,X86_SUB,X86_ESP,12);
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_c_call(b,mips64_trigger_trap_exception);
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);
   
   mips64_jit_tcb_push_epilog(b);

   /* end */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);
   return(0);
}

/* TEQI (Trap If Equal Immediate) */
DECLARE_INSN(TEQI)
{
   int rs  = bits(insn,21,25);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);
   u_char *test1,*test2;

   /* edx:eax = val */
   mips64_load_imm(b,X86_EDX,X86_EAX,val);

   /* Compare low part */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_alu_reg_reg(b->jit_ptr,X86_CMP,X86_ECX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* Compare high part */
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);
   x86_alu_reg_reg(b->jit_ptr,X86_CMP,X86_EBX,X86_EDX);
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* Generate trap exception */
   x86_alu_reg_imm(b->jit_ptr,X86_SUB,X86_ESP,12);
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_c_call(b,mips64_trigger_trap_exception);
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);

   mips64_jit_tcb_push_epilog(b);

   /* end */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);
   return(0);
}

/* TLBP */
DECLARE_INSN(TLBP)
{
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbp);
   return(0);
}

/* TLBR */
DECLARE_INSN(TLBR)
{  
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbr);
   return(0);
}

/* TLBWI */
DECLARE_INSN(TLBWI)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbwi);
   return(0);
}

/* TLBWR */
DECLARE_INSN(TLBWR)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbwr);
   return(0);
}

/* XOR */
DECLARE_INSN(XOR)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_alu_reg_membase(b->jit_ptr,X86_XOR,X86_EAX,X86_EDI,REG_OFFSET(rt));
   x86_alu_reg_membase(b->jit_ptr,X86_XOR,X86_EBX,X86_EDI,REG_OFFSET(rt)+4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* XORI */
DECLARE_INSN(XORI)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = imm;

   mips64_load_imm(b,X86_EBX,X86_EAX,val);

   x86_alu_reg_membase(b->jit_ptr,X86_XOR,X86_EAX,X86_EDI,REG_OFFSET(rs));
   x86_alu_reg_membase(b->jit_ptr,X86_XOR,X86_EBX,X86_EDI,REG_OFFSET(rs)+4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rt)+4,X86_EBX,4);

   return(0);
}

/* MIPS instruction array */
struct mips64_insn_tag mips64_insn_tags[] = {
   { mips64_emit_LI      , 0xffe00000 , 0x24000000, 1 },   /* virtual */
   { mips64_emit_MOVE    , 0xfc1f07ff , 0x00000021, 1 },   /* virtual */
   { mips64_emit_B       , 0xffff0000 , 0x10000000, 0 },   /* virtual */
   { mips64_emit_BAL     , 0xffff0000 , 0x04110000, 0 },   /* virtual */
   { mips64_emit_BEQZ    , 0xfc1f0000 , 0x10000000, 0 },   /* virtual */
   { mips64_emit_BNEZ    , 0xfc1f0000 , 0x14000000, 0 },   /* virtual */
   { mips64_emit_ADD     , 0xfc0007ff , 0x00000020, 1 },
   { mips64_emit_ADDI    , 0xfc000000 , 0x20000000, 1 },
   { mips64_emit_ADDIU   , 0xfc000000 , 0x24000000, 1 },
   { mips64_emit_ADDU    , 0xfc0007ff , 0x00000021, 1 },
   { mips64_emit_AND     , 0xfc0007ff , 0x00000024, 1 },
   { mips64_emit_ANDI    , 0xfc000000 , 0x30000000, 1 },
   { mips64_emit_BEQ     , 0xfc000000 , 0x10000000, 0 },
   { mips64_emit_BEQL    , 0xfc000000 , 0x50000000, 0 },
   { mips64_emit_BGEZ    , 0xfc1f0000 , 0x04010000, 0 },
   { mips64_emit_BGEZAL  , 0xfc1f0000 , 0x04110000, 0 },
   { mips64_emit_BGEZALL , 0xfc1f0000 , 0x04130000, 0 },
   { mips64_emit_BGEZL   , 0xfc1f0000 , 0x04030000, 0 },
   { mips64_emit_BGTZ    , 0xfc1f0000 , 0x1c000000, 0 },
   { mips64_emit_BGTZL   , 0xfc1f0000 , 0x5c000000, 0 },
   { mips64_emit_BLEZ    , 0xfc1f0000 , 0x18000000, 0 },
   { mips64_emit_BLEZL   , 0xfc1f0000 , 0x58000000, 0 },
   { mips64_emit_BLTZ    , 0xfc1f0000 , 0x04000000, 0 },
   { mips64_emit_BLTZAL  , 0xfc1f0000 , 0x04100000, 0 },
   { mips64_emit_BLTZALL , 0xfc1f0000 , 0x04120000, 0 },
   { mips64_emit_BLTZL   , 0xfc1f0000 , 0x04020000, 0 },
   { mips64_emit_BNE     , 0xfc000000 , 0x14000000, 0 },
   { mips64_emit_BNEL    , 0xfc000000 , 0x54000000, 0 },
   { mips64_emit_BREAK   , 0xfc00003f , 0x0000000d, 1 },
   { mips64_emit_CACHE   , 0xfc000000 , 0xbc000000, 1 },
   { mips64_emit_CFC0    , 0xffe007ff , 0x40400000, 1 },
   { mips64_emit_CTC0    , 0xffe007ff , 0x40600000, 1 },
   { mips64_emit_DADDIU  , 0xfc000000 , 0x64000000, 1 },
   { mips64_emit_DADDU   , 0xfc0007ff , 0x0000002d, 1 },
   { mips64_emit_DIV     , 0xfc00ffff , 0x0000001a, 1 },
   { mips64_emit_DIVU    , 0xfc00ffff , 0x0000001b, 1 },
   { mips64_emit_DMFC0   , 0xffe007f8 , 0x40200000, 1 },
   { mips64_emit_DMFC1   , 0xffe007ff , 0x44200000, 1 },
   { mips64_emit_DMTC0   , 0xffe007f8 , 0x40a00000, 1 },
   { mips64_emit_DMTC1   , 0xffe007ff , 0x44a00000, 1 },
   { mips64_emit_DSLL    , 0xffe0003f , 0x00000038, 1 },
   { mips64_emit_DSLL32  , 0xffe0003f , 0x0000003c, 1 },
   { mips64_emit_DSLLV   , 0xfc0007ff , 0x00000014, 1 },
   { mips64_emit_DSRA    , 0xffe0003f , 0x0000003b, 1 },
   { mips64_emit_DSRA32  , 0xffe0003f , 0x0000003f, 1 },
   { mips64_emit_DSRAV   , 0xfc0007ff , 0x00000017, 1 },
   { mips64_emit_DSRL    , 0xffe0003f , 0x0000003a, 1 },
   { mips64_emit_DSRL32  , 0xffe0003f , 0x0000003e, 1 },
   { mips64_emit_DSRLV   , 0xfc0007ff , 0x00000016, 1 },
   { mips64_emit_DSUBU   , 0xfc0007ff , 0x0000002f, 1 },
   { mips64_emit_ERET    , 0xffffffff , 0x42000018, 0 },
   { mips64_emit_J       , 0xfc000000 , 0x08000000, 0 },
   { mips64_emit_JAL     , 0xfc000000 , 0x0c000000, 0 },
   { mips64_emit_JALR    , 0xfc1f003f , 0x00000009, 0 },
   { mips64_emit_JR      , 0xfc1ff83f , 0x00000008, 0 },
   { mips64_emit_LB      , 0xfc000000 , 0x80000000, 1 },
   { mips64_emit_LBU     , 0xfc000000 , 0x90000000, 1 },
   { mips64_emit_LD      , 0xfc000000 , 0xdc000000, 1 },
   { mips64_emit_LDC1    , 0xfc000000 , 0xd4000000, 1 },
   { mips64_emit_LDL     , 0xfc000000 , 0x68000000, 1 },
   { mips64_emit_LDR     , 0xfc000000 , 0x6c000000, 1 },
   { mips64_emit_LH      , 0xfc000000 , 0x84000000, 1 },
   { mips64_emit_LHU     , 0xfc000000 , 0x94000000, 1 },
   { mips64_emit_LL      , 0xfc000000 , 0xc0000000, 1 },
   { mips64_emit_LUI     , 0xffe00000 , 0x3c000000, 1 },
   { mips64_emit_LW      , 0xfc000000 , 0x8c000000, 1 },
   { mips64_emit_LWL     , 0xfc000000 , 0x88000000, 1 },
   { mips64_emit_LWR     , 0xfc000000 , 0x98000000, 1 },
   { mips64_emit_LWU     , 0xfc000000 , 0x9c000000, 1 },
   { mips64_emit_MFC0    , 0xffe007ff , 0x40000000, 1 },
   { mips64_emit_CFC0    , 0xffe007ff , 0x40000001, 1 },  /* MFC0 / Set 1 */
   { mips64_emit_MFC1    , 0xffe007ff , 0x44000000, 1 },
   { mips64_emit_MFHI    , 0xffff07ff , 0x00000010, 1 },
   { mips64_emit_MFLO    , 0xffff07ff , 0x00000012, 1 },
   { mips64_emit_MTC0    , 0xffe007ff , 0x40800000, 1 },
   { mips64_emit_MTC1    , 0xffe007ff , 0x44800000, 1 },
   { mips64_emit_MTHI    , 0xfc1fffff , 0x00000011, 1 },
   { mips64_emit_MTLO    , 0xfc1fffff , 0x00000013, 1 },
   { mips64_emit_MUL     , 0xfc0007ff , 0x70000002, 1 },
   { mips64_emit_MULT    , 0xfc00ffff , 0x00000018, 1 },
   { mips64_emit_MULTU   , 0xfc00ffff , 0x00000019, 1 },
   { mips64_emit_NOP     , 0xffffffff , 0x00000000, 1 },
   { mips64_emit_NOR     , 0xfc0007ff , 0x00000027, 1 },
   { mips64_emit_OR      , 0xfc0007ff , 0x00000025, 1 },
   { mips64_emit_ORI     , 0xfc000000 , 0x34000000, 1 },
   { mips64_emit_PREF    , 0xfc000000 , 0xcc000000, 1 },
   { mips64_emit_PREFI   , 0xfc0007ff , 0x4c00000f, 1 },
   { mips64_emit_SB      , 0xfc000000 , 0xa0000000, 1 },
   { mips64_emit_SC      , 0xfc000000 , 0xe0000000, 1 },
   { mips64_emit_SD      , 0xfc000000 , 0xfc000000, 1 },
   { mips64_emit_SDC1    , 0xfc000000 , 0xf4000000, 1 },
   { mips64_emit_SDL     , 0xfc000000 , 0xb0000000, 1 },
   { mips64_emit_SDR     , 0xfc000000 , 0xb4000000, 1 },
   { mips64_emit_SH      , 0xfc000000 , 0xa4000000, 1 },
   { mips64_emit_SLL     , 0xffe0003f , 0x00000000, 1 },
   { mips64_emit_SLLV    , 0xfc0007ff , 0x00000004, 1 },
   { mips64_emit_SLT     , 0xfc0007ff , 0x0000002a, 1 },
   { mips64_emit_SLTI    , 0xfc000000 , 0x28000000, 1 },
   { mips64_emit_SLTIU   , 0xfc000000 , 0x2c000000, 1 },
   { mips64_emit_SLTU    , 0xfc0007ff , 0x0000002b, 1 },
   { mips64_emit_SRA     , 0xffe0003f , 0x00000003, 1 },
   { mips64_emit_SRAV    , 0xfc0007ff , 0x00000007, 1 },
   { mips64_emit_SRL     , 0xffe0003f , 0x00000002, 1 },
   { mips64_emit_SRLV    , 0xfc0007ff , 0x00000006, 1 },
   { mips64_emit_SUB     , 0xfc0007ff , 0x00000022, 1 },
   { mips64_emit_SUBU    , 0xfc0007ff , 0x00000023, 1 },
   { mips64_emit_SW      , 0xfc000000 , 0xac000000, 1 },
   { mips64_emit_SWL     , 0xfc000000 , 0xa8000000, 1 },
   { mips64_emit_SWR     , 0xfc000000 , 0xb8000000, 1 },
   { mips64_emit_SYNC    , 0xfffff83f , 0x0000000f, 1 },
   { mips64_emit_SYSCALL , 0xfc00003f , 0x0000000c, 1 },
   { mips64_emit_TEQ     , 0xfc00003f , 0x00000034, 1 },
   { mips64_emit_TEQI    , 0xfc1f0000 , 0x040c0000, 1 },
   { mips64_emit_TLBP    , 0xffffffff , 0x42000008, 1 },
   { mips64_emit_TLBR    , 0xffffffff , 0x42000001, 1 },
   { mips64_emit_TLBWI   , 0xffffffff , 0x42000002, 1 },
   { mips64_emit_TLBWR   , 0xffffffff , 0x42000006, 1 },
   { mips64_emit_XOR     , 0xfc0007ff , 0x00000026, 1 },
   { mips64_emit_XORI    , 0xfc000000 , 0x38000000, 1 },
   { mips64_emit_unknown , 0x00000000 , 0x00000000, 1 },
   { NULL                , 0x00000000 , 0x00000000, 0 },
};
