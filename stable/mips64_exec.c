/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * MIPS64 Step-by-step execution.
 */

#if __GNUC__ > 2

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "cpu.h"
#include "vm.h"
#include "mips64_exec.h"
#include "memory.h"
#include "insn_lookup.h"
#include "dynamips.h"

/* Forward declaration of instruction array */
static struct mips64_insn_exec_tag mips64_exec_tags[];
static insn_lookup_t *ilt = NULL;

/* ILT */
static forced_inline void *mips64_exec_get_insn(int index)
{
   return(&mips64_exec_tags[index]);
}

static int mips64_exec_chk_lo(struct mips64_insn_exec_tag *tag,int value)
{
   return((value & tag->mask) == (tag->value & 0xFFFF));
}

static int mips64_exec_chk_hi(struct mips64_insn_exec_tag *tag,int value)
{
   return((value & (tag->mask >> 16)) == (tag->value >> 16));
}

/* Destroy instruction lookup table */
static void destroy_ilt(void)
{
   assert(ilt);
   ilt_destroy(ilt);
   ilt = NULL;
}

/* Initialize instruction lookup table */
void mips64_exec_create_ilt(void)
{
   int i,count;

   for(i=0,count=0;mips64_exec_tags[i].exec;i++)
      count++;

   ilt = ilt_create("mips64e",count,
                    (ilt_get_insn_cbk_t)mips64_exec_get_insn,
                    (ilt_check_cbk_t)mips64_exec_chk_lo,
                    (ilt_check_cbk_t)mips64_exec_chk_hi);

   atexit(destroy_ilt);
}

/* Dump statistics */
void mips64_dump_stats(cpu_mips_t *cpu)
{
   int i;

#if NJM_STATS_ENABLE
   printf("\n");

   for(i=0;mips64_exec_tags[i].exec;i++)
      printf("  * %-10s : %10llu\n",
             mips64_exec_tags[i].name,mips64_exec_tags[i].count);

   printf("%llu instructions executed since startup.\n",cpu->insn_exec_count);
#else
   printf("Statistics support is not compiled in.\n");
#endif
}

/* Dump an instruction */
int mips64_dump_insn(char *buffer,size_t buf_size,size_t insn_name_size,
                     m_uint64_t pc,mips_insn_t instruction)
{
   char insn_name[64],insn_format[32],*name;
   int base,rs,rd,rt,sa,offset,imm;
   struct mips64_insn_exec_tag *tag;
   m_uint64_t new_pc;
   int index;

   /* Lookup for instruction */
   index = ilt_lookup(ilt,instruction);
   tag = mips64_exec_get_insn(index);

   if (!tag) {
      snprintf(buffer,buf_size,"%8.8x  (unknown)",instruction);
      return(-1);
   }
   
   if (!(name = tag->name))
      name = "[unknown]";

   if (!insn_name_size)
      insn_name_size = 10;

   snprintf(insn_format,sizeof(insn_format),"%%-%lus",(u_long)insn_name_size);
   snprintf(insn_name,sizeof(insn_name),insn_format,name);

   switch(tag->instr_type) {
      case 1:   /* instructions without operands */
         snprintf(buffer,buf_size,"%8.8x  %s",instruction,insn_name);
         break;

      case 2:   /* load/store instructions */
         base   = bits(instruction,21,25);
         rt     = bits(instruction,16,20);
         offset = (m_int16_t)bits(instruction,0,15);
         snprintf(buffer,buf_size,"%8.8x  %s %s,%d(%s)",
                  instruction,insn_name,mips64_gpr_reg_names[rt],
                  offset,mips64_gpr_reg_names[base]);
         break;

      case 3:   /* GPR[rd] = GPR[rs] op GPR[rt] */
         rs = bits(instruction,21,25);
         rt = bits(instruction,16,20);
         rd = bits(instruction,11,15);
         snprintf(buffer,buf_size,"%8.8x  %s %s,%s,%s",
                  instruction,insn_name,mips64_gpr_reg_names[rd],
                  mips64_gpr_reg_names[rs],mips64_gpr_reg_names[rt]);
         break;

      case 4:   /* GPR[rd] = GPR[rt] op GPR[rs] */
         rs = bits(instruction,21,25);
         rt = bits(instruction,16,20);
         rd = bits(instruction,11,15);
         snprintf(buffer,buf_size,"%8.8x  %s %s,%s,%s",
                  instruction,insn_name,mips64_gpr_reg_names[rd],
                  mips64_gpr_reg_names[rt],mips64_gpr_reg_names[rs]);
         break;

      case 5:   /* GPR[rt] = GPR[rs] op immediate (hex) */
         rs  = bits(instruction,21,25);
         rt  = bits(instruction,16,20);
         imm = bits(instruction,0,15);
         snprintf(buffer,buf_size,"%8.8x  %s %s,%s,0x%x",
                  instruction,insn_name,mips64_gpr_reg_names[rt],
                  mips64_gpr_reg_names[rs],imm);
         break;

      case 6:   /* GPR[rt] = GPR[rs] op immediate (dec) */
         rs  = bits(instruction,21,25);
         rt  = bits(instruction,16,20);
         imm = bits(instruction,0,15);
         snprintf(buffer,buf_size,"%8.8x  %s %s,%s,%d",
                  instruction,insn_name,mips64_gpr_reg_names[rt],
                  mips64_gpr_reg_names[rs],(m_int16_t)imm);
         break;

      case 7:   /* GPR[rd] = GPR[rt] op sa */
         rt = bits(instruction,16,20);
         rd = bits(instruction,11,15);
         sa = bits(instruction,6,10);
         snprintf(buffer,buf_size,"%8.8x  %s %s,%s,%d",
                  instruction,insn_name,mips64_gpr_reg_names[rd],
                  mips64_gpr_reg_names[rt],sa);
         break;

      case 8:   /* Branch with: GPR[rs] / GPR[rt] / offset */
         rs = bits(instruction,21,25);
         rt = bits(instruction,16,20);
         offset = bits(instruction,0,15);
         new_pc = (pc + 4) + sign_extend(offset << 2,18);
         snprintf(buffer,buf_size,"%8.8x  %s %s,%s,0x%llx",
                  instruction,insn_name,mips64_gpr_reg_names[rs],
                  mips64_gpr_reg_names[rt],new_pc);
         break;

      case 9:   /* Branch with: GPR[rs] / offset */
         rs = bits(instruction,21,25);
         offset = bits(instruction,0,15);
         new_pc = (pc + 4) + sign_extend(offset << 2,18);
         snprintf(buffer,buf_size,"%8.8x  %s %s,0x%llx",
                  instruction,insn_name,mips64_gpr_reg_names[rs],new_pc);
         break;

      case 10:   /* Branch with: offset */
         offset = bits(instruction,0,15);
         new_pc = (pc + 4) + sign_extend(offset << 2,18);
         snprintf(buffer,buf_size,"%8.8x  %s 0x%llx",
                  instruction,insn_name,new_pc);
         break;

      case 11:   /* Jump */
         offset = bits(instruction,0,25);
         new_pc = (pc & ~((1 << 28) - 1)) | (offset << 2);
         snprintf(buffer,buf_size,"%8.8x  %s 0x%llx",
                  instruction,insn_name,new_pc);
         break;

      case 13:   /* op GPR[rs] */
         rs = bits(instruction,21,25);
         snprintf(buffer,buf_size,"%8.8x  %s %s",
                  instruction,insn_name,mips64_gpr_reg_names[rs]);
         break;

      case 14:   /* op GPR[rd] */
         rd = bits(instruction,11,15);
         snprintf(buffer,buf_size,"%8.8x  %s %s",
                  instruction,insn_name,mips64_gpr_reg_names[rd]);
         break;

      case 15:   /* op GPR[rd], GPR[rs] */
         rs = bits(instruction,21,25);
         rd = bits(instruction,11,15);
         snprintf(buffer,buf_size,"%8.8x  %s %s,%s",
                  instruction,insn_name,mips64_gpr_reg_names[rd],
                  mips64_gpr_reg_names[rs]);
         break;

      case 16:   /* op GPR[rt], imm */
         rt  = bits(instruction,16,20);
         imm = bits(instruction,0,15);
         snprintf(buffer,buf_size,"%8.8x  %s %s,0x%x",
                  instruction,insn_name,mips64_gpr_reg_names[rt],imm);
         break;

      case 17:   /* op GPR[rs], GPR[rt] */
         rs = bits(instruction,21,25);
         rt = bits(instruction,16,20);         
         snprintf(buffer,buf_size,"%8.8x  %s %s,%s",
                  instruction,insn_name,mips64_gpr_reg_names[rs],
                  mips64_gpr_reg_names[rt]);
         break;

      case 18:   /* op GPR[rt], CP0[rd] */
         rt = bits(instruction,16,20);
         rd = bits(instruction,11,15);
         snprintf(buffer,buf_size,"%8.8x  %s %s,%s",
                  instruction,insn_name,mips64_gpr_reg_names[rt],
                  mips64_cp0_reg_names[rd]);
         break;

      case 19:   /* op GPR[rt], $rd */
         rt = bits(instruction,16,20);
         rd = bits(instruction,11,15);
         snprintf(buffer,buf_size,"%8.8x  %s %s,$%d",
                  instruction,insn_name,mips64_gpr_reg_names[rt],rd);
         break;

      case 20:   /* op GPR[rs], imm */
         rs = bits(instruction,21,25);
         imm = bits(instruction,0,15);
         snprintf(buffer,buf_size,"%8.8x  %s %s,0x%x",
                  instruction,insn_name,mips64_gpr_reg_names[rs],imm);
         break;

      default:
         snprintf(buffer,buf_size,"%8.8x  %s (TO DEFINE - %d)",
                  instruction,insn_name,tag->instr_type);
         return(-1);
   }
   
   return(0);
}

