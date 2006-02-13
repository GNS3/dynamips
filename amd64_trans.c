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

#include "amd64_trans.h"
#include "cp0.h"

/* Load a 64 bit immediate value */
static inline void mips64_load_imm(insn_block_t *b,u_int reg,
                                   m_uint64_t value)
{
   if (value > 0xffffffffULL)
      amd64_mov_reg_imm_size(b->jit_ptr,reg,value,8);
   else
      amd64_mov_reg_imm(b->jit_ptr,reg,value);
}

/* Set the Pointer Counter (PC) register */
void mips64_set_pc(insn_block_t *b,m_uint64_t new_pc)
{
   mips64_load_imm(b,AMD64_RAX,new_pc);
   amd64_mov_membase_reg(b->jit_ptr,
                         AMD64_R15,OFFSET(cpu_mips_t,pc),
                         AMD64_RAX,8);
}

/* Set the Return Address (RA) register */
void mips64_set_ra(insn_block_t *b,m_uint64_t ret_pc)
{
   mips64_load_imm(b,AMD64_RAX,ret_pc);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,
                         REG_OFFSET(MIPS_GPR_RA),
                         AMD64_RAX,8);
}

/* Set Jump */
static void mips64_set_jump(insn_block_t *b,m_uint64_t new_pc)
{   
   u_char *jump_ptr;

   /* set the new pc in cpu structure */
   mips64_set_pc(b,new_pc);

   if (insn_block_local_addr(b,new_pc,&jump_ptr)) {      
      if (jump_ptr) {
         amd64_jump_code(b->jit_ptr,jump_ptr);
      } else {
         insn_block_record_patch(b,b->jit_ptr,new_pc);
         amd64_jump32(b->jit_ptr,0);
      }
   } else {
      /* address is in another block, for now, returns to caller */
      insn_block_push_epilog(b);
   }
}

/* Basic C call */
static forced_inline 
void mips64_emit_basic_c_call(insn_block_t *b,void *f)
{
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RCX,f);
   amd64_call_reg(b->jit_ptr,AMD64_RCX);
}

/* Emit a simple call to a C function without any parameter */
static void mips64_emit_c_call(insn_block_t *b,void *f)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RCX,f);
   amd64_call_reg(b->jit_ptr,AMD64_RCX);
}

/* Memory operation */
static void mips64_emit_memop(insn_block_t *b,int op,int base,int offset,
                              int target,int keep_ll_bit)
{
   m_uint64_t val = sign_extend(offset,16);
   u_char *test1;

   /* Save PC for exception handling (delay slot management OK ?) */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   /* RDI = CPU instance */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);

   if (!keep_ll_bit) {
      amd64_clear_reg(b->jit_ptr,AMD64_RCX);
      amd64_mov_membase_reg(b->jit_ptr,AMD64_RDI,OFFSET(cpu_mips_t,ll_bit),
                            X86_ECX,4);
   }

   /* RSI = GPR[base] + sign-extended offset */
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RSI,val);
   amd64_alu_reg_membase(b->jit_ptr,X86_ADD,
                         AMD64_RSI,AMD64_RDI,REG_OFFSET(base));

   /* RDX = target register */
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RDX,target);

   /* Push parameters on stack and call memory function */
   amd64_call_membase(b->jit_ptr,AMD64_RDI,MEMOP_OFFSET(op));

   /* Exception ? */
   amd64_test_reg_reg_size(b->jit_ptr,AMD64_RAX,AMD64_RAX,4);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_Z, 0, 1);
   insn_block_push_epilog(b);
   amd64_patch(test1,b->jit_ptr);
}

/* Coprocessor Register transfert operation */
static void mips64_emit_cp_xfr_op(insn_block_t *b,int rt,int rd,void *f)
{
   /* update pc */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   /* cp0 register */
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RDX,rd);

   /* gpr */
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RSI,rt);

   /* cpu instance */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);

   mips64_emit_basic_c_call(b,f);
}

/* Virtual Breakpoint */
void mips64_emit_breakpoint(insn_block_t *b)
{
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_c_call(b,mips64_run_breakpoint);
}

/* Unknown opcode handler */
fastcall static void mips64_unknown_opcode(cpu_mips_t *cpu,m_uint32_t opcode)
{
   printf("CPU = %p\n",cpu);

   printf("MIPS64: unhandled opcode 0x%8.8x at 0x%llx (ra=0x%llx)\n",
          opcode,cpu->pc,cpu->gpr[MIPS_GPR_RA]);

   mips64_dump_regs(cpu);
   //exit(1);
}

