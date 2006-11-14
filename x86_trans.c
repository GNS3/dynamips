/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "x86_trans.h"
#include "cp0.h"
#include "memory.h"

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
static inline void mips64_load_imm(insn_block_t *b,
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
void mips64_set_pc(insn_block_t *b,m_uint64_t new_pc)
{
   x86_mov_membase_imm(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc),
                       new_pc & 0xFFFFFFFF,4);
   x86_mov_membase_imm(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc)+4,
                       new_pc >> 32,4);
}

/* Set the Return Address (RA) register */
void mips64_set_ra(insn_block_t *b,m_uint64_t ret_pc)
{
   mips64_load_imm(b,X86_EDX,X86_EAX,ret_pc);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(MIPS_GPR_RA),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(MIPS_GPR_RA)+4,X86_EDX,4);
}

/* Set Jump */
static void mips64_set_jump(cpu_mips_t *cpu,insn_block_t *b,m_uint64_t new_pc,
                            int local_jump)
{      
   int return_to_caller = FALSE;
   u_char *jump_ptr;

   if (cpu->sym_trace && !local_jump)
      return_to_caller = TRUE;

   if (!return_to_caller && insn_block_local_addr(b,new_pc,&jump_ptr)) {
      if (jump_ptr) {
         x86_jump_code(b->jit_ptr,jump_ptr);
      } else {
         insn_block_record_patch(b,b->jit_ptr,new_pc);
         x86_jump32(b->jit_ptr,0);
      }
   } else {
      /* save PC */
      mips64_set_pc(b,new_pc);

      /* address is in another block, for now, returns to caller */
      insn_block_push_epilog(b);
   }
}

/* Basic C call */
static forced_inline void mips64_emit_basic_c_call(insn_block_t *b,void *f)
{
   x86_mov_reg_imm(b->jit_ptr,X86_EBX,f);
   x86_call_reg(b->jit_ptr,X86_EBX);
}

/* Emit a simple call to a C function without any parameter */
static void mips64_emit_c_call(insn_block_t *b,void *f)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   mips64_emit_basic_c_call(b,f);
}

/* Fast memory operation prototype */
typedef void (*memop_fast_access)(insn_block_t *b,int target);

/* Fast LW */
static void mips64_memop_fast_lw(insn_block_t *b,int target)
{
   x86_mov_reg_memindex(b->jit_ptr,X86_EAX,X86_EAX,0,X86_EBX,0,4);
   x86_bswap(b->jit_ptr,X86_EAX);
   x86_cdq(b->jit_ptr);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(target),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(target)+4,X86_EDX,4);
}

/* Fast SW */
static void mips64_memop_fast_sw(insn_block_t *b,int target)
{
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,X86_EDI,REG_OFFSET(target),4);
   x86_bswap(b->jit_ptr,X86_EDX);
   x86_mov_memindex_reg(b->jit_ptr,X86_EAX,0,X86_EBX,0,X86_EDX,4);
}

