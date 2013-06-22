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

/* Forward declaration for NM-16ESW private data */
struct nm_16esw_data;

/* Rewrite the base MAC address */
int dev_nm_16esw_burn_mac_addr(vm_instance_t *vm,u_int nm_bay,
                               struct cisco_eeprom *eeprom);

/* Initialize a NM-16ESW module */
struct nm_16esw_data *
dev_nm_16esw_init(vm_instance_t *vm,char *name,u_int nm_bay,
                  struct pci_bus *pci_bus,int pci_device,int irq);

/* Remove a NM-16ESW from the specified slot */
int dev_nm_16esw_remove(struct nm_16esw_data *data);

/* Bind a Network IO descriptor */
int dev_nm_16esw_set_nio(struct nm_16esw_data *d,u_int port_id,
                         netio_desc_t *nio);

/* Unbind a Network IO descriptor */
int dev_nm_16esw_unset_nio(struct nm_16esw_data *d,u_int port_id);

/* Show debugging information */
int dev_nm_16esw_show_info(struct nm_16esw_data *d);

#endif
