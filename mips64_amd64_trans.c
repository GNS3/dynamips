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
#include <sys/mman.h>
#include <fcntl.h>

#include "cpu.h"
#include "mips64_jit.h"
#include "mips64_amd64_trans.h"
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
static inline void mips64_load_imm(mips64_jit_tcb_t *b,u_int reg,
                                   m_uint64_t value)
{
   if (value > 0xffffffffULL)
      amd64_mov_reg_imm_size(b->jit_ptr,reg,value,8);
   else
      amd64_mov_reg_imm(b->jit_ptr,reg,value);
}

/* Set the Pointer Counter (PC) register */
void mips64_set_pc(mips64_jit_tcb_t *b,m_uint64_t new_pc)
{
   mips64_load_imm(b,AMD64_RAX,new_pc);
   amd64_mov_membase_reg(b->jit_ptr,
                         AMD64_R15,OFFSET(cpu_mips_t,pc),
                         AMD64_RAX,8);
}

/* Set the Return Address (RA) register */
void mips64_set_ra(mips64_jit_tcb_t *b,m_uint64_t ret_pc)
{
   mips64_load_imm(b,AMD64_RAX,ret_pc);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,
                         REG_OFFSET(MIPS_GPR_RA),
                         AMD64_RAX,8);
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
   u_char *test1,*test2,*test3;

   new_page = new_pc & MIPS_MIN_PAGE_MASK;
   pc_offset = (new_pc & MIPS_MIN_PAGE_IMASK) >> 2;
   pc_hash = mips64_jit_get_pc_hash(new_pc);
   
   /* Get JIT block info in %rdx */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RBX,
                         AMD64_R15,OFFSET(cpu_mips_t,exec_blk_map),8);
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_RBX,pc_hash*sizeof(void *),8);

   /* no JIT block found ? */
   amd64_test_reg_reg(b->jit_ptr,AMD64_RDX,AMD64_RDX);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_Z, 0, 1);

   /* Check block IA */
   mips64_load_imm(b,AMD64_RAX,new_page);
   amd64_alu_reg_membase_size(b->jit_ptr,X86_CMP,X86_EAX,AMD64_RDX,
                              OFFSET(mips64_jit_tcb_t,start_pc),4);
   test2 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_NE, 0, 1);

   /* Jump to the code */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RSI,
                         AMD64_RDX,OFFSET(mips64_jit_tcb_t,jit_insn_ptr),8);
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RBX,
                         AMD64_RSI,pc_offset * sizeof(void *),8);
   
   amd64_test_reg_reg(b->jit_ptr,AMD64_RBX,AMD64_RBX);
   test3 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_Z, 0, 1);
   amd64_jump_reg(b->jit_ptr,AMD64_RBX);

   /* Returns to caller... */
   amd64_patch(test1,b->jit_ptr);
   amd64_patch(test2,b->jit_ptr);
   amd64_patch(test3,b->jit_ptr);

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
         amd64_jump_code(b->jit_ptr,jump_ptr);
      } else {
         /* Never jump directly to code in a delay slot */
         if (mips64_jit_is_delay_slot(b,new_pc)) {
            mips64_set_pc(b,new_pc);
            mips64_jit_tcb_push_epilog(b);
            return;
         }

         mips64_jit_tcb_record_patch(b,b->jit_ptr,new_pc);
         amd64_jump32(b->jit_ptr,0);
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
static forced_inline 
void mips64_emit_basic_c_call(mips64_jit_tcb_t *b,void *f)
{
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RCX,f);
   amd64_call_reg(b->jit_ptr,AMD64_RCX);
}

/* Emit a simple call to a C function without any parameter */
static void mips64_emit_c_call(mips64_jit_tcb_t *b,void *f)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RCX,f);
   amd64_call_reg(b->jit_ptr,AMD64_RCX);
}

/* Single-step operation */
void mips64_emit_single_step(mips64_jit_tcb_t *b,mips_insn_t insn)
{
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RSI,insn);
   mips64_emit_basic_c_call(b,mips64_exec_single_step);
}

