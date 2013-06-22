/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco card routines and definitions.
 */

#ifndef __CISCO_CARD_H__
#define __CISCO_CARD_H__

#include <pthread.h>

#include "utils.h"
#include "net.h"
#include "net_io.h"
#include "cisco_eeprom.h"

#define CISCO_CARD_MAX_WIC  8
#define CISCO_CARD_MAX_SUBSLOTS  16

/* Card types */
enum {
   CISCO_CARD_TYPE_UNDEF = 0,
   CISCO_CARD_TYPE_PA,
   CISCO_CARD_TYPE_NM,
   CISCO_CARD_TYPE_WIC,
};

/* Card flags */
enum {
   CISCO_CARD_FLAG_OVERRIDE = 1,
};

/* Forward declarations */
struct cisco_card;
struct cisco_card_driver;

/* Prototype of card driver initialization function */
typedef int (*cisco_card_init_fn)(vm_instance_t *vm,struct cisco_card *card);

/* Prototype of card driver shutdown function */
typedef int (*cisco_card_shutdown_fn)(vm_instance_t *vm,
                                      struct cisco_card *card);

/* Prototype of card NIO get sub-slot info function */
typedef int 
(*cisco_card_get_sub_info_fn)(vm_instance_t *vm,struct cisco_card *card,
                              u_int port_id,
                              struct cisco_card_driver ***drv_array,
                              u_int *subcard_type);

/* Prototype of card NIO set function */
typedef int (*cisco_card_set_nio_fn)(vm_instance_t *vm,struct cisco_card *card,
                                     u_int port_id,netio_desc_t *nio);

/* Prototype of card NIO unset function */
typedef int (*cisco_card_unset_nio_fn)(vm_instance_t *vm,
                                       struct cisco_card *card,
                                       u_int port_id);

/* Prototype of card NIO show info function */
typedef int (*cisco_card_show_info_fn)(vm_instance_t *vm,
                                       struct cisco_card *card);

/* Cisco NIO binding to a slot/port */
struct cisco_nio_binding {
   netio_desc_t *nio;
   u_int port_id,orig_port_id;
   struct cisco_nio_binding *prev,*next;
};

/* Generic Cisco card driver */
struct cisco_card_driver {
   char *dev_type;
   int supported;
   int wic_slots;
   cisco_card_init_fn card_init;
   cisco_card_shutdown_fn card_shutdown;
   cisco_card_get_sub_info_fn card_get_sub_info;
   cisco_card_set_nio_fn card_set_nio;
   cisco_card_unset_nio_fn card_unset_nio;
   cisco_card_show_info_fn card_show_info;
};

/* Generic Cisco card */
struct cisco_card {
   char *dev_name;                       /* Device name */
   char *dev_type;                       /* Device Type */
   u_int card_type;                      /* Card type (NM,PA,WIC,...) */
   u_int card_flags;                     /* Card flags */
   u_int card_id;                        /* Card ID (slot or sub-slot) */
   u_int slot_id,subslot_id;             /* Slot and Sub-slot ID */
   struct cisco_eeprom eeprom;           /* EEPROM */
   struct pci_bus *pci_bus;              /* PCI bus */
   struct cisco_card_driver *driver;     /* Driver */
   void *drv_info;                       /* Private driver info */
   struct cisco_nio_binding *nio_list;   /* NIO bindings to ports */
   struct cisco_card *parent;            /* Parent card */
   struct cisco_card *sub_slots[CISCO_CARD_MAX_SUBSLOTS];   /* Sub-slots */
};

/* Set EEPROM definition for the specified Cisco card */
int cisco_card_set_eeprom(vm_instance_t *vm,struct cisco_card *card,
                          const struct cisco_eeprom *eeprom);

/* Unset EEPROM definition */
int cisco_card_unset_eeprom(struct cisco_card *card);

/* Check if a card has a valid EEPROM defined */
int cisco_card_check_eeprom(struct cisco_card *card);

/* Get slot info */
struct cisco_card *vm_slot_get_card_ptr(vm_instance_t *vm,u_int slot_id);

/* Check if a slot has an active card */
int vm_slot_active(vm_instance_t *vm,u_int slot_id,u_int port_id);

/* Set a flag for a card */
int vm_slot_set_flag(vm_instance_t *vm,u_int slot_id,u_int port_id,u_int flag);

/* Add a slot binding */
int vm_slot_add_binding(vm_instance_t *vm,char *dev_type,
                        u_int slot_id,u_int port_id);

/* Remove a slot binding */
int vm_slot_remove_binding(vm_instance_t *vm,u_int slot_id,u_int port_id);

/* Add a network IO binding */
int vm_slot_add_nio_binding(vm_instance_t *vm,u_int slot_id,u_int port_id,
                            char *nio_name);

/* Remove a NIO binding */
int vm_slot_remove_nio_binding(vm_instance_t *vm,u_int slot_id,u_int port_id);

/* Remove all NIO bindings for the specified slot (sub-slots included) */
int vm_slot_remove_all_nio_bindings(vm_instance_t *vm,u_int slot_id);

/* Enable a Network IO descriptor for the specified slot */
int vm_slot_enable_nio(vm_instance_t *vm,u_int slot_id,u_int port_id);

/* Disable Network IO descriptor for the specified slot */
int vm_slot_disable_nio(vm_instance_t *vm,u_int slot_id,u_int port_id);

/* Enable all NIO for the specified slot (sub-slots included) */
int vm_slot_enable_all_nio(vm_instance_t *vm,u_int slot_id);

/* Disable all NIO for the specified slot (sub-slots included) */
int vm_slot_disable_all_nio(vm_instance_t *vm,u_int slot_id);

/* Initialize the specified slot (sub-slots included) */
int vm_slot_init(vm_instance_t *vm,u_int slot_id);

/* Initialize all slots of a VM */
int vm_slot_init_all(vm_instance_t *vm);

/* Shutdown the specified slot (sub-slots included) */
int vm_slot_shutdown(vm_instance_t *vm,u_int slot_id);

/* Shutdown all slots of a VM */
int vm_slot_shutdown_all(vm_instance_t *vm);

/* Show info about the specified slot (sub-slots included) */
int vm_slot_show_info(vm_instance_t *vm,u_int slot_id);

/* Show info about all slots */
int vm_slot_show_all_info(vm_instance_t *vm);

/* Check if the specified slot has a valid EEPROM defined */
int vm_slot_check_eeprom(vm_instance_t *vm,u_int slot_id,u_int port_id);

/* Returns the EEPROM data of the specified slot */
struct cisco_eeprom *
vm_slot_get_eeprom(vm_instance_t *vm,u_int slot_id,u_int port_id);

/* Save config for the specified slot (sub-slots included) */
int vm_slot_save_config(vm_instance_t *vm,u_int slot_id,FILE *fd);

/* Save config for all slots */
int vm_slot_save_all_config(vm_instance_t *vm,FILE *fd);

/* Show slot drivers */
int vm_slot_show_drivers(vm_instance_t *vm);

/* Create a Network Module (command line) */
int vm_slot_cmd_create(vm_instance_t *vm,char *str);

/* Add a Network IO descriptor binding (command line) */
int vm_slot_cmd_add_nio(vm_instance_t *vm,char *str);

#endif
