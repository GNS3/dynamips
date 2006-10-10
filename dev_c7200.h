/*
 * Cisco 7200 (Predator) simulation platform.
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

#ifndef __DEV_C7200_H__
#define __DEV_C7200_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "device.h"
#include "pci_dev.h"
#include "nmc93c46.h"
#include "net_io.h"
#include "vm.h"

/* Default C7200 parameters */
#define C7200_DEFAULT_NPE_TYPE     "npe-200"
#define C7200_DEFAULT_MIDPLANE     "vxr"
#define C7200_DEFAULT_RAM_SIZE     256
#define C7200_DEFAULT_ROM_SIZE     4
#define C7200_DEFAULT_NVRAM_SIZE   128
#define C7200_DEFAULT_CONF_REG     0x2102
#define C7200_DEFAULT_CLOCK_DIV    4
#define C7200_DEFAULT_RAM_MMAP     1
#define C7200_DEFAULT_DISK0_SIZE   64
#define C7200_DEFAULT_DISK1_SIZE   0

/* 6 slots + 1 I/O card */
#define C7200_MAX_PA_BAYS  7

/* C7200 DUART Interrupt */
#define C7200_DUART_IRQ  5

/* C7200 Network I/O Interrupt */
#define C7200_NETIO_IRQ  2

/* C7200 PA Management Interrupt handler */
#define C7200_PA_MGMT_IRQ  3

/* C7200 GT64k DMA/Timer Interrupt */
#define C7200_GT64K_IRQ  4

/* C7200 Error/OIR Interrupt */
#define C7200_OIR_IRQ    6

/* C7200 base ram limit (256 Mb) */
#define C7200_BASE_RAM_LIMIT  256

/* C7200 common device addresses */
#define C7200_GT64K_ADDR        0x14000000ULL
#define C7200_GT64K_SEC_ADDR    0x15000000ULL
#define C7200_BOOTFLASH_ADDR    0x1a000000ULL
#define C7200_NVRAM_ADDR        0x1e000000ULL
#define C7200_NPEG1_NVRAM_ADDR  0x1e400000ULL
#define C7200_MPFPGA_ADDR       0x1e800000ULL
#define C7200_IOFPGA_ADDR       0x1e840000ULL
#define C7200_BITBUCKET_ADDR    0x1f000000ULL
#define C7200_ROM_ADDR          0x1fc00000ULL
#define C7200_IOMEM_ADDR        0x20000000ULL
#define C7200_SRAM_ADDR         0x4b000000ULL
#define C7200_PCI_IO_ADDR       0x100000000ULL

/* Reserved space for ROM in NVRAM */
#define C7200_NVRAM_ROM_RES_SIZE  2048

/* C7200 physical address bus mask: keep only the lower 33 bits */
#define C7200_ADDR_BUS_MASK   0x1ffffffffULL

/* C7200 ELF Platform ID */
#define C7200_ELF_MACHINE_ID  0x19

/* C7200 router */
typedef struct c7200_router c7200_t;

/* C7200 EEPROM */
struct c7200_eeprom {
   char *name;
   m_uint16_t *data;
   u_int len;
};

/* Prototype of NPE driver initialization function */
typedef int (*c7200_npe_init_fn)(c7200_t *router);

/* Prototype of PA driver initialization function */
typedef int (*c7200_pa_init_fn)(c7200_t *router,char *name,u_int pa_bay);

/* Prototype of PA driver shutdown function */
typedef int (*c7200_pa_shutdown_fn)(c7200_t *router,u_int pa_bay);

/* Prototype of PA NIO set function */
typedef int (*c7200_pa_set_nio_fn)(c7200_t *router,u_int pa_bay,u_int port_id,
                                   netio_desc_t *nio);

/* Prototype of PA NIO unset function */
typedef int (*c7200_pa_unset_nio_fn)(c7200_t *router,u_int pa_bay,
                                     u_int port_id);

/* Prototype of NM NIO show info function */
typedef int (*c7200_pa_show_info_fn)(c7200_t *router,u_int pa_bay);

/* C7200 Port Adapter Driver */
struct c7200_pa_driver {
   char *dev_type;
   int supported;
   c7200_pa_init_fn pa_init;
   c7200_pa_shutdown_fn pa_shutdown;
   c7200_pa_set_nio_fn pa_set_nio;
   c7200_pa_unset_nio_fn pa_unset_nio;
   c7200_pa_show_info_fn pa_show_info;
};

/* C7200 NIO binding to a slot/port */
struct c7200_nio_binding {
   netio_desc_t *nio;
   u_int port_id;
   struct c7200_nio_binding *prev,*next;
};

