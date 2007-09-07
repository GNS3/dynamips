/*  
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Serial Network Modules.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

#include "utils.h"
#include "net.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_mueslix.h"
#include "dev_c3600.h"
#include "dev_c3600_bay.h"

/* ====================================================================== */
/* NM-4T                                                                  */
/* ====================================================================== */

/*
 * dev_c3600_nm_4t_init()
 *
 * Add a NM-4T network module into specified slot.
 */
int dev_c3600_nm_4t_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct nm_bay_info *bay_info;
   struct mueslix_data *data;
   u_int slot = card->slot_id;
   u_int chassis_id;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_nm("NM-4T"));
   c3600_set_slot_eeprom(VM_C3600(vm),slot,&card->eeprom);

   /* Get PCI bus info about this bay */
   chassis_id = c3600_chassis_get_id(VM_C3600(vm));
   bay_info = c3600_nm_get_bay_info(chassis_id,slot);

   if (!bay_info) {
      vm_error(vm,"unable to get info for NM bay %u\n",slot);
      return(-1);
   }

   /* Create the Mueslix chip */
   data = dev_mueslix_init(vm,card->dev_name,0,
                           card->pci_bus,bay_info->pci_device,
                           c3600_net_irq_for_slot_port(slot,0));
   if (!data) return(-1);

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* Remove a NM-4T from the specified slot */
int dev_c3600_nm_4t_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Remove the NM EEPROM */
   cisco_card_unset_eeprom(card);
   c3600_set_slot_eeprom(VM_C3600(vm),card->slot_id,NULL);

   /* Remove the mueslix driver */
   dev_mueslix_remove(card->drv_info);
   return(0);
}

/* Bind a Network IO descriptor to a specific port */
int dev_c3600_nm_4t_set_nio(vm_instance_t *vm,struct cisco_card *card,
                            u_int port_id,netio_desc_t *nio)
{
   struct mueslix_data *d = card->drv_info;

   if (!d || (port_id >= MUESLIX_NR_CHANNELS))
      return(-1);

   return(dev_mueslix_set_nio(d,port_id,nio));
}

/* Unbind a Network IO descriptor to a specific port */
int dev_c3600_nm_4t_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                              u_int port_id)
{
   struct mueslix_data *d = card->drv_info;

   if (!d || (port_id >= MUESLIX_NR_CHANNELS))
      return(-1);
   
   return(dev_mueslix_unset_nio(d,port_id));
}

/* NM-4T driver */
struct cisco_card_driver dev_c3600_nm_4t_driver = {
   "NM-4T", 1, 0,
   dev_c3600_nm_4t_init, 
   dev_c3600_nm_4t_shutdown,
   NULL,
   dev_c3600_nm_4t_set_nio,
   dev_c3600_nm_4t_unset_nio,
   NULL,
};
