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
#include "jit_op.h"
#include "ppc32_jit.h"
#include "ppc32_arm64_trans.h"
#include "memory.h"

/*
 * Register assignments:
 * X9 - general scratch
 * X25 - direct far jump temporary
 * X26 - preserved host lr
 * X27 - new address for updating memops, direct far jump temporary 2
 * X28 - pointer to cpu_ppc_t
 * All other registers are used for regalloc, except for X18
 */

/* Macros for CPU structure access */
#define REG_OFFSET(reg)   (OFFSET(cpu_ppc_t,gpr[(reg)]))
#define MEMOP_OFFSET(op)  (OFFSET(cpu_ppc_t,mem_op_fn[(op)]))

#define DECLARE_INSN(name) \
   static int ppc32_emit_##name(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b, \
                                ppc_insn_t insn)

/* Load a 32 bit immediate value */
static inline void ppc32_load_imm(u_char **ptr,u_int reg,m_uint32_t value)
{
   if (value >= 0xFFFF0000) {
      arm64_movn32(*ptr,reg,~value,0);
      return;
   }
   arm64_movz64(*ptr,reg,value&0xFFFF,0);
   if (value >> 16)
      arm64_movk64(*ptr,reg,(value>>16)&0xFFFF,16);
}

static inline void ppc32_load_imm64(u_char **ptr,u_int reg,m_uint64_t value)
{
   int shift;
   if (value >= 0xFFFF0000 && value < 0x100000000) {
      arm64_movn32(*ptr,reg,~value,0);
      return;
   }
   if (value >= 0xFFFFFFFFFFFF0000) {
      arm64_movn64(*ptr,reg,~value,0);
      return;
   }
   if (value >= 0xFFFFFFFF00000000) {
      arm64_movn64(*ptr,reg,(~value)&0xFFFF,0);
      arm64_movk64(*ptr,reg,(value>>16)&0xFFFF,16);
      return;
   }
   arm64_movz64(*ptr,reg,value&0xFFFF,0);
   for (shift = 16; shift < 64; shift += 16)
      if (value >> shift)
         arm64_movk64(*ptr,reg,(value>>shift)&0xFFFF,shift);
}


/* Set the Instruction Address (IA) register */
static void ppc32_set_ia(u_char **ptr,m_uint32_t new_ia)
{
   ppc32_load_imm(ptr,ARM64_W9,new_ia);
   arm64_str32_imm(*ptr,ARM64_W9,ARM64_X28,OFFSET(cpu_ppc_t,ia));
}

/* Set the Link Register (LR) */
static void ppc32_set_lr(jit_op_t *iop,m_uint32_t new_lr)
{
   ppc32_load_imm(&iop->ob_ptr,ARM64_W9,new_lr);
   arm64_str32_imm(iop->ob_ptr,ARM64_W9,ARM64_X28,OFFSET(cpu_ppc_t,lr));
}

/*
 * Try to branch directly to the specified JIT block without returning to
 * main loop.
 */
static void ppc32_try_direct_far_jump(cpu_ppc_t *cpu,jit_op_t *iop,
                                      m_uint32_t new_ia)
{
   m_uint32_t new_page,ia_hash,ia_offset;
   u_char *test1,*test2,*test3,*test4;

   new_page = new_ia & PPC32_MIN_PAGE_MASK;

   ia_offset = (new_ia & PPC32_MIN_PAGE_IMASK) >> 2;
   /* Get JIT block info in x9 */
#if DYNAMIPS_ARM64_STABLE == 1
   ia_hash = ppc32_jit_get_ia_hash(new_ia);
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X9,ARM64_X28,OFFSET(cpu_ppc_t,exec_blk_map));
#else
   ia_hash = ppc32_jit_get_virt_hash(new_ia);
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X9,ARM64_X28,OFFSET(cpu_ppc_t,tcb_virt_hash));
#endif

   if (ia_hash * sizeof(void *) <= MAX_LD64_IMM_DISP) {
      arm64_ldr64_imm(iop->ob_ptr,ARM64_X9,ARM64_X9,ia_hash*sizeof(void *));
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_X25,ia_hash);
      arm64_ldr64_reg(iop->ob_ptr,ARM64_X9,ARM64_X9,ARM64_X25,ARM64_M_LSL,3);
   }

   /* no JIT block found ? */
   test1 = iop->ob_ptr;
   arm64_cbz64(iop->ob_ptr,ARM64_X9,0);

   /* Check block IA */
   ppc32_load_imm(&iop->ob_ptr,ARM64_X27,new_page);
   arm64_ldr32_imm(iop->ob_ptr,ARM64_X25,ARM64_X9,OFFSET(ppc32_jit_tcb_t,start_ia));
   arm64_cmp32_reg(iop->ob_ptr,ARM64_X25,ARM64_X27,ARM64_A_LSL,0);

   test2 = iop->ob_ptr;
   arm64_bcond(iop->ob_ptr,ARM64_CC_NE,0);

   /* Jump to the code */
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X9,ARM64_X9,OFFSET(ppc32_jit_tcb_t,jit_insn_ptr));

   test3 = iop->ob_ptr;
   arm64_cbz64(iop->ob_ptr,ARM64_X9,0);

   if (ia_offset * sizeof(void *) <= MAX_LD64_IMM_DISP) {
      arm64_ldr64_imm(iop->ob_ptr,ARM64_X9,ARM64_X9,ia_offset * sizeof(void *));
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_X25,ia_offset);
      arm64_ldr64_reg(iop->ob_ptr,ARM64_X9,ARM64_X9,ARM64_X25,ARM64_M_LSL,3);
   }

   test4 = iop->ob_ptr;
   arm64_cbz64(iop->ob_ptr,ARM64_X9,0);
   arm64_br(iop->ob_ptr,ARM64_X9);

   /* Returns to caller... */
   arm64_patch(test1,iop->ob_ptr);
   arm64_patch(test2,iop->ob_ptr);
   arm64_patch(test3,iop->ob_ptr);
   arm64_patch(test4,iop->ob_ptr);

   ppc32_set_ia(&iop->ob_ptr,new_ia);
   ppc32_jit_tcb_push_epilog(&iop->ob_ptr);
}

/* Set Jump */
static void ppc32_set_jump(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,jit_op_t *iop,
                           m_uint32_t new_ia,int local_jump)
{
   int return_to_caller = FALSE;
   u_char *jump_ptr;

#if 0
   if (cpu->sym_trace && !local_jump)
      return_to_caller = TRUE;
#endif

   if (!return_to_caller && ppc32_jit_tcb_local_addr(b,new_ia,&jump_ptr)) {
      ppc32_jit_tcb_record_patch(b,iop,iop->ob_ptr,new_ia);
      arm64_b(iop->ob_ptr,0);
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
   ppc32_set_jump(cpu,b,iop,b->start_ia + PPC32_MIN_PAGE_SIZE,FALSE);
   ppc32_op_insn_output(b,iop);

   jit_op_free_list(cpu->gen,op_list);
   cpu->gen->jit_op_current = NULL;
   /* The page is complete: make all emitted code visible */
   for(i=0;i<b->jit_chunk_pos;i++)
      if (b->jit_chunks[i])
         __builtin___clear_cache(b->jit_chunks[i]->ptr,b->jit_chunks[i]->ptr+PPC_JIT_BUFSIZE);
   __builtin___clear_cache(b->jit_buffer->ptr, b->jit_ptr);
}

/* Load a GPR into the specified host register */
static forced_inline void ppc32_load_gpr(u_char **ptr,u_int host_reg,
                                         u_int ppc_reg)
{
   arm64_ldr32_imm(*ptr,host_reg,ARM64_X28,REG_OFFSET(ppc_reg));
}

/* Store contents for a host register into a GPR register */
static forced_inline void ppc32_store_gpr(u_char **ptr,u_int ppc_reg,
                                          u_int host_reg)
{
   arm64_str32_imm(*ptr,host_reg,ARM64_X28,REG_OFFSET(ppc_reg));
}

/*
 * Update CR from NZCV
 */
static void ppc32_update_cr(ppc32_jit_tcb_t *b,int field,int is_signed)
{
   /* Get status bits from NZCV */
   ppc32_load_imm(&b->jit_ptr,ARM64_X9,2);
   arm64_csinc64(b->jit_ptr,ARM64_X9,ARM64_XZR,ARM64_X9,is_signed ? ARM64_CC_GE : ARM64_CC_CS);
   arm64_csinc64(b->jit_ptr,ARM64_X9,ARM64_XZR,ARM64_X9,ARM64_CC_EQ);
   arm64_cinc64(b->jit_ptr,ARM64_X9,ARM64_X9,is_signed ? ARM64_CC_GE : ARM64_CC_CS);
   arm64_lsl32(b->jit_ptr,ARM64_W9,ARM64_W9,1);

#if 0
   /* Check XER Summary of Overflow and report it */
   amd64_mov_reg_membase(b->jit_ptr,AMD64_RCX,
                         AMD64_R15,OFFSET(cpu_ppc_t,xer),4);
   amd64_alu_reg_imm(b->jit_ptr,X86_AND,AMD64_RCX,PPC32_XER_SO);
   amd64_shift_reg_imm(b->jit_ptr,X86_SHR,AMD64_RCX,(field << 2) + 3);
   amd64_alu_reg_reg(b->jit_ptr,X86_OR,AMD64_RDX,AMD64_RCX);
#endif

   /* Store modified CR field */
   arm64_str32_imm(b->jit_ptr,ARM64_W9,ARM64_X28,PPC32_CR_FIELD_OFFSET(field));
}

/*
 * Update CR0 from NZCV
 */
static void ppc32_update_cr0(ppc32_jit_tcb_t *b)
{
   ppc32_update_cr(b,0,TRUE);
}

/* Indicate registers modified by ppc32_update_cr() functions */
void ppc32_update_cr_set_altered_hreg(cpu_ppc_t *cpu)
{
}

/* Basic C call */
static forced_inline void ppc32_emit_basic_c_call(u_char **ptr,void *f)
{
   arm64_mov64(*ptr,ARM64_X26,ARM64_LR);
   ppc32_load_imm64(ptr,ARM64_X9,(m_uint64_t)f);
   arm64_blr(*ptr,ARM64_X9);
   arm64_mov64(*ptr,ARM64_LR,ARM64_X26);
}

/* Emit a simple call to a C function without any parameter */
static void ppc32_emit_c_call(ppc32_jit_tcb_t *b,jit_op_t *iop,void *f)
{
   ppc32_set_ia(&iop->ob_ptr,b->start_ia+(b->ppc_trans_pos << 2));
   ppc32_emit_basic_c_call(&iop->ob_ptr,f);
}

/* ======================================================================== */

/* Initialize register mapping */
void ppc32_jit_init_hreg_mapping(cpu_ppc_t *cpu)
{
   /* Scratch (except x9), then argument in reverse to decrease chance of conflicts */
   int avail_hregs[] = { ARM64_X10, ARM64_X11, ARM64_X12, ARM64_X13, ARM64_X14,
                         ARM64_X15, ARM64_X16, ARM64_X17, ARM64_X8, ARM64_X7,
                         ARM64_X6, ARM64_X5, ARM64_X4, ARM64_X3, ARM64_X2,
                         ARM64_X1, ARM64_X0, ARM64_X19, ARM64_X20, ARM64_X21,
                         ARM64_X22, ARM64_X23, ARM64_X24, -1 };
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
static int ppc32_jit_get_tmp_hreg(cpu_ppc_t *cpu)
{
   return(ARM64_X9);
}

/* ======================================================================== */
/* JIT operations (specific to target CPU).                                 */
/* ======================================================================== */

/* INSN_OUTPUT */
void ppc32_op_insn_output(ppc32_jit_tcb_t *b,jit_op_t *op)
{
   op->ob_final = b->jit_ptr;
   memcpy(b->jit_ptr,op->ob_data,op->ob_ptr - op->ob_data);
   b->jit_ptr += op->ob_ptr - op->ob_data;
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
   if (op->param[0] != JIT_OP_INV_REG)
      ppc32_update_cr(b,op->param[0],op->param[1]);
}

/* MOVE_HOST_REG: p[0] = %host_dst_reg, p[1] = %host_src_reg */
void ppc32_op_move_host_reg(ppc32_jit_tcb_t *b,jit_op_t *op)
{
   if ((op->param[0] != JIT_OP_INV_REG) && (op->param[1] != JIT_OP_INV_REG))
      arm64_mov32(b->jit_ptr,op->param[0],op->param[1]);
}

/* SET_HOST_REG_IMM32: p[0] = %host_reg, p[1] = imm32 */
void ppc32_op_set_host_reg_imm32(ppc32_jit_tcb_t *b,jit_op_t *op)
{
   if (op->param[0] != JIT_OP_INV_REG)
      ppc32_load_imm(&b->jit_ptr,op->param[0],op->param[1]);
}

/* ======================================================================== */

typedef void (*memop_fast_access)(jit_op_t *iop,int target);

/* Fast LBZ */
static void ppc32_memop_fast_lbz(jit_op_t *iop,int target)
{
   arm64_ldrb_reg(iop->ob_ptr,ARM64_W9,ARM64_X12,ARM64_W1,ARM64_M_UXTW,0);
   ppc32_store_gpr(&iop->ob_ptr,target,ARM64_W9);
}

/* Fast STB */
static void ppc32_memop_fast_stb(jit_op_t *iop,int target)
{
   ppc32_load_gpr(&iop->ob_ptr,ARM64_W9,target);
   arm64_strb_reg(iop->ob_ptr,ARM64_W9,ARM64_X12,ARM64_W1,ARM64_M_UXTW,0);
}

/* Fast LHA */
static void ppc32_memop_fast_lha(jit_op_t *iop,int target)
{
   arm64_ldrh_reg(iop->ob_ptr,ARM64_W9,ARM64_X12,ARM64_W1,ARM64_M_UXTW,0);
   arm64_rev16(iop->ob_ptr,ARM64_X9,ARM64_X9);
   arm64_sxth32(iop->ob_ptr,ARM64_X9,ARM64_X9);
   ppc32_store_gpr(&iop->ob_ptr,target,ARM64_W9);
}

/* Fast LHZ */
static void ppc32_memop_fast_lhz(jit_op_t *iop,int target)
{
   arm64_ldrh_reg(iop->ob_ptr,ARM64_W9,ARM64_X12,ARM64_W1,ARM64_M_UXTW,0);
   arm64_rev16(iop->ob_ptr,ARM64_X9,ARM64_X9);
   ppc32_store_gpr(&iop->ob_ptr,target,ARM64_W9);
}

/* Fast STH */
static void ppc32_memop_fast_sth(jit_op_t *iop,int target)
{
   ppc32_load_gpr(&iop->ob_ptr,ARM64_W9,target);
   arm64_rev16(iop->ob_ptr,ARM64_X9,ARM64_X9);
   arm64_strh_reg(iop->ob_ptr,ARM64_W9,ARM64_X12,ARM64_W1,ARM64_M_UXTW,0);
}

/* Fast LWZ */
static void ppc32_memop_fast_lwz(jit_op_t *iop,int target)
{
   arm64_ldr32_reg(iop->ob_ptr,ARM64_W9,ARM64_X12,ARM64_W1,ARM64_M_UXTW,0);
   arm64_rev32(iop->ob_ptr,ARM64_X9,ARM64_X9);
   ppc32_store_gpr(&iop->ob_ptr,target,ARM64_W9);
}

/* Fast STW */
static void ppc32_memop_fast_stw(jit_op_t *iop,int target)
{
   ppc32_load_gpr(&iop->ob_ptr,ARM64_W9,target);
   arm64_rev32(iop->ob_ptr,ARM64_X9,ARM64_X9);
   arm64_str32_reg(iop->ob_ptr,ARM64_W9,ARM64_X12,ARM64_W1,ARM64_M_UXTW,0);
}

/* Fast memory operation */
static void ppc32_emit_memop_fast(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                                  int write_op,int opcode,
                                  int base,int offset,int target,
                                  memop_fast_access op_handler,
                                  int update)
{
   m_uint32_t val = sign_extend(offset,16);
   u_char *test1,*test2,*p_exit;
   jit_op_t *iop;

   /*
    * Since an exception can be triggered, clear JIT state. This allows
    * to use branch target tag (we can directly branch on this instruction).
    */
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_BRANCH_TARGET);
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);

   iop = ppc32_op_emit_insn_output(cpu,5,"memop_fast");

   test2 = NULL;

#if DYNAMIPS_ARM64_STABLE == 0
   /* XXX */
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X9,ARM64_X28,OFFSET(cpu_ppc_t,mts_lookups));
   arm64_add64_imm(iop->ob_ptr,ARM64_X9,ARM64_X9,1,0);
   arm64_str64_imm(iop->ob_ptr,ARM64_X9,ARM64_X28,OFFSET(cpu_ppc_t,mts_lookups));
