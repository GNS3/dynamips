/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEV_AM79C971_H__
#define __DEV_AM79C971_H__

#include <sys/types.h>

#include "utils.h"
#include "cpu.h"
#include "vm.h"
#include "device.h"
#include "net_io.h"

/* Interface type */
#define AM79C971_TYPE_100BASE_TX  1  /* 100baseTX */
#define AM79C971_TYPE_10BASE_T    2  /* 10baseT */

/* Generic AMD Am79c971 initialization code */
struct am79c971_data *
dev_am79c971_init(vm_instance_t *vm,char *name,int interface_type,
                  struct pci_bus *pci_bus,int pci_device,int irq);

/* Remove an AMD Am79c971 device */
void dev_am79c971_remove(struct am79c971_data *d);

/* Bind a NIO to an AMD Am79c971 device */
int dev_am79c971_set_nio(struct am79c971_data *d,netio_desc_t *nio);

/* Unbind a NIO from an AMD Am79c971 device */
void dev_am79c971_unset_nio(struct am79c971_data *d);

#endif