/* C7200 PA bay */
struct c7200_pa_bay {
   char *dev_name;                       /* Device Name */
   char *dev_type;                       /* Device Type */
   struct pci_bus *pci_map;              /* PCI bus */
   struct nmc93c46_eeprom_def eeprom;    /* PA EEPROM */
   struct c7200_pa_driver *pa_driver;    /* PA driver */
   void *drv_info;                       /* Private driver info */
   struct c7200_nio_binding *nio_list;   /* NIO bindings to ports */
};

/* C7200 NPE Driver */
struct c7200_npe_driver {
   char *npe_type;
   c7200_npe_init_fn npe_init;
   int max_ram_size;
   int supported;
   m_uint64_t nvram_addr;
   int clpd6729_pci_bus;
   int clpd6729_pci_dev;
   int dec21140_pci_bus;
   int dec21140_pci_dev;
};

/* C7200 router */
struct c7200_router {
   /* Midplane type (standard,VXR) and chassis MAC address */
   char *midplane_type;
   int midplane_version;
   n_eth_addr_t mac_addr;

   /* Associated VM instance */
   vm_instance_t *vm;

   /* NPE and PA information */
   struct c7200_npe_driver *npe_driver;
   struct c7200_pa_bay pa_bay[C7200_MAX_PA_BAYS];
   m_uint8_t oir_status;

   /* Hidden I/O bridge hack to support PCMCIA */
   struct pci_bridge *io_pci_bridge;
   struct pci_bus *pcmcia_bus;

   /* Midplane EEPROM can be modified to change the chassis MAC address... */
   m_uint16_t mp_eeprom_data[64];
   
   struct nmc93c46_eeprom_def cpu_eeprom;  /* CPU EEPROM */
   struct nmc93c46_eeprom_def mp_eeprom;   /* Midplane EEPROM */
   struct nmc93c46_eeprom_def pem_eeprom;  /* Power Entry Module EEPROM */

   struct nmc93c46_group sys_eeprom_g1;    /* EEPROMs for CPU and Midplane */
   struct nmc93c46_group sys_eeprom_g2;    /* EEPROM for PEM */
   struct nmc93c46_group pa_eeprom_g1;     /* EEPROMs for bays 0, 1, 3, 4 */
   struct nmc93c46_group pa_eeprom_g2;     /* EEPROMs for bays 2, 5, 6 */
};

/* Initialize EEPROM groups */
void c7200_init_eeprom_groups(c7200_t *router);

/* Find an EEPROM in the specified array */
struct c7200_eeprom *c7200_get_eeprom(struct c7200_eeprom *eeproms,char *name);

/* Get an EEPROM for a given NPE model */
struct c7200_eeprom *c7200_get_cpu_eeprom(char *npe_name);

/* Get an EEPROM for a given midplane model */
struct c7200_eeprom *c7200_get_midplane_eeprom(char *midplane_name);

/* Get a PEM EEPROM for a given NPE model */
struct c7200_eeprom *c7200_get_pem_eeprom(char *npe_name);

/* Create a new router instance */
c7200_t *c7200_create_instance(char *name,int instance_id);

/* Delete a router instance */
int c7200_delete_instance(char *name);

/* Delete all router instances */
int c7200_delete_all_instances(void);

/* Save configuration of a C7200 instance */
void c7200_save_config(c7200_t *router,FILE *fd);

/* Save configurations of all C7200 instances */
void c7200_save_config_all(FILE *fd);

/* Set PA EEPROM definition */
int c7200_pa_set_eeprom(c7200_t *router,u_int pa_bay,
                        const struct c7200_eeprom *eeprom);

/* Unset PA EEPROM definition (empty bay) */
int c7200_pa_unset_eeprom(c7200_t *router,u_int pa_bay);

/* Check if a bay has a port adapter */
int c7200_pa_check_eeprom(c7200_t *router,u_int pa_bay);

/* Get bay info */
struct c7200_pa_bay *c7200_pa_get_info(c7200_t *router,u_int pa_bay);

/* Get PA type */
char *c7200_pa_get_type(c7200_t *router,u_int pa_bay);

/* Get driver info about the specified slot */
void *c7200_pa_get_drvinfo(c7200_t *router,u_int pa_bay);

/* Set driver info for the specified slot */
int c7200_pa_set_drvinfo(c7200_t *router,u_int pa_bay,void *drv_info);

/* Add a PA binding */
int c7200_pa_add_binding(c7200_t *router,char *dev_type,u_int pa_bay);

