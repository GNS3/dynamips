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
#include "nmc93c46.h"
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
#define C2691_MAX_NM_BAYS  2

/* C2691 DUART Interrupt */
#define C2691_DUART_IRQ  5

/* C2691 Network I/O Interrupt */
#define C2691_NETIO_IRQ  2

/* C2691 GT64k DMA/Timer Interrupt */
#define C2691_GT96K_IRQ  3

/* C2691 External Interrupt */
#define C2691_EXT_IRQ    6

/* C2691 common device addresses */
#define C2691_GT96K_ADDR      0x14000000ULL
#define C2691_IOFPGA_ADDR     0x1e800000ULL
#define C2691_DUART_ADDR      0x3c100000ULL
#define C2691_BITBUCKET_ADDR  0x1ec00000ULL
#define C2691_ROM_ADDR        0x1fc00000ULL
#define C2691_SLOT0_ADDR      0x30000000ULL
#define C2691_SLOT1_ADDR      0x32000000ULL
#define C2691_PCI_IO_ADDR     0x100000000ULL

/* Offset of simulated NVRAM in ROM flash */
#define C2691_NVRAM_OFFSET    0xE0000
#define C2691_NVRAM_SIZE      0xE000

/* Reserved space for ROM in NVRAM */
#define C2691_NVRAM_ROM_RES_SIZE  2048

/* C2691 ELF Platform ID */
#define C2691_ELF_MACHINE_ID  0xFF /* ??? */

/* C2691 router */
typedef struct c2691_router c2691_t;

/* Prototype of NM driver initialization function */
typedef int (*c2691_nm_init_fn)(c2691_t *router,char *name,u_int nm_bay);

/* Prototype of NM driver shutdown function */
typedef int (*c2691_nm_shutdown_fn)(c2691_t *router,u_int nm_bay);

/* Prototype of NM NIO set function */
typedef int (*c2691_nm_set_nio_fn)(c2691_t *router,u_int nm_bay,u_int port_id,
                                   netio_desc_t *nio);

/* Prototype of NM NIO unset function */
typedef int (*c2691_nm_unset_nio_fn)(c2691_t *router,u_int nm_bay,
                                     u_int port_id);

/* Prototype of NM NIO show info function */
typedef int (*c2691_nm_show_info_fn)(c2691_t *router,u_int nm_bay);

/* C2691 Network Module Driver */
struct c2691_nm_driver {
   char *dev_type;
   int supported;
   int wic_slots;
   c2691_nm_init_fn nm_init;
   c2691_nm_shutdown_fn nm_shutdown;
   c2691_nm_set_nio_fn nm_set_nio;
   c2691_nm_unset_nio_fn nm_unset_nio;
   c2691_nm_show_info_fn nm_show_info;

   /* TODO: WAN Interface Cards (WIC) */
};

/* C2691 NIO binding to a slot/port */
struct c2691_nio_binding {
   netio_desc_t *nio;
   u_int port_id;
   struct c2691_nio_binding *prev,*next;
};

/* C2691 NM bay */
struct c2691_nm_bay {
   char *dev_name;                       /* Device name */
   char *dev_type;                       /* Device Type */
   struct cisco_eeprom eeprom;           /* NM EEPROM */
   struct pci_bus *pci_map;              /* PCI bus */
   struct c2691_nm_driver *nm_driver;    /* NM Driver */
   void *drv_info;                       /* Private driver info */
   struct c2691_nio_binding *nio_list;   /* NIO bindings to ports */
};

/* C2691 router */
struct c2691_router {
   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   /* Associated VM instance */
   vm_instance_t *vm;

   /* IO memory size to be passed to Smart Init */
   u_int nm_iomem_size;

   /* Chassis information */
   struct c2691_nm_bay nm_bay[C2691_MAX_NM_BAYS];
   m_uint8_t oir_status;

   /* 
    * Mainboard EEPROM.
    * It can be modified to change the chassis MAC address.
    */
   struct cisco_eeprom mb_eeprom;
   struct nmc93c46_group mb_eeprom_group;

   /* Network Module EEPROM */
   struct nmc93c46_group nm_eeprom_group;
};

/* Create a new router instance */
c2691_t *c2691_create_instance(char *name,int instance_id);

/* Delete a router instance */
int c2691_delete_instance(char *name);

/* Delete all router instances */
int c2691_delete_all_instances(void);

