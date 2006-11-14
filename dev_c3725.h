/*
 * Cisco 3725 simulation platform.
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
#include "nmc93c46.h"
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
#define C3725_MAX_NM_BAYS  3

/* C3725 DUART Interrupt */
#define C3725_DUART_IRQ  5

/* C3725 Network I/O Interrupt */
#define C3725_NETIO_IRQ  2

/* C3725 GT64k DMA/Timer Interrupt */
#define C3725_GT96K_IRQ  3

/* C3725 External Interrupt */
#define C3725_EXT_IRQ    6

/* C3725 common device addresses */
#define C3725_GT96K_ADDR      0x14000000ULL
#define C3725_IOFPGA_ADDR     0x1e800000ULL
#define C3725_DUART_ADDR      0x3c100000ULL
#define C3725_BITBUCKET_ADDR  0x1ec00000ULL
#define C3725_ROM_ADDR        0x1fc00000ULL
#define C3725_SLOT0_ADDR      0x30000000ULL
#define C3725_SLOT1_ADDR      0x32000000ULL
#define C3725_PCI_IO_ADDR     0x100000000ULL

/* Offset of simulated NVRAM in ROM flash */
#define C3725_NVRAM_OFFSET    0xE0000
#define C3725_NVRAM_SIZE      0xE000

/* Reserved space for ROM in NVRAM */
#define C3725_NVRAM_ROM_RES_SIZE  2048

/* C3725 ELF Platform ID */
#define C3725_ELF_MACHINE_ID  0xFF /* ??? */

/* C3725 router */
typedef struct c3725_router c3725_t;

/* Prototype of NM driver initialization function */
typedef int (*c3725_nm_init_fn)(c3725_t *router,char *name,u_int nm_bay);

/* Prototype of NM driver shutdown function */
typedef int (*c3725_nm_shutdown_fn)(c3725_t *router,u_int nm_bay);

/* Prototype of NM NIO set function */
typedef int (*c3725_nm_set_nio_fn)(c3725_t *router,u_int nm_bay,u_int port_id,
                                   netio_desc_t *nio);

/* Prototype of NM NIO unset function */
typedef int (*c3725_nm_unset_nio_fn)(c3725_t *router,u_int nm_bay,
                                     u_int port_id);

/* Prototype of NM NIO show info function */
typedef int (*c3725_nm_show_info_fn)(c3725_t *router,u_int nm_bay);

/* C3725 Network Module Driver */
struct c3725_nm_driver {
   char *dev_type;
   int supported;
   int wic_slots;
   c3725_nm_init_fn nm_init;
   c3725_nm_shutdown_fn nm_shutdown;
   c3725_nm_set_nio_fn nm_set_nio;
   c3725_nm_unset_nio_fn nm_unset_nio;
   c3725_nm_show_info_fn nm_show_info;

   /* TODO: WAN Interface Cards (WIC) */
};

/* C3725 NIO binding to a slot/port */
struct c3725_nio_binding {
   netio_desc_t *nio;
   u_int port_id;
   struct c3725_nio_binding *prev,*next;
};

/* C3725 NM bay */
struct c3725_nm_bay {
   char *dev_name;                       /* Device name */
   char *dev_type;                       /* Device Type */
   struct cisco_eeprom eeprom;           /* NM EEPROM */
   struct pci_bus *pci_map;              /* PCI bus */
   struct c3725_nm_driver *nm_driver;    /* NM Driver */
   void *drv_info;                       /* Private driver info */
   struct c3725_nio_binding *nio_list;   /* NIO bindings to ports */
};

/* C3725 router */
struct c3725_router {
   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   /* Associated VM instance */
   vm_instance_t *vm;

   /* IO memory size to be passed to Smart Init */
   u_int nm_iomem_size;

   /* Chassis information */
   struct c3725_nm_bay nm_bay[C3725_MAX_NM_BAYS];
   m_uint8_t oir_status;

   /* 
    * Mainboard EEPROM.
    * It can be modified to change the chassis MAC address.
    */
   struct cisco_eeprom mb_eeprom;
   struct nmc93c46_group mb_eeprom_group;

   /* Network Module EEPROMs */
   struct nmc93c46_group nm_eeprom_group[2];
};

/* Create a new router instance */
c3725_t *c3725_create_instance(char *name,int instance_id);

/* Delete a router instance */
int c3725_delete_instance(char *name);

/* Delete all router instances */
int c3725_delete_all_instances(void);

