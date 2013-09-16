/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Management of CPU groups (for MP systems).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>

#include "cpu.h"
#include "memory.h"
#include "device.h"
#include "mips64.h"
#include "mips64_cp0.h"
#include "mips64_exec.h"
#include "mips64_jit.h"
#include "ppc32.h"
#include "ppc32_exec.h"
#include "ppc32_jit.h"
#include "dynamips.h"
#include "vm.h"

/* Find a CPU in a group given its ID */
cpu_gen_t *cpu_group_find_id(cpu_group_t *group,u_int id)
{
   cpu_gen_t *cpu;

   if (!group)
      return NULL;

   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      if (cpu->id == id)
         return cpu;

   return NULL;
}

/* Find the highest CPU ID in a CPU group */
int cpu_group_find_highest_id(cpu_group_t *group,u_int *highest_id)
{
   cpu_gen_t *cpu;
   u_int max_id = 0;

   if (!group || group->cpu_list)
      return(-1);

   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      if (cpu->id >= max_id)
         max_id = cpu->id;

   *highest_id = max_id;
   return(0);
}

/* Add a CPU in a CPU group */
int cpu_group_add(cpu_group_t *group,cpu_gen_t *cpu)
{
   if (!group)
      return(-1);

   /* check that we don't already have a CPU with this id */
   if (cpu_group_find_id(group,cpu->id) != NULL) {
      fprintf(stderr,"cpu_group_add: CPU%u already present in group.\n",
              cpu->id);
      return(-1);
   }
   
   cpu->next = group->cpu_list;
   group->cpu_list = cpu;
   return(0);
}

/* Create a new CPU group */
cpu_group_t *cpu_group_create(char *name)
{
   cpu_group_t *group;

   if (!(group = malloc(sizeof(*group))))
      return NULL;

   group->name = name;
   group->cpu_list = NULL;
   return group;
}

/* Delete a CPU group */
void cpu_group_delete(cpu_group_t *group)
{  
   cpu_gen_t *cpu,*next;

   if (group != NULL) {
      for(cpu=group->cpu_list;cpu;cpu=next) {
         next = cpu->next;
         cpu_delete(cpu);
      }

      free(group);
   }
}

/* Rebuild the MTS subsystem for a CPU group */
int cpu_group_rebuild_mts(cpu_group_t *group)
{
   cpu_gen_t *cpu;

   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu->mts_rebuild(cpu);

   return(0);
}

/* Log a message for a CPU */
void cpu_log(cpu_gen_t *cpu,char *module,char *format,...)
{
   char buffer[256];
   va_list ap;

   va_start(ap,format);
   snprintf(buffer,sizeof(buffer),"CPU%u: %s",cpu->id,module);
   vm_flog(cpu->vm,buffer,format,ap);
   va_end(ap);
}

/* Create a new CPU */
cpu_gen_t *cpu_create(vm_instance_t *vm,u_int type,u_int id)
{
   void *(*cpu_run_fn)(void *);
   cpu_gen_t *cpu;

   if (!(cpu = malloc(sizeof(*cpu))))
      return NULL;

   memset(cpu,0,sizeof(*cpu));
   cpu->vm    = vm;
   cpu->id    = id;
   cpu->type  = type;
   cpu->state = CPU_STATE_SUSPENDED;

   switch(cpu->type) {
      case CPU_TYPE_MIPS64:
         cpu->jit_op_array_size = MIPS_INSN_PER_PAGE;
         CPU_MIPS64(cpu)->vm = vm;
         CPU_MIPS64(cpu)->gen = cpu;
         mips64_init(CPU_MIPS64(cpu));

         cpu_run_fn = (void *)mips64_jit_run_cpu;

         if (!cpu->vm->jit_use)
            cpu_run_fn = (void *)mips64_exec_run_cpu;
         else
            mips64_jit_init(CPU_MIPS64(cpu));
         break;

      case CPU_TYPE_PPC32:
         cpu->jit_op_array_size = PPC32_INSN_PER_PAGE;
         CPU_PPC32(cpu)->vm = vm;
         CPU_PPC32(cpu)->gen = cpu;
         ppc32_init(CPU_PPC32(cpu));

         cpu_run_fn = (void *)ppc32_jit_run_cpu;

         if (!cpu->vm->jit_use)
            cpu_run_fn = (void *)ppc32_exec_run_cpu;
         else
            ppc32_jit_init(CPU_PPC32(cpu));
         break;

      default:
         fprintf(stderr,"CPU type %u is not supported yet\n",cpu->type);
         abort();
         break;
   }

   /* create the CPU thread execution */
   if (pthread_create(&cpu->cpu_thread,NULL,cpu_run_fn,cpu) != 0) {
      fprintf(stderr,"cpu_create: unable to create thread for CPU%u\n",id);
      free(cpu);
      return NULL;
   }

   return cpu;
}

