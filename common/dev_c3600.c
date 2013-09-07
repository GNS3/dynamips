/*
 * Cisco 3600 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 * Patched by Jeremy Grossmann for the GNS3 project (www.gns3.net)
 *
 * Generic Cisco 3600 routines and definitions (EEPROM,...).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#include "cpu.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "pci_io.h"
#include "dev_gt.h"
#include "cisco_eeprom.h"
#include "dev_rom.h"
#include "dev_c3600.h"
#include "dev_c3600_iofpga.h"
#include "dev_c3600_bay.h"
#include "dev_vtty.h"
#include "registry.h"
#include "fs_nvram.h"

/* ======================================================================== */
/* EEPROM definitions                                                       */
/* ======================================================================== */

/* Cisco 3620 mainboard EEPROM */
static m_uint16_t eeprom_c3620_mainboard_data[64] = {
   0x0001, 0x0000, 0x0000, 0x0000, 0x0AFF, 0x7318, 0x5011, 0x0020,
   0xFF10, 0x45C5, 0xA0FF, 0x9904, 0x19FF, 0xFFFF, 0xFFFF, 0x0002,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

struct cisco_eeprom eeprom_c3620_mainboard = {
   "C3620 Mainboard", 
   eeprom_c3620_mainboard_data,
   sizeof(eeprom_c3620_mainboard_data)/2,
};

/* Cisco 3640 mainboard EEPROM */
static m_uint16_t eeprom_c3640_mainboard_data[64] = {
   0x0001, 0x0000, 0x0000, 0x0000, 0x0AFF, 0x7316, 0x8514, 0x0040,
   0xFF10, 0x45C5, 0xA1FF, 0x0102, 0x22FF, 0xFFFF, 0xFFFF, 0x0002,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

struct cisco_eeprom eeprom_c3640_mainboard = {
   "C3640 Mainboard", 
   eeprom_c3640_mainboard_data,
   sizeof(eeprom_c3640_mainboard_data)/2,
};

/* Cisco 3660 backplane EEPROM */
static m_uint16_t eeprom_c3660_backplane_data[64] = {
   0x04FF, 0x4000, 0xC841, 0x0100, 0xC046, 0x0320, 0x0012, 0x8402,
   0x4243, 0x3080, 0x0000, 0x0000, 0x0202, 0xC18B, 0x4841, 0x4430,
   0x3434, 0x3431, 0x3135, 0x4A03, 0x0081, 0x0000, 0x0000, 0x0400,
   0xC28B, 0x4654, 0x5830, 0x3934, 0x3557, 0x304D, 0x59C3, 0x0600,
   0x044D, 0x0EC2, 0xD043, 0x0070, 0xC408, 0x0000, 0x0000, 0x0000,
   0x0000, 0x851C, 0x0A5B, 0x0201, 0x06FF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

struct cisco_eeprom eeprom_c3660_backplane = {
   "C3660 Backplane", 
   eeprom_c3660_backplane_data,
   sizeof(eeprom_c3660_backplane_data)/2,
};

/* ======================================================================== */
/* Chassis Drivers                                                          */
/* ======================================================================== */
static int c3620_init(c3600_t *router);
static int c3640_init(c3600_t *router);
static int c3660_init(c3600_t *router);

static struct c3600_chassis_driver chassis_drivers[] = {
   { "3620"  , 3620, 1, c3620_init, &eeprom_c3620_mainboard },
   { "3640"  , 3640, 1, c3640_init, &eeprom_c3640_mainboard },
   { "3660"  , 3660, 1, c3660_init, &eeprom_c3660_backplane },
   { NULL    , -1,   0, NULL,       NULL },
};

/* ======================================================================== */
/* Network Module Drivers                                                   */
/* ======================================================================== */
static struct cisco_card_driver *nm_drivers[] = {
   &dev_c3600_nm_1e_driver,
   &dev_c3600_nm_4e_driver,
   &dev_c3600_nm_1fe_tx_driver,
   &dev_c3600_nm_4t_driver,
   &dev_c3600_leopard_2fe_driver,
   &dev_c3600_nm_16esw_driver,
   NULL,
};

/* ======================================================================== */
/* Cisco 3600 router instances                                              */
/* ======================================================================== */

/* Initialize default parameters for a C3600 */
static void c3600_init_defaults(c3600_t *router);

/* Directly extract the configuration from the NVRAM device */
static int c3600_nvram_extract_config(vm_instance_t *vm,u_char **startup_config,size_t *startup_len,u_char **private_config,size_t *private_len)
{
   int ret;

   ret = generic_nvram_extract_config(vm, "nvram", vm->nvram_rom_space, 0, 0, FS_NVRAM_FORMAT_DEFAULT, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Directly push the IOS configuration to the NVRAM device */
static int c3600_nvram_push_config(vm_instance_t *vm,u_char *startup_config,size_t startup_len,u_char *private_config,size_t private_len)
{
   int ret;

   ret = generic_nvram_push_config(vm, "nvram", vm->nvram_size*1024, vm->nvram_rom_space, 0, 0, FS_NVRAM_FORMAT_DEFAULT, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Create a new router instance */
static int c3600_create_instance(vm_instance_t *vm)
{
   c3600_t *router;

   if (!(router = malloc(sizeof(*router)))) {
      fprintf(stderr,"C3600 '%s': Unable to create new instance!\n",vm->name);
      return(-1);
   }

   memset(router,0,sizeof(*router));
   router->vm = vm;
   vm->hw_data = router;

   c3600_init_defaults(router);
   return(0);
}

/* Free resources used by a router instance */
static int c3600_delete_instance(vm_instance_t *vm)
{
   c3600_t *router = VM_C3600(vm);
   int i;

   /* Stop all CPUs */
   if (vm->cpu_group != NULL) {
      vm_stop(vm);
      
      if (cpu_group_sync_state(vm->cpu_group) == -1) {
         vm_error(vm,"unable to sync with system CPUs.\n");
         return(FALSE);
      }
   }

   /* Remove NIO bindings */
   for(i=0;i<vm->nr_slots;i++)
      vm_slot_remove_all_nio_bindings(vm,i);

   /* Shutdown all Network Modules */
   vm_slot_shutdown_all(vm);

   /* Free mainboard EEPROM */
   cisco_eeprom_free(&router->mb_eeprom);

   /* Free all resources used by VM */
   vm_free(vm);

   /* Free the router structure */
   free(router);
   return(TRUE);
}

/* Save configuration of a C3600 instance */
static void c3600_save_config(vm_instance_t *vm,FILE *fd)
{
   c3600_t *router = VM_C3600(vm);

   fprintf(fd,"c3600 set_chassis %s %s\n\n",
           vm->name,router->chassis_driver->chassis_type);
}

/* Set EEPROM for the specified slot */
int c3600_set_slot_eeprom(c3600_t *router,u_int slot,
                          struct cisco_eeprom *eeprom)
{
   if (slot >= C3600_MAX_NM_BAYS)
      return(-1);

   router->c3660_nm_eeprom_group[slot].eeprom[0] = eeprom;
   return(0);
}

/* Get slot/port corresponding to specified network IRQ */
static inline void 
c3600_net_irq_get_slot_port(u_int irq,u_int *slot,u_int *port)
{
   irq -= C3600_NETIO_IRQ_BASE;
   *port = irq & C3600_NETIO_IRQ_PORT_MASK;
   *slot = irq >> C3600_NETIO_IRQ_PORT_BITS;
}

/* Get network IRQ for specified slot/port */
u_int c3600_net_irq_for_slot_port(u_int slot,u_int port)
{
   u_int irq;

   irq = (slot << C3600_NETIO_IRQ_PORT_BITS) + port;
   irq += C3600_NETIO_IRQ_BASE;

   return(irq);
}

/* Get a chassis driver */
struct c3600_chassis_driver *c3600_chassis_get_driver(char *chassis_type)
{
   int i;

   for(i=0;chassis_drivers[i].chassis_type;i++)
      if (!strcmp(chassis_drivers[i].chassis_type,chassis_type))
         return(&chassis_drivers[i]);

   return NULL;
}

/* Set the system id or processor board id in the eeprom */
int c3600_set_system_id(c3600_t *router,char *id)
{
  /* 11 characters is enough. Array is 20 long */
  strncpy(router->board_id,id,13);
  /* Make sure it is null terminated */
  router->board_id[13] = 0x00;
  c3600_refresh_systemid(router);
  return 0;
}
int c3600_refresh_systemid(c3600_t *router)
{
  if (router->board_id[0] == 0x00) return(0);
  m_uint8_t buf[11];

  if (!strcmp("3660",router->chassis_driver->chassis_type)) {
    parse_board_id(buf,router->board_id,11);
    cisco_eeprom_set_region(&router->mb_eeprom ,50,buf,11);
  } else {
    parse_board_id(buf,router->board_id,4);
    cisco_eeprom_set_region(&router->mb_eeprom ,16,buf,4);
  }
  return (0);
}

/* Set the base MAC address of the chassis */
static int c3600_burn_mac_addr(c3600_t *router,n_eth_addr_t *addr)
{
   m_uint8_t eeprom_ver;
   size_t offset;

   /* Read EEPROM format version */
   cisco_eeprom_get_byte(&router->mb_eeprom,0,&eeprom_ver);

   switch(eeprom_ver) {
      case 0:
         cisco_eeprom_set_region(&router->mb_eeprom,2,addr->eth_addr_byte,6);
         break;

      case 4:
         if (!cisco_eeprom_v4_find_field(&router->mb_eeprom,0xC3,&offset)) {
            cisco_eeprom_set_region(&router->mb_eeprom,offset,
                                    addr->eth_addr_byte,6);
         }
         break;

      default:
         vm_error(router->vm,"c3600_burn_mac_addr: unable to handle "
                  "EEPROM version %u\n",eeprom_ver);
         return(-1);
   }

   return(0);
}

/* Set chassis MAC address */
int c3600_chassis_set_mac_addr(c3600_t *router,char *mac_addr)
{
   if (parse_mac_addr(&router->mac_addr,mac_addr) == -1) {
      vm_error(router->vm,"unable to parse MAC address '%s'.\n",mac_addr);
      return(-1);
   }

   /* Set the chassis base MAC address */
   c3600_burn_mac_addr(router,&router->mac_addr);
   return(0);
}

/* Set the chassis type */
int c3600_chassis_set_type(c3600_t *router,char *chassis_type)
{  
   struct c3600_chassis_driver *driver;

   if (router->vm->status == VM_STATUS_RUNNING) {
      vm_error(router->vm,"unable to change chassis type when online.\n");
      return(-1);
   }

   if (!(driver = c3600_chassis_get_driver(chassis_type))) {
      vm_error(router->vm,"unknown chassis type '%s'.\n",chassis_type);
      return(-1);
   }

   router->chassis_driver = driver;

   /* Copy the mainboard EEPROM */
   if (cisco_eeprom_copy(&router->mb_eeprom,driver->eeprom) == -1) {
      vm_error(router->vm,"unable to set chassis EEPROM '%s'.\n",chassis_type);
      return(-1);
   }

   /* Set the chassis base MAC address */
   c3600_burn_mac_addr(router,&router->mac_addr);

   /* The motherboard has 2 integrated FastEthernet ports on a 3660 */
   if (driver->chassis_id == 3660) {
      vm_slot_remove_binding(router->vm,0,0);
      vm_slot_add_binding(router->vm,"Leopard-2FE",0,0);
   }
   c3600_refresh_systemid(router);
   return(0);
}

/* Get the chassis ID */
int c3600_chassis_get_id(c3600_t *router)
{
   if (router->chassis_driver)
      return(router->chassis_driver->chassis_id);

   return(-1);
}

/* Show the list of available chassis drivers */
static void c3600_chassis_show_drivers(void)
{
   int i;

   printf("Available C3600 chassis drivers:\n");

   for(i=0;chassis_drivers[i].chassis_type;i++) {
      printf("  * %s %s\n",
             chassis_drivers[i].chassis_type,
             !chassis_drivers[i].supported ? "(NOT WORKING)" : "");
   }
   
   printf("\n");
}

/* Create the main PCI bus for a GT64010 based system */
static int c3600_init_gt64010(c3600_t *router)
{
   if (!(router->vm->pci_bus[0] = pci_bus_create("PCI bus",0))) {
      vm_error(router->vm,"unable to create PCI data.\n");
      return(-1);
   }
   
   return(dev_gt64010_init(router->vm,"gt64010",C3600_GT64K_ADDR,0x1000,
                           C3600_GT64K_IRQ));
}

/* Create the two main PCI busses for a GT64120 based system */
static int c3600_init_gt64120(c3600_t *router)
{
   vm_instance_t *vm = router->vm;

   vm->pci_bus[0] = pci_bus_create("PCI bus #0",0);
   vm->pci_bus[1] = pci_bus_create("PCI bus #1",0);

   if (!vm->pci_bus[0] || !vm->pci_bus[1]) {
      vm_error(router->vm,"unable to create PCI data.\n");
      return(-1);
   }
   
   return(dev_gt64120_init(vm,"gt64120",C3600_GT64K_ADDR,0x1000,
                           C3600_GT64K_IRQ));
}

/* Initialize a Cisco 3620 */
static int c3620_init(c3600_t *router)
{
   vm_instance_t *vm = router->vm;
   int i;

   /* Set the processor type: R4700 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R4700);

   /* Initialize the Galileo GT-64010 PCI controller */
   if (c3600_init_gt64010(router) == -1)
      return(-1);

   /* Initialize PCI map (no PCI bridge for this chassis) */
   for(i=0;i<C3600_MAX_NM_BAYS;i++)
      vm->slots_pci_bus[i] = vm->pci_bus[0];

   vm->elf_machine_id = C3620_ELF_MACHINE_ID;
   return(0);
}

/* Initialize a Cisco 3640 */
static int c3640_init(c3600_t *router)
{
   vm_instance_t *vm = router->vm;
   struct nm_bay_info *bay;
   int i;

   /* Set the processor type: R4700 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R4700);

   /* Initialize the Galileo GT-64010 PCI controller */
   if (c3600_init_gt64010(router) == -1)
      return(-1);

   /* Create the NM PCI busses */
   vm->pci_bus_pool[0] = pci_bus_create("NM Slots 0,2",-1);
   vm->pci_bus_pool[1] = pci_bus_create("NM Slots 1,3",-1);

   /* Initialize PCI map and PCI bridges */
   for(i=0;i<=3;i++) {
      bay = c3600_nm_get_bay_info(3640,i);

      /* Map the NM PCI bus */
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i & 1];

      if (bay && (bay->pci_bridge_device != -1))
         dev_dec21052_init(vm->pci_bus[0],bay->pci_bridge_device,
                           vm->slots_pci_bus[i]);
   }

   vm->elf_machine_id = C3640_ELF_MACHINE_ID;
   return(0);
}

/* Initialize a Cisco 3660 */
static int c3660_init(c3600_t *router)
{   
   vm_instance_t *vm = router->vm;
   struct nm_bay_info *bay;
   char bus_name[128];
   int i;

   /* Set the processor type: R5271 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R527x);

   /* Initialize the Galileo GT-64120 PCI controller */
   if (c3600_init_gt64120(router) == -1)
      return(-1);

   /* Create the NM PCI busses */
   for(i=1;i<=6;i++) {
      snprintf(bus_name,sizeof(bus_name),"NM Slot %d",i);
      vm->pci_bus_pool[i] = pci_bus_create(bus_name,-1);
   }

   /* Slot 0 is mapped to the first bus of GT64120 */
   vm->slots_pci_bus[0] = vm->pci_bus[0];

   /* Initialize PCI map and PCI bridges */
   for(i=1;i<C3600_MAX_NM_BAYS;i++) {
      bay = c3600_nm_get_bay_info(3660,i);

      /* Map the NM PCI bus */
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i];

      /* Slots 1-6 are mapped to the second bus of GT64120 */
      if (bay && (bay->pci_bridge_device != -1))
         dev_dec21152_init(vm->pci_bus[1],bay->pci_bridge_device,
                           vm->slots_pci_bus[i]);
   }

   vm->elf_machine_id = C3660_ELF_MACHINE_ID;
   return(0);
}

/* Show C3600 hardware info */
void c3600_show_hardware(c3600_t *router)
{
   vm_instance_t *vm = router->vm;

   printf("C3600 instance '%s' (id %d):\n",vm->name,vm->instance_id);

   printf("  VM Status  : %d\n",vm->status);
   printf("  RAM size   : %u Mb\n",vm->ram_size);
   printf("  NVRAM size : %u Kb\n",vm->nvram_size);
   printf("  Chassis    : %s\n",router->chassis_driver->chassis_type);
   printf("  IOS image  : %s\n\n",vm->ios_image);

   if (vm->debug_level > 0) {
      dev_show_list(vm);
      pci_dev_show_list(vm->pci_bus[0]);
      pci_dev_show_list(vm->pci_bus[1]);
      printf("\n");
   }
}

/* Initialize default parameters for a C3600 */
static void c3600_init_defaults(c3600_t *router)
{   
   vm_instance_t *vm = router->vm;   
   n_eth_addr_t *m;
   m_uint16_t pid;

   /* Set platform slots characteristics */
   vm->nr_slots   = C3600_MAX_NM_BAYS;
   vm->slots_type = CISCO_CARD_TYPE_NM;
   vm->slots_drivers = nm_drivers;

   pid = (m_uint16_t)getpid();

   /* Generate a chassis MAC address based on the instance ID */
   m = &router->mac_addr;
   m->eth_addr_byte[0] = vm_get_mac_addr_msb(vm);
   m->eth_addr_byte[1] = vm->instance_id & 0xFF;
   m->eth_addr_byte[2] = pid >> 8;
   m->eth_addr_byte[3] = pid & 0xFF;
   m->eth_addr_byte[4] = 0x00;
   m->eth_addr_byte[5] = 0x00;
   router->board_id[0] = 0x00;

   c3600_init_eeprom_groups(router);
   c3600_chassis_set_type(router,C3600_DEFAULT_CHASSIS);

   vm->ram_mmap          = C3600_DEFAULT_RAM_MMAP;
   vm->ram_size          = C3600_DEFAULT_RAM_SIZE;
   vm->rom_size          = C3600_DEFAULT_ROM_SIZE;
   vm->nvram_size        = C3600_DEFAULT_NVRAM_SIZE;
   vm->conf_reg_setup    = C3600_DEFAULT_CONF_REG;
   vm->clock_divisor     = C3600_DEFAULT_CLOCK_DIV;
   vm->nvram_rom_space   = C3600_NVRAM_ROM_RES_SIZE;
   vm->nm_iomem_size     = C3600_DEFAULT_IOMEM_SIZE;

   vm->pcmcia_disk_size[0] = C3600_DEFAULT_DISK0_SIZE;
   vm->pcmcia_disk_size[1] = C3600_DEFAULT_DISK1_SIZE;
}

/* Initialize the C3600 Platform */
static int c3600_init_platform(c3600_t *router)
{
   vm_instance_t *vm = router->vm;
   cpu_mips_t *cpu;
   cpu_gen_t *gen;
   vm_obj_t *obj;

   /* Copy config register setup into "active" config register */
   vm->conf_reg = vm->conf_reg_setup;

   /* Create Console and AUX ports */
   vm_init_vtty(vm);

   /* Create a CPU group */
   vm->cpu_group = cpu_group_create("System CPU");

   /* Initialize the virtual MIPS processor */
   if (!(gen = cpu_create(vm,CPU_TYPE_MIPS64,0))) {
      vm_error(vm,"unable to create CPU!\n");
      return(-1);
   }

   cpu = CPU_MIPS64(gen);

   /* Add this CPU to the system CPU group */
   cpu_group_add(vm->cpu_group,gen);
   vm->boot_cpu = gen;

   /* Initialize the IRQ routing vectors */
   vm->set_irq = mips64_vm_set_irq;
   vm->clear_irq = mips64_vm_clear_irq;

   /* Mark the Network IO interrupt as high priority */
   cpu->irq_idle_preempt[C3600_NETIO_IRQ] = TRUE;
   cpu->irq_idle_preempt[C3600_GT64K_IRQ] = TRUE;
   cpu->irq_idle_preempt[C3600_DUART_IRQ] = TRUE;

   /* Copy some parameters from VM to CPU (idle PC, ...) */
   cpu->idle_pc = vm->idle_pc;

   if (vm->timer_irq_check_itv)
      cpu->timer_irq_check_itv = vm->timer_irq_check_itv;

   /* Get chassis specific driver */
   if (!router->chassis_driver) {
      vm_error(vm,"no chassis defined.\n");
      return(-1);
   }

   /* Remote emulator control */
   dev_remote_control_init(vm,0x16000000,0x1000);

   /* Bootflash (8 Mb) */
   dev_bootflash_init(vm,"bootflash","c3600-bootflash-8mb",
                      C3600_BOOTFLASH_ADDR);

   /* NVRAM and calendar */
   dev_nvram_init(vm,"nvram",
                  C3600_NVRAM_ADDR,vm->nvram_size*1024,&vm->conf_reg);

   /* Bit-bucket zone */
   dev_zero_init(vm,"zero",C3600_BITBUCKET_ADDR,0xc00000);

   /* IO FPGA */
   if (dev_c3600_iofpga_init(router,C3600_IOFPGA_ADDR,0x40000) == -1)
      return(-1);

   if (!(obj = vm_object_find(router->vm,"io_fpga")))
      return(-1);

   router->iofpga_data = obj->data;

   /* PCI IO space */
   if (!(vm->pci_io_space = pci_io_data_init(vm,C3600_PCI_IO_ADDR)))
      return(-1);

   /* Initialize the chassis */
   if (router->chassis_driver->chassis_init(router) == -1)
      return(-1);

   /* Initialize RAM */
   vm_ram_init(vm,0x00000000ULL);

   /* Initialize ROM */
   if (!vm->rom_filename) {
      /* use embedded ROM */
      dev_rom_init(vm,"rom",C3600_ROM_ADDR,vm->rom_size*1048576,
                   mips64_microcode,mips64_microcode_len);
   } else {
      /* use alternate ROM */
      dev_ram_init(vm,"rom",TRUE,TRUE,NULL,FALSE,
                   C3600_ROM_ADDR,vm->rom_size*1048576);
   }

   /* Initialize the NS16552 DUART */
   dev_ns16552_init(vm,C3600_DUART_ADDR,0x1000,3,C3600_DUART_IRQ,
                    vm->vtty_con,vm->vtty_aux);

   /* Cirrus Logic PD6729 (PCI-to-PCMCIA host adapter) */
   dev_clpd6729_init(vm,vm->pci_bus[0],20,vm->pci_io_space,0x4402,0x4403);

   /* Initialize Network Modules */
   if (vm_slot_init_all(vm) == -1)
      return(-1);

   /* Show device list */
   c3600_show_hardware(router);
   return(0);
}

/* Boot the IOS image */
static int c3600_boot_ios(c3600_t *router)
{   
   vm_instance_t *vm = router->vm;
   cpu_mips_t *cpu;

   if (!vm->boot_cpu)
      return(-1);

   /* Suspend CPU activity since we will restart directly from ROM */
   vm_suspend(vm);

   /* Check that CPU activity is really suspended */
   if (cpu_group_sync_state(vm->cpu_group) == -1) {
      vm_error(vm,"unable to sync with system CPUs.\n");
      return(-1);
   }

   /* Reset the boot CPU */
   cpu = CPU_MIPS64(vm->boot_cpu);
   mips64_reset(cpu);

   /* Load IOS image */
   if (mips64_load_elf_image(cpu,vm->ios_image,
                             (vm->ghost_status == VM_GHOST_RAM_USE),
                             &vm->ios_entry_point) < 0) 
   {
      vm_error(vm,"failed to load Cisco IOS image '%s'.\n",vm->ios_image);
      return(-1);
   }

   /* Launch the simulation */
   printf("\nC3600 '%s': starting simulation (CPU0 PC=0x%llx), "
          "JIT %sabled.\n",
          vm->name,cpu->pc,vm->jit_use ? "en":"dis");

   vm_log(vm,"C3600_BOOT",
          "starting instance (CPU0 PC=0x%llx,idle_pc=0x%llx,JIT %s)\n",
          cpu->pc,cpu->idle_pc,vm->jit_use ? "on":"off");

   /* Start main CPU */
   if (vm->ghost_status != VM_GHOST_RAM_GENERATE) {
      vm->status = VM_STATUS_RUNNING;
      cpu_start(vm->boot_cpu);
   } else {
      vm->status = VM_STATUS_SHUTDOWN;
   }
   return(0);
}

/* Set an IRQ */
static void c3600_set_irq(vm_instance_t *vm,u_int irq)
{
   c3600_t *router = VM_C3600(vm);
   cpu_mips_t *cpu0 = CPU_MIPS64(vm->boot_cpu);
   u_int slot,port;

   switch(irq) {
      case 0 ... 7:
         mips64_set_irq(cpu0,irq);

         if (cpu0->irq_idle_preempt[irq])
            cpu_idle_break_wait(cpu0->gen);
         break;

      case C3600_NETIO_IRQ_BASE ... C3600_NETIO_IRQ_END:
         c3600_net_irq_get_slot_port(irq,&slot,&port);
         dev_c3600_iofpga_net_set_irq(router->iofpga_data,slot,port);
         break;
   }
}

/* Clear an IRQ */
static void c3600_clear_irq(vm_instance_t *vm,u_int irq)
{
   c3600_t *router = VM_C3600(vm);
   cpu_mips_t *cpu0 = CPU_MIPS64(vm->boot_cpu);
   u_int slot,port;

   switch(irq) {
      case 0 ... 7:
         mips64_clear_irq(cpu0,irq);
         break;

      case C3600_NETIO_IRQ_BASE ... C3600_NETIO_IRQ_END:
         c3600_net_irq_get_slot_port(irq,&slot,&port);
         dev_c3600_iofpga_net_clear_irq(router->iofpga_data,slot,port);
         break;
   }
}

/* Initialize a Cisco 3600 instance */
static int c3600_init_instance(vm_instance_t *vm)
{   
   c3600_t *router = VM_C3600(vm);
   m_uint32_t rom_entry_point;
   cpu_mips_t *cpu0;

   if (!vm->ios_image) {
      vm_error(vm,"no Cisco IOS image defined.");
      return(-1);
   }

   /* Initialize the C3600 platform */
   if (c3600_init_platform(router) == -1) {
      vm_error(vm,"unable to initialize the platform hardware.\n");
      return(-1);
   }

   /* IRQ routing */
   vm->set_irq = c3600_set_irq;
   vm->clear_irq = c3600_clear_irq;

   /* Load IOS configuration files */
   if (vm->ios_startup_config != NULL || vm->ios_private_config != NULL) {
      vm_nvram_push_config(vm,vm->ios_startup_config,vm->ios_private_config);
      vm->conf_reg &= ~0x40;
   }

   /* Load ROM (ELF image or embedded) */
   cpu0 = CPU_MIPS64(vm->boot_cpu);
   rom_entry_point = (m_uint32_t)MIPS_ROM_PC;

   if ((vm->rom_filename != NULL) &&
       (mips64_load_elf_image(cpu0,vm->rom_filename,0,&rom_entry_point) < 0))
   {
      vm_error(vm,"unable to load alternate ROM '%s', "
               "fallback to embedded ROM.\n\n",vm->rom_filename);
      vm->rom_filename = NULL;
   }

   /* Load symbol file */
   if (vm->sym_filename) {
      mips64_sym_load_file(cpu0,vm->sym_filename);
      cpu0->sym_trace = 1;
   }

   return(c3600_boot_ios(router));
}

/* Stop a Cisco 3600 instance */
static int c3600_stop_instance(vm_instance_t *vm)
{
   printf("\nC3600 '%s': stopping simulation.\n",vm->name);
   vm_log(vm,"C3600_STOP","stopping simulation.\n");

   /* Stop all CPUs */
   if (vm->cpu_group != NULL) {
      vm_stop(vm);
      
      if (cpu_group_sync_state(vm->cpu_group) == -1) {
         vm_error(vm,"unable to sync with system CPUs.\n");
         return(-1);
      }
   }

   /* Free resources that were used during execution to emulate hardware */
   vm_slot_shutdown_all(vm);
   vm_hardware_shutdown(vm);
   return(0);
}

static m_uint16_t c3660_oir_masks[C3600_MAX_NM_BAYS] = {
   0x0000,
   0x0900,  /* slot 1 */
   0x0009,  /* slot 2 */
   0x0A00,  /* slot 3 */
   0x000A,  /* slot 4 */
   0x0C00,  /* slot 5 */
   0x000C,  /* slot 6 */
};

/* Trigger an OIR event (3660 only) */
static int c3600_trigger_oir_event(c3600_t *router,u_int nm_bay)
{
   if (nm_bay >= C3600_MAX_NM_BAYS)
      return(-1);

   router->oir_status = c3660_oir_masks[nm_bay];
   vm_set_irq(router->vm,C3600_EXT_IRQ);
   return(0);
}

/* Initialize a new NM while the virtual router is online (OIR) */
static int c3600_nm_init_online(vm_instance_t *vm,u_int slot,u_int subslot)
{
   if (!slot) {
      vm_error(vm,"OIR not supported on slot 0.\n");
      return(-1);
   }

   /* 
    * Suspend CPU activity while adding new hardware (since we change the
    * memory maps).
    */
   vm_suspend(vm);

   /* Check that CPU activity is really suspended */
   if (cpu_group_sync_state(vm->cpu_group) == -1) {
      vm_error(vm,"unable to sync with system CPUs.\n");
      return(-1);
   }

   /* Add the new hardware elements */
   if (vm_slot_init(vm,slot) == -1)
      return(-1);

   /* Resume normal operations */
   vm_resume(vm);

   /* Now, we can safely trigger the OIR event */
   c3600_trigger_oir_event(VM_C3600(vm),slot);
   return(0);
}

/* Stop a NM while the virtual router is online (OIR) */
static int c3600_nm_stop_online(vm_instance_t *vm,u_int slot,u_int subslot)
{   
   if (!slot) {
      vm_error(vm,"OIR not supported on slot 0.\n");
      return(-1);
   }

   /* The NM driver must be initialized */
   if (!vm_slot_get_card_ptr(vm,slot)) {
      vm_error(vm,"trying to shut down empty slot %u.\n",slot);
      return(-1);
   }

   /* Disable all NIOs to stop traffic forwarding */
   vm_slot_disable_all_nio(vm,slot);

   /* We can safely trigger the OIR event */
   c3600_trigger_oir_event(VM_C3600(vm),slot);

   /* 
    * Suspend CPU activity while removing the hardware (since we change the
    * memory maps).
    */
   vm_suspend(vm);

   /* Device removal */
   if (vm_slot_shutdown(vm,slot) != 0)
      vm_error(vm,"unable to shutdown slot %u.\n",slot);

   /* Resume normal operations */
   vm_resume(vm);
   return(0);
}

/* Get MAC address MSB */
static u_int c3600_get_mac_addr_msb(void)
{
   return(0xCC);
}

/* Parse specific options for the Cisco 3600 platform */
static int c3600_cli_parse_options(vm_instance_t *vm,int option)
{
   c3600_t *router = VM_C3600(vm);

   switch(option) {
      /* IO memory reserved for NMs (in percents!) */
      case OPT_IOMEM_SIZE:
         vm->nm_iomem_size = 0x8000 | atoi(optarg);
         break;

      /* Chassis type */
      case 't':
         c3600_chassis_set_type(router,optarg);
         break;

       /* Set the base MAC address */
      case 'm':
         if (!c3600_chassis_set_mac_addr(router,optarg))
            printf("MAC address set to '%s'.\n",optarg);
         break;

      /* Set the System ID */
      case 'I':
         if (!c3600_set_system_id(router,optarg))
            printf("System ID set to '%s'.\n",optarg);
         break;


      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Show specific CLI options */
static void c3600_cli_show_options(vm_instance_t *vm)
{
   printf("  -t <chassis_type>  : Select Chassis type (default: \"%s\")\n"
          "  --iomem-size <val> : IO memory (in percents, default: %u)\n"
          "  -p <nm_desc>       : Define a Network Module\n"
          "  -I <serialno>      : Set Processor Board Serial Number\n"
          "  -s <nm_nio>        : Bind a Network IO interface to a "
          "Network Module\n",
          C3600_DEFAULT_CHASSIS,vm->nm_iomem_size);
}

/* Platform definition */
static vm_platform_t c3600_platform = {
   "c3600", "C3600", "3600",
   c3600_create_instance,
   c3600_delete_instance,
   c3600_init_instance,
   c3600_stop_instance,
   c3600_nm_init_online,
   c3600_nm_stop_online,
   c3600_nvram_extract_config,
   c3600_nvram_push_config,
   c3600_get_mac_addr_msb,
   c3600_save_config,
   c3600_cli_parse_options,
   c3600_cli_show_options,
   c3600_chassis_show_drivers,
};

/* Register the c3600 platform */
int c3600_platform_register(void)
{
   if (vm_platform_register(&c3600_platform) == -1)
      return(-1);

   return(hypervisor_c3600_init(&c3600_platform));
}
