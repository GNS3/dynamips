/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEV_I8255X_H__
#define __DEV_I8255X_H__

#include <sys/types.h>

#include "utils.h"
#include "cpu.h"
#include "vm.h"
#include "device.h"
#include "net_io.h"

/* Generic Intel i8255x initialization code */
struct i8255x_data *
dev_i8255x_init(vm_instance_t *vm,char *name,int interface_type,
                struct pci_bus *pci_bus,int pci_device,int irq);

/* Remove an Intel i8255x device */
void dev_i8255x_remove(struct i8255x_data *d);

/* Bind a NIO to an Intel i8255x device */
int dev_i8255x_set_nio(struct i8255x_data *d,netio_desc_t *nio);

/* Unbind a NIO from an Intel i8255x device */
void dev_i8255x_unset_nio(struct i8255x_data *d);

#endif