#endif

   /* X1 = GPR[base] + sign-extended offset */
   if (update || base) {
      arm64_ldr32_imm(iop->ob_ptr,ARM64_W1,ARM64_X28,REG_OFFSET(base));
      if (val <= 4095) {
         arm64_add32_imm(iop->ob_ptr,ARM64_W1,ARM64_W1,val,0);
      } else if (-val <= 4095) {
         arm64_sub32_imm(iop->ob_ptr,ARM64_W1,ARM64_W1,-val,0);
      } else {
         ppc32_load_imm(&iop->ob_ptr,ARM64_W9,val);
         arm64_add32_reg(iop->ob_ptr,ARM64_W1,ARM64_W1,ARM64_W9,ARM64_A_LSL,0);
      }
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W1,val);
   }
   if (update)
      arm64_mov32(iop->ob_ptr,ARM64_W27,ARM64_W1);

   /* W9 = mts32_entry index */
#if DYNAMIPS_ARM64_STABLE == 1
   arm64_lsr32(iop->ob_ptr,ARM64_W9,ARM64_W1,MTS32_HASH_SHIFT);
#else
   arm64_lsr32(iop->ob_ptr,ARM64_W9,ARM64_W1,MTS32_HASH_SHIFT1);
   arm64_eor32_reg(iop->ob_ptr,ARM64_W9,ARM64_W9,ARM64_W1,ARM64_A_LSR,MTS32_HASH_SHIFT2);
#endif
   arm64_and32_imm(iop->ob_ptr,ARM64_W9,ARM64_W9,arm64_encode_bitmasks(MTS32_HASH_MASK, 32));

   /* X0 = mts32 entry */
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X10,ARM64_X28,OFFSET(cpu_ppc_t,mts_cache[PPC32_MTS_DCACHE]));
   arm64_add64_reg(iop->ob_ptr,ARM64_X10,ARM64_X10,ARM64_X9,ARM64_A_LSL,5); /* TO FIX */

   /* Compare virtual page address (w11 = vpage) */
   arm64_and32_imm(iop->ob_ptr,ARM64_W11,ARM64_W1,arm64_encode_bitmasks(PPC32_MIN_PAGE_MASK, 32));

   arm64_ldr32_imm(iop->ob_ptr,ARM64_W12,ARM64_X10,OFFSET(mts32_entry_t,gvpa));
   arm64_cmp32_reg(iop->ob_ptr,ARM64_W12,ARM64_W11,ARM64_A_LSL,0);
   test1 = iop->ob_ptr;
   arm64_bcond(iop->ob_ptr,ARM64_CC_NE,0);

   /* Test if we are writing to a COW page */
   if (write_op) {
      arm64_ldr32_imm(iop->ob_ptr,ARM64_W12,ARM64_X10,OFFSET(mts32_entry_t,flags));
      arm64_tst64_imm(iop->ob_ptr,ARM64_X12,arm64_encode_bitmasks(MTS_FLAG_COW|MTS_FLAG_EXEC, 64));
      test2 = iop->ob_ptr;
      arm64_bcond(iop->ob_ptr,ARM64_CC_NE,0);
   }

   /* W1 = offset in page, X12 = Host Page Address */
   arm64_and32_imm(iop->ob_ptr,ARM64_W1,ARM64_W1,arm64_encode_bitmasks(PPC32_MIN_PAGE_IMASK, 32));
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X12,ARM64_X10,OFFSET(mts32_entry_t,hpa));

   /* Memory access */
   op_handler(iop,target);

   if (update)
      ppc32_store_gpr(&iop->ob_ptr,base,ARM64_W27);

   p_exit = iop->ob_ptr;
   arm64_b(iop->ob_ptr,0);

   /* === Slow lookup === */
   arm64_patch(test1,iop->ob_ptr);
   if (test2)
      arm64_patch(test2,iop->ob_ptr);

   /* Save IA for exception handling */
   ppc32_set_ia(&iop->ob_ptr,b->start_ia+(b->ppc_trans_pos << 2));

   /* X2 = target register */
   ppc32_load_imm(&iop->ob_ptr,ARM64_X2,target);

   /* X0 = CPU instance */
   arm64_mov64(iop->ob_ptr,ARM64_X0,ARM64_X28);

   /* Call memory access function */
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X9,ARM64_X28,MEMOP_OFFSET(opcode));
   arm64_mov64(iop->ob_ptr,ARM64_X26,ARM64_LR);
   arm64_blr(iop->ob_ptr,ARM64_X9);
   arm64_mov64(iop->ob_ptr,ARM64_LR,ARM64_X26);

   if (update)
      ppc32_store_gpr(&iop->ob_ptr,base,ARM64_W27);

   arm64_patch(p_exit,iop->ob_ptr);
}

/* Fast memory operation (indexed) */
static void ppc32_emit_memop_fast_idx(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                                      int write_op,int opcode,
                                      int ra,int rb,int target,
                                      memop_fast_access op_handler,
                                      int update)
{
   u_char *test1,*test2,*p_exit;
   jit_op_t *iop;

   /*
    * Since an exception can be triggered, clear JIT state. This allows
    * to use branch target tag (we can directly branch on this instruction).
    */
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_BRANCH_TARGET);
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);

   iop = ppc32_op_emit_insn_output(cpu,5,"memop_fast_idx");

   test2 = NULL;

#if DYNAMIPS_ARM64_STABLE == 0
   /* XXX */
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X9,ARM64_X28,OFFSET(cpu_ppc_t,mts_lookups));
   arm64_add64_imm(iop->ob_ptr,ARM64_X9,ARM64_X9,1,0);
   arm64_str64_imm(iop->ob_ptr,ARM64_X9,ARM64_X28,OFFSET(cpu_ppc_t,mts_lookups));
#endif

   ppc32_load_gpr(&iop->ob_ptr,ARM64_W1,rb);

   /* X1 = GPR[base] + sign-extended offset */
   if (update || ra) {
      ppc32_load_gpr(&iop->ob_ptr,ARM64_W9,ra);
      arm64_add32_reg(iop->ob_ptr,ARM64_W1,ARM64_W1,ARM64_W9,ARM64_A_LSL,0);
   }

   if (update)
      arm64_mov32(iop->ob_ptr,ARM64_W27,ARM64_W1);

   /* W9 = mts32_entry index */
#if DYNAMIPS_ARM64_STABLE == 1
   arm64_lsr32(iop->ob_ptr,ARM64_W9,ARM64_W1,MTS32_HASH_SHIFT);
#else
   arm64_lsr32(iop->ob_ptr,ARM64_W9,ARM64_W1,MTS32_HASH_SHIFT1);
   arm64_eor32_reg(iop->ob_ptr,ARM64_W9,ARM64_W9,ARM64_W1,ARM64_A_LSR,MTS32_HASH_SHIFT2);
