/* 
 * Cisco router simulation platform.
 * Copyright (c) 2005-2007 Christophe Fillot (cf@utc.fr)
 *
 * Cisco MSFC1 Midplane FPGA.
 */

#ifndef __DEV_C6MSFC1_MPFPGA_H__
#define __DEV_C6MSFC1_MPFPGA_H__

/* Forward declaration for MP_FPGA private data */
struct c6msfc1_mpfpga_data;

/* Trigger a Network IRQ for the specified slot/port */
void dev_c6msfc1_mpfpga_net_set_irq(struct c6msfc1_mpfpga_data *d,
                                    u_int slot,u_int port);

/* Clear a Network IRQ for the specified slot/port */
void dev_c6msfc1_mpfpga_net_clear_irq(struct c6msfc1_mpfpga_data *d,
                                      u_int slot,u_int port);

/* Create the MSFC1 Midplane FPGA */
int dev_c6msfc1_mpfpga_init(c6msfc1_t *router,m_uint64_t paddr,m_uint32_t len);

#endif
