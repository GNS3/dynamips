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
#include "ppc32_jit.h"
#include "ppc32_amd64_trans.h"
#include "memory.h"

/* Macros for CPU structure access */
#define REG_OFFSET(reg)   (OFFSET(cpu_ppc_t,gpr[(reg)]))
#define MEMOP_OFFSET(op)  (OFFSET(cpu_ppc_t,mem_op_fn[(op)]))

#define DECLARE_INSN(name) \
   static int ppc32_emit_##name(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b, \
                                ppc_insn_t insn)

/* Load a 32 bit immediate value */
static inline void ppc32_load_imm(ppc32_jit_tcb_t *b,u_int reg,m_uint32_t val)
{
   if (val)
      amd64_mov_reg_imm_size(b->jit_ptr,reg,val,4);
   else
      amd64_alu_reg_reg_size(b->jit_ptr,X86_XOR,reg,reg,4);
}

/* Set the Instruction Address (IA) register */
void ppc32_set_ia(ppc32_jit_tcb_t *b,m_uint32_t new_ia)
{
   amd64_mov_membase_imm(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,ia),new_ia,4);
}

/* Set the Link Register (LR) */
void ppc32_set_lr(ppc32_jit_tcb_t *b,m_uint32_t new_lr)
{  
   amd64_mov_membase_imm(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,lr),new_lr,4);
}

/* Set Jump */
static void ppc32_set_jump(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                           m_uint32_t new_ia,int local_jump)
{      
   int return_to_caller = FALSE;
   u_char *jump_ptr;

#if 0
   if (cpu->sym_trace && !local_jump)
      return_to_caller = TRUE;
#endif

   if (!return_to_caller && ppc32_jit_tcb_local_addr(b,new_ia,&jump_ptr)) {
      if (jump_ptr) {
         amd64_jump_code(b->jit_ptr,jump_ptr);
      } else {
         ppc32_jit_tcb_record_patch(b,b->jit_ptr,new_ia);
         amd64_jump32(b->jit_ptr,0);
      }
   } else {
      /* save PC */
      ppc32_set_ia(b,new_ia);

      /* address is in another block, for now, returns to caller */
      ppc32_jit_tcb_push_epilog(b);
   }
}

/* Load the Condition Register (CR) into the specified host register */
static forced_inline void ppc32_load_cr(ppc32_jit_tcb_t *b,u_int host_reg)
{
   amd64_mov_reg_membase(b->jit_ptr,host_reg,AMD64_R15,OFFSET(cpu_ppc_t,cr),4);
}

/* Store the Condition Register (CR) from the specified host register */
static forced_inline void ppc32_store_cr(ppc32_jit_tcb_t *b,u_int host_reg)
{
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,cr),host_reg,4);
}

/* Load a GPR into the specified host register */
static forced_inline void ppc32_load_gpr(ppc32_jit_tcb_t *b,u_int host_reg,
                                         u_int ppc_reg)
{
   amd64_mov_reg_membase(b->jit_ptr,host_reg,AMD64_R15,REG_OFFSET(ppc_reg),4);
}

/* Store contents for a host register into a GPR register */
static forced_inline void ppc32_store_gpr(ppc32_jit_tcb_t *b,u_int ppc_reg,
                                          u_int host_reg)
{
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,REG_OFFSET(ppc_reg),host_reg,4);
}

/* Apply an ALU operation on a GPR register and a host register */
static forced_inline void ppc32_alu_gpr(ppc32_jit_tcb_t *b,u_int op,
                                        u_int host_reg,u_int ppc_reg)
{
   amd64_alu_reg_membase_size(b->jit_ptr,op,host_reg,
                              AMD64_R15,REG_OFFSET(ppc_reg),4);
}

/* 
 * Update CR from %eflags
 * %eax, %ecx, %edx, %esi are modified.
 */
#define PPC32_CR_LT_BIT  3
#define PPC32_CR_GT_BIT  2
#define PPC32_CR_EQ_BIT  1
#define PPC32_CR_SO_BIT  0

static void ppc32_update_cr(ppc32_jit_tcb_t *b,int field,int is_signed)
{
   m_uint32_t cr_mask;
   u_int cfb;

   cr_mask = 0xF0000000 >> (field << 2);
   cfb = 28 - (field << 2);

   amd64_set_reg(b->jit_ptr,X86_CC_LT,AMD64_RAX,is_signed);
   amd64_set_reg(b->jit_ptr,X86_CC_GT,AMD64_RCX,is_signed);
   amd64_set_reg(b->jit_ptr,X86_CC_Z,AMD64_RDX,is_signed);

   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RAX,(cfb + PPC32_CR_LT_BIT));
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RCX,(cfb + PPC32_CR_GT_BIT));
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RDX,(cfb + PPC32_CR_EQ_BIT));

   amd64_alu_reg_reg(b->jit_ptr,X86_OR,X86_EAX,X86_ECX);
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,X86_EAX,X86_EDX);

   /* Load Condition Register */
   ppc32_load_cr(b,AMD64_RDX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RDX,~cr_mask);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,cr_mask);
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RDX,AMD64_RAX);

   /* Check XER Summary of Overflow and report it */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,
                         AMD64_R15,OFFSET(cpu_ppc_t,xer),4);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,PPC32_XER_SO);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHR,AMD64_RCX,(field << 2) + 3);
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RDX,AMD64_RCX);

   /* Store modified CR */
   ppc32_store_cr(b,AMD64_RDX);
}

/* 
 * Update CR0 from %eflags
 * %eax, %ecx, %edx, %esi are modified.
 */
static void ppc32_update_cr0(ppc32_jit_tcb_t *b)
{
   ppc32_update_cr(b,0,TRUE);
}

/* Basic C call */
static forced_inline void ppc32_emit_basic_c_call(ppc32_jit_tcb_t *b,void *f)
{
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RCX,f);
   amd64_call_reg(b->jit_ptr,AMD64_RCX);
}

/* Emit a simple call to a C function without any parameter */
static void ppc32_emit_c_call(ppc32_jit_tcb_t *b,void *f)
{   
   ppc32_set_ia(b,b->start_ia+((b->ppc_trans_pos-1)<<2));
   ppc32_emit_basic_c_call(b,f);
}

/* Memory operation */
static void ppc32_emit_memop(ppc32_jit_tcb_t *b,int op,int base,int offset,
                             int target,int update)
{
   m_uint32_t val = sign_extend(offset,16);
   u_char *test1;

   /* Save PC for exception handling */
   ppc32_set_ia(b,b->start_ia+((b->ppc_trans_pos-1)<<2));

   /* RSI = sign-extended offset */
   ppc32_load_imm(b,AMD64_RSI,val);

   /* RSI = GPR[base] + sign-extended offset */
   if (update || (base != 0))
      ppc32_alu_gpr(b,X86_ADD,AMD64_RSI,base);

   if (update)
      amd64_mov_reg_reg(b->jit_ptr,AMD64_R14,AMD64_RSI,4);

   /* RDX = target register */
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RDX,target);
   
   /* RDI = CPU instance pointer */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);

   /* Call memory function */
   amd64_call_membase(b->jit_ptr,AMD64_R15,MEMOP_OFFSET(op));

   /* Exception ? */
   amd64_test_reg_reg_size(b->jit_ptr,AMD64_RAX,AMD64_RAX,4);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_Z, 0, 1);
   ppc32_jit_tcb_push_epilog(b);
   amd64_patch(test1,b->jit_ptr);

   if (update)
      ppc32_store_gpr(b,base,AMD64_R14);
}

