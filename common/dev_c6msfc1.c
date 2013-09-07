/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Generic MSFC1 routines and definitions (EEPROM,...).
 *
 * This is not a working platform! I only added it to play, since it is very
 * similar to an NPE-200. I think that could work with a functional CatOS SP.
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
#include "dev_dec21140.h"
#include "dev_i8254x.h"
#include "dev_c6msfc1.h"
#include "dev_c6msfc1_mpfpga.h"
#include "dev_vtty.h"
#include "registry.h"
#include "net.h"
#include "fs_nvram.h"

/* MSFC1 EEPROM */
static m_uint16_t eeprom_msfc1_data[128] = {
   0xabab, 0x0190, 0x1262, 0x0100, 0x0002, 0x6003, 0x00cf, 0x4369,
   0x7363, 0x6f20, 0x5379, 0x7374, 0x656d, 0x732c, 0x2049, 0x6e63,
   0x2e00, 0x5753, 0x2d46, 0x3630, 0x3031, 0x2d52, 0x5346, 0x4300,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3733, 0x2d37, 0x3135,
   0x302d, 0x3036, 0x0000, 0x0000, 0x0000, 0x4130, 0x3100, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x012d, 0x0000, 0x0000, 0x0009, 0x0005, 0x0001,
   0x0003, 0x0001, 0x0001, 0x0002, 0x00cf, 0xffbf, 0x0000, 0x0000,
   0x6003, 0x0162, 0x0afd, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0005, 0x00e0, 0xaabb, 0xcc00, 0x0100,
   0x0100, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x1401, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x1000, 0x4b3c, 0x4132, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080,
   0x8080, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
};

static struct cisco_eeprom msfc1_eeprom = {
   "msfc1", eeprom_msfc1_data, sizeof(eeprom_msfc1_data)/2,
};

/* ====================================================================== */
/* EOBC - Ethernet Out of Band Channel                                    */
/* ====================================================================== */
static int dev_c6msfc1_eobc_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct dec21140_data *data;

   /* Create the DEC21140 chip */
   data = dev_dec21140_init(vm,card->dev_name,vm->pci_bus[0],6,
                            c6msfc1_net_irq_for_slot_port(0,0));
   if (!data) return(-1);

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* Remove EOBC */
static int dev_c6msfc1_eobc_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   struct dec21140_data *data = card->drv_info;
   dev_dec21140_remove(data);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c6msfc1_eobc_set_nio(vm_instance_t *vm,struct cisco_card *card,
                                    u_int port_id,netio_desc_t *nio)
{
   struct dec21140_data *d = card->drv_info;

   if (!d || (port_id != 0))
      return(-1);

   return(dev_dec21140_set_nio(d,nio));
}

/* Unbind a Network IO descriptor */
static int dev_c6msfc1_eobc_unset_nio(vm_instance_t *vm,
                                      struct cisco_card *card,
                                      u_int port_id)
{
   struct dec21140_data *d = card->drv_info;
   
   if (!d || (port_id != 0))
      return(-1);
   
   dev_dec21140_unset_nio(d);
   return(0);
}

/* EOBC driver */
struct cisco_card_driver dev_c6msfc1_eobc = {
   "C6MSFC1_EOBC", 0, 0,
   dev_c6msfc1_eobc_init,
   dev_c6msfc1_eobc_shutdown,
   NULL,
   dev_c6msfc1_eobc_set_nio,
   dev_c6msfc1_eobc_unset_nio,
   NULL,
};

/* ====================================================================== */
/* IBC - InBand Channel                                                   */
/* ====================================================================== */
static int dev_c6msfc1_ibc_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct i8254x_data *data;

   /* Create the Intel Wiseman/Livengood chip */
   data = dev_i8254x_init(vm,card->dev_name,0,vm->pci_bus_pool[24],1,
                          c6msfc1_net_irq_for_slot_port(1,0));
   if (!data) return(-1);

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* Remove EOBC */
static int dev_c6msfc1_ibc_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   struct i8254x_data *data = card->drv_info;
   dev_i8254x_remove(data);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c6msfc1_ibc_set_nio(vm_instance_t *vm,struct cisco_card *card,
                                   u_int port_id,netio_desc_t *nio)
{
   struct i8254x_data *d = card->drv_info;

   if (!d || (port_id != 0))
      return(-1);

   return(dev_i8254x_set_nio(d,nio));
}

/* Unbind a Network IO descriptor */
static int dev_c6msfc1_ibc_unset_nio(vm_instance_t *vm,
                                     struct cisco_card *card,
                                     u_int port_id)
{
   struct i8254x_data *d = card->drv_info;

   if (!d || (port_id != 0))
      return(-1);

   dev_i8254x_unset_nio(d);
   return(0);
}