#endif
   arm64_and32_imm(iop->ob_ptr,ARM64_W9,ARM64_W9,arm64_encode_bitmasks(MTS32_HASH_MASK, 32));

   /* X0 = mts32 entry */
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X10,ARM64_X28,OFFSET(cpu_ppc_t,mts_cache[PPC32_MTS_DCACHE]));
   arm64_add64_reg(iop->ob_ptr,ARM64_X10,ARM64_X10,ARM64_X9,ARM64_A_LSL,5); /* TO FIX */

   /* Compare virtual page address (w11 = vpage) */
   arm64_and32_imm(iop->ob_ptr,ARM64_W11,ARM64_W1,arm64_encode_bitmasks(PPC32_MIN_PAGE_MASK, 32));

   arm64_ldr32_imm(iop->ob_ptr,ARM64_W12,ARM64_X10,OFFSET(mts32_entry_t,gvpa));
   arm64_cmp32_reg(iop->ob_ptr,ARM64_W12,ARM64_W11,ARM64_A_LSL,0);
   test1 = iop->ob_ptr;
   arm64_bcond(iop->ob_ptr,ARM64_CC_NE,0);

   /* Test if we are writing to a COW page */
   if (write_op) {
      arm64_ldr32_imm(iop->ob_ptr,ARM64_W12,ARM64_X10,OFFSET(mts32_entry_t,flags));
      arm64_tst64_imm(iop->ob_ptr,ARM64_X12,arm64_encode_bitmasks(MTS_FLAG_COW|MTS_FLAG_EXEC, 64));
      test2 = iop->ob_ptr;
      arm64_bcond(iop->ob_ptr,ARM64_CC_NE,0);
   }

   /* W1 = offset in page, X12 = Host Page Address */
   arm64_and32_imm(iop->ob_ptr,ARM64_W1,ARM64_W1,arm64_encode_bitmasks(PPC32_MIN_PAGE_IMASK, 32));
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X12,ARM64_X10,OFFSET(mts32_entry_t,hpa));

   /* Memory access */
   op_handler(iop,target);

   if (update)
      ppc32_store_gpr(&iop->ob_ptr,ra,ARM64_W27);

   p_exit = iop->ob_ptr;
   arm64_b(iop->ob_ptr,0);

   /* === Slow lookup === */
   arm64_patch(test1,iop->ob_ptr);
   if (test2)
      arm64_patch(test2,iop->ob_ptr);

   /* Save IA for exception handling */
   ppc32_set_ia(&iop->ob_ptr,b->start_ia+(b->ppc_trans_pos << 2));

   /* X2 = target register */
   ppc32_load_imm(&iop->ob_ptr,ARM64_X2,target);

   /* X0 = CPU instance */
   arm64_mov64(iop->ob_ptr,ARM64_X0,ARM64_X28);

   /* Call memory access function */
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X9,ARM64_X28,MEMOP_OFFSET(opcode));
   arm64_mov64(iop->ob_ptr,ARM64_X26,ARM64_LR);
   arm64_blr(iop->ob_ptr,ARM64_X9);
   arm64_mov64(iop->ob_ptr,ARM64_LR,ARM64_X26);

   if (update)
      ppc32_store_gpr(&iop->ob_ptr,ra,ARM64_W27);

   arm64_patch(p_exit,iop->ob_ptr);
}

/* Fast STMW */
static void ppc32_memop_fast_stmw(jit_op_t *iop,int target)
{
   int i;
   m_uint32_t offs = 0;
   for (i=target;i<32;i++) {
      ppc32_load_gpr(&iop->ob_ptr,ARM64_W9,i);
      arm64_rev32(iop->ob_ptr,ARM64_X9,ARM64_X9);
      arm64_str32_imm(iop->ob_ptr,ARM64_W9,ARM64_X12,offs);
      offs += 4;
   }
}

/* Fast LMW */
static void ppc32_memop_fast_lmw(jit_op_t *iop,int target)
{
   int i;
   m_uint32_t offs = 0;
   for (i=target;i<32;i++) {
      arm64_ldr32_imm(iop->ob_ptr,ARM64_W9,ARM64_X12,offs);
      arm64_rev32(iop->ob_ptr,ARM64_X9,ARM64_X9);
      ppc32_store_gpr(&iop->ob_ptr,i,ARM64_W9);
      offs += 4;
   }
}

static void ppc32_stmw_fallback(cpu_ppc_t *cpu, m_uint32_t addr, int reg)
{
   int i;
   for(i=reg;i<=31;i++) {
      cpu->mem_op_fn[PPC_MEMOP_STW](cpu,addr,i);
      addr += sizeof(m_uint32_t);
   }
}

static void ppc32_lmw_fallback(cpu_ppc_t *cpu, m_uint32_t addr, int reg)
{
   int i;
   for(i=reg;i<=31;i++) {
      cpu->mem_op_fn[PPC_MEMOP_LWZ](cpu,addr,i);
      addr += sizeof(m_uint32_t);
   }
}

typedef void (*mem_mult_fallback)(cpu_ppc_t *cpu, m_uint32_t addr, int reg);

/* Fast memory operation */
static void ppc32_emit_memop_fast_mult(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                                       int write_op, int base,int offset,int target,
                                       memop_fast_access op_handler,
                                       mem_mult_fallback fallback)
{
   m_uint32_t val = sign_extend(offset,16);
   u_char *test1,*test2,*test3,*p_exit;
   jit_op_t *iop;

   /*
    * Since an exception can be triggered, clear JIT state. This allows
    * to use branch target tag (we can directly branch on this instruction).
    */
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_BRANCH_TARGET);
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);

   iop = ppc32_op_emit_insn_output(cpu,6,"memop_fast_mult");

   test2 = NULL;

#if DYNAMIPS_ARM64_STABLE == 0
   /* XXX */
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X9,ARM64_X28,OFFSET(cpu_ppc_t,mts_lookups));
   arm64_add64_imm(iop->ob_ptr,ARM64_X9,ARM64_X9,1,0);
   arm64_str64_imm(iop->ob_ptr,ARM64_X9,ARM64_X28,OFFSET(cpu_ppc_t,mts_lookups));
#endif

   /* X1 = GPR[base] + sign-extended offset */
   if (base) {
      arm64_ldr32_imm(iop->ob_ptr,ARM64_W1,ARM64_X28,REG_OFFSET(base));
      if (val <= 4095) {
         arm64_add32_imm(iop->ob_ptr,ARM64_W1,ARM64_W1,val,0);
      } else if (-val <= 4095) {
         arm64_sub32_imm(iop->ob_ptr,ARM64_W1,ARM64_W1,-val,0);
      } else {
         ppc32_load_imm(&iop->ob_ptr,ARM64_W9,val);
         arm64_add32_reg(iop->ob_ptr,ARM64_W1,ARM64_W1,ARM64_W9,ARM64_A_LSL,0);
      }
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W1,val);
   }

   /* check if we are crossing a page boundary */
   arm64_and32_imm(iop->ob_ptr,ARM64_W9,ARM64_W1,arm64_encode_bitmasks(0xFFF, 32));
   arm64_cmp32_imm(iop->ob_ptr,ARM64_W9,4096-(32-target)*4,0);
   test3 = iop->ob_ptr;
   arm64_bcond(iop->ob_ptr,ARM64_CC_HI,0);

   /* W9 = mts32_entry index */
#if DYNAMIPS_ARM64_STABLE == 1
   arm64_lsr32(iop->ob_ptr,ARM64_W9,ARM64_W1,MTS32_HASH_SHIFT);
#else
   arm64_lsr32(iop->ob_ptr,ARM64_W9,ARM64_W1,MTS32_HASH_SHIFT1);
   arm64_eor32_reg(iop->ob_ptr,ARM64_W9,ARM64_W9,ARM64_W1,ARM64_A_LSR,MTS32_HASH_SHIFT2);
#endif
   arm64_and32_imm(iop->ob_ptr,ARM64_W9,ARM64_W9,arm64_encode_bitmasks(MTS32_HASH_MASK, 32));

   /* X0 = mts32 entry */
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X10,ARM64_X28,OFFSET(cpu_ppc_t,mts_cache[PPC32_MTS_DCACHE]));
   arm64_add64_reg(iop->ob_ptr,ARM64_X10,ARM64_X10,ARM64_X9,ARM64_A_LSL,5); /* TO FIX */

   /* Compare virtual page address (w11 = vpage) */
   arm64_and32_imm(iop->ob_ptr,ARM64_W11,ARM64_W1,arm64_encode_bitmasks(PPC32_MIN_PAGE_MASK, 32));

   arm64_ldr32_imm(iop->ob_ptr,ARM64_W12,ARM64_X10,OFFSET(mts32_entry_t,gvpa));
   arm64_cmp32_reg(iop->ob_ptr,ARM64_W12,ARM64_W11,ARM64_A_LSL,0);
   test1 = iop->ob_ptr;
   arm64_bcond(iop->ob_ptr,ARM64_CC_NE,0);

   /* Test if we are writing to a COW page */
   if (write_op) {
      arm64_ldr32_imm(iop->ob_ptr,ARM64_W12,ARM64_X10,OFFSET(mts32_entry_t,flags));
      arm64_tst64_imm(iop->ob_ptr,ARM64_X12,arm64_encode_bitmasks(MTS_FLAG_COW|MTS_FLAG_EXEC, 64));
      test2 = iop->ob_ptr;
      arm64_bcond(iop->ob_ptr,ARM64_CC_NE,0);
   }

   /* W1 = offset in page, X12 = Host Page Address */
   arm64_and32_imm(iop->ob_ptr,ARM64_W1,ARM64_W1,arm64_encode_bitmasks(PPC32_MIN_PAGE_IMASK, 32));
   arm64_ldr64_imm(iop->ob_ptr,ARM64_X12,ARM64_X10,OFFSET(mts32_entry_t,hpa));
   arm64_add64_reg(iop->ob_ptr,ARM64_X12,ARM64_X12,ARM64_W1,ARM64_A_LSL,0);

   /* Memory access */
   op_handler(iop,target);

   p_exit = iop->ob_ptr;
   arm64_b(iop->ob_ptr,0);

   /* === Slow lookup === */
   arm64_patch(test1,iop->ob_ptr);
   if (test2)
      arm64_patch(test2,iop->ob_ptr);
   arm64_patch(test3,iop->ob_ptr);

   /* Save IA for exception handling */
   ppc32_set_ia(&iop->ob_ptr,b->start_ia+(b->ppc_trans_pos << 2));

   /* X2 = target register */
   ppc32_load_imm(&iop->ob_ptr,ARM64_X2,target);

   /* X0 = CPU instance */
   arm64_mov64(iop->ob_ptr,ARM64_X0,ARM64_X28);

   /* Call memory access function */
   ppc32_emit_basic_c_call(&iop->ob_ptr,fallback);

   arm64_patch(p_exit,iop->ob_ptr);
}

/* Emit unhandled instruction code */
static int ppc32_emit_unknown(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b,
                              ppc_insn_t opcode)
{
   u_char *test1;
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,3,"unknown");

   /* Update IA */
   ppc32_set_ia(&iop->ob_ptr,b->start_ia+(b->ppc_trans_pos << 2));

   /* Fallback to non-JIT mode */
   arm64_mov64(iop->ob_ptr,ARM64_X0,ARM64_X28);
   ppc32_load_imm(&iop->ob_ptr,ARM64_X1,opcode);

   ppc32_emit_c_call(b,iop,ppc32_exec_single_insn_ext);

   test1 = iop->ob_ptr;
   arm64_cbz32(iop->ob_ptr,ARM64_W0,0);
   ppc32_jit_tcb_push_epilog(&iop->ob_ptr);

   arm64_patch(test1,iop->ob_ptr);

   /* Signal this as an EOB to reset JIT state */
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   return(0);
}

/* Virtual Breakpoint */
void ppc32_emit_breakpoint(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b)
{
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,2,"breakpoint");

   arm64_mov64(iop->ob_ptr,ARM64_X0,ARM64_X28);

   ppc32_emit_c_call(b,iop,ppc32_run_breakpoint);

   /* Signal this as an EOB to to reset JIT state */
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
}

/* Increment the number of executed instructions (performance debugging) */
void ppc32_inc_perf_counter(cpu_ppc_t *cpu)
{
   jit_op_t *iop;

   iop = ppc32_op_emit_insn_output(cpu,1,"perf_cnt");
   arm64_ldr32_imm(iop->ob_ptr,ARM64_W9,ARM64_X28,OFFSET(cpu_ppc_t,perf_counter));
   arm64_add32_imm(iop->ob_ptr,ARM64_W9,ARM64_W9,1,0);
   arm64_str32_imm(iop->ob_ptr,ARM64_W9,ARM64_X28,OFFSET(cpu_ppc_t,perf_counter));
}

/* ======================================================================== */

