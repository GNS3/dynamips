/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Virtual Machines.
 */

#ifndef __VM_H__
#define __VM_H__

#include <pthread.h>

#include "dynamips.h"
#include "memory.h"
#include "cpu.h"
#include "dev_vtty.h"
#include "cisco_eeprom.h"
#include "cisco_card.h"
#include "rommon_var.h"

#include "gdb_utils.h"

#define VM_PAGE_SHIFT  12
#define VM_PAGE_SIZE   (1 << VM_PAGE_SHIFT)
#define VM_PAGE_IMASK  (VM_PAGE_SIZE - 1)
#define VM_PAGE_MASK   (~(VM_PAGE_IMASK))

/* Number of pages in chunk area */
#define VM_CHUNK_AREA_SIZE  256

/* VM memory chunk */
typedef struct vm_chunk vm_chunk_t;
struct vm_chunk {
   void *area;
   u_int page_alloc,page_total;
   vm_chunk_t *next;
};

/* VM ghost pool entry */
typedef struct vm_ghost_image vm_ghost_image_t;
struct vm_ghost_image {
   char *filename;
   u_int ref_count;
   int fd;
   off_t file_size;
   u_char *area_ptr;
   vm_ghost_image_t *next;
};

/* Maximum number of devices per VM */
#define VM_DEVICE_MAX  (1 << 6)

/* Size of the PCI bus pool */
#define VM_PCI_POOL_SIZE  32

/* VM instance status */
enum {   
   VM_STATUS_HALTED = 0,      /* VM is halted and no HW resources are used */
   VM_STATUS_SHUTDOWN,        /* Shutdown procedure engaged */
   VM_STATUS_RUNNING,         /* VM is running */
   VM_STATUS_SUSPENDED,       /* VM is suspended */
};

/* Ghost RAM status */
enum {
   VM_GHOST_RAM_NONE = 0,
   VM_GHOST_RAM_GENERATE,
   VM_GHOST_RAM_USE,
};

/* Timer IRQ check interval */
#define VM_TIMER_IRQ_CHECK_ITV  1000

/* Max slots per VM */
#define VM_MAX_SLOTS  16

/* forward declarations */
typedef struct vm_obj vm_obj_t;

/* Shutdown function prototype for an object */
typedef void *(*vm_shutdown_t)(vm_instance_t *vm,void *data);

/* VM object, used to keep track of devices and various things */
struct vm_obj {
   char *name;
   void *data;
   struct vm_obj *next,**pprev;
   vm_shutdown_t shutdown;
};

/* VM instance */
struct vm_instance {
   char *name;
   vm_platform_t *platform;       /* Platform specific helpers */
   int status;                    /* Instance status */
   int instance_id;               /* Instance Identifier */
   char *lock_file;               /* Lock file */
   char *log_file;                /* Log filename */
   int log_file_enabled;          /* Logging enabled */
   u_int ram_size,rom_size;       /* RAM and ROM size in Mb */
   u_int ram_res_size;            /* RAM reserved space size */
   u_int iomem_size;              /* IOMEM size in Mb */
   u_int nvram_size;              /* NVRAM size in Kb */
   u_int pcmcia_disk_size[2];     /* PCMCIA disk0 and disk1 sizes (in Mb) */
   u_int conf_reg,conf_reg_setup; /* Config register */
   u_int clock_divisor;           /* Clock Divisor (see cp0.c) */
   u_int ram_mmap;                /* Memory-mapped RAM ? */
   u_int restart_ios;             /* Restart IOS on reload ? */
   u_int elf_machine_id;          /* ELF machine identifier */
   u_int exec_area_size;          /* Size of execution area for CPU */
   m_uint32_t ios_entry_point;    /* IOS entry point */
   char *ios_image;               /* IOS image filename */
   char *ios_startup_config;      /* IOS configuration file for startup-config */
   char *ios_private_config;      /* IOS configuration file for private-config */
   char *rom_filename;            /* ROM filename */
   char *sym_filename;            /* Symbol filename */
   FILE *lock_fd,*log_fd;         /* Lock/Log file descriptors */
   int debug_level;               /* Debugging Level */
   int jit_use;                   /* CPUs use JIT */
   int sparse_mem;                /* Use sparse virtual memory */
   u_int nm_iomem_size;           /* IO mem size to be passed to Smart Init */
   
   /* GDB Server variables */
   int gdb_server_running;        /* Indicate current state of the GDB Server */
   int gdb_port;                  /* TCP port for listening incomming GDB client connections. */
   gdb_debug_context_t *gdb_ctx;
   gdb_server_conn_t *gdb_conn;

   /* ROMMON variables */
   struct rommon_var_list rommon_vars;

   /* Memory chunks */
   vm_chunk_t *chunks;

