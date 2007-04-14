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

/* 2600 characteristics: 1 NM + mainboard */
#define C2600_MAX_NM_BAYS  2

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
#define C2600_IOFPGA_ADDR     0x67400000ULL
#define C2600_NVRAM_ADDR      0x67c00000ULL
#define C2600_FLASH_ADDR      0x60000000ULL
#define C2600_PCICTRL_ADDR    0x68000000ULL
#define C2600_MPC860_ADDR     0x68010000ULL
#define C2600_DUART_ADDR      0xffe00000ULL
#define C2600_ROM_ADDR        0xfff00000ULL

/* Reserved space for ROM in NVRAM */
#define C2600_NVRAM_ROM_RES_SIZE  2048

/* C2600 ELF Platform ID */
#define C2600_ELF_MACHINE_ID  0x2b

/* C2600 router */
typedef struct c2600_router c2600_t;

/* Prototype of NM driver initialization function */
typedef int (*c2600_nm_init_fn)(c2600_t *router,char *name,u_int nm_bay);

/* Prototype of NM driver shutdown function */
typedef int (*c2600_nm_shutdown_fn)(c2600_t *router,u_int nm_bay);

/* Prototype of NM NIO set function */
typedef int (*c2600_nm_set_nio_fn)(c2600_t *router,u_int nm_bay,u_int port_id,
                                   netio_desc_t *nio);

/* Prototype of NM NIO unset function */
typedef int (*c2600_nm_unset_nio_fn)(c2600_t *router,u_int nm_bay,
                                     u_int port_id);

/* Prototype of NM NIO show info function */
typedef int (*c2600_nm_show_info_fn)(c2600_t *router,u_int nm_bay);

/* C2600 Network Module Driver */
struct c2600_nm_driver {
   char *dev_type;
   int supported;
   int wic_slots;
   c2600_nm_init_fn nm_init;
   c2600_nm_shutdown_fn nm_shutdown;
   c2600_nm_set_nio_fn nm_set_nio;
   c2600_nm_unset_nio_fn nm_unset_nio;
   c2600_nm_show_info_fn nm_show_info;

   /* TODO: WAN Interface Cards (WIC) */
};

/* C2600 NIO binding to a slot/port */
struct c2600_nio_binding {
   netio_desc_t *nio;
   u_int port_id;
   struct c2600_nio_binding *prev,*next;
};

/* C2600 NM bay */
struct c2600_nm_bay {
   char *dev_name;                       /* Device name */
   char *dev_type;                       /* Device Type */
   struct cisco_eeprom eeprom;           /* NM EEPROM */
   struct pci_bus *pci_map;              /* PCI bus */
   struct c2600_nm_driver *nm_driver;    /* NM Driver */
   void *drv_info;                       /* Private driver info */
   struct c2600_nio_binding *nio_list;   /* NIO bindings to ports */
};

/* C2600 router */
struct c2600_router {
   /* Mainboard type (2610, 2611, etc) */
   char *mainboard_type;

   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   /* Associated VM instance */
   vm_instance_t *vm;

   /* IO memory size to be passed to Smart Init */
   u_int nm_iomem_size;

   /* I/O FPGA */
   struct c2600_iofpga_data *iofpga_data;

