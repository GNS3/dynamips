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
#include "mips64_jit.h"
#include "mips64_ppc32_trans.h"
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

/* Set the Pointer Counter (PC) register */
void mips64_set_pc(mips64_jit_tcb_t *b,m_uint64_t new_pc)
{
   ppc_load(b->jit_ptr,ppc_r0,new_pc >> 32);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,pc),ppc_r3);
   ppc_load(b->jit_ptr,ppc_r0,new_pc & 0xffffffff);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,pc)+4,ppc_r3);
}

/* Set the Return Address (RA) register */
void mips64_set_ra(mips64_jit_tcb_t *b,m_uint64_t ret_pc)
{
   ppc_load(b->jit_ptr,ppc_r0,ret_pc >> 32);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(MIPS_GPR_RA),ppc_r3);
   ppc_load(b->jit_ptr,ppc_r0,ret_pc & 0xffffffff);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(MIPS_GPR_RA)+4,ppc_r3);
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
   pc_offset = ((new_pc & MIPS_MIN_PAGE_IMASK) >> 2) * sizeof(u_char *);
   pc_hash = mips64_jit_get_pc_hash(new_pc) * sizeof(mips64_jit_tcb_t *);

   /* Get JIT block info in r4 */
   ppc_lwz(b->jit_ptr,ppc_r4,OFFSET(cpu_mips_t,exec_blk_map),ppc_r3);
   /* Check if offset is too big for a 16-bit immediate value */
   if (pc_hash > 0x7fff)
      ppc_addis(b->jit_ptr,ppc_r4,ppc_r4,ppc_ha16(pc_hash));
   ppc_lwz(b->jit_ptr,ppc_r4,ppc_lo16(pc_hash),ppc_r4);

   /* no JIT block found ? */
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r4,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* r5:r6 = start_pc, r7:r8 = new_page */
   ppc_lwz(b->jit_ptr,ppc_r5,OFFSET(mips64_jit_tcb_t,start_pc),ppc_r4);
   ppc_lwz(b->jit_ptr,ppc_r6,OFFSET(mips64_jit_tcb_t,start_pc)+4,ppc_r4);
   ppc_load(b->jit_ptr,ppc_r7,new_page >> 32);
   ppc_load(b->jit_ptr,ppc_r8,new_page & 0xffffffff);

   /* Check block PC (lower 32-bits first) */
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r6,ppc_r8);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* Check higher bits... */
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r5,ppc_r7);
   test3 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* Jump to the code */
   ppc_lwz(b->jit_ptr,ppc_r4,OFFSET(mips64_jit_tcb_t,jit_insn_ptr),ppc_r4);
   ppc_lwz(b->jit_ptr,ppc_r12,pc_offset,ppc_r4);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r12,0);
   test4 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);
   ppc_mtlr(b->jit_ptr,ppc_r12);
   ppc_blr(b->jit_ptr);

   /* Returns to caller... */
   ppc_patch(test1,b->jit_ptr);
   ppc_patch(test2,b->jit_ptr);
   ppc_patch(test3,b->jit_ptr);
   ppc_patch(test4,b->jit_ptr);

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
         ppc_emit_jump_code(b->jit_ptr, jump_ptr, 0);
      } else {
         /* To check if the target is in a delay slot (previous
            instruction's delay_slot == 0) */
         if (mips64_jit_is_delay_slot(b,new_pc)) {
            mips64_set_pc(b,new_pc);
            mips64_jit_tcb_push_epilog(b);
            return;
         }
      /* When using ppc_patch from mono, the following code will be
         invalid if cpu->vm->exec_area_size, or MIPS_EXEC_AREA_SIZE > 32,
         so here's a hacked way - ppc_emit_jump_code has been used to
         support 32-bit addressing.
         Also notice that += 16 hack depends on insn_block_record_patch
         is called for unconditional branch only.
       */
         mips64_jit_tcb_record_patch(b,b->jit_ptr,new_pc);
         b->jit_ptr += 16;
         /*ppc_nop(b->jit_ptr);
         ppc_nop(b->jit_ptr);
         ppc_nop(b->jit_ptr);
         ppc_nop(b->jit_ptr);*/
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
   ppc_emit_jump_code(b->jit_ptr, (u_char *)f, 1/*linked*/);
   /* Restore the volatile r3 */
   ppc_lwz(b->jit_ptr,ppc_r3,PPC_STACK_DECREMENTER+PPC_STACK_PARAM_OFFSET,ppc_r1);
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
   ppc_load(b->jit_ptr,ppc_r4,insn);
//   mips64_emit_basic_c_call(b,mips64_exec_single_step);
   /* Restore link register */
   ppc_lwz(b->jit_ptr,ppc_r0,PPC_STACK_DECREMENTER+PPC_RET_ADDR_OFFSET,ppc_r1);
   ppc_mtlr(b->jit_ptr,ppc_r0);
   /* Trick: let callee return directly to the caller */   
   ppc_emit_jump_code(b->jit_ptr, (u_char *)mips64_exec_single_step, 0);
}

/* Fast memory operation prototype */
typedef void (*memop_fast_access)(mips64_jit_tcb_t *b,int target);

/* Fast LB */
static void mips64_memop_fast_lb(mips64_jit_tcb_t *b,int target)
{
   ppc_lbzx(b->jit_ptr,ppc_r10,ppc_r7,ppc_r8);
   ppc_extsb(b->jit_ptr,ppc_r10,ppc_r10);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(target),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(target)+4,ppc_r3);
}

/* Fast LBU */
static void mips64_memop_fast_lbu(mips64_jit_tcb_t *b,int target)
{
   ppc_lbzx(b->jit_ptr,ppc_r10,ppc_r7,ppc_r8);
   ppc_li(b->jit_ptr,ppc_r0,0);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(target),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(target)+4,ppc_r3);
}

/* Fast LD */
static void mips64_memop_fast_ld(mips64_jit_tcb_t *b,int target)
{
   ppc_lwzux(b->jit_ptr,ppc_r9,ppc_r7,ppc_r8);
   ppc_lwz(b->jit_ptr,ppc_r10,4,ppc_r7);
   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(target),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(target)+4,ppc_r3);
}

/* Fast LH */
static void mips64_memop_fast_lh(mips64_jit_tcb_t *b,int target)
{
   ppc_lhax(b->jit_ptr,ppc_r10,ppc_r7,ppc_r8);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(target),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(target)+4,ppc_r3);
}

/* Fast LHU */
static void mips64_memop_fast_lhu(mips64_jit_tcb_t *b,int target)
{
   ppc_lhzx(b->jit_ptr,ppc_r10,ppc_r7,ppc_r8);
   ppc_li(b->jit_ptr,ppc_r0,0);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(target),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(target)+4,ppc_r3);
}