/* Emit unhandled instruction code */
static int mips64_emit_unknown(cpu_mips_t *cpu,insn_block_t *b,
                               mips_insn_t opcode)
{  
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   amd64_mov_reg_imm(b->jit_ptr,AMD64_RSI,opcode);
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);

   mips64_emit_basic_c_call(b,mips64_unknown_opcode);
   return(0);
}

/* 
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
void mips64_inc_cp0_count_reg(insn_block_t *b)
{
   u_char *test1;

   /* increment the virtual count register */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,
                         AMD64_R15,OFFSET(cpu_mips_t,cp0_virt_cnt_reg),4);
   amd64_inc_reg_size(b->jit_ptr,AMD64_RAX,4);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,
                         OFFSET(cpu_mips_t,cp0_virt_cnt_reg),
                         AMD64_RAX,4);

   /* check with the virtual compare register */ 
   amd64_alu_reg_membase_size(b->jit_ptr,X86_CMP,AMD64_RAX,
                              AMD64_R15,OFFSET(cpu_mips_t,cp0_virt_cmp_reg),4);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* we have to trigger the timer irq  */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,mips64_trigger_timer_irq);

   amd64_patch(test1,b->jit_ptr);
}

/* Check if there are pending IRQ */
void mips64_check_pending_irq(insn_block_t *b)
{
   u_char *test1;

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,
                         AMD64_R15,OFFSET(cpu_mips_t,irq_pending),4);

   amd64_test_reg_reg_size(b->jit_ptr,AMD64_RAX,AMD64_RAX,4);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_Z, 0, 1);

   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,mips64_trigger_irq);
   insn_block_push_epilog(b);

   amd64_patch(test1,b->jit_ptr);
}

/* ADD */
static int mips64_emit_ADD(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_ADD,AMD64_RAX,AMD64_R15,
                         REG_OFFSET(rt));

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
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

   mips64_load_imm(b,AMD64_RAX,val);
   amd64_alu_reg_membase(b->jit_ptr,X86_ADD,AMD64_RAX,
                         AMD64_R15,REG_OFFSET(rs));

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RAX,8);
   return(0);
}

/* ADDIU */
static int mips64_emit_ADDIU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);

   mips64_load_imm(b,AMD64_RAX,val);

   if (rs != 0) {
      amd64_alu_reg_membase(b->jit_ptr,X86_ADD,AMD64_RAX,
                            AMD64_R15,REG_OFFSET(rs));
   }

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RDX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RDX,8);
   return(0);
}

/* ADDU */
static int mips64_emit_ADDU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_ADD,AMD64_RAX,AMD64_R15,
                         REG_OFFSET(rt));

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* AND */
static int mips64_emit_AND(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_AND,AMD64_RAX,AMD64_R15,
                         REG_OFFSET(rt));
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* ANDI */
static int mips64_emit_ANDI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   mips64_load_imm(b,AMD64_RAX,imm);

   amd64_alu_reg_membase(b->jit_ptr,X86_AND,AMD64_RAX,
                         AMD64_R15,REG_OFFSET(rs));

   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RAX,8);
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
   mips64_set_jump(b,new_pc);
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
   mips64_set_jump(b,new_pc);
   return(0);
}

/* BEQ (Branch On Equal) */
static int mips64_emit_BEQ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* 
    * compare gpr[rs] and gpr[rt]. 
    */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_CMP,AMD64_RAX,
                         AMD64_R15,REG_OFFSET(rt));
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);

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
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);
   
   /* 
    * compare gpr[rs] and gpr[rt]. 
    */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_CMP,AMD64_RAX,
                         AMD64_R15,REG_OFFSET(rt));
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* BEQZ (Branch On Equal Zero) */
static int mips64_emit_BEQZ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* 
    * compare gpr[rs] with 0. 
    */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);

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
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);

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
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGEZALL (Branch On Greater or Equal Than Zero And Link Likely) */
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

   /* If sign bit is set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);
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

   /* If sign bit is set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* BGTZ (Branch On Greater Than Zero) */