/* IBC driver */
struct cisco_card_driver dev_c6msfc1_ibc = {
   "C6MSFC1_IBC", 0, 0,
   dev_c6msfc1_ibc_init,
   dev_c6msfc1_ibc_shutdown,
   NULL,
   dev_c6msfc1_ibc_set_nio,
   dev_c6msfc1_ibc_unset_nio,
   NULL,
};

/* ======================================================================== */
/* Port Adapter Drivers                                                     */
/* ======================================================================== */
static struct cisco_card_driver *pa_drivers[] = {
   &dev_c6msfc1_eobc,
   &dev_c6msfc1_ibc,
   NULL,
};

/* ======================================================================== */
/* C6MSFC1 router instances                                                 */
/* ======================================================================== */

/* Initialize default parameters for a MSFC1 */
static void c6msfc1_init_defaults(c6msfc1_t *router);

/* Directly extract the configuration from the NVRAM device */
static int c6msfc1_nvram_extract_config(vm_instance_t *vm,u_char **startup_config,size_t *startup_len,u_char **private_config,size_t *private_len)
{
   int ret;

   ret = generic_nvram_extract_config(vm, "nvram", vm->nvram_rom_space, 0, C6MSFC1_NVRAM_ADDR + vm->nvram_rom_space, FS_NVRAM_FORMAT_ABSOLUTE_C6, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Directly push the IOS configuration to the NVRAM device */
static int c6msfc1_nvram_push_config(vm_instance_t *vm,u_char *startup_config,size_t startup_len,u_char *private_config,size_t private_len)
{
   int ret;

   ret = generic_nvram_push_config(vm, "nvram", vm->nvram_size*1024, vm->nvram_rom_space, 0, C6MSFC1_NVRAM_ADDR + vm->nvram_rom_space, FS_NVRAM_FORMAT_ABSOLUTE_C6, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Get slot/port corresponding to specified network IRQ */
static inline void 
c6msfc1_net_irq_get_slot_port(u_int irq,u_int *slot,u_int *port)
{
   *slot = irq - C6MSFC1_NETIO_IRQ_BASE;
   *port = 0;
}

/* Get network IRQ for specified slot/port */
u_int c6msfc1_net_irq_for_slot_port(u_int slot,u_int port)
{
   u_int irq;
   
   irq = C6MSFC1_NETIO_IRQ_BASE + slot;
   return(irq);
}

/* Set MSFC eeprom definition */
static int c6msfc1_set_eeprom(c6msfc1_t *router)
{
   if (cisco_eeprom_copy(&router->cpu_eeprom,&msfc1_eeprom) == -1) {
      vm_error(router->vm,"unable to set NPE EEPROM.\n");
      return(-1);
   }

   return(0);
}

/* Set the base MAC address of the chassis */
__unused static int c6msfc1_burn_mac_addr(c6msfc1_t *router,n_eth_addr_t *addr)
{
   m_uint8_t eeprom_ver;

   /* Read EEPROM format version */
   cisco_eeprom_get_byte(&router->mp_eeprom,0,&eeprom_ver);

   if (eeprom_ver != 1) {
      vm_error(router->vm,"c6msfc1_burn_mac_addr: unable to handle "
              "EEPROM version %u\n",eeprom_ver);
      return(-1);
   }

   cisco_eeprom_set_region(&router->mp_eeprom,12,addr->eth_addr_byte,6);
   return(0);
}

/* Create a new router instance */
static int c6msfc1_create_instance(vm_instance_t *vm)
{
   c6msfc1_t *router;

   if (!(router = malloc(sizeof(*router)))) {
      fprintf(stderr,"C6MFC1 '%s': Unable to create new instance!\n",vm->name);
      return(-1);
   }

   memset(router,0,sizeof(*router));
   router->vm = vm;
   vm->hw_data = router;
   vm->elf_machine_id = C6MSFC1_ELF_MACHINE_ID;

   c6msfc1_init_defaults(router);
   return(0);
}

/* Free resources used by a router instance */
static int c6msfc1_delete_instance(vm_instance_t *vm)
{
   c6msfc1_t *router = VM_C6MSFC1(vm);
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
   for(i=0;i<C6MSFC1_MAX_PA_BAYS;i++)
      vm_slot_remove_all_nio_bindings(vm,i);

   /* Shutdown all Network Modules */
   vm_slot_shutdown_all(vm);

   /* Free EEPROMs */
   cisco_eeprom_free(&router->cpu_eeprom);
   cisco_eeprom_free(&router->mp_eeprom);

   /* Free all resources used by VM */
   vm_free(vm);

   /* Free the router structure */
   free(router);
   return(TRUE);
}

/* Create the main PCI bus for a GT64010 based system */
static int c6msfc1_init_gt64010(c6msfc1_t *router)
{   
   vm_instance_t *vm = router->vm;

   if (!(vm->pci_bus[0] = pci_bus_create("PCI Bus 0",0))) {
      vm_error(vm,"unable to create PCI data.\n");
      return(-1);
   }
   
   return(dev_gt64010_init(vm,"gt64010",C6MSFC1_GT64K_ADDR,0x1000,
                           C6MSFC1_GT64K_IRQ));
}

/* Initialize a MSFC1 board */
static int c6msfc1_init_hw(c6msfc1_t *router)
{
   vm_instance_t *vm = router->vm;

   /* Set the processor type: R5000 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R5000);

   /* Initialize the Galileo GT-64010 PCI controller */
   if (c6msfc1_init_gt64010(router) == -1)
      return(-1);

   /* Create PCI bus 1 */
   vm->pci_bus_pool[24] = pci_bus_create("PCI Bus 1",-1);
   dev_dec21154_init(vm->pci_bus[0],1,vm->pci_bus_pool[24]);

   /* Initialize SRAM (4Mb) */
   dev_c7200_sram_init(vm,"sram",C6MSFC1_SRAM_ADDR,C6MSFC1_SRAM_SIZE,
                       vm->pci_bus_pool[24],0);

   /* PCI IO space */
   if (!(vm->pci_io_space = pci_io_data_init(vm,C6MSFC1_PCI_IO_ADDR)))
      return(-1);

   /* Cirrus Logic PD6729 (PCI-to-PCMCIA host adapter) */
   dev_clpd6729_init(vm,vm->pci_bus[0],5,vm->pci_io_space,0x402,0x403);

   return(0);
}

/* Show MSFC1 hardware info */
void c6msfc1_show_hardware(c6msfc1_t *router)
{
   vm_instance_t *vm = router->vm;

   printf("C6MSFC1 instance '%s' (id %d):\n",vm->name,vm->instance_id);

   printf("  VM Status  : %d\n",vm->status);
   printf("  RAM size   : %u Mb\n",vm->ram_size);
   printf("  IOMEM size : %u Mb\n",vm->iomem_size);
   printf("  NVRAM size : %u Kb\n",vm->nvram_size);
   printf("  IOS image  : %s\n\n",vm->ios_image);

   if (vm->debug_level > 0) {
      dev_show_list(vm);
      pci_dev_show_list(vm->pci_bus[0]);
      pci_dev_show_list(vm->pci_bus[1]);
      printf("\n");
   }
}

/* Initialize default parameters for a MSFC1 */
static void c6msfc1_init_defaults(c6msfc1_t *router)
{
   vm_instance_t *vm = router->vm;
   n_eth_addr_t *m;
   m_uint16_t pid;

   /* Set platform slots characteristics */
   vm->nr_slots   = C6MSFC1_MAX_PA_BAYS;
   vm->slots_type = CISCO_CARD_TYPE_PA;
   vm->slots_drivers = pa_drivers;
      
   pid = (m_uint16_t)getpid();

   /* Generate a chassis MAC address based on the instance ID */
   m = &router->mac_addr;
   m->eth_addr_byte[0] = vm_get_mac_addr_msb(vm);
   m->eth_addr_byte[1] = vm->instance_id & 0xFF;
   m->eth_addr_byte[2] = pid >> 8;
   m->eth_addr_byte[3] = pid & 0xFF;
   m->eth_addr_byte[4] = 0x00;
   m->eth_addr_byte[5] = 0x00;

   /* Default slot: 1 */
   router->msfc_slot = 1;

   c6msfc1_set_eeprom(router);
   c6msfc1_init_eeprom_groups(router);
   
   /* Create EOBC and IBC interfaces */
   vm_slot_add_binding(vm,"C6MSFC1_EOBC",0,0);
   vm_slot_add_binding(vm,"C6MSFC1_IBC",1,0);

   vm->ram_mmap        = C6MSFC1_DEFAULT_RAM_MMAP;
   vm->ram_size        = C6MSFC1_DEFAULT_RAM_SIZE;
   vm->rom_size        = C6MSFC1_DEFAULT_ROM_SIZE;
   vm->nvram_size      = C6MSFC1_DEFAULT_NVRAM_SIZE;
   vm->iomem_size      = 0;
   vm->conf_reg_setup  = C6MSFC1_DEFAULT_CONF_REG;
   vm->clock_divisor   = C6MSFC1_DEFAULT_CLOCK_DIV;
   vm->nvram_rom_space = C6MSFC1_NVRAM_ROM_RES_SIZE;
}

/* Run the checklist */
static int c6msfc1_checklist(c6msfc1_t *router)
{
   struct vm_instance *vm = router->vm;
   int res = 0;

   res += vm_object_check(vm,"ram");
   res += vm_object_check(vm,"rom");
   res += vm_object_check(vm,"nvram");
   res += vm_object_check(vm,"zero");

   if (res < 0)
      vm_error(vm,"incomplete initialization (no memory?)\n");

   return(res);
}

/* Initialize Port Adapters */
static int c6msfc1_init_platform_pa(c6msfc1_t *router)
{
   return(vm_slot_init_all(router->vm));
}

/* Initialize the MSFC1 Platform */
static int c6msfc1_init_platform(c6msfc1_t *router)
{
   struct vm_instance *vm = router->vm;
   cpu_mips_t *cpu0; 
   cpu_gen_t *gen0;
   vm_obj_t *obj;

   /* Copy config register setup into "active" config register */
   vm->conf_reg = vm->conf_reg_setup;

   /* Create Console and AUX ports */
   vm_init_vtty(vm);

   /* Create a CPU group */
   vm->cpu_group = cpu_group_create("System CPU");

   /* Initialize the virtual MIPS processor */
   if (!(gen0 = cpu_create(vm,CPU_TYPE_MIPS64,0))) {
      vm_error(vm,"unable to create CPU0!\n");
      return(-1);
   }

   cpu0 = CPU_MIPS64(gen0);

   /* Add this CPU to the system CPU group */
   cpu_group_add(vm->cpu_group,gen0);
   vm->boot_cpu = gen0;

   /* Initialize the IRQ routing vectors */
   vm->set_irq = mips64_vm_set_irq;
   vm->clear_irq = mips64_vm_clear_irq;

   /* Mark the Network IO interrupt as high priority */
   cpu0->irq_idle_preempt[C6MSFC1_NETIO_IRQ] = TRUE;
   cpu0->irq_idle_preempt[C6MSFC1_GT64K_IRQ] = TRUE;

   /* Copy some parameters from VM to CPU0 (idle PC, ...) */
   cpu0->idle_pc = vm->idle_pc;

   if (vm->timer_irq_check_itv)
      cpu0->timer_irq_check_itv = vm->timer_irq_check_itv;

   /*
    * On the MSFC1, bit 33 of physical addresses is used to bypass L2 cache.
    * We clear it systematically.
    */
   cpu0->addr_bus_mask = C6MSFC1_ADDR_BUS_MASK;

   /* Remote emulator control */
   dev_remote_control_init(vm,0x16000000,0x1000);

   /* Bootflash (8 Mb) */
   dev_bootflash_init(vm,"bootflash","c7200-bootflash-8mb",
                      C6MSFC1_BOOTFLASH_ADDR);

   /* NVRAM and calendar */
   dev_nvram_init(vm,"nvram",C6MSFC1_NVRAM_ADDR,
                  vm->nvram_size*1024,&vm->conf_reg);

   /* Bit-bucket zone */
   dev_zero_init(vm,"zero",C6MSFC1_BITBUCKET_ADDR,0xc00000);

   /* Initialize the NPE board */
   if (c6msfc1_init_hw(router) == -1)
      return(-1);

   /* Initialize RAM */
   vm_ram_init(vm,0x00000000ULL);

   /* Initialize ROM */
   if (!vm->rom_filename) {
      /* use embedded ROM */
      dev_rom_init(vm,"rom",C6MSFC1_ROM_ADDR,vm->rom_size*1048576,
                   mips64_microcode,mips64_microcode_len);
   } else {
      /* use alternate ROM */
      dev_ram_init(vm,"rom",TRUE,TRUE,NULL,FALSE,
                   C6MSFC1_ROM_ADDR,vm->rom_size*1048576);
   }

   /* Byte swapping */
   dev_bswap_init(vm,"mem_bswap",C6MSFC1_BSWAP_ADDR,1024*1048576,0x00000000ULL);

   /* PCI IO space */
   if (!(vm->pci_io_space = pci_io_data_init(vm,C6MSFC1_PCI_IO_ADDR)))
      return(-1);

   /* Initialize the Port Adapters */
   if (c6msfc1_init_platform_pa(router) == -1)
      return(-1);

   /* Verify the check list */
   if (c6msfc1_checklist(router) == -1)
      return(-1);

   /* Midplane FPGA */
   if (dev_c6msfc1_mpfpga_init(router,C6MSFC1_MPFPGA_ADDR,0x1000) == -1)
      return(-1);

   if (!(obj = vm_object_find(router->vm,"mp_fpga")))
      return(-1);
   
   router->mpfpga_data = obj->data;
   
   /* IO FPGA */
   if (dev_c6msfc1_iofpga_init(router,C6MSFC1_IOFPGA_ADDR,0x1000) == -1)
      return(-1);

   /* Show device list */
   c6msfc1_show_hardware(router);
   return(0);
}

/* Boot the IOS image */
static int c6msfc1_boot_ios(c6msfc1_t *router)
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
   printf("\nC6MSFC1 '%s': starting simulation (CPU0 PC=0x%llx), "
          "JIT %sabled.\n",
          vm->name,cpu->pc,vm->jit_use ? "en":"dis");

   vm_log(vm,"C6MSFC1_BOOT",
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
static void c6msfc1_set_irq(vm_instance_t *vm,u_int irq)
{
   c6msfc1_t *router = VM_C6MSFC1(vm);
   cpu_mips_t *cpu0 = CPU_MIPS64(vm->boot_cpu);
   u_int slot,port;
   
   switch(irq) {
      case 0 ... 7:
         mips64_set_irq(cpu0,irq);
         
         if (cpu0->irq_idle_preempt[irq])
            cpu_idle_break_wait(cpu0->gen);
         break;
         
      case C6MSFC1_NETIO_IRQ_BASE ... C6MSFC1_NETIO_IRQ_END:
         c6msfc1_net_irq_get_slot_port(irq,&slot,&port);
         dev_c6msfc1_mpfpga_net_set_irq(router->mpfpga_data,slot,port);
         break;
   }
}

/* Clear an IRQ */
static void c6msfc1_clear_irq(vm_instance_t *vm,u_int irq)
{
   c6msfc1_t *router = VM_C6MSFC1(vm);
   cpu_mips_t *cpu0 = CPU_MIPS64(vm->boot_cpu);
   u_int slot,port;
   
   switch(irq) {
      case 0 ... 7:
         mips64_clear_irq(cpu0,irq);
         break;
         
      case C6MSFC1_NETIO_IRQ_BASE ... C6MSFC1_NETIO_IRQ_END:
         c6msfc1_net_irq_get_slot_port(irq,&slot,&port);
         dev_c6msfc1_mpfpga_net_clear_irq(router->mpfpga_data,slot,port);
         break;
   }
}

/* Initialize a MSFC1 instance */
static int c6msfc1_init_instance(vm_instance_t *vm)
{
   c6msfc1_t *router = VM_C6MSFC1(vm);
   m_uint32_t rom_entry_point;
   cpu_mips_t *cpu0;

   /* Initialize the MSFC1 platform */
   if (c6msfc1_init_platform(router) == -1) {
      vm_error(vm,"unable to initialize the platform hardware.\n");
      return(-1);
   }

   /* IRQ routing */
   vm->set_irq = c6msfc1_set_irq;
   vm->clear_irq = c6msfc1_clear_irq;   
   
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

   return(c6msfc1_boot_ios(router));
}

/* Stop a MSFC1 instance */
static int c6msfc1_stop_instance(vm_instance_t *vm)
{
   printf("\nC6MSFC1 '%s': stopping simulation.\n",vm->name);
   vm_log(vm,"C6MSFC1_STOP","stopping simulation.\n");

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

/* Get MAC address MSB */
static u_int c6msfc1_get_mac_addr_msb(void)
{
   return(0xC6);
}

/* Show specific CLI options */
static void c6msfc1_cli_show_options(vm_instance_t *vm)
{
   printf("  -s <pa_nio>        : Bind a Network IO interface to a "
          "Port Adapter\n");
}

/* Platform definition */
static vm_platform_t c6msfc1_platform = {
   "c6msfc1", "C6MSFC1", "C6MSFC1",
   c6msfc1_create_instance,
   c6msfc1_delete_instance,
   c6msfc1_init_instance,
   c6msfc1_stop_instance,
   NULL,
   NULL,
   c6msfc1_nvram_extract_config,
   c6msfc1_nvram_push_config,
   c6msfc1_get_mac_addr_msb,
   NULL,
   NULL,
   c6msfc1_cli_show_options,
   NULL,
};

/* Register the C6-MSFC1 platform */
int c6msfc1_platform_register(void)
{
   return(vm_platform_register(&c6msfc1_platform));
}
