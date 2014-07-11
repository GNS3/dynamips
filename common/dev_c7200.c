/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 * Patched by Jeremy Grossmann for the GNS3 project (www.gns3.net)
 *
 * Generic Cisco 7200 routines and definitions (EEPROM,...).
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
#include "ppc32_mem.h"
#include "device.h"
#include "pci_io.h"
#include "dev_gt.h"
#include "dev_mv64460.h"
#include "cisco_eeprom.h"
#include "dev_rom.h"
#include "dev_c7200.h"
#include "dev_c7200_mpfpga.h"
#include "dev_vtty.h"
#include "registry.h"
#include "net.h"
#include "fs_nvram.h"

/* ======================================================================== */
/* CPU EEPROM definitions                                                   */
/* ======================================================================== */

/* NPE-100 */
static m_uint16_t eeprom_cpu_npe100_data[16] = {
   0x0135, 0x0203, 0xff10, 0x45C5, 0x4906, 0x0004, 0x0000, 0x0000,
   0x6000, 0x0000, 0x9901, 0x0600, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-150 */
static m_uint16_t eeprom_cpu_npe150_data[16] = {
   0x0115, 0x0203, 0xff10, 0x45C5, 0x4906, 0x0004, 0x0000, 0x0000,
   0x6000, 0x0000, 0x9901, 0x0600, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-175 */
static m_uint16_t eeprom_cpu_npe175_data[16] = {
   0x01C2, 0x0203, 0xff10, 0x45C5, 0x4906, 0x0004, 0x0000, 0x0000,
   0x6000, 0x0000, 0x9901, 0x0600, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-200 */
static m_uint16_t eeprom_cpu_npe200_data[16] = {
   0x0169, 0x0200, 0xff10, 0x45C5, 0x4909, 0x8902, 0x0000, 0x0000,
   0x6800, 0x0000, 0x9710, 0x2200, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-225 (same as NPE-175) */
static m_uint16_t eeprom_cpu_npe225_data[16] = {
   0x01C2, 0x0203, 0xff10, 0x45C5, 0x4906, 0x0004, 0x0000, 0x0000,
   0x6000, 0x0000, 0x9901, 0x0600, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-300 */
static m_uint16_t eeprom_cpu_npe300_data[16] = {
   0x01AE, 0x0402, 0xff10, 0x45C5, 0x490D, 0x5108, 0x0000, 0x0000,
   0x5000, 0x0000, 0x0012, 0x1000, 0x0000, 0xFFFF, 0xFFFF, 0xFF00,
};

/* NPE-400 */
static m_uint16_t eeprom_cpu_npe400_data[64] = {
   0x04FF, 0x4001, 0xF841, 0x0100, 0xC046, 0x0320, 0x001F, 0xC802,
   0x8249, 0x14BC, 0x0242, 0x4230, 0xC18B, 0x3131, 0x3131, 0x3131,
   0x3131, 0x0000, 0x0004, 0x0002, 0x0285, 0x1C0F, 0xF602, 0xCB87,
   0x4E50, 0x452D, 0x3430, 0x3080, 0x0000, 0x0000, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-G1 */
static m_uint16_t eeprom_cpu_npeg1_data[64] = {
   0x04FF, 0x4003, 0x5B41, 0x0200, 0xC046, 0x0320, 0x0049, 0xD00B,
   0x8249, 0x1B4C, 0x0B42, 0x4130, 0xC18B, 0x3131, 0x3131, 0x3131,
   0x3131, 0x0000, 0x0004, 0x0002, 0x0985, 0x1C13, 0xDA09, 0xCB86,
   0x4E50, 0x452D, 0x4731, 0x8000, 0x0000, 0x00FF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-G2 */
static m_uint16_t eeprom_cpu_npeg2_data[64] = {
   0x04FF, 0x4004, 0xCA41, 0x0201, 0x8744, 0x19BC, 0x0182, 0x4928,
   0x5901, 0x42FF, 0xFFC1, 0x8B43, 0x534A, 0x3039, 0x3435, 0x3239,
   0x3237, 0x0400, 0x0201, 0x851C, 0x1DA2, 0x01CB, 0x864E, 0x5045,
   0x2D47, 0x3280, 0x0000, 0x0000, 0x8956, 0x3031, 0x2DFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0x15FF,
};
/*
 * CPU EEPROM array.
 */
static struct cisco_eeprom c7200_cpu_eeprom[] = {
   { "npe-100", eeprom_cpu_npe100_data, sizeof(eeprom_cpu_npe100_data)/2 },
   { "npe-150", eeprom_cpu_npe150_data, sizeof(eeprom_cpu_npe150_data)/2 },
   { "npe-175", eeprom_cpu_npe175_data, sizeof(eeprom_cpu_npe175_data)/2 },
   { "npe-200", eeprom_cpu_npe200_data, sizeof(eeprom_cpu_npe200_data)/2 },
   { "npe-225", eeprom_cpu_npe225_data, sizeof(eeprom_cpu_npe225_data)/2 },
   { "npe-300", eeprom_cpu_npe300_data, sizeof(eeprom_cpu_npe300_data)/2 },
   { "npe-400", eeprom_cpu_npe400_data, sizeof(eeprom_cpu_npe400_data)/2 },
   { "npe-g1" , eeprom_cpu_npeg1_data , sizeof(eeprom_cpu_npeg1_data)/2  },
   { "npe-g2" , eeprom_cpu_npeg2_data , sizeof(eeprom_cpu_npeg2_data)/2  },
   { NULL, NULL, 0 },
};

/* ======================================================================== */
/* Midplane EEPROM definitions                                              */
/* ======================================================================== */

/* Standard Midplane EEPROM contents */
static m_uint16_t eeprom_midplane_data[32] = {
   0x0106, 0x0101, 0xff10, 0x45C5, 0x4906, 0x0303, 0xFFFF, 0xFFFF,
   0xFFFF, 0x0400, 0x0000, 0x0000, 0x4C09, 0x10B0, 0xFFFF, 0x00FF,
   0x0000, 0x0000, 0x6335, 0x8B28, 0x631D, 0x0000, 0x608E, 0x6D1C,
   0x62BB, 0x0000, 0x6335, 0x8B28, 0x0000, 0x0000, 0x6335, 0x8B28,
};

/* VXR Midplane EEPROM contents */
static m_uint16_t eeprom_vxr_midplane_data[32] = {
   0x0106, 0x0201, 0xff10, 0x45C5, 0x4906, 0x0303, 0xFFFF, 0xFFFF,
   0xFFFF, 0x0400, 0x0000, 0x0000, 0x4C09, 0x10B0, 0xFFFF, 0x00FF,
   0x0000, 0x0000, 0x6335, 0x8B28, 0x631D, 0x0000, 0x608E, 0x6D1C,
   0x62BB, 0x0000, 0x6335, 0x8B28, 0x0000, 0x0000, 0x6335, 0x8B28,
};

/*
 * Midplane EEPROM array.
 */
static struct cisco_eeprom c7200_midplane_eeprom[] = {
   { "std", eeprom_midplane_data, sizeof(eeprom_midplane_data)/2 },
   { "vxr", eeprom_vxr_midplane_data, sizeof(eeprom_vxr_midplane_data)/2 },
   { NULL, NULL, 0 },
};

/* ======================================================================== */
/* PEM EEPROM definitions (for NPE-175 and NPE-225)                         */
/* ======================================================================== */

/* NPE-175 */
static m_uint16_t eeprom_pem_npe175_data[16] = {
   0x01C3, 0x0100, 0xFFFF, 0xFFFF, 0x490D, 0x8A04, 0x0000, 0x0000,
   0x5000, 0x0000, 0x9906, 0x0400, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-225 */
static m_uint16_t eeprom_pem_npe225_data[16] = {
   0x01D5, 0x0100, 0xFFFF, 0xFFFF, 0x490D, 0x8A04, 0x0000, 0x0000,
   0x5000, 0x0000, 0x9906, 0x0400, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF,
};

/*
 * PEM EEPROM array.
 */
static struct cisco_eeprom c7200_pem_eeprom[] = {
   { "npe-175", eeprom_pem_npe175_data, sizeof(eeprom_pem_npe175_data)/2 },
   { "npe-225", eeprom_pem_npe225_data, sizeof(eeprom_pem_npe225_data)/2 },
   { NULL, NULL, 0 },
};

/* ======================================================================== */
/* Port Adapter Drivers                                                     */
/* ======================================================================== */
static struct cisco_card_driver *pa_drivers[] = {
   &dev_c7200_npeg2_driver,
   &dev_c7200_iocard_fe_driver,
   &dev_c7200_iocard_2fe_driver,
   &dev_c7200_iocard_ge_e_driver,
   &dev_c7200_pa_fe_tx_driver,
   &dev_c7200_pa_2fe_tx_driver,
   &dev_c7200_pa_ge_driver,
   &dev_c7200_pa_4e_driver,
   &dev_c7200_pa_8e_driver,
   &dev_c7200_pa_4t_driver,
   &dev_c7200_pa_8t_driver,
   &dev_c7200_pa_a1_driver,
   &dev_c7200_pa_pos_oc3_driver,
   &dev_c7200_pa_4b_driver,   
   &dev_c7200_pa_mc8te1_driver,
   &dev_c7200_jcpa_driver,
   NULL,
};

/* ======================================================================== */
/* NPE Drivers                                                              */
/* ======================================================================== */
#define DECLARE_NPE(type) \
   int (c7200_init_##type)(c7200_t *router)
   
DECLARE_NPE(npe100);
DECLARE_NPE(npe150);
DECLARE_NPE(npe175);
DECLARE_NPE(npe200);
DECLARE_NPE(npe225);
DECLARE_NPE(npe300);
DECLARE_NPE(npe400);
DECLARE_NPE(npeg1);
DECLARE_NPE(npeg2);

static struct c7200_npe_driver npe_drivers[] = {
   { "npe-100" , C7200_NPE_FAMILY_MIPS, c7200_init_npe100, 256, 1, 
     C7200_NVRAM_ADDR, TRUE, 0, 5,  0, 6 },
   { "npe-150" , C7200_NPE_FAMILY_MIPS, c7200_init_npe150, 256, 1, 
     C7200_NVRAM_ADDR, TRUE, 0, 5,  0, 6 },
   { "npe-175" , C7200_NPE_FAMILY_MIPS, c7200_init_npe175, 256, 1, 
     C7200_NVRAM_ADDR, TRUE, 2, 16, 1, 0 },
   { "npe-200" , C7200_NPE_FAMILY_MIPS, c7200_init_npe200, 256, 1, 
     C7200_NVRAM_ADDR, TRUE, 0, 5,  0, 6 },
   { "npe-225" , C7200_NPE_FAMILY_MIPS, c7200_init_npe225, 256, 1, 
     C7200_NVRAM_ADDR, TRUE, 2, 16, 1, 0 },
   { "npe-300" , C7200_NPE_FAMILY_MIPS, c7200_init_npe300, 256, 1, 
     C7200_NVRAM_ADDR, TRUE, 2, 16, 1, 0 },
   { "npe-400" , C7200_NPE_FAMILY_MIPS, c7200_init_npe400, 512, 1, 
     C7200_NVRAM_ADDR, TRUE, 2, 16, 1, 0 },
   { "npe-g1"  , C7200_NPE_FAMILY_MIPS, c7200_init_npeg1, 1024, 0, 
     C7200_G1_NVRAM_ADDR, FALSE, 17, 16, 16, 0 },
   { "npe-g2"  , C7200_NPE_FAMILY_PPC , c7200_init_npeg2, 1024, 0,
     C7200_G2_NVRAM_ADDR, FALSE, 17, 16, 16, 0 },
   { NULL, -1, NULL, -1, -1, 0, -1, -1, -1, -1 },
};

/* ======================================================================== */
/* Cisco 7200 router instances                                              */
/* ======================================================================== */

/* Initialize default parameters for a C7200 */
static void c7200_init_defaults(c7200_t *router);

/* Directly extract the configuration from the NVRAM device */
static int c7200_nvram_extract_config(vm_instance_t *vm,u_char **startup_config,size_t *startup_len,u_char **private_config,size_t *private_len)
{
   int ret;

   ret = generic_nvram_extract_config(vm, "nvram", vm->nvram_rom_space, 0, VM_C7200(vm)->npe_driver->nvram_addr + vm->nvram_rom_space, FS_NVRAM_FORMAT_ABSOLUTE, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Directly push the IOS configuration to the NVRAM device */
static int c7200_nvram_push_config(vm_instance_t *vm,u_char *startup_config,size_t startup_len,u_char *private_config,size_t private_len)
{
   int ret;

   ret = generic_nvram_push_config(vm, "nvram", vm->nvram_size*1024, vm->nvram_rom_space, 0, VM_C7200(vm)->npe_driver->nvram_addr + vm->nvram_rom_space, FS_NVRAM_FORMAT_ABSOLUTE, startup_config, startup_len, private_config, private_len);

   return(ret);
}

/* Get an EEPROM for a given NPE model */
static const struct cisco_eeprom *c7200_get_cpu_eeprom(char *npe_name)
{
   return(cisco_eeprom_find(c7200_cpu_eeprom,npe_name));
}

/* Get an EEPROM for a given midplane model */
static const struct cisco_eeprom *
c7200_get_midplane_eeprom(char *midplane_name)
{
   return(cisco_eeprom_find(c7200_midplane_eeprom,midplane_name));
}

/* Get a PEM EEPROM for a given NPE model */
static const struct cisco_eeprom *c7200_get_pem_eeprom(char *npe_name)
{
   return(cisco_eeprom_find(c7200_pem_eeprom,npe_name));
}

/* Set the base MAC address of the chassis */
static int c7200_burn_mac_addr(c7200_t *router,n_eth_addr_t *addr)
{
   m_uint8_t eeprom_ver;

   /* Read EEPROM format version */
   cisco_eeprom_get_byte(&router->mp_eeprom,0,&eeprom_ver);

   if (eeprom_ver != 1) {
      vm_error(router->vm,"c7200_burn_mac_addr: unable to handle "
              "EEPROM version %u\n",eeprom_ver);
      return(-1);
   }

   cisco_eeprom_set_region(&router->mp_eeprom,12,addr->eth_addr_byte,6);
   return(0);
}

/* Free specific hardware resources used by C7200 */
static void c7200_free_hw_ressources(c7200_t *router)
{
   /* Shutdown all Port Adapters */
   vm_slot_shutdown_all(router->vm);

   /* Inactivate the PCMCIA bus */
   router->pcmcia_bus = NULL;

   /* Remove the hidden I/O bridge */
   if (router->io_pci_bridge != NULL) {
      pci_bridge_remove(router->io_pci_bridge);
      router->io_pci_bridge = NULL;
   }
}

/* Create a new router instance */
static int c7200_create_instance(vm_instance_t *vm)
{
   c7200_t *router;

   if (!(router = malloc(sizeof(*router)))) {
      fprintf(stderr,"C7200 '%s': Unable to create new instance!\n",vm->name);
      return(-1);
   }

   memset(router,0,sizeof(*router));
   router->vm = vm;
   router->npe400_ram_size = C7200_DEFAULT_RAM_SIZE;
   vm->hw_data = router;
   vm->elf_machine_id = C7200_ELF_MACHINE_ID;

   c7200_init_defaults(router);
   return(0);
}

/* Free resources used by a router instance */
static int c7200_delete_instance(vm_instance_t *vm)
{
   c7200_t *router = VM_C7200(vm);
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

   /* Free specific HW resources */
   c7200_free_hw_ressources(router);

   /* Free EEPROMs */
   cisco_eeprom_free(&router->cpu_eeprom);
   cisco_eeprom_free(&router->mp_eeprom);
   cisco_eeprom_free(&router->pem_eeprom);

   /* Free all resources used by VM */
   vm_free(vm);

   /* Free the router structure */
   free(router);
   return(TRUE);
}

/* Save configuration of a C7200 instance */
static void c7200_save_config(vm_instance_t *vm,FILE *fd)
{
   c7200_t *router = VM_C7200(vm);

   fprintf(fd,"c7200 set_npe %s %s\n",vm->name,router->npe_driver->npe_type);
   fprintf(fd,"c7200 set_midplane %s %s\n\n",vm->name,router->midplane_type);
}

/* Returns TRUE if the specified card in slot 0 is an I/O card */
int c7200_slot0_iocard_present(c7200_t *router)
{
   struct cisco_eeprom *eeprom;
   m_uint8_t eeprom_ver,data[2];
   m_uint16_t card_type;
   size_t offset;

   if (!(eeprom = router->pa_eeprom_g1.eeprom[0]))
      return(FALSE);
    
   /* Read EEPROM format version */
   card_type = 0;
   cisco_eeprom_get_byte(eeprom,0,&eeprom_ver);

   switch(eeprom_ver) {
      case 1:
         cisco_eeprom_get_byte(eeprom,0,&data[0]);
         cisco_eeprom_get_byte(eeprom,1,&data[1]);
         card_type = ((m_uint16_t)data[0] << 8) | data[1];
         break;

      case 4:
         if (!cisco_eeprom_v4_find_field(eeprom,0x40,&offset)) {
            cisco_eeprom_get_byte(eeprom,offset,&data[0]);
            cisco_eeprom_get_byte(eeprom,offset+1,&data[1]);
            card_type = ((m_uint16_t)data[0] << 8) | data[1];        
         }
         break;
   }

   /* jacket card is not an i/o card */
   if (card_type == 0x0511)
      return(FALSE);

   /* by default, if there is something, consider it as an i/o card */
   return(TRUE);
}

/* Set EEPROM for the specified slot */
int c7200_set_slot_eeprom(c7200_t *router,u_int slot,
                          struct cisco_eeprom *eeprom)
{
   if (slot >= C7200_MAX_PA_BAYS)
      return(-1);

   switch(slot) {
      /* Group 1: bays 0, 1, 3, 4 */
      case 0:
         router->pa_eeprom_g1.eeprom[0] = eeprom;
         break;
      case 1:
         router->pa_eeprom_g1.eeprom[1] = eeprom;
         break;
      case 3:
         router->pa_eeprom_g1.eeprom[2] = eeprom;
         break;
      case 4:
         router->pa_eeprom_g1.eeprom[3] = eeprom;
         break;

      /* Group 2: bays 2, 5, 6 */
      case 2:
         router->pa_eeprom_g2.eeprom[0] = eeprom;
         break;        
      case 5:
         router->pa_eeprom_g2.eeprom[1] = eeprom;
         break;
      case 6:
         router->pa_eeprom_g2.eeprom[2] = eeprom;
         break;

      /* Group 3: bay 7 */
      case 7:
         router->pa_eeprom_g3.eeprom[0] = eeprom;
         break;
   }

   return(0);
}

/* Network IRQ distribution */
struct net_irq_distrib  {
   u_int reg;
   u_int offset;
};

static struct net_irq_distrib net_irq_dist[C7200_MAX_PA_BAYS] = {
   { 0,  0 },  /* Slot 0: reg 0x10,  0x000000XX */
   { 0,  8 },  /* Slot 1: reg 0x10,  0x0000XX00 */
   { 1,  8 },  /* Slot 2: reg 0x18,  0x0000XX00 */
   { 0, 24 },  /* Slot 3: reg 0x10,  0xXX000000 */
   { 0, 16 },  /* Slot 4: reg 0x10,  0x00XX0000 */
   { 1, 24 },  /* Slot 5: reg 0x18,  0xXX000000 */
   { 1, 16 },  /* Slot 6: reg 0x18,  0x00XX0000 */
   { 2, 24 },  /* Slot 7: reg 0x294, 0xXX000000 */ 
};

/* Get register offset for the specified slot */
u_int dev_c7200_net_get_reg_offset(u_int slot)
{
   return(net_irq_dist[slot].offset);
}

/* Update network interrupt status */
void dev_c7200_net_update_irq(c7200_t *router)
{
   int i,status = 0;

   for(i=0;i<3;i++)
      status |= router->net_irq_status[i] & router->net_irq_mask[i];
   
   if (status) {
      vm_set_irq(router->vm,C7200_NETIO_IRQ);
   } else {
      vm_clear_irq(router->vm,C7200_NETIO_IRQ);
   }
}

/* Trigger a Network IRQ for the specified slot/port */
void dev_c7200_net_set_irq(c7200_t *router,u_int slot,u_int port)
{
   struct net_irq_distrib *irq_dist;

#if DEBUG_NET_IRQ
   vm_log(router->vm,"C7200","setting NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   irq_dist = &net_irq_dist[slot];
   router->net_irq_status[irq_dist->reg] |= 1 << (irq_dist->offset + port);
   dev_c7200_net_update_irq(router);
}

/* Clear a Network IRQ for the specified slot/port */
void dev_c7200_net_clear_irq(c7200_t *router,u_int slot,u_int port)
{
   struct net_irq_distrib *irq_dist;

#if DEBUG_NET_IRQ
   vm_log(router->vm,"C7200","clearing NetIRQ for slot %u port %u\n",
          slot,port);
#endif
   irq_dist = &net_irq_dist[slot];
   router->net_irq_status[irq_dist->reg] &= ~(1 << (irq_dist->offset + port));
   dev_c7200_net_update_irq(router);
}

/* Get slot/port corresponding to specified network IRQ */
static inline void 
c7200_net_irq_get_slot_port(u_int irq,u_int *slot,u_int *port)
{
   irq -= C7200_NETIO_IRQ_BASE;
   *port = irq & C7200_NETIO_IRQ_PORT_MASK;
   *slot = irq >> C7200_NETIO_IRQ_PORT_BITS;
}

/* Get network IRQ for specified slot/port */
u_int c7200_net_irq_for_slot_port(u_int slot,u_int port)
{
   u_int irq;

   irq = (slot << C7200_NETIO_IRQ_PORT_BITS) + port;
   irq += C7200_NETIO_IRQ_BASE;

   return(irq);
}

/* Set NPE eeprom definition */
static int c7200_npe_set_eeprom(c7200_t *router)
{
   const struct cisco_eeprom *eeprom;

   if (!(eeprom = c7200_get_cpu_eeprom(router->npe_driver->npe_type))) {
      vm_error(router->vm,"unknown NPE \"%s\" (internal error)!\n",
              router->npe_driver->npe_type);
      return(-1);
   }

   if (cisco_eeprom_copy(&router->cpu_eeprom,eeprom) == -1) {
      vm_error(router->vm,"unable to set NPE EEPROM.\n");
      return(-1);
   }

   return(0);
}

/* Set PEM eeprom definition */
static int c7200_pem_set_eeprom(c7200_t *router)
{
   const struct cisco_eeprom *eeprom;

   if (!(eeprom = c7200_get_pem_eeprom(router->npe_driver->npe_type))) {
      vm_error(router->vm,"no PEM EEPROM found for NPE type \"%s\"!\n",
              router->npe_driver->npe_type);
      return(-1);
   }

   if (cisco_eeprom_copy(&router->pem_eeprom,eeprom) == -1) {
      vm_error(router->vm,"unable to set PEM EEPROM.\n");
      return(-1);
   }

   return(0);
}

/* Get an NPE driver */
struct c7200_npe_driver *c7200_npe_get_driver(char *npe_type)
{
   int i;

   for(i=0;npe_drivers[i].npe_type;i++)
      if (!strcmp(npe_drivers[i].npe_type,npe_type))
         return(&npe_drivers[i]);

   return NULL;
}

/* Set the NPE type */
int c7200_npe_set_type(c7200_t *router,char *npe_type)
{
   struct c7200_npe_driver *driver;

   if (router->vm->status == VM_STATUS_RUNNING) {
      vm_error(router->vm,"unable to change NPE type when online.\n");
      return(-1);
   }

   if (!(driver = c7200_npe_get_driver(npe_type))) {
      vm_error(router->vm,"unknown NPE type '%s'.\n",npe_type);
      return(-1);
   }

   router->npe_driver = driver;

   if (c7200_npe_set_eeprom(router) == -1) {
      vm_error(router->vm,"unable to find NPE '%s' EEPROM!\n",
               router->npe_driver->npe_type);
      return(-1);
   }

#if 0 /* FIXME - for a later release */
   /* Use a C7200-IO-FE by default in slot 0 if an I/O card is required */
   if (driver->iocard_required) {
      vm_slot_add_binding(router->vm,"C7200-IO-FE",0,0);
      vm_slot_set_flag(router->vm,0,0,CISCO_CARD_FLAG_OVERRIDE);
   }
#endif
   c7200_refresh_systemid(router);
   return(0);
}

/* Show the list of available NPE drivers */
static void c7200_npe_show_drivers(void)
{
   int i;

   printf("Available C7200 NPE drivers:\n");

   for(i=0;npe_drivers[i].npe_type;i++) {
      printf("  * %s %s\n",
             npe_drivers[i].npe_type,
             !npe_drivers[i].supported ? "(NOT WORKING)" : "");
   }
   
   printf("\n");
}

/* Set Midplane type */
int c7200_midplane_set_type(c7200_t *router,char *midplane_type)
{
   const struct cisco_eeprom *eeprom;
   m_uint8_t version;

   if (router->vm->status == VM_STATUS_RUNNING) {
      vm_error(router->vm,"unable to change Midplane type when online.\n");
      return(-1);
   }

   /* Set EEPROM */
   if (!(eeprom = c7200_get_midplane_eeprom(midplane_type))) {
      vm_error(router->vm,"unknown Midplane \"%s\"!\n",midplane_type);
      return(-1);
   }

   /* Copy the midplane EEPROM */
   if (cisco_eeprom_copy(&router->mp_eeprom,eeprom) == -1) {
      vm_error(router->vm,"unable to set midplane EEPROM.\n");
      return(-1);
   }

   /* Set the chassis base MAC address */
   c7200_burn_mac_addr(router,&router->mac_addr);

   /* Get the midplane version */
   cisco_eeprom_get_byte(&router->mp_eeprom,2,&version);
   router->midplane_version = version;  
   router->midplane_type = eeprom->name;
   c7200_refresh_systemid(router);
   return(0);
}

/* Set the system id or processor board id in the eeprom */
int c7200_set_system_id(c7200_t *router,char *id)
{
  // 11 characters is enough. Array is 20 long
  strncpy(router->board_id,id,13);
  // Make sure it is null terminated
  router->board_id[13] = 0x00;
  c7200_refresh_systemid(router);
  return 0;
}
int c7200_refresh_systemid(c7200_t *router)
{
  //fprintf(stderr,"Starting mp dump\n");
  //cisco_eeprom_dump(&router->mp_eeprom);
  //fprintf(stderr,"Starting cpu dump\n");
  //cisco_eeprom_dump(&router->cpu_eeprom);

  if (router->board_id[0] == 0x00) return(0);

  m_uint8_t buf[11];
  if (  (!strcmp("npe-100",router->npe_driver->npe_type))
      ||(!strcmp("npe-140",router->npe_driver->npe_type))
      ||(!strcmp("npe-175",router->npe_driver->npe_type))
      ||(!strcmp("npe-200",router->npe_driver->npe_type))
      ||(!strcmp("npe-225",router->npe_driver->npe_type))
      ||(!strcmp("npe-300",router->npe_driver->npe_type)))
  {
    //parse_board_id(buf,"4279256517",4);
    parse_board_id(buf,router->board_id,4);
    cisco_eeprom_set_region(&router->mp_eeprom ,4,buf,4);
    cisco_eeprom_set_region(&router->cpu_eeprom,4,buf,4);
    //fprintf(stderr,"Starting post mp dump\n");
    //cisco_eeprom_dump(&router->mp_eeprom);
    //fprintf(stderr,"Starting post cpu dump\n");
    //cisco_eeprom_dump(&router->cpu_eeprom);
    return (0);
  }
  return (-1);
}

/* Set chassis MAC address */
int c7200_midplane_set_mac_addr(c7200_t *router,char *mac_addr)
{
   if (parse_mac_addr(&router->mac_addr,mac_addr) == -1) {
      vm_error(router->vm,"unable to parse MAC address '%s'.\n",mac_addr);
      return(-1);
   }

   /* Set the chassis base MAC address */
   c7200_burn_mac_addr(router,&router->mac_addr);
   return(0);
}

/* Create the main PCI bus for a GT64010 based system */
static int c7200_init_gt64010(c7200_t *router)
{   
   vm_instance_t *vm = router->vm;

   if (!(vm->pci_bus[0] = pci_bus_create("MB0/MB1/MB2",0))) {
      vm_error(vm,"unable to create PCI data.\n");
      return(-1);
   }
   
   return(dev_gt64010_init(vm,"gt64010",C7200_GT64K_ADDR,0x1000,
                           C7200_GT64K_IRQ));
}

/* Create the two main PCI busses for a GT64120 based system */
static int c7200_init_gt64120(c7200_t *router)
{
   vm_instance_t *vm = router->vm;

   vm->pci_bus[0] = pci_bus_create("MB0/MB1",0);
   vm->pci_bus[1] = pci_bus_create("MB2",0);

   if (!vm->pci_bus[0] || !vm->pci_bus[1]) {
      vm_error(vm,"unable to create PCI data.\n");
      return(-1);
   }
   
   return(dev_gt64120_init(vm,"gt64120",C7200_GT64K_ADDR,0x1000,
                           C7200_GT64K_IRQ));
}

/* Create the two main PCI busses for a dual GT64120 system */
static int c7200_init_dual_gt64120(c7200_t *router)
{
   vm_instance_t *vm = router->vm;

   vm->pci_bus[0] = pci_bus_create("MB0/MB1",0);
   vm->pci_bus[1] = pci_bus_create("MB2",0);

   if (!vm->pci_bus[0] || !vm->pci_bus[1]) {
      vm_error(vm,"unable to create PCI data.\n",vm->name);
      return(-1);
   }
   
   /* Initialize the first GT64120 at 0x14000000 */
   if (dev_gt64120_init(vm,"gt64120(1)",C7200_GT64K_ADDR,0x1000,
                        C7200_GT64K_IRQ) == -1)
      return(-1);

   /* Initialize the second GT64120 at 0x15000000 */
   if (dev_gt64120_init(vm,"gt64120(2)",C7200_GT64K_SEC_ADDR,0x1000,
                        C7200_GT64K_IRQ) == -1)
      return(-1);

   return(0);
}

/* Create the two main PCI busses for a MV64460 based system */
static int c7200_init_mv64460(c7200_t *router)
{
   vm_instance_t *vm = router->vm;

   vm->pci_bus[0] = pci_bus_create("MB0/MB1",3);
   vm->pci_bus[1] = pci_bus_create("MB2",0);

   if (!vm->pci_bus[0] || !vm->pci_bus[1]) {
      vm_error(vm,"unable to create PCI data.\n");
      return(-1);
   }

   return(dev_mv64460_init(vm,"mv64460",C7200_G2_MV64460_ADDR,0x10000));
}

/* Create the PA PCI busses */
static int c7200_pa_create_pci_busses(c7200_t *router)
{   
   vm_instance_t *vm = router->vm;
   char bus_name[128];
   int i;

   for(i=1;i<C7200_MAX_PA_BAYS;i++) {
      snprintf(bus_name,sizeof(bus_name),"PA Slot %d",i);
      vm->pci_bus_pool[i] = pci_bus_create(bus_name,-1);

      if (!vm->pci_bus_pool[i])
         return(-1);
   }

   return(0);
}

/* Create a PA bridge, depending on the midplane */
static int c7200_pa_init_pci_bridge(c7200_t *router,u_int pa_bay,
                                    struct pci_bus *pci_bus,int pci_device)
{
   struct pci_bus *pa_bus;

   pa_bus = router->vm->slots_pci_bus[pa_bay];

   switch(router->midplane_version) {
      case 0:
      case 1:
         dev_dec21050_init(pci_bus,pci_device,pa_bus);
         break;
      default:
         dev_dec21150_init(pci_bus,pci_device,pa_bus);
   }
   return(0);
}

/* 
 * Hidden "I/O" PCI bridge hack for PCMCIA controller.
 *
 * On NPE-175, NPE-225, NPE-300 and NPE-400, PCMCIA controller is
 * identified on PCI as Bus=2,Device=16. On NPE-G1, this is Bus=17,Device=16.
 *
 * However, I don't understand how the bridging between PCI bus 1 and 2
 * is done (16 and 17 on NPE-G1). 
 *
 * Maybe I'm missing something about PCI-to-PCI bridge mechanism, or there
 * is a special hidden device that does the job silently (it should be
 * visible on the PCI bus...)
 *
 * BTW, it works.
 */
static int 
c7200_create_io_pci_bridge(c7200_t *router,struct pci_bus *parent_bus)
{
   vm_instance_t *vm = router->vm;

   /* Create the PCI bus where the PCMCIA controller will seat */
   if (!(vm->pci_bus_pool[16] = pci_bus_create("I/O secondary bus",-1)))
      return(-1);

   /* Create the hidden bridge with "special" handling... */
   if (!(router->io_pci_bridge = pci_bridge_add(parent_bus)))
      return(-1);

   router->io_pci_bridge->skip_bus_check = TRUE;
   pci_bridge_map_bus(router->io_pci_bridge,vm->pci_bus_pool[16]);

   router->pcmcia_bus = vm->pci_bus_pool[16];
   return(0);
}

/* Initialize an NPE-100 board */
int c7200_init_npe100(c7200_t *router)
{   
   vm_instance_t *vm = router->vm;
   int i;

   /* Set the processor type: R4600 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R4600);

   /* Initialize the Galileo GT-64010 system controller */
   if (c7200_init_gt64010(router) == -1)
      return(-1);

   /* PCMCIA controller is on bus 0 */
   router->pcmcia_bus = vm->pci_bus[0];

   /* Initialize the PA PCI busses */
   if (c7200_pa_create_pci_busses(router) == -1)
      return(-1);

   /* Create PCI busses for PA Bays 1,3,5 and PA Bays 2,4,6 */
   vm->pci_bus_pool[24] = pci_bus_create("PA Slots 1,3,5",-1);
   vm->pci_bus_pool[25] = pci_bus_create("PA Slots 2,4,6",-1);

   /* PCI bridges (MB0/MB1, MB0/MB2) */
   dev_dec21050_init(vm->pci_bus[0],1,NULL);
   dev_dec21050_init(vm->pci_bus[0],2,vm->pci_bus_pool[24]);
   dev_dec21050_init(vm->pci_bus[0],3,NULL);
   dev_dec21050_init(vm->pci_bus[0],4,vm->pci_bus_pool[25]);

   /* Map the PA PCI busses */
   vm->slots_pci_bus[0] = vm->pci_bus[0];

   for(i=1;i<C7200_MAX_PA_BAYS;i++)
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i];

   /* PCI bridges for PA Bays 1 to 6 */
   c7200_pa_init_pci_bridge(router,1,vm->pci_bus_pool[24],1);
   c7200_pa_init_pci_bridge(router,3,vm->pci_bus_pool[24],2);
   c7200_pa_init_pci_bridge(router,5,vm->pci_bus_pool[24],3);

   c7200_pa_init_pci_bridge(router,2,vm->pci_bus_pool[25],1);
   c7200_pa_init_pci_bridge(router,4,vm->pci_bus_pool[25],2);
   c7200_pa_init_pci_bridge(router,6,vm->pci_bus_pool[25],3);
   return(0);
}

/* Initialize an NPE-150 board */
int c7200_init_npe150(c7200_t *router)
{
   vm_instance_t *vm = router->vm;
   m_uint32_t bank_size;
   int i;

   /* Set the processor type: R4700 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R4700);

   /* Initialize the Galileo GT-64010 system controller */
   if (c7200_init_gt64010(router) == -1)
      return(-1);

   /* PCMCIA controller is on bus 0 */
   router->pcmcia_bus = vm->pci_bus[0];

   /* Initialize the PA PCI busses */
   if (c7200_pa_create_pci_busses(router) == -1)
      return(-1);

   /* Create PCI busses for PA Bays 1,3,5 and PA Bays 2,4,6 */
   vm->pci_bus_pool[24] = pci_bus_create("PA Slots 1,3,5",-1);
   vm->pci_bus_pool[25] = pci_bus_create("PA Slots 2,4,6",-1);

   /* PCI bridges (MB0/MB1, MB0/MB2) */
   dev_dec21050_init(vm->pci_bus[0],1,NULL);
   dev_dec21050_init(vm->pci_bus[0],2,vm->pci_bus_pool[24]);
   dev_dec21050_init(vm->pci_bus[0],3,NULL);
   dev_dec21050_init(vm->pci_bus[0],4,vm->pci_bus_pool[25]);

   /* Map the PA PCI busses */   
   vm->slots_pci_bus[0] = vm->pci_bus[0];

   for(i=1;i<C7200_MAX_PA_BAYS;i++)
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i];

   /* PCI bridges for PA Bays 1 to 6 */
   c7200_pa_init_pci_bridge(router,1,vm->pci_bus_pool[24],1);
   c7200_pa_init_pci_bridge(router,3,vm->pci_bus_pool[24],2);
   c7200_pa_init_pci_bridge(router,5,vm->pci_bus_pool[24],3);

   c7200_pa_init_pci_bridge(router,2,vm->pci_bus_pool[25],1);
   c7200_pa_init_pci_bridge(router,4,vm->pci_bus_pool[25],2);
   c7200_pa_init_pci_bridge(router,6,vm->pci_bus_pool[25],3);

   /* Packet SRAM: 1 Mb */
   bank_size = 0x80000;

   dev_c7200_sram_init(vm,"sram0",C7200_SRAM_ADDR,bank_size,
                       vm->pci_bus_pool[24],0);

   dev_c7200_sram_init(vm,"sram1",C7200_SRAM_ADDR+bank_size,bank_size,
                       vm->pci_bus_pool[25],0);
   return(0);
}

/* Initialize an NPE-175 board */
int c7200_init_npe175(c7200_t *router)
{
   vm_instance_t *vm = router->vm;
   int i;

   /* Set the processor type: R5271 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R527x);

   /* Initialize the Galileo GT-64120 PCI controller */
   if (c7200_init_gt64120(router) == -1)
      return(-1);

   /* Initialize the PA PCI busses */
   if (c7200_pa_create_pci_busses(router) == -1)
      return(-1);

   /* Create PCI bus for PA Bay 0 (I/O Card, PCMCIA, Interfaces) */
   vm->pci_bus_pool[0] = pci_bus_create("PA Slot 0",-1);

   /* PCI bridge for I/O card device on MB0 */
   dev_dec21150_init(vm->pci_bus[0],1,vm->pci_bus_pool[0]);

   /* Create the hidden "I/O" PCI bridge for PCMCIA controller */
   c7200_create_io_pci_bridge(router,vm->pci_bus_pool[0]);

   /* Map the PA PCI busses */
   for(i=0;i<C7200_MAX_PA_BAYS;i++)
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i];

   /* PCI bridges for PA Bays 1 to 6 */
   c7200_pa_init_pci_bridge(router,1,vm->pci_bus[0],7);
   c7200_pa_init_pci_bridge(router,3,vm->pci_bus[0],8);
   c7200_pa_init_pci_bridge(router,5,vm->pci_bus[0],9);

   c7200_pa_init_pci_bridge(router,2,vm->pci_bus[1],7);
   c7200_pa_init_pci_bridge(router,4,vm->pci_bus[1],8);
   c7200_pa_init_pci_bridge(router,6,vm->pci_bus[1],9);

   /* Enable PEM EEPROM */
   c7200_pem_set_eeprom(router);
   return(0);
}

/* Initialize an NPE-200 board */
int c7200_init_npe200(c7200_t *router)
{
   vm_instance_t *vm = router->vm;
   m_uint32_t bank_size;
   int i;

   /* Set the processor type: R5000 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R5000);

   /* Initialize the Galileo GT-64010 PCI controller */
   if (c7200_init_gt64010(router) == -1)
      return(-1);

   /* PCMCIA controller is on bus 0 */
   router->pcmcia_bus = vm->pci_bus[0];

   /* Initialize the PA PCI busses */
   if (c7200_pa_create_pci_busses(router) == -1)
      return(-1);

   /* Create PCI busses for PA Bays 1,3,5 and PA Bays 2,4,6 */
   vm->pci_bus_pool[24] = pci_bus_create("PA Slots 1,3,5",-1);
   vm->pci_bus_pool[25] = pci_bus_create("PA Slots 2,4,6",-1);

   /* PCI bridges (MB0/MB1, MB0/MB2) */
   dev_dec21050_init(vm->pci_bus[0],1,NULL);
   dev_dec21050_init(vm->pci_bus[0],2,vm->pci_bus_pool[24]);
   dev_dec21050_init(vm->pci_bus[0],3,NULL);
   dev_dec21050_init(vm->pci_bus[0],4,vm->pci_bus_pool[25]);

   /* Map the PA PCI busses */
   vm->slots_pci_bus[0] = vm->pci_bus[0];

   for(i=1;i<C7200_MAX_PA_BAYS;i++)
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i];

   /* PCI bridges for PA Bays 1 to 6 */
   c7200_pa_init_pci_bridge(router,1,vm->pci_bus_pool[24],1);
   c7200_pa_init_pci_bridge(router,3,vm->pci_bus_pool[24],2);
   c7200_pa_init_pci_bridge(router,5,vm->pci_bus_pool[24],3);

   c7200_pa_init_pci_bridge(router,2,vm->pci_bus_pool[25],1);
   c7200_pa_init_pci_bridge(router,4,vm->pci_bus_pool[25],2);
   c7200_pa_init_pci_bridge(router,6,vm->pci_bus_pool[25],3);

   /* Packet SRAM: 4 Mb */
   bank_size = 0x200000;

   dev_c7200_sram_init(vm,"sram0",C7200_SRAM_ADDR,bank_size,
                       vm->pci_bus_pool[24],0);

   dev_c7200_sram_init(vm,"sram1",C7200_SRAM_ADDR+bank_size,bank_size,
                       vm->pci_bus_pool[25],0);
   return(0);
}

/* Initialize an NPE-225 board */
int c7200_init_npe225(c7200_t *router)
{   
   vm_instance_t *vm = router->vm;
   int i;

   /* Set the processor type: R5271 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R527x);

   /* Initialize the Galileo GT-64120 PCI controller */
   if (c7200_init_gt64120(router) == -1)
      return(-1);

   /* Initialize the PA PCI busses */
   if (c7200_pa_create_pci_busses(router) == -1)
      return(-1);

   /* Create PCI bus for PA Bay 0 (I/O Card, PCMCIA, Interfaces) */
   vm->pci_bus_pool[0] = pci_bus_create("PA Slot 0",-1);

   /* PCI bridge for I/O card device on MB0 */
   dev_dec21150_init(vm->pci_bus[0],1,vm->pci_bus_pool[0]);

   /* Create the hidden "I/O" PCI bridge for PCMCIA controller */
   c7200_create_io_pci_bridge(router,vm->pci_bus_pool[0]);

   /* Map the PA PCI busses */
   for(i=0;i<C7200_MAX_PA_BAYS;i++)
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i];

   /* PCI bridges for PA Bays 1 to 6 */
   c7200_pa_init_pci_bridge(router,1,vm->pci_bus[0],7);
   c7200_pa_init_pci_bridge(router,3,vm->pci_bus[0],8);
   c7200_pa_init_pci_bridge(router,5,vm->pci_bus[0],9);

   c7200_pa_init_pci_bridge(router,2,vm->pci_bus[1],7);
   c7200_pa_init_pci_bridge(router,4,vm->pci_bus[1],8);
   c7200_pa_init_pci_bridge(router,6,vm->pci_bus[1],9);

   /* Enable PEM EEPROM */
   c7200_pem_set_eeprom(router);
   return(0);
}

/* Initialize an NPE-300 board */
int c7200_init_npe300(c7200_t *router)
{
   vm_instance_t *vm = router->vm;
   int i;

   /* Set the processor type: R7000 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R7000);

   /* 32 Mb of I/O memory */
   vm->iomem_size = 32;
   dev_ram_init(vm,"iomem",vm->ram_mmap,TRUE,NULL,vm->sparse_mem,
                C7200_IOMEM_ADDR,32*1048576);

   /* Initialize the two Galileo GT-64120 system controllers */
   if (c7200_init_dual_gt64120(router) == -1)
      return(-1);

   /* Initialize the PA PCI busses */
   if (c7200_pa_create_pci_busses(router) == -1)
      return(-1);

   /* Create PCI bus for PA Bay 0 (I/O Card, PCMCIA, Interfaces) */
   vm->pci_bus_pool[0] = pci_bus_create("PA Slot 0",-1);

   /* Create PCI busses for PA Bays 1,3,5 and PA Bays 2,4,6 */
   vm->pci_bus_pool[24] = pci_bus_create("PA Slots 1,3,5",-1);
   vm->pci_bus_pool[25] = pci_bus_create("PA Slots 2,4,6",-1);

   /* PCI bridge for I/O card device on MB0 */
   dev_dec21150_init(vm->pci_bus[0],1,vm->pci_bus_pool[0]);

   /* Create the hidden "I/O" PCI bridge for PCMCIA controller */
   c7200_create_io_pci_bridge(router,vm->pci_bus_pool[0]);

   /* PCI bridges for PA PCI "Head" Busses */
   dev_dec21150_init(vm->pci_bus[0],2,vm->pci_bus_pool[24]);
   dev_dec21150_init(vm->pci_bus[1],1,vm->pci_bus_pool[25]);

   /* Map the PA PCI busses */
   for(i=0;i<C7200_MAX_PA_BAYS;i++)
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i];

   /* PCI bridges for PA Bays 1 to 6 */
   c7200_pa_init_pci_bridge(router,1,vm->pci_bus_pool[24],1);
   c7200_pa_init_pci_bridge(router,3,vm->pci_bus_pool[24],2);
   c7200_pa_init_pci_bridge(router,5,vm->pci_bus_pool[24],3);

   c7200_pa_init_pci_bridge(router,2,vm->pci_bus_pool[25],1);
   c7200_pa_init_pci_bridge(router,4,vm->pci_bus_pool[25],2);
   c7200_pa_init_pci_bridge(router,6,vm->pci_bus_pool[25],3);
   return(0);
}

/* Initialize an NPE-400 board */
int c7200_init_npe400(c7200_t *router)
{
   vm_instance_t *vm = router->vm;
   int i;

   /* Set the processor type: R7000 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R7000);

   /* 
    * Add supplemental memory (as "iomem") if we have more than 256 Mb.
    */
   if (VM_C7200(vm)->npe400_ram_size > C7200_BASE_RAM_LIMIT) {
      vm->iomem_size = VM_C7200(vm)->npe400_ram_size - C7200_BASE_RAM_LIMIT;
      vm->ram_size = C7200_BASE_RAM_LIMIT;
      dev_ram_init(vm,"iomem",vm->ram_mmap,TRUE,NULL,vm->sparse_mem,
                   C7200_IOMEM_ADDR,vm->iomem_size*1048576);
   }
   else {
      vm->iomem_size = 0;
   }

   /* Initialize the Galileo GT-64120 system controller */
   if (c7200_init_gt64120(router) == -1)
      return(-1);

   /* Initialize the PA PCI busses */
   if (c7200_pa_create_pci_busses(router) == -1)
      return(-1);

   /* Create PCI bus for PA Bay 0 (I/O Card, PCMCIA, Interfaces) */
   vm->pci_bus_pool[0] = pci_bus_create("PA Slot 0",-1);

   /* PCI bridge for I/O card device on MB0 */
   dev_dec21050_init(vm->pci_bus[0],1,vm->pci_bus_pool[0]);

   /* Create the hidden "I/O" PCI bridge for PCMCIA controller */
   c7200_create_io_pci_bridge(router,vm->pci_bus_pool[0]);

   /* Map the PA PCI busses */
   for(i=0;i<C7200_MAX_PA_BAYS;i++)
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i];

   /* PCI bridges for PA Bays 1 to 6 */
   c7200_pa_init_pci_bridge(router,1,vm->pci_bus[0],7);
   c7200_pa_init_pci_bridge(router,3,vm->pci_bus[0],8);
   c7200_pa_init_pci_bridge(router,5,vm->pci_bus[0],9);

   c7200_pa_init_pci_bridge(router,2,vm->pci_bus[1],7);
   c7200_pa_init_pci_bridge(router,4,vm->pci_bus[1],8);
   c7200_pa_init_pci_bridge(router,6,vm->pci_bus[1],9);
   return(0);
}

/* Initialize an NPE-G1 board (XXX not working) */
int c7200_init_npeg1(c7200_t *router)
{     
   vm_instance_t *vm = router->vm;
   int i;

   /* Just some tests */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_BCM1250);
   vm->pci_bus[0] = pci_bus_create("HT/PCI bus",0);

   /* SB-1 System control devices */
   dev_sb1_init(vm);

   /* SB-1 I/O devices */
   dev_sb1_io_init(vm,C7200_DUART_IRQ);

   /* SB-1 PCI bus configuration zone */
   dev_sb1_pci_init(vm,"pci_cfg",0xFE000000ULL);

   /* Initialize the PA PCI busses */
   if (c7200_pa_create_pci_busses(router) == -1)
      return(-1);

   /* Create PCI bus for PA Bay 0 (I/O Card, PCMCIA, Interfaces) */
   vm->pci_bus_pool[0] = pci_bus_create("PA Slot 0",-1);

   /* Create PCI busses for PA Bays 1,3,5 and PA Bays 2,4,6 */
   vm->pci_bus_pool[24] = pci_bus_create("PA Slots 1,3,5",-1);
   vm->pci_bus_pool[25] = pci_bus_create("PA Slots 2,4,6",-1);

   /* HyperTransport/PCI bridges */
   dev_ap1011_init(vm->pci_bus_pool[28],0,NULL);
   dev_ap1011_init(vm->pci_bus_pool[28],1,vm->pci_bus_pool[24]);
   dev_ap1011_init(vm->pci_bus_pool[28],2,vm->pci_bus_pool[25]);

   /* PCI bridge for I/O card device on MB0 */
   dev_dec21150_init(vm->pci_bus[0],3,vm->pci_bus_pool[0]);

   /* Create the hidden "I/O" PCI bridge for PCMCIA controller */
   c7200_create_io_pci_bridge(router,vm->pci_bus_pool[0]);

   /* Map the PA PCI busses */
   vm->slots_pci_bus[0] = vm->pci_bus[0];

   for(i=1;i<C7200_MAX_PA_BAYS;i++)
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i];

   /* PCI bridges for PA Bays 1 to 6 */
   c7200_pa_init_pci_bridge(router,1,vm->pci_bus_pool[24],1);
   c7200_pa_init_pci_bridge(router,3,vm->pci_bus_pool[24],2);
   c7200_pa_init_pci_bridge(router,5,vm->pci_bus_pool[24],3);

   c7200_pa_init_pci_bridge(router,2,vm->pci_bus_pool[25],1);
   c7200_pa_init_pci_bridge(router,4,vm->pci_bus_pool[25],2);
   c7200_pa_init_pci_bridge(router,6,vm->pci_bus_pool[25],3);
   return(0);
}

/* Initialize an NPE-G2 board (XXX not working) */
int c7200_init_npeg2(c7200_t *router)
{     
   vm_instance_t *vm = router->vm;
   int i;

   /* Set the processor type: PowerPC G4 */
   ppc32_set_pvr(CPU_PPC32(vm->boot_cpu),0x80040201);

   /* Initialize the PA PCI busses */
   if (c7200_pa_create_pci_busses(router) == -1)
      return(-1);

   /* Create PCI bus for PA Bay 0 (I/O Card, PCMCIA, Interfaces) */
   vm->pci_bus_pool[0] = pci_bus_create("PA Slot 0",-1);

   /* PCI bridge for I/O card device on MB0 */
   dev_plx6520cb_init(vm->pci_bus[1],3,vm->pci_bus_pool[0]);

   /* Create PCI busses for PA Bays 1,3,5 and PA Bays 2,4,6 */
   vm->pci_bus_pool[24] = pci_bus_create("PA Slots 1,3,5",-1);
   vm->pci_bus_pool[25] = pci_bus_create("PA Slots 2,4,6",-1);

   dev_plx6520cb_init(vm->pci_bus[0],1,vm->pci_bus_pool[24]);
   dev_plx6520cb_init(vm->pci_bus[0],2,vm->pci_bus_pool[25]);

   /* Create the hidden "I/O" PCI bridge for PCMCIA controller */
   c7200_create_io_pci_bridge(router,vm->pci_bus_pool[0]);

   /* Map the PA PCI busses */
   vm->slots_pci_bus[0] = vm->pci_bus_pool[0];

   for(i=1;i<C7200_MAX_PA_BAYS;i++)
      vm->slots_pci_bus[i] = vm->pci_bus_pool[i];

   /* PCI bridges for PA Bays 1 to 7 */
   c7200_pa_init_pci_bridge(router,1,vm->pci_bus_pool[24],1);
   c7200_pa_init_pci_bridge(router,3,vm->pci_bus_pool[24],2);
   c7200_pa_init_pci_bridge(router,5,vm->pci_bus_pool[24],3);

   c7200_pa_init_pci_bridge(router,2,vm->pci_bus_pool[25],1);
   c7200_pa_init_pci_bridge(router,4,vm->pci_bus_pool[25],2);
   c7200_pa_init_pci_bridge(router,6,vm->pci_bus_pool[25],3);

   c7200_pa_init_pci_bridge(router,7,vm->pci_bus_pool[0],8);


#if 0 /* too late at this stage... */
   /* Add a fake slot (8) for NPE-G2 Ethernet ports */
   if (vm_slot_add_binding(router->vm,"NPE-G2",8,0) == -1)
      printf("unable to set slot 8\n");
#endif

   return(0);
}

/* Show C7200 hardware info */
void c7200_show_hardware(c7200_t *router)
{
   vm_instance_t *vm = router->vm;

   printf("C7200 instance '%s' (id %d):\n",vm->name,vm->instance_id);

   printf("  VM Status  : %d\n",vm->status);
   printf("  RAM size   : %u Mb\n",vm->ram_size);
   printf("  IOMEM size : %u Mb\n",vm->iomem_size);
   printf("  NVRAM size : %u Kb\n",vm->nvram_size);
   printf("  NPE model  : %s\n",router->npe_driver->npe_type);
   printf("  Midplane   : %s\n",router->midplane_type);
   printf("  IOS image  : %s\n\n",vm->ios_image);

   if (vm->debug_level > 0) {
      dev_show_list(vm);
      pci_dev_show_list(vm->pci_bus[0]);
      pci_dev_show_list(vm->pci_bus[1]);
      printf("\n");
   }
}

/* Initialize default parameters for a C7200 */
static void c7200_init_defaults(c7200_t *router)
{
   vm_instance_t *vm = router->vm;
   n_eth_addr_t *m;
   m_uint16_t pid;

   /* Set platform slots characteristics */
   vm->nr_slots   = C7200_MAX_PA_BAYS;
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

   router->board_id[0] = 0x00;

   c7200_init_sys_eeprom_groups(router);
   c7200_init_mp_eeprom_groups(router);
   c7200_npe_set_type(router,C7200_DEFAULT_NPE_TYPE);
   c7200_midplane_set_type(router,C7200_DEFAULT_MIDPLANE);

   vm->ram_mmap        = C7200_DEFAULT_RAM_MMAP;
   vm->ram_size        = C7200_DEFAULT_RAM_SIZE;
   vm->rom_size        = C7200_DEFAULT_ROM_SIZE;
   vm->nvram_size      = C7200_DEFAULT_NVRAM_SIZE;
   vm->iomem_size      = 0;
   vm->conf_reg_setup  = C7200_DEFAULT_CONF_REG;
   vm->clock_divisor   = C7200_DEFAULT_CLOCK_DIV;
   vm->nvram_rom_space = C7200_NVRAM_ROM_RES_SIZE;

   vm->pcmcia_disk_size[0] = C7200_DEFAULT_DISK0_SIZE;
   vm->pcmcia_disk_size[1] = C7200_DEFAULT_DISK1_SIZE;
}

/* Run the checklist */
static int c7200_checklist(c7200_t *router)
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
static int c7200_init_platform_pa(c7200_t *router)
{
#if 1 /* FIXME - for a later release */
   /* Use a C7200-IO-FE by default in slot 0 if an I/O card is required */
   if (router->npe_driver->iocard_required && !vm_slot_active(router->vm,0,0))
      vm_slot_add_binding(router->vm,"C7200-IO-FE",0,0);
#endif

   return(vm_slot_init_all(router->vm));
}

/* Initialize the C7200 Platform (MIPS) */
static int c7200m_init_platform(c7200_t *router)
{
   struct vm_instance *vm = router->vm;
   cpu_mips_t *cpu0; 
   cpu_gen_t *gen0;

   /* Copy config register setup into "active" config register */
   vm->conf_reg = vm->conf_reg_setup;

   /* Create Console and AUX ports */
   vm_init_vtty(vm);

   /* Check that the amount of RAM is valid */
   if (vm->ram_size > router->npe_driver->max_ram_size) {
      vm_error(vm,"%u is not a valid RAM size for this NPE. "
               "Fallback to %u Mb.\n\n",
               vm->ram_size,router->npe_driver->max_ram_size);
   
      vm->ram_size = router->npe_driver->max_ram_size;
   }

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
   cpu0->irq_idle_preempt[C7200_NETIO_IRQ] = TRUE;
   cpu0->irq_idle_preempt[C7200_GT64K_IRQ] = TRUE;

   /* Copy some parameters from VM to CPU0 (idle PC, ...) */
   cpu0->idle_pc = vm->idle_pc;

   if (vm->timer_irq_check_itv)
      cpu0->timer_irq_check_itv = vm->timer_irq_check_itv;

   /*
    * On the C7200, bit 33 of physical addresses is used to bypass L2 cache.
    * We clear it systematically.
    */
   cpu0->addr_bus_mask = C7200_ADDR_BUS_MASK;

   /* Remote emulator control */
   dev_remote_control_init(vm,0x16000000,0x1000);

   /* Bootflash (8 Mb) */
   dev_bootflash_init(vm,"bootflash","c7200-bootflash-8mb",
                      C7200_BOOTFLASH_ADDR);

   /* NVRAM and calendar */
   dev_nvram_init(vm,"nvram",router->npe_driver->nvram_addr,
                  vm->nvram_size*1024,&vm->conf_reg);

   /* Bit-bucket zone */
   dev_zero_init(vm,"zero",C7200_BITBUCKET_ADDR,0xc00000);

   /* Initialize the NPE board */
   if (router->npe_driver->npe_init(router) == -1)
      return(-1);

   /* Initialize RAM */
   vm_ram_init(vm,0x00000000ULL);

   /* Initialize ROM */
   if (!vm->rom_filename) {
      /* use embedded ROM */
      dev_rom_init(vm,"rom",C7200_ROM_ADDR,vm->rom_size*1048576,
                   mips64_microcode,mips64_microcode_len);
   } else {
      /* use alternate ROM */
      dev_ram_init(vm,"rom",TRUE,TRUE,NULL,FALSE,
                   C7200_ROM_ADDR,vm->rom_size*1048576);
   }

   /* Byte swapping */
   dev_bswap_init(vm,"mem_bswap",C7200_BSWAP_ADDR,1024*1048576,0x00000000ULL);

   /* PCI IO space */
   if (!(vm->pci_io_space = pci_io_data_init(vm,C7200_PCI_IO_ADDR)))
      return(-1);

   /* Cirrus Logic PD6729 (PCI-to-PCMCIA host adapter) */
   dev_clpd6729_init(vm,router->pcmcia_bus,
                     router->npe_driver->clpd6729_pci_dev,
                     vm->pci_io_space,0x402,0x403);

   /* Initialize the Port Adapters */
   if (c7200_init_platform_pa(router) == -1)
      return(-1);

   /* Verify the check list */
   if (c7200_checklist(router) == -1)
      return(-1);

   /* Midplane FPGA */
   dev_c7200_mpfpga_init(router,C7200_MPFPGA_ADDR,0x1000);

   /* IO FPGA */
   if (dev_c7200_iofpga_init(router,C7200_IOFPGA_ADDR,0x1000) == -1)
      return(-1);

   /* Show device list */
   c7200_show_hardware(router);
   return(0);
}

/* Initialize the C7200 Platform (PowerPC) */
static int c7200p_init_platform(c7200_t *router)
{
   struct vm_instance *vm = router->vm;
   cpu_ppc_t *cpu0; 
   cpu_gen_t *gen0;
   vm_obj_t *obj;

   /* Copy config register setup into "active" config register */
   vm->conf_reg = vm->conf_reg_setup;

   /* Create Console and AUX ports */
   vm_init_vtty(vm);

   /* Check that the amount of RAM is valid */
   if (vm->ram_size > router->npe_driver->max_ram_size) {
      vm_error(vm,"%u is not a valid RAM size for this NPE. "
               "Fallback to %u Mb.\n\n",
               vm->ram_size,router->npe_driver->max_ram_size);
   
      vm->ram_size = router->npe_driver->max_ram_size;
   }

   /* Create a CPU group */
   vm->cpu_group = cpu_group_create("System CPU");

   /* Initialize the virtual PowerPC processor */
   if (!(gen0 = cpu_create(vm,CPU_TYPE_PPC32,0))) {
      vm_error(vm,"unable to create CPU0!\n");
      return(-1);
   }

   cpu0 = CPU_PPC32(gen0);

   /* Add this CPU to the system CPU group */
   cpu_group_add(vm->cpu_group,gen0);
   vm->boot_cpu = gen0;

   /* Mark the Network IO interrupt as high priority */
   vm->irq_idle_preempt[C7200_NETIO_IRQ] = TRUE;

   /* Copy some parameters from VM to CPU0 (idle PC, ...) */
   cpu0->idle_pc = vm->idle_pc;

   if (vm->timer_irq_check_itv)
      cpu0->timer_irq_check_itv = vm->timer_irq_check_itv;

   /* Initialize the Marvell MV-64460 system controller */
   if (c7200_init_mv64460(router) == -1)
      return(-1);

   if (!(obj = vm_object_find(router->vm,"mv64460")))
      return(-1);

   router->mv64460_sysctr = obj->data;

   /* Remote emulator control */
   dev_remote_control_init(vm,0xf6000000,0x1000);

   /* Bootflash (64 Mb) */
   dev_bootflash_init(vm,"bootflash","c7200-bootflash-64mb",
                      C7200_G2_BOOTFLASH_ADDR);

   /* NVRAM and calendar */
   vm->nvram_size = C7200_G2_NVRAM_SIZE / 1024;
   dev_nvram_init(vm,"nvram",router->npe_driver->nvram_addr,
                  C7200_G2_NVRAM_SIZE,&vm->conf_reg);

   /* Initialize the NPE board */
   if (router->npe_driver->npe_init(router) == -1)
      return(-1);

   /* Initialize RAM */
   vm_ram_init(vm,0x00000000ULL);

   /* Initialize ROM */
   if (!vm->rom_filename) {
      /* use embedded ROM */
      dev_rom_init(vm,"rom",C7200_G2_ROM_ADDR,vm->rom_size*1048576,
                   ppc32_microcode,ppc32_microcode_len);
   } else {
      /* use alternate ROM */
      dev_ram_init(vm,"rom",TRUE,TRUE,NULL,FALSE,
                   C7200_G2_ROM_ADDR,vm->rom_size*1048576);
   }

   /* Byte swapping - FIXME */
   dev_bswap_init(vm,"mem_bswap0",C7200_G2_BSWAP_ADDR,32*1048576,
                  0x00000000ULL);
   //dev_bswap_init(vm,"mem_bswap0",C7200_G2_BSWAP_ADDR+0x10000000,32*1048576,
   //               0x00000000ULL);

   /* PCI IO space */
   if (!(vm->pci_io_space = pci_io_data_init(vm,C7200_G2_PCI_IO_ADDR)))
      return(-1);

   /* Cirrus Logic PD6729 (PCI-to-PCMCIA host adapter) */
   dev_clpd6729_init(vm,router->pcmcia_bus,
                     router->npe_driver->clpd6729_pci_dev,
                     vm->pci_io_space,0x402,0x403);

   /* Initialize the Port Adapters */
   if (c7200_init_platform_pa(router) == -1)
      return(-1);
   
   /* IO FPGA */
   if (dev_c7200_iofpga_init(router,C7200_G2_IOFPGA_ADDR,0x1000) == -1)
      return(-1);

   /* MP FPGA */
   if (dev_c7200_mpfpga_init(router,C7200_G2_MPFPGA_ADDR,0x10000) == -1)
      return(-1);

   /* 
    * If we have no i/o card in slot 0, the console is handled by 
    * the MV64460.
    */
   if (!c7200_slot0_iocard_present(router)) {
      vm_log(vm,"CONSOLE","console managed by NPE-G2 board\n");
      mv64460_sdma_bind_vtty(router->mv64460_sysctr,0,vm->vtty_con);
      mv64460_sdma_bind_vtty(router->mv64460_sysctr,1,vm->vtty_aux);
   }

   /* Show device list */
   c7200_show_hardware(router);
   return(0);
}

/* Boot the IOS image (MIPS) */
static int c7200m_boot_ios(c7200_t *router)
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
   printf("\nC7200 '%s': starting simulation (CPU0 PC=0x%llx), "
          "JIT %sabled.\n",
          vm->name,cpu->pc,vm->jit_use ? "en":"dis");

   vm_log(vm,"C7200_BOOT",
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

/* Boot the IOS image (PowerPC) */
static int c7200p_boot_ios(c7200_t *router)
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

   /* Load IOS image */
   if (ppc32_load_elf_image(cpu,vm->ios_image,
                            (vm->ghost_status == VM_GHOST_RAM_USE),
                            &vm->ios_entry_point) < 0)
   {
      vm_error(vm,"failed to load Cisco IOS image '%s'.\n",vm->ios_image);
      return(-1);
   }

   /* Launch the simulation */
   printf("\nC7200P '%s': starting simulation (CPU0 IA=0x%8.8x), "
          "JIT %sabled.\n",
          vm->name,cpu->ia,vm->jit_use ? "en":"dis");

   vm_log(vm,"C7200P_BOOT",
          "starting instance (CPU0 IA=0x%8.8x,idle_pc=0x%8.8x,JIT %s)\n",
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

/* Set an IRQ */
static void c7200m_set_irq(vm_instance_t *vm,u_int irq)
{
   c7200_t *router = VM_C7200(vm);
   cpu_mips_t *cpu0 = CPU_MIPS64(vm->boot_cpu);
   u_int slot,port;

   switch(irq) {
      case 0 ... 7:
         mips64_set_irq(cpu0,irq);

         if (cpu0->irq_idle_preempt[irq])
            cpu_idle_break_wait(cpu0->gen);
         break;

      case C7200_NETIO_IRQ_BASE ... C7200_NETIO_IRQ_END:
         c7200_net_irq_get_slot_port(irq,&slot,&port);
         dev_c7200_net_set_irq(router,slot,port);
         break;
   }
}

/* Clear an IRQ */
static void c7200m_clear_irq(vm_instance_t *vm,u_int irq)
{
   c7200_t *router = VM_C7200(vm);
   cpu_mips_t *cpu0 = CPU_MIPS64(vm->boot_cpu);
   u_int slot,port;

   switch(irq) {
      case 0 ... 7:
         mips64_clear_irq(cpu0,irq);
         break;

      case C7200_NETIO_IRQ_BASE ... C7200_NETIO_IRQ_END:
         c7200_net_irq_get_slot_port(irq,&slot,&port);
         dev_c7200_net_clear_irq(router,slot,port);
         break;
   }
}

/* Initialize a Cisco 7200 instance (MIPS) */
static int c7200m_init_instance(c7200_t *router)
{
   vm_instance_t *vm = router->vm;
   m_uint32_t rom_entry_point;
   cpu_mips_t *cpu0;

   /* Initialize the C7200 platform */
   if (c7200m_init_platform(router) == -1) {
      vm_error(vm,"unable to initialize the platform hardware.\n");
      return(-1);
   }

   /* IRQ routing */
   vm->set_irq = c7200m_set_irq;
   vm->clear_irq = c7200m_clear_irq;

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

   return(c7200m_boot_ios(router));
}

/* Set an IRQ */
static void c7200p_set_irq(vm_instance_t *vm,u_int irq)
{
   c7200_t *router = VM_C7200(vm);
   cpu_ppc_t *cpu0 = CPU_PPC32(vm->boot_cpu);
   u_int slot,port;

   switch(irq) {
      case C7200_VTIMER_IRQ:
         ppc32_trigger_timer_irq(cpu0);
         break;
      case C7200_DUART_IRQ:
         dev_mv64460_set_gpp_intr(router->mv64460_sysctr,10);
         break;
      case C7200_NETIO_IRQ:
         dev_mv64460_set_gpp_intr(router->mv64460_sysctr,24);
         break;
      case C7200_PA_MGMT_IRQ:
         dev_mv64460_set_gpp_intr(router->mv64460_sysctr,20);
         break;
      case C7200_OIR_IRQ:
         dev_mv64460_set_gpp_intr(router->mv64460_sysctr,0);
         break;
      case C7200_NETIO_IRQ_BASE ... C7200_NETIO_IRQ_END:
         c7200_net_irq_get_slot_port(irq,&slot,&port);
         dev_c7200_net_set_irq(router,slot,port);
         break;
   }

   if (vm->irq_idle_preempt[irq])
      cpu_idle_break_wait(cpu0->gen);
}

/* Clear an IRQ */
static void c7200p_clear_irq(vm_instance_t *vm,u_int irq)
{
   c7200_t *router = VM_C7200(vm);
   u_int slot,port;

   switch(irq) {
      case C7200_DUART_IRQ:
         dev_mv64460_clear_gpp_intr(router->mv64460_sysctr,10);
         break;
      case C7200_NETIO_IRQ:
         dev_mv64460_clear_gpp_intr(router->mv64460_sysctr,24);
         break;
      case C7200_PA_MGMT_IRQ:
         dev_mv64460_clear_gpp_intr(router->mv64460_sysctr,20);
         break;
      case C7200_OIR_IRQ: 
         dev_mv64460_clear_gpp_intr(router->mv64460_sysctr,0);
         break;
      case C7200_NETIO_IRQ_BASE ... C7200_NETIO_IRQ_END:
         c7200_net_irq_get_slot_port(irq,&slot,&port);
         dev_c7200_net_clear_irq(router,slot,port);
         break;
   }
}

/* Initialize a Cisco 7200 instance (PowerPC) */
static int c7200p_init_instance(c7200_t *router)
{
   vm_instance_t *vm = router->vm;
   m_uint32_t rom_entry_point;
   cpu_ppc_t *cpu0;
   int i;

   /* Initialize the C7200 platform */
   if (c7200p_init_platform(router) == -1) {
      vm_error(vm,"unable to initialize the platform hardware.\n");
      return(-1);
   }

   /* IRQ routing */
   vm->set_irq = c7200p_set_irq;
   vm->clear_irq = c7200p_clear_irq;

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

   /* Initialize the MMU (TEST) */
   for(i=0;i<PPC32_SR_NR;i++)
      cpu0->sr[i] = i << 16;

   /* The page table takes 2 Mb of memory */
   vm->ram_res_size = 2;
   ppc32_set_sdr1(cpu0,((vm->ram_size - 2) * 1048576) + 0x1F);
   ppc32_init_page_table(cpu0);
   ppc32_map_zone(cpu0,cpu0->sr[C7200_G2_BOOTFLASH_ADDR >> 28],
                  C7200_G2_BOOTFLASH_ADDR,C7200_G2_BOOTFLASH_ADDR,
                  64*1048576,0,0x02);

   ppc32_map_zone(cpu0,cpu0->sr[0xD8000000 >> 28],
                  0xD8000000,0xD8000000,0x400000,0,0x02);
   ppc32_map_zone(cpu0,cpu0->sr[0xDC000000 >> 28],
                  0xDC000000,0xDC000000,0x400000,0,0x02);

   /* FIXME */
   ppc32_map_zone(cpu0,cpu0->sr[0xDF000000 >> 28],
                  0xDF000000,0xDF000000,0x400000,0,0x02);

   /* INST */
   cpu0->bat[PPC32_IBAT_IDX][0].reg[0] = 0x00007FFE;
   cpu0->bat[PPC32_IBAT_IDX][0].reg[1] = 0x00000003;

   cpu0->bat[PPC32_IBAT_IDX][3].reg[0] = 0xF0001FFE;
   cpu0->bat[PPC32_IBAT_IDX][3].reg[1] = 0xF0000003;

   /* DATA */
   cpu0->bat[PPC32_DBAT_IDX][0].reg[0] = 0x00007FFE;
   cpu0->bat[PPC32_DBAT_IDX][0].reg[1] = 0x00000003;

   cpu0->bat[PPC32_DBAT_IDX][3].reg[0] = 0xF0001FFE;
   cpu0->bat[PPC32_DBAT_IDX][3].reg[1] = 0xF0000003;

   return(c7200p_boot_ios(router));
}

/* Initialize a Cisco 7200 instance */
static int c7200_init_instance(vm_instance_t *vm)
{
   c7200_t *router = VM_C7200(vm);

   switch(router->npe_driver->npe_family) {
      case C7200_NPE_FAMILY_MIPS:
         return(c7200m_init_instance(router));

      case C7200_NPE_FAMILY_PPC:
         return(c7200p_init_instance(router));
         
      default:
         vm_error(router->vm,"unsupported NPE family %d",
                  router->npe_driver->npe_family);
         return(-1);
   }
}

/* Stop a Cisco 7200 instance */
static int c7200_stop_instance(vm_instance_t *vm)
{
   printf("\nC7200 '%s': stopping simulation.\n",vm->name);
   vm_log(vm,"C7200_STOP","stopping simulation.\n");

   /* Stop all CPUs */
   if (vm->cpu_group != NULL) {
      vm_stop(vm);
      
      if (cpu_group_sync_state(vm->cpu_group) == -1) {
         vm_error(vm,"unable to sync with system CPUs.\n");
         return(-1);
      }
   }

   /* Free resources that were used during execution to emulate hardware */
   c7200_free_hw_ressources(VM_C7200(vm));
   vm_hardware_shutdown(vm);
   return(0);
}

/* Trigger an OIR event */
static int c7200_trigger_oir_event(c7200_t *router,u_int slot)
{
   switch(slot) {
      case 1 ... 6:
         router->oir_status[0] = 1 << slot;
         break;
      case 7:
         /* signal the OIR on slot 0, and set the "extended status" */
         router->oir_status[0] = 1 << 0;
         router->oir_status[1] = 1 << 24;
         break;
   }

   vm_set_irq(router->vm,C7200_OIR_IRQ);
   return(0);
}

/* Initialize a new PA while the virtual router is online (OIR) */
static int c7200_pa_init_online(vm_instance_t *vm,u_int slot,u_int subslot)
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
   c7200_trigger_oir_event(VM_C7200(vm),slot);
   return(0);
}

/* Stop a PA while the virtual router is online (OIR) */
static int c7200_pa_stop_online(vm_instance_t *vm,u_int slot,u_int subslot)
{   
   if (!slot) {
      vm_error(vm,"OIR not supported on slot 0.\n");
      return(-1);
   }

   /* The PA driver must be initialized */
   if (!vm_slot_get_card_ptr(vm,slot)) {
      vm_error(vm,"trying to shut down empty slot %u.\n",slot);
      return(-1);
   }

   /* Disable all NIOs to stop traffic forwarding */
   vm_slot_disable_all_nio(vm,slot);

   /* We can safely trigger the OIR event */
   c7200_trigger_oir_event(VM_C7200(vm),slot);

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
static u_int c7200_get_mac_addr_msb(void)
{
   return(0xCA);
}

/* Parse specific options for the Cisco 7200 platform */
static int c7200_cli_parse_options(vm_instance_t *vm,int option)
{
   c7200_t *router = VM_C7200(vm);

   switch(option) {
      /* NPE type */
      case 't':
         c7200_npe_set_type(router,optarg);
         break;

      /* Midplane type */
      case 'M':
         c7200_midplane_set_type(router,optarg);
         break;

      /* Set the base MAC address */
      case 'm':
         if (!c7200_midplane_set_mac_addr(router,optarg))
            printf("MAC address set to '%s'.\n",optarg);
         break;

      /* Set the System ID */
      case 'I':
         if (!c7200_set_system_id(router,optarg))
            printf("System ID set to '%s'.\n",optarg);
         break;

      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Show specific CLI options */
static void c7200_cli_show_options(vm_instance_t *vm)
{
   printf("  -t <npe_type>      : Select NPE type (default: \"%s\")\n"
          "  -M <midplane>      : Select Midplane (\"std\" or \"vxr\")\n"
          "  -p <pa_desc>       : Define a Port Adapter\n"
          "  -s <pa_nio>        : Bind a Network IO interface to a "
          "Port Adapter\n" 
          "  -I <serialno>      : Set Processor Board Serial Number\n",
          C7200_DEFAULT_NPE_TYPE);
}

/* Platform definition */
static vm_platform_t c7200_platform = {
   "c7200", "C7200", "7200",
   c7200_create_instance,
   c7200_delete_instance,
   c7200_init_instance,
   c7200_stop_instance,
   c7200_pa_init_online,
   c7200_pa_stop_online,
   c7200_nvram_extract_config,
   c7200_nvram_push_config,
   c7200_get_mac_addr_msb,
   c7200_save_config,
   c7200_cli_parse_options,
   c7200_cli_show_options,
   c7200_npe_show_drivers,
};

/* Register the c7200 platform */
int c7200_platform_register(void)
{
   if (vm_platform_register(&c7200_platform) == -1)
      return(-1);

   return(hypervisor_c7200_init(&c7200_platform));
}
