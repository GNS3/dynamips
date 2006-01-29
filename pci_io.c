/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * PCI I/O space.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

/* Add a new PCI I/O device */
int pci_io_add(struct pci_io_data *d,m_uint32_t start,m_uint32_t end,
               struct vdevice *dev,dev_handler_t handler)
{
   struct pci_io_dev *p;

   if (!(p = malloc(sizeof(*p)))) {
      fprintf(stderr,"pci_io_add: unable to create a new device.\n");
      return(-1);
   }

   p->start    = start;
   p->end      = end;
   p->real_dev = dev;
   p->handler  = handler;

   p->next = d->dev_list;
   d->dev_list = p;
   return(0);
}            

/*
 * pci_io_access()
 */
static void *pci_io_access(cpu_mips_t *cpu,struct vdevice *dev,
                           m_uint32_t offset,u_int op_size,u_int op_type,
                           m_uint64_t *data)
{
   struct pci_io_data *d = dev->priv_data;
   struct pci_io_dev *p;

   if (op_type == MTS_READ)
      *data = 0;

   for(p=d->dev_list;p;p=p->next)
      if ((offset >= p->start) && (offset <= p->end))
         return(p->handler(cpu,p->real_dev,offset,op_size,op_type,data));

   return NULL;
}

/* Initialize PCI I/O space */
struct pci_io_data *pci_io_init(cpu_group_t *cpu_group,m_uint64_t paddr)
{
   struct vdevice *dev;
   struct pci_io_data *d;

   if (!(dev = dev_create("pci_io"))) {
      fprintf(stderr,"PCI_IO: unable to create device.\n");
      return NULL;
   }

   /* allocate the private data structure */
   if (!(d = malloc(sizeof(*d)))) {
      fprintf(stderr,"PCI_IO: out of memory\n");
      return NULL;
   }

   memset(d,0,sizeof(*d));

   dev->phys_addr = paddr;
   dev->phys_len  = 2 * 1048576;
   dev->handler   = pci_io_access;
   dev->priv_data = d;
   
   /* Map this device to all CPU */
   cpu_group_bind_device(cpu_group,dev);
   return(d);
}
