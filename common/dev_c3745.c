/*
 * Cisco 3745 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 * Patched by Jeremy Grossmann for the GNS3 project (www.gns3.net)
 *
 * Generic Cisco 3745 routines and definitions (EEPROM,...).
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
#include "dev_c3745.h"
#include "dev_c3745_iofpga.h"
#include "dev_vtty.h"
#include "registry.h"
#include "fs_nvram.h"

/* ======================================================================== */
/* EEPROM definitions                                                       */
/* ======================================================================== */

/* Cisco 3745 motherboard EEPROM */
static m_uint16_t eeprom_c3745_motherboard_data[] = {
   0x04FF, 0xC18B, 0x5858, 0x5858, 0x5858, 0x5858, 0x5858, 0x5809,
   0x6940, 0x02F7, 0xC046, 0x0320, 0x003E, 0x3E03, 0x4241, 0x3085,
   0x1C12, 0x4004, 0x80FF, 0xFFFF, 0xFFC4, 0x08FF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFF81, 0x0000, 0x0000, 0x0400, 0x0300, 0xC508, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0x4102, 0x0002, 0x04C2, 0x8B58, 0x5858,
   0x5858, 0x5858, 0x5858, 0x5858, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

struct cisco_eeprom eeprom_c3745_motherboard = {
   "C3745 Motherboard", 
   eeprom_c3745_motherboard_data,
   sizeof(eeprom_c3745_motherboard_data)/2,
};

/* Cisco 3745 I/O board EEPROM */
static m_uint16_t eeprom_c3745_ioboard_data[] = {
   0x04FF, 0x4002, 0xF841, 0x0200, 0xC046, 0x0320, 0x0038, 0x7E01,
   0x4242, 0x3080, 0x0000, 0x0000, 0x0203, 0xC18B, 0x5858, 0x5858,
   0x5858, 0x5858, 0x5858, 0x5803, 0x0081, 0x0000, 0x0000, 0x0400,
   0xC809, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFC2, 0x8B58, 0x5858,
   0x5858, 0x5858, 0x5858, 0x5858, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

struct cisco_eeprom eeprom_c3745_ioboard = {
   "C3745 I/O board", 
   eeprom_c3745_ioboard_data,
   sizeof(eeprom_c3745_ioboard_data)/2,
};

/* Cisco 3745 midplane EEPROM */
static m_uint16_t eeprom_c3745_midplane_data[] = {
   0x04FF, 0x4003, 0x3E41, 0x0200, 0xC046, 0x0320, 0x0030, 0x0101,
   0x4241, 0x3080, 0x0000, 0x0000, 0x0205, 0xC18B, 0x5858, 0x5858,
   0x5858, 0x5858, 0x5858, 0x5803, 0x0081, 0x0000, 0x0000, 0x0400,
   0xC809, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFC3, 0x0600, 0x0DED,
   0xCD7D, 0x8043, 0x0050, 0xC28B, 0x4654, 0x5830, 0x3934, 0x3557,
   0x304D, 0x59FF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

struct cisco_eeprom eeprom_c3745_midplane = {
   "C3745 Midplane", 
   eeprom_c3745_midplane_data,
   sizeof(eeprom_c3745_midplane_data)/2,
};


/* ======================================================================== */
/* Network Module Drivers                                                   */
/* ======================================================================== */
static struct cisco_card_driver *nm_drivers[] = {
   &dev_c3745_nm_1fe_tx_driver,
   &dev_c3745_nm_16esw_driver,
   &dev_c3745_gt96100_fe_driver,
   &dev_c3745_nm_4t_driver,
   &dev_c3745_nm_nam_driver,
   &dev_c3745_nm_cids_driver,
   NULL,
};

/* ======================================================================== */
/* Cisco 3745 router instances                                              */
/* ======================================================================== */

/* Initialize default parameters for a C3745 */
static void c3745_init_defaults(c3745_t *router);

/* Directly extract the configuration from the NVRAM device */
static int c3745_nvram_extract_config(vm_instance_t *vm,u_char **startup_config,size_t *startup_len,u_char **private_config,size_t *private_len)
{
   int ret;

   ret = generic_nvram_extract_config(vm, "rom", C3745_NVRAM_OFFSET, C3745_NVRAM_SIZE, 0, FS_NVRAM_FORMAT_WITH_BACKUP, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Directly push the IOS configuration to the NVRAM device */
static int c3745_nvram_push_config(vm_instance_t *vm,u_char *startup_config,size_t startup_len,u_char *private_config,size_t private_len)
{
   int ret;

   ret = generic_nvram_push_config(vm, "rom", vm->rom_size*1048576, C3745_NVRAM_OFFSET, C3745_NVRAM_SIZE, 0, FS_NVRAM_FORMAT_WITH_BACKUP, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Check for empty config */
static int c3745_nvram_check_empty_config(vm_instance_t *vm)
{
   struct vdevice *rom_dev;
   m_uint64_t addr;
   size_t len;

   if (!(rom_dev = dev_get_by_name(vm,"rom")))
      return(-1);

   addr = rom_dev->phys_addr + C3745_NVRAM_OFFSET;
   len  = C3745_NVRAM_SIZE;

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
static int c3745_create_instance(vm_instance_t *vm)
{
   c3745_t *router;

   if (!(router = malloc(sizeof(*router)))) {
      fprintf(stderr,"C3745 '%s': Unable to create new instance!\n",vm->name);
      return(-1);
   }

   memset(router,0,sizeof(*router));
   router->vm = vm;
   vm->hw_data = router;

   c3745_init_defaults(router);
   return(0);
}

/* Free resources used by a router instance */
static int c3745_delete_instance(vm_instance_t *vm)
{
   c3745_t *router = VM_C3745(vm);
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
   for(i=0;i<3;i++)
      cisco_eeprom_free(&router->sys_eeprom[i]);
   
   /* Free all resources used by VM */
   vm_free(vm);

   /* Free the router structure */
   free(router);
   return(TRUE);
}

/* Get WIC device address for the specified onboard port */
int c3745_get_onboard_wic_addr(u_int slot,m_uint64_t *phys_addr)
{
   if (slot >= C3745_MAX_WIC_BAYS)
      return(-1);

   *phys_addr = C3745_WIC_ADDR + (slot * C3745_WIC_SIZE);
   return(0);
}

/* Set EEPROM for the specified slot */
int c3745_set_slot_eeprom(c3745_t *router,u_int slot,
                          struct cisco_eeprom *eeprom)
{
   if ((slot < 1) || (slot >= C3745_MAX_NM_BAYS))
      return(-1);

   router->nm_eeprom_group[slot-1].eeprom[0] = eeprom;
   return(0);
}

/* Get slot/port corresponding to specified network IRQ */
static inline void 
c3745_net_irq_get_slot_port(u_int irq,u_int *slot,u_int *port)
{
   irq -= C3745_NETIO_IRQ_BASE;
   *port = irq & C3745_NETIO_IRQ_PORT_MASK;
   *slot = irq >> C3745_NETIO_IRQ_PORT_BITS;
}

/* Get network IRQ for specified slot/port */
u_int c3745_net_irq_for_slot_port(u_int slot,u_int port)
{
   u_int irq;

   irq = (slot << C3745_NETIO_IRQ_PORT_BITS) + port;
   irq += C3745_NETIO_IRQ_BASE;

   return(irq);
}

/* Set the system id or processor board id in the eeprom */
int c3745_set_system_id(c3745_t *router,char *id)
{
  /* 11 characters is enough. Array is 20 long */
  strncpy(router->board_id,id,13);
  /* Make sure it is null terminated */
  router->board_id[13] = 0x00;
  c3745_refresh_systemid(router);
  return 0;
}
int c3745_refresh_systemid(c3745_t *router)
{
  if (router->board_id[0] == 0x00) return(0);
  m_uint8_t buf[11];
  parse_board_id(buf,router->board_id,11);
  cisco_eeprom_set_region(&router->sys_eeprom[2] ,72,buf,11);
  return (0);
}

/* Set the base MAC address of the chassis */
static int c3745_burn_mac_addr(c3745_t *router,n_eth_addr_t *addr)
{
   m_uint8_t eeprom_ver;
   size_t offset;

   /* Read EEPROM format version */
   cisco_eeprom_get_byte(&router->sys_eeprom[2],0,&eeprom_ver);

   switch(eeprom_ver) {
      case 0:
         cisco_eeprom_set_region(&router->sys_eeprom[2],2,
                                 addr->eth_addr_byte,6);
         break;

      case 4:
         if (!cisco_eeprom_v4_find_field(&router->sys_eeprom[2],
                                         0xC3,&offset)) {
            cisco_eeprom_set_region(&router->sys_eeprom[2],offset,
                                    addr->eth_addr_byte,6);
         }
         break;

      default:
         vm_error(router->vm,"c3745_burn_mac_addr: unable to handle "
                  "EEPROM version %u\n",eeprom_ver);
         return(-1);
   }

   return(0);
}

/* Set chassis MAC address */
int c3745_chassis_set_mac_addr(c3745_t *router,char *mac_addr)
{
   if (parse_mac_addr(&router->mac_addr,mac_addr) == -1) {
      vm_error(router->vm,"unable to parse MAC address '%s'.\n",mac_addr);
      return(-1);
   }

   /* Set the chassis base MAC address */
   c3745_burn_mac_addr(router,&router->mac_addr);
   return(0);
}

/* Create the two main PCI busses for a GT64120 based system */
static int c3745_init_gt96100(c3745_t *router)
{
   vm_instance_t *vm = router->vm;
   vm_obj_t *obj;

   vm->pci_bus[0] = pci_bus_create("PCI bus #0",0);
   vm->pci_bus[1] = pci_bus_create("PCI bus #1",0);

   if (!vm->pci_bus[0] || !vm->pci_bus[1]) {
      vm_error(router->vm,"unable to create PCI data.\n");
      return(-1);
   }
   
   if (dev_gt96100_init(vm,"gt96100",C3745_GT96K_ADDR,0x200000,
                        C3745_GT96K_IRQ,
                        C3745_EXT_IRQ,
                        c3745_net_irq_for_slot_port(0,0),
                        255) == -1)
      return(-1);

   if (!(obj = vm_object_find(router->vm,"gt96100")))
      return(-1);

   router->gt_data = obj->data;
   return(0);
}

/* Initialize a Cisco 3745 */
static int c3745_init(c3745_t *router)
{   
   vm_instance_t *vm = router->vm;
   char bus_name[128];
   int i;

   /* Set the processor type: R7000 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R7000);

   /* Initialize the Galileo GT-96100 PCI controller */
   if (c3745_init_gt96100(router) == -1)
      return(-1);

   /* Create the NM PCI busses for slots 1-4 */
   for(i=1;i<=4;i++) {
      snprintf(bus_name,sizeof(bus_name),"NM Slot %d",i);
      vm->pci_bus_pool[i] = pci_bus_create(bus_name,-1);

      /* Map the NM PCI bus */
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i];

      /* Create the PCI bridge */
      dev_ti2050b_init(vm->pci_bus[1],i,vm->slots_pci_bus[i]);
   }

   vm->elf_machine_id = C3745_ELF_MACHINE_ID;
   return(0);
}

/* Show C3745 hardware info */
void c3745_show_hardware(c3745_t *router)
{
   vm_instance_t *vm = router->vm;

   printf("C3745 instance '%s' (id %d):\n",vm->name,vm->instance_id);

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

/* Initialize default parameters for a C3745 */
static void c3745_init_defaults(c3745_t *router)
{   
   vm_instance_t *vm = router->vm;   
   n_eth_addr_t *m;
   m_uint16_t pid;

   /* Set platform slots characteristics */
   vm->nr_slots   = C3745_MAX_NM_BAYS;
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

   c3745_init_eeprom_groups(router);
   cisco_eeprom_copy(&router->sys_eeprom[0],&eeprom_c3745_motherboard);
   cisco_eeprom_copy(&router->sys_eeprom[1],&eeprom_c3745_ioboard);
   cisco_eeprom_copy(&router->sys_eeprom[2],&eeprom_c3745_midplane);
   c3745_burn_mac_addr(router,&router->mac_addr);

   /* The GT96100 system controller has 2 integrated FastEthernet ports */
   vm_slot_add_binding(vm,"GT96100-FE",0,0);

   vm->ram_mmap          = C3745_DEFAULT_RAM_MMAP;
   vm->ram_size          = C3745_DEFAULT_RAM_SIZE;
   vm->rom_size          = C3745_DEFAULT_ROM_SIZE;
   vm->nvram_size        = C3745_DEFAULT_NVRAM_SIZE;
   vm->conf_reg_setup    = C3745_DEFAULT_CONF_REG;
   vm->clock_divisor     = C3745_DEFAULT_CLOCK_DIV;
   vm->nvram_rom_space   = C3745_NVRAM_ROM_RES_SIZE;
   vm->nm_iomem_size     = C3745_DEFAULT_IOMEM_SIZE;

   vm->pcmcia_disk_size[0] = C3745_DEFAULT_DISK0_SIZE;
   vm->pcmcia_disk_size[1] = C3745_DEFAULT_DISK1_SIZE;
}

/* Initialize the C3745 Platform */
static int c3745_init_platform(c3745_t *router)
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
   cpu->irq_idle_preempt[C3745_NETIO_IRQ] = TRUE;
   cpu->irq_idle_preempt[C3745_GT96K_IRQ] = TRUE;
   cpu->irq_idle_preempt[C3745_DUART_IRQ] = TRUE;

   /* Copy some parameters from VM to CPU (idle PC, ...) */
   cpu->idle_pc = vm->idle_pc;

   if (vm->timer_irq_check_itv)
      cpu->timer_irq_check_itv = vm->timer_irq_check_itv;

   /* Remote emulator control */
   dev_remote_control_init(vm,0x16000000,0x1000);

   /* Specific Storage Area (SSA) */
   dev_ram_init(vm,"ssa",TRUE,FALSE,NULL,FALSE,0x16001000ULL,0x7000);

   /* IO FPGA */
   if (dev_c3745_iofpga_init(router,C3745_IOFPGA_ADDR,0x200000) == -1)
      return(-1);

   if (!(obj = vm_object_find(router->vm,"io_fpga")))
      return(-1);

   router->iofpga_data = obj->data;

#if 0
   /* PCI IO space */
   if (!(vm->pci_io_space = pci_io_data_init(vm,C3745_PCI_IO_ADDR)))
      return(-1);
#endif

   /* Initialize the chassis */
   if (c3745_init(router) == -1)
      return(-1);

   /* Initialize RAM */
   vm_ram_init(vm,0x00000000ULL);

   /* Initialize ROM (as a Flash) */
   if (!(obj = dev_flash_init(vm,"rom",C3745_ROM_ADDR,vm->rom_size*1048576)))
      return(-1);

   dev_flash_copy_data(obj,0,mips64_microcode,mips64_microcode_len);
   c3745_nvram_check_empty_config(vm);

   /* Byte swapping */
   dev_bswap_init(vm,"mem_bswap",C3745_BSWAP_ADDR,1024*1048576,0x00000000ULL);

   /* Initialize the NS16552 DUART */
   dev_ns16552_init(vm,C3745_DUART_ADDR,0x1000,3,C3745_DUART_IRQ,
                    vm->vtty_con,vm->vtty_aux);

   /* PCMCIA Slot 0 */
   dev_pcmcia_disk_init(vm,"slot0",C3745_SLOT0_ADDR,0x200000,
                        vm->pcmcia_disk_size[0],1);

   /* PCMCIA Slot 1 */
   dev_pcmcia_disk_init(vm,"slot1",C3745_SLOT1_ADDR,0x200000,
                        vm->pcmcia_disk_size[1],1);

   /* Initialize Network Modules */
   if (vm_slot_init_all(vm) == -1)
      return(-1);

   /* Show device list */
   c3745_show_hardware(router);
   return(0);
}

/* Boot the IOS image */
static int c3745_boot_ios(c3745_t *router)
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
   printf("\nC3745 '%s': starting simulation (CPU0 PC=0x%llx), "
          "JIT %sabled.\n",
          vm->name,cpu->pc,vm->jit_use ? "en":"dis");

   vm_log(vm,"C3745_BOOT",
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
static void c3745_set_irq(vm_instance_t *vm,u_int irq)
{
   c3745_t *router = VM_C3745(vm);
   cpu_mips_t *cpu0 = CPU_MIPS64(vm->boot_cpu);
   u_int slot,port;

   switch(irq) {
      case 0 ... 7:
         mips64_set_irq(cpu0,irq);

         if (cpu0->irq_idle_preempt[irq])
            cpu_idle_break_wait(cpu0->gen);
         break;

      case C3745_NETIO_IRQ_BASE ... C3745_NETIO_IRQ_END:
         c3745_net_irq_get_slot_port(irq,&slot,&port);
         dev_c3745_iofpga_net_set_irq(router->iofpga_data,slot,port);
         break;
   }
}

/* Clear an IRQ */
static void c3745_clear_irq(vm_instance_t *vm,u_int irq)
{
   c3745_t *router = VM_C3745(vm);
   cpu_mips_t *cpu0 = CPU_MIPS64(vm->boot_cpu);
   u_int slot,port;

   switch(irq) {
      case 0 ... 7:
         mips64_clear_irq(cpu0,irq);
         break;

      case C3745_NETIO_IRQ_BASE ... C3745_NETIO_IRQ_END:
         c3745_net_irq_get_slot_port(irq,&slot,&port);
         dev_c3745_iofpga_net_clear_irq(router->iofpga_data,slot,port);
         break;
   }
}

/* Initialize a Cisco 3745 instance */
static int c3745_init_instance(vm_instance_t *vm)
{   
   c3745_t *router = VM_C3745(vm);
   m_uint32_t rom_entry_point;
   cpu_mips_t *cpu0;

   if (!vm->ios_image) {
      vm_error(vm,"no Cisco IOS image defined.");
      return(-1);
   }

   /* Initialize the C3745 platform */
   if (c3745_init_platform(router) == -1) {
      vm_error(vm,"unable to initialize the platform hardware.\n");
      return(-1);
   }

   /* IRQ routing */
   vm->set_irq = c3745_set_irq;
   vm->clear_irq = c3745_clear_irq;

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

   return(c3745_boot_ios(router));
}

/* Stop a Cisco 3745 instance */
static int c3745_stop_instance(vm_instance_t *vm)
{
   printf("\nC3745 '%s': stopping simulation.\n",vm->name);
   vm_log(vm,"C3745_STOP","stopping simulation.\n");

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
   VM_C3745(vm)->iofpga_data = NULL;
   VM_C3745(vm)->gt_data = NULL;
   return(0);
}

/* Trigger an OIR event */
static int c3745_trigger_oir_event(c3745_t *router,u_int slot_mask)
{
   router->oir_status = slot_mask;
   vm_set_irq(router->vm,C3745_EXT_IRQ);
   return(0);
}

/* Initialize a new NM while the virtual router is online (OIR) */
static int c3745_nm_init_online(vm_instance_t *vm,u_int slot,u_int subslot)
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
   c3745_trigger_oir_event(VM_C3745(vm),1 << (slot - 1));
   return(0);
}

/* Stop a NM while the virtual router is online (OIR) */
static int c3745_nm_stop_online(vm_instance_t *vm,u_int slot,u_int subslot)
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
   c3745_trigger_oir_event(VM_C3745(vm),1 << (slot - 1));

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
static u_int c3745_get_mac_addr_msb(void)
{
   return(0xC4);
}

/* Parse specific options for the Cisco 1700 platform */
static int c3745_cli_parse_options(vm_instance_t *vm,int option)
{
   c3745_t *router = VM_C3745(vm);

   switch(option) {
      /* IO memory reserved for NMs (in percents!) */
      case OPT_IOMEM_SIZE:
         vm->nm_iomem_size = 0x8000 | atoi(optarg);
         break;

      /* Set the base MAC address */
      case 'm':
         if (!c3745_chassis_set_mac_addr(router,optarg))
            printf("MAC address set to '%s'.\n",optarg);
         break;

      /* Set the System ID */
      case 'I':
         if (!c3745_set_system_id(router,optarg))
            printf("System ID set to '%s'.\n",optarg);
         break;

      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Show specific CLI options */
static void c3745_cli_show_options(vm_instance_t *vm)
{
   printf("  --iomem-size <val> : IO memory (in percents, default: %u)\n"
          "  -p <nm_desc>       : Define a Network Module\n"
          "  -I <serialno>      : Set Processor Board Serial Number\n"
          "  -s <nm_nio>        : Bind a Network IO interface to a "
          "Network Module\n",
          vm->nm_iomem_size);
}

/* Platform definition */
static vm_platform_t c3745_platform = {
   "c3745", "C3745", "3745",
   c3745_create_instance,
   c3745_delete_instance,
   c3745_init_instance,
   c3745_stop_instance,
   c3745_nm_init_online,
   c3745_nm_stop_online,
   c3745_nvram_extract_config,
   c3745_nvram_push_config,
   c3745_get_mac_addr_msb,
   NULL,
   c3745_cli_parse_options,
   c3745_cli_show_options,
   NULL,
};

/* Register the c3745 platform */
int c3745_platform_register(void)
{
   if (vm_platform_register(&c3745_platform) == -1)
      return(-1);
   
   return(hypervisor_c3745_init(&c3745_platform));
}
