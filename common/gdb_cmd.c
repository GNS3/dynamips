/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Added by : Sebastian 'topo' Muniz
 * Contact  : sebastianmuniz@gmail.com
 *
 * VM debugging with GDB stub routines.
 */

#include "gdb_cmd.h"
#include "gdb_proto.h"

#include "stdlib.h"

#include "vm.h"
#include "mips64_exec.h"
#include "ppc32_exec.h"

/* 
 * convert the memory pointed to by mem into hex, placing result in buf
 * return a pointer to the last char put in buf (null)
 */

int gdb_cmd_read_mem(gdb_debug_context_t* ctx)
{
        int addr, length;
        void* ptr_mem;

        if (parse2hexnum(&ctx->inbuf[1], (int*)&addr, &length))
        {
            //if (ctx->vm->boot_cpu->type == CPU_TYPE_MIPS64)
                addr = addr & ~0xe0000000;

            if (gdb_debug)
               gdb_printf("Reading %d bytes at memory address 0x%x\n", length, addr);
               
            ptr_mem = malloc(length);
            physmem_copy_from_vm(ctx->vm, ptr_mem, addr, length);
            mem2hex((char*) ptr_mem, ctx->outbuf, length);
            free(ptr_mem);
        }
        else
        {
            strcpy(ctx->outbuf,"E01");
            
            if (gdb_debug)
                gdb_printf("malformed read memory command: %s", ctx->inbuf);
        }     
        return 1;
}

/*
 * gdb_cmd_write_mem:
 * Write bytes from the gdb client command buffer to our memory
 */
void gdb_cmd_write_mem(gdb_debug_context_t *ctx)
{
        int length, address;
        char *ptr;
        void* ptr_mem;
        //int i;

        //
        // NOT WORKING !!!!!!!!!!!!!!!!!!!!!!!
        //
        if (parse2hexnum(&ctx->inbuf[1], &address, &length))
        {
            ptr = strchr(ctx->inbuf,':');
            ptr += 1; /* point 1 past the colon */
            
            if (ctx->vm->boot_cpu->type == CPU_TYPE_MIPS64)
                address = address & ~0xe0000000;

            ptr_mem = malloc(length);
            hex2mem(ptr, ptr_mem, length);
            //gdb_printf("writting 0x%X bytes to 0x%X:\n", length, address);
            //for (i = 0; i < length; i++)
            //    gdb_printf("%X ", (char)*(ptr_mem + i));
            physmem_copy_to_vm(ctx->vm, ptr_mem, address, length);
            free(ptr_mem);
            strcpy(ctx->outbuf,"OK");
        }
        else
        {
            strcpy(ctx->outbuf,"E02");
            if (gdb_debug)
                gdb_printf("malformed write memory command: %s",ctx->inbuf);
        }
}

/*
 * gdb_proc_continue:
 * Restart a process at an optional address
 */
void gdb_cmd_proc_continue(gdb_debug_context_t *ctx, m_uint64_t address, int stepping)
{
    /* 
     * try to read optional parameter, address unchanged if no parm
     */
    if (address) 
    {
        if (gdb_debug)
            gdb_printf("New PC : 0x%llx", address);
            
        switch(ctx->vm->boot_cpu->type)
        {
        case CPU_TYPE_MIPS64:
            CPU_MIPS64(ctx->vm->boot_cpu)->pc = address;
        case CPU_TYPE_PPC32:
            CPU_PPC32(ctx->vm->boot_cpu)->ia = address;
        }
    }

    if (stepping)
    {
        gdb_cmd_proc_step(ctx);
    }
    else
    {
        gdb_cmd_proc_step(ctx);
        vm_resume(ctx->vm);
    }
}

/*
 * gdb_proc_step:
 * Execute one step at an optional address
 */