/* Dump an instruction block */
void mips64_dump_insn_block(cpu_mips_t *cpu,m_uint64_t pc,u_int count,
                            size_t insn_name_size)
{
   mips_insn_t *ptr,insn;
   char buffer[80];
   int i;

   for(i=0;i<count;i++) {
      ptr = cpu->mem_op_lookup(cpu,pc);
      insn = vmtoh32(*ptr);

      mips64_dump_insn(buffer,sizeof(buffer),insn_name_size,pc,insn);
      printf("0x%llx: %s\n",pc,buffer);
      pc += sizeof(mips_insn_t);
   }
}

/* Execute a memory operation */
_unused static forced_inline void mips64_exec_memop(cpu_mips_t *cpu,int memop,
                                            m_uint64_t vaddr,u_int dst_reg,
                                            int keep_ll_bit)
{     
   fastcall mips_memop_fn fn;

   if (!keep_ll_bit) cpu->ll_bit = 0;
   fn = cpu->mem_op_fn[memop];
   fn(cpu,vaddr,dst_reg);
}

/* Execute a memory operation (2) */
static forced_inline void mips64_exec_memop2(cpu_mips_t *cpu,int memop,
                                             m_uint64_t base,int offset,
                                             u_int dst_reg,int keep_ll_bit)
{
   m_uint64_t vaddr = cpu->gpr[base] + sign_extend(offset,16);
   fastcall mips_memop_fn fn;
      
   if (!keep_ll_bit) cpu->ll_bit = 0;
   fn = cpu->mem_op_fn[memop];
   fn(cpu,vaddr,dst_reg);
}

/* Fetch an instruction */
static forced_inline int mips64_exec_fetch(cpu_mips_t *cpu,m_uint64_t pc,
                                           mips_insn_t *insn)
{   
   m_uint64_t exec_page;
   m_uint32_t offset;

   exec_page = pc & ~(m_uint64_t)MIPS_MIN_PAGE_IMASK;

   if (unlikely(exec_page != cpu->njm_exec_page)) {
      cpu->njm_exec_page = exec_page;
      cpu->njm_exec_ptr  = cpu->mem_op_lookup(cpu,exec_page);
   }

   offset = (pc & MIPS_MIN_PAGE_IMASK) >> 2;
   *insn = vmtoh32(cpu->njm_exec_ptr[offset]);
   return(0);
}

/* Unknown opcode */
static fastcall int mips64_exec_unknown(cpu_mips_t *cpu,mips_insn_t insn)
{
   printf("MIPS64: unknown opcode 0x%8.8x at pc = 0x%llx\n",insn,cpu->pc);
   mips64_dump_regs(cpu->gen);
   return(0);
}

/* Execute a single instruction */
static forced_inline int 
mips64_exec_single_instruction(cpu_mips_t *cpu,mips_insn_t instruction)
{
   register fastcall int (*exec)(cpu_mips_t *,mips_insn_t) = NULL;
   struct mips64_insn_exec_tag *tag;
   int index;

#if DEBUG_INSN_PERF_CNT
   cpu->perf_counter++;
#endif
   
   /* Increment CP0 count register */
   mips64_exec_inc_cp0_cnt(cpu);

   /* Lookup for instruction */
   index = ilt_lookup(ilt,instruction);
   tag = mips64_exec_get_insn(index);
   exec = tag->exec;

#if NJM_STATS_ENABLE
   cpu->insn_exec_count++;
   mips64_exec_tags[index].count++;
#endif
#if 0
   {
      char buffer[80];
      
      if (mips64_dump_insn(buffer,sizeof(buffer),0,cpu->pc,instruction)!=-1)
         cpu_log(cpu->gen,"EXEC","0x%llx: %s\n",cpu->pc,buffer);
   }
#endif
   return(exec(cpu,instruction));
}

/* Single-step execution */
fastcall void mips64_exec_single_step(cpu_mips_t *cpu,mips_insn_t instruction)
{
   int res;

   res = mips64_exec_single_instruction(cpu,instruction);

   /* Normal flow ? */
   if (likely(!res)) cpu->pc += 4;
}