   /* Basic hardware: system CPU, PCI busses and PCI I/O space */
   cpu_group_t *cpu_group;
   cpu_gen_t *boot_cpu;
   struct pci_bus *pci_bus[2];
   struct pci_bus *pci_bus_pool[VM_PCI_POOL_SIZE];
   struct pci_io_data *pci_io_space;

   /* Memory mapped devices */
   struct vdevice *dev_list;
   struct vdevice *dev_array[VM_DEVICE_MAX];

   /* IRQ routing */
   void (*set_irq)(vm_instance_t *vm,u_int irq);
   void (*clear_irq)(vm_instance_t *vm,u_int irq);

   /* Slots for PA/NM/... */
   u_int nr_slots;
   u_int slots_type;
   struct cisco_card *slots[VM_MAX_SLOTS];
   struct cisco_card_driver **slots_drivers;
   struct pci_bus *slots_pci_bus[VM_MAX_SLOTS];

   /* Filename for ghosted RAM */
   char *ghost_ram_filename;
   
   /* Ghost RAM image handling */
   int ghost_status;

   /* Timer IRQ interval check */
   u_int timer_irq_check_itv;

   /* "idling" pointer counter */
   m_uint64_t idle_pc;

   /* JIT block direct jumps */
   int exec_blk_direct_jump;

   /* IRQ idling preemption */
   u_int irq_idle_preempt[256];

   /* Console and AUX port VTTY type and parameters */
   int vtty_con_type,vtty_aux_type;
   int vtty_con_tcp_port,vtty_aux_tcp_port;
   vtty_serial_option_t vtty_con_serial_option,vtty_aux_serial_option;

   /* Virtual TTY for Console and AUX ports */
   vtty_t *vtty_con,*vtty_aux;

   /* Space reserved in NVRAM by ROM monitor */
   u_int nvram_rom_space;

   /* Chassis cookie (for c2600 and maybe other routers) */
   m_uint16_t chassis_cookie[64];

   /* Specific hardware data */
   void *hw_data;

   /* VM objects */
   struct vm_obj *vm_object_list;

};

/* VM Platform definition */
struct vm_platform {
   char *name;
   char *log_name;
   char *cli_name;
   int (*create_instance)(vm_instance_t *vm);
   int (*delete_instance)(vm_instance_t *vm);
   int (*init_instance)(vm_instance_t *vm);
   int (*stop_instance)(vm_instance_t *vm);
   int (*oir_start)(vm_instance_t *vm,u_int slot_id,u_int subslot_id);
   int (*oir_stop)(vm_instance_t *vm,u_int slot_id,u_int subslot_id);
   int (*nvram_extract_config)(vm_instance_t *vm,u_char **startup_config,size_t *startup_len,u_char **private_config,size_t *private_len);
   int (*nvram_push_config)(vm_instance_t *vm,u_char *startup_config,size_t startup_len,u_char *private_config,size_t private_len);
   u_int (*get_mac_addr_msb)(void);
   void (*save_config)(vm_instance_t *vm,FILE *fd);
   int (*cli_parse_options)(vm_instance_t *vm,int option);
   void (*cli_show_options)(vm_instance_t *vm);
   void (*show_spec_drivers)(void);
};

/* VM platform list item */
struct vm_platform_list {
   struct vm_platform_list *next;
   struct vm_platform *platform;
};

extern int vm_file_naming_type;

/* Set an IRQ for a VM */
static inline void vm_set_irq(vm_instance_t *vm,u_int irq)
{
   if (vm->set_irq != NULL)
      vm->set_irq(vm,irq);
}

/* Clear an IRQ for a VM */
static inline void vm_clear_irq(vm_instance_t *vm,u_int irq)
{
   if (vm->clear_irq != NULL)
      vm->clear_irq(vm,irq);
}

/* Initialize a VM object */
void vm_object_init(vm_obj_t *obj);

/* Add a VM object to an instance */
void vm_object_add(vm_instance_t *vm,vm_obj_t *obj);

/* Remove a VM object from an instance */
void vm_object_remove(vm_instance_t *vm,vm_obj_t *obj);

/* Find an object given its name */
vm_obj_t *vm_object_find(vm_instance_t *vm,char *name);

/* Check that a mandatory object is present */
int vm_object_check(vm_instance_t *vm,char *name);

/* Dump the object list of an instance */
void vm_object_dump(vm_instance_t *vm);

/* Get VM type */
char *vm_get_type(vm_instance_t *vm);

/* Get MAC address MSB */
u_int vm_get_mac_addr_msb(vm_instance_t *vm);

/* Generate a filename for use by the instance */
char *vm_build_filename(vm_instance_t *vm,char *name);

/* Get the amount of host virtual memory used by a VM */
size_t vm_get_vspace_size(vm_instance_t *vm);

/* Check that an instance lock file doesn't already exist */
int vm_get_lock(vm_instance_t *vm);

/* Erase lock file */
void vm_release_lock(vm_instance_t *vm,int erase);

