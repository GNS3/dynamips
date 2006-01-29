/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * All CPU in a CPU group share a group of memory mapped devices 
 * in an uniform way.
 *
 * Each CPU has its own device array, so it is possible to map a device
 * to specific processor(s) in a group.
 *
 * TODO: - IRQ routing.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
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
#include "mips64_exec.h"

/* System CPU group */
cpu_group_t *sys_cpu_group = NULL;

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

/* Bind a device to a specific CPU */
int cpu_bind_device(cpu_mips_t *cpu,struct vdevice *dev,u_int *dev_id)
{
   u_int i;

   for(i=0;i<MIPS64_DEVICE_MAX;i++)
      if (!cpu->dev_array[i])
         break;

   if (i == MIPS64_DEVICE_MAX) {
      fprintf(stderr,"CPU%u: cpu_bind_device: device table full.\n",cpu->id);
      return(-1);
   }

   if (dev_id) *dev_id = i;
   cpu->dev_array[i] = dev;
   return(0);
}

/* Add a memory mapped device to all CPUs of a CPU group */
int cpu_group_bind_device(cpu_group_t *group,struct vdevice *dev)
{
   cpu_mips_t *cpu;

   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      if (cpu_bind_device(cpu,dev,NULL) == -1) {
         fprintf(stderr,"CPU Group %s: unable to bind device %s to CPU%u.\n",
                 group->name,dev->name,cpu->id);
         return(-1);
      }

   return(0);
}

/* Create a new CPU */
cpu_mips_t *cpu_create(u_int id)
{
   void *(*cpu_run_fn)(void *);
   cpu_mips_t *cpu;

   if (!(cpu = malloc(sizeof(*cpu))))
      return NULL;

   /* by default, use a standard initialization (CPU is halted) */
   mips64_init(cpu);
   cpu->id = id;
   cpu->state = MIPS_CPU_HALTED;

   cpu_run_fn = (void *)insn_block_execute;
#if __GNUC__ > 2
   if (!jit_use)
      cpu_run_fn = (void *)mips64_exec_run_cpu;
#endif

   /* create the CPU thread execution */
   if (pthread_create(&cpu->cpu_thread,NULL,cpu_run_fn,cpu) != 0) {
      fprintf(stderr,"cpu_create: unable to create thread for CPU%u\n",id);
      free(cpu);
      return NULL;
   }

   return cpu;
}

/* Start a CPU */
void cpu_start(cpu_mips_t *cpu)
{
   if (cpu) {
      m_log("CPU","Starting CPU%u (old state=%u)...\n",cpu->id,cpu->state);
      cpu->state = MIPS_CPU_RUNNING;
   }
}

/* Stop a CPU */
void cpu_stop(cpu_mips_t *cpu)
{
   if (cpu) {
      m_log("CPU","Halting CPU%u (old state=%u)...\n",cpu->id,cpu->state);
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

/* Returns TRUE if all CPUs in a CPU group are in the specified state */
int cpu_group_check_state(cpu_group_t *group,u_int state)
{
   cpu_mips_t *cpu;
   
   for(cpu=group->cpu_list;cpu;cpu=cpu->next)
      if (cpu->state != state)
         return(FALSE);

   return(TRUE);
}