/* Run MIPS code in step-by-step mode */
void *mips64_exec_run_cpu(cpu_gen_t *gen)
{   
   cpu_mips_t *cpu = CPU_MIPS64(gen);
   pthread_t timer_irq_thread;
   int timer_irq_check = 0;
   mips_insn_t insn;
   int res;

   if (pthread_create(&timer_irq_thread,NULL,
                      (void *)mips64_timer_irq_run,cpu))
   {
      fprintf(stderr,"VM '%s': unable to create Timer IRQ thread for CPU%u.\n",
              cpu->vm->name,gen->id);
      cpu_stop(gen);
      return NULL;
   }

   gen->cpu_thread_running = TRUE;
   cpu_exec_loop_set(gen);

 start_cpu:
   gen->idle_count = 0;

   for(;;) {
      if (unlikely(gen->state != CPU_STATE_RUNNING))
         break;

      /* Handle virtual idle loop */
      if (unlikely(cpu->pc == cpu->idle_pc)) {
         if (++gen->idle_count == gen->idle_max) {
            cpu_idle_loop(gen);
            gen->idle_count = 0;
         }
      }

      /* Handle the virtual CPU clock */
      if (++timer_irq_check == cpu->timer_irq_check_itv) {
         timer_irq_check = 0;

         if (cpu->timer_irq_pending && !cpu->irq_disable) {
            mips64_trigger_timer_irq(cpu);
            mips64_trigger_irq(cpu);
            cpu->timer_irq_pending--;
         }
      }

      /* Reset "zero register" (for safety) */
      cpu->gpr[0] = 0;

      /* Check IRQ */
      if (unlikely(cpu->irq_pending)) {
         mips64_trigger_irq(cpu);
         continue;
      }

      /* Fetch and execute the instruction */      
      mips64_exec_fetch(cpu,cpu->pc,&insn);
      res = mips64_exec_single_instruction(cpu,insn);

      /* Normal flow ? */
      if (likely(!res)) cpu->pc += sizeof(mips_insn_t);
   }

   if (!cpu->pc) {
      cpu_stop(gen);
      cpu_log(gen,"SLOW_EXEC","PC=0, halting CPU.\n");
   }

   /* Check regularly if the CPU has been restarted */
   while(gen->cpu_thread_running) {
      gen->seq_state++;

      switch(gen->state) {
         case CPU_STATE_RUNNING:
            gen->state = CPU_STATE_RUNNING;
            goto start_cpu;

         case CPU_STATE_HALTED:     
            gen->cpu_thread_running = FALSE;
            pthread_join(timer_irq_thread,NULL);
            break;
      }
      
      /* CPU is paused */
      usleep(200000);
   }

   return NULL;
}

/* Execute the instruction in delay slot */
static forced_inline void mips64_exec_bdslot(cpu_mips_t *cpu)
{
   mips_insn_t insn;

   /* Fetch the instruction in delay slot */
   mips64_exec_fetch(cpu,cpu->pc+4,&insn);

   /* Execute the instruction */
   mips64_exec_single_instruction(cpu,insn);
}

/* ADD */
static fastcall int mips64_exec_ADD(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   m_uint64_t res;

   /* TODO: Exception handling */
   res = (m_uint32_t)cpu->gpr[rs] + (m_uint32_t)cpu->gpr[rt];
   cpu->gpr[rd] = sign_extend(res,32);
   return(0);
}

/* ADDI */
static fastcall int mips64_exec_ADDI(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint32_t res,val = sign_extend(imm,16);

   /* TODO: Exception handling */
   res = (m_uint32_t)cpu->gpr[rs] + val;
   cpu->gpr[rt] = sign_extend(res,32);
   return(0);
}

/* ADDIU */
static fastcall int mips64_exec_ADDIU(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint32_t res,val = sign_extend(imm,16);

   res = (m_uint32_t)cpu->gpr[rs] + val;
   cpu->gpr[rt] = sign_extend(res,32);
   return(0);
}

/* ADDU */
static fastcall int mips64_exec_ADDU(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   m_uint32_t res;

   res = (m_uint32_t)cpu->gpr[rs] + (m_uint32_t)cpu->gpr[rt];
   cpu->gpr[rd] = sign_extend(res,32);
   return(0);
}

/* AND */
static fastcall int mips64_exec_AND(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   cpu->gpr[rd] = cpu->gpr[rs] & cpu->gpr[rt];
   return(0);
}

/* ANDI */
static fastcall int mips64_exec_ANDI(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   cpu->gpr[rt] = cpu->gpr[rs] & imm;
   return(0);
}

/* B (Branch, virtual instruction) */
static fastcall int mips64_exec_B(cpu_mips_t *cpu,mips_insn_t insn)
{
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* set the new pc in cpu structure */
   cpu->pc = new_pc;
   return(1);
}

/* BAL (Branch And Link, virtual instruction) */
static fastcall int mips64_exec_BAL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* set the return address (instruction after the delay slot) */
   cpu->gpr[MIPS_GPR_RA] = cpu->pc + 8;

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* set the new pc in cpu structure */
   cpu->pc = new_pc;
   return(1);
}

/* BEQ (Branch On Equal) */
static fastcall int mips64_exec_BEQ(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] == gpr[rt] */
   res = (cpu->gpr[rs] == cpu->gpr[rt]);

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* take the branch if the test result is true */
   if (res) 
      cpu->pc = new_pc;
   else
      cpu->pc += 8;

   return(1);
}

/* BEQL (Branch On Equal Likely) */
static fastcall int mips64_exec_BEQL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] == gpr[rt] */
   res = (cpu->gpr[rs] == cpu->gpr[rt]);

   /* take the branch if the test result is true */
   if (res) {
      mips64_exec_bdslot(cpu);
      cpu->pc = new_pc;
   } else
      cpu->pc += 8;

   return(1);
}

/* BEQZ (Branch On Equal Zero) - Virtual Instruction */
static fastcall int mips64_exec_BEQZ(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] == 0 */
   res = (cpu->gpr[rs] == 0);

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* take the branch if the test result is true */
   if (res) 
      cpu->pc = new_pc;
   else
      cpu->pc += 8;

   return(1);
}

/* BNEZ (Branch On Not Equal Zero) - Virtual Instruction */
static fastcall int mips64_exec_BNEZ(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] != 0 */
   res = (cpu->gpr[rs] != 0);

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* take the branch if the test result is true */
   if (res) 
      cpu->pc = new_pc;
   else
      cpu->pc += 8;

   return(1);
}

/* BGEZ (Branch On Greater or Equal Than Zero) */
static fastcall int mips64_exec_BGEZ(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] >= 0 */
   res = ((m_int64_t)cpu->gpr[rs] >= 0);

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* take the branch if the test result is true */
   if (res) 
      cpu->pc = new_pc;
   else
      cpu->pc += 8;

   return(1);
}

/* BGEZAL (Branch On Greater or Equal Than Zero And Link) */
static fastcall int mips64_exec_BGEZAL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* set the return address (instruction after the delay slot) */
   cpu->gpr[MIPS_GPR_RA] = cpu->pc + 8;

   /* take the branch if gpr[rs] >= 0 */
   res = ((m_int64_t)cpu->gpr[rs] >= 0);

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* take the branch if the test result is true */
   if (res) 
      cpu->pc = new_pc;
   else
      cpu->pc += 8;

   return(1);
}

/* BGEZALL (Branch On Greater or Equal Than Zero And Link Likely) */
static fastcall int mips64_exec_BGEZALL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* set the return address (instruction after the delay slot) */
   cpu->gpr[MIPS_GPR_RA] = cpu->pc + 8;

   /* take the branch if gpr[rs] >= 0 */
   res = ((m_int64_t)cpu->gpr[rs] >= 0);

   /* take the branch if the test result is true */
   if (res) {
      mips64_exec_bdslot(cpu);
      cpu->pc = new_pc;
   } else
      cpu->pc += 8;

   return(1);
}

/* BGEZL (Branch On Greater or Equal Than Zero Likely) */
static fastcall int mips64_exec_BGEZL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] >= 0 */
   res = ((m_int64_t)cpu->gpr[rs] >= 0);

   /* take the branch if the test result is true */
   if (res) {
      mips64_exec_bdslot(cpu);
      cpu->pc = new_pc;
   } else
      cpu->pc += 8;

   return(1);
}

/* BGTZ (Branch On Greater Than Zero) */
static fastcall int mips64_exec_BGTZ(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] > 0 */
   res = ((m_int64_t)cpu->gpr[rs] > 0);

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* take the branch if the test result is true */
   if (res) 
      cpu->pc = new_pc;
   else
      cpu->pc += 8;

   return(1);
}

