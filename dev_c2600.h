/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco 2600 routines and definitions (EEPROM,...).
 */

#ifndef __DEV_C2600_H__
#define __DEV_C2600_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "device.h"
#include "pci_dev.h"
#include "nmc93cX6.h"
#include "net_io.h"
#include "dev_mpc860.h"
#include "vm.h"

/* Default C2600 parameters */
#define C2600_DEFAULT_MAINBOARD    "2610"
#define C2600_DEFAULT_RAM_SIZE     64
#define C2600_DEFAULT_ROM_SIZE     2
#define C2600_DEFAULT_NVRAM_SIZE   128
#define C2600_DEFAULT_CONF_REG     0x2102
#define C2600_DEFAULT_CLOCK_DIV    8
#define C2600_DEFAULT_RAM_MMAP     1
#define C2600_DEFAULT_DISK0_SIZE   0
#define C2600_DEFAULT_DISK1_SIZE   0
#define C2600_DEFAULT_IOMEM_SIZE   15  /* Percents! */

/* 2600 characteristics: 1 NM + mainboard, 2 onboard WIC slots */
#define C2600_MAX_NM_BAYS   2
#define C2600_MAX_WIC_BAYS  2

/* C2600 Virtual Timer Interrupt */
#define C2600_VTIMER_IRQ  0

/* C2600 DUART Interrupt */
#define C2600_DUART_IRQ   1

/* C2600 Network I/O Interrupt */
#define C2600_NETIO_IRQ   2

/* C2600 PA Management Interrupt */
#define C2600_PA_MGMT_IRQ   3

/* Network IRQ */
#define C2600_NETIO_IRQ_BASE       32
#define C2600_NETIO_IRQ_PORT_BITS  2
#define C2600_NETIO_IRQ_PORT_MASK  ((1 << C2600_NETIO_IRQ_PORT_BITS) - 1)
#define C2600_NETIO_IRQ_PER_SLOT   (1 << C2600_NETIO_IRQ_PORT_BITS)
#define C2600_NETIO_IRQ_END        \
  (C2600_NETIO_IRQ_BASE + (C2600_MAX_NM_BAYS * C2600_NETIO_IRQ_PER_SLOT) - 1)

/* C2600 common device addresses */
#define C2600_FLASH_ADDR      0x60000000ULL
#define C2600_WIC_ADDR        0x67000000ULL
#define C2600_IOFPGA_ADDR     0x67400000ULL
#define C2600_NVRAM_ADDR      0x67c00000ULL
#define C2600_PCICTRL_ADDR    0x68000000ULL
#define C2600_MPC860_ADDR     0x68010000ULL
#define C2600_DUART_ADDR      0xffe00000ULL
#define C2600_ROM_ADDR        0xfff00000ULL

/* WIC interval in address space */
#define C2600_WIC_SIZE  0x400

/* Reserved space for ROM in NVRAM */
#define C2600_NVRAM_ROM_RES_SIZE  2048

/* C2600 ELF Platform ID */
#define C2600_ELF_MACHINE_ID  0x2b

#define VM_C2600(vm) ((c2600_t *)vm->hw_data)

/* C2600 router */
typedef struct c2600_router c2600_t;

/* C2600 router */
struct c2600_router {
   /* Mainboard type (2610, 2611, etc) */
   char *mainboard_type;

   /* Is the router a XM model ? */
   int xm_model;

   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   /* Associated VM instance */
   vm_instance_t *vm;

   /* I/O FPGA */
   struct c2600_iofpga_data *iofpga_data;

   /* 
    * Mainboard EEPROM.
    * It can be modified to change the chassis MAC address.
    */
   struct cisco_eeprom mb_eeprom;
   struct nmc93cX6_group mb_eeprom_group;

   /* Network Module EEPROM */
   struct nmc93cX6_group nm_eeprom_group;

   /* MPC860 device private data */
   struct mpc860_data *mpc_data;
};

/* Get WIC device address for the specified onboard port */
int c2600_get_onboard_wic_addr(u_int slot,m_uint64_t *phys_addr);

/* Set EEPROM for the specified slot */
int c2600_set_slot_eeprom(c2600_t *router,u_int slot,
                          struct cisco_eeprom *eeprom);

/* Get network IRQ for specified slot/port */
u_int c2600_net_irq_for_slot_port(u_int slot,u_int port);

/* Set mainboard type */
int c2600_mainboard_set_type(c2600_t *router,char *mainboard_type);

/* Set chassis MAC address */
int c2600_chassis_set_mac_addr(c2600_t *router,char *mac_addr);

/* Show C2600 hardware info */
void c2600_show_hardware(c2600_t *router);

/* Initialize EEPROM groups */
void c2600_init_eeprom_groups(c2600_t *router);

/* Create the c2600 PCI controller device */
int dev_c2600_pci_init(vm_instance_t *vm,char *name,
                       m_uint64_t paddr,m_uint32_t len,
                       struct pci_bus *bus);

/* dev_c2600_iofpga_init() */
int dev_c2600_iofpga_init(c2600_t *router,m_uint64_t paddr,m_uint32_t len);

/* Register the c2600 platform */
int c2600_platform_register(void);

/* Hypervisor C2600 initialization */
extern int hypervisor_c2600_init(vm_platform_t *platform);

/* NM drivers */
extern struct cisco_card_driver dev_c2600_mb1e_eth_driver;
extern struct cisco_card_driver dev_c2600_mb2e_eth_driver;
extern struct cisco_card_driver dev_c2600_mb1fe_eth_driver;
extern struct cisco_card_driver dev_c2600_mb2fe_eth_driver;

extern struct cisco_card_driver dev_c2600_nm_1e_driver;
extern struct cisco_card_driver dev_c2600_nm_4e_driver;
extern struct cisco_card_driver dev_c2600_nm_1fe_tx_driver;
extern struct cisco_card_driver dev_c2600_nm_16esw_driver;

extern struct cisco_card_driver dev_c2600_nm_nam_driver;
extern struct cisco_card_driver dev_c2600_nm_cids_driver;

/* WIC drivers */
extern struct cisco_card_driver *dev_c2600_mb_wic_drivers[];

#endif
