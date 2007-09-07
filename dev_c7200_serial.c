/*  
 * Cisco router simulation platform.
 * Copyright (C) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Serial Interfaces (Mueslix).
 *
 * EEPROM types:
 *   - 0x0C: PA-4T+
 *   - 0x0D: PA-8T-V35
 *   - 0x0E: PA-8T-X21
 *   - 0x0F: PA-8T-232
 *   - 0x10: PA-2H (HSSI)
 *   - 0x40: PA-4E1G/120
 * 
 * It seems that the PA-8T is a combination of two PA-4T+.
 * 
 * Note: "debug serial mueslix" gives more technical info.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "net.h"
#include "net_io.h"
#include "ptask.h"
#include "dev_mueslix.h"
#include "dev_c7200.h"

/* ====================================================================== */
/* PA-4T+                                                                 */
/* ====================================================================== */

/*
 * dev_c7200_pa_4t_init()
 *
 * Add a PA-4T port adapter into specified slot.
 */
static int dev_c7200_pa_4t_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct mueslix_data *data;
   u_int slot = card->slot_id;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_pa("PA-4T+"));
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Create the Mueslix chip */
   data = dev_mueslix_init(vm,card->dev_name,1,
                           card->pci_bus,0,
                           c7200_net_irq_for_slot_port(slot,0));
   if (!data) return(-1);

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* Remove a PA-4T+ from the specified slot */
static int dev_c7200_pa_4t_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Remove the PA EEPROM */
   cisco_card_unset_eeprom(card);
   c7200_set_slot_eeprom(VM_C7200(vm),card->slot_id,NULL);
   
   /* Remove the Mueslix chip */
   dev_mueslix_remove(card->drv_info);
   return(0);
}

/* Bind a Network IO descriptor to a specific port */
static int dev_c7200_pa_4t_set_nio(vm_instance_t *vm,struct cisco_card *card,
                                   u_int port_id,netio_desc_t *nio)
{
   struct mueslix_data *data = card->drv_info;

   if (!data || (port_id >= MUESLIX_NR_CHANNELS))
      return(-1);

   return(dev_mueslix_set_nio(data,port_id,nio));
}

/* Unbind a Network IO descriptor to a specific port */
static int dev_c7200_pa_4t_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                                     u_int port_id)
{
   struct mueslix_data *data = card->drv_info;

   if (!data || (port_id >= MUESLIX_NR_CHANNELS))
      return(-1);
   
   return(dev_mueslix_unset_nio(data,port_id));
}

/* PA-4T+ driver */
struct cisco_card_driver dev_c7200_pa_4t_driver = {
   "PA-4T+", 1, 0,
   dev_c7200_pa_4t_init, 
   dev_c7200_pa_4t_shutdown,
   NULL,
   dev_c7200_pa_4t_set_nio,
   dev_c7200_pa_4t_unset_nio,
};

/* ====================================================================== */
/* PA-8T                                                                  */
/* ====================================================================== */

/* PA-8T data */
struct pa8t_data {
   struct mueslix_data *mueslix[2];
};

/*
 * dev_c7200_pa_8t_init()
 *
 * Add a PA-8T port adapter into specified slot.
 */
static int dev_c7200_pa_8t_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa8t_data *data;
   u_int slot = card->slot_id;

   /* Allocate the private data structure for the PA-8T */
   if (!(data = malloc(sizeof(*data)))) {
      vm_log(vm,"%s: out of memory\n",card->dev_name);
      return(-1);
   }

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_pa("PA-8T"));
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Create the 1st Mueslix chip */
   data->mueslix[0] = dev_mueslix_init(vm,card->dev_name,1,
                                       card->pci_bus,0,
                                       c7200_net_irq_for_slot_port(slot,0));
   if (!data->mueslix[0]) return(-1);

   /* Create the 2nd Mueslix chip */
   data->mueslix[1] = dev_mueslix_init(vm,card->dev_name,1,
                                       card->pci_bus,1,
                                       c7200_net_irq_for_slot_port(slot,1));
   if (!data->mueslix[1]) return(-1);

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* Remove a PA-8T from the specified slot */
static int dev_c7200_pa_8t_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa8t_data *data = card->drv_info;

   /* Remove the PA EEPROM */
   cisco_card_unset_eeprom(card);
   c7200_set_slot_eeprom(VM_C7200(vm),card->slot_id,NULL);

   /* Remove the two Mueslix chips */
   dev_mueslix_remove(data->mueslix[0]);
   dev_mueslix_remove(data->mueslix[1]);
   free(data);
   return(0);
}

/* Bind a Network IO descriptor to a specific port */
static int dev_c7200_pa_8t_set_nio(vm_instance_t *vm,struct cisco_card *card,
                                   u_int port_id,netio_desc_t *nio)
{
   struct pa8t_data *d = card->drv_info;

   if (!d || (port_id >= (MUESLIX_NR_CHANNELS*2)))
      return(-1);

   return(dev_mueslix_set_nio(d->mueslix[port_id>>2],(port_id&0x03),nio));
}

/* Bind a Network IO descriptor to a specific port */
static int dev_c7200_pa_8t_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                                     u_int port_id)
{
   struct pa8t_data *d = card->drv_info;

   if (!d || (port_id >= (MUESLIX_NR_CHANNELS*2)))
      return(-1);

   return(dev_mueslix_unset_nio(d->mueslix[port_id>>2],port_id&0x03));
}

/* PA-8T driver */
struct cisco_card_driver dev_c7200_pa_8t_driver = {
   "PA-8T", 1, 0,
   dev_c7200_pa_8t_init, 
   dev_c7200_pa_8t_shutdown, 
   NULL,
   dev_c7200_pa_8t_set_nio,
   dev_c7200_pa_8t_unset_nio,
};