/* Fast memory operation (64-bit) */
static void mips64_emit_memop_fast64(insn_block_t *b,int op,
                                     int base,int offset,
                                     int target,int keep_ll_bit,
                                     memop_fast_access op_handler)
{
   m_uint64_t val = sign_extend(offset,16);
   u_char *test1,*test2,*test3,*test4;
   u_char *p_exception,*p_exit;

   /* ECX:EBX = sign-extended offset */
   mips64_load_imm(b,X86_ECX,X86_EBX,val);

   /* ECX:EBX = GPR[base] + sign-extended offset */
   x86_alu_reg_membase(b->jit_ptr,X86_ADD,X86_EBX,X86_EDI,REG_OFFSET(base));
   x86_alu_reg_membase(b->jit_ptr,X86_ADC,X86_ECX,X86_EDI,REG_OFFSET(base)+4);

   /* EAX = mts64_entry index */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EBX,4);
   x86_shift_reg_imm(b->jit_ptr,X86_SHR,X86_EAX,MTS64_HASH_SHIFT);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_EAX,MTS64_HASH_MASK);

   /* EDX = mts_cache */
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,
                       X86_EDI,OFFSET(cpu_mips_t,mts_cache),4);

   /* ESI = mts64_entry */
   x86_mov_reg_memindex(b->jit_ptr,X86_ESI,X86_EDX,0,X86_EAX,2,4);
   x86_test_reg_reg(b->jit_ptr,X86_ESI,X86_ESI);   /* slow lookup */
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_Z, 0, 1);

   /* Compare the high part of the vaddr */
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_ECX,X86_ESI,
                       OFFSET(mts64_entry_t,start)+4);
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* EAX = entry mask, compare low part of the vaddr */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,
                       X86_ESI,OFFSET(mts64_entry_t,mask),4);
   x86_alu_reg_reg(b->jit_ptr,X86_AND,X86_EAX,X86_EBX);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_EAX,X86_ESI,
                       OFFSET(mts64_entry_t,start));
   test3 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* Ok, we have the good entry. Test if this is a device */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,
                       X86_ESI,OFFSET(mts64_entry_t,action),4);
   x86_mov_reg_reg(b->jit_ptr,X86_EDX,X86_EAX,4);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_EDX,MTS_DEV_MASK);
   test4 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* EAX = action */
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_EAX,MTS_ADDR_MASK);

   /* Compute offset */
   x86_alu_reg_membase(b->jit_ptr,X86_SUB,X86_EBX,
                       X86_ESI,OFFSET(mts64_entry_t,start));

   /* Memory access */
   op_handler(b,target);
 
   p_exit = b->jit_ptr;
   x86_jump8(b->jit_ptr,0);

   /* === Slow lookup === */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);
   x86_patch(test4,b->jit_ptr);

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
   x86_call_membase(b->jit_ptr,X86_EDI,MEMOP_OFFSET(op));
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);

   /* Check for exception */
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   p_exception = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_Z, 0, 1);
   insn_block_push_epilog(b);

   x86_patch(p_exit,b->jit_ptr);
   x86_patch(p_exception,b->jit_ptr);
}

/* Fast memory operation (32-bit) */
static void mips64_emit_memop_fast32(insn_block_t *b,int op,
                                     int base,int offset,
                                     int target,int keep_ll_bit,
                                     memop_fast_access op_handler)
{
   m_uint32_t val = sign_extend(offset,16);
   u_char *test1,*test2,*test3;
   u_char *p_exception,*p_exit;

   /* EBX = sign-extended offset */
   x86_mov_reg_imm(b->jit_ptr,X86_EBX,val);

   /* EBX = GPR[base] + sign-extended offset */
   x86_alu_reg_membase(b->jit_ptr,X86_ADD,X86_EBX,X86_EDI,REG_OFFSET(base));

   /* EAX = mts32_entry index */
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EBX,4);
   x86_shift_reg_imm(b->jit_ptr,X86_SHR,X86_EAX,MTS32_HASH_SHIFT);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_EAX,MTS32_HASH_MASK);

   /* EDX = mts_cache */
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,
                       X86_EDI,OFFSET(cpu_mips_t,mts_cache),4);

   /* ESI = mts32_entry */
   x86_mov_reg_memindex(b->jit_ptr,X86_ESI,X86_EDX,0,X86_EAX,2,4);
   x86_test_reg_reg(b->jit_ptr,X86_ESI,X86_ESI);   /* slow lookup */
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_Z, 0, 1);

   /* ECX = entry mask, compare the virtual addresses */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,
                       X86_ESI,OFFSET(mts32_entry_t,mask),4);
   x86_alu_reg_reg(b->jit_ptr,X86_AND,X86_ECX,X86_EBX);
   x86_alu_reg_membase(b->jit_ptr,X86_CMP,X86_ECX,X86_ESI,
                       OFFSET(mts32_entry_t,start));
   test2 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* Ok, we have the good entry. Test if this is a device */
   x86_mov_reg_membase(b->jit_ptr,X86_EAX,
                       X86_ESI,OFFSET(mts32_entry_t,action),4);
   x86_mov_reg_reg(b->jit_ptr,X86_EDX,X86_EAX,4);
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_EDX,MTS_DEV_MASK);
   test3 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* EAX = action */
   x86_alu_reg_imm(b->jit_ptr,X86_AND,X86_EAX,MTS_ADDR_MASK);

   /* Compute offset */
   x86_alu_reg_membase(b->jit_ptr,X86_SUB,X86_EBX,
                       X86_ESI,OFFSET(mts32_entry_t,start));

   /* Memory access */
   op_handler(b,target);
 
   p_exit = b->jit_ptr;
   x86_jump8(b->jit_ptr,0);

   /* === Slow lookup === */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);

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
   x86_call_membase(b->jit_ptr,X86_EDI,MEMOP_OFFSET(op));
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);

   /* Check for exception */
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   p_exception = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_Z, 0, 1);
   insn_block_push_epilog(b);

   x86_patch(p_exit,b->jit_ptr);
   x86_patch(p_exception,b->jit_ptr);
}