/* BLR - Branch to Link Register */
DECLARE_INSN(BLR)
{
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"blr");

   iop = ppc32_op_emit_insn_output(cpu,2,"blr");

   arm64_ldr32_imm(iop->ob_ptr,ARM64_W9,ARM64_X28,OFFSET(cpu_ppc_t,lr));
   arm64_str32_imm(iop->ob_ptr,ARM64_W9,ARM64_X28,OFFSET(cpu_ppc_t,ia));

   /* set the return address */
   if (insn & 1)
      ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));

   ppc32_jit_tcb_push_epilog(&iop->ob_ptr);
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1) << 2));

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* BCTR - Branch to Count Register */
DECLARE_INSN(BCTR)
{
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"bctr");

   iop = ppc32_op_emit_insn_output(cpu,2,"bctr");

   arm64_ldr32_imm(iop->ob_ptr,ARM64_W9,ARM64_X28,OFFSET(cpu_ppc_t,ctr));
   arm64_str32_imm(iop->ob_ptr,ARM64_W9,ARM64_X28,OFFSET(cpu_ppc_t,ia));

   /* set the return address */
   if (insn & 1)
      ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));

   ppc32_jit_tcb_push_epilog(&iop->ob_ptr);
   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1) << 2));

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MFLR - Move From Link Register */
DECLARE_INSN(MFLR)
{
   int rd = bits(insn,21,25);
   int hreg_rd;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mflr");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);
   iop = ppc32_op_emit_insn_output(cpu,1,"mflr");

   arm64_ldr32_imm(iop->ob_ptr,hreg_rd,ARM64_X28,OFFSET(cpu_ppc_t,lr));
   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MTLR - Move To Link Register */
DECLARE_INSN(MTLR)
{
   int rs = bits(insn,21,25);
   int hreg_rs;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mtlr");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,1,"mtlr");
   arm64_str32_imm(iop->ob_ptr,hreg_rs,ARM64_X28,OFFSET(cpu_ppc_t,lr));
   return(0);
}

/* MFCTR - Move From Counter Register */
DECLARE_INSN(MFCTR)
{
   int rd = bits(insn,21,25);
   int hreg_rd;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mfctr");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   iop = ppc32_op_emit_insn_output(cpu,1,"mfctr");

   arm64_ldr32_imm(iop->ob_ptr,hreg_rd,ARM64_X28,OFFSET(cpu_ppc_t,ctr));
   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MTCTR - Move To Counter Register */
DECLARE_INSN(MTCTR)
{
   int rs = bits(insn,21,25);
   int hreg_rs;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mtctr");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,1,"mtctr");

   arm64_str32_imm(iop->ob_ptr,hreg_rs,ARM64_X28,OFFSET(cpu_ppc_t,ctr));

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MFTBU - Move from Time Base (Up) */
DECLARE_INSN(MFTBU)
{
   int rd = bits(insn,21,25);
   int hreg_rd;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mftbu");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   iop = ppc32_op_emit_insn_output(cpu,1,"mftbu");
   arm64_ldr32_imm(iop->ob_ptr,hreg_rd,ARM64_X28,OFFSET(cpu_ppc_t,tb)+4);
   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

#define PPC32_TB_INCREMENT  50

/* MFTBL - Move from Time Base (Lo) */
DECLARE_INSN(MFTBL)
{
   int rd = bits(insn,21,25);
   int hreg_rd;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mftbl");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   iop = ppc32_op_emit_insn_output(cpu,3,"mftbl");

   arm64_ldr64_imm(iop->ob_ptr,hreg_rd,ARM64_X28,OFFSET(cpu_ppc_t,perf_counter));
   arm64_add64_imm(iop->ob_ptr,hreg_rd,hreg_rd,PPC32_TB_INCREMENT,0);
   arm64_str64_imm(iop->ob_ptr,hreg_rd,ARM64_X28,OFFSET(cpu_ppc_t,perf_counter));

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* ADD */
DECLARE_INSN(ADD)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rd,hreg_ra,hreg_rb;
   jit_op_t *iop;

   /* $rd = $ra + $rb */
   ppc32_jit_start_hreg_seq(cpu,"add");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,2,"add");

   arm64_adds32_reg(iop->ob_ptr,hreg_rd,hreg_ra,hreg_rb,ARM64_A_LSL,0);

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* ADDC */
DECLARE_INSN(ADDC)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rd,hreg_ra,hreg_rb,hreg_t0;
   jit_op_t *iop;

   /* $rd = $ra + $rb */
   ppc32_jit_start_hreg_seq(cpu,"addc");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   /* store the carry flag */
   hreg_t0 = ppc32_jit_get_tmp_hreg(cpu);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,2,"addc");

   arm64_adds32_reg(iop->ob_ptr,hreg_rd,hreg_ra,hreg_rb,ARM64_A_LSL,0);

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   /* store the carry flag */
   arm64_cset64(iop->ob_ptr,hreg_t0,ARM64_CC_CS);
   arm64_str32_imm(iop->ob_ptr,hreg_t0,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));

   if (insn & 1) {
      ppc32_op_emit_update_flags(cpu,0,TRUE);
   }

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* ADDE - Add Extended */
DECLARE_INSN(ADDE)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_ra,hreg_rb,hreg_rd,hreg_t1;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"adde");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   hreg_t1 = ppc32_jit_get_tmp_hreg(cpu);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,3,"adde");

   arm64_ldr32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));
   arm64_cmp32_imm(iop->ob_ptr,hreg_t1,1,0);
   arm64_adcs32_reg(iop->ob_ptr,hreg_rd,hreg_ra,hreg_rb);

   arm64_cset64(iop->ob_ptr,hreg_t1,ARM64_CC_CS);
   arm64_str32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* ADDI - ADD Immediate */
DECLARE_INSN(ADDI)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint32_t tmp = sign_extend_32(imm,16);
   int hreg_rd,hreg_ra;
   jit_op_t *iop;

   /* $rd = $ra + imm */
   ppc32_jit_start_hreg_seq(cpu,"addi");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   if (ra != 0) {
      hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
      ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);

      iop = ppc32_op_emit_insn_output(cpu,2,"addi");

      if (tmp <= 4095) {
         arm64_add32_imm(iop->ob_ptr,hreg_rd,hreg_ra,tmp,0);
      } else if (-tmp <= 4095) {
         arm64_sub32_imm(iop->ob_ptr,hreg_rd,hreg_ra,-tmp,0);
      } else {
         ppc32_load_imm(&iop->ob_ptr,ARM64_W9,tmp);
         arm64_add32_reg(iop->ob_ptr,hreg_rd,hreg_ra,ARM64_W9,ARM64_A_LSL,0);
      }
   } else {
      iop = ppc32_op_emit_insn_output(cpu,1,"addi");
      ppc32_load_imm(&iop->ob_ptr,hreg_rd,tmp);
   }

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* ADDIC - ADD Immediate with Carry */
DECLARE_INSN(ADDIC)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint32_t tmp = sign_extend_32(imm,16);
   int hreg_rd,hreg_ra;
   jit_op_t *iop;

   /* $rd = $ra + imm */
   ppc32_jit_start_hreg_seq(cpu,"addic");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);

   iop = ppc32_op_emit_insn_output(cpu,1,"addic");

   if (tmp <= 4095) {
      arm64_adds32_imm(iop->ob_ptr,hreg_rd,hreg_ra,tmp,0);
   } else if (-tmp <= 4095) {
      arm64_subs32_imm(iop->ob_ptr,hreg_rd,hreg_ra,-tmp,0);
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W9,tmp);
      arm64_adds32_reg(iop->ob_ptr,hreg_rd,hreg_ra,ARM64_W9,ARM64_A_LSL,0);
   }
   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   arm64_cset64(iop->ob_ptr,ARM64_X9,ARM64_CC_CS);
   arm64_str32_imm(iop->ob_ptr,ARM64_W9,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* ADDIC. */
DECLARE_INSN(ADDIC_dot)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint32_t tmp = sign_extend_32(imm,16);
   int hreg_rd,hreg_ra;
   jit_op_t *iop;

   /* $rd = $ra + imm */
   ppc32_jit_start_hreg_seq(cpu,"addic.");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);

   iop = ppc32_op_emit_insn_output(cpu,1,"addic.");

   if (tmp <= 4095) {
      arm64_adds32_imm(iop->ob_ptr,hreg_rd,hreg_ra,tmp,0);
   } else if (-tmp <= 4095) {
      arm64_subs32_imm(iop->ob_ptr,hreg_rd,hreg_ra,-tmp,0);
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W9,tmp);
      arm64_adds32_reg(iop->ob_ptr,hreg_rd,hreg_ra,ARM64_W9,ARM64_A_LSL,0);
   }

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   arm64_cset64(iop->ob_ptr,ARM64_X9,ARM64_CC_CS);
   arm64_str32_imm(iop->ob_ptr,ARM64_W9,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));

   ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* ADDIS - ADD Immediate Shifted */
DECLARE_INSN(ADDIS)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);
   m_uint32_t tmp = imm << 16;
   int hreg_rd,hreg_ra;
   jit_op_t *iop;

   /* $rd = $ra + (imm << 16) */
   ppc32_jit_start_hreg_seq(cpu,"addis");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   if (ra != 0) {
      hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
      ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);

      iop = ppc32_op_emit_insn_output(cpu,1,"addis");
      ppc32_load_imm(&iop->ob_ptr,ARM64_W9,tmp);
      arm64_add32_reg(iop->ob_ptr,hreg_rd,hreg_ra,ARM64_W9,ARM64_A_LSL,0);
   } else {
      iop = ppc32_op_emit_insn_output(cpu,1,"addis");
      ppc32_load_imm(&iop->ob_ptr,hreg_rd,tmp);
   }

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* ADDZE */
DECLARE_INSN(ADDZE)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int hreg_rd,hreg_ra,hreg_t0;
   jit_op_t *iop;

   /* $rd = $ra + xer_ca + set_carry */
   ppc32_jit_start_hreg_seq(cpu,"addze");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_t0 = ppc32_jit_get_tmp_hreg(cpu);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);

   iop = ppc32_op_emit_insn_output(cpu,2,"addze");

   arm64_ldr32_imm(iop->ob_ptr,hreg_t0,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));

   arm64_adds32_reg(iop->ob_ptr,hreg_rd,hreg_ra,hreg_t0,ARM64_A_LSL,0);

   arm64_cset64(iop->ob_ptr,hreg_t0,ARM64_CC_CS);
   arm64_str32_imm(iop->ob_ptr,hreg_t0,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* AND */
DECLARE_INSN(AND)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rs,hreg_ra,hreg_rb;
   jit_op_t *iop;

   /* $ra = $rs & $rb */
   ppc32_jit_start_hreg_seq(cpu,"and");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,1,"and");

   arm64_ands32_reg(iop->ob_ptr,hreg_ra,hreg_rs,hreg_rb,ARM64_A_LSL,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1) {
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);
      ppc32_op_emit_update_flags(cpu,0,TRUE);
   }

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* ANDC */
DECLARE_INSN(ANDC)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rs,hreg_ra,hreg_rb;
   jit_op_t *iop;

   /* $ra = $rs & ~$rb */
   ppc32_jit_start_hreg_seq(cpu,"andc");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,1,"andc");

   arm64_bics32_reg(iop->ob_ptr,hreg_ra,hreg_rs,hreg_rb,ARM64_A_LSL,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1) {
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);
      ppc32_op_emit_update_flags(cpu,0,TRUE);
   }

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* AND Immediate */
DECLARE_INSN(ANDI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t tmp = imm;
   int hreg_rs,hreg_ra;
   jit_op_t *iop;
   m_uint32_t enc;

   /* $ra = $rs & imm */
   ppc32_jit_start_hreg_seq(cpu,"andi");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,2,"andi");

   enc = arm64_encode_bitmasks(tmp, 32);
   if (enc != -1) {
      arm64_ands32_imm(iop->ob_ptr,hreg_ra,hreg_rs,enc);
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W9,tmp);
      arm64_ands32_reg(iop->ob_ptr,hreg_ra,hreg_rs,ARM64_W9,ARM64_A_LSL,0);
   }
   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);
   ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* AND Immediate Shifted */
