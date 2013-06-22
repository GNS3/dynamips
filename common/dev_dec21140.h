/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEV_DEC21140_H__
#define __DEV_DEC21140_H__

#include <sys/types.h>

#include "utils.h"
#include "vm.h"
#include "cpu.h"
#include "device.h"
#include "net_io.h"

/* Generic DEC21140 initialization code */
struct dec21140_data *dev_dec21140_init(vm_instance_t *vm,char *name,
                                        struct pci_bus *pci_bus,int pci_device,
                                        int irq);

/* Remove a DEC21140 device */
void dev_dec21140_remove(struct dec21140_data *d);

/* Bind a NIO to DEC21140 device */
int dev_dec21140_set_nio(struct dec21140_data *d,netio_desc_t *nio);

/* Unbind a NIO from a DEC21140 device */
void dev_dec21140_unset_nio(struct dec21140_data *d);

#endif
