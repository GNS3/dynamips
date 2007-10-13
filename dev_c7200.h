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

#ifndef __DEV_C7200_H__
#define __DEV_C7200_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "device.h"
#include "pci_dev.h"
#include "nmc93cX6.h"
#include "dev_mv64460.h"
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

/* C7200 Timer IRQ (virtual) */
#define C7200_VTIMER_IRQ 0

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

/* Network IRQ */
#define C7200_NETIO_IRQ_BASE       32
#define C7200_NETIO_IRQ_PORT_BITS  3
#define C7200_NETIO_IRQ_PORT_MASK  ((1 << C7200_NETIO_IRQ_PORT_BITS) - 1)
#define C7200_NETIO_IRQ_PER_SLOT   (1 << C7200_NETIO_IRQ_PORT_BITS)
#define C7200_NETIO_IRQ_END        \
    (C7200_NETIO_IRQ_BASE + (C7200_MAX_PA_BAYS * C7200_NETIO_IRQ_PER_SLOT) - 1)

/* C7200 base ram limit (256 Mb) */
#define C7200_BASE_RAM_LIMIT  256

/* C7200 common device addresses */
#define C7200_GT64K_ADDR         0x14000000ULL
#define C7200_GT64K_SEC_ADDR     0x15000000ULL
#define C7200_BOOTFLASH_ADDR     0x1a000000ULL
#define C7200_NVRAM_ADDR         0x1e000000ULL
#define C7200_MPFPGA_ADDR        0x1e800000ULL
#define C7200_IOFPGA_ADDR        0x1e840000ULL
#define C7200_BITBUCKET_ADDR     0x1f000000ULL
#define C7200_ROM_ADDR           0x1fc00000ULL
#define C7200_IOMEM_ADDR         0x20000000ULL
#define C7200_SRAM_ADDR          0x4b000000ULL
#define C7200_BSWAP_ADDR         0xc0000000ULL
#define C7200_PCI_IO_ADDR        0x100000000ULL

/* NPE-G1 specific info */
#define C7200_G1_NVRAM_ADDR      0x1e400000ULL

/* NPE-G2 specific info */
#define C7200_G2_BSWAP_ADDR      0xce000000ULL
#define C7200_G2_BOOTFLASH_ADDR  0xe8000000ULL
#define C7200_G2_PCI_IO_ADDR     0xf0000000ULL
#define C7200_G2_MV64460_ADDR    0xf1000000ULL
#define C7200_G2_MPFPGA_ADDR     0xfe000000ULL
#define C7200_G2_IOFPGA_ADDR     0xfe040000ULL
#define C7200_G2_NVRAM_ADDR      0xff000000ULL
#define C7200_G2_ROM_ADDR        0xfff00000ULL

/* NVRAM size for NPE-G2: 2 Mb */
#define C7200_G2_NVRAM_SIZE      (2 * 1048576)

/* Reserved space for ROM in NVRAM */
#define C7200_NVRAM_ROM_RES_SIZE  2048

/* C7200 physical address bus mask: keep only the lower 33 bits */
#define C7200_ADDR_BUS_MASK   0x1ffffffffULL

/* C7200 ELF Platform ID */
#define C7200_ELF_MACHINE_ID  0x19

/* NPE families */
enum {
   C7200_NPE_FAMILY_MIPS = 0,
   C7200_NPE_FAMILY_PPC,
};

#define VM_C7200(vm) ((c7200_t *)vm->hw_data)

/* C7200 router */
typedef struct c7200_router c7200_t;

/* Prototype of NPE driver initialization function */
typedef int (*c7200_npe_init_fn)(c7200_t *router);

/* C7200 NPE Driver */
struct c7200_npe_driver {
   char *npe_type;
   int npe_family;
   c7200_npe_init_fn npe_init;
   int max_ram_size;
   int supported;
   m_uint64_t nvram_addr;
   int iocard_required;
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

   /* MV64460 device for NPE-G2 */
   struct mv64460_data *mv64460_sysctr;