/* Log a message */
void vm_flog(vm_instance_t *vm,char *module,char *format,va_list ap);

/* Log a message */
void vm_log(vm_instance_t *vm,char *module,char *format,...);

/* Close the log file */
int vm_close_log(vm_instance_t *vm);

/* Create the log file */
int vm_create_log(vm_instance_t *vm);

/* Reopen the log file */
int vm_reopen_log(vm_instance_t *vm);

/* Error message */
void vm_error(vm_instance_t *vm,char *format,...);

/* Shutdown hardware resources used by a VM */
int vm_hardware_shutdown(vm_instance_t *vm);

/* Free resources used by a VM */
void vm_free(vm_instance_t *vm);

/* Get an instance given a name */
vm_instance_t *vm_acquire(char *name);

/* Release a VM (decrement reference count) */
int vm_release(vm_instance_t *vm);

/* Initialize RAM */
int vm_ram_init(vm_instance_t *vm,m_uint64_t paddr);

/* Initialize VTTY */
int vm_init_vtty(vm_instance_t *vm);

/* Delete VTTY */
void vm_delete_vtty(vm_instance_t *vm);

/* Bind a device to a virtual machine */
int vm_bind_device(vm_instance_t *vm,struct vdevice *dev);

/* Unbind a device from a virtual machine */
int vm_unbind_device(vm_instance_t *vm,struct vdevice *dev);

/* Map a device at the specified physical address */
int vm_map_device(vm_instance_t *vm,struct vdevice *dev,m_uint64_t base_addr);

/* Set an IRQ for a VM */
void vm_set_irq(vm_instance_t *vm,u_int irq);

/* Clear an IRQ for a VM */
void vm_clear_irq(vm_instance_t *vm,u_int irq);

/* Suspend a VM instance */
int vm_suspend(vm_instance_t *vm);

/* Resume a VM instance */
int vm_resume(vm_instance_t *vm);

/* Stop an instance */
int vm_stop(vm_instance_t *vm);

/* Monitor an instance periodically */
void vm_monitor(vm_instance_t *vm);

/* Allocate an host page */
void *vm_alloc_host_page(vm_instance_t *vm);

/* Free an host page */
void vm_free_host_page(vm_instance_t *vm,void *ptr);

/* Get a ghost image */
int vm_ghost_image_get(char *filename,u_char **ptr,int *fd);

/* Release a ghost image */
int vm_ghost_image_release(int fd);

/* Open a VM file and map it in memory */
int vm_mmap_open_file(vm_instance_t *vm,char *name,
                      u_char **ptr,off_t *fsize);

/* Open/Create a VM file and map it in memory */
int vm_mmap_create_file(vm_instance_t *vm,char *name,size_t len,u_char **ptr);

/* Close a memory mapped file */
int vm_mmap_close_file(int fd,u_char *ptr,size_t len);

/* Save the Cisco IOS configuration from NVRAM */
int vm_ios_save_config(vm_instance_t *vm);

/* Set Cisco IOS image to use */
int vm_ios_set_image(vm_instance_t *vm,char *ios_image);

/* Unset a Cisco IOS configuration file */
void vm_ios_unset_config(vm_instance_t *vm);

/* Set Cisco IOS configuration files to use (NULL to keep existing data) */
int vm_ios_set_config(vm_instance_t *vm,const char *startup_filename,const char *private_filename);

/* Extract IOS configuration from NVRAM and write it to a file */
int vm_nvram_extract_config(vm_instance_t *vm,char *filename);

/* Read IOS configuraton from the files and push it to NVRAM (NULL to keep existing data) */
int vm_nvram_push_config(vm_instance_t *vm,const char *startup_filename,const char *private_filename);

/* Save general VM configuration into the specified file */
void vm_save_config(vm_instance_t *vm,FILE *fd);

/* Find a platform */
vm_platform_t *vm_platform_find(char *name);

/* Find a platform given its CLI name */
vm_platform_t *vm_platform_find_cli_name(char *name);

/* Register a platform */
int vm_platform_register(vm_platform_t *platform);

/* Create an instance of the specified type */
vm_instance_t *vm_create_instance(char *name,int instance_id,char *type);

/* Delete a VM instance */
int vm_delete_instance(char *name);

/* Rename a VM instance */
int vm_rename_instance(vm_instance_t *vm, char *name);

/* Initialize a VM instance */
int vm_init_instance(vm_instance_t *vm);

/* Stop a VM instance */
int vm_stop_instance(vm_instance_t *vm);

/* Delete all VM instances */
int vm_delete_all_instances(void);

/* Save all VM configs */
int vm_save_config_all(FILE *fd);

/* OIR to start a slot/subslot */
int vm_oir_start(vm_instance_t *vm,u_int slot,u_int subslot);

/* OIR to stop a slot/subslot */
int vm_oir_stop(vm_instance_t *vm,u_int slot,u_int subslot);

#endif