void gdb_cmd_proc_step(gdb_debug_context_t *ctx)
{
   switch(ctx->vm->boot_cpu->type)
   {
      case CPU_TYPE_MIPS64:
        gdb_mips64_exec_single_step_cpu(ctx->vm->boot_cpu);
        break;
      case CPU_TYPE_PPC32:
        gdb_ppc32_exec_single_step_cpu(ctx->vm->boot_cpu);
        break;
   }
}

/* Run MIPS code in step-by-step mode */
void gdb_mips64_exec_single_step_cpu(cpu_gen_t *gen)
{   
   cpu_mips_t *cpu = CPU_MIPS64(gen);
   mips_insn_t insn;
   int res;

   /* Reset "zero register" (for safety) */
   cpu->gpr[0] = 0;

   /* Fetch and execute the instruction */      
   mips64_exec_fetch(cpu,cpu->pc,&insn);
   res = mips64_exec_single_instruction(cpu,insn);

   /* Normal flow ? */
   if (likely(!res))
      cpu->pc += sizeof(mips_insn_t);
}

/* Execute a single step on PowerPC code in step-by-step mode */
void gdb_ppc32_exec_single_step_cpu(cpu_gen_t *gen)
{   
   cpu_ppc_t *cpu = CPU_PPC32(gen);
   ppc_insn_t insn;
   int res;

   /* Increment the time base */
   //cpu->tb += 100;

   /* Fetch and execute the instruction */
   if (ppc32_exec_fetch(cpu,cpu->ia,&insn))
       return;
       
   cpu->ia_prev = cpu->ia;
   res = ppc32_exec_single_instruction(cpu,insn);

   /* Normal flow ? */
   if (likely(!res)) cpu->ia += sizeof(ppc_insn_t);
}

/*
 * gdb_cmd_set_cpu_regs:
 * Set the content of the CPU registers according to the current architecture
 */
void gdb_cmd_set_cpu_regs(gdb_debug_context_t* ctx)
{
   strcpy(ctx->outbuf,"OK");
}

/*
 * gdb_cmd_set_cpu_reg_mips64:
 * Set the content of a CPU register according to the MIPS architecture
 */
void gdb_cmd_set_cpu_reg_mips64(gdb_debug_context_t* ctx)
{
        cpu_mips_t *pcpu = CPU_MIPS64(ctx->vm->boot_cpu);
        int reg_n=0;
        m_uint64_t val=0;
        char *ptr;
        
        if (!parsehexnum(ctx->inbuf + 1, &reg_n))
                return;
                
        ptr = strchr(ctx->inbuf + 2,'=');
        ptr += 1; /* point 1 past the colon */

        hex2mem(ptr, (char*)&val, 4);
        val <<= 32;
        val += 0xffffffff;
        
        if (reg_n < 32) {
                if (gdb_debug)
                        gdb_printf("Set GPR CPU register number %d to value 0x%llx\n", reg_n, htovm64(val));
                pcpu->gpr[reg_n] = htovm64(val);
        }
        else {
                if (gdb_debug)
                        gdb_printf("Set CPU register number %d to value 0x%llx\n", reg_n, htovm64(val));
                //if (env->CP0_Config1 & (1 << CP0C1_FP)) {
                //        if (reg_n >= 38 && reg_n < 70) {
                //            if (env->CP0_Status & (1 << CP0St_FR))
                //                GET_REGL(env->active_fpu.fpr[reg_n - 38].d);
                //            else
                //                GET_REGL(env->active_fpu.fpr[reg_n - 38].w[FP_ENDIAN_IDX]);
                //}
                switch (reg_n) {
                case 70:
                        //GET_REGL((int32_t)env->active_fpu.fcr31);
                        break;
                case 71: 
                        //GET_REGL((int32_t)env->active_fpu.fcr0);
                        break;

                case 32: 
                        //GET_REGL((int32_t)env->CP0_Status);
                        break;
                case 33: 
                        //GET_REGL(env->active_tc.LO[0]);
                        pcpu->lo = htovm64(val);
                        break;
                case 34: 
                        //GET_REGL(env->active_tc.PC);
                        //preg = vmtoh32(pcpu->pc);
                        pcpu->pc = htovm64(val);
                        break;
                case 35: 
                        //GET_REGL(env->CP0_BadVAddr);
                        break;
                case 36: 
                        //GET_REGL((int32_t)env->CP0_Cause);
                        break;
                case 37: 
                        //GET_REGL(env->active_tc.HI[0]);
                        pcpu->hi = htovm64(val);
                        break;
                case 72: 
                        //GET_REGL(0); /* fp */
                        pcpu->gpr[30] = htovm64(val);
                        break;
                case 89: 
                        //GET_REGL((int32_t)env->CP0_PRid);
                        pcpu->cp0.reg[MIPS_CP0_PRID] = htovm64(val);
                        break;
                }
                if (reg_n >= 73 && reg_n <= 88) {
                        /* 16 embedded regs.  */
                        //GET_REGL(0);
                }
        }

        strcpy(ctx->outbuf,"OK");
}
/*
 * gdb_cmd_set_cpu_reg_ppc32:
 * Set the content of a CPU register according to the PowerPC architecture
 */
