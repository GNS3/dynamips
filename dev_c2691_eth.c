/*  
 * Cisco C2691 simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Ethernet Network Modules.
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
#include "dev_am79c971.h"
#include "dev_nm_16esw.h"
#include "dev_gt.h"
#include "dev_c2691.h"

/* Multi-Ethernet NM with Am79c971 chips */
struct nm_eth_data {
   u_int nr_port;
   struct am79c971_data *port[8];
};

/* Return sub-slot info for integrated WIC slots (on motherboard) */
static int dev_c2691_mb_get_sub_info(vm_instance_t *vm,struct cisco_card *card,
                                     u_int port_id,
                                     struct cisco_card_driver ***drv_array,
                                     u_int *subcard_type)
{
   /* 3 integrated WIC slots */
   if ((port_id & 0x0F) >= 3)
      return(-1);

   *drv_array = dev_c2691_mb_wic_drivers;
   *subcard_type = CISCO_CARD_TYPE_WIC;
   return(0);
}

/*
 * dev_c2691_nm_eth_init()
 *
 * Add an Ethernet Network Module into specified slot.
 */
static int dev_c2691_nm_eth_init(vm_instance_t *vm,struct cisco_card *card,
                                 int nr_port,int interface_type,
                                 const struct cisco_eeprom *eeprom)
{
   struct nm_eth_data *data;
   u_int slot = card->slot_id;
   int i;

   /* Allocate the private data structure */
   if (!(data = malloc(sizeof(*data)))) {
      vm_error(vm,"%s: out of memory.\n",card->dev_name);
      return(-1);
   }

   memset(data,0,sizeof(*data));
   data->nr_port = nr_port;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,eeprom);
   c2691_set_slot_eeprom(VM_C2691(vm),slot,&card->eeprom);

   /* Create the AMD Am971c971 chip(s) */
   for(i=0;i<data->nr_port;i++) {
      data->port[i] = dev_am79c971_init(vm,card->dev_name,interface_type,
                                        card->pci_bus,6+i,
                                        c2691_net_irq_for_slot_port(slot,i));
   }

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* Remove an Ethernet NM from the specified slot */
static int dev_c2691_nm_eth_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   struct nm_eth_data *data = card->drv_info;
   int i;

   /* Remove the NM EEPROM */
   cisco_card_unset_eeprom(card);
   c2691_set_slot_eeprom(VM_C2691(vm),card->slot_id,NULL);

   /* Remove the AMD Am79c971 chips */
   for(i=0;i<data->nr_port;i++)
      dev_am79c971_remove(data->port[i]);

   free(data);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c2691_nm_eth_set_nio(vm_instance_t *vm,struct cisco_card *card,
                                    u_int port_id,netio_desc_t *nio)
{
   struct nm_eth_data *d = card->drv_info;

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_am79c971_set_nio(d->port[port_id],nio);
   return(0);
}

/* Unbind a Network IO descriptor */
static int dev_c2691_nm_eth_unset_nio(vm_instance_t *vm,
                                      struct cisco_card *card,
                                      u_int port_id)
{
   struct nm_eth_data *d = card->drv_info;

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_am79c971_unset_nio(d->port[port_id]);
   return(0);
}


/* ====================================================================== */
/* NM-1FE-TX                                                              */
/* ====================================================================== */

/*
 * dev_c2691_nm_1fe_tx_init()
 *
 * Add a NM-1FE-TX Network Module into specified slot.
 */
static int dev_c2691_nm_1fe_tx_init(vm_instance_t *vm,struct cisco_card *card)
{
   return(dev_c2691_nm_eth_init(vm,card,1,AM79C971_TYPE_100BASE_TX,
                                cisco_eeprom_find_nm("NM-1FE-TX")));
}

/* ====================================================================== */
/* NM-16ESW                                                               */
/* ====================================================================== */

