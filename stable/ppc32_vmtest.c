/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * PowerPC VM experimentations.
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
#include "dev_rom.h"
#include "pci_io.h"
#include "dev_vtty.h"
#include "registry.h"
#include "net.h"
#include "ppc32_mem.h"
#include "ppc32_vmtest.h"

static struct ppc32_bat_prog bat_array[] = {
   { PPC32_IBAT_IDX, 0, 0xfff0001e, 0xfff00001 },
   { PPC32_IBAT_IDX, 1, 0x00001ffe, 0x00000001 },
   { PPC32_IBAT_IDX, 2, 0x00000000, 0xee3e0072 },
   { PPC32_IBAT_IDX, 3, 0x80001ffe, 0x80000001 },

   { PPC32_DBAT_IDX, 0, 0x80001ffe, 0x80000042 },
   { PPC32_DBAT_IDX, 1, 0x00001ffe, 0x0000002a },
   { PPC32_DBAT_IDX, 2, 0x60001ffe, 0x6000002a },
   { PPC32_DBAT_IDX, 3, 0xfc0007fe, 0xfc00002a },
   { -1, -1, 0, 0 },
};

/* Create a new router instance */
static int ppc32_vmtest_create_instance(vm_instance_t *vm)
{
   vm->ram_size = PPC32_VMTEST_DEFAULT_RAM_SIZE;
   return(0);
}

/* Free resources used by a test instance */
static int ppc32_vmtest_delete_instance(vm_instance_t *vm)
{
   /* Stop all CPUs */
   if (vm->cpu_group != NULL) {
      vm_stop(vm);
      
      if (cpu_group_sync_state(vm->cpu_group) == -1) {
         vm_error(vm,"unable to sync with system CPUs.\n");
         return(FALSE);
      }
   }

   /* Free all resources used by VM */
   vm_free(vm);
   return(TRUE);
}

/* Set IRQ line */
static void ppc32_vmtest_set_irq(vm_instance_t *vm,u_int irq)
{
   cpu_ppc_t *cpu = CPU_PPC32(vm->boot_cpu);

   cpu->irq_check = cpu->irq_pending = TRUE;
}

/* Clear IRQ line */
static void ppc32_vmtest_clear_irq(vm_instance_t *vm,u_int irq)
{
   cpu_ppc_t *cpu = CPU_PPC32(vm->boot_cpu);

   cpu->irq_check = cpu->irq_pending = FALSE;
}

/* Initialize the PPC32 VM test Platform */
static int ppc32_vmtest_init_platform(vm_instance_t *vm)
{
   _maybe_used cpu_ppc_t *cpu0; 
   cpu_gen_t *gen0;

   /* Create Console and AUX ports */
   vm_init_vtty(vm);

   /* Create a CPU group */
   vm->cpu_group = cpu_group_create("System CPU");

   /* Initialize the virtual PowerPC processor */
   if (!(gen0 = cpu_create(vm,CPU_TYPE_PPC32,0))) {
      vm_error(vm,"unable to create CPU0!\n");
      return(-1);
   }

   cpu0 = CPU_PPC32(gen0);

   /* Enable as PowerPC 405 */
   //ppc32_set_pvr(cpu0,PPC32_PVR_405 | 0x0102);

   /* Add this CPU to the system CPU group */
   cpu_group_add(vm->cpu_group,gen0);
   vm->boot_cpu = gen0;

   /* Set IRQ vectors */
   vm->set_irq   = ppc32_vmtest_set_irq;
   vm->clear_irq = ppc32_vmtest_clear_irq;

#if 0
   {
      vm_obj_t *obj;
   /* Initialize ROM (as a Flash) */
   if (!(obj = dev_flash_init(vm,"rom",0xFF000000,16*1048576)))
      return(-1);

   dev_flash_copy_data(obj,0x0F00000,ppc32_microcode,ppc32_microcode_len);
   }
#endif

   //dev_bootflash_init(vm,"bootflash",0xFF000000,8*1048576);

#if 1
   /* Initialize ROM */
   if (!vm->rom_filename) {
      /* use embedded ROM */
      dev_rom_init(vm,"rom",0xFFF00000,512*1024,
                   ppc32_microcode,ppc32_microcode_len);
   } else {
      /* use alternate ROM */
      dev_ram_init(vm,"rom",TRUE,TRUE,NULL,FALSE,
                   0xFFF00000,512*1024);
   }
#endif

   dev_ram_init(vm,"nvram",TRUE,FALSE,NULL,FALSE,
                0x67c00000,vm->nvram_size*4096);

   dev_ns16552_init(vm,0xffe00000,0x1000,0,0,vm->vtty_con,vm->vtty_aux);

   /* Remote emulator control */
   dev_remote_control_init(vm,0xf6000000,0x1000);

   /* Initialize RAM */
   vm_ram_init(vm,0x00000000);

   /* RAM aliasing */
   dev_create_ram_alias(vm,"ram_alias","ram",0x80000000,vm->ram_size*1048576);

   /* Display the device list */
   dev_show_list(vm);
   return(0);
}