/* Fast LW */
static void mips64_memop_fast_lw(mips64_jit_tcb_t *b,int target)
{
   ppc_lwzx(b->jit_ptr,ppc_r10,ppc_r7,ppc_r8);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(target),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(target)+4,ppc_r3);
}

/* Fast LWU */
static void mips64_memop_fast_lwu(mips64_jit_tcb_t *b,int target)
{
   ppc_lwzx(b->jit_ptr,ppc_r10,ppc_r7,ppc_r8);
   ppc_li(b->jit_ptr,ppc_r0,0);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(target),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(target)+4,ppc_r3);
}

/* Fast SB */
static void mips64_memop_fast_sb(mips64_jit_tcb_t *b,int target)
{
   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(target)+4,ppc_r3);
   ppc_stbx(b->jit_ptr,ppc_r10,ppc_r7,ppc_r8);
}

/* Fast SD */
static void mips64_memop_fast_sd(mips64_jit_tcb_t *b,int target)
{
   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(target),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(target)+4,ppc_r3);
   ppc_stwux(b->jit_ptr,ppc_r9,ppc_r7,ppc_r8);
   ppc_stw(b->jit_ptr,ppc_r10,4,ppc_r7);
}

/* Fast SH */
static void mips64_memop_fast_sh(mips64_jit_tcb_t *b,int target)
{
   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(target)+4,ppc_r3);
   ppc_sthx(b->jit_ptr,ppc_r10,ppc_r7,ppc_r8);
}

/* Fast SW */
static void mips64_memop_fast_sw(mips64_jit_tcb_t *b,int target)
{
   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(target)+4,ppc_r3);
   ppc_stwx(b->jit_ptr,ppc_r10,ppc_r7,ppc_r8);
}

/* Fast memory operation (64-bit) */
static void mips64_emit_memop_fast64(mips64_jit_tcb_t *b,int write_op,
                                     int opcode,int base,int offset,
                                     int target,int keep_ll_bit,
                                     memop_fast_access op_handler)
{
   u_char *test1,*test2,*test3,*p_exit;

   test3 = NULL;

   /* r4:r5 = GPR[base] + sign-extended offset */
   ppc_lwz(b->jit_ptr,ppc_r4,REG_OFFSET(base),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(base)+4,ppc_r3);
   ppc_addic(b->jit_ptr,ppc_r5,ppc_r5,offset);
   if (offset & 0x8000 /* < 0 */)
      ppc_addme(b->jit_ptr,ppc_r4,ppc_r4);
   else
      ppc_addze(b->jit_ptr,ppc_r4,ppc_r4);

   /* r7 = offset in mts cache */
   ppc_srwi(b->jit_ptr,ppc_r7,ppc_r5,MTS64_HASH_SHIFT1);
   ppc_srwi(b->jit_ptr,ppc_r6,ppc_r5,MTS64_HASH_SHIFT2);
   ppc_xor(b->jit_ptr,ppc_r8,ppc_r7,ppc_r6);
   ppc_rlwinm(b->jit_ptr,ppc_r7,ppc_r8,
              MTS64_HASH_BITS+5,
              32-(MTS64_HASH_BITS+5),
              31-MTS64_HASH_BITS);
                 
   /* r8 = mts64_cache */
   ppc_lwz(b->jit_ptr,ppc_r8,OFFSET(cpu_mips_t,mts_u.mts64_cache),ppc_r3);
   /* r6 = mts64_entry */
   ppc_add(b->jit_ptr,ppc_r6,ppc_r8,ppc_r7);
   /* r7, r8 are temporary */

   /* Compare virtual page address */
   ppc_lwz(b->jit_ptr,ppc_r7,OFFSET(mts64_entry_t,gvpa),ppc_r6);
   ppc_lwz(b->jit_ptr,ppc_r8,OFFSET(mts64_entry_t,gvpa)+4,ppc_r6);
 
   /* Compare the high part of the vaddr */
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r4,ppc_r7);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);
 
   /* Compare the low part of the vaddr & MIPS_MIN_PAGE_MASK (vpage) */
   ppc_rlwinm(b->jit_ptr,ppc_r0,ppc_r5,0,0,19);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r0,ppc_r8);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* Test if we are writing to a COW page */
   if (write_op) {
      ppc_lwz(b->jit_ptr,ppc_r0,OFFSET(mts64_entry_t,flags),ppc_r6);
      ppc_mtcrf(b->jit_ptr,0x01,ppc_r0);
	  /* MTS_FLAG_COW is moved to EQ bit of cr7 */
      test3 = b->jit_ptr;
      ppc_bc(b->jit_ptr,PPC_BR_TRUE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);
   }

   /* r7 = Host Page Address, r8 = offset in page */
   ppc_lwz(b->jit_ptr,ppc_r7,OFFSET(mts64_entry_t,hpa),ppc_r6);
   //ppc_rlwinm(b->jit_ptr,ppc_r8,ppc_r5,0,32-MIPS_MIN_PAGE_SHIFT,31);
   ppc_andid(b->jit_ptr,ppc_r8,ppc_r5,MIPS_MIN_PAGE_IMASK);

   /* Memory access */
   op_handler(b,target);
 
   p_exit = b->jit_ptr;
   ppc_b(b->jit_ptr,0);

   /* === Slow lookup === */
   ppc_patch(test1,b->jit_ptr);
   ppc_patch(test2,b->jit_ptr);
   if (test3)
      ppc_patch(test3,b->jit_ptr);

   /* The following codes are copied from mips64_emit_memop */
   /* lr = r12 = address of memory function */
   ppc_lwz(b->jit_ptr,ppc_r12,MEMOP_OFFSET(opcode),ppc_r3);
   ppc_mtlr(b->jit_ptr,ppc_r12);
   /* Save PC for exception handling */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   /* r6 = target register */
   ppc_li(b->jit_ptr,ppc_r6,target);
   /* Call memory function */
   ppc_blrl(b->jit_ptr);
   /* Restore the volatile r3 */
   ppc_lwz(b->jit_ptr,ppc_r3,PPC_STACK_DECREMENTER+PPC_STACK_PARAM_OFFSET,ppc_r1);

   ppc_patch(p_exit,b->jit_ptr);
}