/* Delete a CPU */
void cpu_delete(cpu_gen_t *cpu)
{
   if (cpu) {
      /* Stop activity of this CPU */
      cpu_stop(cpu);
      pthread_join(cpu->cpu_thread,NULL);

      /* Free resources */
      switch(cpu->type) {
         case CPU_TYPE_MIPS64:
            mips64_delete(CPU_MIPS64(cpu));
            break;

         case CPU_TYPE_PPC32:
            ppc32_delete(CPU_PPC32(cpu));
            break;
      }

      free(cpu->jit_op_array);
      free(cpu);
   }
}

/* Start a CPU */
void cpu_start(cpu_gen_t *cpu)
{
   if (cpu) {
      cpu_log(cpu,"CPU_STATE","Starting CPU (old state=%u)...\n",cpu->state);
      cpu->state = CPU_STATE_RUNNING;
   }
}

/* Stop a CPU */
void cpu_stop(cpu_gen_t *cpu)
{
   if (cpu) {
      cpu_log(cpu,"CPU_STATE","Halting CPU (old state=%u)...\n",cpu->state);
      cpu->state = CPU_STATE_HALTED;
   }
}

/* Start all CPUs of a CPU group */
void cpu_group_start_all_cpu(cpu_group_t *group)
{
   cpu_gen_t *cpu;
   
   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu_start(cpu);
}

/* Stop all CPUs of a CPU group */
void cpu_group_stop_all_cpu(cpu_group_t *group)
{
   cpu_gen_t *cpu;
   
   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu_stop(cpu);
}

/* Set a state of all CPUs of a CPU group */
void cpu_group_set_state(cpu_group_t *group,u_int state)
{
   cpu_gen_t *cpu;
   
   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu->state = state;
}

/* Returns TRUE if all CPUs in a CPU group are inactive */
static int cpu_group_check_activity(cpu_group_t *group)
{
   cpu_gen_t *cpu;

   for(cpu=group->cpu_list;cpu;cpu=cpu->next) {
      if (!cpu->cpu_thread_running)
         continue;

      if ((cpu->state == CPU_STATE_RUNNING) || !cpu->seq_state)
         return(FALSE);
   }

   return(TRUE);
}

/* Synchronize on CPUs (all CPUs must be inactive) */
int cpu_group_sync_state(cpu_group_t *group)
{   
   cpu_gen_t *cpu;
   m_tmcnt_t t1,t2;

   /* Check that CPU activity is really suspended */
   t1 = m_gettime();

   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu->seq_state = 0;

   while(!cpu_group_check_activity(group)) {
      t2 = m_gettime();

      if (t2 > (t1 + 10000))
         return(-1);

      usleep(50000);
   }

   return(0);
}

/* Save state of all CPUs */
int cpu_group_save_state(cpu_group_t *group)
{
   cpu_gen_t *cpu;
   
   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu->prev_state = cpu->state;
   
   return(TRUE);
}

/* Restore state of all CPUs */
int cpu_group_restore_state(cpu_group_t *group)
{
   cpu_gen_t *cpu;
   
   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu->state = cpu->prev_state;

   return(TRUE);
}

/* Virtual idle loop */
void cpu_idle_loop(cpu_gen_t *cpu)
{
   struct timespec t_spc;
   m_tmcnt_t expire;

   expire = m_gettime_usec() + cpu->idle_sleep_time;

   pthread_mutex_lock(&cpu->idle_mutex);
   t_spc.tv_sec = expire / 1000000;
   t_spc.tv_nsec = (expire % 1000000) * 1000;
   pthread_cond_timedwait(&cpu->idle_cond,&cpu->idle_mutex,&t_spc);
   pthread_mutex_unlock(&cpu->idle_mutex);
}

/* Break idle wait state */
void cpu_idle_break_wait(cpu_gen_t *cpu)
{
   pthread_cond_signal(&cpu->idle_cond);
   cpu->idle_count = 0;
}
