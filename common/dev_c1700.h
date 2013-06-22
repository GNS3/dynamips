/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco 1700 routines and definitions (EEPROM,...).
 */

#ifndef __DEV_C1700_H__
#define __DEV_C1700_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "device.h"
#include "pci_dev.h"
#include "nmc93cX6.h"
#include "net_io.h"
#include "dev_mpc860.h"
#include "vm.h"

/* Default C1700 parameters */
#define C1700_DEFAULT_MAINBOARD    "1720"
#define C1700_DEFAULT_RAM_SIZE     64
#define C1700_DEFAULT_ROM_SIZE     2
#define C1700_DEFAULT_NVRAM_SIZE   32
#define C1700_DEFAULT_CONF_REG     0x2102
#define C1700_DEFAULT_CLOCK_DIV    8
#define C1700_DEFAULT_RAM_MMAP     1
#define C1700_DEFAULT_DISK0_SIZE   0
#define C1700_DEFAULT_DISK1_SIZE   0
#define C1700_DEFAULT_IOMEM_SIZE   15  /* Percents! */

/* 1700 characteristics: only mainboard (considered as fake NM) */
#define C1700_MAX_NM_BAYS   1
#define C1700_MAX_WIC_BAYS  2

/* C1700 Virtual Timer Interrupt */
#define C1700_VTIMER_IRQ  0

/* C1700 DUART Interrupt */
#define C1700_DUART_IRQ   1

/* C1700 Network I/O Interrupt */
#define C1700_NETIO_IRQ   2

/* C1700 PA Management Interrupt */
#define C1700_PA_MGMT_IRQ   3

/* Network IRQ */
#define C1700_NETIO_IRQ_BASE       32
#define C1700_NETIO_IRQ_PORT_BITS  2
#define C1700_NETIO_IRQ_PORT_MASK  ((1 << C1700_NETIO_IRQ_PORT_BITS) - 1)
#define C1700_NETIO_IRQ_PER_SLOT   (1 << C1700_NETIO_IRQ_PORT_BITS)
#define C1700_NETIO_IRQ_END        \
  (C1700_NETIO_IRQ_BASE + (C1700_MAX_NM_BAYS * C1700_NETIO_IRQ_PER_SLOT) - 1)

/* C1700 common device addresses */
#define C1700_FLASH_ADDR      0x60000000ULL
#define C1700_NVRAM_ADDR      0x68000000ULL
#define C1700_IOFPGA_ADDR     0x68020000ULL
#define C1700_WIC_ADDR        0x68030000ULL
#define C1700_DUART_ADDR      0x68050000ULL
#define C1700_MPC860_ADDR     0xff000000ULL
#define C1700_ROM_ADDR        0xfff00000ULL

/* WIC interval in address space */
#define C1700_WIC_SIZE  0x1000

/* Reserved space for ROM in NVRAM */
#define C1700_NVRAM_ROM_RES_SIZE  2048

/* C1700 ELF Platform ID */
#define C1700_ELF_MACHINE_ID  0x33

#define VM_C1700(vm) ((c1700_t *)vm->hw_data)

/* C1700 router */
typedef struct c1700_router c1700_t;

/* C1700 router */
struct c1700_router {
   /* Mainboard type (2610, 2611, etc) */
   char *mainboard_type;

   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   char board_id[20];

   /* Associated VM instance */
   vm_instance_t *vm;

   /* I/O FPGA */
   struct c1700_iofpga_data *iofpga_data;

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
int c1700_get_onboard_wic_addr(u_int slot,m_uint64_t *phys_addr);

/* Set EEPROM for the specified slot */
int c1700_set_slot_eeprom(c1700_t *router,u_int slot,
                          struct cisco_eeprom *eeprom);

/* Get network IRQ for specified slot/port */
u_int c1700_net_irq_for_slot_port(u_int slot,u_int port);

/* Set mainboard type */
int c1700_mainboard_set_type(c1700_t *router,char *mainboard_type);

/* Set chassis MAC address */
int c1700_chassis_set_mac_addr(c1700_t *router,char *mac_addr);

/* Set the system id */
int c1700_set_system_id(c1700_t *router,char *id);

/* Burn the system id into the appropriate eeprom if possible */
int c1700_refresh_systemid(c1700_t *router);

/* Show C1700 hardware info */
void c1700_show_hardware(c1700_t *router);

/* Initialize EEPROM groups */
void c1700_init_eeprom_groups(c1700_t *router);

/* dev_c1700_iofpga_init() */
int dev_c1700_iofpga_init(c1700_t *router,m_uint64_t paddr,m_uint32_t len);

/* Register the c1700 platform */
int c1700_platform_register(void);

/* Hypervisor C1700 initialization */
extern int hypervisor_c1700_init(vm_platform_t *platform);

/* c1700 Motherboard drivers */
extern struct cisco_card_driver dev_c1700_mb_eth_driver;
extern struct cisco_card_driver dev_c1710_mb_eth_driver;

/* WIC drivers */
extern struct cisco_card_driver *dev_c1700_mb_wic_drivers[];

#endif
