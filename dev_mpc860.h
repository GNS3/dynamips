/*
 * Cisco Router Simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEV_MPC860_H__
#define __DEV_MPC860_H__

#include <sys/types.h>
#include "utils.h"
#include "mips64.h"
#include "cpu.h"
#include "device.h"
#include "net_io.h"
#include "vm.h"

/* Forward declaration for MPC860 private data */
struct mpc860_data;

/* Set IRQ pending status */
void mpc860_set_pending_irq(struct mpc860_data *d,m_uint32_t val);

/* Clear a pending IRQ */
void mpc860_clear_pending_irq(struct mpc860_data *d,m_uint32_t val);

/* Create the MPC860 device */
int dev_mpc860_init(vm_instance_t *vm,char *name,
                    m_uint64_t paddr,m_uint32_t len);

#endif