/* Remove a PA binding */
int c7200_pa_remove_binding(c7200_t *router,u_int pa_bay);

/* Find a NIO binding */
struct c7200_nio_binding *
c7200_pa_find_nio_binding(c7200_t *router,u_int pa_bay,u_int port_id);

/* Add a network IO binding */
int c7200_pa_add_nio_binding(c7200_t *router,u_int pa_bay,u_int port_id,
                             char *nio_name);

/* Remove a NIO binding */
int c7200_pa_remove_nio_binding(c7200_t *router,u_int pa_bay,u_int port_id);

/* Remove all NIO bindings for the specified PA */
int c7200_pa_remove_all_nio_bindings(c7200_t *router,u_int pa_bay);

/* Enable a Network IO descriptor for a Port Adapter */
int c7200_pa_enable_nio(c7200_t *router,u_int pa_bay,u_int port_id);

/* Disable Network IO descriptor of a Port Adapter */
int c7200_pa_disable_nio(c7200_t *router,u_int pa_bay,u_int port_id);

/* Enable all NIO of the specified PA */
int c7200_pa_enable_all_nio(c7200_t *router,u_int pa_bay);

/* Disable all NIO of the specified PA */
int c7200_pa_disable_all_nio(c7200_t *router,u_int pa_bay);

/* Initialize a Port Adapter */
int c7200_pa_init(c7200_t *router,u_int pa_bay);

/* Shutdown a Port Adapter */
int c7200_pa_shutdown(c7200_t *router,u_int pa_bay);

/* Shutdown all PA of a router */
int c7200_pa_shutdown_all(c7200_t *router);

/* Show info about all NMs */
int c7200_pa_show_all_info(c7200_t *router);

/* Create a Port Adapter (command line) */
int c7200_cmd_pa_create(c7200_t *router,char *str);

/* Add a Network IO descriptor binding (command line) */
int c7200_cmd_add_nio(c7200_t *router,char *str);

/* Show the list of available PA drivers */
void c7200_pa_show_drivers(void);

/* Get an NPE driver */
struct c7200_npe_driver *c7200_npe_get_driver(char *npe_type);

/* Set the NPE type */
int c7200_npe_set_type(c7200_t *router,char *npe_type);

/* Show the list of available NPE drivers */
void c7200_npe_show_drivers(void);

/* Set Midplane type */
int c7200_midplane_set_type(c7200_t *router,char *midplane_type);

/* Set chassis MAC address */
int c7200_midplane_set_mac_addr(c7200_t *router,char *mac_addr);

/* Show C7200 hardware info */
void c7200_show_hardware(c7200_t *router);

/* Initialize default parameters for a C7200 */
void c7200_init_defaults(c7200_t *router);

/* Initialize the C7200 Platform */
int c7200_init_platform(c7200_t *router);

/* Boot the IOS image */
int c7200_boot_ios(c7200_t *router);

/* Initialize a Cisco 7200 instance */
int c7200_init_instance(c7200_t *router);

/* Stop a Cisco 7200 instance */
int c7200_stop_instance(c7200_t *router);

/* Trigger an OIR event */
int c7200_trigger_oir_event(c7200_t *router,u_int slot_mask);

/* Initialize a new PA while the virtual router is online (OIR) */
int c7200_pa_init_online(c7200_t *router,u_int pa_bay);

/* Stop a PA while the virtual router is online (OIR) */
int c7200_pa_stop_online(c7200_t *router,u_int pa_bay);

/* dev_c7200_iofpga_init() */
int dev_c7200_iofpga_init(c7200_t *router,m_uint64_t paddr,m_uint32_t len);

/* dev_mpfpga_init() */
int dev_c7200_mpfpga_init(c7200_t *router,m_uint64_t paddr,m_uint32_t len);

/* PA drivers */
extern struct c7200_pa_driver dev_c7200_io_fe_driver;
extern struct c7200_pa_driver dev_c7200_pa_fe_tx_driver;
extern struct c7200_pa_driver dev_c7200_pa_4e_driver;
extern struct c7200_pa_driver dev_c7200_pa_8e_driver;
extern struct c7200_pa_driver dev_c7200_pa_4t_driver;
extern struct c7200_pa_driver dev_c7200_pa_8t_driver;
extern struct c7200_pa_driver dev_c7200_pa_a1_driver;
extern struct c7200_pa_driver dev_c7200_pa_pos_oc3_driver;
extern struct c7200_pa_driver dev_c7200_pa_4b_driver;

#endif