   /* Midplane FPGA */
   struct c7200_mpfpga_data *mpfpga_data;

   /* NPE and OIR status */
   struct c7200_npe_driver *npe_driver;
   m_uint8_t oir_status;

   /* Hidden I/O bridge hack to support PCMCIA */
   struct pci_bridge *io_pci_bridge;
   struct pci_bus *pcmcia_bus;

   /* Midplane EEPROM can be modified to change the chassis MAC address... */
   struct cisco_eeprom cpu_eeprom,mp_eeprom,pem_eeprom;

   struct nmc93cX6_group sys_eeprom_g1;    /* EEPROMs for CPU and Midplane */
   struct nmc93cX6_group sys_eeprom_g2;    /* EEPROM for PEM */
   struct nmc93cX6_group pa_eeprom_g1;     /* EEPROMs for bays 0, 1, 3, 4 */
   struct nmc93cX6_group pa_eeprom_g2;     /* EEPROMs for bays 2, 5, 6 */
};

/* Initialize system EEPROM groups */
void c7200_init_sys_eeprom_groups(c7200_t *router);

/* Initialize midplane EEPROM groups */
void c7200_init_mp_eeprom_groups(c7200_t *router);

/* Set EEPROM for the specified slot */
int c7200_set_slot_eeprom(c7200_t *router,u_int slot,
                          struct cisco_eeprom *eeprom);

/* Get network IRQ for specified slot/port */
u_int c7200_net_irq_for_slot_port(u_int slot,u_int port);

/* Show the list of available PA drivers */
void c7200_pa_show_drivers(void);

/* Get an NPE driver */
struct c7200_npe_driver *c7200_npe_get_driver(char *npe_type);

/* Set the NPE type */
int c7200_npe_set_type(c7200_t *router,char *npe_type);

/* Set Midplane type */
int c7200_midplane_set_type(c7200_t *router,char *midplane_type);

/* Set chassis MAC address */
int c7200_midplane_set_mac_addr(c7200_t *router,char *mac_addr);

/* Show C7200 hardware info */
void c7200_show_hardware(c7200_t *router);

/* Trigger an OIR event */
int c7200_trigger_oir_event(c7200_t *router,u_int slot_mask);

/* Initialize a new PA while the virtual router is online (OIR) */
int c7200_pa_init_online(c7200_t *router,u_int pa_bay);

/* Stop a PA while the virtual router is online (OIR) */
int c7200_pa_stop_online(c7200_t *router,u_int pa_bay);

/* dev_c7200_iofpga_init() */
int dev_c7200_iofpga_init(c7200_t *router,m_uint64_t paddr,m_uint32_t len);

/* Register the c7200 platform */
int c7200_platform_register(void);

/* Hypervisor C7200 initialization */
extern int hypervisor_c7200_init(vm_platform_t *platform);

/* PA drivers */
extern struct cisco_card_driver dev_c7200_iocard_fe_driver;
extern struct cisco_card_driver dev_c7200_iocard_2fe_driver;
extern struct cisco_card_driver dev_c7200_iocard_ge_e_driver;
extern struct cisco_card_driver dev_c7200_pa_fe_tx_driver;
extern struct cisco_card_driver dev_c7200_pa_2fe_tx_driver;
extern struct cisco_card_driver dev_c7200_pa_ge_driver;
extern struct cisco_card_driver dev_c7200_pa_4e_driver;
extern struct cisco_card_driver dev_c7200_pa_8e_driver;
extern struct cisco_card_driver dev_c7200_pa_4t_driver;
extern struct cisco_card_driver dev_c7200_pa_8t_driver;
extern struct cisco_card_driver dev_c7200_pa_a1_driver;
extern struct cisco_card_driver dev_c7200_pa_pos_oc3_driver;
extern struct cisco_card_driver dev_c7200_pa_4b_driver;
extern struct cisco_card_driver dev_c7200_pa_mc8te1_driver;

#endif