void gdb_cmd_set_cpu_reg_ppc32(gdb_debug_context_t* ctx)
{
        cpu_ppc_t *pcpu = CPU_PPC32(ctx->vm->boot_cpu);
        int reg_n=0;
        m_uint32_t val=0;
        char *ptr;
        
        if (!parsehexnum(ctx->inbuf + 1, &reg_n))
                return;
                
        ptr = strchr(ctx->inbuf + 2,'=');
        ptr += 1; /* point 1 past the colon */

        hex2mem(ptr, (char*)&val, 4);
        
        if (reg_n < 32) {
                if (gdb_debug)
                        gdb_printf("Set GPR CPU register number %d to value 0x%llx\n", reg_n, htovm32(val));
                pcpu->gpr[reg_n] = htovm32(val);
        }
        else
        {
                if (gdb_debug)
                        gdb_printf("Set CPU register number %d to value 0x%llx\n", reg_n, htovm32(val));

                switch (reg_n)
                {
                        case 32:
                            pcpu->ia = htovm32(val);
                            break;
                        case 65:
                            //ppc_store_msr(env, ldtul_p(mem_buf));
                            break;
                        case 66:
                            {
                                //uint32_t cr = ldl_p(mem_buf);
                                //int i;
                                //for (i = 0; i < 8; i++)
                                //    env->crf[i] = (cr >> (32 - ((i + 1) * 4))) & 0xF;
                                //return 4;
                            break;
                            }
                        case 67:
                            //pcpu->lr = htovm32(val);
                            break;
                        case 68:
                            //pcpu->ctr = htovm32(val);
                            break;
                        case 69:
                            //pcpu->xer = htovm32(val);
                            break;
                        case 70:
                            ///* fpscr */
                            //if (gdb_has_xml)
                            //    return 0;
                            //return 4;
                            break;
                }
        }

        strcpy(ctx->outbuf,"OK");
}