/* Fast memory operation (32-bit) */
static void mips64_emit_memop_fast32(mips64_jit_tcb_t *b,int write_op,
                                     int opcode,int base,int offset,
                                     int target,int keep_ll_bit,
                                     memop_fast_access op_handler)
{
   u_char *test2,*test3,*p_exit;

   test3 = NULL;

   /* r5 = GPR[base] + sign-extended offset */
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(base)+4,ppc_r3);
   ppc_addi(b->jit_ptr,ppc_r5,ppc_r5,offset);

   /* r7 = offset in mts cache */
   ppc_srwi(b->jit_ptr,ppc_r7,ppc_r5,MTS32_HASH_SHIFT1);
   ppc_srwi(b->jit_ptr,ppc_r6,ppc_r5,MTS32_HASH_SHIFT2);
   ppc_xor(b->jit_ptr,ppc_r8,ppc_r7,ppc_r6);
   ppc_rlwinm(b->jit_ptr,ppc_r7,ppc_r8,
              MTS32_HASH_BITS+4,
              32-(MTS32_HASH_BITS+4),
              31-MTS32_HASH_BITS);         
              
   /* r8 = mts32_cache */
   ppc_lwz(b->jit_ptr,ppc_r8,OFFSET(cpu_mips_t,mts_u.mts32_cache),ppc_r3);
   /* r6 = mts32_entry */
   ppc_add(b->jit_ptr,ppc_r6,ppc_r8,ppc_r7);
   /* r7, r8 are temporary */

   /* Compare virtual page address (vaddr & MIPS_MIN_PAGE_MASK) */
   ppc_lwz(b->jit_ptr,ppc_r8,OFFSET(mts32_entry_t,gvpa),ppc_r6);

   ppc_rlwinm(b->jit_ptr,ppc_r0,ppc_r5,0,0,19);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r0,ppc_r8);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* Test if we are writing to a COW page */
   if (write_op) {
      ppc_lwz(b->jit_ptr,ppc_r0,OFFSET(mts32_entry_t,flags),ppc_r6);
      ppc_mtcrf(b->jit_ptr,0x01,ppc_r0);
	  /* MTS_FLAG_COW is moved to EQ bit of cr7 */
      test3 = b->jit_ptr;
      ppc_bc(b->jit_ptr,PPC_BR_TRUE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);
   }

   /* r7 = Host Page Address, r8 = offset in page */
   ppc_lwz(b->jit_ptr,ppc_r7,OFFSET(mts32_entry_t,hpa),ppc_r6);
   //ppc_rlwinm(b->jit_ptr,ppc_r8,ppc_r5,0,32-MIPS_MIN_PAGE_SHIFT,31);
   ppc_andid(b->jit_ptr,ppc_r8,ppc_r5,MIPS_MIN_PAGE_IMASK);

   /* Memory access */
   op_handler(b,target);
 
   p_exit = b->jit_ptr;
   ppc_b(b->jit_ptr,0);

   /* === Slow lookup === */
   ppc_patch(test2,b->jit_ptr);
   if (test3)
      ppc_patch(test3,b->jit_ptr);

   /* The following codes are copied from mips64_emit_memop */
   /* lr = r12 = address of memory function */
   ppc_lwz(b->jit_ptr,ppc_r12,MEMOP_OFFSET(opcode),ppc_r3);
   ppc_mtlr(b->jit_ptr,ppc_r12);
   /* Save PC for exception handling */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   /* r4 = sign extention of r5 */
   ppc_srawi(b->jit_ptr,ppc_r4,ppc_r5,31);
   /* r6 = target register */
   ppc_li(b->jit_ptr,ppc_r6,target);
   /* Call memory function */
   ppc_blrl(b->jit_ptr);
   /* Restore the volatile r3 */
   ppc_lwz(b->jit_ptr,ppc_r3,PPC_STACK_DECREMENTER+PPC_STACK_PARAM_OFFSET,ppc_r1);

   ppc_patch(p_exit,b->jit_ptr);
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
static void mips64_emit_memop(mips64_jit_tcb_t *b,int opcode,int base,int offset,
                              int target,int keep_ll_bit)
{
   /* lr = r12 = address of memory function */
   ppc_lwz(b->jit_ptr,ppc_r12,MEMOP_OFFSET(opcode),ppc_r3);
   ppc_mtlr(b->jit_ptr,ppc_r12);

   /* Save PC for exception handling */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   if (!keep_ll_bit) {
      ppc_li(b->jit_ptr,ppc_r0,0);
      ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,ll_bit),ppc_r3);
   }

   /* r3 = CPU instance pointer */
   /* r4:r5 = GPR[base] + sign-extended offset */
   ppc_lwz(b->jit_ptr,ppc_r4,REG_OFFSET(base),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(base)+4,ppc_r3);
   ppc_addic(b->jit_ptr,ppc_r5,ppc_r5,offset);

   if (offset & 0x8000 /* < 0 */)
      ppc_addme(b->jit_ptr,ppc_r4,ppc_r4);
   else
      ppc_addze(b->jit_ptr,ppc_r4,ppc_r4);
      
   /* r6 = target register */
   ppc_li(b->jit_ptr,ppc_r6,target);

   /* Call memory function */
   ppc_blrl(b->jit_ptr);

   /* Restore the volatile r3 */
   ppc_lwz(b->jit_ptr,ppc_r3,PPC_STACK_DECREMENTER+PPC_STACK_PARAM_OFFSET,ppc_r1);
}

/* Coprocessor Register transfert operation */
static void mips64_emit_cp_xfr_op(mips64_jit_tcb_t *b,int rt,int rd,void *f)
{
   /* update pc */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   /* r3 = CPU instance pointer */
   /* r4 = gpr */
   ppc_load(b->jit_ptr,ppc_r4,rt);

   /* r5 = cp0 register */
   ppc_load(b->jit_ptr,ppc_r5,rd);

   mips64_emit_basic_c_call(b,f);
}