/* Save configuration of a C3725 instance */
void c3725_save_config(c3725_t *router,FILE *fd);

/* Save configurations of all C3725 instances */
void c3725_save_config_all(FILE *fd);

/* Get PCI device for the specified NM bay */
int c3725_nm_get_pci_device(u_int nm_bay);

/* Set NM EEPROM definition */
int c3725_nm_set_eeprom(c3725_t *router,u_int nm_bay,
                        const struct cisco_eeprom *eeprom);

/* Unset NM EEPROM definition (empty bay) */
int c3725_nm_unset_eeprom(c3725_t *router,u_int nm_bay);

/* Check if a bay has a Network Module */
int c3725_nm_check_eeprom(c3725_t *router,u_int nm_bay);

/* Get bay info */
struct c3725_nm_bay *c3725_nm_get_info(c3725_t *router,u_int nm_bay);

/* Get NM type */
char *c3725_nm_get_type(c3725_t *router,u_int nm_bay);

/* Get driver info about the specified slot */
void *c3725_nm_get_drvinfo(c3725_t *router,u_int nm_bay);

/* Set driver info for the specified slot */
int c3725_nm_set_drvinfo(c3725_t *router,u_int nm_bay,void *drv_info);

/* Add a NM binding */
int c3725_nm_add_binding(c3725_t *router,char *dev_type,u_int nm_bay);

/* Remove a NM binding */
int c3725_nm_remove_binding(c3725_t *router,u_int nm_bay);

/* Find a NIO binding */
struct c3725_nio_binding *
c3725_nm_find_nio_binding(c3725_t *router,u_int nm_bay,u_int port_id);

/* Add a network IO binding */
int c3725_nm_add_nio_binding(c3725_t *router,u_int nm_bay,u_int port_id,
                             char *nio_name);

/* Remove a NIO binding */
int c3725_nm_remove_nio_binding(c3725_t *router,u_int nm_bay,u_int port_id);

/* Remove all NIO bindings for the specified NM */
int c3725_nm_remove_all_nio_bindings(c3725_t *router,u_int nm_bay);

/* Enable a Network IO descriptor for a Network Module */
int c3725_nm_enable_nio(c3725_t *router,u_int nm_bay,u_int port_id);

/* Disable Network IO descriptor of a Network Module */
int c3725_nm_disable_nio(c3725_t *router,u_int nm_bay,u_int port_id);

/* Enable all NIO of the specified NM */
int c3725_nm_enable_all_nio(c3725_t *router,u_int nm_bay);

/* Disable all NIO of the specified NM */
int c3725_nm_disable_all_nio(c3725_t *router,u_int nm_bay);

/* Initialize a Network Module */
int c3725_nm_init(c3725_t *router,u_int nm_bay);

/* Shutdown a Network Module */
int c3725_nm_shutdown(c3725_t *router,u_int nm_bay);

/* Shutdown all NM of a router */
int c3725_nm_shutdown_all(c3725_t *router);

/* Show info about all NMs */
int c3725_nm_show_all_info(c3725_t *router);

/* Create a Network Module (command line) */
int c3725_cmd_nm_create(c3725_t *router,char *str);

/* Add a Network IO descriptor binding (command line) */
int c3725_cmd_add_nio(c3725_t *router,char *str);

/* Show the list of available NM drivers */
void c3725_nm_show_drivers(void);

/* Set chassis MAC address */
int c3725_chassis_set_mac_addr(c3725_t *router,char *mac_addr);

/* Show C3725 hardware info */
void c3725_show_hardware(c3725_t *router);

/* Initialize default parameters for a C3725 */
void c3725_init_defaults(c3725_t *router);

/* Initialize the C3725 Platform */
int c3725_init_platform(c3725_t *router);

/* Initialize a Cisco 3725 instance */
int c3725_init_instance(c3725_t *router);

/* Stop a Cisco 3725 instance */
int c3725_stop_instance(c3725_t *router);

/* Initialize EEPROM groups */
void c3725_init_eeprom_groups(c3725_t *router);

/* dev_c3725_iofpga_init() */
int dev_c3725_iofpga_init(c3725_t *router,m_uint64_t paddr,m_uint32_t len);

/* NM drivers */
extern struct c3725_nm_driver dev_c3725_nm_1fe_tx_driver;
extern struct c3725_nm_driver dev_c3725_gt96100_fe_driver;
extern struct c3725_nm_driver dev_c3725_nm_4t_driver;
extern struct c3725_nm_driver dev_c3725_nm_16esw_driver;

#endif
