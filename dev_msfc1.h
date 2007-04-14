/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco 7200 routines and definitions (EEPROM,...).
 *
 * Notes on IRQs (see "show stack"):
 *
 *   - triggering IRQ 3: we get indefinitely (for each slot):
 *        "Error: Unexpected NM Interrupt received from slot: 6"
 *
 *   - triggering IRQ 4: GT64010 reg access: probably "DMA/Timer Interrupt"
 *
 *   - triggering IRQ 6: we get (probably "OIR/Error Interrupt")
 *        %ERR-1-PERR: PCI bus parity error
 *        %ERR-1-SERR: PCI bus system/parity error
 *        %ERR-1-FATAL: Fatal error interrupt, No reloading
 *        err_stat=0x0, err_enable=0x0, mgmt_event=0xFFFFFFFF
 *
 */

#ifndef __DEV_MSFC1_H__
#define __DEV_MSFC1_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "device.h"
#include "pci_dev.h"
#include "nmc93cX6.h"
#include "net_io.h"
#include "vm.h"

/* Default MSFC1 parameters */
#define MSFC1_DEFAULT_RAM_SIZE     256
#define MSFC1_DEFAULT_ROM_SIZE     4
#define MSFC1_DEFAULT_NVRAM_SIZE   128
#define MSFC1_DEFAULT_CONF_REG     0x2102
#define MSFC1_DEFAULT_CLOCK_DIV    4
#define MSFC1_DEFAULT_RAM_MMAP     1

/* EOBC + IBC */
#define MSFC1_MAX_PA_BAYS  2

/* MSFC1 Timer IRQ (virtual) */
#define MSFC1_VTIMER_IRQ 0

/* MSFC1 DUART Interrupt */
#define MSFC1_DUART_IRQ  5

/* MSFC1 Network I/O Interrupt */
#define MSFC1_NETIO_IRQ  2

/* MSFC1 PA Management Interrupt handler */
#define MSFC1_PA_MGMT_IRQ  3

/* MSFC1 GT64k DMA/Timer Interrupt */
#define MSFC1_GT64K_IRQ  4

/* MSFC1 Error/OIR Interrupt */
#define MSFC1_OIR_IRQ    6

/* MSFC1 base ram limit (256 Mb) */
#define MSFC1_BASE_RAM_LIMIT  256

/* MSFC1 common device addresses */
#define MSFC1_GT64K_ADDR         0x14000000ULL
#define MSFC1_GT64K_SEC_ADDR     0x15000000ULL
#define MSFC1_BOOTFLASH_ADDR     0x1a000000ULL
#define MSFC1_NVRAM_ADDR         0x1e000000ULL
#define MSFC1_MPFPGA_ADDR        0x1e800000ULL
#define MSFC1_IOFPGA_ADDR        0x1e840000ULL
#define MSFC1_BITBUCKET_ADDR     0x1f000000ULL
#define MSFC1_ROM_ADDR           0x1fc00000ULL
#define MSFC1_IOMEM_ADDR         0x20000000ULL
#define MSFC1_SRAM_ADDR          0x4b000000ULL
#define MSFC1_BSWAP_ADDR         0xc0000000ULL
#define MSFC1_PCI_IO_ADDR        0x100000000ULL

/* SRAM size */
#define MSFC1_SRAM_SIZE  (4096*1024)

/* Reserved space for ROM in NVRAM */
#define MSFC1_NVRAM_ROM_RES_SIZE  2048

/* MSFC1 physical address bus mask: keep only the lower 33 bits */
#define MSFC1_ADDR_BUS_MASK   0x1ffffffffULL

/* MSFC1 ELF Platform ID */
#define MSFC1_ELF_MACHINE_ID  0x19

/* MSFC1 router */
typedef struct msfc1_router msfc1_t;

/* Prototype of NPE driver initialization function */
typedef int (*msfc1_npe_init_fn)(msfc1_t *router);

/* Prototype of PA driver initialization function */
typedef int (*msfc1_pa_init_fn)(msfc1_t *router,char *name,u_int pa_bay);

/* Prototype of PA driver shutdown function */
typedef int (*msfc1_pa_shutdown_fn)(msfc1_t *router,u_int pa_bay);