/* BGTZL (Branch On Greater Than Zero Likely) */
static fastcall int mips64_exec_BGTZL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] > 0 */
   res = ((m_int64_t)cpu->gpr[rs] > 0);

   /* take the branch if the test result is true */
   if (res) {
      mips64_exec_bdslot(cpu);
      cpu->pc = new_pc;
   } else
      cpu->pc += 8;

   return(1);
}

/* BLEZ (Branch On Less or Equal Than Zero) */
static fastcall int mips64_exec_BLEZ(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] <= 0 */
   res = ((m_int64_t)cpu->gpr[rs] <= 0);

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* take the branch if the test result is true */
   if (res) 
      cpu->pc = new_pc;
   else
      cpu->pc += 8;

   return(1);
}

/* BLEZL (Branch On Less or Equal Than Zero Likely) */
static fastcall int mips64_exec_BLEZL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] <= 0 */
   res = ((m_int64_t)cpu->gpr[rs] <= 0);

   /* take the branch if the test result is true */
   if (res) {
      mips64_exec_bdslot(cpu);
      cpu->pc = new_pc;
   } else
      cpu->pc += 8;

   return(1);
}

/* BLTZ (Branch On Less Than Zero) */
static fastcall int mips64_exec_BLTZ(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] < 0 */
   res = ((m_int64_t)cpu->gpr[rs] < 0);

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* take the branch if the test result is true */
   if (res)
      cpu->pc = new_pc;
   else
      cpu->pc += 8;

   return(1);
}

/* BLTZAL (Branch On Less Than Zero And Link) */
static fastcall int mips64_exec_BLTZAL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* set the return address (instruction after the delay slot) */
   cpu->gpr[MIPS_GPR_RA] = cpu->pc + 8;

   /* take the branch if gpr[rs] < 0 */
   res = ((m_int64_t)cpu->gpr[rs] < 0);

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* take the branch if the test result is true */
   if (res)
      cpu->pc = new_pc;
   else
      cpu->pc += 8;

   return(1);
}

/* BLTZALL (Branch On Less Than Zero And Link Likely) */
static fastcall int mips64_exec_BLTZALL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* set the return address (instruction after the delay slot) */
   cpu->gpr[MIPS_GPR_RA] = cpu->pc + 8;

   /* take the branch if gpr[rs] < 0 */
   res = ((m_int64_t)cpu->gpr[rs] < 0);

   /* take the branch if the test result is true */
   if (res) {
      mips64_exec_bdslot(cpu);
      cpu->pc = new_pc;
   } else
      cpu->pc += 8;

   return(1);
}

/* BLTZL (Branch On Less Than Zero Likely) */
static fastcall int mips64_exec_BLTZL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] < 0 */
   res = ((m_int64_t)cpu->gpr[rs] < 0);

   /* take the branch if the test result is true */
   if (res) {
      mips64_exec_bdslot(cpu);
      cpu->pc = new_pc;
   } else
      cpu->pc += 8;

   return(1);
}

/* BNE (Branch On Not Equal) */
static fastcall int mips64_exec_BNE(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] != gpr[rt] */
   res = (cpu->gpr[rs] != cpu->gpr[rt]);

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* take the branch if the test result is true */
   if (res) 
      cpu->pc = new_pc;
   else
      cpu->pc += 8;

   return(1);
}

/* BNEL (Branch On Not Equal Likely) */
static fastcall int mips64_exec_BNEL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int offset = bits(insn,0,15);
   m_uint64_t new_pc;
   int res;

   /* compute the new pc */
   new_pc = (cpu->pc + 4) + sign_extend(offset << 2,18);

   /* take the branch if gpr[rs] != gpr[rt] */
   res = (cpu->gpr[rs] != cpu->gpr[rt]);

   /* take the branch if the test result is true */
   if (res) {
      mips64_exec_bdslot(cpu);
      cpu->pc = new_pc;
   } else
      cpu->pc += 8;

   return(1);
}

/* BREAK */
static fastcall int mips64_exec_BREAK(cpu_mips_t *cpu,mips_insn_t insn)
{
   u_int code = bits(insn,6,25);

   mips64_exec_break(cpu,code);
   return(1);
}

/* CACHE */
static fastcall int mips64_exec_CACHE(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int op     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_CACHE,base,offset,op,FALSE);
   return(0);
}

/* CFC0 */
static fastcall int mips64_exec_CFC0(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_cp0_exec_cfc0(cpu,rt,rd);
   return(0);
}

/* CTC0 */
static fastcall int mips64_exec_CTC0(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_cp0_exec_ctc0(cpu,rt,rd);
   return(0);
}

/* DADDIU */
static fastcall int mips64_exec_DADDIU(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);

   cpu->gpr[rt] = cpu->gpr[rs] + val;
   return(0);
}

/* DADDU: rd = rs + rt */
static fastcall int mips64_exec_DADDU(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   cpu->gpr[rd] = cpu->gpr[rs] + cpu->gpr[rt];
   return(0);
}

/* DIV */
static fastcall int mips64_exec_DIV(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   cpu->lo = (m_int32_t)cpu->gpr[rs] / (m_int32_t)cpu->gpr[rt];
   cpu->hi = (m_int32_t)cpu->gpr[rs] % (m_int32_t)cpu->gpr[rt];

   cpu->lo = sign_extend(cpu->lo,32);
   cpu->hi = sign_extend(cpu->hi,32);
   return(0);
}

/* DIVU */
static fastcall int mips64_exec_DIVU(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   if (cpu->gpr[rt] == 0)
      return(0);

   cpu->lo = (m_uint32_t)cpu->gpr[rs] / (m_uint32_t)cpu->gpr[rt];
   cpu->hi = (m_uint32_t)cpu->gpr[rs] % (m_uint32_t)cpu->gpr[rt];

   cpu->lo = sign_extend(cpu->lo,32);
   cpu->hi = sign_extend(cpu->hi,32);
   return(0);
}

/* DMFC0 */
static fastcall int mips64_exec_DMFC0(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_cp0_exec_dmfc0(cpu,rt,rd);
   return(0);
}

/* DMFC1 */
static fastcall int mips64_exec_DMFC1(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_exec_dmfc1(cpu,rt,rd);
   return(0);
}

/* DMTC0 */
static fastcall int mips64_exec_DMTC0(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_cp0_exec_dmtc0(cpu,rt,rd);
   return(0);
}

/* DMTC1 */
static fastcall int mips64_exec_DMTC1(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_exec_dmtc1(cpu,rt,rd);
   return(0);
}

/* DSLL */
static fastcall int mips64_exec_DSLL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   cpu->gpr[rd] = cpu->gpr[rt] << sa;
   return(0);
}

/* DSLL32 */
static fastcall int mips64_exec_DSLL32(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   cpu->gpr[rd] = cpu->gpr[rt] << (32 + sa);
   return(0);
}

/* DSLLV */
static fastcall int mips64_exec_DSLLV(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   cpu->gpr[rd] = cpu->gpr[rt] << (cpu->gpr[rs] & 0x3f);
   return(0);
}

/* DSRA */
static fastcall int mips64_exec_DSRA(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   cpu->gpr[rd] = (m_int64_t)cpu->gpr[rt] >> sa;
   return(0);
}

/* DSRA32 */
static fastcall int mips64_exec_DSRA32(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   cpu->gpr[rd] = (m_int64_t)cpu->gpr[rt] >> (32 + sa);
   return(0);
}

/* DSRAV */
static fastcall int mips64_exec_DSRAV(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   cpu->gpr[rd] = (m_int64_t)cpu->gpr[rt] >> (cpu->gpr[rs] & 0x3f);
   return(0);
}

