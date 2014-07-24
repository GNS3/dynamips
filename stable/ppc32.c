/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * PowerPC (32-bit) generic routines.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "rbtree.h"
#include "cpu.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "ppc32_mem.h"
#include "ppc32_exec.h"
#include "ppc32_jit.h"

#include "gdb_proto.h"

/* Reset a PowerPC CPU */
int ppc32_reset(cpu_ppc_t *cpu)
{
   cpu->ia_prev = cpu->ia = PPC32_ROM_START;
   cpu->gpr[1] = PPC32_ROM_SP;
   cpu->msr = PPC32_MSR_IP;

   /* Restart the MTS subsystem */
   ppc32_mem_restart(cpu);

   /* Flush JIT structures */
   ppc32_jit_flush(cpu,0);
   return(0);
}

/* Initialize a PowerPC processor */
int ppc32_init(cpu_ppc_t *cpu)
{
   /* Initialize JIT operations */
   jit_op_init_cpu(cpu->gen);

   /* Initialize idle timer */
   cpu->gen->idle_max = 500;
   cpu->gen->idle_sleep_time = 30000;

   /* Timer IRQ parameters (default frequency: 250 Hz <=> 4ms period) */
   cpu->timer_irq_check_itv = 1000;
   cpu->timer_irq_freq      = 250;

   /* Enable/disable direct block jump */
   cpu->exec_blk_direct_jump = cpu->vm->exec_blk_direct_jump;

   /* Idle loop mutex and condition */
   pthread_mutex_init(&cpu->gen->idle_mutex,NULL);
   pthread_cond_init(&cpu->gen->idle_cond,NULL);

   /* Set the CPU methods */
   cpu->gen->reg_set =  (void *)ppc32_reg_set;
   cpu->gen->reg_dump = (void *)ppc32_dump_regs;
   cpu->gen->mmu_dump = (void *)ppc32_dump_mmu;
   cpu->gen->mmu_raw_dump = (void *)ppc32_dump_mmu;
   cpu->gen->add_breakpoint = (void *)ppc32_add_breakpoint;
   cpu->gen->remove_breakpoint = (void *)ppc32_remove_breakpoint;
   cpu->gen->set_idle_pc = (void *)ppc32_set_idle_pc;
   cpu->gen->get_idling_pc = (void *)ppc32_get_idling_pc;

   /* zzz */
   memset(cpu->vtlb,0xFF,sizeof(cpu->vtlb));

   /* Set the startup parameters */
   ppc32_reset(cpu);
   return(0);
}

/* Delete a PowerPC processor */
void ppc32_delete(cpu_ppc_t *cpu)
{
   if (cpu) {
      ppc32_mem_shutdown(cpu);
      ppc32_jit_shutdown(cpu);
   }
}

/* Set the processor version register (PVR) */
void ppc32_set_pvr(cpu_ppc_t *cpu,m_uint32_t pvr)
{
   cpu->pvr = pvr;
   ppc32_mem_restart(cpu);
}

/* Set idle PC value */
void ppc32_set_idle_pc(cpu_gen_t *cpu,m_uint64_t addr)
{
   CPU_PPC32(cpu)->idle_pc = (m_uint32_t)addr;
}