/* Fast memory operation */
static void mips64_emit_memop_fast(cpu_mips_t *cpu,insn_block_t *b,int op,
                                   int base,int offset,
                                   int target,int keep_ll_bit,
                                   memop_fast_access op_handler)
{
   switch(cpu->addr_mode) {
      case 32:
         mips64_emit_memop_fast32(b,op,base,offset,target,keep_ll_bit,
                                  op_handler);
         break;
      case 64:
         mips64_emit_memop_fast64(b,op,base,offset,target,keep_ll_bit,
                                  op_handler);
         break;
   }
}

/* Memory operation */
static void mips64_emit_memop(insn_block_t *b,int op,int base,int offset,
                              int target,int keep_ll_bit)
{
   m_uint64_t val = sign_extend(offset,16);
   u_char *test1;

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

   /* Exception ? */
   x86_test_reg_reg(b->jit_ptr,X86_EAX,X86_EAX);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_Z, 0, 1);
   insn_block_push_epilog(b);
   x86_patch(test1,b->jit_ptr);
}

/* Coprocessor Register transfert operation */
static void mips64_emit_cp_xfr_op(insn_block_t *b,int rt,int rd,void *f)
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
void mips64_emit_breakpoint(insn_block_t *b)
{
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_c_call(b,mips64_run_breakpoint);
}

/* Unknown opcode handler */
static asmlinkage void mips64_unknown_opcode(cpu_mips_t *cpu,m_uint32_t opcode)
{
   printf("MIPS64: unhandled opcode 0x%8.8x at 0x%llx (ra=0x%llx)\n",
          opcode,cpu->pc,cpu->gpr[MIPS_GPR_RA]);

   mips64_dump_regs(cpu);
}

/* Emit unhandled instruction code */
static int mips64_emit_unknown(cpu_mips_t *cpu,insn_block_t *b,
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

   mips64_dump_regs(cpu);

   /* Halt the virtual CPU */
   cpu->pc = 0;
}

/* Emit unhandled instruction code */
int mips64_emit_invalid_delay_slot(insn_block_t *b)
{   
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_c_call(b,mips64_invalid_delay_slot);
   insn_block_push_epilog(b);
   return(0);
}

/* Located in external assembly module */
#ifdef FAST_ASM
extern void mips64_inc_cp0_cnt_asm(void);
#endif

/* 
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
void mips64_inc_cp0_count_reg(insn_block_t *b)
{
   x86_inc_membase(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,cp0_virt_cnt_reg));

#if 0 /* TIMER_IRQ */
#ifdef FAST_ASM
   mips64_emit_basic_c_call(b,mips64_inc_cp0_cnt_asm);
#else
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_exec_inc_cp0_cnt);
#endif
#endif
}

/* Check if there are pending IRQ */
void mips64_check_pending_irq(insn_block_t *b)
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
   insn_block_push_epilog(b);

   x86_patch(test1,b->jit_ptr);
}

/* Increment the number of executed instructions (performance debugging) */
void mips64_inc_perf_counter(insn_block_t *b)
{
   x86_alu_membase_imm(b->jit_ptr,X86_ADD,
                       X86_EDI,OFFSET(cpu_mips_t,perf_counter),1);
   x86_alu_membase_imm(b->jit_ptr,X86_ADC,
                       X86_EDI,OFFSET(cpu_mips_t,perf_counter)+4,0);
}

/* ADD */
static int mips64_emit_ADD(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_ADDI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_ADDIU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_ADDU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_AND(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_ANDI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_B(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);
   return(0);
}

/* BAL (Branch and Link, virtual instruction) */
static int mips64_emit_BAL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* set the return address (instruction after the delay slot) */
   mips64_set_ra(b,b->start_pc + ((b->mips_trans_pos + 1) << 2));

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,0);
   return(0);
}