static int mips64_emit_BGTZ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* compare reg to zero */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_clear_reg(b->jit_ptr,AMD64_RCX);

   amd64_alu_reg_reg(b->jit_ptr,X86_CMP,AMD64_RAX,AMD64_RCX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_LE, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGTZL (Branch On Greater Than Zero Likely) */
static int mips64_emit_BGTZL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* compare reg to zero */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_clear_reg(b->jit_ptr,AMD64_RCX);

   amd64_alu_reg_reg(b->jit_ptr,X86_CMP,AMD64_RAX,AMD64_RCX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_LE, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* BLEZ (Branch On Less or Equal Than Zero) */
static int mips64_emit_BLEZ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* compare reg to zero */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_clear_reg(b->jit_ptr,AMD64_RCX);

   amd64_alu_reg_reg(b->jit_ptr,X86_CMP,AMD64_RAX,AMD64_RCX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_GT, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   insn_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BLEZL (Branch On Less or Equal Than Zero Likely) */
static int mips64_emit_BLEZL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);

   /* compare reg to zero */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_clear_reg(b->jit_ptr,AMD64_RCX);

   amd64_alu_reg_reg(b->jit_ptr,X86_CMP,AMD64_RAX,AMD64_RCX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_GT, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);
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

   /* If sign bit isn't set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);

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

   /* If sign bit isn't set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);

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

   /* If sign bit isn't set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);
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

   /* If sign bit isn't set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* BNE (Branch On Not Equal) */
static int mips64_emit_BNE(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int offset = bits(insn,0,15);
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);
   
   /* 
    * compare gpr[rs] and gpr[rt]. 
    */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_CMP,AMD64_RAX,
                         AMD64_R15,REG_OFFSET(rt));
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_E, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);

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
   u_char *test1;
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc += sign_extend(offset << 2,18);
   
   /* 
    * compare gpr[rs] and gpr[rt]. 
    */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_CMP,AMD64_RAX,
                         AMD64_R15,REG_OFFSET(rt));
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_E, 0, 1);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);

   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* BREAK */
static int mips64_emit_BREAK(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   u_int code = bits(insn,6,25);

   amd64_mov_reg_imm(b->jit_ptr,AMD64_RSI,code);
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
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

   mips64_emit_memop(b,MIPS_MEMOP_CACHE,base,offset,op,0);
   return(0);
}

/* DADDIU */
static int mips64_emit_DADDIU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);
   
   mips64_load_imm(b,AMD64_RCX,val);
   amd64_alu_reg_membase(b->jit_ptr,X86_ADD,AMD64_RCX,
                         AMD64_R15,REG_OFFSET(rs));
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RCX,8);
   return(0);
}

/* DADDU: rd = rs + rt */
static int mips64_emit_DADDU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_ADD,AMD64_RCX,
                         AMD64_R15,REG_OFFSET(rt));
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RCX,8);
   return(0);
}

/* DIV */
static int mips64_emit_DIV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   /* eax = gpr[rs] */
   amd64_clear_reg(b->jit_ptr,AMD64_RDX);
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),4);

   /* ecx = gpr[rt] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rt),4);

   /* eax = quotient (LO), edx = remainder (HI) */
   amd64_div_reg_size(b->jit_ptr,AMD64_RCX,1,4);

   /* store LO */
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_mips_t,lo),
                         AMD64_RAX,8);

   /* store HI */
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RDX,X86_EDX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_mips_t,hi),
                         AMD64_RDX,8);
   return(0);
}

/* DIVU */
static int mips64_emit_DIVU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   /* eax = gpr[rs] */
   amd64_clear_reg(b->jit_ptr,AMD64_RDX);
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),4);

   /* ecx = gpr[rt] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rt),4);

   /* eax = quotient (LO), edx = remainder (HI) */
   amd64_div_reg_size(b->jit_ptr,AMD64_RCX,0,4);

   /* store LO */
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_mips_t,lo),
                         AMD64_RAX,8);

   /* store HI */
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RDX,X86_EDX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_mips_t,hi),
                         AMD64_RDX,8);
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
   
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RAX,sa);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* DSLL32 */
static int mips64_emit_DSLL32(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);
   
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RAX,sa+32);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* DSLLV */
static int mips64_emit_DSLLV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rs),4);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,0x3f);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);
   amd64_shift_reg(b->jit_ptr,X86_SHL,AMD64_RAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* DSRA */
static int mips64_emit_DSRA(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);
   amd64_shift_reg_imm(b->jit_ptr,X86_SAR,AMD64_RAX,sa);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* DSRA32 */
static int mips64_emit_DSRA32(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);
   amd64_shift_reg_imm(b->jit_ptr,X86_SAR,AMD64_RAX,sa+32);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* DSRAV */
static int mips64_emit_DSRAV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rs),4);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,0x3f);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);
   amd64_shift_reg(b->jit_ptr,X86_SAR,AMD64_RAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* DSRL */
static int mips64_emit_DSRL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHR,AMD64_RAX,sa);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* DSRL32 */
static int mips64_emit_DSRL32(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHR,AMD64_RAX,sa+32);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* DSRLV */
static int mips64_emit_DSRLV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rs),4);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,0x3f);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);
   amd64_shift_reg(b->jit_ptr,X86_SHR,AMD64_RAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* DSUBU: rd = rs - rt */
static int mips64_emit_DSUBU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_SUB,AMD64_RAX,
                         AMD64_R15,REG_OFFSET(rt));
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* ERET */
static int mips64_emit_ERET(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
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
   mips64_set_jump(b,new_pc);
   return(0);
}

