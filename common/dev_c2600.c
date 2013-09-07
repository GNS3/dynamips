/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 * Patched by Jeremy Grossmann for the GNS3 project (www.gns3.net)
 *
 * Generic Cisco 2600 routines and definitions (EEPROM,...).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "ppc32_mem.h"
#include "pci_io.h"
#include "cisco_eeprom.h"
#include "dev_mpc860.h"
#include "dev_rom.h"
#include "dev_c2600.h"
#include "dev_c2600_iofpga.h"
#include "dev_vtty.h"
#include "registry.h"
#include "fs_nvram.h"

/* ======================================================================== */
/* EEPROM definitions                                                       */
/* ======================================================================== */

/* Cisco 2600 mainboard EEPROM */
static m_uint16_t eeprom_c2600_mb_data[] = {
   0x0101, 0x0404, 0x0000, 0x0000, 0x4320, 0x00FF, 0x0091, 0x0020,
   0x0000, 0x0000, 0x0000, 0x0000, 0x4654, 0x5809, 0x4557, 0x304D,
   0x5902, 0x0200, 0x0000, 0x0000, 0x00FF, 0xFFFF, 0x5006, 0x490B,
   0x1709, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

struct c2600_mb_id {
   char *name;
   char *mb_driver;
   m_uint16_t id;
   int xm_model;
   int supported;
};

struct c2600_mb_id c2600_mainboard_id[] = {
   { "2610"   , "CISCO2600-MB-1E"  , 0x0091, FALSE, TRUE  },
   { "2611"   , "CISCO2600-MB-2E"  , 0x0092, FALSE, TRUE  },
   { "2620"   , "CISCO2600-MB-1FE" , 0x0094, FALSE, TRUE  },
   { "2621"   , "CISCO2600-MB-2FE" , 0x00a2, FALSE, TRUE  },
   { "2610XM" , "CISCO2600-MB-1FE" , 0x036a, TRUE,  TRUE  },
   { "2611XM" , "CISCO2600-MB-2FE" , 0x036b, TRUE,  FALSE },
   { "2620XM" , "CISCO2600-MB-1FE" , 0x036c, TRUE,  TRUE  },
   { "2621XM" , "CISCO2600-MB-2FE" , 0x036d, TRUE,  FALSE },
   { "2650XM" , "CISCO2600-MB-1FE" , 0x036e, TRUE,  TRUE  },
   { "2651XM" , "CISCO2600-MB-2FE" , 0x036f, TRUE,  FALSE },
   { NULL     , NULL               , 0x0000, FALSE, FALSE },
};

/* ======================================================================== */
/* Network Module Drivers                                                   */
/* ======================================================================== */
static struct cisco_card_driver *nm_drivers[] = {
   &dev_c2600_mb1e_eth_driver,
   &dev_c2600_mb2e_eth_driver,
   &dev_c2600_mb1fe_eth_driver,
   &dev_c2600_mb2fe_eth_driver,

   &dev_c2600_nm_1e_driver,
   &dev_c2600_nm_4e_driver,
   &dev_c2600_nm_1fe_tx_driver,
   &dev_c2600_nm_16esw_driver,

   &dev_c2600_nm_nam_driver,
   &dev_c2600_nm_cids_driver,

   NULL,
};

/* ======================================================================== */
/* Cisco 2600 router instances                                              */
/* ======================================================================== */

/* Initialize default parameters for a C2600 */
static void c2600_init_defaults(c2600_t *router);

/* Directly extract the configuration from the NVRAM device */
static int c2600_nvram_extract_config(vm_instance_t *vm,u_char **startup_config,size_t *startup_len,u_char **private_config,size_t *private_len)
{
   int ret;

   ret = generic_nvram_extract_config(vm, "nvram", vm->nvram_rom_space*4, 0, 0, FS_NVRAM_FORMAT_SCALE_4, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Directly push the IOS configuration to the NVRAM device */
static int c2600_nvram_push_config(vm_instance_t *vm,u_char *startup_config,size_t startup_len,u_char *private_config,size_t private_len)
{
   int ret;

   ret = generic_nvram_push_config(vm, "nvram", vm->nvram_size*4096, vm->nvram_rom_space*4, 0, 0, FS_NVRAM_FORMAT_SCALE_4, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Check for empty config */
int c2600_nvram_check_empty_config(vm_instance_t *vm)
{
   struct vdevice *dev;
   m_uint64_t addr;
   m_uint32_t len;

   if (!(dev = dev_get_by_name(vm,"nvram")))
      return(-1);

   addr = dev->phys_addr + (vm->nvram_rom_space << 2);
   len  = dev->phys_len - (vm->nvram_rom_space << 2);

   while(len > 0) {
      if (physmem_copy_u32_from_vm(vm,addr) != 0)
         return(0);

      addr += sizeof(m_uint32_t);
      len  -= sizeof(m_uint32_t);
   }

   /* Empty NVRAM */
   vm->conf_reg |= 0x0040;
   printf("NVRAM is empty, setting config register to 0x%x\n",vm->conf_reg);
   return(0);
}

/* Create a new router instance */
static int c2600_create_instance(vm_instance_t *vm)
{
   c2600_t *router;

   if (!(router = malloc(sizeof(*router)))) {
      fprintf(stderr,"C2600 '%s': Unable to create new instance!\n",vm->name);
      return(-1);
   }

   memset(router,0,sizeof(*router));
   router->vm = vm;
   vm->hw_data = router;

   c2600_init_defaults(router);
   return(0);
}

/* Free resources used by a router instance */
static int c2600_delete_instance(vm_instance_t *vm)
{
   c2600_t *router = VM_C2600(vm);
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

/* Save configuration of a C2600 instance */
void c2600_save_config(vm_instance_t *vm,FILE *fd)
{
   c2600_t *router = VM_C2600(vm);

   fprintf(fd,"c2600 set_chassis %s %s\n\n",vm->name,router->mainboard_type);
}

/* Get WIC device address for the specified onboard port */
int c2600_get_onboard_wic_addr(u_int slot,m_uint64_t *phys_addr)
{
   if (slot >= C2600_MAX_WIC_BAYS)
      return(-1);

   *phys_addr = C2600_WIC_ADDR + (slot * C2600_WIC_SIZE);
   return(0);
}

/* Set EEPROM for the specified slot */
int c2600_set_slot_eeprom(c2600_t *router,u_int slot,
                          struct cisco_eeprom *eeprom)
{
   switch(slot) {
      case 1:
         router->nm_eeprom_group.eeprom[0] = eeprom;
         return(0);
      default:
         return(-1);
   }
}

/* Get slot/port corresponding to specified network IRQ */
static inline void 
c2600_net_irq_get_slot_port(u_int irq,u_int *slot,u_int *port)
{
   irq -= C2600_NETIO_IRQ_BASE;
   *port = irq & C2600_NETIO_IRQ_PORT_MASK;
   *slot = irq >> C2600_NETIO_IRQ_PORT_BITS;
}

/* Get network IRQ for specified slot/port */
u_int c2600_net_irq_for_slot_port(u_int slot,u_int port)
{
   u_int irq;

   irq = (slot << C2600_NETIO_IRQ_PORT_BITS) + port;
   irq += C2600_NETIO_IRQ_BASE;

   return(irq);
}

/* Find Cisco 2600 Mainboard info */
static struct c2600_mb_id *c2600_get_mb_info(char *mainboard_type)
{   
   int i;

   for(i=0;c2600_mainboard_id[i].name;i++)
      if (!strcmp(c2600_mainboard_id[i].name,mainboard_type))
         return(&c2600_mainboard_id[i]);

   return NULL;
}

/* Show all available mainboards */
void c2600_mainboard_show_drivers(void)
{
   int i;

   printf("Available C2600 chassis drivers:\n");

   for(i=0;c2600_mainboard_id[i].name;i++)
      printf("  * %s %s\n",
             c2600_mainboard_id[i].name,
             !c2600_mainboard_id[i].supported ? "(NOT WORKING)" : "");

   printf("\n");
}

/* Show the list of available NM drivers */
void c2600_nm_show_drivers(void)
{
   int i;

   printf("Available C2600 Network Module drivers:\n");

   for(i=0;nm_drivers[i];i++) {
      printf("  * %s %s\n",
             nm_drivers[i]->dev_type,
             !nm_drivers[i]->supported ? "(NOT WORKING)" : "");
   }
   
   printf("\n");
}

/* Set the system id or processor board id in the eeprom */
int c2600_set_system_id(c2600_t *router,char *id)
{
  /* 11 characters is enough. Array is 20 long */
  strncpy(router->board_id,id,13);
  /* Make sure it is null terminated */
  router->board_id[13] = 0x00;
  c2600_refresh_systemid(router);
  return 0;
}

int c2600_refresh_systemid(c2600_t *router)
{
  if (router->board_id[0] == 0x00) return(0);
  m_uint8_t buf[11];
  parse_board_id(buf,router->board_id,9);
  // Does not use the cisco_eeprom libraries.. do it by hand
  int i;
  for(i=0;i<4;i++) {
      router->vm->chassis_cookie[i+12] = buf[i*2] << 8;
      router->vm->chassis_cookie[i+12] |= buf[(i*2)+1];
  }
  router->vm->chassis_cookie[i+12] &= 0x00ff;
  router->vm->chassis_cookie[i+12] |= buf[(i*2)]<<8;

  return (0);
}

/* Set the base MAC address of the chassis */
static int c2600_burn_mac_addr(c2600_t *router,n_eth_addr_t *addr)
{
   int i;

   for(i=0;i<3;i++) {
      router->vm->chassis_cookie[i+1] = addr->eth_addr_byte[i*2] << 8;
      router->vm->chassis_cookie[i+1] |= addr->eth_addr_byte[(i*2)+1];
   }

   return(0);
}

/* Set mainboard type */
int c2600_mainboard_set_type(c2600_t *router,char *mainboard_type)
{
   struct c2600_mb_id *mb_info;

   if (router->vm->status == VM_STATUS_RUNNING) {
      vm_error(router->vm,"unable to change mainboard type when online.\n");
      return(-1);
   }

   if (!(mb_info = c2600_get_mb_info(mainboard_type))) {
      vm_error(router->vm,"unknown mainboard '%s'\n",mainboard_type);
      return(-1);
   }

   router->mainboard_type = mainboard_type;
   router->xm_model = mb_info->xm_model;

   /* Set the cookie */
   memcpy(router->vm->chassis_cookie,
          eeprom_c2600_mb_data,sizeof(eeprom_c2600_mb_data));

   router->vm->chassis_cookie[6] = mb_info->id;

   /* Set the chassis base MAC address */
   c2600_burn_mac_addr(router,&router->mac_addr);

   /* Set the mainboard driver */
   if (vm_slot_active(router->vm,0,0))
      vm_slot_remove_binding(router->vm,0,0);

   vm_slot_add_binding(router->vm,mb_info->mb_driver,0,0);
   c2600_refresh_systemid(router);
   return(0);
}

/* Set chassis MAC address */
int c2600_chassis_set_mac_addr(c2600_t *router,char *mac_addr)
{
   if (parse_mac_addr(&router->mac_addr,mac_addr) == -1) {
      vm_error(router->vm,"unable to parse MAC address '%s'.\n",mac_addr);
      return(-1);
   }

   /* Set the chassis base MAC address */
   c2600_burn_mac_addr(router,&router->mac_addr);
   return(0);
}

/* Initialize a Cisco 2600 */
static int c2600_init(c2600_t *router)
{   
   vm_instance_t *vm = router->vm;

   /* Create the PCI bus */
   if (!(vm->pci_bus[0] = pci_bus_create("PCI0",0))) {
      vm_error(vm,"unable to create PCI data.\n");
      return(-1);
   }
   
   /* Create the PCI controller */
   if (dev_c2600_pci_init(vm,"c2600_pci",C2600_PCICTRL_ADDR,0x10000,
                          vm->pci_bus[0]) == -1)
      return(-1);

   /* Bind PCI bus to slots 0 and 1 */
   vm->slots_pci_bus[0] = vm->pci_bus[0];
   vm->slots_pci_bus[1] = vm->pci_bus[0];

   vm->elf_machine_id = C2600_ELF_MACHINE_ID;
   return(0);
}

/* Show C2600 hardware info */
void c2600_show_hardware(c2600_t *router)
{
   vm_instance_t *vm = router->vm;

   printf("C2600 instance '%s' (id %d):\n",vm->name,vm->instance_id);

   printf("  VM Status  : %d\n",vm->status);
   printf("  RAM size   : %u Mb\n",vm->ram_size);
   printf("  NVRAM size : %u Kb\n",vm->nvram_size);
   printf("  IOS image  : %s\n\n",vm->ios_image);

   if (vm->debug_level > 0) {
      dev_show_list(vm);
      pci_dev_show_list(vm->pci_bus[0]);
      pci_dev_show_list(vm->pci_bus[1]);
      printf("\n");
   }
}

/* Initialize default parameters for a C2600 */
static void c2600_init_defaults(c2600_t *router)
{   
   vm_instance_t *vm = router->vm;   
   n_eth_addr_t *m;
   m_uint16_t pid;

   /* Set platform slots characteristics */
   vm->nr_slots   = C2600_MAX_NM_BAYS;
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

   c2600_init_eeprom_groups(router);
   c2600_mainboard_set_type(router,C2600_DEFAULT_MAINBOARD);
   c2600_burn_mac_addr(router,&router->mac_addr);

   vm->ram_mmap          = C2600_DEFAULT_RAM_MMAP;
   vm->ram_size          = C2600_DEFAULT_RAM_SIZE;
   vm->rom_size          = C2600_DEFAULT_ROM_SIZE;
   vm->nvram_size        = C2600_DEFAULT_NVRAM_SIZE;
   vm->conf_reg_setup    = C2600_DEFAULT_CONF_REG;
   vm->clock_divisor     = C2600_DEFAULT_CLOCK_DIV;
   vm->nvram_rom_space   = C2600_NVRAM_ROM_RES_SIZE;
   vm->nm_iomem_size     = C2600_DEFAULT_IOMEM_SIZE;

   vm->pcmcia_disk_size[0] = C2600_DEFAULT_DISK0_SIZE;
   vm->pcmcia_disk_size[1] = C2600_DEFAULT_DISK1_SIZE;
}

/* Set an IRQ */
static void c2600_set_irq(vm_instance_t *vm,u_int irq)
{
   c2600_t *router = VM_C2600(vm);
   cpu_ppc_t *cpu = CPU_PPC32(vm->boot_cpu);
   u_int slot,port;

   switch(irq) {
      case C2600_VTIMER_IRQ:
         mpc860_set_pending_irq(router->mpc_data,30);
         break;
      case C2600_DUART_IRQ:
         mpc860_set_pending_irq(router->mpc_data,29);
         break;
      case C2600_NETIO_IRQ:
         mpc860_set_pending_irq(router->mpc_data,25);
         break;
      case C2600_PA_MGMT_IRQ:
         mpc860_set_pending_irq(router->mpc_data,27);
         break;

      case C2600_NETIO_IRQ_BASE ... C2600_NETIO_IRQ_END:
         c2600_net_irq_get_slot_port(irq,&slot,&port);
         dev_c2600_iofpga_net_set_irq(router->iofpga_data,slot,port);
         break;

      /* IRQ test */
      case 255:
         mpc860_set_pending_irq(router->mpc_data,24);
         break;
   }

   if (vm->irq_idle_preempt[irq])
      cpu_idle_break_wait(cpu->gen);
}

/* Clear an IRQ */
static void c2600_clear_irq(vm_instance_t *vm,u_int irq)
{
   c2600_t *router = VM_C2600(vm);
   u_int slot,port;

   switch(irq) {
      case C2600_VTIMER_IRQ:
         mpc860_clear_pending_irq(router->mpc_data,30);
         break;
      case C2600_DUART_IRQ:
         mpc860_clear_pending_irq(router->mpc_data,29);
         break;
      case C2600_NETIO_IRQ:
         mpc860_clear_pending_irq(router->mpc_data,25);
         break;
      case C2600_PA_MGMT_IRQ:
         mpc860_clear_pending_irq(router->mpc_data,27);
         break;

      case C2600_NETIO_IRQ_BASE ... C2600_NETIO_IRQ_END:
         c2600_net_irq_get_slot_port(irq,&slot,&port);
         dev_c2600_iofpga_net_clear_irq(router->iofpga_data,slot,port);
         break;

      /* IRQ test */
      case 255:
         mpc860_clear_pending_irq(router->mpc_data,24);
         break;
   }
}

/* Initialize the C2600 Platform */
static int c2600_init_platform(c2600_t *router)
{
   vm_instance_t *vm = router->vm;
   vm_obj_t *obj;
   cpu_ppc_t *cpu;
   cpu_gen_t *gen;

   /* Copy config register setup into "active" config register */
   vm->conf_reg = vm->conf_reg_setup;

   /* Create Console and AUX ports */
   vm_init_vtty(vm);

   /* Create a CPU group */
   vm->cpu_group = cpu_group_create("System CPU");

   /* Initialize the virtual PowerPC processor */
   if (!(gen = cpu_create(vm,CPU_TYPE_PPC32,0))) {
      vm_error(vm,"unable to create CPU!\n");
      return(-1);
   }

   cpu = CPU_PPC32(gen);

   /* Add this CPU to the system CPU group */
   cpu_group_add(vm->cpu_group,gen);
   vm->boot_cpu = gen;

   /* Set processor ID */
   ppc32_set_pvr(cpu,0x00500202);
   
   /* Mark the Network IO interrupt as high priority */
   vm->irq_idle_preempt[C2600_NETIO_IRQ] = TRUE;
   vm->irq_idle_preempt[C2600_DUART_IRQ] = TRUE;

   /* Copy some parameters from VM to CPU (idle PC, ...) */
   cpu->idle_pc = vm->idle_pc;

   if (vm->timer_irq_check_itv)
      cpu->timer_irq_check_itv = vm->timer_irq_check_itv;

   /* Remote emulator control */
   dev_remote_control_init(vm,0xf6000000,0x1000);

   /* MPC860 */
   cpu->mpc860_immr = C2600_MPC860_ADDR;

   if (dev_mpc860_init(vm,"MPC860",C2600_MPC860_ADDR,0x10000) == -1)
      return(-1);

   if (!(obj = vm_object_find(router->vm,"MPC860")))
      return(-1);

   router->mpc_data = obj->data;

   /* IO FPGA */
   if (dev_c2600_iofpga_init(router,C2600_IOFPGA_ADDR,0x10000) == -1)
      return(-1);

   if (!(obj = vm_object_find(router->vm,"io_fpga")))
      return(-1);

   router->iofpga_data = obj->data;

   /* Initialize the chassis */
   if (c2600_init(router) == -1)
      return(-1);

   /* Initialize RAM */
   vm_ram_init(vm,0x00000000ULL);

   /* Initialize ROM */
   if (!vm->rom_filename) {
      /* use embedded ROM */
      dev_rom_init(vm,"rom",C2600_ROM_ADDR,512*1024,
                   ppc32_microcode,ppc32_microcode_len);
   } else {
      /* use alternate ROM */
      dev_ram_init(vm,"rom",TRUE,TRUE,NULL,FALSE,C2600_ROM_ADDR,512*1024);
   }

   /* RAM aliasing */
   dev_create_ram_alias(vm,"ram_alias","ram",0x80000000,vm->ram_size*1048576);

   /* NVRAM */
   dev_ram_init(vm,"nvram",TRUE,FALSE,NULL,FALSE,
                C2600_NVRAM_ADDR,vm->nvram_size*4096);
   c2600_nvram_check_empty_config(vm);

   /* Bootflash */
   dev_bootflash_init(vm,"flash0","c2600-bootflash-8mb",
                      C2600_FLASH_ADDR);
   dev_bootflash_init(vm,"flash1","c2600-bootflash-8mb",
                      C2600_FLASH_ADDR+0x800000);

   /* Initialize the NS16552 DUART */
   dev_ns16552_init(vm,C2600_DUART_ADDR,0x1000,0,C2600_DUART_IRQ,
                    vm->vtty_con,vm->vtty_aux);

   /* Initialize Network Modules */
   if (vm_slot_init_all(vm) == -1)
      return(-1);

   /* Show device list */
   c2600_show_hardware(router);
   return(0);
}

static struct ppc32_bat_prog bat_array[] = {
   { PPC32_IBAT_IDX, 0, 0xfff0001e, 0xfff00001 },
   { PPC32_IBAT_IDX, 1, 0x00001ffe, 0x00000001 },
   { PPC32_IBAT_IDX, 2, 0x00000000, 0xee3e0072 },
   { PPC32_IBAT_IDX, 3, 0x80001ffe, 0x00000001 },

   { PPC32_DBAT_IDX, 0, 0x80001ffe, 0x00000042 },
   { PPC32_DBAT_IDX, 1, 0x00001ffe, 0x0000002a },
   { PPC32_DBAT_IDX, 2, 0x40007ffe, 0x4000002a },
   { PPC32_DBAT_IDX, 3, 0xf0001ffe, 0xf000002a },
   { -1, -1, 0, 0 },
};

/* Boot the IOS image */
int c2600_boot_ios(c2600_t *router)
{   
   vm_instance_t *vm = router->vm;
   cpu_ppc_t *cpu;

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
   cpu = CPU_PPC32(vm->boot_cpu);
   ppc32_reset(cpu);

   /* Adjust stack pointer */
   cpu->gpr[1] |= 0x80000000;

   /* Load BAT registers */
   printf("Loading BAT registers\n");
   ppc32_load_bat_array(cpu,bat_array);
   cpu->msr |= PPC32_MSR_IR|PPC32_MSR_DR;

   /* IRQ routing */
   vm->set_irq = c2600_set_irq;
   vm->clear_irq = c2600_clear_irq;

   /* Load IOS image */
   if (ppc32_load_elf_image(cpu,vm->ios_image,
                            (vm->ghost_status == VM_GHOST_RAM_USE),
                            &vm->ios_entry_point) < 0) 
   {
      vm_error(vm,"failed to load Cisco IOS image '%s'.\n",vm->ios_image);
      return(-1);
   }

   /* Launch the simulation */
   printf("\nC2600 '%s': starting simulation (CPU0 IA=0x%8.8x), "
          "JIT %sabled.\n",
          vm->name,cpu->ia,vm->jit_use ? "en":"dis");

   vm_log(vm,"C2600_BOOT",
          "starting instance (CPU0 PC=0x%8.8x,idle_pc=0x%8.8x,JIT %s)\n",
          cpu->ia,cpu->idle_pc,vm->jit_use ? "on":"off");

   /* Start main CPU */
   if (vm->ghost_status != VM_GHOST_RAM_GENERATE) {
      vm->status = VM_STATUS_RUNNING;
      cpu_start(vm->boot_cpu);
   } else {
      vm->status = VM_STATUS_SHUTDOWN;
   }
   return(0);
}

/* Initialize a Cisco 2600 instance */
static int c2600_init_instance(vm_instance_t *vm)
{   
   c2600_t *router = VM_C2600(vm);
   m_uint32_t rom_entry_point;
   cpu_ppc_t *cpu0;

   if (!vm->ios_image) {
      vm_error(vm,"no Cisco IOS image defined.");
      return(-1);
   }

   /* Initialize the C2600 platform */
   if (c2600_init_platform(router) == -1) {
      vm_error(vm,"unable to initialize the platform hardware.\n");
      return(-1);
   }

   /* Load IOS configuration files */
   if (vm->ios_startup_config != NULL || vm->ios_private_config != NULL) {
      vm_nvram_push_config(vm,vm->ios_startup_config,vm->ios_private_config);
      vm->conf_reg &= ~0x40;
   }

   /* Load ROM (ELF image or embedded) */
   cpu0 = CPU_PPC32(vm->boot_cpu);
   rom_entry_point = (m_uint32_t)PPC32_ROM_START;

   if ((vm->rom_filename != NULL) &&
       (ppc32_load_elf_image(cpu0,vm->rom_filename,0,&rom_entry_point) < 0))
   {
      vm_error(vm,"unable to load alternate ROM '%s', "
               "fallback to embedded ROM.\n\n",vm->rom_filename);
      vm->rom_filename = NULL;
   }

   return(c2600_boot_ios(router));
}

/* Stop a Cisco 2600 instance */
int c2600_stop_instance(vm_instance_t *vm)
{
   printf("\nC2600 '%s': stopping simulation.\n",vm->name);
   vm_log(vm,"C2600_STOP","stopping simulation.\n");

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

   /* Cleanup */   
   VM_C2600(vm)->iofpga_data = NULL;
   VM_C2600(vm)->mpc_data = NULL;
   return(0);
}

/* Get MAC address MSB */
static u_int c2600_get_mac_addr_msb(void)
{
   return(0xC8);
}

/* Parse specific options for the Cisco 2600 platform */
static int c2600_cli_parse_options(vm_instance_t *vm,int option)
{
   c2600_t *router = VM_C2600(vm);

   switch(option) {
      /* IO memory reserved for NMs (in percents!) */
      case OPT_IOMEM_SIZE:
         vm->nm_iomem_size = 0x8000 | atoi(optarg);
         break;

      /* Mainboard type */
      case 't':
         c2600_mainboard_set_type(router,optarg);
         break;

      /* Set the base MAC address */
      case 'm':
         if (!c2600_chassis_set_mac_addr(router,optarg))
            printf("MAC address set to '%s'.\n",optarg);
         break;

      /* Set the System ID */
      case 'I':
         if (!c2600_set_system_id(router,optarg))
            printf("System ID set to '%s'.\n",optarg);
         break;

      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Show specific CLI options */
static void c2600_cli_show_options(vm_instance_t *vm)
{
   printf("  --iomem-size <val> : IO memory (in percents, default: %u)\n"
          "  -p <nm_desc>       : Define a Network Module\n"
          "  -I <serialno>      : Set Processor Board Serial Number\n"
          "  -s <nm_nio>        : Bind a Network IO interface to a "
          "Network Module\n",
          vm->nm_iomem_size);
}

/* Platform definition */
static vm_platform_t c2600_platform = {
   "c2600", "C2600", "2600",
   c2600_create_instance,
   c2600_delete_instance,
   c2600_init_instance,
   c2600_stop_instance,
   NULL,
   NULL,
   c2600_nvram_extract_config,
   c2600_nvram_push_config,
   c2600_get_mac_addr_msb,
   c2600_save_config,
   c2600_cli_parse_options,
   c2600_cli_show_options,
   c2600_mainboard_show_drivers,
};

/* Register the c2600 platform */
int c2600_platform_register(void)
{
   if (vm_platform_register(&c2600_platform) == -1)
      return(-1);

   return(hypervisor_c2600_init(&c2600_platform));
}