/* DSRL */
static fastcall int mips64_exec_DSRL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   cpu->gpr[rd] = cpu->gpr[rt] >> sa;
   return(0);
}

/* DSRL32 */
static fastcall int mips64_exec_DSRL32(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);

   cpu->gpr[rd] = cpu->gpr[rt] >> (32 + sa);
   return(0);
}

/* DSRLV */
static fastcall int mips64_exec_DSRLV(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   cpu->gpr[rd] = cpu->gpr[rt] >> (cpu->gpr[rs] & 0x3f);
   return(0);
}

/* DSUBU */
static fastcall int mips64_exec_DSUBU(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   cpu->gpr[rd] = cpu->gpr[rs] - cpu->gpr[rt];
   return(0);
}

/* ERET */
static fastcall int mips64_exec_ERET(cpu_mips_t *cpu,mips_insn_t insn)
{
   mips64_exec_eret(cpu);
   return(1);
}

/* J */
static fastcall int mips64_exec_J(cpu_mips_t *cpu,mips_insn_t insn)
{
   u_int instr_index = bits(insn,0,25);
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = cpu->pc & ~((1 << 28) - 1);
   new_pc |= instr_index << 2;

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);
   
   /* set the new pc */
   cpu->pc = new_pc;
   return(1);
}

/* JAL */
static fastcall int mips64_exec_JAL(cpu_mips_t *cpu,mips_insn_t insn)
{
   u_int instr_index = bits(insn,0,25);
   m_uint64_t new_pc;

   /* compute the new pc */
   new_pc = cpu->pc & ~((1 << 28) - 1);
   new_pc |= instr_index << 2;

   /* set the return address (instruction after the delay slot) */
   cpu->gpr[MIPS_GPR_RA] = cpu->pc + 8;

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);
   
   /* set the new pc */
   cpu->pc = new_pc;
   return(1);
}

/* JALR */
static fastcall int mips64_exec_JALR(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rd = bits(insn,11,15);
   m_uint64_t new_pc;

   /* set the return pc (instruction after the delay slot) in GPR[rd] */
   cpu->gpr[rd] = cpu->pc + 8;

   /* get the new pc */
   new_pc = cpu->gpr[rs];

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* set the new pc */
   cpu->pc = new_pc;
   return(1);
}

/* JR */
static fastcall int mips64_exec_JR(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   m_uint64_t new_pc;

   /* get the new pc */
   new_pc = cpu->gpr[rs];

   /* exec the instruction in the delay slot */
   mips64_exec_bdslot(cpu);

   /* set the new pc */
   cpu->pc = new_pc;
   return(1);
}

/* LB (Load Byte) */
static fastcall int mips64_exec_LB(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LB,base,offset,rt,TRUE);
   return(0);
}

/* LBU (Load Byte Unsigned) */
static fastcall int mips64_exec_LBU(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LBU,base,offset,rt,TRUE);
   return(0);
}

/* LD (Load Double-Word) */
static fastcall int mips64_exec_LD(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LD,base,offset,rt,TRUE);
   return(0);
}

/* LDC1 (Load Double-Word to Coprocessor 1) */
static fastcall int mips64_exec_LDC1(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int ft     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LDC1,base,offset,ft,TRUE);
   return(0);
}

/* LDL (Load Double-Word Left) */
static fastcall int mips64_exec_LDL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LDL,base,offset,rt,TRUE);
   return(0);
}

/* LDR (Load Double-Word Right) */
static fastcall int mips64_exec_LDR(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LDR,base,offset,rt,TRUE);
   return(0);
}

/* LH (Load Half-Word) */
static fastcall int mips64_exec_LH(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LH,base,offset,rt,TRUE);
   return(0);
}

/* LHU (Load Half-Word Unsigned) */
static fastcall int mips64_exec_LHU(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LHU,base,offset,rt,TRUE);
   return(0);
}

/* LI (virtual) */
static fastcall int mips64_exec_LI(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   cpu->gpr[rt] = sign_extend(imm,16);
   return(0);
}

/* LL (Load Linked) */
static fastcall int mips64_exec_LL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LL,base,offset,rt,TRUE);
   return(0);
}

/* LUI */
static fastcall int mips64_exec_LUI(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   cpu->gpr[rt] = sign_extend(imm,16) << 16;
   return(0);
}

/* LW (Load Word) */
static fastcall int mips64_exec_LW(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LW,base,offset,rt,TRUE);
   return(0);
}

/* LWL (Load Word Left) */
static fastcall int mips64_exec_LWL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LWL,base,offset,rt,TRUE);
   return(0);
}

/* LWR (Load Word Right) */
static fastcall int mips64_exec_LWR(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LWR,base,offset,rt,TRUE);
   return(0);
}

/* LWU (Load Word Unsigned) */
static fastcall int mips64_exec_LWU(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_LWU,base,offset,rt,TRUE);
   return(0);
}

/* MFC0 */
static fastcall int mips64_exec_MFC0(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_cp0_exec_mfc0(cpu,rt,rd);
   return(0);
}

/* MFC1 */
static fastcall int mips64_exec_MFC1(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_exec_mfc1(cpu,rt,rd);
   return(0);
}

/* MFHI */
static fastcall int mips64_exec_MFHI(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rd = bits(insn,11,15);

   if (rd) cpu->gpr[rd] = cpu->hi;
   return(0);
}

/* MFLO */
static fastcall int mips64_exec_MFLO(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rd = bits(insn,11,15);

   if (rd) cpu->gpr[rd] = cpu->lo;
   return(0);
}

/* MOVE (virtual instruction, real: ADDU) */
static fastcall int mips64_exec_MOVE(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rd = bits(insn,11,15);

   cpu->gpr[rd] = sign_extend(cpu->gpr[rs],32);
   return(0);
}

/* MTC0 */
static fastcall int mips64_exec_MTC0(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_cp0_exec_mtc0(cpu,rt,rd);
   return(0);
}

/* MTC1 */
static fastcall int mips64_exec_MTC1(cpu_mips_t *cpu,mips_insn_t insn)
{	
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   mips64_exec_mtc1(cpu,rt,rd);
   return(0);
}

/* MTHI */
static fastcall int mips64_exec_MTHI(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);

   cpu->hi = cpu->gpr[rs];
   return(0);
}

/* MTLO */
static fastcall int mips64_exec_MTLO(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);

   cpu->lo = cpu->gpr[rs];
   return(0);
}

/* MUL */
static fastcall int mips64_exec_MUL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   m_int32_t val;

   /* note: after this instruction, HI/LO regs are undefined */
   val = (m_int32_t)cpu->gpr[rs] * (m_int32_t)cpu->gpr[rt];
   cpu->gpr[rd] = sign_extend(val,32);
   return(0);
}

/* MULT */
static fastcall int mips64_exec_MULT(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   m_int64_t val;

   val = (m_int64_t)(m_int32_t)cpu->gpr[rs];
   val *= (m_int64_t)(m_int32_t)cpu->gpr[rt];

   cpu->lo = sign_extend(val,32);
   cpu->hi = sign_extend(val >> 32,32);
   return(0);
}

/* MULTU */
static fastcall int mips64_exec_MULTU(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   m_uint64_t val;

   val = (m_uint64_t)(m_uint32_t)cpu->gpr[rs];
   val *= (m_uint64_t)(m_uint32_t)cpu->gpr[rt];
   cpu->lo = sign_extend(val,32);
   cpu->hi = sign_extend(val >> 32,32);
   return(0);
}