/* Save configuration of a C2691 instance */
void c2691_save_config(c2691_t *router,FILE *fd);

/* Save configurations of all C2691 instances */
void c2691_save_config_all(FILE *fd);

/* Set NM EEPROM definition */
int c2691_nm_set_eeprom(c2691_t *router,u_int nm_bay,
                        const struct cisco_eeprom *eeprom);

/* Unset NM EEPROM definition (empty bay) */
int c2691_nm_unset_eeprom(c2691_t *router,u_int nm_bay);

/* Check if a bay has a Network Module */
int c2691_nm_check_eeprom(c2691_t *router,u_int nm_bay);

/* Get bay info */
struct c2691_nm_bay *c2691_nm_get_info(c2691_t *router,u_int nm_bay);

/* Get NM type */
char *c2691_nm_get_type(c2691_t *router,u_int nm_bay);

/* Get driver info about the specified slot */
void *c2691_nm_get_drvinfo(c2691_t *router,u_int nm_bay);

/* Set driver info for the specified slot */
int c2691_nm_set_drvinfo(c2691_t *router,u_int nm_bay,void *drv_info);

/* Add a NM binding */
int c2691_nm_add_binding(c2691_t *router,char *dev_type,u_int nm_bay);

/* Remove a NM binding */
int c2691_nm_remove_binding(c2691_t *router,u_int nm_bay);

/* Find a NIO binding */
struct c2691_nio_binding *
c2691_nm_find_nio_binding(c2691_t *router,u_int nm_bay,u_int port_id);

/* Add a network IO binding */
int c2691_nm_add_nio_binding(c2691_t *router,u_int nm_bay,u_int port_id,
                             char *nio_name);

/* Remove a NIO binding */
int c2691_nm_remove_nio_binding(c2691_t *router,u_int nm_bay,u_int port_id);

/* Remove all NIO bindings for the specified NM */
int c2691_nm_remove_all_nio_bindings(c2691_t *router,u_int nm_bay);

/* Enable a Network IO descriptor for a Network Module */
int c2691_nm_enable_nio(c2691_t *router,u_int nm_bay,u_int port_id);

/* Disable Network IO descriptor of a Network Module */
int c2691_nm_disable_nio(c2691_t *router,u_int nm_bay,u_int port_id);

/* Enable all NIO of the specified NM */
int c2691_nm_enable_all_nio(c2691_t *router,u_int nm_bay);

/* Disable all NIO of the specified NM */
int c2691_nm_disable_all_nio(c2691_t *router,u_int nm_bay);

/* Initialize a Network Module */
int c2691_nm_init(c2691_t *router,u_int nm_bay);

/* Shutdown a Network Module */
int c2691_nm_shutdown(c2691_t *router,u_int nm_bay);

/* Shutdown all NM of a router */
int c2691_nm_shutdown_all(c2691_t *router);

/* Show info about all NMs */
int c2691_nm_show_all_info(c2691_t *router);

/* Create a Network Module (command line) */
int c2691_cmd_nm_create(c2691_t *router,char *str);

/* Add a Network IO descriptor binding (command line) */
int c2691_cmd_add_nio(c2691_t *router,char *str);

/* Show the list of available NM drivers */
void c2691_nm_show_drivers(void);

/* Set chassis MAC address */
int c2691_chassis_set_mac_addr(c2691_t *router,char *mac_addr);

/* Show C2691 hardware info */
void c2691_show_hardware(c2691_t *router);

/* Initialize default parameters for a C2691 */
void c2691_init_defaults(c2691_t *router);

/* Initialize the C2691 Platform */
int c2691_init_platform(c2691_t *router);

/* Initialize a Cisco 2691 instance */
int c2691_init_instance(c2691_t *router);

/* Stop a Cisco 2691 instance */
int c2691_stop_instance(c2691_t *router);

/* Initialize EEPROM groups */
void c2691_init_eeprom_groups(c2691_t *router);

/* dev_c2691_iofpga_init() */
int dev_c2691_iofpga_init(c2691_t *router,m_uint64_t paddr,m_uint32_t len);

/* NM drivers */
extern struct c2691_nm_driver dev_c2691_nm_1fe_tx_driver;
extern struct c2691_nm_driver dev_c2691_gt96100_fe_driver;
extern struct c2691_nm_driver dev_c2691_nm_4t_driver;
extern struct c2691_nm_driver dev_c2691_nm_16esw_driver;

#endif