/* Memory operation (indexed) */
static void ppc32_emit_memop_idx(ppc32_jit_tcb_t *b,int op,int ra,int rb,
                                 int target,int update)
{
   u_char *test1;

   /* Save PC for exception handling */
   ppc32_set_ia(b,b->start_ia+((b->ppc_trans_pos-1)<<2));

   /* RSI = $rb */
   ppc32_load_gpr(b,AMD64_RSI,rb);

   /* RSI = GPR[base] + sign-extended offset */
   if (update || (ra != 0))
      ppc32_alu_gpr(b,X86_ADD,AMD64_RSI,ra);

   if (update)
      amd64_mov_reg_reg(b->jit_ptr,AMD64_R14,AMD64_RSI,4);

   /* RDX = target register */
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RDX,target);
   
   /* RDI = CPU instance pointer */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);

   /* Call memory function */
   amd64_call_membase(b->jit_ptr,AMD64_R15,MEMOP_OFFSET(op));

   /* Exception ? */
   amd64_test_reg_reg_size(b->jit_ptr,AMD64_RAX,AMD64_RAX,4);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_Z, 0, 1);
   ppc32_jit_tcb_push_epilog(b);
   amd64_patch(test1,b->jit_ptr);

   if (update)
      ppc32_store_gpr(b,ra,AMD64_R14);
}

typedef void (*memop_fast_access)(ppc32_jit_tcb_t *b,int target);

/* Fast LBZ */
static void ppc32_memop_fast_lbz(ppc32_jit_tcb_t *b,int target)
{
   amd64_clear_reg(b->jit_ptr,AMD64_RCX);
   amd64_mov_reg_memindex(b->jit_ptr,AMD64_RCX,AMD64_RBX,0,AMD64_RSI,0,1);
   ppc32_store_gpr(b,target,AMD64_RCX);
}

/* Fast STB */
static void ppc32_memop_fast_stb(ppc32_jit_tcb_t *b,int target)
{
   ppc32_load_gpr(b,AMD64_RDX,target);
   amd64_mov_memindex_reg(b->jit_ptr,AMD64_RBX,0,AMD64_RSI,0,AMD64_RDX,1);
}

/* Fast LWZ */
static void ppc32_memop_fast_lwz(ppc32_jit_tcb_t *b,int target)
{
   amd64_mov_reg_memindex(b->jit_ptr,AMD64_RAX,AMD64_RBX,0,AMD64_RSI,0,4);
   amd64_bswap32(b->jit_ptr,AMD64_RAX);
   ppc32_store_gpr(b,target,AMD64_RAX);
}

/* Fast STW */
static void ppc32_memop_fast_stw(ppc32_jit_tcb_t *b,int target)
{
   ppc32_load_gpr(b,AMD64_RDX,target);
   amd64_bswap32(b->jit_ptr,AMD64_RDX);
   amd64_mov_memindex_reg(b->jit_ptr,AMD64_RBX,0,AMD64_RSI,0,AMD64_RDX,4);
}

/* Fast memory operation */
static void ppc32_emit_memop_fast(ppc32_jit_tcb_t *b,int write_op,
                                  int opcode,int base,int offset,int target,
                                  memop_fast_access op_handler)
{   
   m_uint32_t val = sign_extend(offset,16);
   u_char *test1,*test2,*p_exception,*p_exit;

   test2 = NULL;

   /* RSI = GPR[base] + sign-extended offset */
   ppc32_load_imm(b,AMD64_RSI,val);
   if (base != 0)
      ppc32_alu_gpr(b,X86_ADD,AMD64_RSI,base);

   /* RBX = mts32_entry index */
   amd64_mov_reg_reg_size(b->jit_ptr,X86_EBX,X86_ESI,4);
   amd64_shift_reg_imm_size(b->jit_ptr,X86_SHR,X86_EBX,MTS32_HASH_SHIFT,4);
   amd64_alu_reg_imm_size(b->jit_ptr,X86_AND,X86_EBX,MTS32_HASH_MASK,4);

   /* RCX = mts32 entry */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,
                         AMD64_R15,
                         OFFSET(cpu_ppc_t,mts_cache[PPC32_MTS_DCACHE]),8);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,5);  /* TO FIX */
   amd64_alu_reg_reg(b->jit_ptr,X86_ADD,AMD64_RCX,AMD64_RBX);

   /* Compare virtual page address (EAX = vpage) */
   amd64_mov_reg_reg(b->jit_ptr,X86_EAX,X86_ESI,4);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,X86_EAX,PPC32_MIN_PAGE_MASK);

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
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,X86_ESI,PPC32_MIN_PAGE_IMASK);
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

   /* Save IA for exception handling */
   ppc32_set_ia(b,b->start_ia+((b->ppc_trans_pos-1)<<2));

   /* RDX = target register */
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RDX,target);

   /* RDI = CPU instance */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);

   /* Call memory access function */
   amd64_call_membase(b->jit_ptr,AMD64_R15,MEMOP_OFFSET(opcode));

   /* Exception ? */
   amd64_test_reg_reg_size(b->jit_ptr,AMD64_RAX,AMD64_RAX,4);
   p_exception = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_Z, 0, 1);
   ppc32_jit_tcb_push_epilog(b);

   amd64_patch(p_exit,b->jit_ptr);
   amd64_patch(p_exception,b->jit_ptr);
}

/* Emit unhandled instruction code */
static int ppc32_emit_unknown(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                              ppc_insn_t opcode)
{
   u_char *test1;

#if 0      
   x86_mov_reg_imm(b->jit_ptr,X86_EAX,opcode);
   x86_alu_reg_imm(b->jit_ptr,X86_SUB,X86_ESP,4);
   x86_push_reg(b->jit_ptr,X86_EAX);
   x86_push_reg(b->jit_ptr,X86_EDI);
   ppc32_emit_c_call(b,ppc32_unknown_opcode);
   x86_alu_reg_imm(b->jit_ptr,X86_ADD,X86_ESP,12);
#endif

   /* Fallback to non-JIT mode */
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RDI,AMD64_R15,8);
   amd64_mov_reg_imm(b->jit_ptr,AMD64_RSI,opcode);

   ppc32_emit_c_call(b,ppc32_exec_single_insn_ext);
   amd64_test_reg_reg_size(b->jit_ptr,AMD64_RAX,AMD64_RAX,4);
   test1 = b->jit_ptr;
   amd64_branch8(b->jit_ptr, X86_CC_Z, 0, 1);
   ppc32_jit_tcb_push_epilog(b);

   amd64_patch(test1,b->jit_ptr);
   return(0);
}

/* Increment the number of executed instructions (performance debugging) */
void ppc32_inc_perf_counter(ppc32_jit_tcb_t *b)
{ 
   amd64_inc_membase(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,perf_counter));
}

/* ======================================================================== */

/* BLR - Branch to Link Register */
DECLARE_INSN(BLR)
{
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_R15,OFFSET(cpu_ppc_t,lr),4);
   amd64_mov_membase_reg(b->jit_ptr,
                         AMD64_R15,OFFSET(cpu_ppc_t,ia),AMD64_RDX,4);

   /* set the return address */
   if (insn & 1)
      ppc32_set_lr(b,b->start_ia + (b->ppc_trans_pos << 2));

   ppc32_jit_tcb_push_epilog(b);
   return(0);
}

