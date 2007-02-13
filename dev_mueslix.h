/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEV_MUESLIX_H__
#define __DEV_MUESLIX_H__

#include <sys/types.h>

#include "utils.h"
#include "cpu.h"
#include "vm.h"
#include "device.h"
#include "net_io.h"

/* Number of channels (4 interfaces) */
#define MUESLIX_NR_CHANNELS  4

/* Initialize a Mueslix chip */
struct mueslix_data *
dev_mueslix_init(vm_instance_t *vm,char *name,int chip_mode,
                 struct pci_bus *pci_bus,int pci_device,int irq);

/* Remove a Mueslix device */
void dev_mueslix_remove(struct mueslix_data *d);

/* Bind a NIO to a Mueslix channel */
int dev_mueslix_set_nio(struct mueslix_data *d,u_int channel_id,
                        netio_desc_t *nio);

/* Unbind a NIO from a Mueslix channel */
int dev_mueslix_unset_nio(struct mueslix_data *d,u_int channel_id);

#endif