/* JAL (Jump And Link) */
static int mips64_emit_JAL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   u_int instr_index = bits(insn,0,25);
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc &= ~((1 << 28) - 1);
   new_pc |= instr_index << 2;

   /* set the return address (instruction after the delay slot) */
   mips64_set_ra(b,b->start_pc + ((b->mips_trans_pos + 1) << 2));

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(b,new_pc);
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
   mips64_load_imm(b,AMD64_RAX,ret_pc);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);

   /* get the new pc */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_R14,AMD64_R15,REG_OFFSET(rs),8);

   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc */
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_mips_t,pc),
                         AMD64_R14,8);

   /* returns to the caller which will determine the next path */
   insn_block_push_epilog(b);
   return(0);
}

/* JR (Jump Register) */
static int mips64_emit_JR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rs = bits(insn,21,25);

   /* get the new pc */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_push_reg(b->jit_ptr,AMD64_RCX);
      
   /* insert the instruction in the delay slot */
   insn_fetch_and_emit(cpu,b,1);

   /* set the new pc */
   amd64_pop_reg(b->jit_ptr,AMD64_RCX);
   amd64_mov_membase_reg(b->jit_ptr,
                         AMD64_R15,OFFSET(cpu_mips_t,pc),
                         AMD64_RCX,8);

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

   mips64_load_imm(b,AMD64_RCX,val);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RCX,8);
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

#if 1
   mips64_load_imm(b,AMD64_RCX,val);
#else
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RCX,imm);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RCX,48);
   amd64_shift_reg_imm(b->jit_ptr,X86_SAR,AMD64_RCX,32);
#endif

   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RCX,8);
   return(0);
}

/* LW (Load Word) */
static int mips64_emit_LW(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LW,base,offset,rt,TRUE);
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

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_R15,OFFSET(cpu_mips_t,hi),8);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RDX,8);
   return(0);
}

/* MFLO */
static int mips64_emit_MFLO(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rd = bits(insn,11,15);

   if (!rd) return(0);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_R15,OFFSET(cpu_mips_t,lo),8);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RDX,8);
   return(0);
}

/* MOVE (virtual instruction, real: ADDU) */
static int mips64_emit_MOVE(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rs = bits(insn,21,25);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,AMD64_R15,REG_OFFSET(rs),4);
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RDX,X86_EDX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RDX,8);
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

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,AMD64_R15,REG_OFFSET(rs),8);

   amd64_mov_membase_reg(b->jit_ptr,
                         AMD64_R15,OFFSET(cpu_mips_t,hi),AMD64_RDX,8);
   return(0);
}

/* MTLO */
static int mips64_emit_MTLO(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,AMD64_R15,REG_OFFSET(rs),8);

   amd64_mov_membase_reg(b->jit_ptr,
                         AMD64_R15,OFFSET(cpu_mips_t,lo),AMD64_RDX,8);
   return(0); 
}

/* MULT */
static int mips64_emit_MULT(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   /* eax = gpr[rs] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),4);

   /* ecx = gpr[rt] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rt),4);

   amd64_mul_reg_size(b->jit_ptr,AMD64_RCX,1,4);

   /* store LO */
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_mips_t,lo),
                         AMD64_RAX,8);

   /* store HI */
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RDX,X86_EDX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_mips_t,hi),
                         AMD64_RDX,8);
   return(0);
}

