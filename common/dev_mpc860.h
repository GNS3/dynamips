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

/* SPI callback for TX data */
typedef void (*mpc860_spi_tx_callback_t)(struct mpc860_data *d,
                                         u_char *buffer,u_int len,
                                         void *user_arg);

/* Set IRQ pending status */
void mpc860_set_pending_irq(struct mpc860_data *d,m_uint32_t val);

/* Clear a pending IRQ */
void mpc860_clear_pending_irq(struct mpc860_data *d,m_uint32_t val);

/* Put a buffer into SPI receive buffers */
int mpc860_spi_receive(struct mpc860_data *d,u_char *buffer,u_int len);

/* Set SPI TX callback */
void mpc860_spi_set_tx_callback(struct mpc860_data *d,
                                mpc860_spi_tx_callback_t cbk,
                                void *user_arg);

/* Set NIO for the specified SCC channel */
int mpc860_scc_set_nio(struct mpc860_data *d,u_int scc_chan,netio_desc_t *nio);

/* Unset NIO of the specified SCC channel */
int mpc860_scc_unset_nio(struct mpc860_data *d,u_int scc_chan);

/* Set NIO for the Fast Ethernet Controller */
int mpc860_fec_set_nio(struct mpc860_data *d,netio_desc_t *nio);

/* Unset NIO of the Fast Ethernet Controller */
int mpc860_fec_unset_nio(struct mpc860_data *d);

/* Create the MPC860 device */
int dev_mpc860_init(vm_instance_t *vm,char *name,
                    m_uint64_t paddr,m_uint32_t len);

#endif
