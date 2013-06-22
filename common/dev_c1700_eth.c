/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Cisco 1700 Mainboard Ethernet driver.
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
#include "vm.h"
#include "dev_mpc860.h"
#include "dev_c1700.h"

/* Return sub-slot info for integrated WIC slots (on motherboard) */
static int dev_c1700_mb_get_sub_info(vm_instance_t *vm,struct cisco_card *card,
                                     u_int port_id,
                                     struct cisco_card_driver ***drv_array,
                                     u_int *subcard_type)
{
   /* 2 integrated WIC slots */
   if ((port_id & 0x0F) >= 2)
      return(-1);

   *drv_array = dev_c1700_mb_wic_drivers;
   *subcard_type = CISCO_CARD_TYPE_WIC;
   return(0);
}

/* ====================================================================== */
/* MPC860 - Integrated Ethernet port                                      */
/* ====================================================================== */

/* Initialize Ethernet part of the MPC860 */
static int dev_c1700_mb_eth_init(vm_instance_t *vm,struct cisco_card *card)
{
   if (card->slot_id != 0) {
      vm_error(vm,"dev_c1700_mb_eth_init: bad slot %u specified.\n",
               card->slot_id);
      return(-1);
   }

   /* Store device info into the router structure */
   card->drv_info = VM_C1700(vm)->mpc_data;
   return(0);
}

/* Nothing to do, we never remove the system controller */
static int dev_c1700_mb_eth_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c1700_mb_eth_set_nio(vm_instance_t *vm,struct cisco_card *card,
                                    u_int port_id,netio_desc_t *nio)
{
   struct mpc860_data *d = card->drv_info;

   if (port_id != 0)
      return(-1);

   return(mpc860_fec_set_nio(d,nio));
}

/* Unbind a Network IO descriptor */
static int dev_c1700_mb_eth_unset_nio(vm_instance_t *vm,
                                      struct cisco_card *card,
                                      u_int port_id)
{
   struct mpc860_data *d = card->drv_info;

   if (port_id != 0)
      return(-1);

   return(mpc860_fec_unset_nio(d));
}

/* c1700 Motherboard driver */
struct cisco_card_driver dev_c1700_mb_eth_driver = {
   "C1700-MB-1ETH", 1, 2,
   dev_c1700_mb_eth_init,
   dev_c1700_mb_eth_shutdown,
   dev_c1700_mb_get_sub_info,
   dev_c1700_mb_eth_set_nio,
   dev_c1700_mb_eth_unset_nio,
   NULL,
};

/* ====================================================================== */
/* C1710: 1 FastEthernet port + 1 Ethernet port as SCC channel 1.         */
/* ====================================================================== */

/* Bind a Network IO descriptor */
static int dev_c1710_mb_eth_set_nio(vm_instance_t *vm,struct cisco_card *card,
                                    u_int port_id,netio_desc_t *nio)
{
   struct mpc860_data *d = card->drv_info;

   switch(port_id) {
      case 0:
         return(mpc860_fec_set_nio(d,nio));
      case 1:
         return(mpc860_scc_set_nio(d,0,nio));
      default:
         return(-1);
   }
}

/* Unbind a Network IO descriptor */
static int dev_c1710_mb_eth_unset_nio(vm_instance_t *vm,
                                      struct cisco_card *card,
                                      u_int port_id)
{
   struct mpc860_data *d = card->drv_info;

   switch(port_id) {
      case 0:
         return(mpc860_fec_unset_nio(d));
      case 1:
         return(mpc860_scc_unset_nio(d,0));
      default:
         return(-1);
   }
}

/* c1710 Motherboard driver */
struct cisco_card_driver dev_c1710_mb_eth_driver = {
   "C1710-MB-1FE-1E", 1, 0,
   dev_c1700_mb_eth_init,
   dev_c1700_mb_eth_shutdown,
   NULL,
   dev_c1710_mb_eth_set_nio,
   dev_c1710_mb_eth_unset_nio,
   NULL,
};

