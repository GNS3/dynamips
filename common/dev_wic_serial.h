/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * WIC-1T & WIC-2T devices.
 */

#ifndef __DEV_WIC_SERIAL_H__
#define __DEV_WIC_SERIAL_H__

#include <sys/types.h>

#include "utils.h"
#include "vm.h"
#include "cpu.h"
#include "device.h"

enum {
   WIC_SERIAL_MODEL_1T = 1,
   WIC_SERIAL_MODEL_2T,
};

/* Create a WIC serial device */
struct wic_serial_data *
dev_wic_serial_init(vm_instance_t *vm,char *name,u_int model,
                    m_uint64_t paddr,m_uint32_t len);

/* Remove a WIC serial device */
void dev_wic_serial_remove(struct wic_serial_data *d);

#endif
