/*  
 * Cisco C7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Ethernet Port Adapters.
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
#include "dev_dec21140.h"
#include "dev_c7200.h"

/* ====================================================================== */
/* C7200-IO-FE EEPROM                                                     */
/* ====================================================================== */

/* C7200-IO-FE: C7200 IOCard with one FastEthernet port EEPROM */
static const m_uint16_t eeprom_c7200_io_fe_data[16] = {
   0x0183, 0x010E, 0xffff, 0xffff, 0x490B, 0x8C02, 0x0000, 0x0000,
   0x5000, 0x0000, 0x9812, 0x2800, 0x00FF, 0xFFFF, 0xFFFF, 0xFFFF,
};

static const struct cisco_eeprom eeprom_c7200_io_fe = {
   "C7200-IO-FE", (m_uint16_t *)eeprom_c7200_io_fe_data,
   sizeof(eeprom_c7200_io_fe_data)/2,
};

/*
 * dev_c7200_iocard_init()
 *
 * Add an IOcard into slot 0.
 */
static int dev_c7200_iocard_init(c7200_t *router,char *name,u_int pa_bay)
{
   struct dec21140_data *data;
   
   if (pa_bay != 0) {
      fprintf(stderr,"C7200 '%s': cannot put IOCARD in PA bay %u!\n",
              router->vm->name,pa_bay);
      return(-1);
   }

   /* Set the EEPROM */
   c7200_pa_set_eeprom(router,pa_bay,&eeprom_c7200_io_fe);

   /* Create the DEC21140 chip */
   data = dev_dec21140_init(router->vm,name,
                            router->pa_bay[pa_bay].pci_map,
                            router->npe_driver->dec21140_pci_dev,
                            C7200_NETIO_IRQ);
   if (!data) return(-1);

   /* Store device info into the router structure */
   return(c7200_pa_set_drvinfo(router,pa_bay,data));
}

