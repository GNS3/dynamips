/*  
 * Cisco C3725 simulation platform.
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
#include "dev_c3725.h"

/* Multi-Ethernet NM with Am79c971 chips */
struct nm_eth_data {
   u_int nr_port;
   struct am79c971_data *port[8];
};

/*
 * dev_c3725_nm_eth_init()
 *
 * Add an Ethernet Network Module into specified slot.
 */
static int dev_c3725_nm_eth_init(c3725_t *router,char *name,u_int nm_bay,
                                 int nr_port,int interface_type,
                                 const struct cisco_eeprom *eeprom)
{
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
   c3725_nm_set_eeprom(router,nm_bay,eeprom);

   /* Create the AMD Am971c971 chip(s) */
   for(i=0;i<data->nr_port;i++) {
      data->port[i] = dev_am79c971_init(router->vm,name,interface_type,
                                        router->nm_bay[nm_bay].pci_map,
                                        c3725_nm_get_pci_device(nm_bay),
                                        c3725_net_irq_for_slot_port(nm_bay,i));
   }

   /* Store device info into the router structure */
   return(c3725_nm_set_drvinfo(router,nm_bay,data));
}

/* Remove an Ethernet NM from the specified slot */
static int dev_c3725_nm_eth_shutdown(c3725_t *router,u_int nm_bay) 
{
   struct c3725_nm_bay *bay;
   struct nm_eth_data *data;
   int i;

   if (!(bay = c3725_nm_get_info(router,nm_bay)))
      return(-1);

   data = bay->drv_info;

   /* Remove the NM EEPROM */
   c3725_nm_unset_eeprom(router,nm_bay);

   /* Remove the AMD Am79c971 chips */
   for(i=0;i<data->nr_port;i++)
      dev_am79c971_remove(data->port[i]);

   free(data);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c3725_nm_eth_set_nio(c3725_t *router,u_int nm_bay,
                                    u_int port_id,netio_desc_t *nio)
{
   struct nm_eth_data *d;

   d = c3725_nm_get_drvinfo(router,nm_bay);

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_am79c971_set_nio(d->port[port_id],nio);
   return(0);
}

/* Unbind a Network IO descriptor */
static int dev_c3725_nm_eth_unset_nio(c3725_t *router,u_int nm_bay,
                                      u_int port_id)
{
   struct nm_eth_data *d;

   d = c3725_nm_get_drvinfo(router,nm_bay);

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_am79c971_unset_nio(d->port[port_id]);
   return(0);
}

/* ====================================================================== */
/* NM-1FE-TX                                                              */
/* ====================================================================== */

/*
 * dev_c3725_nm_1fe_tx_init()
 *
 * Add a NM-1FE-TX Network Module into specified slot.
 */
static int dev_c3725_nm_1fe_tx_init(c3725_t *router,char *name,u_int nm_bay)
{
   return(dev_c3725_nm_eth_init(router,name,nm_bay,1,AM79C971_TYPE_100BASE_TX,
                                cisco_eeprom_find_nm("NM-1FE-TX")));
}

/* ====================================================================== */
/* NM-16ESW                                                               */
/* ====================================================================== */

/* Add a NM-16ESW */
static int dev_c3725_nm_16esw_init(c3725_t *router,char *name,u_int nm_bay)
{
   struct nm_16esw_data *data;

   /* Set the EEPROM */
   c3725_nm_set_eeprom(router,nm_bay,cisco_eeprom_find_nm("NM-16ESW"));
   dev_nm_16esw_burn_mac_addr(router->vm,nm_bay,
                              &router->nm_bay[nm_bay].eeprom);

   /* Create the device */
   data = dev_nm_16esw_init(router->vm,name,nm_bay,
                            router->nm_bay[nm_bay].pci_map,
                            c3725_nm_get_pci_device(nm_bay),
                            c3725_net_irq_for_slot_port(nm_bay,0));

   /* Store device info into the router structure */
   return(c3725_nm_set_drvinfo(router,nm_bay,data));
}

/* Remove a NM-16ESW from the specified slot */
static int dev_c3725_nm_16esw_shutdown(c3725_t *router,u_int nm_bay) 
{
   struct c3725_nm_bay *bay;
   struct nm_16esw_data *data;

   if (!(bay = c3725_nm_get_info(router,nm_bay)))
      return(-1);

   data = bay->drv_info;

   /* Remove the NM EEPROM */
   c3725_nm_unset_eeprom(router,nm_bay);

   /* Remove the BCM5600 chip */
   dev_nm_16esw_remove(data);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c3725_nm_16esw_set_nio(c3725_t *router,u_int nm_bay,
                                      u_int port_id,netio_desc_t *nio)
{
   struct nm_16esw_data *d;

   d = c3725_nm_get_drvinfo(router,nm_bay);
   dev_nm_16esw_set_nio(d,port_id,nio);
   return(0);
}

/* Unbind a Network IO descriptor */
static int dev_c3725_nm_16esw_unset_nio(c3725_t *router,u_int nm_bay,
                                        u_int port_id)
{
   struct nm_16esw_data *d;

   d = c3725_nm_get_drvinfo(router,nm_bay);
   dev_nm_16esw_unset_nio(d,port_id);
   return(0);
}

/* Show debug info */
static int dev_c3725_nm_16esw_show_info(c3725_t *router,u_int nm_bay)
{
   struct nm_16esw_data *d;

   d = c3725_nm_get_drvinfo(router,nm_bay);
   dev_nm_16esw_show_info(d);
   return(0);
}

/* ====================================================================== */
/* GT96100 - Integrated Ethernet ports                                    */
/* ====================================================================== */

/* Initialize Ethernet part of the GT96100 controller */
static int dev_c3725_gt96100_fe_init(c3725_t *router,char *name,u_int nm_bay)
{
   vm_obj_t *obj;

   if (nm_bay != 0) {
      fprintf(stderr,"dev_c3725_gt96100_fe_init: bad slot specified.\n");
      return(-1);
   }

   if (!(obj = vm_object_find(router->vm,"gt96100"))) {
      fprintf(stderr,"dev_c3725_gt96100_fe_init: unable to find "
              "system controller!\n");
      return(-1);
   }

   /* Store device info into the router structure */
   return(c3725_nm_set_drvinfo(router,0,obj->data));
}

/* Nothing to do, we never remove the system controller */
static int dev_c3725_gt96100_fe_shutdown(c3725_t *router,u_int nm_bay) 
{   
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c3725_gt96100_fe_set_nio(c3725_t *router,u_int nm_bay,
                                        u_int port_id,netio_desc_t *nio)
{
   struct gt_data *d;

   if (!(d = c3725_nm_get_drvinfo(router,nm_bay)))
      return(-1);

   dev_gt96100_set_nio(d,port_id,nio);
   return(0);
}

/* Unbind a Network IO descriptor */
static int dev_c3725_gt96100_fe_unset_nio(c3725_t *router,u_int nm_bay,
                                        u_int port_id)
{
   struct gt_data *d;

   if (!(d = c3725_nm_get_drvinfo(router,nm_bay)))
      return(-1);

   dev_gt96100_unset_nio(d,port_id);
   return(0);
}

/* ====================================================================== */

/* NM-1FE-TX driver */
struct c3725_nm_driver dev_c3725_nm_1fe_tx_driver = {
   "NM-1FE-TX", 1, 0,
   dev_c3725_nm_1fe_tx_init, 
   dev_c3725_nm_eth_shutdown,
   dev_c3725_nm_eth_set_nio,
   dev_c3725_nm_eth_unset_nio,
   NULL,
};

/* NM-16ESW driver */
struct c3725_nm_driver dev_c3725_nm_16esw_driver = {
   "NM-16ESW", 1, 0,
   dev_c3725_nm_16esw_init, 
   dev_c3725_nm_16esw_shutdown,
   dev_c3725_nm_16esw_set_nio,
   dev_c3725_nm_16esw_unset_nio,
   dev_c3725_nm_16esw_show_info,
};

/* GT96100 FastEthernet integrated ports */
struct c3725_nm_driver dev_c3725_gt96100_fe_driver = {
   "GT96100-FE", 1, 0,
   dev_c3725_gt96100_fe_init, 
   dev_c3725_gt96100_fe_shutdown,
   dev_c3725_gt96100_fe_set_nio,
   dev_c3725_gt96100_fe_unset_nio,
   NULL,
};
