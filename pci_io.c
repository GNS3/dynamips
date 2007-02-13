/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * PCI I/O space.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "pci_io.h"

/* Debugging flags */
#define DEBUG_ACCESS  0

/* Add a new PCI I/O device */
struct pci_io_device *pci_io_add(struct pci_io_data *d,
                                 m_uint32_t start,m_uint32_t end,
                                 struct vdevice *dev,dev_handler_t handler)
{
   struct pci_io_device *p;

   if (!(p = malloc(sizeof(*p)))) {
      fprintf(stderr,"pci_io_add: unable to create a new device.\n");
      return NULL;
   }

   p->start    = start;
   p->end      = end;
   p->real_dev = dev;
   p->handler  = handler;

   p->next = d->dev_list;
   p->pprev = &d->dev_list;

   if (d->dev_list != NULL)
      d->dev_list->pprev = &p->next;

   d->dev_list = p;
   return p;
}            

/* Remove a PCI I/O device */
void pci_io_remove(struct pci_io_device *dev)
{
   if (dev != NULL) {
      if (dev->next)
         dev->next->pprev = dev->pprev;

      *(dev->pprev) = dev->next;
      free(dev);
   }
}

/*
 * pci_io_access()
 */
static void *pci_io_access(cpu_gen_t *cpu,struct vdevice *dev,
                           m_uint32_t offset,u_int op_size,u_int op_type,
                           m_uint64_t *data)
{
   struct pci_io_data *d = dev->priv_data;
   struct pci_io_device *p;

#if DEBUG_ACCESS
   if (op_type == MTS_READ) {
      cpu_log(cpu,"PCI_IO","read request at pc=0x%llx, offset=0x%x\n",
              cpu_get_pc(cpu),offset);
   } else {
      cpu_log(cpu,"PCI_IO",
              "write request (data=0x%llx) at pc=0x%llx, offset=0x%x\n",
              *data,cpu_get_pc(cpu),offset);
   }
#endif

   if (op_type == MTS_READ)
      *data = 0;

   for(p=d->dev_list;p;p=p->next)
      if ((offset >= p->start) && (offset <= p->end)) {
         return(p->handler(cpu,p->real_dev,(offset - p->start),
                           op_size,op_type,data));
      }

   return NULL;
}

/* Remove PCI I/O space */
void pci_io_data_remove(vm_instance_t *vm,struct pci_io_data *d)
{
   if (d != NULL) {
      /* Remove the device */
      dev_remove(vm,&d->dev);

      /* Free the structure itself */
      free(d);
   }
}

/* Initialize PCI I/O space */
struct pci_io_data *pci_io_data_init(vm_instance_t *vm,m_uint64_t paddr)
{
   struct pci_io_data *d;

   /* Allocate the PCI I/O data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"PCI_IO: out of memory\n");
      return NULL;
   }

   memset(d,0,sizeof(*d));
   dev_init(&d->dev);
   d->dev.name      = "pci_io";
   d->dev.priv_data = d;
   d->dev.phys_addr = paddr;
   d->dev.phys_len  = 2 * 1048576;
   d->dev.handler   = pci_io_access;
   
   /* Map this device to the VM */
   vm_bind_device(vm,&d->dev);
   return(d);
}