/* Remove an IOcard from slot 0 */
static int dev_c7200_iocard_shutdown(c7200_t *router,u_int pa_bay) 
{
   struct c7200_pa_bay *bay;

   if (!(bay = c7200_pa_get_info(router,pa_bay)))
      return(-1);

   c7200_pa_unset_eeprom(router,pa_bay);
   dev_dec21140_remove(bay->drv_info);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c7200_iocard_set_nio(c7200_t *router,u_int pa_bay,u_int port_id,
                                    netio_desc_t *nio)
{
   struct dec21140_data *d;

   if ((port_id > 0) || !(d = c7200_pa_get_drvinfo(router,pa_bay)))
      return(-1);

   return(dev_dec21140_set_nio(d,nio));
}

/* Unbind a Network IO descriptor */
static int dev_c7200_iocard_unset_nio(c7200_t *router,u_int pa_bay,
                                      u_int port_id)
{
   struct dec21140_data *d;

   if ((port_id > 0) || !(d = c7200_pa_get_drvinfo(router,pa_bay)))
      return(-1);
   
   dev_dec21140_unset_nio(d);
   return(0);
}

/*
 * dev_c7200_pa_fe_tx_init()
 *
 * Add a PA-FE-TX port adapter into specified slot.
 */
static int dev_c7200_pa_fe_tx_init(c7200_t *router,char *name,u_int pa_bay)
{
   struct dec21140_data *data;

   /* Set the EEPROM */
   c7200_pa_set_eeprom(router,pa_bay,cisco_eeprom_find_pa("PA-FE-TX"));

   /* Create the DEC21140 chip */
   data = dev_dec21140_init(router->vm,name,router->pa_bay[pa_bay].pci_map,0,
                            C7200_NETIO_IRQ);
   if (!data) return(-1);

   /* Store device info into the router structure */
   return(c7200_pa_set_drvinfo(router,pa_bay,data));
}

/* Remove a PA-FE-TX from the specified slot */
static int dev_c7200_pa_fe_tx_shutdown(c7200_t *router,u_int pa_bay) 
{
   struct c7200_pa_bay *bay;

   if (!(bay = c7200_pa_get_info(router,pa_bay)))
      return(-1);

   c7200_pa_unset_eeprom(router,pa_bay);
   dev_dec21140_remove(bay->drv_info);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c7200_pa_fe_tx_set_nio(c7200_t *router,u_int pa_bay,
                                      u_int port_id,netio_desc_t *nio)
{
   struct dec21140_data *d;

   if ((port_id > 0) || !(d = c7200_pa_get_drvinfo(router,pa_bay)))
      return(-1);
   
   return(dev_dec21140_set_nio(d,nio));
}

/* Unbind a Network IO descriptor */
static int dev_c7200_pa_fe_tx_unset_nio(c7200_t *router,u_int pa_bay,
                                        u_int port_id)
{
   struct dec21140_data *d;

   if ((port_id > 0) || !(d = c7200_pa_get_drvinfo(router,pa_bay)))
      return(-1);
   
   dev_dec21140_unset_nio(d);
   return(0);
}

/* C7200-IO-FE driver */
struct c7200_pa_driver dev_c7200_io_fe_driver = {
   "C7200-IO-FE", 1, 
   dev_c7200_iocard_init, 
   dev_c7200_iocard_shutdown,
   dev_c7200_iocard_set_nio,
   dev_c7200_iocard_unset_nio,
   NULL,
};

/* PA-FE-TX driver */
struct c7200_pa_driver dev_c7200_pa_fe_tx_driver = {
   "PA-FE-TX", 1, 
   dev_c7200_pa_fe_tx_init, 
   dev_c7200_pa_fe_tx_shutdown,
   dev_c7200_pa_fe_tx_set_nio,
   dev_c7200_pa_fe_tx_unset_nio,
   NULL,
};

/* ====================================================================== */
/* PA-4E / PA-8E                                                          */
/* ====================================================================== */

/* PA-4E/PA-8E data */
struct pa_4e8e_data {
   u_int nr_port;
   struct am79c971_data *port[8];
};

/*
 * dev_c7200_pa_4e_init()
 *
 * Add a PA-4E port adapter into specified slot.
 */
static int dev_c7200_pa_4e_init(c7200_t *router,char *name,u_int pa_bay)
{
   struct pa_4e8e_data *data;
   int i;

   /* Allocate the private data structure for the PA-4E */
   if (!(data = malloc(sizeof(*data)))) {
      fprintf(stderr,"%s (PA-4E): out of memory\n",name);
      return(-1);
   }

   /* 4 Ethernet ports */
   memset(data,0,sizeof(*data));
   data->nr_port = 4;

   /* Set the EEPROM */
   c7200_pa_set_eeprom(router,pa_bay,cisco_eeprom_find_pa("PA-4E"));

   /* Create the AMD Am79c971 chips */
   for(i=0;i<data->nr_port;i++) {
      data->port[i] = dev_am79c971_init(router->vm,name,AM79C971_TYPE_10BASE_T,
                                        router->pa_bay[pa_bay].pci_map,i,
                                        C7200_NETIO_IRQ);
   }

   /* Store device info into the router structure */
   return(c7200_pa_set_drvinfo(router,pa_bay,data));
}

/*
 * dev_c7200_pa_8e_init()
 *
 * Add a PA-8E port adapter into specified slot.
 */
static int dev_c7200_pa_8e_init(c7200_t *router,char *name,u_int pa_bay)
{
   struct pa_4e8e_data *data;
   int i;

   /* Allocate the private data structure for the PA-8E */
   if (!(data = malloc(sizeof(*data)))) {
      fprintf(stderr,"%s (PA-8E): out of memory\n",name);
      return(-1);
   }

   /* 4 Ethernet ports */
   memset(data,0,sizeof(*data));
   data->nr_port = 8;

   /* Set the EEPROM */
   c7200_pa_set_eeprom(router,pa_bay,cisco_eeprom_find_pa("PA-8E"));

   /* Create the AMD Am79c971 chips */
   for(i=0;i<data->nr_port;i++) {
      data->port[i] = dev_am79c971_init(router->vm,name,AM79C971_TYPE_10BASE_T,
                                        router->pa_bay[pa_bay].pci_map,i,
                                        C7200_NETIO_IRQ);
   }

   /* Store device info into the router structure */
   return(c7200_pa_set_drvinfo(router,pa_bay,data));
}

/* Remove a PA-4E/PA-8E from the specified slot */
static int dev_c7200_pa_4e8e_shutdown(c7200_t *router,u_int pa_bay) 
{
   struct c7200_pa_bay *bay;
   struct pa_4e8e_data *data;
   int i;

   if (!(bay = c7200_pa_get_info(router,pa_bay)))
      return(-1);

   data = bay->drv_info;

   /* Remove the PA EEPROM */
   c7200_pa_unset_eeprom(router,pa_bay);

   /* Remove the AMD Am79c971 chips */
   for(i=0;i<data->nr_port;i++)
      dev_am79c971_remove(data->port[i]);

   free(data);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c7200_pa_4e8e_set_nio(c7200_t *router,u_int pa_bay,
                                     u_int port_id,netio_desc_t *nio)
{
   struct pa_4e8e_data *d;

   d = c7200_pa_get_drvinfo(router,pa_bay);

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_am79c971_set_nio(d->port[port_id],nio);
   return(0);
}

/* Unbind a Network IO descriptor */
static int dev_c7200_pa_4e8e_unset_nio(c7200_t *router,u_int pa_bay,
                                       u_int port_id)
{
   struct pa_4e8e_data *d;

   d = c7200_pa_get_drvinfo(router,pa_bay);

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_am79c971_unset_nio(d->port[port_id]);
   return(0);
}

/* PA-4E driver */
struct c7200_pa_driver dev_c7200_pa_4e_driver = {
   "PA-4E", 1, 
   dev_c7200_pa_4e_init, 
   dev_c7200_pa_4e8e_shutdown, 
   dev_c7200_pa_4e8e_set_nio,
   dev_c7200_pa_4e8e_unset_nio,
   NULL,
};

/* PA-8E driver */
struct c7200_pa_driver dev_c7200_pa_8e_driver = {
   "PA-8E", 1, 
   dev_c7200_pa_8e_init, 
   dev_c7200_pa_4e8e_shutdown, 
   dev_c7200_pa_4e8e_set_nio,
   dev_c7200_pa_4e8e_unset_nio,
   NULL,
};
