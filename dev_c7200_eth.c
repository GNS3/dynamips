/*  
 * Cisco router simulation platform.
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
#include "dev_i8254x.h"
#include "dev_mv64460.h"
#include "dev_c7200.h"

/* ====================================================================== */
/* C7200-IO-FE EEPROM                                                     */
/* ====================================================================== */

/* C7200-IO-FE: C7200 IOCard with one FastEthernet port EEPROM */
static const m_uint16_t eeprom_c7200_io_fe_data[] = {
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
static int dev_c7200_iocard_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct dec21140_data *data;
   u_int slot = card->slot_id;

   if (slot != 0) {
      vm_error(vm,"cannot put IOCARD in PA bay %u!\n",slot);
      return(-1);
   }

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,&eeprom_c7200_io_fe);
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Create the DEC21140 chip */
   data = dev_dec21140_init(vm,card->dev_name,
                            card->pci_bus,
                            VM_C7200(vm)->npe_driver->dec21140_pci_dev,
                            c7200_net_irq_for_slot_port(slot,0));
   if (!data) return(-1);

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* Remove an IOcard from slot 0 */
static int dev_c7200_iocard_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Remove the PA EEPROM */
   cisco_card_unset_eeprom(card);
   c7200_set_slot_eeprom(VM_C7200(vm),card->slot_id,NULL);

   /* Shutdown the DEC21140 */
   dev_dec21140_remove(card->drv_info);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_c7200_iocard_set_nio(vm_instance_t *vm,struct cisco_card *card,
                                    u_int port_id,netio_desc_t *nio)
{
   struct dec21140_data *d = card->drv_info;

   if (!d || (port_id > 0))
      return(-1);

   return(dev_dec21140_set_nio(d,nio));
}

/* Unbind a Network IO descriptor */
static int dev_c7200_iocard_unset_nio(vm_instance_t *vm,
                                      struct cisco_card *card,
                                      u_int port_id)
{
   struct dec21140_data *d = card->drv_info;

   if (!d || (port_id > 0))
      return(-1);
   
   dev_dec21140_unset_nio(d);
   return(0);
}

/*
 * dev_c7200_pa_fe_tx_init()
 *
 * Add a PA-FE-TX port adapter into specified slot.
 */
static int dev_c7200_pa_fe_tx_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct dec21140_data *data;
   u_int slot = card->slot_id;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_pa("PA-FE-TX"));
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Create the DEC21140 chip */
   data = dev_dec21140_init(vm,card->dev_name,card->pci_bus,0,
                            c7200_net_irq_for_slot_port(slot,0));
   if (!data) return(-1);

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* Remove a PA-FE-TX from the specified slot */
static int 
dev_c7200_pa_fe_tx_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Remove the PA EEPROM */
   cisco_card_unset_eeprom(card);
   c7200_set_slot_eeprom(VM_C7200(vm),card->slot_id,NULL);

   /* Shutdown the DEC21140 */
   dev_dec21140_remove(card->drv_info);
   return(0);
}

/* Bind a Network IO descriptor */
static int 
dev_c7200_pa_fe_tx_set_nio(vm_instance_t *vm,struct cisco_card *card,
                           u_int port_id,netio_desc_t *nio)
{
   struct dec21140_data *d = card->drv_info;

   if (!d || (port_id > 0))
      return(-1);

   return(dev_dec21140_set_nio(d,nio));
}

/* Unbind a Network IO descriptor */
static int 
dev_c7200_pa_fe_tx_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                             u_int port_id)
{
   struct dec21140_data *d = card->drv_info;

   if (!d || (port_id > 0))
      return(-1);
   
   dev_dec21140_unset_nio(d);
   return(0);
}

/* C7200-IO-FE driver */
struct cisco_card_driver dev_c7200_iocard_fe_driver = {
   "C7200-IO-FE", 1, 0, 
   dev_c7200_iocard_init, 
   dev_c7200_iocard_shutdown,
   NULL,
   dev_c7200_iocard_set_nio,
   dev_c7200_iocard_unset_nio,
   NULL,
};

