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
#include "dev_nm_16esw.h"
#include "dev_mpc860.h"
#include "dev_c1700.h"
#include "dev_wic_serial.h"

/* Get the SCC channel associated to a WIC sub-slot */
static int dev_c1700_mb_wic_get_scc_chan(struct cisco_card *card,u_int port_id,
                                         u_int *scc_chan)
{
   u_int cid;

   cid = card->subslot_id + port_id;
   
   switch(cid) {
      /* WIC 0 port 0 mapped to MPC860 SCC1 */
      case 0x10:
         *scc_chan = 0;
         break;

      /* WIC 0 port 1 mapped to MPC860 SCC4 */
      case 0x11:
         *scc_chan = 3;
         break;

      /* WIC 1 port 0 mapped to MPC860 SCC2 */
      case 0x20:
         *scc_chan = 1;
         break;

      /* WIC 1 port 1 mapped to MPC860 SCC3 */
      case 0x21:
         *scc_chan = 2;
         break;

      default:
         return(-1);
   }

   return(0);
}

/* ======================================================================== */
/* WIC-1T                                                                   */
/* ======================================================================== */

/* Initialize a WIC-1T in the specified slot */
static int dev_c1700_mb_wic1t_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct wic_serial_data *wic_data;
   m_uint64_t phys_addr;
   u_int wic_id;

   /* Create the WIC device */
   wic_id = (card->subslot_id >> 4) - 1;
   
   if (c1700_get_onboard_wic_addr(wic_id,&phys_addr) == -1) {
      vm_error(vm,"WIC","invalid slot %u (subslot_id=%u)\n",
               wic_id,card->subslot_id);
      return(-1);
   }

   wic_data = dev_wic_serial_init(vm,card->dev_name,WIC_SERIAL_MODEL_1T,
                                  phys_addr,C1700_WIC_SIZE);

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
dev_c1700_mb_wic1t_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Remove the WIC device */
   dev_wic_serial_remove(card->drv_info);

   /* Remove the WIC EEPROM */
   cisco_card_unset_eeprom(card);
   return(0);
}

/* Bind a Network IO descriptor */
static int 
dev_c1700_mb_wic1t_set_nio(vm_instance_t *vm,struct cisco_card *card,
                           u_int port_id,netio_desc_t *nio)
{
   u_int scc_chan;

   if ((port_id > 0) || 
       (dev_c1700_mb_wic_get_scc_chan(card,port_id,&scc_chan) == -1))
      return(-1);

   return(mpc860_scc_set_nio(VM_C1700(vm)->mpc_data,scc_chan,nio));
}

/* Unbind a Network IO descriptor */
static int 
dev_c1700_mb_wic1t_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                             u_int port_id)
{
   u_int scc_chan;

   if ((port_id > 0) || 
       (dev_c1700_mb_wic_get_scc_chan(card,port_id,&scc_chan) == -1))
      return(-1);

   return(mpc860_scc_unset_nio(VM_C1700(vm)->mpc_data,scc_chan));
}

/* ======================================================================== */
/* WIC-2T                                                                   */
/* ======================================================================== */

/* Initialize a WIC-2T in the specified slot */
static int dev_c1700_mb_wic2t_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct wic_serial_data *wic_data;
   m_uint64_t phys_addr;
   u_int wic_id;

   /* Create the WIC device */
   wic_id = (card->subslot_id >> 4) - 1;
   
   if (c1700_get_onboard_wic_addr(wic_id,&phys_addr) == -1) {
      vm_error(vm,"WIC","invalid slot %u (subslot_id=%u)\n",
               wic_id,card->subslot_id);
      return(-1);
   }

   wic_data = dev_wic_serial_init(vm,card->dev_name,WIC_SERIAL_MODEL_2T,
                                  phys_addr,C1700_WIC_SIZE);

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
dev_c1700_mb_wic2t_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Remove the WIC device */
   dev_wic_serial_remove(card->drv_info);

   /* Remove the WIC EEPROM */
   cisco_card_unset_eeprom(card);
   return(0);
}

/* Bind a Network IO descriptor */
static int 
dev_c1700_mb_wic2t_set_nio(vm_instance_t *vm,struct cisco_card *card,
                           u_int port_id,netio_desc_t *nio)
{
   u_int scc_chan;

   if ((port_id > 1) || 
       (dev_c1700_mb_wic_get_scc_chan(card,port_id,&scc_chan) == -1))
      return(-1);

   return(mpc860_scc_set_nio(VM_C1700(vm)->mpc_data,scc_chan,nio));
}