/* Timer IRQ */
void *ppc32_timer_irq_run(cpu_ppc_t *cpu)
{
   pthread_mutex_t umutex = PTHREAD_MUTEX_INITIALIZER;
   pthread_cond_t ucond = PTHREAD_COND_INITIALIZER;
   struct timespec t_spc;
   m_tmcnt_t expire;
   u_int interval;
   u_int threshold;

#if 0
   while(!cpu->timer_irq_armed)
      sleep(1);
#endif

   interval = 1000000 / cpu->timer_irq_freq;
   threshold = cpu->timer_irq_freq * 10;
   expire = m_gettime_usec() + interval;

   while(cpu->gen->state != CPU_STATE_HALTED) {
      pthread_mutex_lock(&umutex);
      t_spc.tv_sec = expire / 1000000;
      t_spc.tv_nsec = (expire % 1000000) * 1000;
      pthread_cond_timedwait(&ucond,&umutex,&t_spc);
      pthread_mutex_unlock(&umutex);

      if (likely(!cpu->irq_disable) &&
          likely(cpu->gen->state == CPU_STATE_RUNNING) &&
          likely(cpu->msr & PPC32_MSR_EE))
      {
         cpu->timer_irq_pending++;

         if (unlikely(cpu->timer_irq_pending > threshold)) {
            cpu->timer_irq_pending = 0;
            cpu->timer_drift++;
#if 0
            printf("Timer IRQ not accurate (%u pending IRQ): "
                   "reduce the \"--timer-irq-check-itv\" parameter "
                   "(current value: %u)\n",
                   cpu->timer_irq_pending,cpu->timer_irq_check_itv);
#endif
         }
      }

      expire += interval;
   }

   return NULL;
}

#define IDLE_HASH_SIZE  8192

/* Idle PC hash item */
struct ppc32_idle_pc_hash {
   m_uint32_t ia;
   u_int count;
   struct ppc32_idle_pc_hash *next;
};

/* Determine an "idling" PC */
int ppc32_get_idling_pc(cpu_gen_t *cpu)
{
   cpu_ppc_t *pcpu = CPU_PPC32(cpu);
   struct ppc32_idle_pc_hash **pc_hash,*p;
   struct cpu_idle_pc *res;
   u_int h_index;
   m_uint32_t cur_ia;
   int i;

   cpu->idle_pc_prop_count = 0;

   if (pcpu->idle_pc != 0) {
      printf("\nYou already use an idle PC, using the calibration would give "
             "incorrect results.\n");
      return(-1);
   }

   printf("\nPlease wait while gathering statistics...\n");

   pc_hash = calloc(IDLE_HASH_SIZE,sizeof(struct ppc32_idle_pc_hash *));

   /* Disable IRQ */
   pcpu->irq_disable = TRUE;

   /* Take 1000 measures, each mesure every 10ms */
   for(i=0;i<1000;i++) {
      cur_ia = pcpu->ia;
      h_index = (cur_ia >> 2) & (IDLE_HASH_SIZE-1);

      for(p=pc_hash[h_index];p;p=p->next)
         if (p->ia == cur_ia) {
            p->count++;
            break;
         }

      if (!p) {
         if ((p = malloc(sizeof(*p)))) {
            p->ia    = cur_ia;
            p->count = 1;
            p->next  = pc_hash[h_index];
            pc_hash[h_index] = p;
         }
      }

      usleep(10000);
   }

   /* Select PCs */
   for(i=0;i<IDLE_HASH_SIZE;i++) {
      for(p=pc_hash[i];p;p=p->next)
         if ((p->count >= 20) && (p->count <= 80)) {
            res = &cpu->idle_pc_prop[cpu->idle_pc_prop_count++];

            res->pc    = p->ia;
            res->count = p->count;

            if (cpu->idle_pc_prop_count >= CPU_IDLE_PC_MAX_RES)
               goto done;
         }
   }

 done:
   /* Set idle PC */
   if (cpu->idle_pc_prop_count) {
      printf("Done. Suggested idling PC:\n");

      for(i=0;i<cpu->idle_pc_prop_count;i++) {
         printf("   0x%llx (count=%u)\n",
                cpu->idle_pc_prop[i].pc,
                cpu->idle_pc_prop[i].count);
      }         

      printf("Restart the emulator with \"--idle-pc=0x%llx\" (for example)\n",
             cpu->idle_pc_prop[0].pc);
   } else {
      printf("Done. No suggestion for idling PC, dumping the full table:\n");

      for(i=0;i<IDLE_HASH_SIZE;i++)
         for(p=pc_hash[i];p;p=p->next) {
            printf("  0x%8.8x (%3u)\n",p->ia,p->count);

            if (cpu->idle_pc_prop_count < CPU_IDLE_PC_MAX_RES) {
               res = &cpu->idle_pc_prop[cpu->idle_pc_prop_count++];

               res->pc    = p->ia;
               res->count = p->count;
            }
         }
       
      printf("\n");
   }

   /* Re-enable IRQ */
   pcpu->irq_disable = FALSE;
   return(0);
}

