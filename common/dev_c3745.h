/*
 * Cisco 3745 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco 3745 routines and definitions (EEPROM,...).
 */

#ifndef __DEV_C3745_H__
#define __DEV_C3745_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "device.h"
#include "pci_dev.h"
#include "nmc93cX6.h"
#include "dev_gt.h"
#include "net_io.h"
#include "vm.h"

/* Default C3745 parameters */
#define C3745_DEFAULT_RAM_SIZE     128
#define C3745_DEFAULT_ROM_SIZE     2
#define C3745_DEFAULT_NVRAM_SIZE   304
#define C3745_DEFAULT_CONF_REG     0x2102
#define C3745_DEFAULT_CLOCK_DIV    8
#define C3745_DEFAULT_RAM_MMAP     1
#define C3745_DEFAULT_DISK0_SIZE   16
#define C3745_DEFAULT_DISK1_SIZE   0
#define C3745_DEFAULT_IOMEM_SIZE   5   /* Percents! */

/* 3745 characteritics: 4 NM (+ motherboard), 3 WIC, 2 AIM */
#define C3745_MAX_NM_BAYS   5
#define C3745_MAX_WIC_BAYS  3

/* C3745 DUART Interrupt */
#define C3745_DUART_IRQ  5

/* C3745 Network I/O Interrupt */
#define C3745_NETIO_IRQ  2

/* C3745 GT64k DMA/Timer Interrupt */
#define C3745_GT96K_IRQ  3

/* C3745 External Interrupt */
#define C3745_EXT_IRQ    6

/* Network IRQ */
#define C3745_NETIO_IRQ_BASE       32
#define C3745_NETIO_IRQ_PORT_BITS  2
#define C3745_NETIO_IRQ_PORT_MASK  ((1 << C3745_NETIO_IRQ_PORT_BITS) - 1)
#define C3745_NETIO_IRQ_PER_SLOT   (1 << C3745_NETIO_IRQ_PORT_BITS)
#define C3745_NETIO_IRQ_END        \
  (C3745_NETIO_IRQ_BASE + (C3745_MAX_NM_BAYS * C3745_NETIO_IRQ_PER_SLOT) - 1)

/* C3745 common device addresses */
#define C3745_BITBUCKET_ADDR  0x1ec00000ULL
#define C3745_IOFPGA_ADDR     0x1fa00000ULL
#define C3745_ROM_ADDR        0x1fc00000ULL
#define C3745_GT96K_ADDR      0x24000000ULL
#define C3745_SLOT0_ADDR      0x30000000ULL
#define C3745_SLOT1_ADDR      0x32000000ULL
#define C3745_DUART_ADDR      0x3c100000ULL
#define C3745_WIC_ADDR        0x3c200000ULL
#define C3745_BSWAP_ADDR      0xc0000000ULL
#define C3745_PCI_IO_ADDR     0x100000000ULL

/* WIC interval in address space */
#define C3745_WIC_SIZE  0x2000

/* Offset of simulated NVRAM in ROM flash */
#define C3745_NVRAM_OFFSET    0xB0000
#define C3745_NVRAM_SIZE      0x4C000 // with backup

/* Reserved space for ROM in NVRAM */
#define C3745_NVRAM_ROM_RES_SIZE  0

/* C3745 ELF Platform ID */
#define C3745_ELF_MACHINE_ID  0x69

#define VM_C3745(vm) ((c3745_t *)vm->hw_data)

/* C3745 router */
typedef struct c3745_router c3745_t;

/* C3745 router */
struct c3745_router {
   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   char board_id[20];

   /* Associated VM instance */
   vm_instance_t *vm;

   /* GT96100 data */
   struct gt_data *gt_data;

   /* I/O FPGA */
   struct c3745_iofpga_data *iofpga_data;

   /* OIR status */
   m_uint8_t oir_status;

   /* 
    * System EEPROMs.
    * It can be modified to change the chassis MAC address.
    */
   struct cisco_eeprom sys_eeprom[3];
   struct nmc93cX6_group sys_eeprom_group;

   /* Network Module EEPROMs */
   struct nmc93cX6_group nm_eeprom_group[4];
};

/* Get WIC device address for the specified onboard port */
int c3745_get_onboard_wic_addr(u_int slot,m_uint64_t *phys_addr);

/* Set EEPROM for the specified slot */
int c3745_set_slot_eeprom(c3745_t *router,u_int slot,
                          struct cisco_eeprom *eeprom);

/* Get network IRQ for specified slot/port */
u_int c3745_net_irq_for_slot_port(u_int slot,u_int port);

/* Set chassis MAC address */
int c3745_chassis_set_mac_addr(c3745_t *router,char *mac_addr);

/* Set the system id */
int c3745_set_system_id(c3745_t *router,char *id);

/* Burn the system id into the appropriate eeprom if possible */
int c3745_refresh_systemid(c3745_t *router);

/* Show C3745 hardware info */
void c3745_show_hardware(c3745_t *router);

/* Initialize EEPROM groups */
void c3745_init_eeprom_groups(c3745_t *router);

/* dev_c3745_iofpga_init() */
int dev_c3745_iofpga_init(c3745_t *router,m_uint64_t paddr,m_uint32_t len);

/* Register the c3745 platform */
int c3745_platform_register(void);

/* Hypervisor C3745 initialization */
extern int hypervisor_c3745_init(vm_platform_t *platform);

/* NM drivers */
extern struct cisco_card_driver dev_c3745_nm_1fe_tx_driver;
extern struct cisco_card_driver dev_c3745_gt96100_fe_driver;
extern struct cisco_card_driver dev_c3745_nm_4t_driver;
extern struct cisco_card_driver dev_c3745_nm_16esw_driver;
extern struct cisco_card_driver dev_c3745_nmd_36esw_driver;
extern struct cisco_card_driver dev_c3745_nm_nam_driver;
extern struct cisco_card_driver dev_c3745_nm_cids_driver;

/* WIC drivers */
extern struct cisco_card_driver *dev_c3745_mb_wic_drivers[];

#endif
