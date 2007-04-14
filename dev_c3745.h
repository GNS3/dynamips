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
#include "net_io.h"
#include "vm.h"

/* Default C3745 parameters */
#define C3745_DEFAULT_RAM_SIZE     128
#define C3745_DEFAULT_ROM_SIZE     2
#define C3745_DEFAULT_NVRAM_SIZE   128
#define C3745_DEFAULT_CONF_REG     0x2102
#define C3745_DEFAULT_CLOCK_DIV    8
#define C3745_DEFAULT_RAM_MMAP     1
#define C3745_DEFAULT_DISK0_SIZE   16
#define C3745_DEFAULT_DISK1_SIZE   0
#define C3745_DEFAULT_IOMEM_SIZE   5   /* Percents! */

/* 3745 characteritics: 4 NM, 3 WIC, 2 AIM */
#define C3745_MAX_NM_BAYS  5

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
#define C3745_GT96K_ADDR      0x24000000ULL
#define C3745_IOFPGA_ADDR     0x1fa00000ULL
#define C3745_DUART_ADDR      0x3c100000ULL
#define C3745_BITBUCKET_ADDR  0x1ec00000ULL
#define C3745_ROM_ADDR        0x1fc00000ULL
#define C3745_SLOT0_ADDR      0x30000000ULL
#define C3745_SLOT1_ADDR      0x32000000ULL
#define C3745_PCI_IO_ADDR     0x100000000ULL

/* Offset of simulated NVRAM in ROM flash */
#define C3745_NVRAM_OFFSET    0xB0000
#define C3745_NVRAM_SIZE      0x20000

/* Reserved space for ROM in NVRAM */
#define C3745_NVRAM_ROM_RES_SIZE  2048

/* C3745 ELF Platform ID */
#define C3745_ELF_MACHINE_ID  0xFF /* ??? */

/* C3745 router */
typedef struct c3745_router c3745_t;

/* Prototype of NM driver initialization function */
typedef int (*c3745_nm_init_fn)(c3745_t *router,char *name,u_int nm_bay);

/* Prototype of NM driver shutdown function */
typedef int (*c3745_nm_shutdown_fn)(c3745_t *router,u_int nm_bay);

/* Prototype of NM NIO set function */
typedef int (*c3745_nm_set_nio_fn)(c3745_t *router,u_int nm_bay,u_int port_id,
                                   netio_desc_t *nio);

/* Prototype of NM NIO unset function */
typedef int (*c3745_nm_unset_nio_fn)(c3745_t *router,u_int nm_bay,
                                     u_int port_id);

/* Prototype of NM NIO show info function */
typedef int (*c3745_nm_show_info_fn)(c3745_t *router,u_int nm_bay);

/* C3745 Network Module Driver */
struct c3745_nm_driver {
   char *dev_type;
   int supported;
   int wic_slots;
   c3745_nm_init_fn nm_init;
   c3745_nm_shutdown_fn nm_shutdown;
   c3745_nm_set_nio_fn nm_set_nio;
   c3745_nm_unset_nio_fn nm_unset_nio;
   c3745_nm_show_info_fn nm_show_info;

   /* TODO: WAN Interface Cards (WIC) */
};

/* C3745 NIO binding to a slot/port */
struct c3745_nio_binding {
   netio_desc_t *nio;
   u_int port_id;
   struct c3745_nio_binding *prev,*next;
};

/* C3745 NM bay */
struct c3745_nm_bay {
   char *dev_name;                       /* Device name */
   char *dev_type;                       /* Device Type */
   struct cisco_eeprom eeprom;           /* NM EEPROM */
   struct pci_bus *pci_map;              /* PCI bus */
   struct c3745_nm_driver *nm_driver;    /* NM Driver */
   void *drv_info;                       /* Private driver info */
   struct c3745_nio_binding *nio_list;   /* NIO bindings to ports */
};

/* C3745 router */
struct c3745_router {
   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   /* Associated VM instance */
   vm_instance_t *vm;

   /* IO memory size to be passed to Smart Init */
   u_int nm_iomem_size;

   /* I/O FPGA */
   struct c3745_iofpga_data *iofpga_data;

