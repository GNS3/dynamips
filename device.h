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
#include "vm.h"

/* Device Flags */
#define VDEVICE_FLAG_NO_MTS_MMAP  0x01 /* Prevent MMAPed access by MTS */
#define VDEVICE_FLAG_CACHING      0x02 /* Device does support caching */
#define VDEVICE_FLAG_REMAP        0x04 /* Physical address remapping */
#define VDEVICE_FLAG_SYNC         0x08 /* Forced sync */

typedef void *(*dev_handler_t)(cpu_mips_t *cpu,struct vdevice *dev,
                               m_uint32_t offset,u_int op_size,u_int op_type,
                               m_uint64_t *data);

/* Virtual Device */
struct vdevice {
   char *name;
   u_int id;
   m_uint64_t phys_addr;
   m_uint32_t phys_len;
   m_iptr_t host_addr;
   void *priv_data;
   int flags;
   int fd;
   dev_handler_t handler;
   struct vdevice *next,**pprev;
};

/* PCI part */
#include "pci_dev.h"

/* device access function */
static forced_inline 
void *dev_access_fast(cpu_mips_t *cpu,u_int dev_id,m_uint32_t offset,
                      u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct vdevice *dev = cpu->vm->dev_array[dev_id];

   if (unlikely(!dev)) {
      cpu_log(cpu,"dev_access_fast","null handler (dev_id=%u,offset=0x%x)\n",
              dev_id,offset);
      return NULL;
   }

   return(dev->handler(cpu,dev,offset,op_size,op_type,data));
}


/* Map a memory zone from a file */
u_char *memzone_map_file(int fd,size_t len);

/* Create a file to serve as a memory zone */
int memzone_create_file(char *filename,size_t len,u_char **ptr);

/* Get device by ID */
struct vdevice *dev_get_by_id(vm_instance_t *vm,u_int dev_id);

/* Get device by name */
struct vdevice *dev_get_by_name(vm_instance_t *vm,char *name);

/* Device lookup by physical address */
struct vdevice *dev_lookup(vm_instance_t *vm,m_uint64_t phys_addr,int cached);

/* Find the next device after the specified address */
struct vdevice *dev_lookup_next(vm_instance_t *vm,m_uint64_t phys_addr,
                                struct vdevice *dev_start,int cached);

/* Initialize a device */
void dev_init(struct vdevice *dev);

/* Allocate a device */
struct vdevice *dev_create(char *name);

/* Remove a device */
void dev_remove(vm_instance_t *vm,struct vdevice *dev);

/* Show properties of a device */
void dev_show(struct vdevice *dev);

/* Show the device list */
void dev_show_list(vm_instance_t *vm);

/* device access function */
void *dev_access(cpu_mips_t *cpu,u_int dev_id,m_uint32_t offset,
                 u_int op_size,u_int op_type,m_uint64_t *data);

/* Remap a device at specified physical address */
struct vdevice *dev_remap(char *name,struct vdevice *orig,
                          m_uint64_t paddr,m_uint32_t len);

/* Create a RAM device */
struct vdevice *dev_create_ram(vm_instance_t *vm,char *name,char *filename,
                               m_uint64_t paddr,m_uint32_t len);

/* Create a memory alias */
struct vdevice *dev_create_ram_alias(vm_instance_t *vm,char *name,char *orig,
                                     m_uint64_t paddr,m_uint32_t len);

/* Create a dummy console */
int dev_create_dummy_console(vm_instance_t *vm);

/* Initialized a zeroed memory zone */
int dev_zero_init(vm_instance_t *vm,char *name,
                  m_uint64_t paddr,m_uint32_t len);

/* Initialize a RAM zone */
int dev_ram_init(vm_instance_t *vm,char *name,int use_mmap,
                 m_uint64_t paddr,m_uint32_t len);

/* Initialize a ROM zone */
int dev_rom_init(vm_instance_t *vm,char *name,m_uint64_t paddr,m_uint32_t len);

/* Create the NVRAM device */
int dev_nvram_init(vm_instance_t *vm,char *name,
                   m_uint64_t paddr,m_uint32_t len,
                   u_int *conf_reg);

/* Compute NVRAM checksum */
m_uint16_t nvram_cksum(vm_instance_t *vm,m_uint64_t addr,size_t count);

/* Create a 8 Mb bootflash */
int dev_bootflash_init(vm_instance_t *vm,char *name,
                       m_uint64_t paddr,m_uint32_t len);

/* Create a PLX9060 device */
vm_obj_t *dev_plx9060_init(vm_instance_t *vm,char *name,
                           struct pci_bus *pci_bus,int pci_device,
                           struct vdevice *dev);

/* Create a new GT64010 controller */
int dev_gt64010_init(vm_instance_t *vm,char *name,
                     m_uint64_t paddr,m_uint32_t len,u_int irq);

/* Create a new GT64120 controller */
int dev_gt64120_init(vm_instance_t *vm,char *name,
                     m_uint64_t paddr,m_uint32_t len,u_int irq);

/* dev_dec21050_init() */
int dev_dec21050_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus);

/* dev_dec21052_init() */
int dev_dec21052_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus);

/* dev_dec21150_init() */
int dev_dec21150_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus);

/* dev_dec21152_init() */
int dev_dec21152_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus);

/* dev_pericom_init() */
int dev_pericom_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus);

/* Create an AP1011 Sturgeon HyperTransport-PCI Bridge */
int dev_ap1011_init(struct pci_bus *pci_bus,int pci_device,
                    struct pci_bus *sec_bus);

/* dev_clpd6729_init() */
int dev_clpd6729_init(vm_instance_t *vm,
                      struct pci_bus *pci_bus,int pci_device,
                      struct pci_io_data *pci_io_data,
                      m_uint32_t io_start,m_uint32_t io_end);

/* Create a NS16552 device */
int dev_ns16552_init(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t len,
                     u_int reg_div,u_int irq,vtty_t *vtty_A,vtty_t *vtty_B);

/* Initialize an SRAM device */
int dev_c7200_sram_init(vm_instance_t *vm,char *name,
                        m_uint64_t paddr,m_uint32_t len,
                        struct pci_bus *pci_bus,int pci_device);

/* Initialize a PCMCIA disk */
vm_obj_t *dev_pcmcia_disk_init(vm_instance_t *vm,char *name,
                               m_uint64_t paddr,m_uint32_t len,
                               u_int disk_size);

/* Create SB-1 system control devices */
int dev_sb1_init(vm_instance_t *vm);

/* Create SB-1 I/O devices */
int dev_sb1_io_init(vm_instance_t *vm,u_int duart_irq);

/* Create the SB-1 PCI bus configuration zone */
int dev_sb1_pci_init(vm_instance_t *vm,char *name,m_uint64_t paddr);

/* dev_sb1_duart_init() */
int dev_sb1_duart_init(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t len);

/* remote control device */
int dev_remote_control_init(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t len);

#endif