/* NOP */
static fastcall int mips64_exec_NOP(cpu_mips_t *cpu,mips_insn_t insn)
{
   return(0);
}

/* NOR */
static fastcall int mips64_exec_NOR(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   cpu->gpr[rd] = ~(cpu->gpr[rs] | cpu->gpr[rt]);
   return(0);
}

/* OR */
static fastcall int mips64_exec_OR(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   cpu->gpr[rd] = cpu->gpr[rs] | cpu->gpr[rt];
   return(0);
}

/* ORI */
static fastcall int mips64_exec_ORI(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   cpu->gpr[rt] = cpu->gpr[rs] | imm;
   return(0);
}

/* PREF */
static fastcall int mips64_exec_PREF(cpu_mips_t *cpu,mips_insn_t insn)
{
   return(0);
}

/* PREFI */
static fastcall int mips64_exec_PREFI(cpu_mips_t *cpu,mips_insn_t insn)
{
   return(0);
}

/* SB (Store Byte) */
static fastcall int mips64_exec_SB(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_SB,base,offset,rt,FALSE);
   return(0);
}

/* SC (Store Conditional) */
static fastcall int mips64_exec_SC(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_SC,base,offset,rt,TRUE);
   return(0);
}

/* SD (Store Double-Word) */
static fastcall int mips64_exec_SD(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_SD,base,offset,rt,FALSE);
   return(0);
}

/* SDL (Store Double-Word Left) */
static fastcall int mips64_exec_SDL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);
   
   mips64_exec_memop2(cpu,MIPS_MEMOP_SDL,base,offset,rt,FALSE);
   return(0);
}

/* SDR (Store Double-Word Right) */
static fastcall int mips64_exec_SDR(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_SDR,base,offset,rt,FALSE);
   return(0);
}

/* SDC1 (Store Double-Word from Coprocessor 1) */
static fastcall int mips64_exec_SDC1(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int ft     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_SDC1,base,offset,ft,FALSE);
   return(0);
}

/* SH (Store Half-Word) */
static fastcall int mips64_exec_SH(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_SH,base,offset,rt,FALSE);
   return(0);
}

/* SLL */
static fastcall int mips64_exec_SLL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);
   m_uint32_t res;
   
   res = (m_uint32_t)cpu->gpr[rt] << sa;
   cpu->gpr[rd] = sign_extend(res,32);
   return(0);
}

/* SLLV */
static fastcall int mips64_exec_SLLV(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   m_uint32_t res;
   
   res = (m_uint32_t)cpu->gpr[rt] << (cpu->gpr[rs] & 0x1f);
   cpu->gpr[rd] = sign_extend(res,32);
   return(0);
}

/* SLT */
static fastcall int mips64_exec_SLT(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   if ((m_int64_t)cpu->gpr[rs] < (m_int64_t)cpu->gpr[rt])
      cpu->gpr[rd] = 1;
   else
      cpu->gpr[rd] = 0;

   return(0);
}

/* SLTI */
static fastcall int mips64_exec_SLTI(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_int64_t val = sign_extend(imm,16);

   if ((m_int64_t)cpu->gpr[rs] < val)
      cpu->gpr[rt] = 1;
   else
      cpu->gpr[rt] = 0;

   return(0);
}

/* SLTIU */
static fastcall int mips64_exec_SLTIU(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);

   if (cpu->gpr[rs] < val)
      cpu->gpr[rt] = 1;
   else
      cpu->gpr[rt] = 0;

   return(0);
}

/* SLTU */
static fastcall int mips64_exec_SLTU(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   if (cpu->gpr[rs] < cpu->gpr[rt])
      cpu->gpr[rd] = 1;
   else
      cpu->gpr[rd] = 0;

   return(0);
}

/* SRA */
static fastcall int mips64_exec_SRA(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);
   m_int32_t res;
   
   res = (m_int32_t)cpu->gpr[rt] >> sa;
   cpu->gpr[rd] = sign_extend(res,32);
   return(0);
}

/* SRAV */
static fastcall int mips64_exec_SRAV(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   m_int32_t res;
   
   res = (m_int32_t)cpu->gpr[rt] >> (cpu->gpr[rs] & 0x1f);
   cpu->gpr[rd] = sign_extend(res,32);
   return(0);
}

/* SRL */
static fastcall int mips64_exec_SRL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   int sa = bits(insn,6,10);
   m_uint32_t res;
   
   res = (m_uint32_t)cpu->gpr[rt] >> sa;
   cpu->gpr[rd] = sign_extend(res,32);
   return(0);
}

/* SRLV */
static fastcall int mips64_exec_SRLV(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   m_uint32_t res;
   
   res = (m_uint32_t)cpu->gpr[rt] >> (cpu->gpr[rs] & 0x1f);
   cpu->gpr[rd] = sign_extend(res,32);
   return(0);
}

/* SUB */
static fastcall int mips64_exec_SUB(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   m_uint32_t res;

   /* TODO: Exception handling */
   res = (m_uint32_t)cpu->gpr[rs] - (m_uint32_t)cpu->gpr[rt];
   cpu->gpr[rd] = sign_extend(res,32);
   return(0);
}

/* SUBU */
static fastcall int mips64_exec_SUBU(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);
   m_uint32_t res;

   res = (m_uint32_t)cpu->gpr[rs] - (m_uint32_t)cpu->gpr[rt];
   cpu->gpr[rd] = sign_extend(res,32);
   return(0);
}

/* SW (Store Word) */
static fastcall int mips64_exec_SW(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_SW,base,offset,rt,FALSE);
   return(0);
}

/* SWL (Store Word Left) */
static fastcall int mips64_exec_SWL(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);
   
   mips64_exec_memop2(cpu,MIPS_MEMOP_SWL,base,offset,rt,FALSE);
   return(0);
}

/* SWR (Store Word Right) */
static fastcall int mips64_exec_SWR(cpu_mips_t *cpu,mips_insn_t insn)
{
   int base   = bits(insn,21,25);
   int rt     = bits(insn,16,20);
   int offset = bits(insn,0,15);

   mips64_exec_memop2(cpu,MIPS_MEMOP_SWR,base,offset,rt,FALSE);
   return(0);
}

/* SYNC */
static fastcall int mips64_exec_SYNC(cpu_mips_t *cpu,mips_insn_t insn)
{
   return(0);
}

/* SYSCALL */
static fastcall int mips64_exec_SYSCALL(cpu_mips_t *cpu,mips_insn_t insn)
{
   mips64_exec_syscall(cpu);
   return(1);
}

/* TEQ (Trap if Equal) */
static fastcall int mips64_exec_TEQ(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);

   if (unlikely(cpu->gpr[rs] == cpu->gpr[rt])) {
      mips64_trigger_trap_exception(cpu);
      return(1);
   }

   return(0);
}

/* TEQI (Trap if Equal Immediate) */
static fastcall int mips64_exec_TEQI(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int imm = bits(insn,0,15);
   m_uint64_t val = sign_extend(imm,16);

   if (unlikely(cpu->gpr[rs] == val)) {
      mips64_trigger_trap_exception(cpu);
      return(1);
   }

   return(0);
}

/* TLBP */
static fastcall int mips64_exec_TLBP(cpu_mips_t *cpu,mips_insn_t insn)
{
   mips64_cp0_exec_tlbp(cpu);
   return(0);
}

/* TLBR */
static fastcall int mips64_exec_TLBR(cpu_mips_t *cpu,mips_insn_t insn)
{
   mips64_cp0_exec_tlbr(cpu);
   return(0);
}

