/* 
 * Cisco router simulation platform.
 * Copyright (c) 2005-2007 Christophe Fillot (cf@utc.fr)
 *
 * Cisco c2691 I/O FPGA.
 */

#ifndef __DEV_C2691_IOFPGA_H__
#define __DEV_C2691_IOFPGA_H__

/* Forward declaration for IO_FPGA private data */
struct c2691_iofpga_data;

/* Trigger a Network IRQ for the specified slot/port */
void dev_c2691_iofpga_net_set_irq(struct c2691_iofpga_data *d,
                                 u_int slot,u_int port);

/* Clear a Network IRQ for the specified slot/port */
void dev_c2691_iofpga_net_clear_irq(struct c2691_iofpga_data *d,
                                   u_int slot,u_int port);

/* Create the c2691 I/O FPGA */
int dev_c2691_iofpga_init(c2691_t *router,m_uint64_t paddr,m_uint32_t len);

#endif