/* BCTR - Branch to Count Register */
DECLARE_INSN(BCTR)
{
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_R15,OFFSET(cpu_ppc_t,ctr),4);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,ia),
                         AMD64_RDX,4);

   /* set the return address */
   if (insn & 1)
      ppc32_set_lr(b,b->start_ia + (b->ppc_trans_pos << 2));

   ppc32_jit_tcb_push_epilog(b);
   return(0);
}

/* MFLR - Move From Link Register */
DECLARE_INSN(MFLR)
{
   int rd = bits(insn,21,25);
   
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_R15,OFFSET(cpu_ppc_t,lr),4);
   ppc32_store_gpr(b,rd,X86_EDX);
   return(0);
}

/* MTLR - Move To Link Register */
DECLARE_INSN(MTLR)
{
   int rs = bits(insn,21,25);

   ppc32_load_gpr(b,X86_EDX,rs);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,lr),
                         AMD64_RDX,4);
   return(0);
}

/* MFCTR - Move From Counter Register */
DECLARE_INSN(MFCTR)
{
   int rd = bits(insn,21,25);
   
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_R15,OFFSET(cpu_ppc_t,ctr),4);
   ppc32_store_gpr(b,rd,AMD64_RDX);
   return(0);
}

/* MTCTR - Move To Counter Register */
DECLARE_INSN(MTCTR)
{
   int rs = bits(insn,21,25);

   ppc32_load_gpr(b,AMD64_RDX,rs);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,ctr),
                         AMD64_RDX,4);
   return(0);
}

/* MFTBU - Move from Time Base (Up) */
DECLARE_INSN(MFTBU)
{
   int rd = bits(insn,21,25);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_R15,OFFSET(cpu_ppc_t,tb)+4,4);
   ppc32_store_gpr(b,rd,AMD64_RDX);
   return(0);
}

#define PPC32_TB_INCREMENT  50

/* MFTBL - Move from Time Base (Lo) */
DECLARE_INSN(MFTBL)
{
   int rd = bits(insn,21,25);

   /* Increment the time base register */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_R15,OFFSET(cpu_ppc_t,tb),8);
   amd64_alu_reg_imm(b->jit_ptr,X86_ADD,AMD64_RDX,PPC32_TB_INCREMENT);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,tb),
                         AMD64_RDX,8);

   ppc32_store_gpr(b,rd,AMD64_RDX);
   return(0);
}

/* ADD */
DECLARE_INSN(ADD)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* $rd = $ra + $rb */
   ppc32_load_gpr(b,AMD64_RBX,ra);
   ppc32_alu_gpr(b,X86_ADD,AMD64_RBX,rb);
   ppc32_store_gpr(b,rd,AMD64_RBX);

   if (insn & 1)
      ppc32_update_cr0(b);
    
   return(0);
}

/* ADDC */
DECLARE_INSN(ADDC)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* $rd = $ra + $rb */
   ppc32_load_gpr(b,AMD64_RBX,ra);
   ppc32_alu_gpr(b,X86_ADD,AMD64_RBX,rb);
   ppc32_store_gpr(b,rd,AMD64_RBX);

   /* store the carry flag */
   amd64_set_reg(b->jit_ptr,X86_CC_C,AMD64_RAX,FALSE);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,0x1);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,xer_ca),
                         AMD64_RAX,4);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }
      
   return(0);
}

/* ADDE - Add Extended */
DECLARE_INSN(ADDE)
{   
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* $ra + carry */
   ppc32_load_gpr(b,AMD64_RSI,ra);
   amd64_alu_reg_membase_size(b->jit_ptr,X86_ADD,AMD64_RSI,
                              AMD64_R15,OFFSET(cpu_ppc_t,xer_ca),4);
   amd64_set_reg(b->jit_ptr,X86_CC_C,AMD64_RAX,FALSE);

   /* add $rb */
   ppc32_alu_gpr(b,X86_ADD,AMD64_RSI,rb);
   amd64_set_reg(b->jit_ptr,X86_CC_C,AMD64_RCX,FALSE);

   ppc32_store_gpr(b,rd,AMD64_RSI);

   /* store the carry flag */
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RAX,AMD64_RCX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,0x1);

   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,xer_ca),
                         AMD64_RAX,4);

   /* update cr0 */
   if (insn & 1) {
      x86_test_reg_reg(b->jit_ptr,AMD64_RSI,AMD64_RSI);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* ADDI - ADD Immediate */
DECLARE_INSN(ADDI)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint32_t tmp = sign_extend_32(imm,16);

   ppc32_load_imm(b,AMD64_RBX,tmp);

   if (ra != 0)
      amd64_alu_reg_membase_size(b->jit_ptr,X86_ADD,AMD64_RBX,
                                 AMD64_R15,REG_OFFSET(ra),4);

   ppc32_store_gpr(b,rd,AMD64_RBX);
   return(0);
}

/* ADDIC - ADD Immediate with Carry */
DECLARE_INSN(ADDIC)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint32_t tmp = sign_extend_32(imm,16);

   ppc32_load_imm(b,AMD64_RAX,tmp);
   ppc32_alu_gpr(b,X86_ADD,AMD64_RAX,ra);
   ppc32_store_gpr(b,rd,AMD64_RAX);
   amd64_set_membase_size(b->jit_ptr,X86_CC_C,
                          AMD64_R15,OFFSET(cpu_ppc_t,xer_ca),
                          FALSE,4);
   return(0);
}

/* ADDIC. */
DECLARE_INSN(ADDIC_dot)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint32_t tmp = sign_extend_32(imm,16);

   ppc32_load_imm(b,AMD64_RAX,tmp);
   ppc32_alu_gpr(b,X86_ADD,AMD64_RAX,ra);
   ppc32_store_gpr(b,rd,AMD64_RAX);
   amd64_set_membase_size(b->jit_ptr,X86_CC_C,
                          AMD64_R15,OFFSET(cpu_ppc_t,xer_ca),
                          FALSE,4);

   amd64_test_reg_reg_size(b->jit_ptr,AMD64_RAX,AMD64_RAX,4);
   ppc32_update_cr0(b);
   return(0);
}

/* ADDIS - ADD Immediate Shifted */
DECLARE_INSN(ADDIS)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);

   ppc32_load_imm(b,AMD64_RBX,imm << 16);

   if (ra != 0)
      amd64_alu_reg_membase_size(b->jit_ptr,X86_ADD,AMD64_RBX,
                                 AMD64_R15,REG_OFFSET(ra),4);

   ppc32_store_gpr(b,rd,AMD64_RBX);
   return(0);
}

/* AND */
DECLARE_INSN(AND)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_load_gpr(b,AMD64_RBX,rs);
   ppc32_alu_gpr(b,X86_AND,AMD64_RBX,rb);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1)
      ppc32_update_cr0(b);

   return(0);
}

/* ANDC */
DECLARE_INSN(ANDC)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* $ra = $rs & ~$rb */
   ppc32_load_gpr(b,AMD64_RBX,rb);
   x86_not_reg(b->jit_ptr,AMD64_RBX);
   ppc32_alu_gpr(b,X86_AND,AMD64_RBX,rs);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1)
      ppc32_update_cr0(b);

   return(0);
}

/* AND Immediate */
DECLARE_INSN(ANDI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);

   /* $ra = $rs & imm */
   ppc32_load_imm(b,AMD64_RBX,imm);
   ppc32_alu_gpr(b,X86_AND,AMD64_RBX,rs);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   ppc32_update_cr0(b);
   return(0);
}

/* AND Immediate Shifted */
DECLARE_INSN(ANDIS)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);

   /* $ra = $rs & imm */
   ppc32_load_imm(b,AMD64_RBX,imm << 16);
   ppc32_alu_gpr(b,X86_AND,AMD64_RBX,rs);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   ppc32_update_cr0(b);
   return(0);
}