/* PA-FE-TX driver */
struct cisco_card_driver dev_c7200_pa_fe_tx_driver = {
   "PA-FE-TX", 1, 0, 
   dev_c7200_pa_fe_tx_init, 
   dev_c7200_pa_fe_tx_shutdown,
   NULL,
   dev_c7200_pa_fe_tx_set_nio,
   dev_c7200_pa_fe_tx_unset_nio,
   NULL,
};

/* ====================================================================== */
/* PA based on Intel i8254x chips                                         */
/* ====================================================================== */

struct pa_i8254x_data {
   u_int nr_port;
   struct i8254x_data *port[2];
};

/* Remove a PA-2FE-TX from the specified slot */
static int 
dev_c7200_pa_i8254x_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa_i8254x_data *data = card->drv_info;
   int i;

   /* Remove the PA EEPROM */
   cisco_card_unset_eeprom(card);
   c7200_set_slot_eeprom(VM_C7200(vm),card->slot_id,NULL);

   /* Remove the Intel i2854x chips */
   for(i=0;i<data->nr_port;i++)
      dev_i8254x_remove(data->port[i]);

   free(data);
   return(0);
}

/* Bind a Network IO descriptor */
static int 
dev_c7200_pa_i8254x_set_nio(vm_instance_t *vm,struct cisco_card *card,
                            u_int port_id,netio_desc_t *nio)
{
   struct pa_i8254x_data *d = card->drv_info;

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_i8254x_set_nio(d->port[port_id],nio);
   return(0);
}

/* Unbind a Network IO descriptor */
static int 
dev_c7200_pa_i8254x_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                              u_int port_id)
{
   struct pa_i8254x_data *d = card->drv_info;

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_i8254x_unset_nio(d->port[port_id]);
   return(0);
}

/* ====================================================================== */
/* PA-2FE-TX                                                              */
/* ====================================================================== */

/*
 * dev_c7200_pa_2fe_tx_init()
 *
 * Add a PA-2FE-TX port adapter into specified slot.
 */
static int dev_c7200_pa_2fe_tx_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa_i8254x_data *data;
   u_int slot = card->slot_id;
   int i;

   /* Allocate the private data structure for the PA-2FE-TX */
   if (!(data = malloc(sizeof(*data)))) {
      vm_error(vm,"%s: out of memory\n",card->dev_name);
      return(-1);
   }

   /* 2 Ethernet ports */
   memset(data,0,sizeof(*data));
   data->nr_port = 2;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_pa("PA-2FE-TX"));
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Create the Intel i8254x chips */
   for(i=0;i<data->nr_port;i++) {
      data->port[i] = dev_i8254x_init(vm,card->dev_name,0,
                                      card->pci_bus,i,
                                      c7200_net_irq_for_slot_port(slot,i));
   }

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* PA-2FE-TX driver */
struct cisco_card_driver dev_c7200_pa_2fe_tx_driver = {
   "PA-2FE-TX", 0, 0,
   dev_c7200_pa_2fe_tx_init, 
   dev_c7200_pa_i8254x_shutdown, 
   NULL,
   dev_c7200_pa_i8254x_set_nio,
   dev_c7200_pa_i8254x_unset_nio,
   NULL,
};

/* ====================================================================== */
/* PA-GE                                                                  */
/* ====================================================================== */

/*
 * dev_c7200_pa_ge_init()
 *
 * Add a PA-GE port adapter into specified slot.
 */