/* Fast memory operation prototype */
typedef void (*memop_fast_access)(mips64_jit_tcb_t *b,int target);

/* Fast LW */
static void mips64_memop_fast_lw(mips64_jit_tcb_t *b,int target)
{
   amd64_mov_reg_memindex(b->jit_ptr,AMD64_RAX,AMD64_RBX,0,AMD64_RSI,0,4);
   amd64_bswap32(b->jit_ptr,X86_EAX);
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RDX,X86_EAX);
   
   /* Save value in register */
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(target),AMD64_RDX,8);
}

/* Fast SW */
static void mips64_memop_fast_sw(mips64_jit_tcb_t *b,int target)
{
   /* Load value from register */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(target),4);
   amd64_bswap32(b->jit_ptr,X86_EAX);
   amd64_mov_memindex_reg(b->jit_ptr,AMD64_RBX,0,AMD64_RSI,0,AMD64_RAX,4);
}

/* Fast memory operation (64-bit) */
static void mips64_emit_memop_fast64(mips64_jit_tcb_t *b,int write_op,
                                     int opcode,int base,int offset,
                                     int target,int keep_ll_bit,
                                     memop_fast_access op_handler)
{   
   m_uint32_t val = sign_extend(offset,16);
   u_char *test1,*test2,*p_exit;

   test2 = NULL;

   /* RSI = GPR[base] + sign-extended offset */
   mips64_load_imm(b,AMD64_RSI,val);
   amd64_alu_reg_membase(b->jit_ptr,X86_ADD,
                         AMD64_RSI,AMD64_R15,REG_OFFSET(base));

   /* RBX = mts64_entry index */
   amd64_mov_reg_reg_size(b->jit_ptr,X86_EBX,X86_ESI,4);
   amd64_shift_reg_imm_size(b->jit_ptr,X86_SHR,X86_EBX,MTS64_HASH_SHIFT,4);
   amd64_alu_reg_imm_size(b->jit_ptr,X86_AND,X86_EBX,MTS64_HASH_MASK,4);

   /* RCX = mts32 entry */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,
                         AMD64_R15,
                         OFFSET(cpu_mips_t,mts_u.mts64_cache),8);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,5);  /* TO FIX */
   amd64_alu_reg_reg(b->jit_ptr,X86_ADD,AMD64_RCX,AMD64_RBX);

   /* Compare virtual page address (EAX = vpage) */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RSI,8);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,MIPS_MIN_PAGE_MASK);

   amd64_alu_reg_membase_size(b->jit_ptr,X86_CMP,AMD64_RAX,AMD64_RCX,
                              OFFSET(mts64_entry_t,gvpa),8);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* Test if we are writing to a COW page */
   if (write_op) {
      amd64_test_membase_imm_size(b->jit_ptr,
                                  AMD64_RCX,OFFSET(mts64_entry_t,flags),
                                  MTS_FLAG_COW,4);
      test2 = b->jit_ptr;
      amd64_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);
   }

   /* ESI = offset in page, RBX = Host Page Address */
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,X86_ESI,MIPS_MIN_PAGE_IMASK);
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RBX,
                         AMD64_RCX,OFFSET(mts64_entry_t,hpa),8);

   /* Memory access */
   op_handler(b,target);

   p_exit = b->jit_ptr;
   amd64_jump8(b->jit_ptr,0);
   if (test2)
      amd64_patch(test2,b->jit_ptr);

   /* === Slow lookup === */
   amd64_patch(test1,b->jit_ptr);

   /* Save PC for exception handling */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   /* Sign-extend virtual address */
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RSI,X86_ESI);

   /* RDX = target register */
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RDX,target);

   /* RDI = CPU instance */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);

   /* Call memory access function */
   amd64_call_membase(b->jit_ptr,AMD64_R15,MEMOP_OFFSET(opcode));

   amd64_patch(p_exit,b->jit_ptr);
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

   /* ESI = GPR[base] + sign-extended offset */
   amd64_mov_reg_imm(b->jit_ptr,X86_ESI,val);
   amd64_alu_reg_membase_size(b->jit_ptr,X86_ADD,
                              X86_ESI,AMD64_R15,REG_OFFSET(base),4);

   /* RBX = mts32_entry index */
   amd64_mov_reg_reg_size(b->jit_ptr,X86_EBX,X86_ESI,4);
   amd64_shift_reg_imm_size(b->jit_ptr,X86_SHR,X86_EBX,MTS32_HASH_SHIFT,4);
   amd64_alu_reg_imm_size(b->jit_ptr,X86_AND,X86_EBX,MTS32_HASH_MASK,4);

   /* RCX = mts32 entry */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,
                         AMD64_R15,
                         OFFSET(cpu_mips_t,mts_u.mts32_cache),8);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,5);  /* TO FIX */
   amd64_alu_reg_reg(b->jit_ptr,X86_ADD,AMD64_RCX,AMD64_RBX);

   /* Compare virtual page address (EAX = vpage) */
   amd64_mov_reg_reg(b->jit_ptr,X86_EAX,X86_ESI,4);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,X86_EAX,MIPS_MIN_PAGE_MASK);

   amd64_alu_reg_membase_size(b->jit_ptr,X86_CMP,X86_EAX,AMD64_RCX,
                              OFFSET(mts32_entry_t,gvpa),4);
   test1 = b->jit_ptr;
   x86_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* Test if we are writing to a COW page */
   if (write_op) {
      amd64_test_membase_imm_size(b->jit_ptr,
                                  AMD64_RCX,OFFSET(mts32_entry_t,flags),
                                  MTS_FLAG_COW,4);
      test2 = b->jit_ptr;
      amd64_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);
   }

   /* ESI = offset in page, RBX = Host Page Address */
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,X86_ESI,MIPS_MIN_PAGE_IMASK);
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RBX,
                         AMD64_RCX,OFFSET(mts32_entry_t,hpa),8);

   /* Memory access */
   op_handler(b,target);

   p_exit = b->jit_ptr;
   amd64_jump8(b->jit_ptr,0);

   /* === Slow lookup === */
   amd64_patch(test1,b->jit_ptr);
   if (test2)
      amd64_patch(test2,b->jit_ptr);

   /* Save PC for exception handling */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   /* Sign-extend virtual address */
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RSI,X86_ESI);

   /* RDX = target register */
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RDX,target);

   /* RDI = CPU instance */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);

   /* Call memory access function */
   amd64_call_membase(b->jit_ptr,AMD64_R15,MEMOP_OFFSET(opcode));

   amd64_patch(p_exit,b->jit_ptr);
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

   /* RDI = CPU instance */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);

   if (!keep_ll_bit) {
      amd64_clear_reg(b->jit_ptr,AMD64_RCX);
      amd64_mov_membase_reg(b->jit_ptr,AMD64_RDI,OFFSET(cpu_mips_t,ll_bit),
                            X86_ECX,4);
   }

   /* RSI = GPR[base] + sign-extended offset */
   mips64_load_imm(b,AMD64_RSI,val);
   amd64_alu_reg_membase(b->jit_ptr,X86_ADD,
                         AMD64_RSI,AMD64_RDI,REG_OFFSET(base));

   /* RDX = target register */
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RDX,target);

   /* Call memory access function */
   amd64_call_membase(b->jit_ptr,AMD64_RDI,MEMOP_OFFSET(op));
}

