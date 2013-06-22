/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __DEV_PLX_H__
#define __DEV_PLX_H__

#include <sys/types.h>
#include "utils.h"
#include "mips64.h"
#include "cpu.h"
#include "device.h"
#include "net_io.h"
#include "vm.h"

/* Forward declaration for PLX private data */
struct plx_data;

/* PLX PCI-to-Local doorbell register callback */
typedef void (*dev_plx_doorbell_cbk)(struct plx_data *d,void *arg,
                                     m_uint32_t val);

/* Create a PLX9060 device */
vm_obj_t *dev_plx9060_init(vm_instance_t *vm,char *name,
                           struct pci_bus *pci_bus,int pci_device,
                           struct vdevice *dev0);

/* Create a PLX9054 device */
vm_obj_t *dev_plx9054_init(vm_instance_t *vm,char *name,
                           struct pci_bus *pci_bus,int pci_device,
                           struct vdevice *dev0,struct vdevice *dev1);

/* Set callback function for PCI-to-Local doorbell register */
void dev_plx_set_pci2loc_doorbell_cbk(struct plx_data *d,
                                      dev_plx_doorbell_cbk wcbk,
                                      void *arg);

/* Set the Local-to-PCI doorbell register (for Local device use) */
void dev_plx_set_loc2pci_doorbell_reg(struct plx_data *d,m_uint32_t value);

#endif
