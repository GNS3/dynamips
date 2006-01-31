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

#include "device.h"
#include "pci_dev.h"
#include "net_io.h"

/* 6 slots + 1 I/O card */
#define MAX_PA_BAYS  7

/* C7200 DUART Interrupt */
#define C7200_DUART_IRQ  5

/* C7200 Network I/O Interrupt */
#define C7200_NETIO_IRQ  2

/* C7200 GT64k DMA/Timer Interrupt */
#define C7200_GT64K_IRQ  4

/* C7200 common device addresses */
#define C7200_BOOTFLASH_ADDR 0x1a000000ULL
#define C7200_NVRAM_ADDR     0x1e000000ULL
#define C7200_MPFPGA_ADDR    0x1e800000ULL
#define C7200_IOFPGA_ADDR    0x1e840000ULL

/* Reserved space for ROM in NVRAM */
#define C7200_NVRAM_ROM_RES_SIZE  2048

/* C7200 physical address bus mask: keep only the lower 33 bits */
#define C7200_ADDR_BUS_MASK  0x1ffffffffULL

/* Default NPE and Midplane types */
#define C7200_DEFAULT_NPE_TYPE "npe-200"
#define C7200_DEFAULT_MIDPLANE "vxr"

/* C7200 router */
typedef struct c7200_router c7200_t;

/* C7200 EEPROM */
struct c7200_eeprom {
   char *name;
   u_short *data;
   u_int len;
};

/* Prototype of PA driver initialization function */
typedef int (*c7200_pa_init_fn)(c7200_t *router,char *name,u_int pa_bay,
                                netio_desc_t *nio);

/* C7200 Port Adapter Driver */
struct c7200_pa_driver {
   char *dev_type;
   int supported;
   c7200_pa_init_fn pa_init;
};

/* Prototype of NPE driver initialization function */
typedef int (*c7200_npe_init_fn)(c7200_t *router,cpu_group_t *cpu_group,
                                 char **pa_desc,char *mac_addr);

/* C7200 NPE Driver */
struct c7200_npe_driver {
   char *npe_type;
   c7200_npe_init_fn npe_init;
   int supported;
   int clpd6729_pci_bus;
   int clpd6729_pci_dev;
   int dec21140_pci_bus;
   int dec21140_pci_dev;
};

/* C7200 router */
struct c7200_router {
   cpu_group_t *cpu_group;
   struct c7200_npe_driver *npe_driver;
   struct pci_data *pci_mb[2];
   struct pci_data *pa_pci_map[MAX_PA_BAYS];
   struct pci_io_data *pci_io_space;
   char *npe_type,*midplane_type;
};

/* Find an EEPROM in the specified array */
struct c7200_eeprom *c7200_get_eeprom(struct c7200_eeprom *eeproms,char *name);

/* Get an EEPROM for a given NPE model */
struct c7200_eeprom *c7200_get_cpu_eeprom(char *npe_name);

/* Get an EEPROM for a given midplane model */
struct c7200_eeprom *c7200_get_midplane_eeprom(char *midplane_name);

/* Get a PEM EEPROM for a given NPE model */
struct c7200_eeprom *c7200_get_pem_eeprom(char *npe_name);

/* Set the base MAC address of the chassis */
int c7200_set_mac_addr(struct c7200_eeprom *mp_eeprom,m_eth_addr_t *addr);

/* Initialize a Port Adapter */
int c7200_pa_init(c7200_t *router,char *dev_type,u_int pa_bay,
                  netio_desc_t *nio);

/* Create a Port Adapter and bind it to a Network IO descriptor */
int c7200_pa_create(c7200_t *router,char *str);

/* Show the list of available PA drivers */
void c7200_pa_show_drivers(void);

/* Show the list of available NPE drivers */
void c7200_npe_show_drivers(void);

/* Initialize the C7200 Platform */
int c7200_init_platform(c7200_t *router,char **pa_desc,char *mac_addr);

/* dev_c7200_iocard_init() */
int dev_c7200_iocard_init(c7200_t *router,char *name,u_int pa_bay,
                          netio_desc_t *nio);

/* dev_c7200_pa_fe_tx_init() */
int dev_c7200_pa_fe_tx_init(c7200_t *router,char *name,u_int pa_bay,
                            netio_desc_t *nio);

/* dev_c7200_pa_a1_init() */
int dev_c7200_pa_a1_init(c7200_t *router,char *name,u_int pa_bay,
                         netio_desc_t *nio);

/* dev_c7200_pa_4t_init() */
int dev_c7200_pa_4t_init(c7200_t *router,char *name,u_int pa_bay,
                         netio_desc_t *nio);

#endif