/* Virtual Breakpoint */
void mips64_emit_breakpoint(mips64_jit_tcb_t *b)
{
   mips64_emit_c_call(b,mips64_run_breakpoint);
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
   ppc_load(b->jit_ptr,ppc_r4,opcode);
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

/* Emit invalid delay slot */
int mips64_emit_invalid_delay_slot(mips64_jit_tcb_t *b)
{
   /* Restore link register */
   ppc_lwz(b->jit_ptr,ppc_r0,PPC_STACK_DECREMENTER+PPC_RET_ADDR_OFFSET,ppc_r1);
   ppc_mtlr(b->jit_ptr,ppc_r0);

   /* Trick: let callee return directly to the caller */   
   ppc_emit_jump_code(b->jit_ptr, (u_char *)mips64_invalid_delay_slot, 0);
   return(0);
}

/* 
 * Increment count register and trigger the timer IRQ if value in compare 
 * register is the same.
 */
void mips64_inc_cp0_count_reg(mips64_jit_tcb_t *b)
{
   ppc_lwz(b->jit_ptr,ppc_r4,OFFSET(cpu_mips_t,cp0_virt_cnt_reg),ppc_r3);
   ppc_addi(b->jit_ptr,ppc_r4,ppc_r4,1); // addi takes 0 instead of r0
   ppc_stw(b->jit_ptr,ppc_r4,OFFSET(cpu_mips_t,cp0_virt_cnt_reg),ppc_r3);
}

/* Check if there are pending IRQ */
void mips64_check_pending_irq(mips64_jit_tcb_t *b)
{
   u_char *test1;

   /* Check the pending IRQ flag */
   ppc_lwz(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,irq_pending),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r0,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* Save PC */
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));

   /* Trigger the IRQ */
   mips64_emit_basic_c_call(b,mips64_trigger_irq);
   mips64_jit_tcb_push_epilog(b);

   ppc_patch(test1,b->jit_ptr);
}

/* Increment the number of executed instructions (performance debugging) */
void mips64_inc_perf_counter(mips64_jit_tcb_t *b)
{
   ppc_lwz(b->jit_ptr,ppc_r4,OFFSET(cpu_mips_t,perf_counter),ppc_r3);
   ppc_addi(b->jit_ptr,ppc_r4,ppc_r4,1); // addi takes 0 instead of r0
   ppc_stw(b->jit_ptr,ppc_r4,OFFSET(cpu_mips_t,perf_counter),ppc_r3);
}

/* ADD */
DECLARE_INSN(ADD)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(rt)+4,ppc_r3);
   ppc_addo(b->jit_ptr,ppc_r10,ppc_r8,ppc_r9);

   /* TODO: Exception handling */

   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* ADDI */
DECLARE_INSN(ADDI)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rs)+4,ppc_r3);
   ppc_addi(b->jit_ptr,ppc_r9,ppc_r8,imm);

   /* TODO: Exception handling */

   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(rt)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r9,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rt),ppc_r3);
   return(0);
}

/* ADDIU */
DECLARE_INSN(ADDIU)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rs)+4,ppc_r3);
   ppc_addi(b->jit_ptr,ppc_r9,ppc_r8,imm);

   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(rt)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r9,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rt),ppc_r3);
   return(0);
}

/* ADDU */
DECLARE_INSN(ADDU)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(rt)+4,ppc_r3);
   ppc_add(b->jit_ptr,ppc_r10,ppc_r8,ppc_r9);

   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* AND */
DECLARE_INSN(AND)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);

   ppc_and(b->jit_ptr,ppc_r9,ppc_r5,ppc_r7);
   ppc_and(b->jit_ptr,ppc_r10,ppc_r6,ppc_r8);

   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* ANDI */
DECLARE_INSN(ANDI)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);

   ppc_andid(b->jit_ptr,ppc_r8,ppc_r6,imm);

   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);
   ppc_li(b->jit_ptr,ppc_r0,0);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rt),ppc_r3);

   return(0);
}

/* B (virtual) */
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

/* BAL (virtual) */
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

/* BEQ */
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
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r6,ppc_r8);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r5,ppc_r7);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test1,b->jit_ptr);
   ppc_patch(test2,b->jit_ptr);

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
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r6,ppc_r8);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r5,ppc_r7);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test1,b->jit_ptr);
   ppc_patch(test2,b->jit_ptr);
   return(0);
}

/* BEQZ (virtual) */
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
    * compare gpr[rs] and gpr[rt]. 
    * compare the low 32 bits first (higher probability).
    */
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r6,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test1,b->jit_ptr);
   ppc_patch(test2,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BNEZ (virtual) */
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
    * compare gpr[rs] and gpr[rt]. 
    * compare the low 32 bits first (higher probability).
    */
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r6,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test2,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGEZ */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_LT),0);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGEZAL */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_LT),0);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGEZALL */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_LT),0);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test1,b->jit_ptr);
   return(0);
}

/* BGEZL */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_LT),0);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test1,b->jit_ptr);
   return(0);
}

/* BGTZ */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_GT),0);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_LT),0);

   /* test the lo word of gpr[rs] (here hi word == 0) */
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r6,0);
   test3 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test2,b->jit_ptr);
   ppc_patch(test3,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BGTZL */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_GT),0);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_LT),0);

   /* test the lo word of gpr[rs] (here hi word == 0) */
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r6,0);
   test3 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test2,b->jit_ptr);
   ppc_patch(test3,b->jit_ptr);
   return(0);
}

/* BLEZ */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_LT),0);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_GT),0);

   /* test the lo word of gpr[rs] (here hi word == 0) */
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r6,0);
   test3 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test2,b->jit_ptr);
   ppc_patch(test3,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BLEZL */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_LT),0);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_GT),0);

   /* test the lo word of gpr[rs] (here hi word == 0) */
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r6,0);
   test3 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test2,b->jit_ptr);
   ppc_patch(test3,b->jit_ptr);
   return(0);
}

/* BLTZ */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_LT),0);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BLTZAL */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_LT),0);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test1,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BLTZALL */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_LT),0);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test1,b->jit_ptr);
   return(0);
}

/* BLTZL */
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
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,0);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_LT),0);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test1,b->jit_ptr);
   return(0);
}

/* BNE */
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
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r6,ppc_r8);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r5,ppc_r7);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,2);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test2,b->jit_ptr);

   /* if the branch is not taken, we have to execute the delay slot too */
   mips64_jit_fetch_and_emit(cpu,b,1);
   return(0);
}

/* BNEL */
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
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r6,ppc_r8);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r5,ppc_r7);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_TRUE_UNLIKELY,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   ppc_patch(test1,b->jit_ptr);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc in cpu structure */
   mips64_set_jump(cpu,b,new_pc,1);

   ppc_patch(test2,b->jit_ptr);
   return(0);
}

/* BREAK */
DECLARE_INSN(BREAK)
{	
   u_int code = bits(insn,6,25);
   /* r3 = CPU instance pointer */
   /* r4 = code */
   ppc_load(b->jit_ptr,ppc_r4,code);
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
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);

   ppc_addic(b->jit_ptr,ppc_r6,ppc_r6,imm);
   if (imm  & 0x8000 /* < 0 */)
      ppc_addme(b->jit_ptr,ppc_r5,ppc_r5);
   else
      ppc_addze(b->jit_ptr,ppc_r5,ppc_r5);

   ppc_stw(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r5,REG_OFFSET(rt),ppc_r3);
   return(0);
}