/* MULTU */
static int mips64_emit_MULTU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   /* eax = gpr[rs] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),4);

   /* ecx = gpr[rt] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rt),4);

   amd64_mul_reg_size(b->jit_ptr,AMD64_RCX,0,4);

   /* store LO */
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_mips_t,lo),
                         AMD64_RAX,8);

   /* store HI */
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RDX,X86_EDX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_mips_t,hi),
                         AMD64_RDX,8);
   return(0);
}

/* NOP */
static int mips64_emit_NOP(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   return(0);
}

/* NOR */
static int mips64_emit_NOR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_OR,AMD64_RAX,AMD64_R15,
                         REG_OFFSET(rt));
   amd64_not_reg(b->jit_ptr,AMD64_RAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* OR */
static int mips64_emit_OR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_OR,AMD64_RAX,AMD64_R15,
                         REG_OFFSET(rt));
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* ORI */
static int mips64_emit_ORI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   mips64_load_imm(b,AMD64_RAX,imm);

   amd64_alu_reg_membase(b->jit_ptr,X86_OR,AMD64_RAX,
                         AMD64_R15,REG_OFFSET(rs));

   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RAX,8);
   return(0);
}

/* PREF */
static int mips64_emit_PREF(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   amd64_nop(b->jit_ptr);
   return(0);
}

/* PREFI */
static int mips64_emit_PREFI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   amd64_nop(b->jit_ptr);
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

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),4);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RAX,sa);

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* SLLV */
static int mips64_emit_SLLV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rs),4);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,0x1f);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),4);
   amd64_shift_reg(b->jit_ptr,X86_SHL,AMD64_RAX);

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* SLT */
static int mips64_emit_SLT(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   u_char *test1;

   /* RDX = gpr[rs] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,AMD64_R15,REG_OFFSET(rs),8);
   
   /* RAX = gpr[rt] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);

   /* we set rd to 1 when gpr[rs] < gpr[rt] */
   amd64_clear_reg(b->jit_ptr,AMD64_RCX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RCX,8);

   amd64_alu_reg_reg(b->jit_ptr,X86_CMP,AMD64_RDX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_GE, 0, 1);
   
   amd64_inc_membase(b->jit_ptr,AMD64_R15,REG_OFFSET(rd));

   /* end */
   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* SLTI */
static int mips64_emit_SLTI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);
   u_char *test1;

   /* RDX = val */
   mips64_load_imm(b,AMD64_RDX,val);
   
   /* RAX = gpr[rs] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);

   /* we set rt to 1 when gpr[rs] < val */
   amd64_clear_reg(b->jit_ptr,AMD64_RCX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RCX,8);

   amd64_alu_reg_reg(b->jit_ptr,X86_CMP,AMD64_RAX,AMD64_RDX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_GE, 0, 1);
   
   amd64_inc_membase(b->jit_ptr,AMD64_R15,REG_OFFSET(rt));

   /* end */
   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* SLTU */
static int mips64_emit_SLTU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   u_char *test1;

   /* RDX = gpr[rs] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,AMD64_R15,REG_OFFSET(rs),8);
   
   /* RAX = gpr[rt] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);

   /* we set rd to 1 when gpr[rs] < gpr[rt] */
   amd64_clear_reg(b->jit_ptr,AMD64_RCX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RCX,8);

   amd64_alu_reg_reg(b->jit_ptr,X86_CMP,AMD64_RDX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_AE, 0, 0);
   
   amd64_inc_membase(b->jit_ptr,AMD64_R15,REG_OFFSET(rd));

   /* end */
   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* SLTIU */
static int mips64_emit_SLTIU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);
   u_char *test1;

   /* RDX = val */
   mips64_load_imm(b,AMD64_RDX,val);
   
   /* RAX = gpr[rs] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);

   /* we set rt to 1 when gpr[rs] < val */
   amd64_clear_reg(b->jit_ptr,AMD64_RCX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RCX,8);

   amd64_alu_reg_reg(b->jit_ptr,X86_CMP,AMD64_RAX,AMD64_RDX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_AE, 0, 0);
   
   amd64_inc_membase(b->jit_ptr,AMD64_R15,REG_OFFSET(rt));

   /* end */
   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* SRA */