/* BEQ (Branch On Equal) */
static int mips64_emit_BEQ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,0);

   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BEQL (Branch On Equal Likely) */
static int mips64_emit_BEQL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);
   return(0);
}

/* BEQZ (Branch On Equal Zero - optimization) */
static int mips64_emit_BEQZ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BNEZ (Branch On Not Equal Zero - optimization) */
static int mips64_emit_BNEZ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test2,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGEZ (Branch On Greater or Equal Than Zero) */
static int mips64_emit_BGEZ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGEZAL (Branch On Greater or Equal Than Zero And Link) */
static int mips64_emit_BGEZAL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGEZALL (Branch On Greater or Equal Than Zero and Link Likely) */
static int mips64_emit_BGEZALL(cpu_mips_t *cpu,insn_block_t *b,
                               mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   return(0);
}

/* BGEZL (Branch On Greater or Equal Than Zero Likely) */
static int mips64_emit_BGEZL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   return(0);
}

/* BGTZ (Branch On Greater Than Zero) */
static int mips64_emit_BGTZ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGTZL (Branch On Greater Than Zero Likely) */
static int mips64_emit_BGTZL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);
   return(0);
}

/* BLEZ (Branch On Less or Equal Than Zero) */
static int mips64_emit_BLEZ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test2,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BLEZL (Branch On Less or Equal Than Zero Likely) */
static int mips64_emit_BLEZL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test2,b->jit_ptr);
   x86_patch(test3,b->jit_ptr);
   return(0);
}

/* BLTZ (Branch On Less Than Zero) */
static int mips64_emit_BLTZ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BLTZAL (Branch On Less Than Zero And Link) */
static int mips64_emit_BLTZAL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BLTZALL (Branch On Less Than Zero And Link Likely) */
static int mips64_emit_BLTZALL(cpu_mips_t *cpu,insn_block_t *b,
                               mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   return(0);
}

/* BLTZL (Branch On Less Than Zero Likely) */
static int mips64_emit_BLTZL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test1,b->jit_ptr);
   return(0);
}

/* BNE (Branch On Not Equal) */
static int mips64_emit_BNE(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test2,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BNEL (Branch On Not Equal Likely) */
static int mips64_emit_BNEL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   x86_patch(test2,b->jit_ptr);
   return(0);
}

/* BREAK */
static int mips64_emit_BREAK(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   u_int code = bits(insn,6,25);

   x86_mov_reg_imm(b->jit_ptr,X86_EDX,code);
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_exec_break);
   insn_block_push_epilog(b);
   return(0);
}

/* CACHE */
static int mips64_emit_CACHE(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{        
   int base   = bits(insn,21,25);
   int op     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_CACHE,base,offset,op,FALSE);
   return(0);
}

/* CFC0 */
static int mips64_emit_CFC0(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,cp0_exec_cfc0);
   return(0);
}

/* CTC0 */
static int mips64_emit_CTC0(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,cp0_exec_ctc0);
   return(0);
}

/* DADDIU */
static int mips64_emit_DADDIU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DADDU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DIV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DIVU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DMFC0(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,cp0_exec_dmfc0);
   return(0);
}

/* DMFC1 */
static int mips64_emit_DMFC1(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_exec_dmfc1);
   return(0);
}

/* DMTC0 */
static int mips64_emit_DMTC0(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,cp0_exec_dmtc0);
   return(0);
}

/* DMTC1 */
static int mips64_emit_DMTC1(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_exec_dmtc1);
   return(0);
}

/* DSLL */
static int mips64_emit_DSLL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DSLL32(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DSLLV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DSRA(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DSRA32(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DSRAV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DSRL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DSRL32(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DSRLV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_DSUBU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_ERET(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_exec_eret);
   insn_block_push_epilog(b);
   return(0);
}

/* J (Jump) */
static int mips64_emit_J(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   u_int instr_index = bits(insn,0,25);
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc &= ~((1 << 28) - 1);
   new_pc |= instr_index << 2;

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);
   return(0);
}

/* JAL (Jump And Link) */
static int mips64_emit_JAL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,0);
   return(0);
}