/* Boot the RAW image */
_unused static int ppc32_vmtest_boot_raw(vm_instance_t *vm)
{   
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

   /* Load RAW image */
   if (ppc32_load_raw_image(cpu,vm->ios_image,0xFFF00000) < 0) {
      vm_error(vm,"failed to load RAW image '%s'.\n",vm->ios_image);
      return(-1);
   }

   cpu->ia = 0xFFF00100;
   cpu->gpr[1] = 0x2000;

   /* Launch the simulation */
   printf("\nPPC32_VMTEST '%s': starting simulation (CPU0 IA=0x%8.8x), "
          "JIT %sabled.\n",
          vm->name,cpu->ia,vm->jit_use ? "en":"dis");

   vm_log(vm,"PPC32_VMTEST_BOOT",
          "starting instance (CPU0 IA=0x%8.8x,JIT %s)\n",
          cpu->ia,vm->jit_use ? "on":"off");
   
   /* Start main CPU */
   if (vm->ghost_status != VM_GHOST_RAM_GENERATE) {
      vm->status = VM_STATUS_RUNNING;
      cpu_start(vm->boot_cpu);
   } else {
      vm->status = VM_STATUS_SHUTDOWN;
   }
   return(0);
}

/* Boot the ELF image */
static int ppc32_vmtest_boot_elf(vm_instance_t *vm)
{     
   m_uint32_t rom_entry_point;
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

   /* Load ROM (ELF image or embedded) */
   cpu = CPU_PPC32(vm->boot_cpu);
   rom_entry_point = (m_uint32_t)PPC32_ROM_START;

   if ((vm->rom_filename != NULL) &&
       (ppc32_load_elf_image(cpu,vm->rom_filename,0,&rom_entry_point) < 0))
   {
      vm_error(vm,"unable to load alternate ROM '%s', "
               "fallback to embedded ROM.\n\n",vm->rom_filename);
      vm->rom_filename = NULL;
   }

   /* Load ELF image */
   if (ppc32_load_elf_image(cpu,vm->ios_image,
                            (vm->ghost_status == VM_GHOST_RAM_USE),
                            &vm->ios_entry_point) < 0)
   {
      vm_error(vm,"failed to load ELF image '%s'.\n",vm->ios_image);
      return(-1);
   }

   /* Launch the simulation */
   printf("\nPPC32_VMTEST '%s': starting simulation (CPU0 IA=0x%8.8x), "
          "JIT %sabled.\n",
          vm->name,cpu->ia,vm->jit_use ? "en":"dis");

   vm_log(vm,"PPC32_VMTEST_BOOT",
          "starting instance (CPU0 IA=0x%8.8x,JIT %s)\n",
          cpu->ia,vm->jit_use ? "on":"off");
   
   /* Start main CPU */
   if (vm->ghost_status != VM_GHOST_RAM_GENERATE) {
      vm->status = VM_STATUS_RUNNING;
      cpu_start(vm->boot_cpu);
   } else {
      vm->status = VM_STATUS_SHUTDOWN;
   }
   return(0);
}

/* Initialize a test instance */
static int ppc32_vmtest_init_instance(vm_instance_t *vm)
{
   /* Initialize the test platform */
   if (ppc32_vmtest_init_platform(vm) == -1) {
      vm_error(vm,"unable to initialize the platform hardware.\n");
      return(-1);
   }

   /* Load BAT registers */
   ppc32_load_bat_array(CPU_PPC32(vm->boot_cpu),bat_array);

   return(ppc32_vmtest_boot_elf(vm));
}

/* Stop a test instance */
static int ppc32_vmtest_stop_instance(vm_instance_t *vm)
{
   printf("\nPPC32_VMTEST '%s': stopping simulation.\n",vm->name);
   vm_log(vm,"PPC32_VMTEST_STOP","stopping simulation.\n");

   /* Stop all CPUs */
   if (vm->cpu_group != NULL) {
      vm_stop(vm);
      
      if (cpu_group_sync_state(vm->cpu_group) == -1) {
         vm_error(vm,"unable to sync with system CPUs.\n");
         return(-1);
      }
   }

   /* Free resources that were used during execution to emulate hardware */
   vm_hardware_shutdown(vm);
   return(0);
}

/* Platform definition */
static vm_platform_t ppc32_vmtest_platform = {
   "ppc32_test", "PPC32_VMTEST", "PPC32_TEST",
   ppc32_vmtest_create_instance,
   ppc32_vmtest_delete_instance,
   ppc32_vmtest_init_instance,
   ppc32_vmtest_stop_instance,
   NULL,
   NULL,
   NULL,
   NULL,
   NULL,
   NULL,
   NULL,
};

/* Register the ppc32_vmtest platform */
int ppc32_vmtest_platform_register(void)
{
   return(vm_platform_register(&ppc32_vmtest_platform));
}