/* Unbind a Network IO descriptor */
static int 
dev_c1700_mb_wic2t_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                             u_int port_id)
{
   u_int scc_chan;

   if ((port_id > 1) || 
       (dev_c1700_mb_wic_get_scc_chan(card,port_id,&scc_chan) == -1))
      return(-1);

   return(mpc860_scc_unset_nio(VM_C1700(vm)->mpc_data,scc_chan));
}

/* ======================================================================== */
/* WIC-1ENET                                                                */
/* ======================================================================== */

/* Initialize a WIC-1ENET in the specified slot */
static int 
dev_c1700_mb_wic1enet_init(vm_instance_t *vm,struct cisco_card *card)
{        
   m_uint8_t eeprom_ver;
   size_t offset;
   n_eth_addr_t addr;
   m_uint16_t pid;

   pid = (m_uint16_t)getpid();

   /* Generate automatically the MAC address */
   addr.eth_addr_byte[0] = vm_get_mac_addr_msb(vm);
   addr.eth_addr_byte[1] = vm->instance_id & 0xFF;
   addr.eth_addr_byte[2] = pid >> 8;
   addr.eth_addr_byte[3] = pid & 0xFF;
   addr.eth_addr_byte[4] = card->subslot_id;
   addr.eth_addr_byte[5] = 0x00;

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_wic("WIC-1ENET"));

   /* Read EEPROM format version */
   cisco_eeprom_get_byte(&card->eeprom,0,&eeprom_ver);

   if (eeprom_ver != 4)
      return(-1);

   if (cisco_eeprom_v4_find_field(&card->eeprom,0xCF,&offset) == -1)
      return(-1);

   cisco_eeprom_set_region(&card->eeprom,offset,addr.eth_addr_byte,6);
   
   /* Store device info into the router structure */
   card->drv_info = VM_C1700(vm)->mpc_data;
   return(0);
}

/* Remove a WIC-1ENET from the specified slot */
static int 
dev_c1700_mb_wic1enet_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Remove the WIC EEPROM */
   cisco_card_unset_eeprom(card);
   return(0);
}

/* Bind a Network IO descriptor */
static int 
dev_c1700_mb_wic1enet_set_nio(vm_instance_t *vm,struct cisco_card *card,
                           u_int port_id,netio_desc_t *nio)
{
   struct mpc860_data *mpc_data = card->drv_info;
   u_int scc_chan;

   if ((port_id > 0) || 
       (dev_c1700_mb_wic_get_scc_chan(card,port_id,&scc_chan) == -1))
      return(-1);

   return(mpc860_scc_set_nio(mpc_data,scc_chan,nio));
}

/* Unbind a Network IO descriptor */
static int 
dev_c1700_mb_wic1enet_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                                u_int port_id)
{
   struct mpc860_data *mpc_data = card->drv_info;
   u_int scc_chan;

   if ((port_id > 0) || 
       (dev_c1700_mb_wic_get_scc_chan(card,port_id,&scc_chan) == -1))
      return(-1);

   return(mpc860_scc_unset_nio(mpc_data,scc_chan));
}

/* ======================================================================== */

/* Cisco 1700 WIC-1T driver */
struct cisco_card_driver dev_c1700_mb_wic1t_driver = {
   "WIC-1T", 1, 0,
   dev_c1700_mb_wic1t_init,
   dev_c1700_mb_wic1t_shutdown,
   NULL,
   dev_c1700_mb_wic1t_set_nio,
   dev_c1700_mb_wic1t_unset_nio,
   NULL,
};

/* Cisco 1700 WIC-2T driver */
struct cisco_card_driver dev_c1700_mb_wic2t_driver = {
   "WIC-2T", 1, 0,
   dev_c1700_mb_wic2t_init,
   dev_c1700_mb_wic2t_shutdown,
   NULL,
   dev_c1700_mb_wic2t_set_nio,
   dev_c1700_mb_wic2t_unset_nio,
   NULL,
};

/* Cisco 1700 WIC-1ENET driver */
struct cisco_card_driver dev_c1700_mb_wic1enet_driver = {
   "WIC-1ENET", 1, 0,
   dev_c1700_mb_wic1enet_init,
   dev_c1700_mb_wic1enet_shutdown,
   NULL,
   dev_c1700_mb_wic1enet_set_nio,
   dev_c1700_mb_wic1enet_unset_nio,
   NULL,
};

/* WIC drivers (mainbord slots) */
struct cisco_card_driver *dev_c1700_mb_wic_drivers[] = {
   &dev_c1700_mb_wic1t_driver,
   &dev_c1700_mb_wic2t_driver,
   &dev_c1700_mb_wic1enet_driver,
   NULL,
};