/* JALR (Jump and Link Register) */
static int mips64_emit_JALR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,
                       X86_EDI,OFFSET(cpu_mips_t,ret_pc),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,
                       X86_EDI,OFFSET(cpu_mips_t,ret_pc)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc),X86_ECX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc)+4,X86_EDX,4);

   /* returns to the caller which will determine the next path */
   insn_block_push_epilog(b);
   return(0);
}

/* JR (Jump Register) */
static int mips64_emit_JR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rs = bits(insn,21,25);

   /* get the new pc */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,ret_pc),X86_ECX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,ret_pc)+4,
                       X86_EDX,4);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc */
   x86_mov_reg_membase(b->jit_ptr,X86_ECX,
                       X86_EDI,OFFSET(cpu_mips_t,ret_pc),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EDX,
                       X86_EDI,OFFSET(cpu_mips_t,ret_pc)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc),X86_ECX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,pc)+4,X86_EDX,4);

   /* returns to the caller which will determine the next path */
   insn_block_push_epilog(b);
   return(0);
}

/* LB (Load Byte) */
static int mips64_emit_LB(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LB,base,offset,rt,TRUE);
   return(0);
}

/* LBU (Load Byte Unsigned) */
static int mips64_emit_LBU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LBU,base,offset,rt,TRUE);
   return(0);
}

/* LD (Load Double-Word) */
static int mips64_emit_LD(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LD,base,offset,rt,TRUE);
   return(0);
}

/* LDC1 (Load Double-Word to Coprocessor 1) */
static int mips64_emit_LDC1(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int ft     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LDC1,base,offset,ft,TRUE);
   return(0);
}

/* LDL (Load Double-Word Left) */
static int mips64_emit_LDL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LDL,base,offset,rt,TRUE);
   return(0);
}

/* LDR (Load Double-Word Right) */
static int mips64_emit_LDR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LDR,base,offset,rt,TRUE);
   return(0);
}

/* LH (Load Half-Word) */
static int mips64_emit_LH(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LH,base,offset,rt,TRUE);
   return(0);
}

/* LHU (Load Half-Word Unsigned) */
static int mips64_emit_LHU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LHU,base,offset,rt,TRUE);
   return(0);
}

/* LI (virtual) */
static int mips64_emit_LI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);

   x86_mov_membase_imm(b->jit_ptr,X86_EDI,REG_OFFSET(rt),val & 0xffffffff,4);
   x86_mov_membase_imm(b->jit_ptr,X86_EDI,REG_OFFSET(rt)+4,val >> 32,4);
   return(0);
}

/* LL (Load Linked) */
static int mips64_emit_LL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LL,base,offset,rt,TRUE);
   return(0);
}

/* LUI */
static int mips64_emit_LUI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_LW(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,MIPS_MEMOP_LW,base,offset,rt,TRUE,
                             mips64_memop_fast_lw);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_LW,base,offset,rt,TRUE);
   }
   return(0);
}

/* LWL (Load Word Left) */
static int mips64_emit_LWL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LWL,base,offset,rt,TRUE);
   return(0);
}

/* LWR (Load Word Right) */
static int mips64_emit_LWR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LWR,base,offset,rt,TRUE);
   return(0);
}

/* LWU (Load Word Unsigned) */
static int mips64_emit_LWU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LWU,base,offset,rt,TRUE);
   return(0);
}

/* MFC0 */
static int mips64_emit_MFC0(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,cp0_exec_mfc0);
   return(0);
}

/* MFC1 */
static int mips64_emit_MFC1(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_exec_mfc1);
   return(0);
}

/* MFHI */
static int mips64_emit_MFHI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rd = bits(insn,11,15);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,OFFSET(cpu_mips_t,hi),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,OFFSET(cpu_mips_t,hi)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,REG_OFFSET(rd)+4,X86_EBX,4);

   return(0);
}

/* MFLO */
static int mips64_emit_MFLO(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_MOVE(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_MTC0(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,cp0_exec_mtc0);
   return(0);
}

/* MTC1 */
static int mips64_emit_MTC1(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_emit_cp_xfr_op(b,rt,rd,mips64_exec_mtc1);
   return(0);
}

/* MTHI */
static int mips64_emit_MTHI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,hi)+4,X86_EBX,4);

   return(0);
}

