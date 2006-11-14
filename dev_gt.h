/*
 * Cisco Router Simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEV_GT_H__
#define __DEV_GT_H__

#include <sys/types.h>
#include "utils.h"
#include "mips64.h"
#include "cpu.h"
#include "device.h"
#include "net_io.h"
#include "vm.h"

/* Forward declaration for Galilo controller private data */
struct gt_data;

/* Create a new GT64010 controller */
int dev_gt64010_init(vm_instance_t *vm,char *name,
                     m_uint64_t paddr,m_uint32_t len,u_int irq);

/* Create a new GT64120 controller */
int dev_gt64120_init(vm_instance_t *vm,char *name,
                     m_uint64_t paddr,m_uint32_t len,u_int irq);

/* Create a new GT96100 controller */
int dev_gt96100_init(vm_instance_t *vm,char *name,
                     m_uint64_t paddr,m_uint32_t len,
                     u_int dma_irq,u_int eth_irq);

/* Bind a NIO to GT96100 device */
int dev_gt96100_set_nio(struct gt_data *d,u_int port_id,netio_desc_t *nio);

/* Unbind a NIO from a GT96100 device */
int dev_gt96100_unset_nio(struct gt_data *d,u_int port_id);

#endif