static int dev_c7200_pa_ge_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa_i8254x_data *data;
   u_int slot = card->slot_id;

   /* Allocate the private data structure for the PA-2FE-TX */
   if (!(data = malloc(sizeof(*data)))) {
      vm_error(vm,"%s: out of memory\n",card->dev_name);
      return(-1);
   }

   /* 2 Ethernet ports */
   memset(data,0,sizeof(*data));
   data->nr_port = 1;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_pa("PA-GE"));
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Create the Intel i8254x chip */
   data->port[0] = dev_i8254x_init(vm,card->dev_name,0,
                                   card->pci_bus,0,
                                   c7200_net_irq_for_slot_port(slot,0));

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* PA-GE driver */
struct cisco_card_driver dev_c7200_pa_ge_driver = {
   "PA-GE", 0, 0,
   dev_c7200_pa_ge_init, 
   dev_c7200_pa_i8254x_shutdown, 
   NULL,
   dev_c7200_pa_i8254x_set_nio,
   dev_c7200_pa_i8254x_unset_nio,
   NULL,
};

/* ====================================================================== */
/* C7200-IO-2FE                                                           */
/* ====================================================================== */

/* C7200-IO-2FE/E: C7200 IOCard with two FastEthernet ports EEPROM */
static const m_uint16_t eeprom_c7200_io_2fe_data[] = {
   0x04FF, 0x4002, 0x1541, 0x0201, 0xC046, 0x0320, 0x001B, 0xCA06,
   0x8249, 0x138B, 0x0642, 0x4230, 0xC18B, 0x3030, 0x3030, 0x3030,
   0x3030, 0x0000, 0x0004, 0x0002, 0x0385, 0x1C0D, 0x7F03, 0xCB8F,
   0x4337, 0x3230, 0x302D, 0x492F, 0x4F2D, 0x3246, 0x452F, 0x4580,
   0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

static const struct cisco_eeprom eeprom_c7200_io_2fe = {
   "C7200-IO-2FE", (m_uint16_t *)eeprom_c7200_io_2fe_data,
   sizeof(eeprom_c7200_io_2fe_data)/2,
};

/*
 * dev_c7200_pa_2fe_tx_init()
 *
 * Add a C7200-IO-2FE/E port adapter into specified slot.
 */
static int dev_c7200_iocard_2fe_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa_i8254x_data *data;
   u_int slot = card->slot_id;

   /* Allocate the private data structure for the iocard */
   if (!(data = malloc(sizeof(*data)))) {
      vm_error(vm,"%s: out of memory\n",card->dev_name);
      return(-1);
   }

   /* 2 Ethernet ports */
   memset(data,0,sizeof(*data));
   data->nr_port = 2;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,&eeprom_c7200_io_2fe);
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Port Fa0/0 is on PCI Device 1 */
   data->port[0] = dev_i8254x_init(vm,card->dev_name,0,
                                   card->pci_bus,1,
                                   c7200_net_irq_for_slot_port(slot,1));

   /* Port Fa0/1 is on PCI Device 0 */
   data->port[1] = dev_i8254x_init(vm,card->dev_name,0,
                                   card->pci_bus,0,
                                   c7200_net_irq_for_slot_port(slot,0));

   if (!data->port[0] || !data->port[1]) {
      dev_i8254x_remove(data->port[0]);
      dev_i8254x_remove(data->port[1]);
      free(data);
      return(-1);
   }

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* C7200-IO-2FE driver */
struct cisco_card_driver dev_c7200_iocard_2fe_driver = {
   "C7200-IO-2FE", 0, 0,
   dev_c7200_iocard_2fe_init, 
   dev_c7200_pa_i8254x_shutdown, 
   NULL,
   dev_c7200_pa_i8254x_set_nio,
   dev_c7200_pa_i8254x_unset_nio,
   NULL,
};

/* ====================================================================== */
/* C7200-IO-GE-E                                                          */
/* ====================================================================== */

/* 
 * C7200-IO-GE+E: C7200 IOCard with 1 GigatEthernet ports 
 * and 1 Ethernet port EEPROM.
 */
