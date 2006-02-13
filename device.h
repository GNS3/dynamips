/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <sys/types.h>
#include "utils.h"
#include "mips64.h"
#include "cpu.h"
#include "net_io.h"

/* Prevent MMAPed access by MTS */
#define VDEVICE_FLAG_NO_MTS_MMAP  1

typedef void *(*dev_handler_t)(cpu_mips_t *cpu,struct vdevice *dev,
                               m_uint32_t offset,u_int op_size,u_int op_type,
                               m_uint64_t *data);

/* Virtual Device */
struct vdevice {
   char *name;
   m_uint64_t phys_addr;
   m_uint32_t phys_len;
   m_iptr_t host_addr;
   void *priv_data;
   int flags;
   int fd;
   dev_handler_t handler;
};

/* PCI part */
#include "pci_dev.h"

/* Map a memory zone from a file */
u_char *memzone_map_file(int fd,size_t len);

/* Create a file to serve as a memory zone */
int memzone_create_file(char *filename,size_t len,u_char **ptr);

/* Get device by ID */
struct vdevice *dev_get_by_id(cpu_mips_t *cpu,u_int dev_id);

/* Get device by name */
struct vdevice *dev_get_by_name(cpu_mips_t *cpu,char *name,u_int *dev_id);

/* Device lookup by physical address */
struct vdevice *dev_lookup(cpu_mips_t *cpu,m_uint64_t paddr,u_int *dev_id);

/* Allocate a device and return its identifier */
struct vdevice *dev_create(char *name);

/* Show properties of a device */
void dev_show(struct vdevice *dev);

/* Show the device list */
void dev_show_list(cpu_mips_t *cpu);

/* device access function */
void *dev_access(cpu_mips_t *cpu,u_int dev_id,m_uint32_t offset,
                 u_int op_size,u_int op_type,m_uint64_t *data);

/* Remap a device at specified physical address */
struct vdevice *dev_remap(char *name,struct vdevice *orig,
                          m_uint64_t paddr,m_uint32_t len);

/* create ram */
int dev_create_ram(cpu_group_t *cpu_group,char *name,char *filename,
                   m_uint64_t paddr,m_uint32_t len);

/* create a memory alias */
int dev_create_ram_alias(cpu_group_t *cpu_group,char *name,char *orig_name,
                         m_uint64_t paddr,m_uint32_t len);

/* create a dummy console */
int dev_create_dummy_console(cpu_group_t *cpu_group);

/* dev_zero_init() */
int dev_zero_init(cpu_group_t *cpu_group,char *name,
                  m_uint64_t paddr,m_uint32_t len);

/* Export configuration from NVRAM */
int dev_nvram_export_config(char *nvram_filename,char *cfg_filename);

/* Directly extract the configuration from the NVRAM device */
int dev_nvram_extract_config(cpu_group_t *cpu_group,char *cfg_filename);

/* Directly push the IOS configuration to the NVRAM device */
int dev_nvram_push_config(cpu_group_t *cpu_group,char *cfg_filename);

/* dev_nvram_init() */
int dev_nvram_init(cpu_group_t *cpu_group,char *filename,
                   m_uint64_t paddr,m_uint32_t len,u_int *conf_reg);

/* dev_rom_init() */
int dev_rom_init(cpu_group_t *cpu_group,m_uint64_t paddr,m_uint32_t len);

/* dev_bootflash_init() */
int dev_bootflash_init(cpu_group_t *cpu_group,char *filename,
                       m_uint64_t paddr,m_uint32_t len);

/* dev_mpfpga_init() */
int dev_mpfpga_init(cpu_group_t *cpu_group,m_uint64_t paddr,m_uint32_t len);

/* Create a new GT64010 controller */
int dev_gt64010_init(cpu_group_t *cpu_group,m_uint64_t paddr,m_uint32_t len,
                     u_int irq,struct pci_data **pci_data);

/* Create a new GT64120 controller */
int dev_gt64120_init(cpu_group_t *cpu_group,m_uint64_t paddr,m_uint32_t len,
                     u_int irq,struct pci_data **pci_data);

/* dev_dec21050_init() */
int dev_dec21050_init(struct pci_data *pci_data,int pci_bus,int pci_device);

/* dev_dec21150_init() */
int dev_dec21150_init(struct pci_data *pci_data,int pci_bus,int pci_device);

/* dev_pericom_init() */
int dev_pericom_init(struct pci_data *pci_data,int pci_bus,int pci_device);

/* dev_clpd6729_init() */
int dev_clpd6729_init(cpu_group_t *cpu_group,struct pci_data *pci_data,
                      int pci_bus,int pci_device,
                      struct pci_io_data *pci_io_data,
                      m_uint32_t io_start,m_uint32_t io_end);

/* dev_c7200_sram_init() */
int dev_c7200_sram_init(cpu_group_t *cpu_group,char *name,char *filename,
                        m_uint64_t paddr,m_uint32_t len,
                        struct pci_data *pci_data,int pci_bus);

/* dev_sb1_duart_init() */
int dev_sb1_duart_init(cpu_group_t *cpu_group,m_uint64_t paddr,m_uint32_t len);

#endif
