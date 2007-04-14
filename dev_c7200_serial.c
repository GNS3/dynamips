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
int dev_c7200_pa_4t_init(c7200_t *router,char *name,u_int pa_bay)
{
   struct mueslix_data *data;

   /* Set the EEPROM */
   c7200_pa_set_eeprom(router,pa_bay,cisco_eeprom_find_pa("PA-4T+"));

   /* Create the Mueslix chip */
   data = dev_mueslix_init(router->vm,name,1,
                           router->pa_bay[pa_bay].pci_map,0,
                           c7200_net_irq_for_slot_port(pa_bay,0));
   if (!data) return(-1);

   /* Store device info into the router structure */
   return(c7200_pa_set_drvinfo(router,pa_bay,data));
}

/* Remove a PA-4T+ from the specified slot */
int dev_c7200_pa_4t_shutdown(c7200_t *router,u_int pa_bay) 
{
   struct c7200_pa_bay *bay;

   if (!(bay = c7200_pa_get_info(router,pa_bay)))
      return(-1);

   c7200_pa_unset_eeprom(router,pa_bay);
   dev_mueslix_remove(bay->drv_info);
   return(0);
}

/* Bind a Network IO descriptor to a specific port */
int dev_c7200_pa_4t_set_nio(c7200_t *router,u_int pa_bay,u_int port_id,
                            netio_desc_t *nio)
{
   struct mueslix_data *data;

   if ((port_id >= MUESLIX_NR_CHANNELS) || 
       !(data = c7200_pa_get_drvinfo(router,pa_bay)))
      return(-1);

   return(dev_mueslix_set_nio(data,port_id,nio));
}

/* Unbind a Network IO descriptor to a specific port */
int dev_c7200_pa_4t_unset_nio(c7200_t *router,u_int pa_bay,u_int port_id)
{
   struct mueslix_data *d;

   if ((port_id >= MUESLIX_NR_CHANNELS) || 
       !(d = c7200_pa_get_drvinfo(router,pa_bay)))
      return(-1);
   
   return(dev_mueslix_unset_nio(d,port_id));
}

/* PA-4T+ driver */
struct c7200_pa_driver dev_c7200_pa_4t_driver = {
   "PA-4T+", 1, 
   dev_c7200_pa_4t_init, 
   dev_c7200_pa_4t_shutdown,
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
int dev_c7200_pa_8t_init(c7200_t *router,char *name,u_int pa_bay)
{
   struct pa8t_data *data;

   /* Allocate the private data structure for the PA-8T */
   if (!(data = malloc(sizeof(*data)))) {
      fprintf(stderr,"%s (PA-8T): out of memory\n",name);
      return(-1);
   }

   /* Set the EEPROM */
   c7200_pa_set_eeprom(router,pa_bay,cisco_eeprom_find_pa("PA-8T"));

   /* Create the 1st Mueslix chip */
   data->mueslix[0] = dev_mueslix_init(router->vm,name,1,
                                       router->pa_bay[pa_bay].pci_map,0,
                                       c7200_net_irq_for_slot_port(pa_bay,0));
   if (!data->mueslix[0]) return(-1);

   /* Create the 2nd Mueslix chip */
   data->mueslix[1] = dev_mueslix_init(router->vm,name,1,
                                       router->pa_bay[pa_bay].pci_map,1,
                                       c7200_net_irq_for_slot_port(pa_bay,1));
   if (!data->mueslix[1]) return(-1);

   /* Store device info into the router structure */
   return(c7200_pa_set_drvinfo(router,pa_bay,data));
}

/* Remove a PA-8T from the specified slot */
int dev_c7200_pa_8t_shutdown(c7200_t *router,u_int pa_bay) 
{
   struct c7200_pa_bay *bay;
   struct pa8t_data *data;

   if (!(bay = c7200_pa_get_info(router,pa_bay)))
      return(-1);

   data = bay->drv_info;

   /* Remove the PA EEPROM */
   c7200_pa_unset_eeprom(router,pa_bay);

   /* Remove the two Mueslix chips */
   dev_mueslix_remove(data->mueslix[0]);
   dev_mueslix_remove(data->mueslix[1]);
   free(data);
   return(0);
}

/* Bind a Network IO descriptor to a specific port */
int dev_c7200_pa_8t_set_nio(c7200_t *router,u_int pa_bay,u_int port_id,
                            netio_desc_t *nio)
{
   struct pa8t_data *d;

   if ((port_id >= (MUESLIX_NR_CHANNELS*2)) || 
       !(d = c7200_pa_get_drvinfo(router,pa_bay)))
      return(-1);

   return(dev_mueslix_set_nio(d->mueslix[port_id>>2],(port_id&0x03),nio));
}

/* Bind a Network IO descriptor to a specific port */
int dev_c7200_pa_8t_unset_nio(c7200_t *router,u_int pa_bay,u_int port_id)
{
   struct pa8t_data *d;

   if ((port_id >= (MUESLIX_NR_CHANNELS*2)) || 
       !(d = c7200_pa_get_drvinfo(router,pa_bay)))
      return(-1);

   return(dev_mueslix_unset_nio(d->mueslix[port_id>>2],port_id&0x03));
}

/* PA-8T driver */
struct c7200_pa_driver dev_c7200_pa_8t_driver = {
   "PA-8T", 1, 
   dev_c7200_pa_8t_init, 
   dev_c7200_pa_8t_shutdown, 
   dev_c7200_pa_8t_set_nio,
   dev_c7200_pa_8t_unset_nio,
};