/* DADDU */
DECLARE_INSN(DADDU)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);

   ppc_addc(b->jit_ptr,ppc_r10,ppc_r6,ppc_r8);
   ppc_adde(b->jit_ptr,ppc_r9,ppc_r5,ppc_r7);

   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* DIV */
DECLARE_INSN(DIV)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);

   ppc_divw(b->jit_ptr,ppc_r10,ppc_r6,ppc_r8);
   /* store LO (quotient) */
   ppc_stw(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,lo)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,lo),ppc_r3);

   ppc_mullw(b->jit_ptr,ppc_r9,ppc_r10,ppc_r8);
   ppc_subf(b->jit_ptr,ppc_r9,ppc_r9,ppc_r6);
   /* store HI (remainder) */
   ppc_stw(b->jit_ptr,ppc_r9,OFFSET(cpu_mips_t,hi)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r9,31);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,hi),ppc_r3);
   return(0);
}

/* DIVU */
DECLARE_INSN(DIVU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);

   ppc_divwu(b->jit_ptr,ppc_r10,ppc_r6,ppc_r8);
   /* store LO (quotient) */
   ppc_stw(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,lo)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,lo),ppc_r3);

   ppc_mullw(b->jit_ptr,ppc_r9,ppc_r10,ppc_r8);
   ppc_subf(b->jit_ptr,ppc_r9,ppc_r9,ppc_r6);
   /* store HI (remainder) */
   ppc_stw(b->jit_ptr,ppc_r9,OFFSET(cpu_mips_t,hi)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r9,31);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,hi),ppc_r3);
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

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rt),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_slwi(b->jit_ptr,ppc_r7,ppc_r5,sa);
   if (sa > 0)
      ppc_rlwimi(b->jit_ptr,ppc_r7,ppc_r6,sa,32-sa,31);
   ppc_slwi(b->jit_ptr,ppc_r8,ppc_r6,sa);

   ppc_stw(b->jit_ptr,ppc_r7,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* DSLL32 */
DECLARE_INSN(DSLL32)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_slwi(b->jit_ptr,ppc_r7,ppc_r6,sa);
   ppc_li(b->jit_ptr,ppc_r8,0);

   ppc_stw(b->jit_ptr,ppc_r7,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* DSLLV */
DECLARE_INSN(DSLLV)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rt),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_andid(b->jit_ptr,ppc_r10,ppc_r10,0x3f);
//   ppc_rlwinm(b->jit_ptr,ppc_r10,ppc_r10,0,26,31);
   ppc_subfic(b->jit_ptr,ppc_r9,ppc_r10,32);
   ppc_slw(b->jit_ptr,ppc_r7,ppc_r5,ppc_r10);
   ppc_srw(b->jit_ptr,ppc_r9,ppc_r6,ppc_r9);
   ppc_or(b->jit_ptr,ppc_r7,ppc_r7,ppc_r9);
   ppc_subi(b->jit_ptr,ppc_r9,ppc_r10,32);
   ppc_slw(b->jit_ptr,ppc_r9,ppc_r6,ppc_r9);
   ppc_or(b->jit_ptr,ppc_r7,ppc_r7,ppc_r9);
   ppc_slw(b->jit_ptr,ppc_r8,ppc_r6,ppc_r10);

   ppc_stw(b->jit_ptr,ppc_r7,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* DSRA */
DECLARE_INSN(DSRA)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rt),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_srawi(b->jit_ptr,ppc_r7,ppc_r5,sa);
   ppc_srwi(b->jit_ptr,ppc_r8,ppc_r6,sa);
   if (sa > 0)
      ppc_rlwimi(b->jit_ptr,ppc_r8,ppc_r5,32-sa,0,sa-1);

   ppc_stw(b->jit_ptr,ppc_r7,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* DSRA32 */
DECLARE_INSN(DSRA32)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rt),ppc_r3);

   ppc_srawi(b->jit_ptr,ppc_r7,ppc_r5,31);
   ppc_srawi(b->jit_ptr,ppc_r8,ppc_r5,sa);

   ppc_stw(b->jit_ptr,ppc_r7,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* DSRAV */
DECLARE_INSN(DSRAV)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   u_char *test1;

   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rt),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_andid(b->jit_ptr,ppc_r10,ppc_r10,0x3f);
//   ppc_rlwinm(b->jit_ptr,ppc_r10,ppc_r10,0,26,31);
   ppc_subfic(b->jit_ptr,ppc_r9,ppc_r10,32);
   ppc_srw(b->jit_ptr,ppc_r8,ppc_r6,ppc_r10);
   ppc_slw(b->jit_ptr,ppc_r7,ppc_r5,ppc_r9);
   ppc_or(b->jit_ptr,ppc_r8,ppc_r8,ppc_r7);
   ppc_subicd(b->jit_ptr,ppc_r9,ppc_r10,32);
   ppc_sraw(b->jit_ptr,ppc_r7,ppc_r5,ppc_r9);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr0,PPC_BR_GT),0);

   ppc_mr(b->jit_ptr,ppc_r8,ppc_r7);
   ppc_sraw(b->jit_ptr,ppc_r7,ppc_r5,ppc_r10);

   ppc_patch(test1,b->jit_ptr);
   ppc_stw(b->jit_ptr,ppc_r7,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* DSRL */
DECLARE_INSN(DSRL)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rt),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_srwi(b->jit_ptr,ppc_r7,ppc_r5,sa);
   ppc_srwi(b->jit_ptr,ppc_r8,ppc_r6,sa);
   if (sa > 0)
      ppc_rlwimi(b->jit_ptr,ppc_r8,ppc_r5,32-sa,0,sa-1);

   ppc_stw(b->jit_ptr,ppc_r7,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* DSRL32 */
DECLARE_INSN(DSRL32)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rt),ppc_r3);

   ppc_li(b->jit_ptr,ppc_r7,0);
   ppc_srwi(b->jit_ptr,ppc_r8,ppc_r5,sa);

   ppc_stw(b->jit_ptr,ppc_r7,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* DSRLV */
DECLARE_INSN(DSRLV)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rt),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_andid(b->jit_ptr,ppc_r10,ppc_r10,0x3f);
//   ppc_rlwinm(b->jit_ptr,ppc_r10,ppc_r10,0,26,31);
   ppc_subfic(b->jit_ptr,ppc_r9,ppc_r10,32);
   ppc_srw(b->jit_ptr,ppc_r8,ppc_r6,ppc_r10);
   ppc_slw(b->jit_ptr,ppc_r9,ppc_r5,ppc_r9);
   ppc_or(b->jit_ptr,ppc_r8,ppc_r8,ppc_r9);
   ppc_subi(b->jit_ptr,ppc_r9,ppc_r10,32);
   ppc_srw(b->jit_ptr,ppc_r9,ppc_r5,ppc_r9);
   ppc_or(b->jit_ptr,ppc_r8,ppc_r8,ppc_r9);
   ppc_srw(b->jit_ptr,ppc_r7,ppc_r5,ppc_r10);

   ppc_stw(b->jit_ptr,ppc_r7,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* DSUBU */
DECLARE_INSN(DSUBU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);

   ppc_subc(b->jit_ptr,ppc_r10,ppc_r6,ppc_r8);
   ppc_subfe(b->jit_ptr,ppc_r9,ppc_r7,ppc_r5);

   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* ERET */
DECLARE_INSN(ERET)
{
   /* Restore link register */
   ppc_lwz(b->jit_ptr,ppc_r0,PPC_STACK_DECREMENTER+PPC_RET_ADDR_OFFSET,ppc_r1);
   ppc_mtlr(b->jit_ptr,ppc_r0);
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   /* Trick: let callee return directly to the caller */   
   ppc_emit_jump_code(b->jit_ptr, (u_char *)mips64_exec_eret, 0);
   return(0);
}

/* J */
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

/* JAL */
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

/* JALR */
DECLARE_INSN(JALR)
{
   int rs = bits(insn,21,25);
   int rd = bits(insn,11,15);
   m_uint64_t ret_pc;

   /* set the return pc (instruction after the delay slot) in GPR[rd] */
   ret_pc = b->start_pc + ((b->mips_trans_pos + 1) << 2);
   ppc_load(b->jit_ptr,ppc_r7,ret_pc >> 32);
   ppc_load(b->jit_ptr,ppc_r8,ret_pc & 0xffffffff);
   ppc_stw(b->jit_ptr,ppc_r7,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);

   /* get the new pc */
   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(rs)+4,ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r9,OFFSET(cpu_mips_t,ret_pc),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,ret_pc)+4,ppc_r3);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc */
   ppc_lwz(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,ret_pc),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,pc),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,ret_pc)+4,ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,pc)+4,ppc_r3);

   /* returns to the caller which will determine the next path */
   mips64_jit_tcb_push_epilog(b);
   return(0);
}