/* MTLO */
static int mips64_emit_MTLO(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);

   x86_mov_reg_membase(b->jit_ptr,X86_EAX,X86_EDI,REG_OFFSET(rs),4);
   x86_mov_reg_membase(b->jit_ptr,X86_EBX,X86_EDI,REG_OFFSET(rs)+4,4);

   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo),X86_EAX,4);
   x86_mov_membase_reg(b->jit_ptr,X86_EDI,OFFSET(cpu_mips_t,lo)+4,X86_EBX,4);

   return(0);
}

/* MUL */
static int mips64_emit_MUL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_MULT(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_MULTU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_NOP(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   //x86_nop(b->jit_ptr);
   return(0);
}

/* NOR */
static int mips64_emit_NOR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_OR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_ORI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_PREF(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   x86_nop(b->jit_ptr);
   return(0);
}

/* PREFI */
static int mips64_emit_PREFI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   x86_nop(b->jit_ptr);
   return(0);
}

/* SB (Store Byte) */
static int mips64_emit_SB(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SB,base,offset,rt,FALSE);
   return(0);
}

/* SC (Store Conditional) */
static int mips64_emit_SC(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SC,base,offset,rt,TRUE);
   return(0);
}

/* SD (Store Double-Word) */
static int mips64_emit_SD(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SD,base,offset,rt,FALSE);
   return(0);
}

/* SDL (Store Double-Word Left) */
static int mips64_emit_SDL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SDL,base,offset,rt,FALSE);
   return(0);
}

/* SDR (Store Double-Word Right) */
static int mips64_emit_SDR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SDR,base,offset,rt,FALSE);
   return(0);
}

/* SDC1 (Store Double-Word from Coprocessor 1) */
static int mips64_emit_SDC1(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int ft     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SDC1,base,offset,ft,FALSE);
   return(0);
}

/* SH (Store Half-Word) */
static int mips64_emit_SH(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SH,base,offset,rt,FALSE);
   return(0);
}

/* SLL */
static int mips64_emit_SLL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SLLV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SLT(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SLTI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SLTIU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SLTU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SRA(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SRAV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SRL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SRLV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SUB(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SUBU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_SW(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,MIPS_MEMOP_SW,base,offset,rt,FALSE,
                             mips64_memop_fast_sw);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_SW,base,offset,rt,FALSE);
   }
   return(0);
}

/* SWL (Store Word Left) */
static int mips64_emit_SWL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SWL,base,offset,rt,FALSE);
   return(0);
}

/* SWR (Store Word Right) */
static int mips64_emit_SWR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SWR,base,offset,rt,FALSE);
   return(0);
}

/* SYNC */
static int mips64_emit_SYNC(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   return(0);
}

/* SYSCALL */
static int mips64_emit_SYSCALL(cpu_mips_t *cpu,insn_block_t *b,
                               mips_insn_t insn)
{
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,mips64_exec_syscall);

   insn_block_push_epilog(b);
   return(0);
}

/* TEQ (Trap If Equal) */
static int mips64_emit_TEQ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_c_call(b,mips64_trigger_trap_exception);
   insn_block_push_epilog(b);

   /* end */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);
   return(0);
}

/* TEQI (Trap If Equal Immediate) */
static int mips64_emit_TEQI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_c_call(b,mips64_trigger_trap_exception);
   insn_block_push_epilog(b);

   /* end */
   x86_patch(test1,b->jit_ptr);
   x86_patch(test2,b->jit_ptr);
   return(0);
}

/* TLBP */
static int mips64_emit_TLBP(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,cp0_exec_tlbp);
   return(0);
}

/* TLBR */
static int mips64_emit_TLBR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{  
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,cp0_exec_tlbr);
   return(0);
}

/* TLBWI */
static int mips64_emit_TLBWI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,cp0_exec_tlbwi);
   return(0);
}

/* TLBWR */
static int mips64_emit_TLBWR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   x86_mov_reg_reg(b->jit_ptr,X86_EAX,X86_EDI,4);
   mips64_emit_basic_c_call(b,cp0_exec_tlbwr);
   return(0);
}

/* XOR */
static int mips64_emit_XOR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
static int mips64_emit_XORI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
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
struct insn_tag mips64_insn_tags[] = {
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
};