   /* Chassis information */
   struct c2600_nm_bay nm_bay[C2600_MAX_NM_BAYS];
   m_uint8_t oir_status;

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

/* Create a new router instance */
c2600_t *c2600_create_instance(char *name,int instance_id);

/* Delete a router instance */
int c2600_delete_instance(char *name);

/* Delete all router instances */
int c2600_delete_all_instances(void);

/* Save configuration of a C2600 instance */
void c2600_save_config(c2600_t *router,FILE *fd);

/* Save configurations of all C2600 instances */
void c2600_save_config_all(FILE *fd);

/* Get network IRQ for specified slot/port */
u_int c2600_net_irq_for_slot_port(u_int slot,u_int port);

/* Show all available mainboards */
void c2600_mainboard_show_drivers(void);

/* Set NM EEPROM definition */
int c2600_nm_set_eeprom(c2600_t *router,u_int nm_bay,
                        const struct cisco_eeprom *eeprom);

/* Unset NM EEPROM definition (empty bay) */
int c2600_nm_unset_eeprom(c2600_t *router,u_int nm_bay);

/* Check if a bay has a Network Module */
int c2600_nm_check_eeprom(c2600_t *router,u_int nm_bay);

/* Get bay info */
struct c2600_nm_bay *c2600_nm_get_info(c2600_t *router,u_int nm_bay);

/* Get NM type */
char *c2600_nm_get_type(c2600_t *router,u_int nm_bay);

/* Get driver info about the specified slot */
void *c2600_nm_get_drvinfo(c2600_t *router,u_int nm_bay);

/* Set driver info for the specified slot */
int c2600_nm_set_drvinfo(c2600_t *router,u_int nm_bay,void *drv_info);

/* Add a NM binding */
int c2600_nm_add_binding(c2600_t *router,char *dev_type,u_int nm_bay);

/* Remove a NM binding */
int c2600_nm_remove_binding(c2600_t *router,u_int nm_bay);

/* Find a NIO binding */
struct c2600_nio_binding *
c2600_nm_find_nio_binding(c2600_t *router,u_int nm_bay,u_int port_id);

/* Add a network IO binding */
int c2600_nm_add_nio_binding(c2600_t *router,u_int nm_bay,u_int port_id,
                             char *nio_name);

/* Remove a NIO binding */
int c2600_nm_remove_nio_binding(c2600_t *router,u_int nm_bay,u_int port_id);

/* Remove all NIO bindings for the specified NM */
int c2600_nm_remove_all_nio_bindings(c2600_t *router,u_int nm_bay);

/* Enable a Network IO descriptor for a Network Module */
int c2600_nm_enable_nio(c2600_t *router,u_int nm_bay,u_int port_id);

/* Disable Network IO descriptor of a Network Module */
int c2600_nm_disable_nio(c2600_t *router,u_int nm_bay,u_int port_id);

/* Enable all NIO of the specified NM */
int c2600_nm_enable_all_nio(c2600_t *router,u_int nm_bay);

/* Disable all NIO of the specified NM */
int c2600_nm_disable_all_nio(c2600_t *router,u_int nm_bay);

/* Initialize a Network Module */
int c2600_nm_init(c2600_t *router,u_int nm_bay);

/* Shutdown a Network Module */
int c2600_nm_shutdown(c2600_t *router,u_int nm_bay);

/* Shutdown all NM of a router */
int c2600_nm_shutdown_all(c2600_t *router);

/* Show info about all NMs */
int c2600_nm_show_all_info(c2600_t *router);

/* Create a Network Module (command line) */
int c2600_cmd_nm_create(c2600_t *router,char *str);

/* Add a Network IO descriptor binding (command line) */
int c2600_cmd_add_nio(c2600_t *router,char *str);

/* Show the list of available NM drivers */
void c2600_nm_show_drivers(void);

/* Set mainboard type */
int c2600_mainboard_set_type(c2600_t *router,char *mainboard_type);

/* Set chassis MAC address */
int c2600_chassis_set_mac_addr(c2600_t *router,char *mac_addr);

/* Show C2600 hardware info */
void c2600_show_hardware(c2600_t *router);

/* Initialize default parameters for a C2600 */
void c2600_init_defaults(c2600_t *router);

/* Initialize the C2600 Platform */
int c2600_init_platform(c2600_t *router);

/* Initialize a Cisco 2600 instance */
int c2600_init_instance(c2600_t *router);

/* Stop a Cisco 2600 instance */
int c2600_stop_instance(c2600_t *router);

/* Initialize EEPROM groups */
void c2600_init_eeprom_groups(c2600_t *router);

/* Create the c2600 PCI controller device */
int dev_c2600_pci_init(vm_instance_t *vm,char *name,
                       m_uint64_t paddr,m_uint32_t len,
                       struct pci_bus *bus);

/* dev_c2600_iofpga_init() */
int dev_c2600_iofpga_init(c2600_t *router,m_uint64_t paddr,m_uint32_t len);

/* NM drivers */
extern struct c2600_nm_driver dev_c2600_mb1e_eth_driver;
extern struct c2600_nm_driver dev_c2600_mb2e_eth_driver;
extern struct c2600_nm_driver dev_c2600_mb1fe_eth_driver;
extern struct c2600_nm_driver dev_c2600_mb2fe_eth_driver;

extern struct c2600_nm_driver dev_c2600_nm_1e_driver;
extern struct c2600_nm_driver dev_c2600_nm_4e_driver;
extern struct c2600_nm_driver dev_c2600_nm_1fe_tx_driver;
extern struct c2600_nm_driver dev_c2600_nm_16esw_driver;

#endif