static const m_uint16_t eeprom_c7200_io_ge_e_data[] = {
   0x04FF, 0x4002, 0x1641, 0x0201, 0xC046, 0x0320, 0x001B, 0xCA06,
   0x8249, 0x138B, 0x0642, 0x4230, 0xC18B, 0x3030, 0x3030, 0x3030,
   0x3030, 0x0000, 0x0004, 0x0002, 0x0385, 0x1C0D, 0x7F03, 0xCB8F,
   0x4337, 0x3230, 0x302D, 0x492F, 0x4F2D, 0x3246, 0x452F, 0x4580,
   0x0000, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

static const struct cisco_eeprom eeprom_c7200_io_ge_e = {
   "C7200-IO-GE-E", (m_uint16_t *)eeprom_c7200_io_ge_e_data,
   sizeof(eeprom_c7200_io_ge_e_data)/2,
};

/*
 * dev_c7200_pa_ge_e_tx_init()
 *
 * Add a C7200-I/O-GE+E port adapter into specified slot.
 */
static int 
dev_c7200_iocard_ge_e_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa_i8254x_data *data;
   u_int slot = card->slot_id;

   /* Allocate the private data structure for the iocard */
   if (!(data = malloc(sizeof(*data)))) {
      vm_error(vm,"%s: out of memory\n",card->dev_name);
      return(-1);
   }

   /* 2 Ethernet ports */
   memset(data,0,sizeof(*data));
   data->nr_port = 2;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,&eeprom_c7200_io_ge_e);
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Port Gi0/0 is on PCI Device 1 */
   data->port[0] = dev_i8254x_init(vm,card->dev_name,0,
                                   card->pci_bus,1,
                                   c7200_net_irq_for_slot_port(slot,1));

   /* Port e0/0 is on PCI Device 0 */
   data->port[1] = dev_i8254x_init(vm,card->dev_name,0,
                                   card->pci_bus,0,
                                   c7200_net_irq_for_slot_port(slot,0));

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* C7200-IO-GE-E driver */
struct cisco_card_driver dev_c7200_iocard_ge_e_driver = {
   "C7200-IO-GE-E", 0, 0,
   dev_c7200_iocard_ge_e_init, 
   dev_c7200_pa_i8254x_shutdown, 
   NULL,
   dev_c7200_pa_i8254x_set_nio,
   dev_c7200_pa_i8254x_unset_nio,
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
static int dev_c7200_pa_4e_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa_4e8e_data *data;
   u_int slot = card->slot_id;
   int i;

   /* Allocate the private data structure for the PA-4E */
   if (!(data = malloc(sizeof(*data)))) {
      vm_error(vm,"%s: out of memory\n",card->dev_name);
      return(-1);
   }

   /* 4 Ethernet ports */
   memset(data,0,sizeof(*data));
   data->nr_port = 4;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_pa("PA-4E"));
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Create the AMD Am79c971 chips */
   for(i=0;i<data->nr_port;i++) {
      data->port[i] = dev_am79c971_init(vm,card->dev_name,
                                        AM79C971_TYPE_10BASE_T,
                                        card->pci_bus,i,
                                        c7200_net_irq_for_slot_port(slot,i));
   }

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/*
 * dev_c7200_pa_8e_init()
 *
 * Add a PA-8E port adapter into specified slot.
 */
static int dev_c7200_pa_8e_init(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa_4e8e_data *data;
   u_int slot = card->slot_id;
   int i;

   /* Allocate the private data structure for the PA-8E */
   if (!(data = malloc(sizeof(*data)))) {
      vm_error(vm,"%s: out of memory\n",card->dev_name);
      return(-1);
   }

   /* 4 Ethernet ports */
   memset(data,0,sizeof(*data));
   data->nr_port = 8;

   /* Set the PCI bus */
   card->pci_bus = vm->slots_pci_bus[slot];

   /* Set the EEPROM */
   cisco_card_set_eeprom(vm,card,cisco_eeprom_find_pa("PA-8E"));
   c7200_set_slot_eeprom(VM_C7200(vm),slot,&card->eeprom);

   /* Create the AMD Am79c971 chips */
   for(i=0;i<data->nr_port;i++) {
      data->port[i] = dev_am79c971_init(vm,card->dev_name,
                                        AM79C971_TYPE_10BASE_T,
                                        card->pci_bus,i,
                                        c7200_net_irq_for_slot_port(slot,i));
   }

   /* Store device info into the router structure */
   card->drv_info = data;
   return(0);
}

