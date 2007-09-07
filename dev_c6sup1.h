/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco C6k-SUP1 routines and definitions (EEPROM,...).
 */

#ifndef __DEV_C6SUP1_H__
#define __DEV_C6SUP1_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "device.h"
#include "pci_dev.h"
#include "nmc93cX6.h"
#include "net_io.h"
#include "vm.h"

/* Default MSFC1 parameters */
#define C6SUP1_DEFAULT_RAM_SIZE     128
#define C6SUP1_DEFAULT_ROM_SIZE     4
#define C6SUP1_DEFAULT_NVRAM_SIZE   512
#define C6SUP1_DEFAULT_CONF_REG     0x2102
#define C6SUP1_DEFAULT_CLOCK_DIV    4
#define C6SUP1_DEFAULT_RAM_MMAP     1

/* EOBC + IBC */
#define C6SUP1_MAX_PA_BAYS  2

/* Maximum slots on 6513 */
#define C6SUP1_MAX_SLOTS    13

/* MSFC1 Timer IRQ (virtual) */
#define C6SUP1_VTIMER_IRQ 0

/* MSFC1 DUART Interrupt */
#define C6SUP1_DUART_IRQ  5

/* MSFC1 Network I/O Interrupt */
#define C6SUP1_NETIO_IRQ  2

/* MSFC1 PA Management Interrupt handler */
#define C6SUP1_PA_MGMT_IRQ  3

/* MSFC1 GT64k DMA/Timer Interrupt */
#define C6SUP1_GT64K_IRQ  4

/* MSFC1 Error/OIR Interrupt */
#define C6SUP1_OIR_IRQ    6

/* Network IRQ */
#define C6SUP1_NETIO_IRQ_BASE       32
#define C6SUP1_NETIO_IRQ_END        \
   (C6SUP1_NETIO_IRQ_BASE + C6SUP1_MAX_PA_BAYS - 1)

/* MSFC1 base ram limit (256 Mb) */
#define C6SUP1_BASE_RAM_LIMIT  256

/* MSFC1 common device addresses */
#define C6SUP1_GT64K_ADDR         0x14000000ULL
#define C6SUP1_GT64K_SEC_ADDR     0x15000000ULL
#define C6SUP1_BOOTFLASH_ADDR     0x1a000000ULL
#define C6SUP1_NVRAM_ADDR         0x1e000000ULL
#define C6SUP1_IOFPGA_ADDR        0x1e840000ULL
#define C6SUP1_MPFPGA_ADDR        0x1e880000ULL
#define C6SUP1_BITBUCKET_ADDR     0x1f000000ULL
#define C6SUP1_ROM_ADDR           0x1fc00000ULL
#define C6SUP1_IOMEM_ADDR         0x20000000ULL
#define C6SUP1_SRAM_ADDR          0x4b000000ULL
#define C6SUP1_BSWAP_ADDR         0xc0000000ULL
#define C6SUP1_PCI_IO_ADDR        0x100000000ULL

/* SRAM size */
#define C6SUP1_SRAM_SIZE  (4096*1024)

/* Reserved space for ROM in NVRAM */
#define C6SUP1_NVRAM_ROM_RES_SIZE  2048

/* MSFC1 physical address bus mask: keep only the lower 33 bits */
#define C6SUP1_ADDR_BUS_MASK   0x1ffffffffULL

/* MSFC1 ELF Platform ID */
#define C6SUP1_ELF_MACHINE_ID  0x19

#define VM_C6SUP1(vm) ((c6sup1_t *)vm->hw_data)

/* MSFC1 router */
typedef struct c6sup1_router c6sup1_t;

/* MSFC1 router */
struct c6sup1_router {
   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   /* Associated VM instance */
   vm_instance_t *vm;

   /* Midplane FPGA */
   struct c6sup1_mpfpga_data *mpfpga_data;

   /* Slots EEPROM data */
   struct cisco_eeprom slot_eeprom[C6SUP1_MAX_SLOTS];

   /* Backplane, Supervisor and Slot EEPROM groups */
   struct nmc93cX6_group bp_eeprom_group;
   struct nmc93cX6_group sup_eeprom_group;
   struct nmc93cX6_group slot_eeprom_group;

   /* Slot of this supervisor */
   u_int sup_slot;
};

/* Initialize EEPROM groups */
void c6sup1_init_eeprom_groups(c6sup1_t *router);

/* Get network IRQ for specified slot/port */
u_int c6sup1_net_irq_for_slot_port(u_int slot,u_int port);

/* Show the list of available PA drivers */
void c6sup1_pa_show_drivers(void);

/* Set chassis MAC address */
int c6sup1_midplane_set_mac_addr(c6sup1_t *router,char *mac_addr);

/* Show MSFC1 hardware info */
void c6sup1_show_hardware(c6sup1_t *router);

/* dev_c6sup1_iofpga_init() */
int dev_c6sup1_iofpga_init(c6sup1_t *router,m_uint64_t paddr,m_uint32_t len);

/* dev_mpfpga_init() */
int dev_c6sup1_mpfpga_init(c6sup1_t *router,m_uint64_t paddr,m_uint32_t len);

/* Register the c6sup1 platform */
int c6sup1_platform_register(void);

#endif