/* Prototype of PA NIO set function */
typedef int (*msfc1_pa_set_nio_fn)(msfc1_t *router,u_int pa_bay,u_int port_id,
                                   netio_desc_t *nio);

/* Prototype of PA NIO unset function */
typedef int (*msfc1_pa_unset_nio_fn)(msfc1_t *router,u_int pa_bay,
                                     u_int port_id);

/* Prototype of NM NIO show info function */
typedef int (*msfc1_pa_show_info_fn)(msfc1_t *router,u_int pa_bay);

/* MSFC1 Port Adapter Driver */
struct msfc1_pa_driver {
   char *dev_type;
   int supported;
   msfc1_pa_init_fn pa_init;
   msfc1_pa_shutdown_fn pa_shutdown;
   msfc1_pa_set_nio_fn pa_set_nio;
   msfc1_pa_unset_nio_fn pa_unset_nio;
   msfc1_pa_show_info_fn pa_show_info;
};

/* MSFC1 NIO binding to a slot/port */
struct msfc1_nio_binding {
   netio_desc_t *nio;
   u_int port_id;
   struct msfc1_nio_binding *prev,*next;
};

/* MSFC1 PA bay */
struct msfc1_pa_bay {
   char *dev_name;                       /* Device Name */
   char *dev_type;                       /* Device Type */
   struct cisco_eeprom eeprom;           /* PA EEPROM */
   struct pci_bus *pci_map;              /* PCI bus */
   struct msfc1_pa_driver *pa_driver;    /* PA driver */
   void *drv_info;                       /* Private driver info */
   struct msfc1_nio_binding *nio_list;   /* NIO bindings to ports */
};

/* MSFC1 NPE Driver */
struct msfc1_npe_driver {
   char *npe_type;
   int npe_family;
   msfc1_npe_init_fn npe_init;
   int max_ram_size;
   int supported;
   m_uint64_t nvram_addr;
   int iocard_required;
   int clpd6729_pci_bus;
   int clpd6729_pci_dev;
   int dec21140_pci_bus;
   int dec21140_pci_dev;
};

/* MSFC1 router */
struct msfc1_router {
   /* Chassis MAC address */
   n_eth_addr_t mac_addr;

   /* Associated VM instance */
   vm_instance_t *vm;

   /* NPE and PA information */
   struct msfc1_pa_bay pa_bay[MSFC1_MAX_PA_BAYS];

   /* Midplane EEPROM can be modified to change the chassis MAC address... */
   struct cisco_eeprom cpu_eeprom,mp_eeprom,pem_eeprom;

   /* EEPROMs for CPU and Midplane */
   struct nmc93cX6_group sys_eeprom_g1;
};

/* Initialize EEPROM groups */
void msfc1_init_eeprom_groups(msfc1_t *router);

/* Create a new router instance */
msfc1_t *msfc1_create_instance(char *name,int instance_id);

/* Delete a router instance */
int msfc1_delete_instance(char *name);

/* Delete all router instances */
int msfc1_delete_all_instances(void);

/* Save configuration of a MSFC1 instance */
void msfc1_save_config(msfc1_t *router,FILE *fd);

/* Save configurations of all MSFC1 instances */
void msfc1_save_config_all(FILE *fd);

/* Set PA EEPROM definition */
int msfc1_pa_set_eeprom(msfc1_t *router,u_int pa_bay,
                        const struct cisco_eeprom *eeprom);

/* Unset PA EEPROM definition (empty bay) */
int msfc1_pa_unset_eeprom(msfc1_t *router,u_int pa_bay);

/* Check if a bay has a port adapter */
int msfc1_pa_check_eeprom(msfc1_t *router,u_int pa_bay);

/* Get bay info */
struct msfc1_pa_bay *msfc1_pa_get_info(msfc1_t *router,u_int pa_bay);

/* Get PA type */
char *msfc1_pa_get_type(msfc1_t *router,u_int pa_bay);

/* Get driver info about the specified slot */
void *msfc1_pa_get_drvinfo(msfc1_t *router,u_int pa_bay);

/* Set driver info for the specified slot */
int msfc1_pa_set_drvinfo(msfc1_t *router,u_int pa_bay,void *drv_info);