/* JR */
DECLARE_INSN(JR)
{	
   int rs = bits(insn,21,25);

   /* get the new pc */
   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(rs)+4,ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r9,OFFSET(cpu_mips_t,ret_pc),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,ret_pc)+4,ppc_r3);

   /* insert the instruction in the delay slot */
   mips64_jit_fetch_and_emit(cpu,b,1);

   /* set the new pc */
   ppc_lwz(b->jit_ptr,ppc_r9,OFFSET(cpu_mips_t,ret_pc),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,ret_pc)+4,ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r9,OFFSET(cpu_mips_t,pc),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,pc)+4,ppc_r3);

   /* returns to the caller which will determine the next path */
   mips64_jit_tcb_push_epilog(b);
   return(0);
}

/* LB */
DECLARE_INSN(LB)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,0,MIPS_MEMOP_LB,base,offset,rt,TRUE,
                             mips64_memop_fast_lb);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_LB,base,offset,rt,TRUE);
   }
   return(0);
}

/* LBU */
DECLARE_INSN(LBU)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,0,MIPS_MEMOP_LBU,base,offset,rt,TRUE,
                             mips64_memop_fast_lbu);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_LBU,base,offset,rt,TRUE);
   }
   return(0);
}

/* LD */
DECLARE_INSN(LD)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,0,MIPS_MEMOP_LD,base,offset,rt,TRUE,
                             mips64_memop_fast_ld);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_LD,base,offset,rt,TRUE);
   }
   return(0);
}

/* LDC1 */
DECLARE_INSN(LDC1)
{
   int base   = bits(insn,21,25);
   int ft     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LDC1,base,offset,ft,TRUE);
   return(0);
}

/* LDL */
DECLARE_INSN(LDL)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LDL,base,offset,rt,TRUE);
   return(0);
}

/* LDR */
DECLARE_INSN(LDR)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LDR,base,offset,rt,TRUE);
   return(0);
}

/* LH */
DECLARE_INSN(LH)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,0,MIPS_MEMOP_LH,base,offset,rt,TRUE,
                             mips64_memop_fast_lh);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_LH,base,offset,rt,TRUE);
   }
   return(0);
}

/* LHU */
DECLARE_INSN(LHU)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,0,MIPS_MEMOP_LHU,base,offset,rt,TRUE,
                             mips64_memop_fast_lhu);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_LHU,base,offset,rt,TRUE);
   }
   return(0);
}

/* LI (virtual) */
DECLARE_INSN(LI)
{
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   ppc_li(b->jit_ptr,ppc_r9,imm);
   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(rt)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r9,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rt),ppc_r3);
   return(0);
}

/* LL */
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

   ppc_lis(b->jit_ptr,ppc_r10,imm);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rt)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rt),ppc_r3);
   return(0);
}

/* LW */
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

/* LWL */
DECLARE_INSN(LWL)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LWL,base,offset,rt,TRUE);
   return(0);
}

/* LWR */
DECLARE_INSN(LWR)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_LWR,base,offset,rt,TRUE);
   return(0);
}

/* LWU */
DECLARE_INSN(LWU)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,0,MIPS_MEMOP_LWU,base,offset,rt,TRUE,
                             mips64_memop_fast_lwu);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_LWU,base,offset,rt,TRUE);
   }
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

   ppc_lwz(b->jit_ptr,ppc_r9,OFFSET(cpu_mips_t,hi),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,hi)+4,ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* MFLO */
DECLARE_INSN(MFLO)
{
   int rd = bits(insn,11,15);

   /* Optimization for "mflo zr" */
   if (!rd) return(0);

   ppc_lwz(b->jit_ptr,ppc_r9,OFFSET(cpu_mips_t,lo),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,lo)+4,ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* MOVE (virtual) */
DECLARE_INSN(MOVE)
{	
   int rs = bits(insn,21,25);
   int rd = bits(insn,11,15);
   
   if (rs != 0) {
      ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rs)+4,ppc_r3);
      ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
      ppc_srawi(b->jit_ptr,ppc_r0,ppc_r8,31);
      ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   } else {
      ppc_li(b->jit_ptr,ppc_r0,0);
      ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd)+4,ppc_r3);
      ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
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

   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(rs)+4,ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r9,OFFSET(cpu_mips_t,hi),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,hi)+4,ppc_r3);
   return(0);
}

/* MTLO */
DECLARE_INSN(MTLO)
{
   int rs = bits(insn,21,25);

   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(rs)+4,ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r9,OFFSET(cpu_mips_t,lo),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,lo)+4,ppc_r3);
   return(0);
}

