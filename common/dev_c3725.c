/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 * Patched by Jeremy Grossmann for the GNS3 project (www.gns3.net)
 *
 * Generic Cisco 3725 routines and definitions (EEPROM,...).
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
#include "pci_io.h"
#include "dev_gt.h"
#include "cisco_eeprom.h"
#include "dev_rom.h"
#include "dev_c3725.h"
#include "dev_c3725_iofpga.h"
#include "dev_vtty.h"
#include "registry.h"
#include "fs_nvram.h"

/* ======================================================================== */
/* EEPROM definitions                                                       */
/* ======================================================================== */

/* Cisco 3725 mainboard EEPROM */
static m_uint16_t eeprom_c3725_mainboard_data[] = {
   0x04FF, 0xC18B, 0x5858, 0x5858, 0x5858, 0x5858, 0x5858, 0x5809,
   0x6140, 0x0259, 0xC046, 0x0320, 0x003F, 0x1302, 0x4244, 0x3085,
   0x1C10, 0x8206, 0x80FF, 0xFFFF, 0xFFC4, 0x08FF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFF81, 0xFFFF, 0xFFFF, 0x03FF, 0x04FF, 0xC28B, 0x4654,
   0x5830, 0x3934, 0x3557, 0x304D, 0x59C3, 0x0600, 0x1319, 0x5C6F,
   0x7043, 0x0030, 0xC508, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x4100,
   0x0101, 0x02FF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

struct cisco_eeprom eeprom_c3725_mainboard = {
   "C3725 Backplane", 
   eeprom_c3725_mainboard_data,
   sizeof(eeprom_c3725_mainboard_data)/2,
};

/* ======================================================================== */
/* Network Module Drivers                                                   */
/* ======================================================================== */
static struct cisco_card_driver *nm_drivers[] = {
   &dev_c3725_nm_1fe_tx_driver,
   &dev_c3725_nm_16esw_driver,
   &dev_c3725_gt96100_fe_driver,
   &dev_c3725_nm_4t_driver,
   &dev_c3725_nm_nam_driver,
   &dev_c3725_nm_cids_driver,
   NULL,
};

/* ======================================================================== */
/* Cisco 3725 router instances                                              */
/* ======================================================================== */

/* Initialize default parameters for a C3725 */
static void c3725_init_defaults(c3725_t *router);

/* Directly extract the configuration from the NVRAM device */
static int c3725_nvram_extract_config(vm_instance_t *vm,u_char **startup_config,size_t *startup_len,u_char **private_config,size_t *private_len)
{
   int ret;

   ret = generic_nvram_extract_config(vm, "rom", C3725_NVRAM_OFFSET, C3725_NVRAM_SIZE, 0, FS_NVRAM_FORMAT_WITH_BACKUP, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Directly push the IOS configuration to the NVRAM device */
static int c3725_nvram_push_config(vm_instance_t *vm,u_char *startup_config,size_t startup_len,u_char *private_config,size_t private_len)
{
   int ret;

   ret = generic_nvram_push_config(vm, "rom", vm->rom_size*1048576, C3725_NVRAM_OFFSET, C3725_NVRAM_SIZE, 0, FS_NVRAM_FORMAT_WITH_BACKUP, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Check for empty config */
static int c3725_nvram_check_empty_config(vm_instance_t *vm)
{
   struct vdevice *rom_dev;
   m_uint64_t addr;
   size_t len;

   if (!(rom_dev = dev_get_by_name(vm,"rom")))
      return(-1);

   addr = rom_dev->phys_addr + C3725_NVRAM_OFFSET;
   len  = C3725_NVRAM_SIZE;

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
static int c3725_create_instance(vm_instance_t *vm)
{
   c3725_t *router;

   if (!(router = malloc(sizeof(*router)))) {
      fprintf(stderr,"C3725 '%s': Unable to create new instance!\n",vm->name);
      return(-1);
   }

   memset(router,0,sizeof(*router));
   router->vm = vm;
   vm->hw_data = router;

   c3725_init_defaults(router);
   return(0);
}

/* Free resources used by a router instance */
static int c3725_delete_instance(vm_instance_t *vm)
{
   c3725_t *router = VM_C3725(vm);
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

/* Get WIC device address for the specified onboard port */
int c3725_get_onboard_wic_addr(u_int slot,m_uint64_t *phys_addr)
{
   if (slot >= C3725_MAX_WIC_BAYS)
      return(-1);

   *phys_addr = C3725_WIC_ADDR + (slot * C3725_WIC_SIZE);
   return(0);
}

/* Set EEPROM for the specified slot */
int c3725_set_slot_eeprom(c3725_t *router,u_int slot,
                          struct cisco_eeprom *eeprom)
{
   if ((slot < 1) || (slot >= C3725_MAX_NM_BAYS))
      return(-1);

   router->nm_eeprom_group[slot-1].eeprom[0] = eeprom;
   return(0);
}

/* Get slot/port corresponding to specified network IRQ */
static inline void 
c3725_net_irq_get_slot_port(u_int irq,u_int *slot,u_int *port)
{
   irq -= C3725_NETIO_IRQ_BASE;
   *port = irq & C3725_NETIO_IRQ_PORT_MASK;
   *slot = irq >> C3725_NETIO_IRQ_PORT_BITS;
}

/* Get network IRQ for specified slot/port */
u_int c3725_net_irq_for_slot_port(u_int slot,u_int port)
{
   u_int irq;

   irq = (slot << C3725_NETIO_IRQ_PORT_BITS) + port;
   irq += C3725_NETIO_IRQ_BASE;

   return(irq);
}

/* Get PCI device for the specified NM bay */
int c3725_nm_get_pci_device(u_int nm_bay)
{
   switch(nm_bay) {
      case 1:
         return(0x06);
      case 2:
         return(0x0A);
      default:
         return(-1);
   }
}

/* Set the system id or processor board id in the eeprom */
int c3725_set_system_id(c3725_t *router,char *id)
{
  /* 11 characters is enough. Array is 20 long */
  strncpy(router->board_id,id,13);
  /* Make sure it is null terminated */
  router->board_id[13] = 0x00;
  c3725_refresh_systemid(router);
  return 0;
}

int c3725_refresh_systemid(c3725_t *router)
{
  if (router->board_id[0] == 0x00) return(0);
  m_uint8_t buf[11];

  parse_board_id(buf,router->board_id,11);
  cisco_eeprom_set_region(&router->mb_eeprom ,62,buf,11);
  return (0);
}


/* Set the base MAC address of the chassis */
static int c3725_burn_mac_addr(c3725_t *router,n_eth_addr_t *addr)
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
         vm_error(router->vm,"c3725_burn_mac_addr: unable to handle "
                  "EEPROM version %u\n",eeprom_ver);
         return(-1);
   }

   return(0);
}

/* Set chassis MAC address */
int c3725_chassis_set_mac_addr(c3725_t *router,char *mac_addr)
{
   if (parse_mac_addr(&router->mac_addr,mac_addr) == -1) {
      vm_error(router->vm,"unable to parse MAC address '%s'.\n",mac_addr);
      return(-1);
   }

   /* Set the chassis base MAC address */
   c3725_burn_mac_addr(router,&router->mac_addr);
   return(0);
}

/* Create the two main PCI busses for a GT64120 based system */
static int c3725_init_gt96100(c3725_t *router)
{
   vm_instance_t *vm = router->vm;
   vm_obj_t *obj;

   vm->pci_bus[0] = pci_bus_create("PCI bus #0",0);
   vm->pci_bus[1] = pci_bus_create("PCI bus #1",0);

   if (!vm->pci_bus[0] || !vm->pci_bus[1]) {
      vm_error(router->vm,"unable to create PCI data.\n");
      return(-1);
   }
   
   if (dev_gt96100_init(vm,"gt96100",C3725_GT96K_ADDR,0x200000,
                        C3725_GT96K_IRQ,
                        C3725_EXT_IRQ,
                        c3725_net_irq_for_slot_port(0,0),
                        255) == -1)
      return(-1);

   if (!(obj = vm_object_find(router->vm,"gt96100")))
      return(-1);

   router->gt_data = obj->data;
   return(0);
}

/* Initialize a Cisco 3725 */
static int c3725_init(c3725_t *router)
{   
   vm_instance_t *vm = router->vm;

   /* Set the processor type: R7000 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R7000);

   /* Initialize the Galileo GT-96100 PCI controller */
   if (c3725_init_gt96100(router) == -1)
      return(-1);

   /* Initialize PCI map (NM slots 1 & 2) */
   vm->slots_pci_bus[1] = vm->pci_bus[1];
   vm->slots_pci_bus[2] = vm->pci_bus[1];

   vm->elf_machine_id = C3725_ELF_MACHINE_ID;
   return(0);
}

/* Show C3725 hardware info */
void c3725_show_hardware(c3725_t *router)
{
   vm_instance_t *vm = router->vm;

   printf("C3725 instance '%s' (id %d):\n",vm->name,vm->instance_id);

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

/* Initialize default parameters for a C3725 */
static void c3725_init_defaults(c3725_t *router)
{   
   vm_instance_t *vm = router->vm;   
   n_eth_addr_t *m;
   m_uint16_t pid;

   /* Set platform slots characteristics */
   vm->nr_slots   = C3725_MAX_NM_BAYS;
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

   c3725_init_eeprom_groups(router);
   cisco_eeprom_copy(&router->mb_eeprom,&eeprom_c3725_mainboard);
   c3725_burn_mac_addr(router,&router->mac_addr);

   /* The GT96100 system controller has 2 integrated FastEthernet ports */
   vm_slot_add_binding(vm,"GT96100-FE",0,0);

   vm->ram_mmap          = C3725_DEFAULT_RAM_MMAP;
   vm->ram_size          = C3725_DEFAULT_RAM_SIZE;
   vm->rom_size          = C3725_DEFAULT_ROM_SIZE;
   vm->nvram_size        = C3725_DEFAULT_NVRAM_SIZE;
   vm->conf_reg_setup    = C3725_DEFAULT_CONF_REG;
   vm->clock_divisor     = C3725_DEFAULT_CLOCK_DIV;
   vm->nvram_rom_space   = C3725_NVRAM_ROM_RES_SIZE;
   vm->nm_iomem_size     = C3725_DEFAULT_IOMEM_SIZE;

   vm->pcmcia_disk_size[0] = C3725_DEFAULT_DISK0_SIZE;
   vm->pcmcia_disk_size[1] = C3725_DEFAULT_DISK1_SIZE;
}

/* Initialize the C3725 Platform */
static int c3725_init_platform(c3725_t *router)
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
   cpu->irq_idle_preempt[C3725_NETIO_IRQ] = TRUE;
   cpu->irq_idle_preempt[C3725_GT96K_IRQ] = TRUE;
   cpu->irq_idle_preempt[C3725_DUART_IRQ] = TRUE;

   /* Copy some parameters from VM to CPU (idle PC, ...) */
   cpu->idle_pc = vm->idle_pc;

   if (vm->timer_irq_check_itv)
      cpu->timer_irq_check_itv = vm->timer_irq_check_itv;

   /* Remote emulator control */
   dev_remote_control_init(vm,0x16000000,0x1000);

   /* Specific Storage Area (SSA) */
   dev_ram_init(vm,"ssa",TRUE,FALSE,NULL,FALSE,0x16001000ULL,0x7000);

   /* IO FPGA */
   if (dev_c3725_iofpga_init(router,C3725_IOFPGA_ADDR,0x40000) == -1)
      return(-1);

   if (!(obj = vm_object_find(router->vm,"io_fpga")))
      return(-1);

   router->iofpga_data = obj->data;

#if 0
   /* PCI IO space */
   if (!(vm->pci_io_space = pci_io_data_init(vm,C3725_PCI_IO_ADDR)))
      return(-1);
#endif

   /* Initialize the chassis */
   if (c3725_init(router) == -1)
      return(-1);

   /* Initialize RAM */
   vm_ram_init(vm,0x00000000ULL);

   /* Initialize ROM (as a Flash) */
   if (!(obj = dev_flash_init(vm,"rom",C3725_ROM_ADDR,vm->rom_size*1048576)))
      return(-1);

   dev_flash_copy_data(obj,0,mips64_microcode,mips64_microcode_len);
   c3725_nvram_check_empty_config(vm);

   /* Byte swapping */
   dev_bswap_init(vm,"mem_bswap",C3725_BSWAP_ADDR,1024*1048576,0x00000000ULL);

   /* Initialize the NS16552 DUART */
   dev_ns16552_init(vm,C3725_DUART_ADDR,0x1000,3,C3725_DUART_IRQ,
                    vm->vtty_con,vm->vtty_aux);

   /* PCMCIA Slot 0 */
   dev_pcmcia_disk_init(vm,"slot0",C3725_SLOT0_ADDR,0x200000,
                        vm->pcmcia_disk_size[0],1);

   /* PCMCIA Slot 1 */
   dev_pcmcia_disk_init(vm,"slot1",C3725_SLOT1_ADDR,0x200000,
                        vm->pcmcia_disk_size[1],1);

   /* Initialize Network Modules */
   if (vm_slot_init_all(vm) == -1)
      return(-1);

   /* Show device list */
   c3725_show_hardware(router);
   return(0);
}

/* Boot the IOS image */
static int c3725_boot_ios(c3725_t *router)
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
   printf("\nC3725 '%s': starting simulation (CPU0 PC=0x%llx), "
          "JIT %sabled.\n",
          vm->name,cpu->pc,vm->jit_use ? "en":"dis");

   vm_log(vm,"C3725_BOOT",
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
static void c3725_set_irq(vm_instance_t *vm,u_int irq)
{
   c3725_t *router = VM_C3725(vm);
   cpu_mips_t *cpu0 = CPU_MIPS64(vm->boot_cpu);
   u_int slot,port;

   switch(irq) {
      case 0 ... 7:
         mips64_set_irq(cpu0,irq);

         if (cpu0->irq_idle_preempt[irq])
            cpu_idle_break_wait(cpu0->gen);
         break;

      case C3725_NETIO_IRQ_BASE ... C3725_NETIO_IRQ_END:
         c3725_net_irq_get_slot_port(irq,&slot,&port);
         dev_c3725_iofpga_net_set_irq(router->iofpga_data,slot,port);
         break;
   }
}

/* Clear an IRQ */
static void c3725_clear_irq(vm_instance_t *vm,u_int irq)
{
   c3725_t *router = VM_C3725(vm);
   cpu_mips_t *cpu0 = CPU_MIPS64(vm->boot_cpu);
   u_int slot,port;

   switch(irq) {
      case 0 ... 7:
         mips64_clear_irq(cpu0,irq);
         break;

      case C3725_NETIO_IRQ_BASE ... C3725_NETIO_IRQ_END:
         c3725_net_irq_get_slot_port(irq,&slot,&port);
         dev_c3725_iofpga_net_clear_irq(router->iofpga_data,slot,port);
         break;
   }
}

/* Initialize a Cisco 3725 instance */
static int c3725_init_instance(vm_instance_t *vm)
{   
   c3725_t *router = VM_C3725(vm);
   m_uint32_t rom_entry_point;
   cpu_mips_t *cpu0;

   if (!vm->ios_image) {
      vm_error(vm,"no Cisco IOS image defined.");
      return(-1);
   }

   /* Initialize the C3725 platform */
   if (c3725_init_platform(router) == -1) {
      vm_error(vm,"unable to initialize the platform hardware.\n");
      return(-1);
   }

   /* IRQ routing */
   vm->set_irq = c3725_set_irq;
   vm->clear_irq = c3725_clear_irq;

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

   return(c3725_boot_ios(router));
}

/* Stop a Cisco 3725 instance */
static int c3725_stop_instance(vm_instance_t *vm)
{
   printf("\nC3725 '%s': stopping simulation.\n",vm->name);
   vm_log(vm,"C3725_STOP","stopping simulation.\n");

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
   VM_C3725(vm)->iofpga_data = NULL;
   VM_C3725(vm)->gt_data = NULL;
   return(0);
}

/* Get MAC address MSB */
static u_int c3725_get_mac_addr_msb(void)
{
   return(0xC2);
}

/* Parse specific options for the Cisco 3725 platform */
static int c3725_cli_parse_options(vm_instance_t *vm,int option)
{
   c3725_t *router = VM_C3725(vm);

   switch(option) {
      /* IO memory reserved for NMs (in percents!) */
      case OPT_IOMEM_SIZE:
         vm->nm_iomem_size = 0x8000 | atoi(optarg);
         break;

      /* Set the base MAC address */
      case 'm':
         if (!c3725_chassis_set_mac_addr(router,optarg))
            printf("MAC address set to '%s'.\n",optarg);
         break;

      /* Set the System ID */
      case 'I':
         if (!c3725_set_system_id(router,optarg))
            printf("System ID set to '%s'.\n",optarg);
         break;

      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Show specific CLI options */
static void c3725_cli_show_options(vm_instance_t *vm)
{
   printf("  --iomem-size <val> : IO memory (in percents, default: %u)\n"
          "  -p <nm_desc>       : Define a Network Module\n"
          "  -I <serialno>      : Set Processor Board Serial Number\n"
          "  -s <nm_nio>        : Bind a Network IO interface to a "
          "Network Module\n",
          vm->nm_iomem_size);
}

/* Platform definition */
static vm_platform_t c3725_platform = {
   "c3725", "C3725", "3725",
   c3725_create_instance,
   c3725_delete_instance,
   c3725_init_instance,
   c3725_stop_instance,
   NULL,
   NULL,
   c3725_nvram_extract_config,
   c3725_nvram_push_config,
   c3725_get_mac_addr_msb,
   NULL,
   c3725_cli_parse_options,
   c3725_cli_show_options,
   NULL,
};

/* Register the c3725 platform */
int c3725_platform_register(void)
{
   if (vm_platform_register(&c3725_platform) == -1)
      return(-1);

   return(hypervisor_c3725_init(&c3725_platform));
}