static int mips64_emit_SRA(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),4);
   amd64_shift_reg_imm_size(b->jit_ptr,X86_SAR,AMD64_RAX,sa,4);

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* SRAV */
static int mips64_emit_SRAV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rs),4);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,0x1f);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),4);
   amd64_shift_reg(b->jit_ptr,X86_SAR,AMD64_RAX);

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* SRL */
static int mips64_emit_SRL(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),4);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHR,AMD64_RAX,sa);

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* SRLV */
static int mips64_emit_SRLV(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rs),4);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,0x1f);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),4);
   amd64_shift_reg(b->jit_ptr,X86_SHR,AMD64_RAX);

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* SUBU */
static int mips64_emit_SUBU(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_SUB,AMD64_RAX,AMD64_R15,
                         REG_OFFSET(rt));

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* SW (Store Word) */
static int mips64_emit_SW(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SW,base,offset,rt,FALSE);
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
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,mips64_exec_syscall);
   insn_block_push_epilog(b);
   return(0);
}

/* TEQ (Trap If Equal) */
static int mips64_emit_TEQ(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   u_char *test1;

   /* 
    * compare gpr[rs] and gpr[rt]. 
    */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_CMP,AMD64_RAX,
                         AMD64_R15,REG_OFFSET(rt));
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* Generate trap exception */
   mips64_emit_c_call(b,mips64_trigger_trap_exception);
   insn_block_push_epilog(b);

   /* end */
   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* TEQI (Trap If Equal Immediate) */
static int mips64_emit_TEQI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs  = bits(insn,21,25);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);
   u_char *test1;

   /* RDX = val */
   mips64_load_imm(b,AMD64_RDX,val);
   
   /* RAX = gpr[rs] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);

   amd64_alu_reg_reg(b->jit_ptr,X86_CMP,AMD64_RAX,AMD64_RDX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* Generate trap exception */
   mips64_emit_c_call(b,mips64_trigger_trap_exception);
   insn_block_push_epilog(b);

   /* end */
   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* TLBP */
static int mips64_emit_TLBP(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,cp0_exec_tlbp);
   return(0);
}

/* TLBR */
static int mips64_emit_TLBR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{  
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,cp0_exec_tlbr);
   return(0);
}

/* TLBWI */
static int mips64_emit_TLBWI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,cp0_exec_tlbwi);
   return(0);
}

/* XOR */
static int mips64_emit_XOR(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_XOR,AMD64_RAX,AMD64_R15,
                         REG_OFFSET(rt));
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* XORI */
static int mips64_emit_XORI(cpu_mips_t *cpu,insn_block_t *b,mips_insn_t insn)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   mips64_load_imm(b,AMD64_RAX,imm);

   amd64_alu_reg_membase(b->jit_ptr,X86_XOR,AMD64_RAX,
                         AMD64_R15,REG_OFFSET(rs));

   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RAX,8);
   return(0);
}

/* MIPS instruction array */
struct insn_tag mips64_insn_tags[] = {
   { mips64_emit_LI      , 0xffe00000 , 0x24000000, 1 },   /* virtual */
   { mips64_emit_MOVE    , 0xfc1f07ff , 0x00000021, 1 },   /* virtual */
   { mips64_emit_B       , 0xffff0000 , 0x10000000, 0 },   /* virtual */
   { mips64_emit_BAL     , 0xffff0000 , 0x04110000, 0 },   /* virtual */
   { mips64_emit_BEQZ    , 0xfc1f0000 , 0x10000000, 0 },   /* virtual */
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
   { mips64_emit_MFC0    , 0xffe007f8 , 0x40000000, 1 },
   { mips64_emit_MFC1    , 0xffe007ff , 0x44000000, 1 },
   { mips64_emit_MFHI    , 0xffff07ff , 0x00000010, 1 },
   { mips64_emit_MFLO    , 0xffff07ff , 0x00000012, 1 },
   { mips64_emit_MTC0    , 0xffe007f8 , 0x40800000, 1 },
   { mips64_emit_MTC1    , 0xffe007ff , 0x44800000, 1 },
   { mips64_emit_MTHI    , 0xfc1fffff , 0x00000011, 1 },
   { mips64_emit_MTLO    , 0xfc1fffff , 0x00000013, 1 },
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
   { mips64_emit_XOR     , 0xfc0007ff , 0x00000026, 1 },
   { mips64_emit_XORI    , 0xfc000000 , 0x38000000, 1 },
   { mips64_emit_unknown , 0x00000000 , 0x00000000, 1 },
};