/* Coprocessor Register transfert operation */
static void mips64_emit_cp_xfr_op(mips64_jit_tcb_t *b,int rt,int rd,void *f)
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
void mips64_emit_breakpoint(mips64_jit_tcb_t *b)
{
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_c_call(b,mips64_run_breakpoint);
}

/* Unknown opcode handler */
static fastcall void mips64_unknown_opcode(cpu_mips_t *cpu,m_uint32_t opcode)
{
   printf("CPU = %p\n",cpu);

   printf("MIPS64: unhandled opcode 0x%8.8x at 0x%llx (ra=0x%llx)\n",
          opcode,cpu->pc,cpu->gpr[MIPS_GPR_RA]);

   mips64_dump_regs(cpu->gen);
}

/* Emit unhandled instruction code */
static int mips64_emit_unknown(cpu_mips_t *cpu,mips64_jit_tcb_t *b,
                               mips_insn_t opcode)
{  
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RSI,opcode);
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);

   mips64_emit_c_call(b,mips64_unknown_opcode);
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
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_c_call(b,mips64_invalid_delay_slot);
   return(0);
}

/* 
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
void mips64_inc_cp0_count_reg(mips64_jit_tcb_t *b)
{   
   amd64_inc_membase(b->jit_ptr,AMD64_R15,OFFSET(cpu_mips_t,cp0_virt_cnt_reg));

#if 0 /* TIMER_IRQ */
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
#endif
}

