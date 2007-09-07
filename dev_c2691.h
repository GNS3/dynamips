/*
 * Cisco 2691 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco 2691 routines and definitions (EEPROM,...).
 */

#ifndef __DEV_C2691_H__
#define __DEV_C2691_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "device.h"
#include "pci_dev.h"
#include "nmc93cX6.h"
#include "net_io.h"
#include "vm.h"

/* Default C2691 parameters */
#define C2691_DEFAULT_RAM_SIZE     128
#define C2691_DEFAULT_ROM_SIZE     2
#define C2691_DEFAULT_NVRAM_SIZE   128
#define C2691_DEFAULT_CONF_REG     0x2102
#define C2691_DEFAULT_CLOCK_DIV    8
#define C2691_DEFAULT_RAM_MMAP     1
#define C2691_DEFAULT_DISK0_SIZE   16
#define C2691_DEFAULT_DISK1_SIZE   0
#define C2691_DEFAULT_IOMEM_SIZE   5   /* Percents! */

/* 2691 characteritics: 1 NM, 3 WIC, 2 AIM */
#define C2691_MAX_NM_BAYS   2
#define C2691_MAX_WIC_BAYS  3

/* C2691 DUART Interrupt */
#define C2691_DUART_IRQ  5

/* C2691 Network I/O Interrupt */
#define C2691_NETIO_IRQ  2

/* C2691 GT64k DMA/Timer Interrupt */
#define C2691_GT96K_IRQ  3

/* C2691 External Interrupt */
#define C2691_EXT_IRQ    6

/* Network IRQ */
#define C2691_NETIO_IRQ_BASE       32
#define C2691_NETIO_IRQ_PORT_BITS  3
#define C2691_NETIO_IRQ_PORT_MASK  ((1 << C2691_NETIO_IRQ_PORT_BITS) - 1)
#define C2691_NETIO_IRQ_PER_SLOT   (1 << C2691_NETIO_IRQ_PORT_BITS)
#define C2691_NETIO_IRQ_END        \
  (C2691_NETIO_IRQ_BASE + (C2691_MAX_NM_BAYS * C2691_NETIO_IRQ_PER_SLOT) - 1)

/* C2691 common device addresses */
#define C2691_GT96K_ADDR      0x14000000ULL
#define C2691_IOFPGA_ADDR     0x1e800000ULL
#define C2691_BITBUCKET_ADDR  0x1ec00000ULL
#define C2691_ROM_ADDR        0x1fc00000ULL
#define C2691_SLOT0_ADDR      0x30000000ULL
#define C2691_SLOT1_ADDR      0x32000000ULL
#define C2691_DUART_ADDR      0x3c100000ULL
#define C2691_WIC_ADDR        0x3c200000ULL
#define C2691_BSWAP_ADDR      0xc0000000ULL
#define C2691_PCI_IO_ADDR     0x100000000ULL

/* WIC interval in address space */
#define C2691_WIC_SIZE  0x2000

/* Offset of simulated NVRAM in ROM flash */
#define C2691_NVRAM_OFFSET    0xE0000
#define C2691_NVRAM_SIZE      0xE000

/* Reserved space for ROM in NVRAM */
#define C2691_NVRAM_ROM_RES_SIZE  2048

/* C2691 ELF Platform ID */
#define C2691_ELF_MACHINE_ID  0x66

#define VM_C2691(vm) ((c2691_t *)vm->hw_data)

/* C2691 router */
typedef struct c2691_router c2691_t;

/* C2691 router */
struct c2691_router {
   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   /* Associated VM instance */
   vm_instance_t *vm;

   /* GT96100 data */
   struct gt_data *gt_data;

   /* I/O FPGA */
   struct c2691_iofpga_data *iofpga_data;

   /* Chassis information */
   m_uint8_t oir_status;

   /* 
    * Mainboard EEPROM.
    * It can be modified to change the chassis MAC address.
    */
   struct cisco_eeprom mb_eeprom;
   struct nmc93cX6_group mb_eeprom_group;

   /* Network Module EEPROM */
   struct nmc93cX6_group nm_eeprom_group;
};

/* Get WIC device address for the specified onboard port */
int c2691_get_onboard_wic_addr(u_int slot,m_uint64_t *phys_addr);

/* Set EEPROM for the specified slot */
int c2691_set_slot_eeprom(c2691_t *router,u_int slot,
                          struct cisco_eeprom *eeprom);

/* Get network IRQ for specified slot/port */
u_int c2691_net_irq_for_slot_port(u_int slot,u_int port);

/* Set chassis MAC address */
int c2691_chassis_set_mac_addr(c2691_t *router,char *mac_addr);

/* Show C2691 hardware info */
void c2691_show_hardware(c2691_t *router);

/* Initialize EEPROM groups */
void c2691_init_eeprom_groups(c2691_t *router);

/* dev_c2691_iofpga_init() */
int dev_c2691_iofpga_init(c2691_t *router,m_uint64_t paddr,m_uint32_t len);

/* Register the c2691 platform */
int c2691_platform_register(void);

/* Hypervisor C2691 initialization */
extern int hypervisor_c2691_init(vm_platform_t *platform);

/* NM drivers */
extern struct cisco_card_driver dev_c2691_nm_1fe_tx_driver;
extern struct cisco_card_driver dev_c2691_gt96100_fe_driver;
extern struct cisco_card_driver dev_c2691_nm_4t_driver;
extern struct cisco_card_driver dev_c2691_nm_16esw_driver;
extern struct cisco_card_driver dev_c2691_nm_nam_driver;
extern struct cisco_card_driver dev_c2691_nm_cids_driver;

/* WIC drivers */
extern struct cisco_card_driver *dev_c2691_mb_wic_drivers[];

#endif