/* B - Branch */
DECLARE_INSN(B)
{
   m_uint32_t offset = bits(insn,2,25);
   m_uint64_t new_ia;

   /* compute the new ia */
   new_ia = b->start_ia + ((b->ppc_trans_pos-1) << 2);
   new_ia += sign_extend(offset << 2,26);
   ppc32_set_jump(cpu,b,new_ia,1);
   return(0);
}

/* BA - Branch Absolute */
DECLARE_INSN(BA)
{
   m_uint32_t offset = bits(insn,2,25);
   m_uint64_t new_ia;

   /* compute the new ia */
   new_ia = sign_extend(offset << 2,26);
   ppc32_set_jump(cpu,b,new_ia,1);
   return(0);
}

/* BL - Branch and Link */
DECLARE_INSN(BL)
{
   m_uint32_t offset = bits(insn,2,25);
   m_uint64_t new_ia;

   /* compute the new ia */
   new_ia = b->start_ia + ((b->ppc_trans_pos-1) << 2);
   new_ia += sign_extend(offset << 2,26);

   /* set the return address */
   ppc32_set_lr(b,b->start_ia + (b->ppc_trans_pos << 2));

   ppc32_set_jump(cpu,b,new_ia,1);
   return(0);
}

/* BLA - Branch and Link Absolute */
DECLARE_INSN(BLA)
{
   m_uint32_t offset = bits(insn,2,25);
   m_uint64_t new_ia;

   /* compute the new ia */
   new_ia = sign_extend(offset << 2,26);

   /* set the return address */
   ppc32_set_lr(b,b->start_ia + (b->ppc_trans_pos << 2));

   ppc32_set_jump(cpu,b,new_ia,1);
   return(0);
}

/* BC - Branch Conditional (Condition Check only) */
DECLARE_INSN(BCC)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   int bd = bits(insn,2,15);
   m_uint32_t new_ia;
   u_char *jump_ptr;
   int local_jump;
   int cond;

   /* Get the wanted value for the condition bit */
   cond = (bo >> 3) & 0x1;

   /* Set the return address */
   if (insn & 1)
      ppc32_set_lr(b,b->start_ia + (b->ppc_trans_pos << 2));

   /* Compute the new ia */
   new_ia = sign_extend_32(bd << 2,16);
   if (!(insn & 0x02))
      new_ia += b->start_ia + ((b->ppc_trans_pos-1) << 2);

   /* Test the condition bit */
   amd64_test_membase_imm_size(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,cr),
                               (1 << (31 - bi)),4);

   local_jump = ppc32_jit_tcb_local_addr(b,new_ia,&jump_ptr);

   /* 
    * Optimize the jump, depending if the destination is in the same 
    * page or not.
    */
   if (local_jump) {
      if (jump_ptr) {
         amd64_branch(b->jit_ptr,(cond) ? X86_CC_NZ : X86_CC_Z,jump_ptr,FALSE);
      } else {
         ppc32_jit_tcb_record_patch(b,b->jit_ptr,new_ia);
         amd64_branch32(b->jit_ptr,(cond) ? X86_CC_NZ : X86_CC_Z,0,FALSE);
      }
   } else {   
      jump_ptr = b->jit_ptr;
      amd64_branch32(b->jit_ptr,(cond) ? X86_CC_Z : X86_CC_NZ,0,FALSE);
      ppc32_set_jump(cpu,b,new_ia,TRUE);
      amd64_patch(jump_ptr,b->jit_ptr);
   }

   return(0);
}

/* BC - Branch Conditional */
DECLARE_INSN(BC)
{   
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   int bd = bits(insn,2,15);
   m_uint32_t new_ia;
   u_char *jump_ptr;
   int local_jump;
   int cond,ctr;

   /* Get the wanted value for the condition bit and CTR value */
   cond = (bo >> 3) & 0x1;
   ctr  = (bo >> 1) & 0x1;

   /* Set the return address */
   if (insn & 1)
      ppc32_set_lr(b,b->start_ia + (b->ppc_trans_pos << 2));

   /* Compute the new ia */
   new_ia = sign_extend_32(bd << 2,16);
   if (!(insn & 0x02))
      new_ia += b->start_ia + ((b->ppc_trans_pos-1) << 2);

   ppc32_load_imm(b,AMD64_RAX,1);

   /* Decrement the count register */
   if (!(bo & 0x04)) {
      amd64_dec_membase_size(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,ctr),4);
      amd64_set_reg(b->jit_ptr,(ctr) ? X86_CC_Z : X86_CC_NZ,AMD64_RBX,FALSE);
      amd64_alu_reg_reg(b->jit_ptr,X86_AND,AMD64_RAX,AMD64_RBX);
   }

   /* Test the condition bit */
   if (!((bo >> 4) & 0x01)) {
      amd64_test_membase_imm_size(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,cr),
                                  (1 << (31 - bi)),4);
      amd64_set_reg(b->jit_ptr,(cond) ? X86_CC_NZ : X86_CC_Z,AMD64_RCX,FALSE);
      amd64_alu_reg_reg(b->jit_ptr,X86_AND,AMD64_RAX,AMD64_RCX);
   }

   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,0x01);

   local_jump = ppc32_jit_tcb_local_addr(b,new_ia,&jump_ptr);

   /* 
    * Optimize the jump, depending if the destination is in the same 
    * page or not.
    */
   if (local_jump) {
      if (jump_ptr) {
         amd64_branch(b->jit_ptr,X86_CC_NZ,jump_ptr,FALSE);
      } else {
         ppc32_jit_tcb_record_patch(b,b->jit_ptr,new_ia);
         amd64_branch32(b->jit_ptr,X86_CC_NZ,0,FALSE);
      }
   } else {   
      jump_ptr = b->jit_ptr;
      amd64_branch32(b->jit_ptr,X86_CC_Z,0,FALSE);
      ppc32_set_jump(cpu,b,new_ia,TRUE);
      amd64_patch(jump_ptr,b->jit_ptr);
   }

   return(0);
}

/* BCLR - Branch Conditional to Link register */
DECLARE_INSN(BCLR)
{   
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   int bd = bits(insn,2,15);
   m_uint32_t new_ia;
   u_char *jump_ptr;
   int cond,ctr;

   /* Get the wanted value for the condition bit and CTR value */
   cond = (bo >> 3) & 0x1;
   ctr  = (bo >> 1) & 0x1;

   /* Compute the new ia */
   new_ia = sign_extend_32(bd << 2,16);
   if (!(insn & 0x02))
      new_ia += b->start_ia + ((b->ppc_trans_pos-1) << 2);

   ppc32_load_imm(b,AMD64_RAX,1);

   /* Decrement the count register */
   if (!(bo & 0x04)) {
      amd64_dec_membase_size(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,ctr),4);
      amd64_set_reg(b->jit_ptr,(ctr) ? X86_CC_Z : X86_CC_NZ,AMD64_RBX,FALSE);
      amd64_alu_reg_reg(b->jit_ptr,X86_AND,AMD64_RAX,AMD64_RBX);
   }

   /* Test the condition bit */
   if (!((bo >> 4) & 0x01)) {
      amd64_test_membase_imm_size(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,cr),
                                  (1 << (31 - bi)),4);
      amd64_set_reg(b->jit_ptr,(cond) ? X86_CC_NZ : X86_CC_Z,AMD64_RCX,FALSE);
      amd64_alu_reg_reg(b->jit_ptr,X86_AND,AMD64_RAX,AMD64_RCX);
   }

   /* Set the return address */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RDX,
                         AMD64_R15,OFFSET(cpu_ppc_t,lr),4);

   if (insn & 1)
      ppc32_set_lr(b,b->start_ia + (b->ppc_trans_pos << 2));

   /* Branching */
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,0x01);

   jump_ptr = b->jit_ptr;
   amd64_branch32(b->jit_ptr,X86_CC_Z,0,FALSE);

   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RDX,0xFFFFFFFC);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,ia),
                         AMD64_RDX,4);
   ppc32_jit_tcb_push_epilog(b);

   amd64_patch(jump_ptr,b->jit_ptr);
   return(0);
}

