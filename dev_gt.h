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
                     u_int int0_irq,u_int int1_irq,
                     u_int serint0_irq,u_int serint1_irq);

/* Set NIO for a MPSC channel */
int dev_gt96100_mpsc_set_nio(struct gt_data *d,u_int chan_id,
                             netio_desc_t *nio);

/* Unset NIO for a MPSC channel */
int dev_gt96100_mpsc_unset_nio(struct gt_data *d,u_int chan_id);

/* Set a VTTY for a MPSC channel */
int dev_gt96100_mpsc_set_vtty(struct gt_data *d,u_int chan_id,vtty_t *vtty);

/* Unset a VTTY for a MPSC channel */
int dev_gt96100_mpsc_unset_vtty(struct gt_data *d,u_int chan_id);

/* Bind a NIO to GT96100 Ethernet device */
int dev_gt96100_eth_set_nio(struct gt_data *d,u_int port_id,netio_desc_t *nio);

/* Unbind a NIO from a GT96100 Ethernet device */
int dev_gt96100_eth_unset_nio(struct gt_data *d,u_int port_id);

/* Show debugging information */
int dev_gt96100_show_info(struct gt_data *d);

#endif