DECLARE_INSN(ANDIS)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);
   m_uint32_t tmp = imm << 16;
   int hreg_rs,hreg_ra;
   jit_op_t *iop;
   m_uint32_t enc;

   /* $ra = $rs & imm */
   ppc32_jit_start_hreg_seq(cpu,"andis");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,2,"andis");

   enc = arm64_encode_bitmasks(tmp, 32);
   if (enc != -1) {
      arm64_ands32_imm(iop->ob_ptr,hreg_ra,hreg_rs,enc);
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W9,tmp);
      arm64_ands32_reg(iop->ob_ptr,hreg_ra,hreg_rs,ARM64_W9,ARM64_A_LSL,0);
   }
   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);
   ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
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
   ppc32_set_jump(cpu,b,iop,new_ia,TRUE);

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

   /* compute the new ia */
   new_ia = sign_extend(offset << 2,26);
   ppc32_set_jump(cpu,b,iop,new_ia,TRUE);

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

   /* compute the new ia */
   new_ia = b->start_ia + (b->ppc_trans_pos << 2);
   new_ia += sign_extend(offset << 2,26);

   /* set the return address */
   ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));
   ppc32_set_jump(cpu,b,iop,new_ia,TRUE);

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

   /* compute the new ia */
   new_ia = sign_extend(offset << 2,26);

   /* set the return address */
   ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));
   ppc32_set_jump(cpu,b,iop,new_ia,TRUE);

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);
   ppc32_op_emit_branch_target(cpu,b,new_ia);
   ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1) << 2));
   return(0);
}

/* BC - Branch Conditional (Condition Check only) */
DECLARE_INSN(BCC)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   int bd = bits(insn,2,15);
   jit_op_t *iop;
   u_int cr_field,cr_bit;
   m_uint32_t new_ia;
   u_char *jump_ptr;
   int local_jump;
   int cond;

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_BRANCH_JUMP);

   iop = ppc32_op_emit_insn_output(cpu,5,"bcc");

   /* Get the wanted value for the condition bit */
   cond = (bo >> 3) & 0x1;

   /* Set the return address */
   if (insn & 1) {
      ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));
      ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1)<<2));
   }

   /* Compute the new ia */
   new_ia = sign_extend_32(bd << 2,16);
   if (!(insn & 0x02))
      new_ia += b->start_ia + (b->ppc_trans_pos << 2);

   /* Test the condition bit */
   cr_field = ppc32_get_cr_field(bi);
   cr_bit = ppc32_get_cr_bit(bi);

   ppc32_op_emit_require_flags(cpu,cr_field);

   arm64_ldr32_imm(iop->ob_ptr,ARM64_W9,ARM64_X28,PPC32_CR_FIELD_OFFSET(cr_field));

   local_jump = ppc32_jit_tcb_local_addr(b,new_ia,&jump_ptr);

   /*
    * Optimize the jump, depending if the destination is in the same
    * page or not.
    */
   if (local_jump) {
      jump_ptr = iop->ob_ptr;
      if (cond)
         arm64_tbz(iop->ob_ptr,ARM64_W9,cr_bit,0);
      else
         arm64_tbnz(iop->ob_ptr,ARM64_W9,cr_bit,0);
      ppc32_jit_tcb_record_patch(b,iop,iop->ob_ptr,new_ia);
      arm64_b(iop->ob_ptr,0);
      arm64_patch(jump_ptr,iop->ob_ptr);
   } else {
      jump_ptr = iop->ob_ptr;
      if (cond)
         arm64_tbz(iop->ob_ptr,ARM64_W9,cr_bit,0);
      else
         arm64_tbnz(iop->ob_ptr,ARM64_W9,cr_bit,0);
      ppc32_set_jump(cpu,b,iop,new_ia,TRUE);
      arm64_patch(jump_ptr,iop->ob_ptr);
   }

   ppc32_op_emit_branch_target(cpu,b,new_ia);
   return(0);
}

/* BC - Branch Conditional */
DECLARE_INSN(BC)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   int bd = bits(insn,2,15);
   int hreg_t0,hreg_t1;
   jit_op_t *iop;
   u_int cr_field,cr_bit;
   m_uint32_t new_ia;
   u_char *jump_ptr;
   int local_jump;
   int cond,ctr,dec_ctr;

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_BRANCH_JUMP);

   iop = ppc32_op_emit_insn_output(cpu,5,"bc");

   ppc32_jit_start_hreg_seq(cpu,"bc");
   hreg_t0 = ppc32_jit_alloc_hreg(cpu,-1);
   hreg_t1 = ppc32_jit_get_tmp_hreg(cpu);

   ppc32_op_emit_alter_host_reg(cpu,hreg_t0);

   /* Get the wanted value for the condition bit and CTR value */
   cond = (bo >> 3) & 0x1;
   ctr  = (bo >> 1) & 0x1;
   dec_ctr = !(bo & 0x04);

   /* Set the return address */
   if (insn & 1) {
      ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));
      ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1)<<2));
   }

   /* Compute the new ia */
   new_ia = sign_extend_32(bd << 2,16);
   if (!(insn & 0x02))
      new_ia += b->start_ia + (b->ppc_trans_pos << 2);

   /* Decrement the count register */
   if (dec_ctr) {
      arm64_ldr32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,OFFSET(cpu_ppc_t,ctr));
      arm64_subs32_imm(iop->ob_ptr,hreg_t1,hreg_t1,1,0);
      arm64_str32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,OFFSET(cpu_ppc_t,ctr));
      arm64_cset64(iop->ob_ptr,hreg_t0,ctr ? ARM64_CC_EQ : ARM64_CC_NE);
   }

   /* Test the condition bit */
   if (!((bo >> 4) & 0x01)) {
      cr_field = ppc32_get_cr_field(bi);
      cr_bit = ppc32_get_cr_bit(bi);

      ppc32_op_emit_require_flags(cpu,cr_field);

      arm64_ldr32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,PPC32_CR_FIELD_OFFSET(cr_field));
      arm64_tst64_imm(iop->ob_ptr,hreg_t1,arm64_encode_bitmasks(1 << cr_bit, 64));
      if (dec_ctr)
         arm64_csel64(iop->ob_ptr,hreg_t0,hreg_t0,ARM64_XZR,cond ? ARM64_CC_NE : ARM64_CC_EQ);
      else
         arm64_cset64(iop->ob_ptr,hreg_t0,cond ? ARM64_CC_NE : ARM64_CC_EQ);
   }

   local_jump = ppc32_jit_tcb_local_addr(b,new_ia,&jump_ptr);

   /*
    * Optimize the jump, depending if the destination is in the same
    * page or not.
    */
   if (local_jump) {
      ppc32_jit_tcb_record_patch(b,iop,iop->ob_ptr,new_ia);
      arm64_cbnz64(iop->ob_ptr,hreg_t0,0);
   } else {
      jump_ptr = iop->ob_ptr;
      arm64_cbz64(iop->ob_ptr,hreg_t0,0);
      ppc32_set_jump(cpu,b,iop,new_ia,TRUE);
      arm64_patch(jump_ptr,iop->ob_ptr);
   }

   ppc32_op_emit_branch_target(cpu,b,new_ia);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

static int emit_bcond_to_reg(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b, ppc_insn_t insn,
                             char *name, size_t reg_offs)
{
   int bo = bits(insn,21,25);
   int bi = bits(insn,16,20);
   int hreg_t0,hreg_t1;
   jit_op_t *iop;
   u_int cr_field,cr_bit;
   u_char *jump_ptr;
   int cond,ctr,dec_ctr;

   ppc32_jit_start_hreg_seq(cpu,name);
   hreg_t0 = ppc32_jit_alloc_hreg(cpu,-1);
   hreg_t1 = ppc32_jit_get_tmp_hreg(cpu);

   ppc32_op_emit_alter_host_reg(cpu,hreg_t0);

   iop = ppc32_op_emit_insn_output(cpu,5,name);

   /* Get the wanted value for the condition bit and CTR value */
   cond = (bo >> 3) & 0x1;
   ctr  = (bo >> 1) & 0x1;
   dec_ctr = !(bo & 0x04);

   /* Decrement the count register */
   if (dec_ctr) {
      arm64_ldr32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,OFFSET(cpu_ppc_t,ctr));
      arm64_subs32_imm(iop->ob_ptr,hreg_t1,hreg_t1,1,0);
      arm64_str32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,OFFSET(cpu_ppc_t,ctr));
      arm64_cset64(iop->ob_ptr,hreg_t0,ctr ? ARM64_CC_EQ : ARM64_CC_NE);
   }

   /* Test the condition bit */
   if (!((bo >> 4) & 0x01)) {
      cr_field = ppc32_get_cr_field(bi);
      cr_bit = ppc32_get_cr_bit(bi);

      ppc32_op_emit_require_flags(cpu,cr_field);

      arm64_ldr32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,PPC32_CR_FIELD_OFFSET(cr_field));
      arm64_tst64_imm(iop->ob_ptr,hreg_t1,arm64_encode_bitmasks(1 << cr_bit, 64));
      if (dec_ctr)
         arm64_csel64(iop->ob_ptr,hreg_t0,hreg_t0,ARM64_XZR,cond ? ARM64_CC_NE : ARM64_CC_EQ);
      else
         arm64_cset64(iop->ob_ptr,hreg_t0,cond ? ARM64_CC_NE : ARM64_CC_EQ);
   }

   /* Set the return address */
   arm64_ldr32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,reg_offs);

   if (insn & 1) {
      ppc32_set_lr(iop,b->start_ia + ((b->ppc_trans_pos+1) << 2));
      ppc32_op_emit_branch_target(cpu,b,b->start_ia+((b->ppc_trans_pos+1)<<2));
   }

   /* Branching */
   jump_ptr = iop->ob_ptr;
   arm64_cbz64(iop->ob_ptr,hreg_t0,0);

   arm64_and32_imm(iop->ob_ptr,hreg_t1,hreg_t1,arm64_encode_bitmasks(0xFFFFFFFC, 32));
   arm64_str32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,OFFSET(cpu_ppc_t,ia));
   ppc32_jit_tcb_push_epilog(&iop->ob_ptr);

   arm64_patch(jump_ptr,iop->ob_ptr);

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_EOB);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);

}

/* BCLR - Branch Conditional to Link register */
DECLARE_INSN(BCLR)
{
   return emit_bcond_to_reg(cpu, b, insn, "bclr", OFFSET(cpu_ppc_t,lr));
}

/* BCLR - Branch Conditional to Count register */
DECLARE_INSN(BCCTR)
{
   return emit_bcond_to_reg(cpu, b, insn, "bcctr", OFFSET(cpu_ppc_t,ctr));
}

/* CMP - Compare */
DECLARE_INSN(CMP)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_ra,hreg_rb;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"cmp");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,1,"cmp");

   arm64_cmp32_reg(iop->ob_ptr,hreg_ra,hreg_rb,ARM64_A_LSL,0);
   ppc32_op_emit_update_flags(cpu,rd,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* CMPI - Compare Immediate */
DECLARE_INSN(CMPI)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t tmp = sign_extend_32(imm,16);
   int hreg_ra;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"cmpi");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);

   iop = ppc32_op_emit_insn_output(cpu,1,"cmpi");

   if (tmp <= 4095) {
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,tmp,0);
   } else if (-tmp <= 4095) {
      arm64_cmn32_imm(iop->ob_ptr,hreg_ra,-tmp,0);
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W9,tmp);
      arm64_cmp32_reg(iop->ob_ptr,hreg_ra,ARM64_W9,ARM64_A_LSL,0);
   }
   ppc32_op_emit_update_flags(cpu,rd,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* CMPL - Compare Logical */
DECLARE_INSN(CMPL)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_ra,hreg_rb;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"cmpl");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,1,"cmpl");

   arm64_cmp32_reg(iop->ob_ptr,hreg_ra,hreg_rb,ARM64_A_LSL,0);
   ppc32_op_emit_update_flags(cpu,rd,FALSE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* CMPLI - Compare Immediate */
DECLARE_INSN(CMPLI)
{
   int rd = bits(insn,23,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);
   int hreg_ra;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"cmpli");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);

   iop = ppc32_op_emit_insn_output(cpu,1,"cmpli");

   if (imm <= 4095) {
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,imm,0);
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W9,imm);
      arm64_cmp32_reg(iop->ob_ptr,hreg_ra,ARM64_W9,ARM64_A_LSL,0);
   }
   ppc32_op_emit_update_flags(cpu,rd,FALSE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

typedef void (*creg_op_impl)(jit_op_t *iop, int hreg_t0, int hreg_t1);

static int emit_cr_op(cpu_ppc_t *cpu,ppc32_jit_tcb_t *b, ppc_insn_t insn,
                      char *name, creg_op_impl impl)
{
   int bd = bits(insn,21,25);
   int bb = bits(insn,16,20);
   int ba = bits(insn,11,15);
   int hreg_t0, hreg_t1;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,name);
   hreg_t0 = ppc32_jit_get_tmp_hreg(cpu);
   hreg_t1 = ppc32_jit_alloc_hreg(cpu,-1);
   ppc32_op_emit_alter_host_reg(cpu,hreg_t1);

   ppc32_op_emit_require_flags(cpu,ppc32_get_cr_field(ba));
   ppc32_op_emit_require_flags(cpu,ppc32_get_cr_field(bb));
   ppc32_op_emit_require_flags(cpu,ppc32_get_cr_field(bd));

   iop = ppc32_op_emit_insn_output(cpu,3,name);

   /* test $ba bit */
   arm64_ldr32_imm(iop->ob_ptr,hreg_t0,ARM64_X28,PPC32_CR_FIELD_OFFSET(ppc32_get_cr_field(ba)));
   arm64_ubfx32(iop->ob_ptr,hreg_t0,hreg_t0,ppc32_get_cr_bit(ba),1);

   /* test $bb bit */
   arm64_ldr32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,PPC32_CR_FIELD_OFFSET(ppc32_get_cr_field(bb)));
   arm64_ubfx32(iop->ob_ptr,hreg_t1,hreg_t1,ppc32_get_cr_bit(bb),1);

   /* result of OP between $ba and $bb */
   impl(iop, hreg_t0, hreg_t1);

   /* set/clear $bd bit depending on the result */
   arm64_ldr32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,PPC32_CR_FIELD_OFFSET(ppc32_get_cr_field(bd)));
   arm64_bfi32(iop->ob_ptr,hreg_t1,hreg_t0,ppc32_get_cr_bit(bd),1);
   arm64_str32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,PPC32_CR_FIELD_OFFSET(ppc32_get_cr_field(bd)));

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