/* Check if there are pending IRQ */
void mips64_check_pending_irq(mips64_jit_tcb_t *b)
{
   u_char *test1;

   /* Check the pending IRQ flag */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,
                         AMD64_R15,OFFSET(cpu_mips_t,irq_pending),4);

   amd64_test_reg_reg_size(b->jit_ptr,AMD64_RAX,AMD64_RAX,4);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_Z, 0, 1);

   /* Update PC */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   /* Trigger the IRQ */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,mips64_trigger_irq);
   mips64_jit_tcb_push_epilog(b);

   amd64_patch(test1,b->jit_ptr);
}

/* Increment the number of executed instructions (performance debugging) */
void mips64_inc_perf_counter(mips64_jit_tcb_t *b)
{ 
   amd64_inc_membase_size(b->jit_ptr,
                          AMD64_R15,OFFSET(cpu_mips_t,perf_counter),4);
}

/* ADD */
DECLARE_INSN(ADD)
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
DECLARE_INSN(ADDI)
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
DECLARE_INSN(ADDIU)
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
DECLARE_INSN(ADDU)
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
DECLARE_INSN(AND)
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
DECLARE_INSN(ANDI)
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
   amd64_branch32(b->jit_ptr, X86_CC_NE, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);

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
   amd64_branch32(b->jit_ptr, X86_CC_NE, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* BEQZ (Branch On Equal Zero) */
DECLARE_INSN(BEQZ)
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
   amd64_branch32(b->jit_ptr, X86_CC_NZ, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BNEZ (Branch On Not Equal Zero) */
DECLARE_INSN(BNEZ)
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
   amd64_branch32(b->jit_ptr, X86_CC_Z, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);

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
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch32(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);

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
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch32(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGEZALL (Branch On Greater or Equal Than Zero And Link Likely) */
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

   /* If sign bit is set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch32(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);
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

   /* If sign bit is set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch32(b->jit_ptr, X86_CC_S, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* BGTZ (Branch On Greater Than Zero) */
DECLARE_INSN(BGTZ)
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
   amd64_branch32(b->jit_ptr, X86_CC_LE, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGTZL (Branch On Greater Than Zero Likely) */
DECLARE_INSN(BGTZL)
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
   amd64_branch32(b->jit_ptr, X86_CC_LE, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* BLEZ (Branch On Less or Equal Than Zero) */
DECLARE_INSN(BLEZ)
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
   amd64_branch32(b->jit_ptr, X86_CC_GT, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BLEZL (Branch On Less or Equal Than Zero Likely) */
DECLARE_INSN(BLEZL)
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
   amd64_branch32(b->jit_ptr, X86_CC_GT, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);
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

   /* If sign bit isn't set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch32(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);

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

   /* If sign bit isn't set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch32(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);

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

   /* If sign bit isn't set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch32(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);
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

   /* If sign bit isn't set, don't take the branch */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
   test1 = b->jit_ptr;
   amd64_branch32(b->jit_ptr, X86_CC_NS, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* BNE (Branch On Not Equal) */
DECLARE_INSN(BNE)
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
   amd64_branch32(b->jit_ptr, X86_CC_E, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);

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
   amd64_branch32(b->jit_ptr, X86_CC_E, 0, 1);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* BREAK */
DECLARE_INSN(BREAK)
{	
   u_int code = bits(insn,6,25);

   amd64_mov_reg_imm(b->jit_ptr,AMD64_RSI,code);
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,mips64_exec_break);
   mips64_jit_tcb_push_epilog(b);
   return(0);
}

/* CACHE */
DECLARE_INSN(CACHE)
{        
   int base   = bits(insn,21,25);
   int op     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_CACHE,base,offset,op,0);
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
   
   mips64_load_imm(b,AMD64_RCX,val);
   amd64_alu_reg_membase(b->jit_ptr,X86_ADD,AMD64_RCX,
                         AMD64_R15,REG_OFFSET(rs));
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RCX,8);
   return(0);
}

/* DADDU: rd = rs + rt */
DECLARE_INSN(DADDU)
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
DECLARE_INSN(DIV)
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
DECLARE_INSN(DIVU)
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
   
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),8);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RAX,sa);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* DSLL32 */
DECLARE_INSN(DSLL32)
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
DECLARE_INSN(DSLLV)
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
DECLARE_INSN(DSRA)
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
DECLARE_INSN(DSRA32)
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
DECLARE_INSN(DSRAV)
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
DECLARE_INSN(DSRL)
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
DECLARE_INSN(DSRL32)
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
DECLARE_INSN(DSRLV)
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
DECLARE_INSN(DSUBU)
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
DECLARE_INSN(ERET)
{
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
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
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = b->start_pc + (b->mips_trans_pos << 2);
   new_pc &= ~((1 << 28) - 1);
   new_pc |= instr_index << 2;

   /* set the return address (instruction after the delay slot) */
   mips64_set_ra(b,b->start_pc + ((b->mips_trans_pos + 1) << 2));

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
   mips64_load_imm(b,AMD64_RAX,ret_pc);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);

   /* get the new pc */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_R14,AMD64_R15,REG_OFFSET(rs),8);

