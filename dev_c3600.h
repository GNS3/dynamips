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
#include "nmc93c46.h"
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
//#define C3660_ELF_MACHINE_ID  ????

/* C3600 router */
typedef struct c3600_router c3600_t;

/* Prototype of chassis driver initialization function */
typedef int (*c3600_chassis_init_fn)(c3600_t *router);

/* Prototype of NM driver initialization function */
typedef int (*c3600_nm_init_fn)(c3600_t *router,char *name,u_int nm_bay);

/* Prototype of NM driver shutdown function */
typedef int (*c3600_nm_shutdown_fn)(c3600_t *router,u_int nm_bay);

/* Prototype of NM NIO set function */
typedef int (*c3600_nm_set_nio_fn)(c3600_t *router,u_int nm_bay,u_int port_id,
                                   netio_desc_t *nio);

/* Prototype of NM NIO unset function */
typedef int (*c3600_nm_unset_nio_fn)(c3600_t *router,u_int nm_bay,
                                     u_int port_id);

/* Prototype of NM NIO show info function */
typedef int (*c3600_nm_show_info_fn)(c3600_t *router,u_int nm_bay);

/* C3600 Network Module Driver */
struct c3600_nm_driver {
   char *dev_type;
   int supported;
   int wic_slots;
   c3600_nm_init_fn nm_init;
   c3600_nm_shutdown_fn nm_shutdown;
   c3600_nm_set_nio_fn nm_set_nio;
   c3600_nm_unset_nio_fn nm_unset_nio;
   c3600_nm_show_info_fn nm_show_info;

   /* TODO: WAN Interface Cards (WIC) */
};

/* C3600 NIO binding to a slot/port */
struct c3600_nio_binding {
   netio_desc_t *nio;
   u_int port_id;
   struct c3600_nio_binding *prev,*next;
};

/* C3600 NM bay */
struct c3600_nm_bay {
   char *dev_name;                       /* Device name */
   char *dev_type;                       /* Device Type */
   struct cisco_eeprom eeprom;           /* NM EEPROM */
   struct pci_bus *pci_map;              /* PCI bus */
   struct c3600_nm_driver *nm_driver;    /* NM Driver */
   void *drv_info;                       /* Private driver info */
   struct c3600_nio_binding *nio_list;   /* NIO bindings to ports */
};

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

   /* IO memory size to be passed to Smart Init */
   u_int nm_iomem_size;

   /* Chassis information */
   struct c3600_chassis_driver *chassis_driver;
   struct c3600_nm_bay nm_bay[C3600_MAX_NM_BAYS];
   m_uint8_t oir_status;

   /* 
    * Mainboard EEPROM.
    * It can be modified to change the chassis MAC address.
    */
   struct cisco_eeprom mb_eeprom;
   struct nmc93c46_group mb_eeprom_group;

   /* Network Module EEPROMs (3620/3640) */
   struct nmc93c46_group nm_eeprom_group;

   /* Cisco 3660 NM EEPROMs */
   struct nmc93c46_group c3660_nm_eeprom_group[C3600_MAX_NM_BAYS];
};

/* Create a new router instance */
c3600_t *c3600_create_instance(char *name,int instance_id);

/* Delete a router instance */
int c3600_delete_instance(char *name);

/* Delete all router instances */
int c3600_delete_all_instances(void);

/* Save configuration of a C3600 instance */
void c3600_save_config(c3600_t *router,FILE *fd);

/* Save configurations of all C3600 instances */
void c3600_save_config_all(FILE *fd);

/* Set NM EEPROM definition */
int c3600_nm_set_eeprom(c3600_t *router,u_int nm_bay,
                        const struct cisco_eeprom *eeprom);

/* Unset NM EEPROM definition (empty bay) */
int c3600_nm_unset_eeprom(c3600_t *router,u_int nm_bay);

/* Check if a bay has a Network Module */
int c3600_nm_check_eeprom(c3600_t *router,u_int nm_bay);

/* Get bay info */
struct c3600_nm_bay *c3600_nm_get_info(c3600_t *router,u_int nm_bay);