static void crand_impl(jit_op_t *iop, int hreg_t0, int hreg_t1)
{
   arm64_and32_reg(iop->ob_ptr,hreg_t0,hreg_t0,hreg_t1,ARM64_A_LSL,0);
}

/* CRAND - Condition Register AND */
DECLARE_INSN(CRAND)
{
   return emit_cr_op(cpu, b, insn, "crand", crand_impl);
}

static void crandc_impl(jit_op_t *iop, int hreg_t0, int hreg_t1)
{
   arm64_bic32_reg(iop->ob_ptr,hreg_t0,hreg_t0,hreg_t1,ARM64_A_LSL,0);
}

/* CRANDC - Condition Register AND with Complement */
DECLARE_INSN(CRANDC)
{
   return emit_cr_op(cpu, b, insn, "crandc", crandc_impl);
}

static void creqv_impl(jit_op_t *iop, int hreg_t0, int hreg_t1)
{
   arm64_eon32_reg(iop->ob_ptr,hreg_t0,hreg_t0,hreg_t1,ARM64_A_LSL,0);
}

/* CREQV - Condition Register EQV */
DECLARE_INSN(CREQV)
{
   return emit_cr_op(cpu, b, insn, "creqv", creqv_impl);
}

static void crnand_impl(jit_op_t *iop, int hreg_t0, int hreg_t1)
{
   arm64_and32_reg(iop->ob_ptr,hreg_t0,hreg_t0,hreg_t1,ARM64_A_LSL,0);
   arm64_mvn32_reg(iop->ob_ptr,hreg_t0,hreg_t0,ARM64_A_LSL,0);
}

/* CRNAND - Condition Register NAND */
DECLARE_INSN(CRNAND)
{
   return emit_cr_op(cpu, b, insn, "crnand", crnand_impl);
}

static void crnor_impl(jit_op_t *iop, int hreg_t0, int hreg_t1)
{
   arm64_orr32_reg(iop->ob_ptr,hreg_t0,hreg_t0,hreg_t1,ARM64_A_LSL,0);
   arm64_mvn32_reg(iop->ob_ptr,hreg_t0,hreg_t0,ARM64_A_LSL,0);
}

/* CRNOR - Condition Register NOR */
DECLARE_INSN(CRNOR)
{
   return emit_cr_op(cpu, b, insn, "crnor", crnor_impl);
}

static void cror_impl(jit_op_t *iop, int hreg_t0, int hreg_t1)
{
   arm64_orr32_reg(iop->ob_ptr,hreg_t0,hreg_t0,hreg_t1,ARM64_A_LSL,0);
}

/* CROR - Condition Register OR */
DECLARE_INSN(CROR)
{
   return emit_cr_op(cpu, b, insn, "cror", cror_impl);
}

static void crorc_impl(jit_op_t *iop, int hreg_t0, int hreg_t1)
{
   arm64_orn32_reg(iop->ob_ptr,hreg_t0,hreg_t0,hreg_t1,ARM64_A_LSL,0);
}

/* CRORC - Condition Register OR with Complement */
DECLARE_INSN(CRORC)
{
   return emit_cr_op(cpu, b, insn, "crorc", crorc_impl);
}

static void crxor_impl(jit_op_t *iop, int hreg_t0, int hreg_t1)
{
   arm64_eor32_reg(iop->ob_ptr,hreg_t0,hreg_t0,hreg_t1,ARM64_A_LSL,0);
}

/* CRXOR - Condition Register XOR */
DECLARE_INSN(CRXOR)
{
   return emit_cr_op(cpu, b, insn, "crxor", crxor_impl);
}

/* DIVWU - Divide Word Unsigned */
DECLARE_INSN(DIVWU)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_ra,hreg_rb,hreg_rd;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"divwu");

   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   /* $rd = $ra / $rb */
   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,2,"divwu");

   arm64_udiv32(iop->ob_ptr,hreg_rd,hreg_ra,hreg_rb);

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_rd,0,0);

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* EQV */
DECLARE_INSN(EQV)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rs,hreg_ra,hreg_rb;
   jit_op_t *iop;

   /* $ra = ~($rs ^ $rb) */
   ppc32_jit_start_hreg_seq(cpu,"eqv");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,1,"eqv");

   arm64_eon32_reg(iop->ob_ptr,hreg_ra,hreg_rs,hreg_rb,ARM64_A_LSL,0);

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* EXTSB - Extend Sign Byte */
DECLARE_INSN(EXTSB)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int hreg_rs,hreg_ra;
   jit_op_t *iop;

   /* $ra = extsb($rs) */
   ppc32_jit_start_hreg_seq(cpu,"extsb");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,2,"extsb");

   arm64_sxtb32(iop->ob_ptr,hreg_ra,hreg_rs);

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* EXTSH - Extend Sign Word */
DECLARE_INSN(EXTSH)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int hreg_rs,hreg_ra;
   jit_op_t *iop;

   /* $ra = extsh($rs) */
   ppc32_jit_start_hreg_seq(cpu,"extsh");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,2,"extsh");

   arm64_sxth32(iop->ob_ptr,hreg_ra,hreg_rs);

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* LBZ - Load Byte and Zero */
DECLARE_INSN(LBZ)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LBZ,ra,offset,rs,
                         ppc32_memop_fast_lbz,0);
   return(0);
}

/* LBZU - Load Byte and Zero with Update */
DECLARE_INSN(LBZU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LBZ,ra,offset,rs,
                         ppc32_memop_fast_lbz,1);
   return(0);
}

/* LBZUX - Load Byte and Zero with Update Indexed */
DECLARE_INSN(LBZUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,0,PPC_MEMOP_LBZ,ra,rb,rs,
                             ppc32_memop_fast_lbz,1);
   return(0);
}

/* LBZX - Load Byte and Zero Indexed */
DECLARE_INSN(LBZX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,0,PPC_MEMOP_LBZ,ra,rb,rs,
                             ppc32_memop_fast_lbz,0);
   return(0);
}

/* LHA - Load Half-Word Algebraic */
DECLARE_INSN(LHA)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LHA,ra,offset,rs,
                         ppc32_memop_fast_lha,0);
   return(0);
}

/* LHAU - Load Half-Word Algebraic with Update */
DECLARE_INSN(LHAU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LHA,ra,offset,rs,
                         ppc32_memop_fast_lha,1);
   return(0);
}

/* LHAUX - Load Half-Word Algebraic with Update Indexed */
DECLARE_INSN(LHAUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,0,PPC_MEMOP_LHA,ra,rb,rs,
                             ppc32_memop_fast_lha,1);

   return(0);
}

/* LHAX - Load Half-Word Algebraic Indexed */
DECLARE_INSN(LHAX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,0,PPC_MEMOP_LHA,ra,rb,rs,
                             ppc32_memop_fast_lha,0);
   return(0);
}

/* LHZ - Load Half-Word and Zero */
DECLARE_INSN(LHZ)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LHZ,ra,offset,rs,
                         ppc32_memop_fast_lhz,0);
   return(0);
}

/* LHZU - Load Half-Word and Zero with Update */
DECLARE_INSN(LHZU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LHZ,ra,offset,rs,
                         ppc32_memop_fast_lhz,1);
   return(0);
}

/* LHZUX - Load Half-Word and Zero with Update Indexed */
DECLARE_INSN(LHZUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,0,PPC_MEMOP_LHZ,ra,rb,rs,
                             ppc32_memop_fast_lhz,1);

   return(0);
}

/* LHZX - Load Half-Word and Zero Indexed */
DECLARE_INSN(LHZX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,0,PPC_MEMOP_LHZ,ra,rb,rs,
                             ppc32_memop_fast_lhz,0);
   return(0);
}

/* LMW - Load Multiple Word */
DECLARE_INSN(LMW)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast_mult(cpu,b,0,ra,offset,rs,
                              ppc32_memop_fast_lmw,ppc32_lmw_fallback);

   return(0);
}

/* LWZ - Load Word and Zero */
DECLARE_INSN(LWZ)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LWZ,ra,offset,rs,
                         ppc32_memop_fast_lwz,0);
   return(0);
}

/* LWZU - Load Word and Zero with Update */
DECLARE_INSN(LWZU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,0,PPC_MEMOP_LWZ,ra,offset,rs,
                         ppc32_memop_fast_lwz,1);
   return(0);
}

/* LWZUX - Load Word and Zero with Update Indexed */
DECLARE_INSN(LWZUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,0,PPC_MEMOP_LWZ,ra,rb,rs,
                             ppc32_memop_fast_lwz,1);

   return(0);
}

/* LWZX - Load Word and Zero Indexed */
DECLARE_INSN(LWZX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,0,PPC_MEMOP_LWZ,ra,rb,rs,
                             ppc32_memop_fast_lwz,0);
   return(0);
}