/* CMP - Compare */
DECLARE_INSN(CMP)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_load_gpr(b,AMD64_RBX,ra);
   ppc32_alu_gpr(b,X86_CMP,AMD64_RBX,rb);
   ppc32_update_cr(b,rd,TRUE);
   return(0);
}

/* CMPI - Compare Immediate */
DECLARE_INSN(CMPI)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t tmp = sign_extend_32(imm,16);

   ppc32_load_imm(b,AMD64_RBX,tmp);
   ppc32_load_gpr(b,AMD64_RSI,ra);
   amd64_alu_reg_reg_size(b->jit_ptr,X86_CMP,AMD64_RSI,AMD64_RBX,4);

   ppc32_update_cr(b,rd,TRUE);
   return(0);
}

/* CMPL - Compare Logical */
DECLARE_INSN(CMPL)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_load_gpr(b,AMD64_RAX,ra);
   ppc32_alu_gpr(b,X86_CMP,AMD64_RAX,rb);
   ppc32_update_cr(b,rd,FALSE);
   return(0);
}

/* CMPLI - Compare Immediate */
DECLARE_INSN(CMPLI)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);

   ppc32_load_imm(b,AMD64_RBX,imm);
   ppc32_load_gpr(b,AMD64_RSI,ra);
   amd64_alu_reg_reg_size(b->jit_ptr,X86_CMP,AMD64_RSI,AMD64_RBX,4);

   ppc32_update_cr(b,rd,FALSE);
   return(0);
}

/* CRAND - Condition Register AND */
DECLARE_INSN(CRAND)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);

   ppc32_load_cr(b,AMD64_RSI);

   /* test $ba bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - ba)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RAX,FALSE);

   /* test $bb bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - bb)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RBX,FALSE);
   
   /* result of AND between $ba and $bb */
   amd64_alu_reg_reg(b->jit_ptr,X86_AND,AMD64_RBX,AMD64_RAX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,0x01);
   
   /* set/clear $bd bit depending on the result */
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RSI,~(1 << (31 - bd)));
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,(31 - bd));
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RSI,AMD64_RBX);

   ppc32_store_cr(b,AMD64_RSI);
   return(0);
}

/* CRANDC - Condition Register AND with Complement */
DECLARE_INSN(CRANDC)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);

   ppc32_load_cr(b,AMD64_RSI);

   /* test $ba bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - ba)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RAX,FALSE);

   /* test $bb bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - bb)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_Z,AMD64_RBX,FALSE);
   
   /* result of AND between $ba and $bb */
   amd64_alu_reg_reg(b->jit_ptr,X86_AND,AMD64_RBX,AMD64_RAX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,0x01);
   
   /* set/clear $bd bit depending on the result */
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RSI,~(1 << (31 - bd)));
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,(31 - bd));
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RSI,AMD64_RBX);

   ppc32_store_cr(b,AMD64_RSI);
   return(0);
}

/* CREQV - Condition Register EQV */
DECLARE_INSN(CREQV)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);

   ppc32_load_cr(b,AMD64_RSI);

   /* test $ba bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - ba)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RAX,FALSE);

   /* test $bb bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - bb)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RBX,FALSE);
   
   /* result of XOR between $ba and $bb */
   amd64_alu_reg_reg(b->jit_ptr,X86_XOR,AMD64_RBX,AMD64_RAX);
   amd64_not_reg(b->jit_ptr,AMD64_RBX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,0x01);
   
   /* set/clear $bd bit depending on the result */
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RSI,~(1 << (31 - bd)));
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,(31 - bd));
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RSI,AMD64_RBX);

   ppc32_store_cr(b,AMD64_RSI);
   return(0);
}

/* CRNAND - Condition Register NAND */
DECLARE_INSN(CRNAND)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);

   ppc32_load_cr(b,AMD64_RSI);

   /* test $ba bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - ba)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RAX,FALSE);

   /* test $bb bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - bb)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RBX,FALSE);
   
   /* result of NAND between $ba and $bb */
   amd64_alu_reg_reg(b->jit_ptr,X86_AND,AMD64_RBX,AMD64_RAX);
   amd64_not_reg(b->jit_ptr,AMD64_RBX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,0x01);
   
   /* set/clear $bd bit depending on the result */
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RSI,~(1 << (31 - bd)));
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,(31 - bd));
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RSI,AMD64_RBX);

   ppc32_store_cr(b,AMD64_RSI);
   return(0);
}

/* CRNOR - Condition Register NOR */
DECLARE_INSN(CRNOR)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);

   ppc32_load_cr(b,AMD64_RSI);

   /* test $ba bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - ba)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RAX,FALSE);

   /* test $bb bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - bb)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RBX,FALSE);
   
   /* result of NOR between $ba and $bb */
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RBX,AMD64_RAX);
   amd64_not_reg(b->jit_ptr,AMD64_RBX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,0x01);
   
   /* set/clear $bd bit depending on the result */
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RSI,~(1 << (31 - bd)));
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,(31 - bd));
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RSI,AMD64_RBX);

   ppc32_store_cr(b,AMD64_RSI);
   return(0);
}

/* CROR - Condition Register OR */
DECLARE_INSN(CROR)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);

   ppc32_load_cr(b,AMD64_RSI);

   /* test $ba bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - ba)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RAX,FALSE);

   /* test $bb bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - bb)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RBX,FALSE);
   
   /* result of NOR between $ba and $bb */
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RBX,AMD64_RAX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,0x01);
   
   /* set/clear $bd bit depending on the result */
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RSI,~(1 << (31 - bd)));
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,(31 - bd));
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RSI,AMD64_RBX);

   ppc32_store_cr(b,AMD64_RSI);
   return(0);
}

/* CRORC - Condition Register OR with Complement */
DECLARE_INSN(CRORC)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);

   ppc32_load_cr(b,AMD64_RSI);

   /* test $ba bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - ba)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RAX,FALSE);

   /* test $bb bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - bb)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_Z,AMD64_RBX,FALSE);
   
   /* result of ORC between $ba and $bb */
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RBX,AMD64_RAX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,0x01);
   
   /* set/clear $bd bit depending on the result */
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RSI,~(1 << (31 - bd)));
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,(31 - bd));
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RSI,AMD64_RBX);

   ppc32_store_cr(b,AMD64_RSI);
   return(0);
}

/* CRXOR - Condition Register XOR */
DECLARE_INSN(CRXOR)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);

   ppc32_load_cr(b,AMD64_RSI);

   /* test $ba bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - ba)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RAX,FALSE);

   /* test $bb bit */
   amd64_test_reg_imm_size(b->jit_ptr,AMD64_RSI,(1 << (31 - bb)),4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RBX,FALSE);
   
   /* result of XOR between $ba and $bb */
   amd64_alu_reg_reg(b->jit_ptr,X86_XOR,AMD64_RBX,AMD64_RAX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,0x01);
   
   /* set/clear $bd bit depending on the result */
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RSI,~(1 << (31 - bd)));
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,(31 - bd));
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RSI,AMD64_RBX);

   ppc32_store_cr(b,AMD64_RSI);
   return(0);
}