/* Remove a PA-4E/PA-8E from the specified slot */
static int 
dev_c7200_pa_4e8e_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   struct pa_4e8e_data *data = card->drv_info;
   int i;

   /* Remove the PA EEPROM */
   cisco_card_unset_eeprom(card);
   c7200_set_slot_eeprom(VM_C7200(vm),card->slot_id,NULL);

   /* Remove the AMD Am79c971 chips */
   for(i=0;i<data->nr_port;i++)
      dev_am79c971_remove(data->port[i]);

   free(data);
   return(0);
}

/* Bind a Network IO descriptor */
static int 
dev_c7200_pa_4e8e_set_nio(vm_instance_t *vm,struct cisco_card *card,
                          u_int port_id,netio_desc_t *nio)
{
   struct pa_4e8e_data *d = card->drv_info;

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_am79c971_set_nio(d->port[port_id],nio);
   return(0);
}

/* Unbind a Network IO descriptor */
static int 
dev_c7200_pa_4e8e_unset_nio(vm_instance_t *vm,struct cisco_card *card,
                            u_int port_id)
{
   struct pa_4e8e_data *d = card->drv_info;

   if (!d || (port_id >= d->nr_port))
      return(-1);

   dev_am79c971_unset_nio(d->port[port_id]);
   return(0);
}

/* PA-4E driver */
struct cisco_card_driver dev_c7200_pa_4e_driver = {
   "PA-4E", 1, 0,
   dev_c7200_pa_4e_init, 
   dev_c7200_pa_4e8e_shutdown, 
   NULL,
   dev_c7200_pa_4e8e_set_nio,
   dev_c7200_pa_4e8e_unset_nio,
   NULL,
};

/* PA-8E driver */
struct cisco_card_driver dev_c7200_pa_8e_driver = {
   "PA-8E", 1, 0,
   dev_c7200_pa_8e_init, 
   dev_c7200_pa_4e8e_shutdown, 
   NULL,
   dev_c7200_pa_4e8e_set_nio,
   dev_c7200_pa_4e8e_unset_nio,
   NULL,
};

/* ====================================================================== */
/* NPE-G2                                                                 */
/* ====================================================================== */

/* Initialize NPE-G2 Ethernet ports */
static int dev_c7200_npeg2_init(vm_instance_t *vm,struct cisco_card *card)
{
   /* Nothing to do */
   card->drv_info = VM_C7200(vm)->mv64460_sysctr;
   return(0);
}

/* Shutdown NPE-G2 Ethernet ports */
static int dev_c7200_npeg2_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Nothing to do */
   return(0);
}

/* Set a NIO descriptor */
static int dev_c7200_npeg2_set_nio(vm_instance_t *vm,struct cisco_card *card,
                                   u_int port_id,netio_desc_t *nio)
{
   c7200_t *router = VM_C7200(vm);
   struct mv64460_data *mv_data = router->mv64460_sysctr;

   if (!mv_data)
      return(-1);

   dev_mv64460_eth_set_nio(mv_data,port_id,nio);
   return(0);
}

/* Unbind a NIO descriptor */
static int dev_c7200_npeg2_unset_nio(vm_instance_t *vm,
                                     struct cisco_card *card,
                                     u_int port_id)
{
   c7200_t *router = VM_C7200(vm);
   struct mv64460_data *mv_data = router->mv64460_sysctr;

   if (!mv_data)
      return(-1);

   dev_mv64460_eth_unset_nio(mv_data,port_id);
   return(0);
}

/* NPE-G2 driver */
struct cisco_card_driver dev_c7200_npeg2_driver = {
   "NPE-G2", 1, 0,
   dev_c7200_npeg2_init, 
   dev_c7200_npeg2_shutdown, 
   NULL,
   dev_c7200_npeg2_set_nio,
   dev_c7200_npeg2_unset_nio,
   NULL,
};