/* MUL */
DECLARE_INSN(MUL)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(rt)+4,ppc_r3);
   ppc_mullw(b->jit_ptr,ppc_r10,ppc_r8,ppc_r9);

   /* store result in gpr[rd] */
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* MULT */
DECLARE_INSN(MULT)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(rt)+4,ppc_r3);

   ppc_mullw(b->jit_ptr,ppc_r10,ppc_r8,ppc_r9);
   /* store LO */
   ppc_stw(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,lo)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,lo),ppc_r3);

   ppc_mulhw(b->jit_ptr,ppc_r10,ppc_r8,ppc_r9);
   /* store HI */
   ppc_stw(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,hi)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,hi),ppc_r3);
   return(0);
}

/* MULTU */
DECLARE_INSN(MULTU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(rt)+4,ppc_r3);

   ppc_mullw(b->jit_ptr,ppc_r10,ppc_r8,ppc_r9);
   /* store LO */
   ppc_stw(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,lo)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,lo),ppc_r3);

   ppc_mulhwu(b->jit_ptr,ppc_r10,ppc_r8,ppc_r9);
   /* store HI */
   ppc_stw(b->jit_ptr,ppc_r10,OFFSET(cpu_mips_t,hi)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,OFFSET(cpu_mips_t,hi),ppc_r3);
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

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);

   ppc_nor(b->jit_ptr,ppc_r9,ppc_r5,ppc_r7);
   ppc_nor(b->jit_ptr,ppc_r10,ppc_r6,ppc_r8);

   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* OR */
DECLARE_INSN(OR)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);

   ppc_or(b->jit_ptr,ppc_r9,ppc_r5,ppc_r7);
   ppc_or(b->jit_ptr,ppc_r10,ppc_r6,ppc_r8);

   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   return(0);
}

/* ORI */
DECLARE_INSN(ORI)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);

   ppc_ori(b->jit_ptr,ppc_r8,ppc_r6,imm);

   ppc_stw(b->jit_ptr,ppc_r5,REG_OFFSET(rt),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);
   return(0);
}

/* PREF */
DECLARE_INSN(PREF)
{
/*   int base   = bits(insn,21,25);
   int hint   = bits(insn,16,20);
   int offset = bits(insn,0,15);
*/
   return(0);
}

/* PREFX */
DECLARE_INSN(PREFX)
{
/*   int base   = bits(insn,21,25);
   int index  = bits(insn,16,20);
   int hint   = bits(insn,11,15);
*/
   return(0);
}

/* SB */
DECLARE_INSN(SB)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,1,MIPS_MEMOP_SB,base,offset,rt,FALSE,
                             mips64_memop_fast_sb);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_SB,base,offset,rt,FALSE);
   }
   return(0);
}

/* SC */
DECLARE_INSN(SC)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SC,base,offset,rt,TRUE);
   return(0);
}

/* SD */
DECLARE_INSN(SD)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,1,MIPS_MEMOP_SD,base,offset,rt,FALSE,
                             mips64_memop_fast_sd);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_SD,base,offset,rt,FALSE);
   }
   return(0);
}

/* SDL */
DECLARE_INSN(SDL)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SDL,base,offset,rt,FALSE);
   return(0);
}

/* SDR */
DECLARE_INSN(SDR)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SDR,base,offset,rt,FALSE);
   return(0);
}

/* SDC1 */
DECLARE_INSN(SDC1)
{
   int base   = bits(insn,21,25);
   int ft     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SDC1,base,offset,ft,FALSE);
   return(0);
}

/* SH */
DECLARE_INSN(SH)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   if (cpu->fast_memop) {
      mips64_emit_memop_fast(cpu,b,1,MIPS_MEMOP_SH,base,offset,rt,FALSE,
                             mips64_memop_fast_sh);
   } else {
      mips64_emit_memop(b,MIPS_MEMOP_SH,base,offset,rt,FALSE);
   }
   return(0);
}

/* SLL */
DECLARE_INSN(SLL)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_slwi(b->jit_ptr,ppc_r8,ppc_r6,sa);

   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r8,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* SLLV */
DECLARE_INSN(SLLV)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_andid(b->jit_ptr,ppc_r10,ppc_r10,0x1f);
//   ppc_rlwinm(b->jit_ptr,ppc_r10,ppc_r10,0,27,31);
   ppc_slw(b->jit_ptr,ppc_r8,ppc_r6,ppc_r10);

   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r8,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* SLT */
DECLARE_INSN(SLT)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);

   ppc_subc(b->jit_ptr,ppc_r0,ppc_r6,ppc_r8);
   ppc_subfe(b->jit_ptr,ppc_r0,ppc_r7,ppc_r5);
   ppc_eqv(b->jit_ptr,ppc_r0,ppc_r7,ppc_r5);
   ppc_srwi(b->jit_ptr,ppc_r0,ppc_r0,31);
   ppc_addze(b->jit_ptr,ppc_r0,ppc_r0);
   ppc_andid(b->jit_ptr,ppc_r10,ppc_r0,0x1);
   //ppc_rlwinm(b->jit_ptr,ppc_r10,ppc_r0,0,31,31);

   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   ppc_li(b->jit_ptr,ppc_r0,0);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* SLTI */
DECLARE_INSN(SLTI)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int imm = bits(insn,0,15);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);

   ppc_subic(b->jit_ptr,ppc_r0,ppc_r6,imm);
   if (imm  & 0x8000 /* < 0 */) {
      ppc_addze(b->jit_ptr,ppc_r0,ppc_r5);
      ppc_srwi(b->jit_ptr,ppc_r0,ppc_r5,31);
   } else {
      ppc_addme(b->jit_ptr,ppc_r0,ppc_r5);
      ppc_srwi(b->jit_ptr,ppc_r0,ppc_r5,31);
      ppc_xori(b->jit_ptr,ppc_r0,ppc_r0,0x1);
   }
   ppc_addze(b->jit_ptr,ppc_r0,ppc_r0);
   ppc_andid(b->jit_ptr,ppc_r10,ppc_r0,0x1);
   //ppc_rlwinm(b->jit_ptr,ppc_r10,ppc_r0,0,31,31);

   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rt)+4,ppc_r3);
   ppc_li(b->jit_ptr,ppc_r0,0);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rt),ppc_r3);
   return(0);
}

