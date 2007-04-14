/*  
 * Cisco C3600 simulation platform.
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
#include "dev_c3600.h"
#include "dev_c3600_bay.h"

/* Multi-Ethernet NM with Am79c971 chips */
struct nm_eth_data {
   u_int nr_port;
   struct am79c971_data *port[8];
};

/*
 * dev_c3600_nm_eth_init()
 *
 * Add an Ethernet Network Module into specified slot.
 */
static int dev_c3600_nm_eth_init(c3600_t *router,char *name,u_int nm_bay,
                                 int nr_port,int interface_type,
                                 const struct cisco_eeprom *eeprom)
{
   struct nm_bay_info *bay_info;
   struct nm_eth_data *data;
   int i;

   /* Allocate the private data structure */
   if (!(data = malloc(sizeof(*data)))) {
      fprintf(stderr,"%s: out of memory\n",name);
      return(-1);
   }

   memset(data,0,sizeof(*data));
   data->nr_port = nr_port;

   /* Set the EEPROM */
   c3600_nm_set_eeprom(router,nm_bay,eeprom);

   /* Get PCI bus info about this bay */
   bay_info = c3600_nm_get_bay_info(c3600_chassis_get_id(router),nm_bay);
      
   if (!bay_info) {
      fprintf(stderr,"%s: unable to get info for NM bay %u\n",name,nm_bay);
      return(-1);
   }

   /* Create the AMD Am971c971 chip(s) */
   for(i=0;i<data->nr_port;i++) {
      data->port[i] = dev_am79c971_init(router->vm,name,interface_type,
                                        router->nm_bay[nm_bay].pci_map,
                                        bay_info->pci_device + i,
                                        c3600_net_irq_for_slot_port(nm_bay,i));
   }

   /* Store device info into the router structure */
   return(c3600_nm_set_drvinfo(router,nm_bay,data));
}

