/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco 3725 routines and definitions (EEPROM,...).
 */

#ifndef __DEV_C3725_H__
#define __DEV_C3725_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "device.h"
#include "pci_dev.h"
#include "nmc93cX6.h"
#include "net_io.h"
#include "vm.h"

/* Default C3725 parameters */
#define C3725_DEFAULT_RAM_SIZE     128
#define C3725_DEFAULT_ROM_SIZE     2
#define C3725_DEFAULT_NVRAM_SIZE   128
#define C3725_DEFAULT_CONF_REG     0x2102
#define C3725_DEFAULT_CLOCK_DIV    8
#define C3725_DEFAULT_RAM_MMAP     1
#define C3725_DEFAULT_DISK0_SIZE   16
#define C3725_DEFAULT_DISK1_SIZE   0
#define C3725_DEFAULT_IOMEM_SIZE   5   /* Percents! */

/* 3725 characteritics: 2 NM, 3 WIC, 2 AIM */
#define C3725_MAX_NM_BAYS   3
#define C3725_MAX_WIC_BAYS  3

/* C3725 DUART Interrupt */
#define C3725_DUART_IRQ  5

/* C3725 Network I/O Interrupt */
#define C3725_NETIO_IRQ  2

/* C3725 GT64k DMA/Timer Interrupt */
#define C3725_GT96K_IRQ  3

/* C3725 External Interrupt */
#define C3725_EXT_IRQ    6

/* Network IRQ */
#define C3725_NETIO_IRQ_BASE       32
#define C3725_NETIO_IRQ_PORT_BITS  2
#define C3725_NETIO_IRQ_PORT_MASK  ((1 << C3725_NETIO_IRQ_PORT_BITS) - 1)
#define C3725_NETIO_IRQ_PER_SLOT   (1 << C3725_NETIO_IRQ_PORT_BITS)
#define C3725_NETIO_IRQ_END        \
  (C3725_NETIO_IRQ_BASE + (C3725_MAX_NM_BAYS * C3725_NETIO_IRQ_PER_SLOT) - 1)

/* C3725 common device addresses */
#define C3725_GT96K_ADDR      0x14000000ULL
#define C3725_IOFPGA_ADDR     0x1e800000ULL
#define C3725_BITBUCKET_ADDR  0x1ec00000ULL
#define C3725_ROM_ADDR        0x1fc00000ULL
#define C3725_SLOT0_ADDR      0x30000000ULL
#define C3725_SLOT1_ADDR      0x32000000ULL
#define C3725_DUART_ADDR      0x3c100000ULL
#define C3725_WIC_ADDR        0x3c200000ULL
#define C3725_BSWAP_ADDR      0xc0000000ULL
#define C3725_PCI_IO_ADDR     0x100000000ULL

/* WIC interval in address space */
#define C3725_WIC_SIZE  0x2000

/* Offset of simulated NVRAM in ROM flash */
#define C3725_NVRAM_OFFSET    0xE0000
#define C3725_NVRAM_SIZE      0xE000

/* Reserved space for ROM in NVRAM */
#define C3725_NVRAM_ROM_RES_SIZE  2048

/* C3725 ELF Platform ID */
#define C3725_ELF_MACHINE_ID  0x61

#define VM_C3725(vm) ((c3725_t *)vm->hw_data)

/* C3725 router */
typedef struct c3725_router c3725_t;

/* C3725 router */
struct c3725_router {
   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   /* Associated VM instance */
   vm_instance_t *vm;

   /* GT96100 data */
   struct gt_data *gt_data;

   /* I/O FPGA */
   struct c3725_iofpga_data *iofpga_data;

   /* Chassis information */
   m_uint8_t oir_status;

   /* 
    * Mainboard EEPROM.
    * It can be modified to change the chassis MAC address.
    */
   struct cisco_eeprom mb_eeprom;
   struct nmc93cX6_group mb_eeprom_group;

   /* Network Module EEPROMs */
   struct nmc93cX6_group nm_eeprom_group[2];
};

/* Get WIC device address for the specified onboard port */
int c3725_get_onboard_wic_addr(u_int slot,m_uint64_t *phys_addr);

/* Set EEPROM for the specified slot */
int c3725_set_slot_eeprom(c3725_t *router,u_int slot,
                          struct cisco_eeprom *eeprom);

/* Get network IRQ for specified slot/port */
u_int c3725_net_irq_for_slot_port(u_int slot,u_int port);

/* Get PCI device for the specified NM bay */
int c3725_nm_get_pci_device(u_int nm_bay);

/* Set chassis MAC address */
int c3725_chassis_set_mac_addr(c3725_t *router,char *mac_addr);

/* Show C3725 hardware info */
void c3725_show_hardware(c3725_t *router);

/* Initialize EEPROM groups */
void c3725_init_eeprom_groups(c3725_t *router);

/* Register the c3725 platform */
int c3725_platform_register(void);

/* Hypervisor C3725 initialization */
extern int hypervisor_c3725_init(vm_platform_t *platform);

/* NM drivers */
extern struct cisco_card_driver dev_c3725_nm_1fe_tx_driver;
extern struct cisco_card_driver dev_c3725_gt96100_fe_driver;
extern struct cisco_card_driver dev_c3725_nm_4t_driver;
extern struct cisco_card_driver dev_c3725_nm_16esw_driver;
extern struct cisco_card_driver dev_c3725_nm_nam_driver;
extern struct cisco_card_driver dev_c3725_nm_cids_driver;

/* WIC drivers */
extern struct cisco_card_driver *dev_c3725_mb_wic_drivers[];

#endif