/* DIVWU - Divide Word Unsigned */
DECLARE_INSN(DIVWU)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_load_gpr(b,AMD64_RAX,ra);
   ppc32_load_gpr(b,AMD64_RBX,rb);
   ppc32_load_imm(b,AMD64_RDX,0);

   amd64_div_reg_size(b->jit_ptr,AMD64_RBX,0,4);
   ppc32_store_gpr(b,rd,AMD64_RAX);

   if (insn & 1) {
      amd64_test_reg_reg(b->jit_ptr,AMD64_RAX,AMD64_RAX);
      ppc32_update_cr0(b);
   }
   
   return(0);
}

/* EQV */
DECLARE_INSN(EQV)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* $ra = ~($rs ^ $rb) */
   ppc32_load_gpr(b,AMD64_RBX,rs);
   ppc32_alu_gpr(b,X86_XOR,AMD64_RBX,rb);
   amd64_not_reg(b->jit_ptr,AMD64_RBX);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* EXTSB - Extend Sign Byte */
DECLARE_INSN(EXTSB)
{   
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);

   ppc32_load_gpr(b,AMD64_RBX,rs);
   amd64_shift_reg_imm_size(b->jit_ptr,X86_SHL,AMD64_RBX,24,4);
   amd64_shift_reg_imm_size(b->jit_ptr,X86_SAR,AMD64_RBX,24,4);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* EXTSH - Extend Sign Word */
DECLARE_INSN(EXTSH)
{   
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);

   ppc32_load_gpr(b,AMD64_RBX,rs);
   amd64_shift_reg_imm_size(b->jit_ptr,X86_SHL,AMD64_RBX,16,4);
   amd64_shift_reg_imm_size(b->jit_ptr,X86_SAR,AMD64_RBX,16,4);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* LBZ - Load Byte and Zero */
DECLARE_INSN(LBZ)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   //ppc32_emit_memop(b,PPC_MEMOP_LBZ,ra,offset,rs,0);
   ppc32_emit_memop_fast(b,0,PPC_MEMOP_LBZ,ra,offset,rs,ppc32_memop_fast_lbz);
   return(0);
}

/* LBZU - Load Byte and Zero with Update */
DECLARE_INSN(LBZU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop(b,PPC_MEMOP_LBZ,ra,offset,rs,1);
   return(0);
}

/* LBZUX - Load Byte and Zero with Update Indexed */
DECLARE_INSN(LBZUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_LBZ,ra,rb,rs,1);
   return(0);
}

/* LBZX - Load Byte and Zero Indexed */
DECLARE_INSN(LBZX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_LBZ,ra,rb,rs,0);
   return(0);
}

/* LHA - Load Half-Word Algebraic */
DECLARE_INSN(LHA)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop(b,PPC_MEMOP_LHA,ra,offset,rs,0);
   return(0);
}

/* LHAU - Load Half-Word Algebraic with Update */
DECLARE_INSN(LHAU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop(b,PPC_MEMOP_LHA,ra,offset,rs,1);
   return(0);
}

/* LHAUX - Load Half-Word Algebraic with Update Indexed */
DECLARE_INSN(LHAUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_LHA,ra,rb,rs,1);
   return(0);
}

/* LHAX - Load Half-Word Algebraic Indexed */
DECLARE_INSN(LHAX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_LHA,ra,rb,rs,0);
   return(0);
}

/* LHZ - Load Half-Word and Zero */
DECLARE_INSN(LHZ)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop(b,PPC_MEMOP_LHZ,ra,offset,rs,0);
   return(0);
}

/* LHZU - Load Half-Word and Zero with Update */
DECLARE_INSN(LHZU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop(b,PPC_MEMOP_LHZ,ra,offset,rs,1);
   return(0);
}

/* LHZUX - Load Half-Word and Zero with Update Indexed */
DECLARE_INSN(LHZUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_LHZ,ra,rb,rs,1);
   return(0);
}

/* LHZX - Load Half-Word and Zero Indexed */
DECLARE_INSN(LHZX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_LHZ,ra,rb,rs,0);
   return(0);
}

/* LWZ - Load Word and Zero */
DECLARE_INSN(LWZ)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   //ppc32_emit_memop(b,PPC_MEMOP_LWZ,ra,offset,rs,0);
   ppc32_emit_memop_fast(b,0,PPC_MEMOP_LWZ,ra,offset,rs,ppc32_memop_fast_lwz);
   return(0);
}

/* LWZU - Load Word and Zero with Update */
DECLARE_INSN(LWZU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop(b,PPC_MEMOP_LWZ,ra,offset,rs,1);
   return(0);
}

/* LWZUX - Load Word and Zero with Update Indexed */
DECLARE_INSN(LWZUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_LWZ,ra,rb,rs,1);
   return(0);
}

/* LWZX - Load Word and Zero Indexed */
DECLARE_INSN(LWZX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_LWZ,ra,rb,rs,0);
   return(0);
}

/* MCRF - Move Condition Register Field */
DECLARE_INSN(MCRF)
{
   int rd = bits(insn,23,25);
   int rs = bits(insn,18,20);
   m_uint32_t dmask;

   /* %rax = %rbx = CR */
   ppc32_load_cr(b,AMD64_RAX);
   amd64_mov_reg_reg(b->jit_ptr,X86_EBX,X86_EAX,8);

   amd64_shift_reg_imm(b->jit_ptr,X86_SHR,AMD64_RBX,(28 - (rs << 2)));
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,0x0F);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHL,AMD64_RBX,(28 - (rd << 2)));

   /* clear the destination bits */
   dmask = (0xF0000000 >> (rd << 2));
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,~dmask);

   /* set the new field value */
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RAX,AMD64_RBX);
   ppc32_store_cr(b,AMD64_RAX);
   return(0);
}

/* MFCR - Move from Condition Register */
DECLARE_INSN(MFCR)
{
   int rd = bits(insn,21,25);

   ppc32_load_cr(b,AMD64_RAX);
   ppc32_store_gpr(b,rd,AMD64_RAX);
   return(0);
}

/* MFMSR - Move from Machine State Register */
DECLARE_INSN(MFMSR)
{
   int rd = bits(insn,21,25);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,
                         AMD64_R15,OFFSET(cpu_ppc_t,msr),4);
   ppc32_store_gpr(b,rd,AMD64_RAX);
   return(0);
}

/* MFSR - Move From Segment Register */
DECLARE_INSN(MFSR)
{
   int rd = bits(insn,21,25);
   int sr = bits(insn,16,19);

   amd64_mov_reg_membase(b->jit_ptr,AMD64_RAX,
                         AMD64_R15,(OFFSET(cpu_ppc_t,sr) + (sr << 2)),4);
   ppc32_store_gpr(b,rd,AMD64_RAX);
   return(0);
}

/* MTCRF - Move to Condition Register Fields */
DECLARE_INSN(MTCRF)
{
   int rs = bits(insn,21,25);
   int crm = bits(insn,12,19);
   m_uint32_t mask = 0;
   int i;

   for(i=0;i<8;i++)
      if (crm & (1 << i))
         mask |= 0xF << (i << 2);

   ppc32_load_cr(b,AMD64_RAX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,~mask);

   ppc32_load_gpr(b,AMD64_RDX,rs);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RDX,mask);

   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RDX,AMD64_RAX);
   ppc32_store_cr(b,AMD64_RDX);
   return(0);
}