/* Remove an Ethernet NM from the specified slot */
static int dev_c3600_nm_eth_shutdown(c3600_t *router,u_int nm_bay) 
{
   struct c3600_nm_bay *bay;
   struct nm_eth_data *data;
   int i;

   if (!(bay = c3600_nm_get_info(router,nm_bay)))
      return(-1);

   data = bay->drv_info;

   /* Remove the NM EEPROM */
   c3600_nm_unset_eeprom(router,nm_bay);

   /* Remove the AMD Am79c971 chips */
   for(i=0;i<data->nr_port;i++)
      dev_am79c971_remove(data->port[i]);

   free(data);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c3600_nm_eth_set_nio(c3600_t *router,u_int nm_bay,
                                    u_int port_id,netio_desc_t *nio)
{
   struct nm_eth_data *d;

   d = c3600_nm_get_drvinfo(router,nm_bay);

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_am79c971_set_nio(d->port[port_id],nio);
   return(0);
}

/* Unbind a Network IO descriptor */
static int dev_c3600_nm_eth_unset_nio(c3600_t *router,u_int nm_bay,
                                      u_int port_id)
{
   struct nm_eth_data *d;

   d = c3600_nm_get_drvinfo(router,nm_bay);

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_am79c971_unset_nio(d->port[port_id]);
   return(0);
}

/* ====================================================================== */
/* NM-1E                                                                  */
/* ====================================================================== */

/*
 * dev_c3600_nm_1e_init()
 *
 * Add a NM-1E Network Module into specified slot.
 */
static int dev_c3600_nm_1e_init(c3600_t *router,char *name,u_int nm_bay)
{
   return(dev_c3600_nm_eth_init(router,name,nm_bay,1,AM79C971_TYPE_10BASE_T,
                                cisco_eeprom_find_nm("NM-1E")));
}

/* ====================================================================== */
/* NM-4E                                                                  */
/* ====================================================================== */

/*
 * dev_c3600_nm_4e_init()
 *
 * Add a NM-4E Network Module into specified slot.
 */
static int dev_c3600_nm_4e_init(c3600_t *router,char *name,u_int nm_bay)
{
   return(dev_c3600_nm_eth_init(router,name,nm_bay,4,AM79C971_TYPE_10BASE_T,
                                cisco_eeprom_find_nm("NM-4E")));
}

/* ====================================================================== */
/* NM-1FE-TX                                                              */
/* ====================================================================== */

/*
 * dev_c3600_nm_1fe_tx_init()
 *
 * Add a NM-1FE-TX Network Module into specified slot.
 */
static int dev_c3600_nm_1fe_tx_init(c3600_t *router,char *name,u_int nm_bay)
{
   return(dev_c3600_nm_eth_init(router,name,nm_bay,1,AM79C971_TYPE_100BASE_TX,
                                cisco_eeprom_find_nm("NM-1FE-TX")));
}

/* ====================================================================== */
/* NM-16ESW                                                               */
/* ====================================================================== */

/* Add a NM-16ESW */
static int dev_c3600_nm_16esw_init(c3600_t *router,char *name,u_int nm_bay)
{
   struct nm_bay_info *bay_info;
   struct nm_16esw_data *data;

   /* Set the EEPROM */
   c3600_nm_set_eeprom(router,nm_bay,cisco_eeprom_find_nm("NM-16ESW"));
   dev_nm_16esw_burn_mac_addr(router->vm,nm_bay,
                              &router->nm_bay[nm_bay].eeprom);

   /* Get PCI bus info about this bay */
   bay_info = c3600_nm_get_bay_info(c3600_chassis_get_id(router),nm_bay);
      
   if (!bay_info) {
      fprintf(stderr,"%s: unable to get info for NM bay %u\n",name,nm_bay);
      return(-1);
   }

   /* Create the device */
   data = dev_nm_16esw_init(router->vm,name,nm_bay,
                            router->nm_bay[nm_bay].pci_map,
                            bay_info->pci_device,
                            c3600_net_irq_for_slot_port(nm_bay,0));

   /* Store device info into the router structure */
   return(c3600_nm_set_drvinfo(router,nm_bay,data));
}

/* Remove a NM-16ESW from the specified slot */
static int dev_c3600_nm_16esw_shutdown(c3600_t *router,u_int nm_bay) 
{
   struct c3600_nm_bay *bay;
   struct nm_16esw_data *data;

   if (!(bay = c3600_nm_get_info(router,nm_bay)))
      return(-1);

   data = bay->drv_info;

   /* Remove the NM EEPROM */
   c3600_nm_unset_eeprom(router,nm_bay);

   /* Remove the BCM5600 chip */
   dev_nm_16esw_remove(data);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c3600_nm_16esw_set_nio(c3600_t *router,u_int nm_bay,
                                      u_int port_id,netio_desc_t *nio)
{
   struct nm_16esw_data *d;

   d = c3600_nm_get_drvinfo(router,nm_bay);
   dev_nm_16esw_set_nio(d,port_id,nio);
   return(0);
}

/* Unbind a Network IO descriptor */
static int dev_c3600_nm_16esw_unset_nio(c3600_t *router,u_int nm_bay,
                                        u_int port_id)
{
   struct nm_16esw_data *d;

   d = c3600_nm_get_drvinfo(router,nm_bay);
   dev_nm_16esw_unset_nio(d,port_id);
   return(0);
}

/* Show debug info */
static int dev_c3600_nm_16esw_show_info(c3600_t *router,u_int nm_bay)
{
   struct nm_16esw_data *d;

   d = c3600_nm_get_drvinfo(router,nm_bay);
   dev_nm_16esw_show_info(d);
   return(0);
}

/* ====================================================================== */
/* Leopard-2FE                                                            */
/* ====================================================================== */

/* 
 * Leopard-2FE: 2 FastEthernet ports on C3660 motherboard.
 *
 * Leopard-2FE is the FRU/Product Number displayed by "show diag".
 */
static m_uint16_t eeprom_c3600_leopard_2fe_data[] = {
   0x04FF, 0xC18B, 0x4A41, 0x4230, 0x3530, 0x3330, 0x3454, 0x3809,
   0x3440, 0x00B3, 0xC046, 0x0320, 0x0012, 0x8104, 0x4241, 0x3085,
   0x1C0C, 0xA202, 0x80FF, 0xFFFF, 0xFFC4, 0x08FF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFA1, 0xFFFF, 0xFFFF, 0x03FF, 0x04FF, 0xC508, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFF00,
};

static const struct cisco_eeprom eeprom_c3600_leopard_2fe = {
   "Leopard-2FE", (m_uint16_t *)eeprom_c3600_leopard_2fe_data, 
   sizeof(eeprom_c3600_leopard_2fe_data)/2,
};

/*
 * dev_c3600_leopard_2fe_init()
 *
 * Add Leopard-2FE (only Cisco 3660, in slot 0).
 */
static int dev_c3600_leopard_2fe_init(c3600_t *router,char *name,u_int nm_bay)
{
   if (nm_bay != 0) {
      fprintf(stderr,"C3600 %s: Leopard-2FE can only be put in slot 0\n",
              router->vm->name);
      return(-1);
   }

   return(dev_c3600_nm_eth_init(router,name,0,2,AM79C971_TYPE_100BASE_TX,
                                &eeprom_c3600_leopard_2fe));
}

/* ====================================================================== */

/* NM-1FE-TX driver */
struct c3600_nm_driver dev_c3600_nm_1fe_tx_driver = {
   "NM-1FE-TX", 1, 0,
   dev_c3600_nm_1fe_tx_init, 
   dev_c3600_nm_eth_shutdown,
   dev_c3600_nm_eth_set_nio,
   dev_c3600_nm_eth_unset_nio,
   NULL,
};

/* NM-1E driver */
struct c3600_nm_driver dev_c3600_nm_1e_driver = {
   "NM-1E", 1, 0,
   dev_c3600_nm_1e_init, 
   dev_c3600_nm_eth_shutdown,
   dev_c3600_nm_eth_set_nio,
   dev_c3600_nm_eth_unset_nio,
   NULL,
};

/* NM-4E driver */
struct c3600_nm_driver dev_c3600_nm_4e_driver = {
   "NM-4E", 1, 0,
   dev_c3600_nm_4e_init, 
   dev_c3600_nm_eth_shutdown,
   dev_c3600_nm_eth_set_nio,
   dev_c3600_nm_eth_unset_nio,
   NULL,
};

/* NM-16ESW driver */
struct c3600_nm_driver dev_c3600_nm_16esw_driver = {
   "NM-16ESW", 1, 0,
   dev_c3600_nm_16esw_init, 
   dev_c3600_nm_16esw_shutdown,
   dev_c3600_nm_16esw_set_nio,
   dev_c3600_nm_16esw_unset_nio,
   dev_c3600_nm_16esw_show_info,
};

/* Leopard-2FE driver */
struct c3600_nm_driver dev_c3600_leopard_2fe_driver = {
   "Leopard-2FE", 1, 0,
   dev_c3600_leopard_2fe_init, 
   dev_c3600_nm_eth_shutdown,
   dev_c3600_nm_eth_set_nio,
   dev_c3600_nm_eth_unset_nio,
   NULL,
};
