/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Dummy module for c7200 jacket card.
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
#include "dev_c7200.h"

/*
 * dev_c7200_jcpa_init()
 *
 * Add a c7200 jacket card into slot 0.
 */
static int dev_c7200_jcpa_init(vm_instance_t *vm,struct cisco_card *card)
{
   u_int slot = card->slot_id;

   if (slot != 0) {
      vm_error(vm,"cannot put C7200-JC-PA in PA bay %u!\n",slot);
      return(-1);
   }

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_pa("C7200-JC-PA"));
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Store dummy info... */
   card->drv_info = card;
   return(0);
}

/* Remove a jacket card from the specified slot */
static int dev_c7200_jcpa_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Remove the PA EEPROM */
   cisco_card_unset_eeprom(card);
   c7200_set_slot_eeprom(VM_C7200(vm),card->slot_id,NULL);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c7200_jcpa_set_nio(vm_instance_t *vm,struct cisco_card *card,
                                  u_int port_id,netio_desc_t *nio)
{
   /* network handling is done by slot 7 */
   return(0);
}

/* Unbind a Network IO descriptor */
static int dev_c7200_jcpa_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                                    u_int port_id)
{
   /* network handling is done by slot 7 */
   return(0);
}

/* Jacket card driver */
struct cisco_card_driver dev_c7200_jcpa_driver = {
   "C7200-JC-PA", 1, 0,
   dev_c7200_jcpa_init, 
   dev_c7200_jcpa_shutdown, 
   NULL,
   dev_c7200_jcpa_set_nio,
   dev_c7200_jcpa_unset_nio,
   NULL,
};