/* Add a PA binding */
int msfc1_pa_add_binding(msfc1_t *router,char *dev_type,u_int pa_bay);

/* Remove a PA binding */
int msfc1_pa_remove_binding(msfc1_t *router,u_int pa_bay);

/* Find a NIO binding */
struct msfc1_nio_binding *
msfc1_pa_find_nio_binding(msfc1_t *router,u_int pa_bay,u_int port_id);

/* Add a network IO binding */
int msfc1_pa_add_nio_binding(msfc1_t *router,u_int pa_bay,u_int port_id,
                             char *nio_name);

/* Remove a NIO binding */
int msfc1_pa_remove_nio_binding(msfc1_t *router,u_int pa_bay,u_int port_id);

/* Remove all NIO bindings for the specified PA */
int msfc1_pa_remove_all_nio_bindings(msfc1_t *router,u_int pa_bay);

/* Enable a Network IO descriptor for a Port Adapter */
int msfc1_pa_enable_nio(msfc1_t *router,u_int pa_bay,u_int port_id);

/* Disable Network IO descriptor of a Port Adapter */
int msfc1_pa_disable_nio(msfc1_t *router,u_int pa_bay,u_int port_id);

/* Enable all NIO of the specified PA */
int msfc1_pa_enable_all_nio(msfc1_t *router,u_int pa_bay);

/* Disable all NIO of the specified PA */
int msfc1_pa_disable_all_nio(msfc1_t *router,u_int pa_bay);

/* Initialize a Port Adapter */
int msfc1_pa_init(msfc1_t *router,u_int pa_bay);

/* Shutdown a Port Adapter */
int msfc1_pa_shutdown(msfc1_t *router,u_int pa_bay);

/* Shutdown all PA of a router */
int msfc1_pa_shutdown_all(msfc1_t *router);

/* Show info about all NMs */
int msfc1_pa_show_all_info(msfc1_t *router);

/* Create a Port Adapter (command line) */
int msfc1_cmd_pa_create(msfc1_t *router,char *str);

/* Add a Network IO descriptor binding (command line) */
int msfc1_cmd_add_nio(msfc1_t *router,char *str);

/* Show the list of available PA drivers */
void msfc1_pa_show_drivers(void);

/* Set chassis MAC address */
int msfc1_midplane_set_mac_addr(msfc1_t *router,char *mac_addr);

/* Show MSFC1 hardware info */
void msfc1_show_hardware(msfc1_t *router);

/* Initialize default parameters for a MSFC1 */
void msfc1_init_defaults(msfc1_t *router);

/* Initialize a Cisco 7200 instance */
int msfc1_init_instance(msfc1_t *router);

/* Stop a Cisco 7200 instance */
int msfc1_stop_instance(msfc1_t *router);

/* dev_msfc1_iofpga_init() */
int dev_msfc1_iofpga_init(msfc1_t *router,m_uint64_t paddr,m_uint32_t len);

/* dev_mpfpga_init() */
int dev_msfc1_mpfpga_init(msfc1_t *router,m_uint64_t paddr,m_uint32_t len);

/* PA drivers */
extern struct msfc1_pa_driver dev_msfc1_iocard_fe_driver;
extern struct msfc1_pa_driver dev_msfc1_iocard_2fe_driver;
extern struct msfc1_pa_driver dev_msfc1_iocard_ge_e_driver;
extern struct msfc1_pa_driver dev_msfc1_pa_fe_tx_driver;
extern struct msfc1_pa_driver dev_msfc1_pa_2fe_tx_driver;
extern struct msfc1_pa_driver dev_msfc1_pa_ge_driver;
extern struct msfc1_pa_driver dev_msfc1_pa_4e_driver;
extern struct msfc1_pa_driver dev_msfc1_pa_8e_driver;
extern struct msfc1_pa_driver dev_msfc1_pa_4t_driver;
extern struct msfc1_pa_driver dev_msfc1_pa_8t_driver;
extern struct msfc1_pa_driver dev_msfc1_pa_a1_driver;
extern struct msfc1_pa_driver dev_msfc1_pa_pos_oc3_driver;
extern struct msfc1_pa_driver dev_msfc1_pa_4b_driver;
extern struct msfc1_pa_driver dev_msfc1_pa_mc8te1_driver;

#endif