void gdb_cmd_get_cpu_reg_mips64(gdb_debug_context_t* ctx)
{
        cpu_mips_t *pcpu = CPU_MIPS64(ctx->vm->boot_cpu);
        int n=0;
        int val=0;
        
        if (!parsehexnum(ctx->inbuf + 1, &n))
                return;
        
        if (n < 32) {
                if (gdb_debug)
                        gdb_printf("Get GPR CPU register number %d\n", n);
                val = vmtoh32(pcpu->gpr[n]);
        }
        else {
                if (gdb_debug)
                        gdb_printf("Get CPU register number %d\n", n);
                //if (env->CP0_Config1 & (1 << CP0C1_FP)) {
                //        if (n >= 38 && n < 70) {
                //            if (env->CP0_Status & (1 << CP0St_FR))
                //                GET_REGL(env->active_fpu.fpr[n - 38].d);
                //            else
                //                GET_REGL(env->active_fpu.fpr[n - 38].w[FP_ENDIAN_IDX]);
                //}
                switch (n) {
                case 70:
                        //GET_REGL((int32_t)env->active_fpu.fcr31);
                        val = vmtoh32(0);
                        break;
                case 71: 
                        //GET_REGL((int32_t)env->active_fpu.fcr0);
                        val = vmtoh32(0);
                        break;

                case 32: 
                        //GET_REGL((int32_t)env->CP0_Status);
                        val = vmtoh32(0);
                        break;
                case 33: 
                        //GET_REGL(env->active_tc.LO[0]);
                        val = vmtoh32(pcpu->lo);
                        break;
                case 34: 
                        //GET_REGL(env->active_tc.HI[0]);
                        val = vmtoh32(pcpu->hi);
                        break;
                case 35: 
                        //GET_REGL(env->CP0_BadVAddr);
                        val = vmtoh32(0);
                        break;
                case 36: 
                        //GET_REGL((int32_t)env->CP0_Cause);
                        val = vmtoh32(0);
                        break;
                case 37: 
                        //GET_REGL(env->active_tc.PC);
                        //preg = vmtoh32(pcpu->pc);
                        val = vmtoh32(pcpu->pc);
                        break;
                case 72: 
                        //GET_REGL(0); /* fp */
                        val = vmtoh32(pcpu->gpr[30]);
                        break;
                case 89: 
                        //GET_REGL((int32_t)env->CP0_PRid);
                        val = vmtoh32(pcpu->cp0.reg[MIPS_CP0_PRID]);
                        break;
                }
                if (n >= 73 && n <= 88) {
                        /* 16 embedded regs.  */
                        //GET_REGL(0);
                }
        }
        mem2hex((char*)&val, ctx->outbuf, 4);

}

void gdb_cmd_get_cpu_reg_ppc32(gdb_debug_context_t* ctx)
{
        cpu_ppc_t *pcpu = CPU_PPC32(ctx->vm->boot_cpu);
        int n=0;
        int val=0;
        
        if (!parsehexnum(ctx->inbuf + 1, &n))
            return;
        
        if (n < 32)
        {
                if (gdb_debug)
                        gdb_printf("Get GPR CPU register number %d\n", n);
                val = vmtoh32(pcpu->gpr[n]);
        }
        else if (n < 64)
        {
                if (gdb_debug)
                        gdb_printf("Get FPR CPU register not implemented!!!!\n");
                val = 0;
                /*if (gdb_has_xml)
                    return 0;
                stfq_p(mem_buf, env->fpr[n-32]);
                return 8;*/
        }
        else
        {
                if (gdb_debug)
                        gdb_printf("Get SPR CPU register number %d\n", n);
                switch (n) {
                        case 64:
                                //GET_REGL(env->nip);
                                val = vmtoh32(pcpu->ia);
                                break;
                        case 65:
                                //GET_REGL(env->msr);
                                val = vmtoh32(0);
                                break;
                        case 66:
                            {/*
                                uint32_t cr = 0;
                                int i;
                                for (i = 0; i < 8; i++)
                                    cr |= env->crf[i] << (32 - ((i + 1) * 4));
                                GET_REG32(cr);*/
                                val = 0;
                            }
                            break;
                        case 67: 
                                //GET_REGL(env->lr);
                                val = vmtoh32(pcpu->lr);
                                break;
                        case 68: 
                                //GET_REGL(env->ctr);
                                val = vmtoh32(0);
                                break;
                        case 69: 
                                //GET_REGL(env->xer);
                                val = vmtoh32(pcpu->xer);
                                break;
                        case 70:
                            {
                                //if (gdb_has_xml)
                                //    return 0;
                                //GET_REG32(0); /* fpscr */
                                val = vmtoh32(0);
                                break;
                            }
                }
        }

        mem2hex((char*)&val, ctx->outbuf, 4);
}