/* Add a NM-16ESW */
static int dev_c2691_nm_16esw_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct nm_16esw_data *data;
   u_int slot = card->slot_id;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_nm("NM-16ESW"));
   dev_nm_16esw_burn_mac_addr(vm,slot,&card->eeprom);
   c2691_set_slot_eeprom(VM_C2691(vm),slot,&card->eeprom);

   /* Create the device */
   data = dev_nm_16esw_init(vm,card->dev_name,slot,
                            card->pci_bus,6,
                            c2691_net_irq_for_slot_port(slot,0));

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* Remove a NM-16ESW from the specified slot */
static int 
dev_c2691_nm_16esw_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   struct nm_16esw_data *data = card->drv_info;

   /* Remove the NM EEPROM */
   cisco_card_unset_eeprom(card);
   c2691_set_slot_eeprom(VM_C2691(vm),card->slot_id,NULL);

   /* Remove the BCM5600 chip */
   dev_nm_16esw_remove(data);
   return(0);
}

/* Bind a Network IO descriptor */
static int 
dev_c2691_nm_16esw_set_nio(vm_instance_t *vm,struct cisco_card *card,
                           u_int port_id,netio_desc_t *nio)
{
   struct nm_16esw_data *d = card->drv_info;
   dev_nm_16esw_set_nio(d,port_id,nio);
   return(0);
}

/* Unbind a Network IO descriptor */
static int dev_c2691_nm_16esw_unset_nio(vm_instance_t *vm,
                                        struct cisco_card *card,
                                        u_int port_id)
{
   struct nm_16esw_data *d = card->drv_info;
   dev_nm_16esw_unset_nio(d,port_id);
   return(0);
}

/* Show debug info */
static int 
dev_c2691_nm_16esw_show_info(vm_instance_t *vm,struct cisco_card *card)
{
   struct nm_16esw_data *d = card->drv_info;
   dev_nm_16esw_show_info(d);
   return(0);
}

/* ====================================================================== */
/* GT96100 - Integrated Ethernet ports                                    */
/* ====================================================================== */

/* Initialize Ethernet part of the GT96100 controller */
static int dev_c2691_gt96100_fe_init(vm_instance_t *vm,struct cisco_card *card)
{
   if (card->slot_id != 0) {
      vm_error(vm,"dev_c2691_gt96100_fe_init: bad slot %u specified.\n",
               card->slot_id);
      return(-1);
   }

   /* Store device info into the router structure */
   card->drv_info = VM_C2691(vm)->gt_data;
   return(0);
}

/* Nothing to do, we never remove the system controller */
static int 
dev_c2691_gt96100_fe_shutdown(vm_instance_t *vm,struct cisco_card *card)
{   
   return(0);
}

/* Bind a Network IO descriptor */
static int
dev_c2691_gt96100_fe_set_nio(vm_instance_t *vm,struct cisco_card *card,
                             u_int port_id,netio_desc_t *nio)
{
   struct gt_data *d = card->drv_info;
   dev_gt96100_eth_set_nio(d,port_id,nio);
   return(0);
}

/* Unbind a Network IO descriptor */
static int dev_c2691_gt96100_fe_unset_nio(vm_instance_t *vm,
                                          struct cisco_card *card,
                                          u_int port_id)
{
   struct gt_data *d = card->drv_info;
   dev_gt96100_eth_unset_nio(d,port_id);
   return(0);
}

/* ====================================================================== */

/* NM-1FE-TX driver */
struct cisco_card_driver dev_c2691_nm_1fe_tx_driver = {
   "NM-1FE-TX", 1, 0,
   dev_c2691_nm_1fe_tx_init, 
   dev_c2691_nm_eth_shutdown,
   NULL,
   dev_c2691_nm_eth_set_nio,
   dev_c2691_nm_eth_unset_nio,
   NULL,
};

/* NM-16ESW driver */
struct cisco_card_driver dev_c2691_nm_16esw_driver = {
   "NM-16ESW", 1, 0,
   dev_c2691_nm_16esw_init, 
   dev_c2691_nm_16esw_shutdown,
   NULL,
   dev_c2691_nm_16esw_set_nio,
   dev_c2691_nm_16esw_unset_nio,
   dev_c2691_nm_16esw_show_info,
};

/* GT96100 FastEthernet integrated ports */
struct cisco_card_driver dev_c2691_gt96100_fe_driver = {
   "GT96100-FE", 1, 0,
   dev_c2691_gt96100_fe_init, 
   dev_c2691_gt96100_fe_shutdown,
   dev_c2691_mb_get_sub_info,
   dev_c2691_gt96100_fe_set_nio,
   dev_c2691_gt96100_fe_unset_nio,
   NULL,
};
