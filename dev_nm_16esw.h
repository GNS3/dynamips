/*
 * Cisco simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEV_NM_16ESW_H__
#define __DEV_NM_16ESW_H__

#include <sys/types.h>

#include "utils.h"
#include "cpu.h"
#include "vm.h"
#include "device.h"
#include "net_io.h"
#include "cisco_eeprom.h"

/* Forward declaration for ESW card private data */
struct esw_data;

/* Rewrite the base MAC address */
int esw_burn_mac_addr(vm_instance_t *vm,u_int nm_bay,
                      struct cisco_eeprom *eeprom);

/* Delete an ESW card */
void esw_remove(struct esw_data *d);

/* Show debugging information */
int esw_show_info(struct esw_data *d);

/* Bind a Network IO descriptor */
int esw_set_nio(struct esw_data *d,u_int port_id,netio_desc_t *nio);

/* Unbind a Network IO descriptor */
int esw_unset_nio(struct esw_data *d,u_int port_id);

/* Initialize a NM-16ESW module */
struct esw_data *
dev_nm_16esw_init(vm_instance_t *vm,char *name,u_int nm_bay,
                  struct pci_bus *pci_bus,int pci_device,int irq);

/* Initialize a NMD-36ESW module */
struct esw_data *
dev_nmd_36esw_init(vm_instance_t *vm,char *name,u_int nm_bay,
                   struct pci_bus *pci_bus,int pci_device,int irq);

#endif