void get_cpu_regs_mips64(gdb_debug_context_t* ctx)
{
    mips64_comm_context_t regs;
    cpu_mips_t *pcpu = CPU_MIPS64(ctx->vm->boot_cpu);
    int i;

    for (i=0; i<32; i++) {
        regs.regs[i] = vmtoh32(pcpu->gpr[i]);
    }

    /*
    for (i=0; i<MIPS64_FPU_REG_NR; i++) {
        regs.fpr[i] = vmtoh32(pcpu->fpu.reg[i]);
    }*/

    regs.sr = 0;
    regs.lo = vmtoh32(pcpu->lo);
    regs.hi = vmtoh32(pcpu->hi);
    regs.bad = 0;//vmtoh32(pcpu->bad);
    regs.cause = 0;//vmtoh32(pcpu->cause);
    regs.pc = vmtoh32(pcpu->pc);
    regs.fp = vmtoh32(pcpu->gpr[30]); // FP

    mem2hex((char*)&regs, ctx->outbuf, MIPS64_COMM_REGBYTES);
}

void get_cpu_regs_ppc32(gdb_debug_context_t* ctx)
{
    ppc_comm_context_t regs;
    cpu_ppc_t *pcpu = CPU_PPC32(ctx->vm->boot_cpu);
    int i;

    for (i=0; i<32; i++) {
        regs.regs[i] = vmtoh32(pcpu->gpr[i]);
    }

    regs.ia = 1;//vmtoh32(pcpu->ia);
    regs.sr = 2;//0;
    regs.lo = 3;//0;
    regs.lr = 4;//vmtoh32(pcpu->lr);
    regs.xer_ca = 5;//vmtoh32(pcpu->xer_ca);
    regs.xer= 6;//vmtoh32(pcpu->xer);
    regs.fp = 7;//vmtoh32(pcpu->gpr[29]);  /* stack pointer */

    for (i=0; i<PPC32_FPU_REG_NR; i++) {
        regs.fpr[i] = 0xaabb;//vmtoh32(pcpu->fpu.reg[i]);
    }
    
    //printf("sending %lu registers(%d bytes)\n", sizeof(regs)/4, (int)PPC32_COMM_REGBYTES);
    mem2hex((char*)&regs, ctx->outbuf, (int)PPC32_COMM_REGBYTES);
    //printf("aaa\n");
}

void gdb_cmd_insert_breakpoint(gdb_debug_context_t* ctx)
{
  int type;
  
  parsehexnum(&ctx->inbuf[1], &type);

  switch (type)
  {
     case GDB_BREAKPOINT_SW:
     case GDB_BREAKPOINT_HW:
     {
         int address, length;

         if (parse2hexnum(&ctx->inbuf[3], &address, &length))
         {
             ctx->vm->boot_cpu->add_breakpoint(ctx->vm->boot_cpu, address);
             strcpy(ctx->outbuf,"OK");
             
             if (gdb_debug)
                 gdb_printf("Added hw breakpoint command at 0x%llx", address);
         }
         else
         {
             strcpy(ctx->outbuf,"E11");
             
             if (gdb_debug)
                 gdb_printf("malformed insert hw breakpoint command: %s", ctx->inbuf);
         }
         break;
     }
     default:
        strcpy (ctx->outbuf, "E10");
        
        if (gdb_debug)
           gdb_printf ("malformed breakpoint/watchpoint type: %d", type);
  }

}

void gdb_cmd_remove_breakpoint(gdb_debug_context_t* ctx)
{
    int address, length;

    if (parse2hexnum(&ctx->inbuf[3], &address, &length))
    {
        ctx->vm->boot_cpu->remove_breakpoint(ctx->vm->boot_cpu, address);
        strcpy(ctx->outbuf,"OK");
        
        if (gdb_debug)
                gdb_printf("Removed hw breakpoint command at 0x%llx", address);
    }
    else
    {
        strcpy(ctx->outbuf,"E20");
        
        if (gdb_debug)
            gdb_printf("malformed remove hw breakpoint command: %s", ctx->inbuf);
    }
}