#if 0
/* Set an IRQ (VM IRQ standard routing) */
void ppc32_vm_set_irq(vm_instance_t *vm,u_int irq)
{
   cpu_ppc_t *boot_cpu;

   boot_cpu = CPU_PPC32(vm->boot_cpu);

   if (boot_cpu->irq_disable) {
      boot_cpu->irq_pending = 0;
      return;
   }

   ppc32_set_irq(boot_cpu,irq);

   if (boot_cpu->irq_idle_preempt[irq])
      cpu_idle_break_wait(vm->boot_cpu);
}

/* Clear an IRQ (VM IRQ standard routing) */
void ppc32_vm_clear_irq(vm_instance_t *vm,u_int irq)
{
   cpu_ppc_t *boot_cpu;

   boot_cpu = CPU_PPC32(vm->boot_cpu);
   ppc32_clear_irq(boot_cpu,irq);
}
#endif

/* Generate an exception */
void ppc32_trigger_exception(cpu_ppc_t *cpu,u_int exc_vector)
{
//    printf("TRIGGER_EXCEPTION: saving cpu->ia=0x%8.8x, msr=0x%8.8x\n",
//          cpu->ia,cpu->msr);

   /* First check if a GDB session is present so it can handle the exception */
   switch (exc_vector)
   {
        //case PPC32_EXC_SYS_RST:   /* System Reset */
        //case PPC32_EXC_MC_CHK:    /* Machine Check */
        case PPC32_EXC_DSI:       /* Data memory access failure */
        case PPC32_EXC_ISI:       /* Instruction fetch failure */
        //case PPC32_EXC_EXT:       /* External Interrupt */
        case PPC32_EXC_ALIGN:     /* Alignment */
        case PPC32_EXC_PROG:      /* FPU, Illegal instruction, ... */
        //case PPC32_EXC_NO_FPU:    /* FPU unavailable */
        //case PPC32_EXC_DEC:       /* Decrementer */
        //case PPC32_EXC_SYSCALL:   /* System Call */
        case PPC32_EXC_TRACE:     /* Trace */
        //case PPC32_EXC_FPU_HLP:   /* Floating-Point Assist */
        if (cpu->vm->gdb_server_running == TRUE)
        {
                cpu->vm->gdb_ctx->signal = GDB_SIGINT;
                vm_suspend(cpu->vm);
                return;
        }
   }

   /* Save the return instruction address */
   cpu->srr0 = cpu->ia;
   
   if (exc_vector == PPC32_EXC_SYSCALL)
      cpu->srr0 += sizeof(ppc_insn_t);

   //printf("SRR0 = 0x%8.8x\n",cpu->srr0);

   /* Save Machine State Register (MSR) */
   cpu->srr1 = cpu->msr & PPC32_EXC_SRR1_MASK;

   //printf("SRR1 = 0x%8.8x\n",cpu->srr1);

   /* Set the new SRR value */
   cpu->msr &= ~PPC32_EXC_MSR_MASK;
   cpu->irq_check = FALSE;

   //printf("MSR = 0x%8.8x\n",cpu->msr);

   /* Use bootstrap vectors ? */
   if (cpu->msr & PPC32_MSR_IP)
   {
      //printf("[-] Exception at IP 0x%8.8x\n", exc_vector);
      cpu->ia = 0xFFF00000 + exc_vector;
   }
   else
      cpu->ia = exc_vector;
}