/* TLBWI */
static fastcall int mips64_exec_TLBWI(cpu_mips_t *cpu,mips_insn_t insn)
{
   mips64_cp0_exec_tlbwi(cpu);
   return(0);
}

/* TLBWR */
static fastcall int mips64_exec_TLBWR(cpu_mips_t *cpu,mips_insn_t insn)
{
   mips64_cp0_exec_tlbwr(cpu);
   return(0);
}

/* XOR */
static fastcall int mips64_exec_XOR(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs = bits(insn,21,25);
   int rt = bits(insn,16,20);
   int rd = bits(insn,11,15);

   cpu->gpr[rd] = cpu->gpr[rs] ^ cpu->gpr[rt];
   return(0);
}

/* XORI */
static fastcall int mips64_exec_XORI(cpu_mips_t *cpu,mips_insn_t insn)
{
   int rs  = bits(insn,21,25);
   int rt  = bits(insn,16,20);
   int imm = bits(insn,0,15);

   cpu->gpr[rt] = cpu->gpr[rs] ^ imm;
   return(0);
}

/* MIPS instruction array */
static struct mips64_insn_exec_tag mips64_exec_tags[] = {
   { "li"     , mips64_exec_LI      , 0xffe00000 , 0x24000000, 1, 16 },
   { "move"   , mips64_exec_MOVE    , 0xfc1f07ff , 0x00000021, 1, 15 },
   { "b"      , mips64_exec_B       , 0xffff0000 , 0x10000000, 0, 10 },
   { "bal"    , mips64_exec_BAL     , 0xffff0000 , 0x04110000, 0, 10 },
   { "beqz"   , mips64_exec_BEQZ    , 0xfc1f0000 , 0x10000000, 0, 9 },
   { "bnez"   , mips64_exec_BNEZ    , 0xfc1f0000 , 0x14000000, 0, 9 },
   { "add"    , mips64_exec_ADD     , 0xfc0007ff , 0x00000020, 1, 3 },
   { "addi"   , mips64_exec_ADDI    , 0xfc000000 , 0x20000000, 1, 6 },
   { "addiu"  , mips64_exec_ADDIU   , 0xfc000000 , 0x24000000, 1, 6 },
   { "addu"   , mips64_exec_ADDU    , 0xfc0007ff , 0x00000021, 1, 3 },
   { "and"    , mips64_exec_AND     , 0xfc0007ff , 0x00000024, 1, 3 },
   { "andi"   , mips64_exec_ANDI    , 0xfc000000 , 0x30000000, 1, 5 },
   { "beq"    , mips64_exec_BEQ     , 0xfc000000 , 0x10000000, 0, 8 },
   { "beql"   , mips64_exec_BEQL    , 0xfc000000 , 0x50000000, 0, 8 },
   { "bgez"   , mips64_exec_BGEZ    , 0xfc1f0000 , 0x04010000, 0, 9 },
   { "bgezal" , mips64_exec_BGEZAL  , 0xfc1f0000 , 0x04110000, 0, 9 },
   { "bgezall", mips64_exec_BGEZALL , 0xfc1f0000 , 0x04130000, 0, 9 },
   { "bgezl"  , mips64_exec_BGEZL   , 0xfc1f0000 , 0x04030000, 0, 9 },
   { "bgtz"   , mips64_exec_BGTZ    , 0xfc1f0000 , 0x1c000000, 0, 9 },
   { "bgtzl"  , mips64_exec_BGTZL   , 0xfc1f0000 , 0x5c000000, 0, 9 },
   { "blez"   , mips64_exec_BLEZ    , 0xfc1f0000 , 0x18000000, 0, 9 },
   { "blezl"  , mips64_exec_BLEZL   , 0xfc1f0000 , 0x58000000, 0, 9 },
   { "bltz"   , mips64_exec_BLTZ    , 0xfc1f0000 , 0x04000000, 0, 9 },
   { "bltzal" , mips64_exec_BLTZAL  , 0xfc1f0000 , 0x04100000, 0, 9 },
   { "bltzall", mips64_exec_BLTZALL , 0xfc1f0000 , 0x04120000, 0, 9 },
   { "bltzl"  , mips64_exec_BLTZL   , 0xfc1f0000 , 0x04020000, 0, 9 },
   { "bne"    , mips64_exec_BNE     , 0xfc000000 , 0x14000000, 0, 8 },
   { "bnel"   , mips64_exec_BNEL    , 0xfc000000 , 0x54000000, 0, 8 },
   { "break"  , mips64_exec_BREAK   , 0xfc00003f , 0x0000000d, 1, 0 },
   { "cache"  , mips64_exec_CACHE   , 0xfc000000 , 0xbc000000, 1, 2 },
   { "cfc0"   , mips64_exec_CFC0    , 0xffe007ff , 0x40400000, 1, 18 },
   { "ctc0"   , mips64_exec_CTC0    , 0xffe007ff , 0x40600000, 1, 18 },
   { "daddiu" , mips64_exec_DADDIU  , 0xfc000000 , 0x64000000, 1, 5 },
   { "daddu"  , mips64_exec_DADDU   , 0xfc0007ff , 0x0000002d, 1, 3 },
   { "div"    , mips64_exec_DIV     , 0xfc00ffff , 0x0000001a, 1, 17 },
   { "divu"   , mips64_exec_DIVU    , 0xfc00ffff , 0x0000001b, 1, 17 },
   { "dmfc0"  , mips64_exec_DMFC0   , 0xffe007f8 , 0x40200000, 1, 18 },
   { "dmfc1"  , mips64_exec_DMFC1   , 0xffe007ff , 0x44200000, 1, 19 },
   { "dmtc0"  , mips64_exec_DMTC0   , 0xffe007f8 , 0x40a00000, 1, 18 },
   { "dmtc1"  , mips64_exec_DMTC1   , 0xffe007ff , 0x44a00000, 1, 19 },
   { "dsll"   , mips64_exec_DSLL    , 0xffe0003f , 0x00000038, 1, 7 },
   { "dsll32" , mips64_exec_DSLL32  , 0xffe0003f , 0x0000003c, 1, 7 },
   { "dsllv"  , mips64_exec_DSLLV   , 0xfc0007ff , 0x00000014, 1, 4 },
   { "dsra"   , mips64_exec_DSRA    , 0xffe0003f , 0x0000003b, 1, 7 },
   { "dsra32" , mips64_exec_DSRA32  , 0xffe0003f , 0x0000003f, 1, 7 },
   { "dsrav"  , mips64_exec_DSRAV   , 0xfc0007ff , 0x00000017, 1, 4 },
   { "dsrl"   , mips64_exec_DSRL    , 0xffe0003f , 0x0000003a, 1, 7 },
   { "dsrl32" , mips64_exec_DSRL32  , 0xffe0003f , 0x0000003e, 1, 7 },
   { "dsrlv"  , mips64_exec_DSRLV   , 0xfc0007ff , 0x00000016, 1, 4 },
   { "dsubu"  , mips64_exec_DSUBU   , 0xfc0007ff , 0x0000002f, 1, 3 },
   { "eret"   , mips64_exec_ERET    , 0xffffffff , 0x42000018, 0, 1 },
   { "j"      , mips64_exec_J       , 0xfc000000 , 0x08000000, 0, 11 },
   { "jal"    , mips64_exec_JAL     , 0xfc000000 , 0x0c000000, 0, 11 },
   { "jalr"   , mips64_exec_JALR    , 0xfc1f003f , 0x00000009, 0, 15 },
   { "jr"     , mips64_exec_JR      , 0xfc1ff83f , 0x00000008, 0, 13 },
   { "lb"     , mips64_exec_LB      , 0xfc000000 , 0x80000000, 1, 2 },
   { "lbu"    , mips64_exec_LBU     , 0xfc000000 , 0x90000000, 1, 2 },
   { "ld"     , mips64_exec_LD      , 0xfc000000 , 0xdc000000, 1, 2 },
   { "ldc1"   , mips64_exec_LDC1    , 0xfc000000 , 0xd4000000, 1, 3 },
   { "ldl"    , mips64_exec_LDL     , 0xfc000000 , 0x68000000, 1, 2 },
   { "ldr"    , mips64_exec_LDR     , 0xfc000000 , 0x6c000000, 1, 2 },
   { "lh"     , mips64_exec_LH      , 0xfc000000 , 0x84000000, 1, 2 },
   { "lhu"    , mips64_exec_LHU     , 0xfc000000 , 0x94000000, 1, 2 },
   { "ll"     , mips64_exec_LL      , 0xfc000000 , 0xc0000000, 1, 2 },
   { "lui"    , mips64_exec_LUI     , 0xffe00000 , 0x3c000000, 1, 16 },
   { "lw"     , mips64_exec_LW      , 0xfc000000 , 0x8c000000, 1, 2 },
   { "lwl"    , mips64_exec_LWL     , 0xfc000000 , 0x88000000, 1, 2 },
   { "lwr"    , mips64_exec_LWR     , 0xfc000000 , 0x98000000, 1, 2 },
   { "lwu"    , mips64_exec_LWU     , 0xfc000000 , 0x9c000000, 1, 2 },
   { "mfc0"   , mips64_exec_MFC0    , 0xffe007ff , 0x40000000, 1, 18 },
   { "mfc0_1" , mips64_exec_CFC0    , 0xffe007ff , 0x40000001, 1, 19 },
   { "mfc1"   , mips64_exec_MFC1    , 0xffe007ff , 0x44000000, 1, 19 },
   { "mfhi"   , mips64_exec_MFHI    , 0xffff07ff , 0x00000010, 1, 14 },
   { "mflo"   , mips64_exec_MFLO    , 0xffff07ff , 0x00000012, 1, 14 },
   { "mtc0"   , mips64_exec_MTC0    , 0xffe007ff , 0x40800000, 1, 18 },
   { "mtc1"   , mips64_exec_MTC1    , 0xffe007ff , 0x44800000, 1, 19 },
   { "mthi"   , mips64_exec_MTHI    , 0xfc1fffff , 0x00000011, 1, 13 },
   { "mtlo"   , mips64_exec_MTLO    , 0xfc1fffff , 0x00000013, 1, 13 },
   { "mul"    , mips64_exec_MUL     , 0xfc0007ff , 0x70000002, 1, 4 },
   { "mult"   , mips64_exec_MULT    , 0xfc00ffff , 0x00000018, 1, 17 },
   { "multu"  , mips64_exec_MULTU   , 0xfc00ffff , 0x00000019, 1, 17 },
   { "nop"    , mips64_exec_NOP     , 0xffffffff , 0x00000000, 1, 1 },
   { "nor"    , mips64_exec_NOR     , 0xfc0007ff , 0x00000027, 1, 3 },
   { "or"     , mips64_exec_OR      , 0xfc0007ff , 0x00000025, 1, 3 },
   { "ori"    , mips64_exec_ORI     , 0xfc000000 , 0x34000000, 1, 5 },
   { "pref"   , mips64_exec_PREF    , 0xfc000000 , 0xcc000000, 1, 0 },
   { "prefi"  , mips64_exec_PREFI   , 0xfc0007ff , 0x4c00000f, 1, 0 },
   { "sb"     , mips64_exec_SB      , 0xfc000000 , 0xa0000000, 1, 2 },
   { "sc"     , mips64_exec_SC      , 0xfc000000 , 0xe0000000, 1, 2 },
   { "sd"     , mips64_exec_SD      , 0xfc000000 , 0xfc000000, 1, 2 },
   { "sdc1"   , mips64_exec_SDC1    , 0xfc000000 , 0xf4000000, 1, 3 },
   { "sdl"    , mips64_exec_SDL     , 0xfc000000 , 0xb0000000, 1, 2 },
   { "sdr"    , mips64_exec_SDR     , 0xfc000000 , 0xb4000000, 1, 2 },
   { "sh"     , mips64_exec_SH      , 0xfc000000 , 0xa4000000, 1, 2 },
   { "sll"    , mips64_exec_SLL     , 0xffe0003f , 0x00000000, 1, 7 },
   { "sllv"   , mips64_exec_SLLV    , 0xfc0007ff , 0x00000004, 1, 4 },
   { "slt"    , mips64_exec_SLT     , 0xfc0007ff , 0x0000002a, 1, 3 },
   { "slti"   , mips64_exec_SLTI    , 0xfc000000 , 0x28000000, 1, 5 },
   { "sltiu"  , mips64_exec_SLTIU   , 0xfc000000 , 0x2c000000, 1, 5 },
   { "sltu"   , mips64_exec_SLTU    , 0xfc0007ff , 0x0000002b, 1, 3 },
   { "sra"    , mips64_exec_SRA     , 0xffe0003f , 0x00000003, 1, 7 },
   { "srav"   , mips64_exec_SRAV    , 0xfc0007ff , 0x00000007, 1, 4 },
   { "srl"    , mips64_exec_SRL     , 0xffe0003f , 0x00000002, 1, 7 },
   { "srlv"   , mips64_exec_SRLV    , 0xfc0007ff , 0x00000006, 1, 4 },
   { "sub"    , mips64_exec_SUB     , 0xfc0007ff , 0x00000022, 1, 3 },
   { "subu"   , mips64_exec_SUBU    , 0xfc0007ff , 0x00000023, 1, 3 },
   { "sw"     , mips64_exec_SW      , 0xfc000000 , 0xac000000, 1, 2 },
   { "swl"    , mips64_exec_SWL     , 0xfc000000 , 0xa8000000, 1, 2 },
   { "swr"    , mips64_exec_SWR     , 0xfc000000 , 0xb8000000, 1, 2 },
   { "sync"   , mips64_exec_SYNC    , 0xfffff83f , 0x0000000f, 1, 1 },
   { "syscall", mips64_exec_SYSCALL , 0xfc00003f , 0x0000000c, 1, 1 },
   { "teq"    , mips64_exec_TEQ     , 0xfc00003f , 0x00000034, 1, 17 },
   { "teqi"   , mips64_exec_TEQI    , 0xfc1f0000 , 0x040c0000, 1, 20 },
   { "tlbp"   , mips64_exec_TLBP    , 0xffffffff , 0x42000008, 1, 1 },
   { "tlbr"   , mips64_exec_TLBR    , 0xffffffff , 0x42000001, 1, 1 },
   { "tlbwi"  , mips64_exec_TLBWI   , 0xffffffff , 0x42000002, 1, 1 },
   { "tlbwr"  , mips64_exec_TLBWR   , 0xffffffff , 0x42000006, 1, 1 },
   { "xor"    , mips64_exec_XOR     , 0xfc0007ff , 0x00000026, 1, 3 },
   { "xori"   , mips64_exec_XORI    , 0xfc000000 , 0x38000000, 1, 5 },
   { "unknown", mips64_exec_unknown , 0x00000000 , 0x00000000, 1, 0 },
   { NULL     , NULL                , 0x00000000 , 0x00000000, 1, 0 },
};

#endif