/* SLTIU */
DECLARE_INSN(SLTIU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int imm = bits(insn,0,15);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);

   ppc_subic(b->jit_ptr,ppc_r0,ppc_r6,imm);
   if (imm  & 0x8000 /* < 0 */)
      ppc_addze(b->jit_ptr,ppc_r0,ppc_r5);
   else
      ppc_addme(b->jit_ptr,ppc_r0,ppc_r5);
   ppc_subfe(b->jit_ptr,ppc_r0,ppc_r0,ppc_r0);
   ppc_neg(b->jit_ptr,ppc_r10,ppc_r0);

   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rt)+4,ppc_r3);
   ppc_li(b->jit_ptr,ppc_r0,0);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rt),ppc_r3);
   return(0);
}

/* SLTU */
DECLARE_INSN(SLTU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);

   ppc_subc(b->jit_ptr,ppc_r0,ppc_r6,ppc_r8);
   ppc_subfe(b->jit_ptr,ppc_r0,ppc_r7,ppc_r5);
   ppc_subfe(b->jit_ptr,ppc_r0,ppc_r0,ppc_r0);
   ppc_neg(b->jit_ptr,ppc_r10,ppc_r0);

   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   ppc_li(b->jit_ptr,ppc_r0,0);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* SRA */
DECLARE_INSN(SRA)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_srawi(b->jit_ptr,ppc_r8,ppc_r6,sa);

   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r8,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* SRAV */
DECLARE_INSN(SRAV)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_andid(b->jit_ptr,ppc_r10,ppc_r10,0x1f);
//   ppc_rlwinm(b->jit_ptr,ppc_r10,ppc_r10,0,27,31);
   ppc_sraw(b->jit_ptr,ppc_r8,ppc_r6,ppc_r10);

   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r8,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* SRL */
DECLARE_INSN(SRL)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_srwi(b->jit_ptr,ppc_r8,ppc_r6,sa);

   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r8,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* SRLV */
DECLARE_INSN(SRLV)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r10,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rt)+4,ppc_r3);

   ppc_andid(b->jit_ptr,ppc_r10,ppc_r10,0x1f);
//   ppc_rlwinm(b->jit_ptr,ppc_r10,ppc_r10,0,27,31);
   ppc_srw(b->jit_ptr,ppc_r8,ppc_r6,ppc_r10);

   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rd)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r8,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* SUB */
DECLARE_INSN(SUB)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(rt)+4,ppc_r3);
   ppc_subfo(b->jit_ptr,ppc_r10,ppc_r9,ppc_r8);

   /* TODO: Exception handling */

   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* SUBU */
DECLARE_INSN(SUBU)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r9,REG_OFFSET(rt)+4,ppc_r3);
   ppc_sub(b->jit_ptr,ppc_r10,ppc_r8,ppc_r9);

   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);
   ppc_srawi(b->jit_ptr,ppc_r0,ppc_r10,31);
   ppc_stw(b->jit_ptr,ppc_r0,REG_OFFSET(rd),ppc_r3);
   return(0);
}

/* SW */
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

/* SWL */
DECLARE_INSN(SWL)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_emit_memop(b,MIPS_MEMOP_SWL,base,offset,rt,FALSE);
   return(0);
}

/* SWR */
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
   /* Restore link register */
   ppc_lwz(b->jit_ptr,ppc_r0,PPC_STACK_DECREMENTER+PPC_RET_ADDR_OFFSET,ppc_r1);
   ppc_mtlr(b->jit_ptr,ppc_r0);
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   /* Trick: let callee return directly to the caller */   
   ppc_emit_jump_code(b->jit_ptr, (u_char *)mips64_exec_syscall, 0);
   return(0);
}

/* TEQ */
DECLARE_INSN(TEQ)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   u_char *test1,*test2;

   /* Compare low part */
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r6,ppc_r8);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* Compare high part */
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);
   ppc_cmpw(b->jit_ptr,ppc_cr7,ppc_r5,ppc_r7);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* Generate trap exception */
   mips64_emit_c_call(b,mips64_trigger_trap_exception);
   mips64_jit_tcb_push_epilog(b);

   /* end */
   ppc_patch(test1,b->jit_ptr);
   ppc_patch(test2,b->jit_ptr);
   return(0);
}

/* TEQI (Trap If Equal Immediate) */
DECLARE_INSN(TEQI)
{
   int rs  = bits(insn,21,25);
   int imm = bits(insn,0,15);
   u_char *test1,*test2;

   /* Compare low part */
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r6,imm);
   test1 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* Compare high part */
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_cmpwi(b->jit_ptr,ppc_cr7,ppc_r5,(imm << 16) >> 31);
   test2 = b->jit_ptr;
   ppc_bc(b->jit_ptr,PPC_BR_FALSE,ppc_crbf(ppc_cr7,PPC_BR_EQ),0);

   /* Generate trap exception */
   mips64_emit_c_call(b,mips64_trigger_trap_exception);
   mips64_jit_tcb_push_epilog(b);

   /* end */
   ppc_patch(test1,b->jit_ptr);
   ppc_patch(test2,b->jit_ptr);
   return(0);
}

/* TLBP */
DECLARE_INSN(TLBP)
{
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbp);
   return(0);
}

/* TLBR */
DECLARE_INSN(TLBR)
{  
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbr);
   return(0);
}

/* TLBWI */
DECLARE_INSN(TLBWI)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbwi);
   return(0);
}

/* TLBWR */
DECLARE_INSN(TLBWR)
{   
   mips64_set_pc(b,b->start_pc+((b->mips_trans_pos-1)<<2));
   mips64_emit_basic_c_call(b,mips64_cp0_exec_tlbwr);
   return(0);
}

/* XOR */
DECLARE_INSN(XOR)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r7,REG_OFFSET(rt),ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);

   ppc_xor(b->jit_ptr,ppc_r9,ppc_r5,ppc_r7);
   ppc_xor(b->jit_ptr,ppc_r10,ppc_r6,ppc_r8);

   ppc_stw(b->jit_ptr,ppc_r9,REG_OFFSET(rd),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r10,REG_OFFSET(rd)+4,ppc_r3);

   return(0);
}

/* XORI */
DECLARE_INSN(XORI)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   ppc_lwz(b->jit_ptr,ppc_r6,REG_OFFSET(rs)+4,ppc_r3);
   ppc_lwz(b->jit_ptr,ppc_r5,REG_OFFSET(rs),ppc_r3);

   ppc_xori(b->jit_ptr,ppc_r8,ppc_r6,imm);

   ppc_stw(b->jit_ptr,ppc_r5,REG_OFFSET(rt),ppc_r3);
   ppc_stw(b->jit_ptr,ppc_r8,REG_OFFSET(rt)+4,ppc_r3);

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
   { mips64_emit_PREFX   , 0xfc0007ff , 0x4c00000f, 1 },
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
