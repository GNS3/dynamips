/* 
 * Cisco router simulation platform.
 * Copyright (c) 2005-2007 Christophe Fillot (cf@utc.fr)
 *
 * Cisco c7200 Midplane FPGA.
 */

#ifndef __DEV_C7200_MPFPGA_H__
#define __DEV_C7200_MPFPGA_H__

/* Create the c7200 Midplane FPGA */
int dev_c7200_mpfpga_init(c7200_t *router,m_uint64_t paddr,m_uint32_t len);

#endif