/* MCRF - Move Condition Register Field */
DECLARE_INSN(MCRF)
{
   int rd = bits(insn,23,25);
   int rs = bits(insn,18,20);
   int hreg_t0;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mcrf");
   hreg_t0 = ppc32_jit_get_tmp_hreg(cpu);
   ppc32_op_emit_require_flags(cpu,rs);

   iop = ppc32_op_emit_insn_output(cpu,1,"mcrf");

   /* Load "rs" field in w9 */
   arm64_ldr32_imm(iop->ob_ptr,hreg_t0,ARM64_X28,PPC32_CR_FIELD_OFFSET(rs));

   /* Store it in "rd" field */
   arm64_str32_imm(iop->ob_ptr,hreg_t0,ARM64_X28,PPC32_CR_FIELD_OFFSET(rd));

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MFCR - Move from Condition Register */
DECLARE_INSN(MFCR)
{
   int rd = bits(insn,21,25);
   int hreg_rd,hreg_t0;
   jit_op_t *iop;
   int i;

   ppc32_jit_start_hreg_seq(cpu,"mfcr");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);
   hreg_t0 = ppc32_jit_get_tmp_hreg(cpu);
   ppc32_op_emit_require_flags(cpu,JIT_OP_PPC_ALL_FLAGS);

   iop = ppc32_op_emit_insn_output(cpu,3,"mfcr");

   for(i=0;i<8;i++) {
      /* load field in %w9 */
      arm64_ldr32_imm(iop->ob_ptr,hreg_t0,ARM64_X28,PPC32_CR_FIELD_OFFSET(i));
      arm64_bfi32(iop->ob_ptr,hreg_rd,hreg_t0,28-(i*4),4);
   }

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MFMSR - Move from Machine State Register */
DECLARE_INSN(MFMSR)
{
   int rd = bits(insn,21,25);
   int hreg_rd;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mfmsr");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   iop = ppc32_op_emit_insn_output(cpu,1,"mfmsr");
   arm64_ldr32_imm(iop->ob_ptr,hreg_rd,ARM64_X28,OFFSET(cpu_ppc_t,msr));
   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MFSR - Move From Segment Register */
DECLARE_INSN(MFSR)
{
   int rd = bits(insn,21,25);
   int sr = bits(insn,16,19);
   int hreg_rd;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mfsr");
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   iop = ppc32_op_emit_insn_output(cpu,1,"mfsr");
   arm64_ldr32_imm(iop->ob_ptr,hreg_rd,ARM64_X28,(OFFSET(cpu_ppc_t,sr) + (sr << 2)));
   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MTCRF - Move to Condition Register Fields */
DECLARE_INSN(MTCRF)
{
   int rs = bits(insn,21,25);
   int crm = bits(insn,12,19);
   int hreg_rs,hreg_t0;
   jit_op_t *iop;
   int i;

   ppc32_jit_start_hreg_seq(cpu,"mtcrf");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_t0 = ppc32_jit_get_tmp_hreg(cpu);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,4,"mtcrf");

   for(i=0;i<8;i++)
      if (crm & (1 << (7 - i))) {
         arm64_ubfx32(iop->ob_ptr,hreg_t0,hreg_rs,28-(i*4),4);
         arm64_str32_imm(iop->ob_ptr,hreg_t0,ARM64_X28,PPC32_CR_FIELD_OFFSET(i));
      }

   ppc32_op_emit_basic_opcode(cpu,JIT_OP_TRASH_FLAGS);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MULHW - Multiply High Word */
DECLARE_INSN(MULHW)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_ra,hreg_rb,hreg_rd;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mulhw");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   /* rd = hi(ra * rb) */
   iop = ppc32_op_emit_insn_output(cpu,2,"mulhw");
   arm64_smull(iop->ob_ptr,hreg_rd,hreg_ra,hreg_rb);
   arm64_lsr64(iop->ob_ptr,hreg_rd,hreg_rd,32);

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_rd,0,0);

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MULHWU - Multiply High Word Unsigned */
DECLARE_INSN(MULHWU)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_ra,hreg_rb,hreg_rd;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mulhwu");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   /* rd = hi(ra * rb) */
   iop = ppc32_op_emit_insn_output(cpu,2,"mulhwu");
   arm64_umull(iop->ob_ptr,hreg_rd,hreg_ra,hreg_rb);
   arm64_lsr64(iop->ob_ptr,hreg_rd,hreg_rd,32);

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_rd,0,0);

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MULLI - Multiply Low Immediate */
DECLARE_INSN(MULLI)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);
   int hreg_t0,hreg_ra,hreg_rd;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mulli");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);
   hreg_t0 = ppc32_jit_get_tmp_hreg(cpu);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);

   /* rd = lo(ra * imm) */
   iop = ppc32_op_emit_insn_output(cpu,2,"mulli");

   ppc32_load_imm(&iop->ob_ptr,hreg_t0,sign_extend_32(imm,16));
   arm64_smull(iop->ob_ptr,hreg_rd,hreg_ra,hreg_t0);
   arm64_ubfx64(iop->ob_ptr,hreg_rd,hreg_rd,0,32);
   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* MULLW - Multiply Low Word */
DECLARE_INSN(MULLW)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_ra,hreg_rb,hreg_rd;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"mullw");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   /* rd = lo(ra * rb) */
   iop = ppc32_op_emit_insn_output(cpu,2,"mullw");
   arm64_smull(iop->ob_ptr,hreg_rd,hreg_ra,hreg_rb);
   arm64_ubfx64(iop->ob_ptr,hreg_rd,hreg_rd,0,32);

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_rd,0,0);

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* NAND */
DECLARE_INSN(NAND)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rs,hreg_ra,hreg_rb;
   jit_op_t *iop;

   /* $ra = ~($rs & $rb) */
   ppc32_jit_start_hreg_seq(cpu,"nand");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,2,"nand");

   arm64_and32_reg(iop->ob_ptr,hreg_ra,hreg_rs,hreg_rb,ARM64_A_LSL,0);
   arm64_mvn32_reg(iop->ob_ptr,hreg_ra,hreg_ra,ARM64_A_LSL,0);

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* NEG */
DECLARE_INSN(NEG)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int hreg_rd,hreg_ra;
   jit_op_t *iop;

   /* $rd = neg($ra) */
   ppc32_jit_start_hreg_seq(cpu,"neg");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);

   iop = ppc32_op_emit_insn_output(cpu,1,"neg");

   arm64_negs32_reg(iop->ob_ptr,hreg_rd,hreg_ra,ARM64_A_LSL,0);

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* NOR */
DECLARE_INSN(NOR)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rs,hreg_ra,hreg_rb;
   jit_op_t *iop;

   /* $ra = ~($rs | $rb) */
   ppc32_jit_start_hreg_seq(cpu,"nor");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,2,"nor");

   arm64_orr32_reg(iop->ob_ptr,hreg_ra,hreg_rs,hreg_rb,ARM64_A_LSL,0);
   arm64_mvn32_reg(iop->ob_ptr,hreg_ra,hreg_ra,ARM64_A_LSL,0);

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* OR */
DECLARE_INSN(OR)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rs,hreg_ra,hreg_rb;
   jit_op_t *iop;

   /* $ra = $rs | $rb */
   ppc32_jit_start_hreg_seq(cpu,"or");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   /* special optimization for move/nop operation */
   if (rs == rb) {
      ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
      iop = ppc32_op_emit_insn_output(cpu,2,"or");

      if (ra != rs)
         arm64_mov32(iop->ob_ptr,hreg_ra,hreg_rs);

      if (insn & 1)
         arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);

      ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

      if (insn & 1)
         ppc32_op_emit_update_flags(cpu,0,TRUE);

      ppc32_jit_close_hreg_seq(cpu);
      return(0);
   }

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,2,"or");

   arm64_orr32_reg(iop->ob_ptr,hreg_ra,hreg_rs,hreg_rb,ARM64_A_LSL,0);
   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1) {
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);
      ppc32_op_emit_update_flags(cpu,0,TRUE);
   }

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* OR with Complement */
DECLARE_INSN(ORC)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rs,hreg_ra,hreg_rb;
   jit_op_t *iop;

   /* $ra = $rs & ~$rb */
   ppc32_jit_start_hreg_seq(cpu,"orc");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,1,"orc");

   arm64_orn32_reg(iop->ob_ptr,hreg_ra,hreg_rs,hreg_rb,ARM64_A_LSL,0);
   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1) {
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);
      ppc32_op_emit_update_flags(cpu,0,TRUE);
   }

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* OR Immediate */
DECLARE_INSN(ORI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t tmp = imm;
   int hreg_rs,hreg_ra;
   jit_op_t *iop;
   m_uint32_t enc;

   /* $ra = $rs | imm */
   ppc32_jit_start_hreg_seq(cpu,"ori");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,1,"ori");

   enc = arm64_encode_bitmasks(tmp, 32);
   if (enc != -1) {
      arm64_orr32_imm(iop->ob_ptr,hreg_ra,hreg_rs,enc);
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W9,tmp);
      arm64_orr32_reg(iop->ob_ptr,hreg_ra,hreg_rs,ARM64_W9,ARM64_A_LSL,0);
   }

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* OR Immediate Shifted */
DECLARE_INSN(ORIS)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t tmp = imm << 16;
   int hreg_rs,hreg_ra;
   jit_op_t *iop;
   m_uint32_t enc;

   /* $ra = $rs | imm */
   ppc32_jit_start_hreg_seq(cpu,"oris");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,1,"oris");

   enc = arm64_encode_bitmasks(tmp, 32);
   if (enc != -1) {
      arm64_orr32_imm(iop->ob_ptr,hreg_ra,hreg_rs,enc);
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W9,tmp);
      arm64_orr32_reg(iop->ob_ptr,hreg_ra,hreg_rs,ARM64_W9,ARM64_A_LSL,0);
   }

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   ppc32_jit_close_hreg_seq(cpu);
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
   m_uint32_t mask;
   int hreg_rs,hreg_ra,hreg_t0,hreg_rot_res,hreg_and_res;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"rlwimi");
   hreg_t0 = ppc32_jit_get_tmp_hreg(cpu);
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);

   mask = ppc32_rotate_mask(mb,me);

   iop = ppc32_op_emit_insn_output(cpu,2,"rlwimi");

   /* Apply inverse mask to $ra */
   if (mask != 0)
      arm64_and32_imm(iop->ob_ptr,hreg_ra,hreg_ra,arm64_encode_bitmasks(~mask, 32));

   /* Rotate $rs of "sh" bits and apply the mask */
   if (sh) {
      arm64_ror32_imm(iop->ob_ptr,hreg_t0,hreg_rs,(32-sh));
      hreg_rot_res = hreg_t0;
   } else {
      hreg_rot_res = hreg_rs;
      arm64_mov32(iop->ob_ptr,hreg_t0,hreg_rs);
   }

   if (mask != 0xFFFFFFFF) {
      arm64_and32_imm(iop->ob_ptr,hreg_t0,hreg_rot_res,arm64_encode_bitmasks(mask, 32));
      hreg_and_res = hreg_t0;
   } else {
      hreg_and_res = hreg_rot_res;
   }

   /* Store the result */
   arm64_orr32_reg(iop->ob_ptr,hreg_ra,hreg_ra,hreg_and_res,ARM64_A_LSL,0);
   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1) {
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);
      ppc32_op_emit_update_flags(cpu,0,TRUE);
   }

   ppc32_jit_close_hreg_seq(cpu);
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
   m_uint32_t mask;
   int hreg_rs,hreg_ra;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"rlwinm");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,2,"rlwinm");

   /* Rotate $rs of "sh" bits and apply the mask */
   mask = ppc32_rotate_mask(mb,me);

   if (sh)
      arm64_ror32_imm(iop->ob_ptr,hreg_ra,hreg_rs,(32-sh));
   else
      arm64_mov32(iop->ob_ptr,hreg_ra,hreg_rs);

   if (mask != 0xFFFFFFFF)
      arm64_and32_imm(iop->ob_ptr,hreg_ra,hreg_ra,arm64_encode_bitmasks(mask, 32));

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
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
   m_uint32_t mask;
   int hreg_rs,hreg_ra,hreg_rb,hreg_t0;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"rlwnm");

   hreg_t0 = ppc32_jit_get_tmp_hreg(cpu);
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,2,"rlwnm");

   /* Load the shift register ("sh") */
   mask = ppc32_rotate_mask(mb,me);

   /* Rotate $rs and apply the mask */
   arm64_neg32_reg(iop->ob_ptr,hreg_t0,hreg_rb,ARM64_A_LSL,0);

   arm64_rorv32(iop->ob_ptr,hreg_ra,hreg_rs,hreg_t0);

   if (mask != 0xFFFFFFFF)
      arm64_and32_imm(iop->ob_ptr,hreg_ra,hreg_ra,arm64_encode_bitmasks(mask, 32));

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* Shift Left Word */
DECLARE_INSN(SLW)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rs,hreg_ra,hreg_rb;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"slw");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   /* $ra = $rs << $rb. If count >= 32, then null result */
   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,3,"slw");

   arm64_cmp32_imm(iop->ob_ptr,hreg_rb,32,0);
   arm64_lslv32(iop->ob_ptr,hreg_ra,hreg_rs,hreg_rb);
   arm64_csel32(iop->ob_ptr,hreg_ra,hreg_ra,ARM64_WZR,ARM64_CC_CC);

   /* store the result */
   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* SRAWI - Shift Right Algebraic Word Immediate */
DECLARE_INSN(SRAWI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int sh = bits(insn,11,15);
   m_uint32_t mask;
   int hreg_rs,hreg_ra,hreg_t0,hreg_t1,hreg_and_res;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"srawi");
   hreg_t0 = ppc32_jit_get_tmp_hreg(cpu);
   hreg_t1 = ppc32_jit_alloc_hreg(cpu,-1);
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_alter_host_reg(cpu,hreg_t1);

   /* $ra = (int32)$rs >> sh */
   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,3,"srawi");

   /* set XER_CA depending on the result */
   mask = ~(0xFFFFFFFFU << sh) | 0x80000000;
   if (mask != 0xFFFFFFFF) {
      arm64_and32_imm(iop->ob_ptr,hreg_t1,hreg_rs,arm64_encode_bitmasks(mask, 32));
      hreg_and_res = hreg_t1;
   } else {
      hreg_and_res = hreg_rs;
   }

   ppc32_load_imm(&iop->ob_ptr,hreg_t0,0x80000000);
   arm64_cmp32_reg(iop->ob_ptr,hreg_and_res,hreg_t0,ARM64_A_LSL,0);
   arm64_cset64(iop->ob_ptr,hreg_t0,ARM64_CC_HI);
   arm64_str32_imm(iop->ob_ptr,hreg_t0,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));

   arm64_asr32(iop->ob_ptr,hreg_ra,hreg_rs,sh);

   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* Shift Right Word */