/* Trigger IRQs */
fastcall void ppc32_trigger_irq(cpu_ppc_t *cpu)
{
   if (unlikely(cpu->irq_disable)) {
      cpu->irq_pending = FALSE;
      cpu->irq_check = FALSE;
      return;
   }

   /* Clear the IRQ check flag */
   cpu->irq_check = FALSE;

   if (cpu->irq_pending && (cpu->msr & PPC32_MSR_EE)) {
      cpu->irq_count++;
      cpu->irq_pending = FALSE;
      ppc32_trigger_exception(cpu,PPC32_EXC_EXT);
   }
}

/* Trigger the decrementer exception */
void ppc32_trigger_timer_irq(cpu_ppc_t *cpu)
{
   cpu->timer_irq_count++;

   if (cpu->msr & PPC32_MSR_EE)
      ppc32_trigger_exception(cpu,PPC32_EXC_DEC);
}

/* Virtual breakpoint */
fastcall void ppc32_run_breakpoint(cpu_ppc_t *cpu)
{
   cpu_log(cpu->gen,"BREAKPOINT",
           "Virtual breakpoint reached at IA=0x%8.8x\n",cpu->ia);

   printf("[[[ Virtual Breakpoint reached at IA=0x%8.8x LR=0x%8.8x]]]\n",
          cpu->ia,cpu->lr);

   ppc32_dump_regs(cpu->gen);
}

/* Check if current EPC has a breakpoint set */
int ppc32_is_breakpoint_at_pc(cpu_ppc_t *cpu)
{
   m_uint64_t pc = cpu->ia;
   int i;
        
   for(i=0; i < PPC32_MAX_BREAKPOINTS; i++)
   {
      if (pc == cpu->breakpoints[i]) {
         return TRUE;
      }
   }
   
   return FALSE;
}

/* Add a virtual breakpoint */
int ppc32_add_breakpoint(cpu_gen_t *cpu,m_uint64_t ia)
{
   cpu_ppc_t *pcpu = CPU_PPC32(cpu);
   int i;

   for(i=0;i<PPC32_MAX_BREAKPOINTS;i++)
      if (!pcpu->breakpoints[i])
         break;

   if (i == PPC32_MAX_BREAKPOINTS)
      return(-1);

   pcpu->breakpoints[i] = ia;
   pcpu->breakpoints_enabled = TRUE;
   return(0);
}

/* Remove a virtual breakpoint */
void ppc32_remove_breakpoint(cpu_gen_t *cpu,m_uint64_t ia)
{   
   cpu_ppc_t *pcpu = CPU_PPC32(cpu);
   int i,j;

   //printf("About to check breakpoints list to remove bp at 0x%llx\n", ia);
   for(i=0;i<PPC32_MAX_BREAKPOINTS;i++)
      if (pcpu->breakpoints[i] == (m_uint32_t)ia)
      {
         //printf ("--->bp found at index %d\n", i);
         for(j=i;j<PPC32_MAX_BREAKPOINTS-1;j++)
            pcpu->breakpoints[j] = pcpu->breakpoints[j+1];

         pcpu->breakpoints[PPC32_MAX_BREAKPOINTS-1] = 0;
      }

   for(i=0;i<PPC32_MAX_BREAKPOINTS;i++)
      if (pcpu->breakpoints[i] != 0)
         return;

   pcpu->breakpoints_enabled = FALSE;
}

/* Set a register */
void ppc32_reg_set(cpu_gen_t *cpu,u_int reg,m_uint64_t val)
{
   if (reg < PPC32_GPR_NR)
      CPU_PPC32(cpu)->gpr[reg] = (m_uint32_t)val;
}