/* MULHW - Multiply High Word */
DECLARE_INSN(MULHW)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_load_gpr(b,AMD64_RAX,ra);
   ppc32_load_gpr(b,AMD64_RBX,rb);
   amd64_mul_reg_size(b->jit_ptr,AMD64_RBX,1,4);
   ppc32_store_gpr(b,rd,AMD64_RDX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RDX,AMD64_RDX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* MULHWU - Multiply High Word Unsigned */
DECLARE_INSN(MULHWU)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_load_gpr(b,AMD64_RAX,ra);
   ppc32_load_gpr(b,AMD64_RBX,rb);
   amd64_mul_reg_size(b->jit_ptr,AMD64_RBX,0,4);
   ppc32_store_gpr(b,rd,AMD64_RDX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RDX,AMD64_RDX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* MULLI - Multiply Low Immediate */
DECLARE_INSN(MULLI)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);

   ppc32_load_gpr(b,AMD64_RAX,ra);
   ppc32_load_imm(b,AMD64_RBX,sign_extend_32(imm,16));

   amd64_mul_reg_size(b->jit_ptr,AMD64_RBX,1,4);
   ppc32_store_gpr(b,rd,X86_EAX);
   return(0);
}

/* MULLW - Multiply Low Word */
DECLARE_INSN(MULLW)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_load_gpr(b,AMD64_RAX,ra);
   ppc32_load_gpr(b,AMD64_RBX,rb);
   amd64_mul_reg_size(b->jit_ptr,AMD64_RBX,1,4);
   ppc32_store_gpr(b,rd,AMD64_RAX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RAX,AMD64_RAX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* NAND */
DECLARE_INSN(NAND)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* $ra = ~($rs & $rb) */
   ppc32_load_gpr(b,AMD64_RBX,rs);
   ppc32_alu_gpr(b,X86_AND,AMD64_RBX,rb);
   amd64_not_reg(b->jit_ptr,AMD64_RBX);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* NEG */
DECLARE_INSN(NEG)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);

   ppc32_load_gpr(b,AMD64_RBX,ra);
   amd64_neg_reg(b->jit_ptr,AMD64_RBX);
   ppc32_store_gpr(b,rd,AMD64_RBX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* NOR */
DECLARE_INSN(NOR)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* $ra = ~($rs | $rb) */
   ppc32_load_gpr(b,AMD64_RBX,rs);
   ppc32_alu_gpr(b,X86_OR,AMD64_RBX,rb);
   amd64_not_reg(b->jit_ptr,AMD64_RBX);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* OR */
DECLARE_INSN(OR)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_load_gpr(b,AMD64_RCX,rs);

   if (rs != rb)
      ppc32_alu_gpr(b,X86_OR,AMD64_RCX,rb);

   ppc32_store_gpr(b,ra,AMD64_RCX);

   if (insn & 1) {
      if (rs == rb)
         amd64_test_reg_reg_size(b->jit_ptr,AMD64_RCX,AMD64_RCX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* OR with Complement */
DECLARE_INSN(ORC)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* $ra = $rs | ~$rb */
   ppc32_load_gpr(b,AMD64_RBX,rb);
   amd64_not_reg(b->jit_ptr,AMD64_RBX);
   ppc32_alu_gpr(b,X86_OR,AMD64_RBX,rs);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1)
      ppc32_update_cr0(b);

   return(0);
}

/* OR Immediate */
DECLARE_INSN(ORI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);

   /* $ra = $rs | imm */
   ppc32_load_imm(b,AMD64_RBX,imm);
   ppc32_alu_gpr(b,X86_OR,AMD64_RBX,rs);
   ppc32_store_gpr(b,ra,AMD64_RBX);
   return(0);
}

/* OR Immediate Shifted */
DECLARE_INSN(ORIS)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);

   /* $ra = $rs | (imm << 16) */
   ppc32_load_imm(b,AMD64_RBX,imm << 16);
   ppc32_alu_gpr(b,X86_OR,AMD64_RBX,rs);
   ppc32_store_gpr(b,ra,AMD64_RBX);
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
   register m_uint32_t mask;
 
   mask = ppc32_rotate_mask(mb,me);

   /* Apply inverse mask to %eax "ra" */
   ppc32_load_gpr(b,AMD64_RAX,ra);
   if (mask != 0)
      amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,~mask);

   /* Rotate %ebx ("rs") of "sh" bits and apply the mask */
   ppc32_load_gpr(b,AMD64_RBX,rs);

   if (sh != 0)
      amd64_shift_reg_imm_size(b->jit_ptr,X86_ROL,AMD64_RBX,sh,4);

   if (mask != 0xFFFFFFFF)
      amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,mask);

   /* Store the result */
   amd64_alu_reg_reg_size(b->jit_ptr,X86_OR,AMD64_RBX,AMD64_RAX,4);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1)
      ppc32_update_cr0(b);

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
   register m_uint32_t mask;

   mask = ppc32_rotate_mask(mb,me);

   /* Rotate %ebx ("rs") of "sh" bits and apply the mask */
   ppc32_load_gpr(b,AMD64_RBX,rs);

   if (sh != 0)
      amd64_shift_reg_imm_size(b->jit_ptr,X86_ROL,AMD64_RBX,sh,4);

   if (mask != 0xFFFFFFFF)
      amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,mask);

   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }

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
   register m_uint32_t mask;

   mask = ppc32_rotate_mask(mb,me);

   /* Load the shift register ("sh") */
   ppc32_load_gpr(b,AMD64_RCX,rb);

   /* Rotate %ebx ("rs") and apply the mask */
   ppc32_load_gpr(b,AMD64_RBX,rs);
   amd64_shift_reg_size(b->jit_ptr,X86_ROL,AMD64_RBX,4);

   if (mask != 0xFFFFFFFF)
      amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RBX,mask);

   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* Shift Left Word */
DECLARE_INSN(SLW)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* If count >= 32, then null result */
   ppc32_load_gpr(b,AMD64_RCX,rb);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,0x3f);

   ppc32_load_gpr(b,AMD64_RBX,rs);
   amd64_shift_reg(b->jit_ptr,X86_SHL,AMD64_RBX);

   /* Store the result */
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* SRAWI - Shift Right Algebraic Word Immediate */
DECLARE_INSN(SRAWI)
{   
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int sh = bits(insn,11,15);
   register m_uint32_t mask;

   mask = ~(0xFFFFFFFFU << sh);

   /* $ra = (int32)$rs >> sh */
   ppc32_load_gpr(b,AMD64_RBX,rs);
   amd64_mov_reg_reg(b->jit_ptr,AMD64_RSI,AMD64_RBX,4);
   amd64_shift_reg_imm_size(b->jit_ptr,X86_SAR,AMD64_RBX,sh,4);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   /* test the sign-bit of gpr[rs] */
   amd64_test_reg_reg_size(b->jit_ptr,AMD64_RSI,AMD64_RSI,4);
   amd64_set_reg(b->jit_ptr,X86_CC_LT,AMD64_RAX,TRUE);

   amd64_alu_reg_imm_size(b->jit_ptr,X86_AND,AMD64_RSI,mask,4);
   amd64_set_reg(b->jit_ptr,X86_CC_NZ,AMD64_RCX,TRUE);
   
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RCX,AMD64_RAX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,0x1);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,xer_ca),
                         AMD64_RCX,4);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* Shift Right Word */
