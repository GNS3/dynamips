/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * PowerPC VM experimentations.
 */

#ifndef __PPC32_VMTEST_H__
#define __PPC32_VMTEST_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "device.h"
#include "pci_dev.h"
#include "vm.h"

/* Default parameters of the test VM */
#define PPC32_VMTEST_DEFAULT_RAM_SIZE  256

/* Create a new test instance */
vm_instance_t *ppc32_vmtest_create_instance(char *name,int instance_id);

/* Delete a router instance */
int ppc32_vmtest_delete_instance(char *name);

/* Delete all router instances */
int ppc32_vmtest_delete_all_instances(void);

/* Initialize a test instance */
int ppc32_vmtest_init_instance(vm_instance_t *vm);

/* Stop a test instance */
int ppc32_vmtest_stop_instance(vm_instance_t *vm);

#endif