/* Dump registers of a PowerPC processor */
void ppc32_dump_regs(cpu_gen_t *cpu)
{ 
   cpu_ppc_t *pcpu = CPU_PPC32(cpu);
   int i;
   
   printf("PowerPC Registers:\n");

   for(i=0;i<PPC32_GPR_NR/4;i++) {
      printf("  $%2d = 0x%8.8x  $%2d = 0x%8.8x"
             "  $%2d = 0x%8.8x  $%2d = 0x%8.8x\n",
             i*4, pcpu->gpr[i*4], (i*4)+1, pcpu->gpr[(i*4)+1],
             (i*4)+2, pcpu->gpr[(i*4)+2], (i*4)+3, pcpu->gpr[(i*4)+3]);
   }

   printf("\n");
   printf("  ia = 0x%8.8x, lr = 0x%8.8x\n", pcpu->ia, pcpu->lr);
   printf("  cr = 0x%8.8x, msr = 0x%8.8x, xer = 0x%8.8x, dec = 0x%8.8x\n", 
          ppc32_get_cr(pcpu), pcpu->msr, 
          pcpu->xer | (pcpu->xer_ca << PPC32_XER_CA_BIT), 
          pcpu->dec);

   printf("  sprg[0] = 0x%8.8x, sprg[1] = 0x%8.8x\n",
          pcpu->sprg[0],pcpu->sprg[1]);

   printf("  sprg[2] = 0x%8.8x, sprg[3] = 0x%8.8x\n",
          pcpu->sprg[2],pcpu->sprg[3]);

   printf("\n  IRQ count: %llu, IRQ false positives: %llu, "
          "IRQ Pending: %u, IRQ Check: %s\n",
          pcpu->irq_count,pcpu->irq_fp_count,pcpu->irq_pending,
          pcpu->irq_check ? "yes" : "no");

   printf("  Timer IRQ count: %llu, pending: %u, timer drift: %u\n\n",
          pcpu->timer_irq_count,pcpu->timer_irq_pending,pcpu->timer_drift);

   printf("  Device access count: %llu\n",cpu->dev_access_counter);

   printf("\n");
}

/* Dump BAT registers */
static void ppc32_dump_bat(cpu_ppc_t *cpu,int index)
{
   int i;

   for(i=0;i<PPC32_BAT_NR;i++)
      printf("    BAT[%d] = 0x%8.8x 0x%8.8x\n",
             i,cpu->bat[index][i].reg[0],cpu->bat[index][i].reg[1]);
}

/* Dump MMU registers */
void ppc32_dump_mmu(cpu_gen_t *cpu)
{
   cpu_ppc_t *pcpu = CPU_PPC32(cpu);
   int i;

   printf("PowerPC MMU Registers:\n");

   printf(" - IBAT Registers:\n");
   ppc32_dump_bat(pcpu,PPC32_IBAT_IDX);

   printf(" - DBAT Registers:\n");
   ppc32_dump_bat(pcpu,PPC32_DBAT_IDX);

   printf(" - Segment Registers:\n");
   for(i=0;i<PPC32_SR_NR;i++)
      printf("    SR[%d] = 0x%8.8x\n",i,pcpu->sr[i]);

   printf(" - SDR1: 0x%8.8x\n",pcpu->sdr1);
}

/* Load a raw image into the simulated memory */
int ppc32_load_raw_image(cpu_ppc_t *cpu,char *filename,m_uint32_t vaddr)
{   
   struct stat file_info;
   size_t len,clen;
   m_uint32_t remain;
   void *haddr;
   FILE *bfd;

   if (!(bfd = fopen(filename,"r"))) {
      perror("fopen");
      return(-1);
   }

   if (fstat(fileno(bfd),&file_info) == -1) {
      perror("stat");
      return(-1);
   }

   len = file_info.st_size;

   printf("Loading RAW file '%s' at virtual address 0x%8.8x (size=%lu)\n",
          filename,vaddr,(u_long)len);

   while(len > 0)
   {
      haddr = cpu->mem_op_lookup(cpu,vaddr,PPC32_MTS_DCACHE);
   
      if (!haddr) {
         fprintf(stderr,"load_raw_image: invalid load address 0x%8.8x\n",
                 vaddr);
         return(-1);
      }

      if (len > PPC32_MIN_PAGE_SIZE)
         clen = PPC32_MIN_PAGE_SIZE;
      else
         clen = len;

      remain = MIPS_MIN_PAGE_SIZE;
      remain -= (vaddr - (vaddr & MIPS_MIN_PAGE_MASK));
      
      clen = m_min(clen,remain);

      if (fread((u_char *)haddr,clen,1,bfd) != 1)
         break;
      
      vaddr += clen;
      len -= clen;
   }
   
   fclose(bfd);
   return(0);
}