DECLARE_INSN(SRW)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* If count >= 32, then null result */
   ppc32_load_gpr(b,AMD64_RCX,rb);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,0x3f);

   ppc32_load_gpr(b,AMD64_RBX,rs);
   amd64_shift_reg(b->jit_ptr,X86_SHR,AMD64_RBX);

   /* Store the result */
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RBX,AMD64_RBX,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* STB - Store Byte */
DECLARE_INSN(STB)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   //ppc32_emit_memop(b,PPC_MEMOP_STB,ra,offset,rs,0);
   ppc32_emit_memop_fast(b,1,PPC_MEMOP_STB,ra,offset,rs,ppc32_memop_fast_stb);
   return(0);
}

/* STBU - Store Byte with Update */
DECLARE_INSN(STBU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop(b,PPC_MEMOP_STB,ra,offset,rs,1);
   return(0);
}

/* STBUX - Store Byte with Update Indexed */
DECLARE_INSN(STBUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_STB,ra,rb,rs,1);
   return(0);
}

/* STBUX - Store Byte Indexed */
DECLARE_INSN(STBX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_STB,ra,rb,rs,0);
   return(0);
}

/* STH - Store Half-Word */
DECLARE_INSN(STH)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop(b,PPC_MEMOP_STH,ra,offset,rs,0);
   return(0);
}

/* STHU - Store Half-Word with Update */
DECLARE_INSN(STHU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop(b,PPC_MEMOP_STH,ra,offset,rs,1);
   return(0);
}

/* STHUX - Store Half-Word with Update Indexed */
DECLARE_INSN(STHUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_STH,ra,rb,rs,1);
   return(0);
}

/* STHUX - Store Half-Word Indexed */
DECLARE_INSN(STHX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_STH,ra,rb,rs,0);
   return(0);
}

/* STW - Store Word */
DECLARE_INSN(STW)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   //ppc32_emit_memop(b,PPC_MEMOP_STW,ra,offset,rs,0);
   ppc32_emit_memop_fast(b,1,PPC_MEMOP_STW,ra,offset,rs,ppc32_memop_fast_stw);
   return(0);
}

/* STWU - Store Word with Update */
DECLARE_INSN(STWU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop(b,PPC_MEMOP_STW,ra,offset,rs,1);
   return(0);
}

/* STWUX - Store Word with Update Indexed */
DECLARE_INSN(STWUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_STW,ra,rb,rs,1);
   return(0);
}

/* STWUX - Store Word Indexed */
DECLARE_INSN(STWX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_idx(b,PPC_MEMOP_STW,ra,rb,rs,0);
   return(0);
}

/* SUBF - Subtract From */
DECLARE_INSN(SUBF)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* $rd = $rb - $rb */
   ppc32_load_gpr(b,AMD64_RBX,rb);
   ppc32_alu_gpr(b,X86_SUB,AMD64_RBX,ra);
   ppc32_store_gpr(b,rd,AMD64_RBX);

   if (insn & 1)
      ppc32_update_cr0(b);
      
   return(0);
}

/* SUBFC - Subtract From Carrying */
DECLARE_INSN(SUBFC)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* ~$ra + 1 */
   ppc32_load_gpr(b,AMD64_RSI,ra);
   amd64_not_reg(b->jit_ptr,AMD64_RSI);
   amd64_alu_reg_imm_size(b->jit_ptr,X86_ADD,AMD64_RSI,1,4);
   amd64_set_reg(b->jit_ptr,X86_CC_C,AMD64_RAX,FALSE);

   /* add $rb */
   ppc32_alu_gpr(b,X86_ADD,AMD64_RSI,rb);
   amd64_set_reg(b->jit_ptr,X86_CC_C,AMD64_RCX,FALSE);

   ppc32_store_gpr(b,rd,AMD64_RSI);

   /* store the carry flag */
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RAX,AMD64_RCX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,0x1);

   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,xer_ca),
                         AMD64_RAX,4);

   /* update cr0 */
   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RSI,AMD64_RSI,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* SUBFE - Subtract From Extended */
DECLARE_INSN(SUBFE)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   /* ~$ra + carry */
   ppc32_load_gpr(b,AMD64_RSI,ra);
   amd64_not_reg(b->jit_ptr,AMD64_RSI);
   amd64_alu_reg_membase_size(b->jit_ptr,X86_ADD,AMD64_RSI,
                              AMD64_R15,OFFSET(cpu_ppc_t,xer_ca),4);
   amd64_set_reg(b->jit_ptr,X86_CC_C,AMD64_RAX,FALSE);

   /* add $rb */
   ppc32_alu_gpr(b,X86_ADD,AMD64_RSI,rb);
   amd64_set_reg(b->jit_ptr,X86_CC_C,AMD64_RCX,FALSE);

   ppc32_store_gpr(b,rd,AMD64_RSI);

   /* store the carry flag */
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RAX,AMD64_RCX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,0x1);
   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,xer_ca),
                         AMD64_RAX,4);

   /* update cr0 */
   if (insn & 1) {
      amd64_test_reg_reg_size(b->jit_ptr,AMD64_RSI,AMD64_RSI,4);
      ppc32_update_cr0(b);
   }

   return(0);
}

/* SUBFIC - Subtract From Immediate Carrying */
DECLARE_INSN(SUBFIC)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t tmp = sign_extend_32(imm,16);

   /* ~$ra + 1 */
   ppc32_load_gpr(b,AMD64_RSI,ra);
   amd64_not_reg(b->jit_ptr,AMD64_RSI);
   amd64_alu_reg_imm_size(b->jit_ptr,X86_ADD,AMD64_RSI,1,4);
   amd64_set_reg(b->jit_ptr,X86_CC_C,AMD64_RAX,FALSE);

   /* add sign-extended $immediate */
   amd64_alu_reg_imm_size(b->jit_ptr,X86_ADD,AMD64_RSI,tmp,4);
   amd64_set_reg(b->jit_ptr,X86_CC_C,AMD64_RCX,FALSE);

   ppc32_store_gpr(b,rd,AMD64_RSI);

   /* store the carry flag */
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RAX,AMD64_RCX);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RAX,0x1);

   amd64_mov_membase_reg(b->jit_ptr,AMD64_R15,OFFSET(cpu_ppc_t,xer_ca),
                         AMD64_RAX,4);
   return(0);
}

/* SYNC - Synchronize */
DECLARE_INSN(SYNC)
{
   return(0);
}

/* XOR */
DECLARE_INSN(XOR)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_load_gpr(b,AMD64_RBX,rs);
   ppc32_alu_gpr(b,X86_XOR,AMD64_RBX,rb);
   ppc32_store_gpr(b,ra,AMD64_RBX);

   if (insn & 1)
      ppc32_update_cr0(b);

   return(0);
}

/* XORI - XOR Immediate */
DECLARE_INSN(XORI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);

   ppc32_load_imm(b,AMD64_RBX,imm);
   ppc32_alu_gpr(b,X86_XOR,AMD64_RBX,rs);
   ppc32_store_gpr(b,ra,AMD64_RBX);
   return(0);
}

/* XORIS - XOR Immediate Shifted */
DECLARE_INSN(XORIS)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);

   ppc32_load_imm(b,AMD64_RBX,imm << 16);
   ppc32_alu_gpr(b,X86_XOR,AMD64_RBX,rs);
   ppc32_store_gpr(b,ra,AMD64_RBX);
   return(0);
}

/* PPC instruction array */
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
};