DECLARE_INSN(SRW)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rs,hreg_ra,hreg_rb;
   jit_op_t *iop;

   ppc32_jit_start_hreg_seq(cpu,"srw");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   /* $ra = $rs >> $rb. If count >= 32, then null result */
   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,3,"srw");

   arm64_cmp32_imm(iop->ob_ptr,hreg_rb,32,0);
   arm64_lsrv32(iop->ob_ptr,hreg_ra,hreg_rs,hreg_rb);
   arm64_csel32(iop->ob_ptr,hreg_ra,hreg_ra,ARM64_WZR,ARM64_CC_CC);

   /* store the result */
   if (insn & 1)
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* STB - Store Byte */
DECLARE_INSN(STB)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,1,PPC_MEMOP_STB,ra,offset,rs,
                         ppc32_memop_fast_stb,0);
   return(0);
}

/* STBU - Store Byte with Update */
DECLARE_INSN(STBU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,1,PPC_MEMOP_STB,ra,offset,rs,
                         ppc32_memop_fast_stb,1);
   return(0);
}

/* STBUX - Store Byte with Update Indexed */
DECLARE_INSN(STBUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,1,PPC_MEMOP_STB,ra,rb,rs,
                             ppc32_memop_fast_stb,1);
   return(0);
}

/* STBUX - Store Byte Indexed */
DECLARE_INSN(STBX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,1,PPC_MEMOP_STB,ra,rb,rs,
                             ppc32_memop_fast_stb,0);
   return(0);
}

/* STH - Store Half-Word */
DECLARE_INSN(STH)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,1,PPC_MEMOP_STH,ra,offset,rs,
                         ppc32_memop_fast_sth,0);
   return(0);
}

/* STHU - Store Half-Word with Update */
DECLARE_INSN(STHU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,1,PPC_MEMOP_STH,ra,offset,rs,
                         ppc32_memop_fast_sth,1);
   return(0);
}

/* STHUX - Store Half-Word with Update Indexed */
DECLARE_INSN(STHUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,1,PPC_MEMOP_STH,ra,rb,rs,
                             ppc32_memop_fast_sth,1);
   return(0);
}

/* STHUX - Store Half-Word Indexed */
DECLARE_INSN(STHX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,1,PPC_MEMOP_STH,ra,rb,rs,
                             ppc32_memop_fast_sth,0);
   return(0);
}

/* STMW - Store Multiple Word */
DECLARE_INSN(STMW)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast_mult(cpu,b,1,ra,offset,rs,
                              ppc32_memop_fast_stmw,ppc32_stmw_fallback);
   return(0);
}

/* STW - Store Word */
DECLARE_INSN(STW)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,1,PPC_MEMOP_STW,ra,offset,rs,
                         ppc32_memop_fast_stw,0);
   return(0);
}

/* STWU - Store Word with Update */
DECLARE_INSN(STWU)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t offset = bits(insn,0,15);

   ppc32_emit_memop_fast(cpu,b,1,PPC_MEMOP_STW,ra,offset,rs,
                         ppc32_memop_fast_stw,1);
   return(0);
}

/* STWUX - Store Word with Update Indexed */
DECLARE_INSN(STWUX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,1,PPC_MEMOP_STW,ra,rb,rs,
                             ppc32_memop_fast_stw,1);
   return(0);
}

/* STWUX - Store Word Indexed */
DECLARE_INSN(STWX)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);

   ppc32_emit_memop_fast_idx(cpu,b,1,PPC_MEMOP_STW,ra,rb,rs,
                             ppc32_memop_fast_stw,0);
   return(0);
}

/* SUBF - Subtract From */
DECLARE_INSN(SUBF)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rd,hreg_ra,hreg_rb;
   jit_op_t *iop;

   /* $rd = $rb - $ra */
   ppc32_jit_start_hreg_seq(cpu,"subf");

   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,2,"subf");

   arm64_subs32_reg(iop->ob_ptr,hreg_rd,hreg_rb,hreg_ra,ARM64_A_LSL,0);

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   if (insn & 1)
      ppc32_op_emit_update_flags(cpu,0,TRUE);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* SUBFC - Subtract From Carrying */
DECLARE_INSN(SUBFC)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_ra,hreg_rb,hreg_rd,hreg_t1;
   jit_op_t *iop;

   /* $rd = ~$ra + 1 + $rb */
   ppc32_jit_start_hreg_seq(cpu,"subfc");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   hreg_t1 = ppc32_jit_get_tmp_hreg(cpu);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,3,"subfc");

   arm64_subs32_reg(iop->ob_ptr,hreg_rd,hreg_rb,hreg_ra,ARM64_A_LSL,0);
   arm64_cset64(iop->ob_ptr,hreg_t1,ARM64_CC_CS);
   arm64_str32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   /* update cr0 */
   if (insn & 1)
      ppc32_update_cr0(b);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* SUBFE - Subtract From Extended */
DECLARE_INSN(SUBFE)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_ra,hreg_rb,hreg_rd,hreg_t1;
   jit_op_t *iop;

   /* $rd = ~$ra + $carry (xer_ca) + $rb */
   ppc32_jit_start_hreg_seq(cpu,"subfe");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   hreg_t1 = ppc32_jit_get_tmp_hreg(cpu);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,3,"subfe");

   arm64_ldr32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));
   arm64_cmp32_imm(iop->ob_ptr,hreg_t1,1,0);
   arm64_sbcs32_reg(iop->ob_ptr,hreg_rd,hreg_rb,hreg_ra);

   arm64_cset64(iop->ob_ptr,hreg_t1,ARM64_CC_CS);
   arm64_str32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   /* update cr0 */
   if (insn & 1)
      ppc32_update_cr0(b);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* SUBFIC - Subtract From Immediate Carrying */
DECLARE_INSN(SUBFIC)
{
   int rd = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t tmp = sign_extend_32(imm,16);
   int hreg_ra,hreg_rd,hreg_t1;
   jit_op_t *iop;

   /* $rd = ~$ra + 1 + sign_extend(imm,16) */
   ppc32_jit_start_hreg_seq(cpu,"subfic");
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rd = ppc32_jit_alloc_hreg(cpu,rd);

   hreg_t1 = ppc32_jit_get_tmp_hreg(cpu);

   ppc32_op_emit_load_gpr(cpu,hreg_ra,ra);

   iop = ppc32_op_emit_insn_output(cpu,3,"subfic");

   ppc32_load_imm(&iop->ob_ptr,hreg_t1,tmp);
   arm64_subs32_reg(iop->ob_ptr,hreg_rd,hreg_t1,hreg_ra,ARM64_A_LSL,0);
   arm64_cset64(iop->ob_ptr,hreg_t1,ARM64_CC_CS);
   arm64_str32_imm(iop->ob_ptr,hreg_t1,ARM64_X28,OFFSET(cpu_ppc_t,xer_ca));

   ppc32_op_emit_store_gpr(cpu,rd,hreg_rd);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* SYNC - Synchronize */
DECLARE_INSN(SYNC)
{
   return(0);
}

/* ISYNC - Instruction Synchronize */
DECLARE_INSN(ISYNC)
{
   return(0);
}

/* XOR */
DECLARE_INSN(XOR)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   int rb = bits(insn,11,15);
   int hreg_rs,hreg_ra,hreg_rb;
   jit_op_t *iop;

   /* $ra = $rs ^ $rb */
   ppc32_jit_start_hreg_seq(cpu,"xor");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);
   hreg_rb = ppc32_jit_alloc_hreg(cpu,rb);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);
   ppc32_op_emit_load_gpr(cpu,hreg_rb,rb);

   iop = ppc32_op_emit_insn_output(cpu,1,"xor");

   arm64_eor32_reg(iop->ob_ptr,hreg_ra,hreg_rs,hreg_rb,ARM64_A_LSL,0);

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   if (insn & 1) {
      arm64_cmp32_imm(iop->ob_ptr,hreg_ra,0,0);
      ppc32_op_emit_update_flags(cpu,0,TRUE);
   }

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* XORI - XOR Immediate */
DECLARE_INSN(XORI)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint32_t imm = bits(insn,0,15);
   int hreg_rs,hreg_ra;
   jit_op_t *iop;
   m_uint32_t enc;

   /* $ra = $rs ^ imm */
   ppc32_jit_start_hreg_seq(cpu,"xori");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,1,"xori");

   enc = arm64_encode_bitmasks(imm, 32);
   if (enc != -1) {
      arm64_eor32_imm(iop->ob_ptr,hreg_ra,hreg_rs,enc);
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W9,imm);
      arm64_eor32_reg(iop->ob_ptr,hreg_ra,hreg_rs,ARM64_W9,ARM64_A_LSL,0);
   }
   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   ppc32_jit_close_hreg_seq(cpu);
   return(0);
}

/* XORIS - XOR Immediate Shifted */
DECLARE_INSN(XORIS)
{
   int rs = bits(insn,21,25);
   int ra = bits(insn,16,20);
   m_uint16_t imm = bits(insn,0,15);
   m_uint32_t tmp = imm << 16;
   int hreg_rs,hreg_ra;
   jit_op_t *iop;
   m_uint32_t enc;

   /* $ra = $rs ^ (imm << 16) */
   ppc32_jit_start_hreg_seq(cpu,"xoris");
   hreg_rs = ppc32_jit_alloc_hreg(cpu,rs);
   hreg_ra = ppc32_jit_alloc_hreg(cpu,ra);

   ppc32_op_emit_load_gpr(cpu,hreg_rs,rs);

   iop = ppc32_op_emit_insn_output(cpu,1,"xoris");

   enc = arm64_encode_bitmasks(tmp, 32);
   if (enc != -1) {
      arm64_eor32_imm(iop->ob_ptr,hreg_ra,hreg_rs,enc);
   } else {
      ppc32_load_imm(&iop->ob_ptr,ARM64_W9,tmp);
      arm64_eor32_reg(iop->ob_ptr,hreg_ra,hreg_rs,ARM64_W9,ARM64_A_LSL,0);
   }

   ppc32_op_emit_store_gpr(cpu,ra,hreg_ra);

   ppc32_jit_close_hreg_seq(cpu);
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
   { ppc32_emit_ADDZE      , 0xfc00fffe , 0x7c000194 },
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
   { ppc32_emit_BCCTR      , 0xfc00fffe , 0x4c000420 },
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
   { ppc32_emit_LMW        , 0xfc000000 , 0xb8000000 },
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
   { ppc32_emit_STMW       , 0xfc000000 , 0xbc000000 },
   { ppc32_emit_STW        , 0xfc000000 , 0x90000000 },
   { ppc32_emit_STWU       , 0xfc000000 , 0x94000000 },
   { ppc32_emit_STWUX      , 0xfc0007ff , 0x7c00016e },
   { ppc32_emit_STWX       , 0xfc0007ff , 0x7c00012e },
   { ppc32_emit_SUBF       , 0xfc0007fe , 0x7c000050 },
   { ppc32_emit_SUBFC      , 0xfc0007fe , 0x7c000010 },
   { ppc32_emit_SUBFE      , 0xfc0007fe , 0x7c000110 },
   { ppc32_emit_SUBFIC     , 0xfc000000 , 0x20000000 },
   { ppc32_emit_SYNC       , 0xffffffff , 0x7c0004ac },
   { ppc32_emit_ISYNC       , 0xffffffff , 0x4c00012c },
   { ppc32_emit_XOR        , 0xfc0007fe , 0x7c000278 },
   { ppc32_emit_XORI       , 0xfc000000 , 0x68000000 },
   { ppc32_emit_XORIS      , 0xfc000000 , 0x6c000000 },
   { ppc32_emit_unknown    , 0x00000000 , 0x00000000 },
   { NULL                  , 0x00000000 , 0x00000000 },
};