/* Load an ELF image into the simulated memory */
int ppc32_load_elf_image(cpu_ppc_t *cpu,char *filename,int skip_load,
                         m_uint32_t *entry_point)
{
   m_uint32_t vaddr,remain;
   void *haddr;
   Elf32_Ehdr *ehdr;
   Elf32_Shdr *shdr;
   Elf_Scn *scn;
   Elf *img_elf;
   size_t len,clen;
   _maybe_used char *name;
   int i,fd;
   FILE *bfd;

   if (!filename)
      return(-1);

#ifdef __CYGWIN__
   fd = open(filename,O_RDONLY|O_BINARY);
#else
   fd = open(filename,O_RDONLY);
#endif

   if (fd == -1) {
      perror("load_elf_image: open");
      return(-1);
   }

   if (elf_version(EV_CURRENT) == EV_NONE) {
      fprintf(stderr,"load_elf_image: library out of date\n");
      return(-1);
   }

   if (!(img_elf = elf_begin(fd,ELF_C_READ,NULL))) {
      fprintf(stderr,"load_elf_image: elf_begin: %s\n",
              elf_errmsg(elf_errno()));
      return(-1);
   }

   if (!(ehdr = elf32_getehdr(img_elf))) {
      fprintf(stderr,"load_elf_image: invalid ELF file\n");
      return(-1);
   }

   printf("Loading ELF file '%s'...\n",filename);
   bfd = fdopen(fd,"rb");

   if (!bfd) {
      perror("load_elf_image: fdopen");
      return(-1);
   }

   if (!skip_load) {
      for(i=0;i<ehdr->e_shnum;i++) {
         scn = elf_getscn(img_elf,i);

         shdr = elf32_getshdr(scn);
         name = elf_strptr(img_elf, ehdr->e_shstrndx, (size_t)shdr->sh_name);
         len  = shdr->sh_size;

         if (!(shdr->sh_flags & SHF_ALLOC) || !len)
            continue;

         fseek(bfd,shdr->sh_offset,SEEK_SET);
         vaddr = shdr->sh_addr;

         if (cpu->vm->debug_level > 0) {
            printf("   * Adding section at virtual address 0x%8.8x "
                   "(len=0x%8.8lx)\n",vaddr,(u_long)len);
         }
         
         while(len > 0)
         {
            haddr = cpu->mem_op_lookup(cpu,vaddr,PPC32_MTS_DCACHE);

            if (!haddr) {
               fprintf(stderr,"load_elf_image: invalid load address 0x%x\n",
                       vaddr);
               return(-1);
            }

            if (len > PPC32_MIN_PAGE_SIZE)
               clen = PPC32_MIN_PAGE_SIZE;
            else
               clen = len;

            remain = PPC32_MIN_PAGE_SIZE;
            remain -= (vaddr - (vaddr & PPC32_MIN_PAGE_MASK));

            clen = m_min(clen,remain);

            if (fread((u_char *)haddr,clen,1,bfd) < 1)
               break;

            vaddr += clen;
            len -= clen;
         }
      }
   } else {
      printf("ELF loading skipped, using a ghost RAM file.\n");
   }

   printf("ELF entry point: 0x%x\n",ehdr->e_entry);

   if (entry_point)
      *entry_point = ehdr->e_entry;

   elf_end(img_elf);
   fclose(bfd);
   return(0);
}