#if DEBUG_JR0
   {
      u_char *test1;

      amd64_test_reg_reg(b->jit_ptr,AMD64_R14,AMD64_R14);
      test1 = b->jit_ptr;
      amd64_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);
      amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
      mips64_emit_c_call(b,mips64_debug_jr0);
      amd64_patch(test1,b->jit_ptr);
   }
#endif

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc */
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_mips_t,pc),
                         AMD64_R14,8);

   /* returns to the caller which will determine the next path */
   mips64_jit_tcb_push_epilog(b);
   return(0);
}

/* JR (Jump Register) */
DECLARE_INSN(JR)
{
   int rs = bits(insn,21,25);

   /* get the new pc */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_R14,AMD64_R15,REG_OFFSET(rs),8);
      
#if DEBUG_JR0
   {
      u_char *test1;

      amd64_test_reg_reg(b->jit_ptr,AMD64_RCX,AMD64_RCX);
      test1 = b->jit_ptr;
      amd64_branch8(b->jit_ptr, X86_CC_NZ, 0, 1);
      amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
      mips64_emit_c_call(b,mips64_debug_jr0);
      amd64_patch(test1,b->jit_ptr);
   }
#endif

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc */
   amd64_mov_membase_reg(b->jit_ptr,
                         AMD64_R15,OFFSET(cpu_mips_t,pc),
                         AMD64_R14,8);

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

   mips64_load_imm(b,AMD64_RCX,val);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rt),AMD64_RCX,8);
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

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_R15,OFFSET(cpu_mips_t,hi),8);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RDX,8);
   return(0);
}

/* MFLO */
DECLARE_INSN(MFLO)
{
   int rd = bits(insn,11,15);

   if (!rd) return(0);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_R15,OFFSET(cpu_mips_t,lo),8);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RDX,8);
   return(0);
}