   /* Chassis information */
   struct c3745_nm_bay nm_bay[C3745_MAX_NM_BAYS];
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

/* Create a new router instance */
c3745_t *c3745_create_instance(char *name,int instance_id);

/* Delete a router instance */
int c3745_delete_instance(char *name);

/* Delete all router instances */
int c3745_delete_all_instances(void);

/* Save configuration of a C3745 instance */
void c3745_save_config(c3745_t *router,FILE *fd);

/* Save configurations of all C3745 instances */
void c3745_save_config_all(FILE *fd);

/* Get network IRQ for specified slot/port */
u_int c3745_net_irq_for_slot_port(u_int slot,u_int port);

/* Set NM EEPROM definition */
int c3745_nm_set_eeprom(c3745_t *router,u_int nm_bay,
                        const struct cisco_eeprom *eeprom);

/* Unset NM EEPROM definition (empty bay) */
int c3745_nm_unset_eeprom(c3745_t *router,u_int nm_bay);

/* Check if a bay has a Network Module */
int c3745_nm_check_eeprom(c3745_t *router,u_int nm_bay);

/* Get bay info */
struct c3745_nm_bay *c3745_nm_get_info(c3745_t *router,u_int nm_bay);

/* Get NM type */
char *c3745_nm_get_type(c3745_t *router,u_int nm_bay);

/* Get driver info about the specified slot */
void *c3745_nm_get_drvinfo(c3745_t *router,u_int nm_bay);

/* Set driver info for the specified slot */
int c3745_nm_set_drvinfo(c3745_t *router,u_int nm_bay,void *drv_info);

/* Add a NM binding */
int c3745_nm_add_binding(c3745_t *router,char *dev_type,u_int nm_bay);

/* Remove a NM binding */
int c3745_nm_remove_binding(c3745_t *router,u_int nm_bay);

/* Find a NIO binding */
struct c3745_nio_binding *
c3745_nm_find_nio_binding(c3745_t *router,u_int nm_bay,u_int port_id);

/* Add a network IO binding */
int c3745_nm_add_nio_binding(c3745_t *router,u_int nm_bay,u_int port_id,
                             char *nio_name);

/* Remove a NIO binding */
int c3745_nm_remove_nio_binding(c3745_t *router,u_int nm_bay,u_int port_id);

/* Remove all NIO bindings for the specified NM */
int c3745_nm_remove_all_nio_bindings(c3745_t *router,u_int nm_bay);

/* Enable a Network IO descriptor for a Network Module */
int c3745_nm_enable_nio(c3745_t *router,u_int nm_bay,u_int port_id);

/* Disable Network IO descriptor of a Network Module */
int c3745_nm_disable_nio(c3745_t *router,u_int nm_bay,u_int port_id);

/* Enable all NIO of the specified NM */
int c3745_nm_enable_all_nio(c3745_t *router,u_int nm_bay);

/* Disable all NIO of the specified NM */
int c3745_nm_disable_all_nio(c3745_t *router,u_int nm_bay);

/* Initialize a Network Module */
int c3745_nm_init(c3745_t *router,u_int nm_bay);

/* Shutdown a Network Module */
int c3745_nm_shutdown(c3745_t *router,u_int nm_bay);

/* Shutdown all NM of a router */
int c3745_nm_shutdown_all(c3745_t *router);

/* Show info about all NMs */
int c3745_nm_show_all_info(c3745_t *router);

/* Create a Network Module (command line) */
int c3745_cmd_nm_create(c3745_t *router,char *str);

/* Add a Network IO descriptor binding (command line) */
int c3745_cmd_add_nio(c3745_t *router,char *str);

/* Show the list of available NM drivers */
void c3745_nm_show_drivers(void);

/* Set chassis MAC address */
int c3745_chassis_set_mac_addr(c3745_t *router,char *mac_addr);

/* Show C3745 hardware info */
void c3745_show_hardware(c3745_t *router);

/* Initialize default parameters for a C3745 */
void c3745_init_defaults(c3745_t *router);

/* Initialize the C3745 Platform */
int c3745_init_platform(c3745_t *router);

/* Initialize a Cisco 3745 instance */
int c3745_init_instance(c3745_t *router);

/* Stop a Cisco 3745 instance */
int c3745_stop_instance(c3745_t *router);

/* Initialize EEPROM groups */
void c3745_init_eeprom_groups(c3745_t *router);

/* dev_c3745_iofpga_init() */
int dev_c3745_iofpga_init(c3745_t *router,m_uint64_t paddr,m_uint32_t len);

/* NM drivers */
extern struct c3745_nm_driver dev_c3745_nm_1fe_tx_driver;
extern struct c3745_nm_driver dev_c3745_gt96100_fe_driver;
extern struct c3745_nm_driver dev_c3745_nm_4t_driver;
extern struct c3745_nm_driver dev_c3745_nm_16esw_driver;

#endif
