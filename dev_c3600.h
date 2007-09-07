/*
 * Cisco 3600 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco 3600 routines and definitions (EEPROM,...).
 */

#ifndef __DEV_C3600_H__
#define __DEV_C3600_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "device.h"
#include "pci_dev.h"
#include "nmc93cX6.h"
#include "net_io.h"
#include "vm.h"

/* Default C3600 parameters */
#define C3600_DEFAULT_CHASSIS      "3640"
#define C3600_DEFAULT_RAM_SIZE     128
#define C3600_DEFAULT_ROM_SIZE     2
#define C3600_DEFAULT_NVRAM_SIZE   128
#define C3600_DEFAULT_CONF_REG     0x2102
#define C3600_DEFAULT_CLOCK_DIV    4
#define C3600_DEFAULT_RAM_MMAP     1
#define C3600_DEFAULT_DISK0_SIZE   0
#define C3600_DEFAULT_DISK1_SIZE   0
#define C3600_DEFAULT_IOMEM_SIZE   5   /* Percents! */

/* 6 NM slots for the 3660 + integrated FastEthernet ports */
#define C3600_MAX_NM_BAYS  7

/* C3600 DUART Interrupt */
#define C3600_DUART_IRQ  5

/* C3600 Network I/O Interrupt */
#define C3600_NETIO_IRQ  2

/* C3600 GT64k DMA/Timer Interrupt */
#define C3600_GT64K_IRQ  4

/* C3600 External Interrupt */
#define C3600_EXT_IRQ    6

/* C3600 NM Management Interrupt handler */
#define C3600_NM_MGMT_IRQ  3

/* Network IRQ */
#define C3600_NETIO_IRQ_BASE       32
#define C3600_NETIO_IRQ_PORT_BITS  2
#define C3600_NETIO_IRQ_PORT_MASK  ((1 << C3600_NETIO_IRQ_PORT_BITS) - 1)
#define C3600_NETIO_IRQ_PER_SLOT   (1 << C3600_NETIO_IRQ_PORT_BITS)
#define C3600_NETIO_IRQ_END        \
    (C3600_NETIO_IRQ_BASE + (C3600_MAX_NM_BAYS * C3600_NETIO_IRQ_PER_SLOT) - 1)

/* C3600 common device addresses */
#define C3600_GT64K_ADDR      0x14000000ULL
#define C3600_IOFPGA_ADDR     0x1e800000ULL
#define C3600_DUART_ADDR      0x1e840000ULL
#define C3600_BITBUCKET_ADDR  0x1ec00000ULL
#define C3600_NVRAM_ADDR      0x1fe00000ULL
#define C3600_ROM_ADDR        0x1fc00000ULL
#define C3600_BOOTFLASH_ADDR  0x30000000ULL
#define C3600_PCI_IO_ADDR     0x100000000ULL

/* Reserved space for ROM in NVRAM */
#define C3600_NVRAM_ROM_RES_SIZE  2048

/* C3600 ELF Platform ID */
#define C3620_ELF_MACHINE_ID  0x1e
#define C3640_ELF_MACHINE_ID  0x1e
#define C3660_ELF_MACHINE_ID  0x34

#define VM_C3600(vm) ((c3600_t *)vm->hw_data)

/* C3600 router */
typedef struct c3600_router c3600_t;

/* Prototype of chassis driver initialization function */
typedef int (*c3600_chassis_init_fn)(c3600_t *router);

/* C3600 Chassis Driver */
struct c3600_chassis_driver {
   char *chassis_type;
   int chassis_id;
   int supported;  
   c3600_chassis_init_fn chassis_init;
   struct cisco_eeprom *eeprom;
};

/* C3600 router */
struct c3600_router {
   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   /* Associated VM instance */
   vm_instance_t *vm;

   /* I/O FPGA */
   struct c3600_iofpga_data *iofpga_data;

   /* Chassis information */
   struct c3600_chassis_driver *chassis_driver;
   m_uint8_t oir_status;

   /* 
    * Mainboard EEPROM.
    * It can be modified to change the chassis MAC address.
    */
   struct cisco_eeprom mb_eeprom;
   struct nmc93cX6_group mb_eeprom_group;

   /* Network Module EEPROMs (3620/3640) */
   struct nmc93cX6_group nm_eeprom_group;

   /* Cisco 3660 NM EEPROMs */
   struct nmc93cX6_group c3660_nm_eeprom_group[C3600_MAX_NM_BAYS];
};

/* Set EEPROM for the specified slot */
int c3600_set_slot_eeprom(c3600_t *router,u_int slot,
                          struct cisco_eeprom *eeprom);

/* Get network IRQ for specified slot/port */
u_int c3600_net_irq_for_slot_port(u_int slot,u_int port);

/* Show the list of available NM drivers */
void c3600_nm_show_drivers(void);

/* Set chassis MAC address */
int c3600_chassis_set_mac_addr(c3600_t *router,char *mac_addr);

/* Set the chassis type */
int c3600_chassis_set_type(c3600_t *router,char *chassis_type);

/* Get the chassis ID */
int c3600_chassis_get_id(c3600_t *router);

/* Show C3600 hardware info */
void c3600_show_hardware(c3600_t *router);

/* Initialize EEPROM groups */
void c3600_init_eeprom_groups(c3600_t *router);

/* dev_c3600_iofpga_init() */
int dev_c3600_iofpga_init(c3600_t *router,m_uint64_t paddr,m_uint32_t len);

/* Register the c3600 platform */
int c3600_platform_register(void);

/* Hypervisor C3600 initialization */
extern int hypervisor_c3600_init(vm_platform_t *platform);

/* NM drivers */
extern struct cisco_card_driver dev_c3600_nm_1e_driver;
extern struct cisco_card_driver dev_c3600_nm_4e_driver;
extern struct cisco_card_driver dev_c3600_nm_1fe_tx_driver;
extern struct cisco_card_driver dev_c3600_nm_4t_driver;
extern struct cisco_card_driver dev_c3600_leopard_2fe_driver;
extern struct cisco_card_driver dev_c3600_nm_16esw_driver;

#endif
