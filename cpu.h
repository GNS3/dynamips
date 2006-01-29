/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __CPU_H__
#define __CPU_H__

#include "mips64.h"

/* CPU group definition */
typedef struct cpu_group cpu_group_t;
struct cpu_group {
   char *name;
   cpu_mips_t *cpu_list;
   void *priv_data;
};

/* System CPU group */
extern cpu_group_t *sys_cpu_group;

/* Find a CPU in a group given its ID */
cpu_mips_t *cpu_group_find_id(cpu_group_t *group,u_int id);

/* Find the highest CPU ID in a CPU group */
int cpu_group_find_highest_id(cpu_group_t *group,u_int *highest_id);

/* Add a CPU in a CPU group */
int cpu_group_add(cpu_group_t *group,cpu_mips_t *cpu);

/* Create a new CPU group */
cpu_group_t *cpu_group_create(char *name);

/* Bind a device to a specific CPU */
int cpu_bind_device(cpu_mips_t *cpu,struct vdevice *dev,u_int *dev_id);

/* Add a memory mapped device to all CPUs of a CPU group */
int cpu_group_bind_device(cpu_group_t *group,struct vdevice *dev);

/* Create a new CPU */
cpu_mips_t *cpu_create(u_int id);

/* Start a CPU */
void cpu_start(cpu_mips_t *cpu);

/* Stop a CPU */
void cpu_stop(cpu_mips_t *cpu);

/* Start all CPUs of a CPU group */
void cpu_group_start_all_cpu(cpu_group_t *group);

/* Stop all CPUs of a CPU group */
void cpu_group_stop_all_cpu(cpu_group_t *group);

/* Set a state of all CPUs of a CPU group */
void cpu_group_set_state(cpu_group_t *group,u_int state);

/* Returns TRUE if all CPUs in a CPU group are in the specified state */
int cpu_group_check_state(cpu_group_t *group,u_int state);

#endif
