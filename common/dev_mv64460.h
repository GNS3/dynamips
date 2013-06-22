/*
 * Cisco Router Simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEV_MV64460_H__
#define __DEV_MV64460_H__

#include <sys/types.h>
#include "utils.h"
#include "mips64.h"
#include "cpu.h"
#include "device.h"
#include "net_io.h"
#include "vm.h"

struct mv64460_data;

/* Create a new MV64460 controller */
int dev_mv64460_init(vm_instance_t *vm,char *name,
                     m_uint64_t paddr,m_uint32_t len);

/* Bind a VTTY to a SDMA channel */
int mv64460_sdma_bind_vtty(struct mv64460_data *d,u_int chan_id,vtty_t *vtty);

/* Bind a NIO to an Ethernet port */
int dev_mv64460_eth_set_nio(struct mv64460_data *d,u_int port_id,
                            netio_desc_t *nio);

/* Unbind a NIO from an Ethernet port */
int dev_mv64460_eth_unset_nio(struct mv64460_data *d,u_int port_id);

/* Set value of GPP register */
void dev_mv64460_set_gpp_reg(struct mv64460_data *d,m_uint32_t val);

/* Set a GPP interrupt */
void dev_mv64460_set_gpp_intr(struct mv64460_data *d,u_int irq);

/* Clear a GPP interrupt */
void dev_mv64460_clear_gpp_intr(struct mv64460_data *d,u_int irq);

#endif
