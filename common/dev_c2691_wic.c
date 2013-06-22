/*  
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * WIC Modules.
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
#include "dev_gt.h"
#include "dev_c2691.h"
#include "dev_wic_serial.h"

/* Get the MPSC channel associated to a WIC sub-slot */
static int dev_c2691_mb_wic_get_mpsc_chan(struct cisco_card *card,
                                          u_int port_id,
                                          u_int *mpsc_chan)
{
   u_int cid;

   cid = card->subslot_id + port_id;
   
   switch(cid) {
      /* WIC 0 port 0 mapped to GT96100 MPSC1 */
      case 0x10:
         *mpsc_chan = 1;
         break;

      /* WIC 0 port 1 mapped to GT96100 MPSC0 */
      case 0x11:
         *mpsc_chan = 0;
         break;

      /* WIC 1 port 0 mapped to GT96100 MPSC4 */
      case 0x20:
         *mpsc_chan = 4;
         break;

      /* WIC 1 port 1 mapped to GT96100 MPSC2 */
      case 0x21:
         *mpsc_chan = 2;
         break;

      /* WIC 2 port 0 mapped to GT96100 MPSC5 */
      case 0x30:
         *mpsc_chan = 5;
         break;

      /* WIC 2 port 1 mapped to GT96100 MPSC3 */
      case 0x31:
         *mpsc_chan = 3;
         break;

      default:
         return(-1);
   }

   return(0);
}

/* Initialize a WIC-1T in the specified slot */
static int dev_c2691_mb_wic1t_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct wic_serial_data *wic_data;
   m_uint64_t phys_addr;
   u_int wic_id;

   /* Create the WIC device */
   wic_id = (card->subslot_id >> 4) - 1;
   
   if (c2691_get_onboard_wic_addr(wic_id,&phys_addr) == -1) {
      vm_error(vm,"WIC","invalid slot %u (subslot_id=%u)\n",
               wic_id,card->subslot_id);
      return(-1);
   }

   wic_data = dev_wic_serial_init(vm,card->dev_name,WIC_SERIAL_MODEL_1T,
                                  phys_addr,C2691_WIC_SIZE);

   if (!wic_data)
      return(-1);

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_wic("WIC-1T"));

   /* Store device info into the router structure */
   card->drv_info = wic_data;
   return(0);
}

/* Remove a WIC-1T from the specified slot */
static int 
dev_c2691_mb_wic1t_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Remove the WIC device */
   dev_wic_serial_remove(card->drv_info);

   /* Remove the WIC EEPROM */
   cisco_card_unset_eeprom(card);
   return(0);
}

/* Bind a Network IO descriptor */
static int 
dev_c2691_mb_wic1t_set_nio(vm_instance_t *vm,struct cisco_card *card,
                           u_int port_id,netio_desc_t *nio)
{
   u_int mpsc_chan;

   if ((port_id > 0) || 
       (dev_c2691_mb_wic_get_mpsc_chan(card,port_id,&mpsc_chan) == -1))
      return(-1);

   return(dev_gt96100_mpsc_set_nio(VM_C2691(vm)->gt_data,mpsc_chan,nio));
}

/* Unbind a Network IO descriptor */
static int 
dev_c2691_mb_wic1t_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                             u_int port_id)
{
   u_int mpsc_chan;

   if ((port_id > 0) || 
       (dev_c2691_mb_wic_get_mpsc_chan(card,port_id,&mpsc_chan) == -1))
      return(-1);

   return(dev_gt96100_mpsc_unset_nio(VM_C2691(vm)->gt_data,mpsc_chan));
}

/* Initialize a WIC-2T in the specified slot */
static int dev_c2691_mb_wic2t_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct wic_serial_data *wic_data;
   m_uint64_t phys_addr;
   u_int wic_id;

   /* Create the WIC device */
   wic_id = (card->subslot_id >> 4) - 1;
   
   if (c2691_get_onboard_wic_addr(wic_id,&phys_addr) == -1) {
      vm_error(vm,"WIC","invalid slot %u (subslot_id=%u)\n",
               wic_id,card->subslot_id);
      return(-1);
   }

   wic_data = dev_wic_serial_init(vm,card->dev_name,WIC_SERIAL_MODEL_2T,
                                  phys_addr,C2691_WIC_SIZE);

   if (!wic_data)
      return(-1);

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_wic("WIC-2T"));

   /* Store device info into the router structure */
   card->drv_info = wic_data;
   return(0);
}

/* Remove a WIC-2T from the specified slot */
static int 
dev_c2691_mb_wic2t_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Remove the WIC device */
   dev_wic_serial_remove(card->drv_info);

   /* Remove the WIC EEPROM */
   cisco_card_unset_eeprom(card);
   return(0);
}

/* Bind a Network IO descriptor */
static int 
dev_c2691_mb_wic2t_set_nio(vm_instance_t *vm,struct cisco_card *card,
                           u_int port_id,netio_desc_t *nio)
{
   u_int mpsc_chan;

   if ((port_id > 1) || 
       (dev_c2691_mb_wic_get_mpsc_chan(card,port_id,&mpsc_chan) == -1))
      return(-1);

   return(dev_gt96100_mpsc_set_nio(VM_C2691(vm)->gt_data,mpsc_chan,nio));
}

/* Unbind a Network IO descriptor */
static int 
dev_c2691_mb_wic2t_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                             u_int port_id)
{
   u_int mpsc_chan;

   if ((port_id > 1) || 
       (dev_c2691_mb_wic_get_mpsc_chan(card,port_id,&mpsc_chan) == -1))
      return(-1);

   return(dev_gt96100_mpsc_unset_nio(VM_C2691(vm)->gt_data,mpsc_chan));
}

/* Cisco 2691 WIC-1T driver (for mainboard) */
struct cisco_card_driver dev_c2691_mb_wic1t_driver = {
   "WIC-1T", 1, 0,
   dev_c2691_mb_wic1t_init,
   dev_c2691_mb_wic1t_shutdown,
   NULL,
   dev_c2691_mb_wic1t_set_nio,
   dev_c2691_mb_wic1t_unset_nio,
   NULL,
};

/* Cisco 2691 WIC-2T driver (for mainboard) */
struct cisco_card_driver dev_c2691_mb_wic2t_driver = {
   "WIC-2T", 1, 0,
   dev_c2691_mb_wic2t_init,
   dev_c2691_mb_wic2t_shutdown,
   NULL,
   dev_c2691_mb_wic2t_set_nio,
   dev_c2691_mb_wic2t_unset_nio,
   NULL,
};

/* WIC drivers (mainbord slots) */
struct cisco_card_driver *dev_c2691_mb_wic_drivers[] = {
   &dev_c2691_mb_wic1t_driver,
   &dev_c2691_mb_wic2t_driver,
   NULL,
};
