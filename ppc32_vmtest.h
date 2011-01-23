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

/* Register the ppc32_vmtest platform */
int ppc32_vmtest_platform_register(void);

#endif
