/*
 * Cisco 7200 (Predator) simulation platform.
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
#include <sys/mman.h>
#include <fcntl.h>
#include <pthread.h>

#include "mips64.h"
#include "dynamips.h"
#include "cpu.h"
#include "memory.h"
#include "device.h"
#include "cp0.h"
#include "mips64_exec.h"
#include "vm.h"

/* Find a CPU in a group given its ID */
cpu_mips_t *cpu_group_find_id(cpu_group_t *group,u_int id)
{
   cpu_mips_t *cpu;

   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      if (cpu->id == id)
         return cpu;

   return NULL;
}

/* Find the highest CPU ID in a CPU group */
int cpu_group_find_highest_id(cpu_group_t *group,u_int *highest_id)
{
   cpu_mips_t *cpu;
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
int cpu_group_add(cpu_group_t *group,cpu_mips_t *cpu)
{
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
   cpu_mips_t *cpu,*next;

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
   cpu_mips_t *cpu;

   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu->mts_rebuild(cpu);

   return(0);
}

/* Log a message for a CPU */
void cpu_log(cpu_mips_t *cpu,char *module,char *format,...)
{
   char buffer[256];
   va_list ap;

   va_start(ap,format);
   snprintf(buffer,sizeof(buffer),"CPU%u: %s",cpu->id,module);
   vm_flog(cpu->vm,buffer,format,ap);
   va_end(ap);
}

/* Create a new CPU */
cpu_mips_t *cpu_create(vm_instance_t *vm,u_int id)
{
   void *(*cpu_run_fn)(void *);
   cpu_mips_t *cpu;

   if (!(cpu = malloc(sizeof(*cpu))))
      return NULL;

   memset(cpu,0,sizeof(*cpu));
   cpu->vm = vm;

   /* by default, use a standard initialization (CPU exec is suspended) */
   mips64_init(cpu);
   cpu->id = id;
   cpu->state = MIPS_CPU_SUSPENDED;

   cpu_run_fn = (void *)insn_block_execute;
#if __GNUC__ > 2
   if (!cpu->vm->jit_use) {
      cpu_run_fn = (void *)mips64_exec_run_cpu;
   } else {
      mips64_jit_init(cpu);
   }
#endif

   /* create the CPU thread execution */
   if (pthread_create(&cpu->cpu_thread,NULL,cpu_run_fn,cpu) != 0) {
      fprintf(stderr,"cpu_create: unable to create thread for CPU%u\n",id);
      free(cpu);
      return NULL;
   }

   return cpu;
}

/* Delete a CPU */
void cpu_delete(cpu_mips_t *cpu)
{
   if (cpu) {
      /* Stop activity of this CPU */
      cpu_stop(cpu);
      pthread_join(cpu->cpu_thread,NULL);

      /* Free resources */
      mips64_delete(cpu);
   }
}

/* Start a CPU */
void cpu_start(cpu_mips_t *cpu)
{
   if (cpu) {
      cpu_log(cpu,"CPU_STATE","Starting CPU (old state=%u)...\n",cpu->state);
      cpu->state = MIPS_CPU_RUNNING;
   }
}

/* Stop a CPU */
void cpu_stop(cpu_mips_t *cpu)
{
   if (cpu) {
      cpu_log(cpu,"CPU_STATE","Halting CPU (old state=%u)...\n",cpu->state);
      cpu->state = MIPS_CPU_HALTED;
   }
}

/* Start all CPUs of a CPU group */
void cpu_group_start_all_cpu(cpu_group_t *group)
{
   cpu_mips_t *cpu;
   
   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu_start(cpu);
}

/* Stop all CPUs of a CPU group */
void cpu_group_stop_all_cpu(cpu_group_t *group)
{
   cpu_mips_t *cpu;
   
   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu_stop(cpu);
}

/* Set a state of all CPUs of a CPU group */
void cpu_group_set_state(cpu_group_t *group,u_int state)
{
   cpu_mips_t *cpu;
   
   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu->state = state;
}

/* Returns TRUE if all CPUs in a CPU group are inactive */
static int cpu_group_check_activity(cpu_group_t *group)
{
   cpu_mips_t *cpu;

   for(cpu=group->cpu_list;cpu;cpu=cpu->next) {
      if (!cpu->cpu_thread_running)
         continue;

      if ((cpu->state == MIPS_CPU_RUNNING) || !cpu->seq_state)
         return(FALSE);
   }

   return(TRUE);
}

/* Synchronize on CPUs (all CPUs must be inactive) */
int cpu_group_sync_state(cpu_group_t *group)
{   
   cpu_mips_t *cpu;
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
   cpu_mips_t *cpu;
   
   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu->prev_state = cpu->state;
   
   return(TRUE);
}

/* Restore state of all CPUs */
int cpu_group_restore_state(cpu_group_t *group)
{
   cpu_mips_t *cpu;
   
   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      cpu->state = cpu->prev_state;

   return(TRUE);
}