/* Get NM type */
char *c3600_nm_get_type(c3600_t *router,u_int nm_bay);

/* Get driver info about the specified slot */
void *c3600_nm_get_drvinfo(c3600_t *router,u_int nm_bay);

/* Set driver info for the specified slot */
int c3600_nm_set_drvinfo(c3600_t *router,u_int nm_bay,void *drv_info);

/* Add a NM binding */
int c3600_nm_add_binding(c3600_t *router,char *dev_type,u_int nm_bay);

/* Remove a NM binding */
int c3600_nm_remove_binding(c3600_t *router,u_int nm_bay);

/* Find a NIO binding */
struct c3600_nio_binding *
c3600_nm_find_nio_binding(c3600_t *router,u_int nm_bay,u_int port_id);

/* Add a network IO binding */
int c3600_nm_add_nio_binding(c3600_t *router,u_int nm_bay,u_int port_id,
                             char *nio_name);

/* Remove a NIO binding */
int c3600_nm_remove_nio_binding(c3600_t *router,u_int nm_bay,u_int port_id);

/* Remove all NIO bindings for the specified NM */
int c3600_nm_remove_all_nio_bindings(c3600_t *router,u_int nm_bay);

/* Enable a Network IO descriptor for a Network Module */
int c3600_nm_enable_nio(c3600_t *router,u_int nm_bay,u_int port_id);

/* Disable Network IO descriptor of a Network Module */
int c3600_nm_disable_nio(c3600_t *router,u_int nm_bay,u_int port_id);

/* Enable all NIO of the specified NM */
int c3600_nm_enable_all_nio(c3600_t *router,u_int nm_bay);

/* Disable all NIO of the specified NM */
int c3600_nm_disable_all_nio(c3600_t *router,u_int nm_bay);

/* Initialize a Network Module */
int c3600_nm_init(c3600_t *router,u_int nm_bay);

/* Shutdown a Network Module */
int c3600_nm_shutdown(c3600_t *router,u_int nm_bay);

/* Shutdown all NM of a router */
int c3600_nm_shutdown_all(c3600_t *router);

/* Show info about all NMs */
int c3600_nm_show_all_info(c3600_t *router);

/* Create a Network Module (command line) */
int c3600_cmd_nm_create(c3600_t *router,char *str);

/* Add a Network IO descriptor binding (command line) */
int c3600_cmd_add_nio(c3600_t *router,char *str);

/* Show the list of available NM drivers */
void c3600_nm_show_drivers(void);

/* Set chassis MAC address */
int c3600_chassis_set_mac_addr(c3600_t *router,char *mac_addr);

/* Set the chassis type */
int c3600_chassis_set_type(c3600_t *router,char *chassis_type);

/* Get the chassis ID */
int c3600_chassis_get_id(c3600_t *router);

/* Show the list of available chassis drivers */
void c3600_chassis_show_drivers(void);

/* Show C3600 hardware info */
void c3600_show_hardware(c3600_t *router);

/* Initialize default parameters for a C3600 */
void c3600_init_defaults(c3600_t *router);

/* Initialize the C3600 Platform */
int c3600_init_platform(c3600_t *router);

/* Initialize a Cisco 3600 instance */
int c3600_init_instance(c3600_t *router);

/* Stop a Cisco 3600 instance */
int c3600_stop_instance(c3600_t *router);

/* Initialize EEPROM groups */
void c3600_init_eeprom_groups(c3600_t *router);

/* dev_c3600_iofpga_init() */
int dev_c3600_iofpga_init(c3600_t *router,m_uint64_t paddr,m_uint32_t len);

/* NM drivers */
extern struct c3600_nm_driver dev_c3600_nm_1e_driver;
extern struct c3600_nm_driver dev_c3600_nm_4e_driver;
extern struct c3600_nm_driver dev_c3600_nm_1fe_tx_driver;
extern struct c3600_nm_driver dev_c3600_nm_4t_driver;
extern struct c3600_nm_driver dev_c3600_leopard_2fe_driver;
extern struct c3600_nm_driver dev_c3600_nm_16esw_driver;

#endif