/* MOVE (virtual instruction, real: ADDU) */
DECLARE_INSN(MOVE)
{	
   int rs = bits(insn,21,25);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,AMD64_R15,REG_OFFSET(rs),4);
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RDX,X86_EDX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RDX,8);
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

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,AMD64_R15,REG_OFFSET(rs),8);

   amd64_mov_membase_reg(b->jit_ptr,
                         AMD64_R15,OFFSET(cpu_mips_t,hi),AMD64_RDX,8);
   return(0);
}

/* MTLO */
DECLARE_INSN(MTLO)
{
   int rs = bits(insn,21,25);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,AMD64_R15,REG_OFFSET(rs),8);

   amd64_mov_membase_reg(b->jit_ptr,
                         AMD64_R15,OFFSET(cpu_mips_t,lo),AMD64_RDX,8);
   return(0); 
}

/* MUL */
DECLARE_INSN(MUL)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   /* eax = gpr[rs] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),4);

   /* ecx = gpr[rt] */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rt),4);

   amd64_mul_reg_size(b->jit_ptr,AMD64_RCX,1,4);

   /* store result in gpr[rd] */
   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* MULT */
DECLARE_INSN(MULT)
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
DECLARE_INSN(MULTU)
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
DECLARE_INSN(NOP)
{
   return(0);
}

/* NOR */
DECLARE_INSN(NOR)
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
DECLARE_INSN(OR)
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
DECLARE_INSN(ORI)
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
DECLARE_INSN(PREF)
{
   amd64_nop(b->jit_ptr);
   return(0);
}

/* PREFI */
DECLARE_INSN(PREFI)
{
   amd64_nop(b->jit_ptr);
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

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),4);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RAX,sa);

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* SLLV */
DECLARE_INSN(SLLV)
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
DECLARE_INSN(SLT)
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
DECLARE_INSN(SLTI)
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
DECLARE_INSN(SLTU)
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
DECLARE_INSN(SLTIU)
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
DECLARE_INSN(SRA)
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
DECLARE_INSN(SRAV)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,AMD64_R15,REG_OFFSET(rs),4);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,0x1f);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rt),4);
   amd64_shift_reg_size(b->jit_ptr,X86_SAR,AMD64_RAX,4);

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* SRL */
DECLARE_INSN(SRL)
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
DECLARE_INSN(SRLV)
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

/* SUB */
DECLARE_INSN(SUB)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   
   /* TODO: Exception handling */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,AMD64_R15,REG_OFFSET(rs),8);
   amd64_alu_reg_membase(b->jit_ptr,X86_SUB,AMD64_RAX,AMD64_R15,
                         REG_OFFSET(rt));

   amd64_movsxd_reg_reg(b->jit_ptr,AMD64_RAX,X86_EAX);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(rd),AMD64_RAX,8);
   return(0);
}

/* SUBU */
DECLARE_INSN(SUBU)
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
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,mips64_exec_syscall);
   mips64_jit_tcb_push_epilog(b);
   return(0);
}

/* TEQ (Trap If Equal) */
DECLARE_INSN(TEQ)
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
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_c_call(b,mips64_trigger_trap_exception);
   mips64_jit_tcb_push_epilog(b);

   /* end */
   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* TEQI (Trap If Equal Immediate) */
DECLARE_INSN(TEQI)
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
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_c_call(b,mips64_trigger_trap_exception);
   mips64_jit_tcb_push_epilog(b);

   /* end */
   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* TLBP */
DECLARE_INSN(TLBP)
{
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbp);
   return(0);
}

/* TLBR */
DECLARE_INSN(TLBR)
{  
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbr);
   return(0);
}

/* TLBWI */
DECLARE_INSN(TLBWI)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbwi);
   return(0);
}

/* TLBWR */
DECLARE_INSN(TLBWR)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbwr);
   return(0);
}

/* XOR */
DECLARE_INSN(XOR)
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
DECLARE_INSN(XORI)
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
