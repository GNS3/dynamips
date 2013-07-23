/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <sys/types.h>
#include "utils.h"
#include "cpu.h"
#include "net_io.h"
#include "vm.h"

/* Device Flags */
#define VDEVICE_FLAG_NO_MTS_MMAP  0x01  /* Prevent MMAPed access by MTS */
#define VDEVICE_FLAG_CACHING      0x02  /* Device does support caching */
#define VDEVICE_FLAG_REMAP        0x04  /* Physical address remapping */
#define VDEVICE_FLAG_SYNC         0x08  /* Forced sync */
#define VDEVICE_FLAG_SPARSE       0x10  /* Sparse device */
#define VDEVICE_FLAG_GHOST        0x20  /* Ghost device */

#define VDEVICE_PTE_DIRTY  0x01

typedef void *(*dev_handler_t)(cpu_gen_t *cpu,struct vdevice *dev,
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
   m_iptr_t *sparse_map;
   struct vdevice *next,**pprev;
};

/* PCI part */
#include "pci_dev.h"

/* device access function */
#ifdef MAC64HACK
static void *__dev_access_fast(cpu_gen_t *cpu,u_int dev_id,m_uint32_t offset,
                      u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct vdevice *dev = cpu->vm->dev_array[dev_id];

   if (unlikely(!dev)) {
      cpu_log(cpu,"dev_access_fast","null handler (dev_id=%u,offset=0x%x)\n",
              dev_id,offset);
      return NULL;
   }

#if DEBUG_DEV_PERF_CNT
   cpu->dev_access_counter++;
#endif

   return(dev->handler(cpu,dev,offset,op_size,op_type,data));
}

static forced_inline
void *dev_access_fast(cpu_gen_t *cpu,u_int dev_id,m_uint32_t offset,
		      u_int op_size,u_int op_type,m_uint64_t *data)
{
  asm("sub $8, %rsp");
  void* ret = __dev_access_fast(cpu, dev_id, offset, op_size, op_type, data);
  asm("add $8, %rsp");
  return ret;
}

#else
static forced_inline 
void *dev_access_fast(cpu_gen_t *cpu,u_int dev_id,m_uint32_t offset,
                      u_int op_size,u_int op_type,m_uint64_t *data)
{
   struct vdevice *dev = cpu->vm->dev_array[dev_id];

   if (unlikely(!dev)) {
      cpu_log(cpu,"dev_access_fast","null handler (dev_id=%u,offset=0x%x)\n",
              dev_id,offset);
      return NULL;
   }

#if DEBUG_DEV_PERF_CNT
   cpu->dev_access_counter++;
#endif

   return(dev->handler(cpu,dev,offset,op_size,op_type,data));
}
#endif

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
void *dev_access(cpu_gen_t *cpu,u_int dev_id,m_uint32_t offset,
                 u_int op_size,u_int op_type,m_uint64_t *data);

/* Synchronize memory for a memory-mapped (mmap) device */
int dev_sync(struct vdevice *dev);

/* Remap a device at specified physical address */
struct vdevice *dev_remap(char *name,struct vdevice *orig,
                          m_uint64_t paddr,m_uint32_t len);

/* Create a RAM device */
struct vdevice *dev_create_ram(vm_instance_t *vm,char *name,
                               int sparse,char *filename,
                               m_uint64_t paddr,m_uint32_t len);

/* Create a ghosted RAM device */
struct vdevice *
dev_create_ghost_ram(vm_instance_t *vm,char *name,int sparse,char *filename,
                     m_uint64_t paddr,m_uint32_t len);

/* Create a memory alias */
struct vdevice *dev_create_ram_alias(vm_instance_t *vm,char *name,char *orig,
                                     m_uint64_t paddr,m_uint32_t len);

/* Initialize a sparse device */
int dev_sparse_init(struct vdevice *dev);

/* Shutdown sparse device structures */
int dev_sparse_shutdown(struct vdevice *dev);

/* Get an host address for a sparse device */
m_iptr_t dev_sparse_get_host_addr(vm_instance_t *vm,struct vdevice *dev,
                                  m_uint64_t paddr,u_int op_type,int *cow);

/* Get virtual address space used on host for the specified device */
size_t dev_get_vspace_size(struct vdevice *dev);

/* Create a dummy console */
int dev_create_dummy_console(vm_instance_t *vm);

/* Initialized a zeroed memory zone */
int dev_zero_init(vm_instance_t *vm,char *name,
                  m_uint64_t paddr,m_uint32_t len);

/* Initialized a byte-swap device */
int dev_bswap_init(vm_instance_t *vm,char *name,
                   m_uint64_t paddr,m_uint32_t len,m_uint64_t remap_addr);

/* Initialize a RAM zone */
int dev_ram_init(vm_instance_t *vm,char *name,int use_mmap,int delete_file,
                 char *alternate_name,int sparse,
                 m_uint64_t paddr,m_uint32_t len);

/* Initialize a ghosted RAM zone */
int dev_ram_ghost_init(vm_instance_t *vm,char *name,int sparse,char *filename,
                       m_uint64_t paddr,m_uint32_t len);

/* Create the NVRAM device */
int dev_nvram_init(vm_instance_t *vm,char *name,
                   m_uint64_t paddr,m_uint32_t len,
                   u_int *conf_reg);

/* Create a 8 Mb bootflash */
int dev_bootflash_init(vm_instance_t *vm,char *name,char *model,
                       m_uint64_t paddr);

/* Create a Flash device */
vm_obj_t *dev_flash_init(vm_instance_t *vm,char *name,
                         m_uint64_t paddr,m_uint32_t len);

/* Copy data directly to a flash device */
int dev_flash_copy_data(vm_obj_t *obj,m_uint32_t offset,
                        u_char *ptr,ssize_t len);

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

/* dev_dec21154_init() */
int dev_dec21154_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus);

/* dev_pericom_init() */
int dev_pericom_init(struct pci_bus *pci_bus,int pci_device,
                      struct pci_bus *sec_bus);

/* dev_ti2050b_init() */
int dev_ti2050b_init(struct pci_bus *pci_bus,int pci_device,
                     struct pci_bus *sec_bus);

/* Create an AP1011 Sturgeon HyperTransport-PCI Bridge */
int dev_ap1011_init(struct pci_bus *pci_bus,int pci_device,
                    struct pci_bus *sec_bus);

/* dev_plx6520cb_init() */
int dev_plx6520cb_init(struct pci_bus *pci_bus,int pci_device,
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
                               u_int disk_size,int mode);

/* Get the device associated with a PCMCIA disk object */
struct vdevice *dev_pcmcia_disk_get_device(vm_obj_t *obj);

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

int generic_nvram_extract_config(vm_instance_t *vm, char *dev_name, size_t nvram_offset, size_t nvram_size, m_uint32_t addr, u_int format,
                                 u_char **startup_config, size_t *startup_len, u_char **private_config, size_t *private_len);

int generic_nvram_push_config(vm_instance_t *vm, char *dev_name, size_t file_size, size_t nvram_offset, size_t nvram_size, m_uint32_t addr, u_int format,
                              u_char *startup_config, size_t startup_len, u_char *private_config, size_t private_len);

#endif
