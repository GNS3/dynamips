/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Generic MSFC1 routines and definitions (EEPROM,...).
 *
 * This is not a working platform! I only added it to play, since it is very
 * similar to an NPE-200. I think that could work with a functional CatOS SP.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "pci_io.h"
#include "dev_gt.h"
#include "cisco_eeprom.h"
#include "dev_rom.h"
#include "dev_dec21140.h"
#include "dev_i8254x.h"
#include "dev_msfc1.h"
#include "dev_vtty.h"
#include "registry.h"
#include "net.h"

/* MSFC1 EEPROM */
static m_uint16_t eeprom_msfc1_data[128] = {
   0xabab, 0x0190, 0x1262, 0x0100, 0x0002, 0x6003, 0x00cf, 0x4369,
   0x7363, 0x6f20, 0x5379, 0x7374, 0x656d, 0x732c, 0x2049, 0x6e63,
   0x2e00, 0x5753, 0x2d46, 0x3630, 0x3031, 0x2d52, 0x5346, 0x4300,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x3733, 0x2d37, 0x3135,
   0x302d, 0x3036, 0x0000, 0x0000, 0x0000, 0x4130, 0x3100, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x012d, 0x0000, 0x0000, 0x0009, 0x0005, 0x0001,
   0x0003, 0x0001, 0x0001, 0x0002, 0x00cf, 0xffbf, 0x0000, 0x0000,
   0x6003, 0x0162, 0x0afd, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0005, 0x00e0, 0xaabb, 0xcc00, 0x0100,
   0x0100, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x1401, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
   0x1000, 0x4b3c, 0x4132, 0x8080, 0x8080, 0x8080, 0x8080, 0x8080,
   0x8080, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
};

static struct cisco_eeprom msfc1_eeprom = {
   "msfc1", eeprom_msfc1_data, sizeof(eeprom_msfc1_data)/2,
};

/* ====================================================================== */
/* EOBC - Ethernet Out of Band Channel                                    */
/* ====================================================================== */
static int dev_msfc1_eobc_init(msfc1_t *router,char *name,u_int pa_bay)
{
   vm_instance_t *vm = router->vm;
   struct dec21140_data *data;

   /* Create the DEC21140 chip */
   data = dev_dec21140_init(vm,name,vm->pci_bus[0],6,MSFC1_NETIO_IRQ);
   if (!data) return(-1);

   /* Store device info into the router structure */
   return(msfc1_pa_set_drvinfo(router,pa_bay,data));
}

/* Remove EOBC */
static int dev_msfc1_eobc_shutdown(msfc1_t *router,u_int pa_bay)
{
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   dev_dec21140_remove(bay->drv_info);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_msfc1_eobc_set_nio(msfc1_t *router,u_int pa_bay,u_int port_id,
                                  netio_desc_t *nio)
{
   struct dec21140_data *d;

   if ((port_id != 0) || !(d = msfc1_pa_get_drvinfo(router,pa_bay)))
      return(-1);

   return(dev_dec21140_set_nio(d,nio));
}

/* Unbind a Network IO descriptor */
static int dev_msfc1_eobc_unset_nio(msfc1_t *router,u_int pa_bay,
                                    u_int port_id)
{
   struct dec21140_data *d;

   if ((port_id != 0) || !(d = msfc1_pa_get_drvinfo(router,pa_bay)))
      return(-1);

   dev_dec21140_unset_nio(d);
   return(0);
}

/* EOBC driver */
struct msfc1_pa_driver dev_msfc1_eobc = {
   "MSFC1_EOBC", 0,
   dev_msfc1_eobc_init,
   dev_msfc1_eobc_shutdown,
   dev_msfc1_eobc_set_nio,
   dev_msfc1_eobc_unset_nio,
   NULL,
};

/* ====================================================================== */
/* IBC - InBand Channel                                                   */
/* ====================================================================== */
static int dev_msfc1_ibc_init(msfc1_t *router,char *name,u_int pa_bay)
{
   vm_instance_t *vm = router->vm;
   struct i8254x_data *data;

   /* Create the Intel Wiseman/Livengood chip */
   data = dev_i8254x_init(vm,name,0,vm->pci_bus_pool[24],1,MSFC1_NETIO_IRQ);
   if (!data) return(-1);

   /* Store device info into the router structure */
   return(msfc1_pa_set_drvinfo(router,pa_bay,data));
}

/* Remove EOBC */
static int dev_msfc1_ibc_shutdown(msfc1_t *router,u_int pa_bay)
{
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   dev_i8254x_remove(bay->drv_info);
   return(0);
}

/* Bind a Network IO descriptor */
static int dev_msfc1_ibc_set_nio(msfc1_t *router,u_int pa_bay,u_int port_id,
                                 netio_desc_t *nio)
{
   struct i8254x_data *d;

   if ((port_id != 1) || !(d = msfc1_pa_get_drvinfo(router,pa_bay)))
      return(-1);

   return(dev_i8254x_set_nio(d,nio));
}

/* Unbind a Network IO descriptor */
static int dev_msfc1_ibc_unset_nio(msfc1_t *router,u_int pa_bay,
                                   u_int port_id)
{
   struct i8254x_data *d;

   if ((port_id != 0) || !(d = msfc1_pa_get_drvinfo(router,pa_bay)))
      return(-1);

   dev_i8254x_unset_nio(d);
   return(0);
}

/* IBC driver */
struct msfc1_pa_driver dev_msfc1_ibc = {
   "MSFC1_IBC", 0,
   dev_msfc1_ibc_init,
   dev_msfc1_ibc_shutdown,
   dev_msfc1_ibc_set_nio,
   dev_msfc1_ibc_unset_nio,
   NULL,
};

/* ======================================================================== */
/* Port Adapter Drivers                                                     */
/* ======================================================================== */
static struct msfc1_pa_driver *pa_drivers[] = {
   &dev_msfc1_eobc,
   &dev_msfc1_ibc,
   NULL,
};

/* ======================================================================== */
/* MSFC1 router instances                                                   */
/* ======================================================================== */

/* Directly extract the configuration from the NVRAM device */
ssize_t msfc1_nvram_extract_config(vm_instance_t *vm,char **buffer)
{   
   u_char *base_ptr,*ios_ptr,*cfg_ptr,*end_ptr;
   m_uint32_t start,end,nvlen,clen;
   m_uint16_t magic1,magic2; 
   struct vdevice *nvram_dev;
   m_uint64_t nvram_addr;
   off_t nvram_size;
   int fd;

   if ((nvram_dev = dev_get_by_name(vm,"nvram")))
      dev_sync(nvram_dev);

   fd = vm_mmap_open_file(vm,"nvram",&base_ptr,&nvram_size);

   if (fd == -1)
      return(-1);

   nvram_addr = MSFC1_NVRAM_ADDR;
   ios_ptr = base_ptr + vm->nvram_rom_space;
   end_ptr = base_ptr + nvram_size;

   if ((ios_ptr + 0x30) >= end_ptr) {
      vm_error(vm,"NVRAM file too small\n");
      return(-1);
   }

   magic1  = ntohs(*PTR_ADJUST(m_uint16_t *,ios_ptr,0x06));
   magic2  = ntohs(*PTR_ADJUST(m_uint16_t *,ios_ptr,0x08));

   if ((magic1 != 0xF0A5) || (magic2 != 0xABCD)) {
      vm_error(vm,"unable to find IOS magic numbers (0x%x,0x%x)!\n",
               magic1,magic2);
      return(-1);
   }

   start = ntohl(*PTR_ADJUST(m_uint32_t *,ios_ptr,0x10)) + 1;
   end   = ntohl(*PTR_ADJUST(m_uint32_t *,ios_ptr,0x14));
   nvlen = ntohl(*PTR_ADJUST(m_uint32_t *,ios_ptr,0x18));
   clen  = end - start;

   if ((clen + 1) != nvlen) {
      vm_error(vm,"invalid configuration size (0x%x)\n",nvlen);
      return(-1);
   }

   if (!(*buffer = malloc(clen+1))) {
      vm_error(vm,"unable to allocate config buffer (%u bytes)\n",clen);
      return(-1);
   }

   cfg_ptr = base_ptr + (start - nvram_addr);

   if ((start < nvram_addr) || ((cfg_ptr + clen) > end_ptr)) {
      vm_error(vm,"NVRAM file too small\n");
      return(-1);
   }

   memcpy(*buffer,cfg_ptr,clen);
   (*buffer)[clen] = 0;
   return(clen);
}

/* Directly push the IOS configuration to the NVRAM device */
int msfc1_nvram_push_config(vm_instance_t *vm,char *buffer,size_t len)
{  
   u_char *base_ptr,*ios_ptr,*cfg_ptr;
   m_uint32_t cfg_addr,cfg_offset;
   m_uint32_t nvram_addr,cklen;
   m_uint16_t cksum;
   int fd;

   fd = vm_mmap_create_file(vm,"nvram",vm->nvram_size*1024,&base_ptr);

   if (fd == -1)
      return(-1);

   cfg_offset = 0x2c;
   ios_ptr = base_ptr + vm->nvram_rom_space;
   cfg_ptr = ios_ptr  + cfg_offset;

   nvram_addr = MSFC1_NVRAM_ADDR;
   cfg_addr = nvram_addr + vm->nvram_rom_space + cfg_offset;

   /* Write IOS tag, uncompressed config... */
   *PTR_ADJUST(m_uint16_t *,ios_ptr,0x06) = htons(0xF0A5);
   *PTR_ADJUST(m_uint16_t *,ios_ptr,0x08) = htons(0xABCD);
   *PTR_ADJUST(m_uint16_t *,ios_ptr,0x0a) = htons(0x0001);
   *PTR_ADJUST(m_uint16_t *,ios_ptr,0x0c) = htons(0x0000);
   *PTR_ADJUST(m_uint16_t *,ios_ptr,0x0e) = htons(0x0000);

   /* Store file contents to NVRAM */
   memcpy(cfg_ptr,buffer,len);

   /* Write config addresses + size */
   *PTR_ADJUST(m_uint32_t *,ios_ptr,0x10) = htonl(cfg_addr);
   *PTR_ADJUST(m_uint32_t *,ios_ptr,0x14) = htonl(cfg_addr + len);
   *PTR_ADJUST(m_uint32_t *,ios_ptr,0x18) = htonl(len);

   /* Compute the checksum */
   cklen = (vm->nvram_size*1024) - (vm->nvram_rom_space + 0x08);
   cksum = nvram_cksum((m_uint16_t *)(ios_ptr+0x08),cklen);
   *PTR_ADJUST(m_uint16_t *,ios_ptr,0x0c) = htons(cksum);

   vm_mmap_close_file(fd,base_ptr,vm->nvram_size*1024);
   return(0);
}

/* Set MSFC eeprom definition */
static int msfc1_set_eeprom(msfc1_t *router)
{
   if (cisco_eeprom_copy(&router->cpu_eeprom,&msfc1_eeprom) == -1) {
      vm_error(router->vm,"unable to set NPE EEPROM.\n");
      return(-1);
   }

   return(0);
}

/* Set the base MAC address of the chassis */
static int msfc1_burn_mac_addr(msfc1_t *router,n_eth_addr_t *addr)
{
   m_uint8_t eeprom_ver;

   /* Read EEPROM format version */
   cisco_eeprom_get_byte(&router->mp_eeprom,0,&eeprom_ver);

   if (eeprom_ver != 1) {
      vm_error(router->vm,"msfc1_burn_mac_addr: unable to handle "
              "EEPROM version %u\n",eeprom_ver);
      return(-1);
   }

   cisco_eeprom_set_region(&router->mp_eeprom,12,addr->eth_addr_byte,6);
   return(0);
}

/* Free specific hardware resources used by MSFC1 */
static void msfc1_free_hw_ressources(msfc1_t *router)
{
   /* Shutdown all Port Adapters */
   msfc1_pa_shutdown_all(router);
}

/* Create a new router instance */
msfc1_t *msfc1_create_instance(char *name,int instance_id)
{
   msfc1_t *router;

   if (!(router = malloc(sizeof(*router)))) {
      fprintf(stderr,"MSFC1 '%s': Unable to create new instance!\n",name);
      return NULL;
   }
   
   memset(router,0,sizeof(*router));

   if (!(router->vm = vm_create(name,instance_id,VM_TYPE_MSFC1))) {
      fprintf(stderr,"MSFC1 '%s': unable to create VM instance!\n",name);
      goto err_vm;
   }

   msfc1_init_defaults(router);
   router->vm->hw_data = router;
   router->vm->elf_machine_id = MSFC1_ELF_MACHINE_ID;
   return router;

 err_vm:
   free(router);
   return NULL;
}

/* Free resources used by a router instance */
static int msfc1_free_instance(void *data,void *arg)
{
   vm_instance_t *vm = data;
   msfc1_t *router;
   int i;

   if (vm->type == VM_TYPE_MSFC1) {
      router = VM_MSFC1(vm);

      /* Stop all CPUs */
      if (vm->cpu_group != NULL) {
         vm_stop(vm);

         if (cpu_group_sync_state(vm->cpu_group) == -1) {
            vm_error(vm,"unable to sync with system CPUs.\n");
            return(FALSE);
         }
      }

      /* Remove NIO bindings */
      for(i=0;i<MSFC1_MAX_PA_BAYS;i++)
         msfc1_pa_remove_all_nio_bindings(router,i);

      /* Free specific HW resources */
      msfc1_free_hw_ressources(router);

      /* Free EEPROMs */
      cisco_eeprom_free(&router->cpu_eeprom);
      cisco_eeprom_free(&router->mp_eeprom);
      cisco_eeprom_free(&router->pem_eeprom);

      /* Free all resources used by VM */
      vm_free(vm);

      /* Free the router structure */
      free(router);
      return(TRUE);
   }

   return(FALSE);
}

/* Delete a router instance */
int msfc1_delete_instance(char *name)
{
   return(registry_delete_if_unused(name,OBJ_TYPE_VM,
                                    msfc1_free_instance,NULL));
}

/* Delete all router instances */
int msfc1_delete_all_instances(void)
{
   return(registry_delete_type(OBJ_TYPE_VM,msfc1_free_instance,NULL));
}

/* Save configuration of a MSFC1 instance */
void msfc1_save_config(msfc1_t *router,FILE *fd)
{
   vm_instance_t *vm = router->vm;
   struct msfc1_nio_binding *nb;
   struct msfc1_pa_bay *bay;
   int i;

   /* General settings */
   fprintf(fd,"msfc1 create %s %u\n",vm->name,vm->instance_id);

   /* VM configuration */
   vm_save_config(vm,fd);

   /* Port Adapter settings */
   for(i=0;i<MSFC1_MAX_PA_BAYS;i++) {
      if (!(bay = msfc1_pa_get_info(router,i)))
         continue;

      if (bay->dev_type) {
         fprintf(fd,"msfc1 add_pa_binding %s %u %s\n",
                 vm->name,i,bay->dev_type);
      }

      for(nb=bay->nio_list;nb;nb=nb->next) {
         fprintf(fd,"msfc1 add_nio_binding %s %u %u %s\n",
                 vm->name,i,nb->port_id,nb->nio->name);
      }
   }

   fprintf(fd,"\n");
}

/* Save configurations of all MSFC1 instances */
static void msfc1_reg_save_config(registry_entry_t *entry,void *opt,int *err)
{
   vm_instance_t *vm = entry->data;
   msfc1_t *router = VM_MSFC1(vm);

   if (vm->type == VM_TYPE_MSFC1)
      msfc1_save_config(router,(FILE *)opt);
}

/* Unset PA EEPROM definition (empty bay) */
int msfc1_pa_unset_eeprom(msfc1_t *router,u_int pa_bay)
{
   if (pa_bay >= MSFC1_MAX_PA_BAYS) {
      vm_error(router->vm,"msfc1_pa_set_eeprom: invalid PA Bay %u.\n",pa_bay);
      return(-1);
   }
   
   cisco_eeprom_free(&router->pa_bay[pa_bay].eeprom);
   return(0);
}

/* Check if a bay has a port adapter */
int msfc1_pa_check_eeprom(msfc1_t *router,u_int pa_bay)
{
   if (pa_bay >= MSFC1_MAX_PA_BAYS)
      return(FALSE);

   return(cisco_eeprom_valid(&router->pa_bay[pa_bay].eeprom));
}

/* Get bay info */
struct msfc1_pa_bay *msfc1_pa_get_info(msfc1_t *router,u_int pa_bay)
{
   if (pa_bay >= MSFC1_MAX_PA_BAYS)
      return NULL;

   return(&router->pa_bay[pa_bay]);
}

/* Get PA type */
char *msfc1_pa_get_type(msfc1_t *router,u_int pa_bay)
{
   struct msfc1_pa_bay *bay;

   bay = msfc1_pa_get_info(router,pa_bay);
   return((bay != NULL) ? bay->dev_type : NULL);
}

/* Get driver info about the specified slot */
void *msfc1_pa_get_drvinfo(msfc1_t *router,u_int pa_bay)
{
   struct msfc1_pa_bay *bay;

   bay = msfc1_pa_get_info(router,pa_bay);
   return((bay != NULL) ? bay->drv_info : NULL);
}

/* Set driver info for the specified slot */
int msfc1_pa_set_drvinfo(msfc1_t *router,u_int pa_bay,void *drv_info)
{
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   bay->drv_info = drv_info;
   return(0);
}

/* Get a PA driver */
static struct msfc1_pa_driver *msfc1_pa_get_driver(char *dev_type)
{
   int i;

   for(i=0;pa_drivers[i];i++)
      if (!strcmp(pa_drivers[i]->dev_type,dev_type))
         return pa_drivers[i];

   return NULL;
}

/* Add a PA binding */
int msfc1_pa_add_binding(msfc1_t *router,char *dev_type,u_int pa_bay)
{   
   struct msfc1_pa_driver *pa_driver;
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   /* check that this bay is empty */
   if (bay->dev_type != NULL) {
      vm_error(router->vm,"a PA already exists in slot %u.\n",pa_bay);
      return(-1);
   }

   /* find the PA driver */
   if (!(pa_driver = msfc1_pa_get_driver(dev_type))) {
      vm_error(router->vm,"unknown PA type '%s'.\n",dev_type);
      return(-1);
   }

   bay->dev_type = pa_driver->dev_type;
   bay->pa_driver = pa_driver;
   return(0);  
}

/* Remove a PA binding */
int msfc1_pa_remove_binding(msfc1_t *router,u_int pa_bay)
{   
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   /* stop if this bay is still active */
   if (bay->drv_info != NULL) {
      vm_error(router->vm,"slot %u still active.\n",pa_bay);
      return(-1);
   }

   /* check that this bay is not empty */
   if (bay->dev_type == NULL) {
      vm_error(router->vm,"slot %u is empty.\n",pa_bay);
      return(-1);
   }
   
   /* remove all NIOs bindings */ 
   msfc1_pa_remove_all_nio_bindings(router,pa_bay);

   bay->dev_type  = NULL;
   bay->pa_driver = NULL;
   return(0);
}

/* Find a NIO binding */
struct msfc1_nio_binding *
msfc1_pa_find_nio_binding(msfc1_t *router,u_int pa_bay,u_int port_id)
{   
   struct msfc1_nio_binding *nb;
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return NULL;

   for(nb=bay->nio_list;nb;nb=nb->next)
      if (nb->port_id == port_id)
         return nb;

   return NULL;
}

/* Add a network IO binding */
int msfc1_pa_add_nio_binding(msfc1_t *router,u_int pa_bay,u_int port_id,
                             char *nio_name)
{
   struct msfc1_nio_binding *nb;
   struct msfc1_pa_bay *bay;
   netio_desc_t *nio;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   /* check that a NIO is not already bound to this port */
   if (msfc1_pa_find_nio_binding(router,pa_bay,port_id) != NULL) {
      vm_error(router->vm,"a NIO already exists for interface %u/%u\n",
               pa_bay,port_id);
      return(-1);
   }

   /* acquire a reference on the NIO object */
   if (!(nio = netio_acquire(nio_name))) {
      vm_error(router->vm,"unable to find NIO '%s'.\n",nio_name);
      return(-1);
   }

   /* create a new binding */
   if (!(nb = malloc(sizeof(*nb)))) {
      vm_error(router->vm,"unable to create NIO binding "
               "for interface %u/%u.\n",pa_bay,port_id);
      netio_release(nio_name);
      return(-1);
   }

   memset(nb,0,sizeof(*nb));
   nb->nio       = nio;
   nb->port_id   = port_id;
   nb->next      = bay->nio_list;
   if (nb->next) nb->next->prev = nb;
   bay->nio_list = nb;
   return(0);
}

/* Remove a NIO binding */
int msfc1_pa_remove_nio_binding(msfc1_t *router,u_int pa_bay,u_int port_id)
{
   struct msfc1_nio_binding *nb;
   struct msfc1_pa_bay *bay;
   
   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   if (!(nb = msfc1_pa_find_nio_binding(router,pa_bay,port_id)))
      return(-1);   /* no nio binding for this slot/port */

   /* tell the PA driver to stop using this NIO */
   if (bay->pa_driver)
      bay->pa_driver->pa_unset_nio(router,pa_bay,port_id);

   /* remove this entry from the double linked list */
   if (nb->next)
      nb->next->prev = nb->prev;

   if (nb->prev) {
      nb->prev->next = nb->next;
   } else {
      bay->nio_list = nb->next;
   }

   /* unreference NIO object */
   netio_release(nb->nio->name);
   free(nb);
   return(0);
}

/* Remove all NIO bindings for the specified PA */
int msfc1_pa_remove_all_nio_bindings(msfc1_t *router,u_int pa_bay)
{  
   struct msfc1_nio_binding *nb,*next;
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   for(nb=bay->nio_list;nb;nb=next) {
      next = nb->next;

      /* tell the PA driver to stop using this NIO */
      if (bay->pa_driver)
         bay->pa_driver->pa_unset_nio(router,pa_bay,nb->port_id);

      /* unreference NIO object */
      netio_release(nb->nio->name);
      free(nb);
   }

   bay->nio_list = NULL;
   return(0);
}

/* Enable a Network IO descriptor for a Port Adapter */
int msfc1_pa_enable_nio(msfc1_t *router,u_int pa_bay,u_int port_id)
{
   struct msfc1_nio_binding *nb;
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   /* check that we have an NIO binding for this interface */
   if (!(nb = msfc1_pa_find_nio_binding(router,pa_bay,port_id)))
      return(-1);

   /* check that the driver is defined and successfully initialized */
   if (!bay->pa_driver || !bay->drv_info)
      return(-1);

   return(bay->pa_driver->pa_set_nio(router,pa_bay,port_id,nb->nio));
}

/* Disable Network IO descriptor of a Port Adapter */
int msfc1_pa_disable_nio(msfc1_t *router,u_int pa_bay,u_int port_id)
{
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   /* check that the driver is defined and successfully initialized */
   if (!bay->pa_driver || !bay->drv_info)
      return(-1);

   return(bay->pa_driver->pa_unset_nio(router,pa_bay,port_id));
}

/* Enable all NIO of the specified PA */
int msfc1_pa_enable_all_nio(msfc1_t *router,u_int pa_bay)
{
   struct msfc1_nio_binding *nb;
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   /* check that the driver is defined and successfully initialized */
   if (!bay->pa_driver || !bay->drv_info)
      return(-1);

   for(nb=bay->nio_list;nb;nb=nb->next)
      bay->pa_driver->pa_set_nio(router,pa_bay,nb->port_id,nb->nio);

   return(0);
}

/* Disable all NIO of the specified PA */
int msfc1_pa_disable_all_nio(msfc1_t *router,u_int pa_bay)
{
   struct msfc1_nio_binding *nb;
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   /* check that the driver is defined and successfully initialized */
   if (!bay->pa_driver || !bay->drv_info)
      return(-1);

   for(nb=bay->nio_list;nb;nb=nb->next)
      bay->pa_driver->pa_unset_nio(router,pa_bay,nb->port_id);

   return(0);
}

/* Initialize a Port Adapter */
int msfc1_pa_init(msfc1_t *router,u_int pa_bay)
{   
   struct msfc1_pa_bay *bay;
   size_t len;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   /* Check that a device type is defined for this bay */
   if (!bay->dev_type || !bay->pa_driver) {
      vm_error(router->vm,"trying to init empty slot %u.\n",pa_bay);
      return(-1);
   }

   /* Allocate device name */
   len = strlen(bay->dev_type) + 10;
   if (!(bay->dev_name = malloc(len))) {
      vm_error(router->vm,"unable to allocate device name.\n");
      return(-1);
   }

   snprintf(bay->dev_name,len,"%s(%u)",bay->dev_type,pa_bay);

   /* Initialize PA driver */
   if (bay->pa_driver->pa_init(router,bay->dev_name,pa_bay) == -1) {
      vm_error(router->vm,"unable to initialize PA %u.\n",pa_bay);
      return(-1);
   }

   /* Enable all NIO */
   msfc1_pa_enable_all_nio(router,pa_bay);
   return(0);
}

/* Shutdown a Port Adapter */
int msfc1_pa_shutdown(msfc1_t *router,u_int pa_bay)
{
   struct msfc1_pa_bay *bay;

   if (!(bay = msfc1_pa_get_info(router,pa_bay)))
      return(-1);

   /* Check that a device type is defined for this bay */   
   if (!bay->dev_type || !bay->pa_driver) {
      vm_error(router->vm,"trying to shut down an empty bay %u.\n",pa_bay);
      return(-1);
   }

   /* Disable all NIO */
   msfc1_pa_disable_all_nio(router,pa_bay);

   /* Shutdown the PA driver */
   if (bay->drv_info && (bay->pa_driver->pa_shutdown(router,pa_bay) == -1)) {
      vm_error(router->vm,"unable to shutdown PA %u.\n",pa_bay);
      return(-1);
   }

   free(bay->dev_name);
   bay->dev_name = NULL;
   bay->drv_info = NULL;
   return(0);
}

/* Shutdown all PA of a router */
int msfc1_pa_shutdown_all(msfc1_t *router)
{
   int i;

   for(i=0;i<MSFC1_MAX_PA_BAYS;i++) {
      if (!router->pa_bay[i].dev_type) 
         continue;

      msfc1_pa_shutdown(router,i);
   }

   return(0);
}

/* Show info about all NMs */
int msfc1_pa_show_all_info(msfc1_t *router)
{
   struct msfc1_pa_bay *bay;
   int i;

   for(i=0;i<MSFC1_MAX_PA_BAYS;i++) {
      if (!(bay = msfc1_pa_get_info(router,i)) || !bay->pa_driver)
         continue;

      if (bay->pa_driver->pa_show_info != NULL)
         bay->pa_driver->pa_show_info(router,i);
   }

   return(0);
}

/* Maximum number of tokens in a PA description */
#define PA_DESC_MAX_TOKENS  8

/* Create a Port Adapter (command line) */
int msfc1_cmd_pa_create(msfc1_t *router,char *str)
{
   char *tokens[PA_DESC_MAX_TOKENS];
   int i,count,res;
   u_int pa_bay;

   /* A port adapter description is like "1:PA-FE-TX" */
   if ((count = m_strsplit(str,':',tokens,PA_DESC_MAX_TOKENS)) != 2) {
      vm_error(router->vm,"unable to parse PA description '%s'.\n",str);
      return(-1);
   }

   /* Parse the PA bay id */
   pa_bay = atoi(tokens[0]);

   /* Add this new PA to the current PA list */
   res = msfc1_pa_add_binding(router,tokens[1],pa_bay);

   /* The complete array was cleaned by strsplit */
   for(i=0;i<PA_DESC_MAX_TOKENS;i++)
      free(tokens[i]);

   return(res);
}

/* Add a Network IO descriptor binding (command line) */
int msfc1_cmd_add_nio(msfc1_t *router,char *str)
{
   char *tokens[PA_DESC_MAX_TOKENS];
   int i,count,nio_type,res=-1;
   u_int pa_bay,port_id;
   netio_desc_t *nio;
   char nio_name[128];

   /* A port adapter description is like "1:3:tap:tap0" */
   if ((count = m_strsplit(str,':',tokens,PA_DESC_MAX_TOKENS)) < 3) {
      vm_error(router->vm,"unable to parse NIO description '%s'.\n",str);
      return(-1);
   }

   /* Parse the PA bay */
   pa_bay = atoi(tokens[0]);

   /* Parse the PA port id */
   port_id = atoi(tokens[1]);

   /* Autogenerate a NIO name */
   snprintf(nio_name,sizeof(nio_name),"msfc1-i%u/%u/%u",
            router->vm->instance_id,pa_bay,port_id);

   /* Create the Network IO descriptor */
   nio = NULL;
   nio_type = netio_get_type(tokens[2]);

   switch(nio_type) {
      case NETIO_TYPE_UNIX:
         if (count != 5) {
            vm_error(router->vm,
                     "invalid number of arguments for UNIX NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_unix(nio_name,tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_VDE:
         if (count != 5) {
            vm_error(router->vm,
                     "invalid number of arguments for VDE NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_vde(nio_name,tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TAP:
         if (count != 4) {
            vm_error(router->vm,
                     "invalid number of arguments for TAP NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_tap(nio_name,tokens[3]);
         break;

      case NETIO_TYPE_UDP:
         if (count != 6) {
            vm_error(router->vm,
                     "invalid number of arguments for UDP NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_udp(nio_name,atoi(tokens[3]),
                                     tokens[4],atoi(tokens[5]));
         break;

      case NETIO_TYPE_TCP_CLI:
         if (count != 5) {
            vm_error(router->vm,
                     "invalid number of arguments for TCP CLI NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_tcp_cli(nio_name,tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TCP_SER:
         if (count != 4) {
            vm_error(router->vm,
                     "invalid number of arguments for TCP SER NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_tcp_ser(nio_name,tokens[3]);
         break;

      case NETIO_TYPE_NULL:
         nio = netio_desc_create_null(nio_name);
         break;

#ifdef LINUX_ETH
      case NETIO_TYPE_LINUX_ETH:
         if (count != 4) {
            vm_error(router->vm,
                     "invalid number of arguments for Linux Eth NIO '%s'\n",
                     str);
            goto done;
         }
         
         nio = netio_desc_create_lnxeth(nio_name,tokens[3]);
         break;
#endif

#ifdef GEN_ETH
      case NETIO_TYPE_GEN_ETH:
         if (count != 4) {
            vm_error(router->vm,"invalid number of "
                     "arguments for Generic Eth NIO '%s'\n",str);
            goto done;
         }
         
         nio = netio_desc_create_geneth(nio_name,tokens[3]);
         break;
#endif

      default:
         vm_error(router->vm,"unknown NETIO type '%s'\n",tokens[2]);
         goto done;
   }

   if (!nio) {
      fprintf(stderr,"msfc1_cmd_add_nio: unable to create NETIO "
              "descriptor for PA bay %u\n",pa_bay);
      goto done;
   }

   if (msfc1_pa_add_nio_binding(router,pa_bay,port_id,nio_name) == -1) {
      vm_error(router->vm,"unable to add NETIO binding for slot %u\n",pa_bay);
      netio_release(nio_name);
      netio_delete(nio_name);
      goto done;
   }
   
   netio_release(nio_name);
   res = 0;

 done:
   /* The complete array was cleaned by strsplit */
   for(i=0;i<PA_DESC_MAX_TOKENS;i++)
      free(tokens[i]);

   return(res);
}

/* Create the main PCI bus for a GT64010 based system */
static int msfc1_init_gt64010(msfc1_t *router)
{   
   vm_instance_t *vm = router->vm;

   if (!(vm->pci_bus[0] = pci_bus_create("PCI Bus 0",0))) {
      vm_error(vm,"unable to create PCI data.\n");
      return(-1);
   }
   
   return(dev_gt64010_init(vm,"gt64010",MSFC1_GT64K_ADDR,0x1000,
                           MSFC1_GT64K_IRQ));
}

/* Initialize a MSFC1 board */
int msfc1_init_hw(msfc1_t *router)
{
   vm_instance_t *vm = router->vm;

   /* Set the processor type: R5000 */
   mips64_set_prid(CPU_MIPS64(vm->boot_cpu),MIPS_PRID_R5000);

   /* Initialize the Galileo GT-64010 PCI controller */
   if (msfc1_init_gt64010(router) == -1)
      return(-1);

   /* Create PCI bus 1 */
   vm->pci_bus_pool[24] = pci_bus_create("PCI Bus 1",-1);
   dev_dec21154_init(vm->pci_bus[0],1,vm->pci_bus_pool[24]);

   /* Initialize SRAM (4Mb) */
   dev_c7200_sram_init(vm,"sram",MSFC1_SRAM_ADDR,MSFC1_SRAM_SIZE,
                       vm->pci_bus_pool[24],0);

   /* PCI IO space */
   if (!(vm->pci_io_space = pci_io_data_init(vm,MSFC1_PCI_IO_ADDR)))
      return(-1);

   /* Cirrus Logic PD6729 (PCI-to-PCMCIA host adapter) */
   dev_clpd6729_init(vm,vm->pci_bus[0],5,vm->pci_io_space,0x402,0x403);

   return(0);
}

/* Show MSFC1 hardware info */
void msfc1_show_hardware(msfc1_t *router)
{
   vm_instance_t *vm = router->vm;

   printf("MSFC1 instance '%s' (id %d):\n",vm->name,vm->instance_id);

   printf("  VM Status  : %d\n",vm->status);
   printf("  RAM size   : %u Mb\n",vm->ram_size);
   printf("  IOMEM size : %u Mb\n",vm->iomem_size);
   printf("  NVRAM size : %u Kb\n",vm->nvram_size);
   printf("  IOS image  : %s\n\n",vm->ios_image);

   if (vm->debug_level > 0) {
      dev_show_list(vm);
      pci_dev_show_list(vm->pci_bus[0]);
      pci_dev_show_list(vm->pci_bus[1]);
      printf("\n");
   }
}

/* Initialize default parameters for a MSFC1 */
void msfc1_init_defaults(msfc1_t *router)
{
   vm_instance_t *vm = router->vm;
   n_eth_addr_t *m;
   m_uint16_t pid;

   pid = (m_uint16_t)getpid();

   /* Generate a chassis MAC address based on the instance ID */
   m = &router->mac_addr;
   m->eth_addr_byte[0] = vm_get_mac_addr_msb(vm);
   m->eth_addr_byte[1] = vm->instance_id & 0xFF;
   m->eth_addr_byte[2] = pid >> 8;
   m->eth_addr_byte[3] = pid & 0xFF;
   m->eth_addr_byte[4] = 0x00;
   m->eth_addr_byte[5] = 0x00;

   msfc1_set_eeprom(router);
   msfc1_init_eeprom_groups(router);
   vm->ram_mmap        = MSFC1_DEFAULT_RAM_MMAP;
   vm->ram_size        = MSFC1_DEFAULT_RAM_SIZE;
   vm->rom_size        = MSFC1_DEFAULT_ROM_SIZE;
   vm->nvram_size      = MSFC1_DEFAULT_NVRAM_SIZE;
   vm->iomem_size      = 0;
   vm->conf_reg_setup  = MSFC1_DEFAULT_CONF_REG;
   vm->clock_divisor   = MSFC1_DEFAULT_CLOCK_DIV;
   vm->nvram_rom_space = MSFC1_NVRAM_ROM_RES_SIZE;

   /* Enable NVRAM operations to load/store configs */
   vm->nvram_extract_config = msfc1_nvram_extract_config;
   vm->nvram_push_config = msfc1_nvram_push_config;
}

/* Run the checklist */
static int msfc1_checklist(msfc1_t *router)
{
   struct vm_instance *vm = router->vm;
   int res = 0;

   res += vm_object_check(vm,"ram");
   res += vm_object_check(vm,"rom");
   res += vm_object_check(vm,"nvram");
   res += vm_object_check(vm,"zero");

   if (res < 0)
      vm_error(vm,"incomplete initialization (no memory?)\n");

   return(res);
}

/* Initialize Port Adapters */
static int msfc1_init_platform_pa(msfc1_t *router)
{
   /* Create EOBC interface */
   msfc1_pa_add_binding(router,"MSFC1_EOBC",0);
   msfc1_pa_init(router,0);

   /* Create IBC interface */
   msfc1_pa_add_binding(router,"MSFC1_IBC",1);
   msfc1_pa_init(router,1);
   return(0);
}

/* Initialize the MSFC1 Platform */
static int msfc1_init_platform(msfc1_t *router)
{
   struct vm_instance *vm = router->vm;
   cpu_mips_t *cpu0; 
   cpu_gen_t *gen0;

   /* Copy config register setup into "active" config register */
   vm->conf_reg = vm->conf_reg_setup;

   /* Create Console and AUX ports */
   vm_init_vtty(vm);

   /* Create a CPU group */
   vm->cpu_group = cpu_group_create("System CPU");

   /* Initialize the virtual MIPS processor */
   if (!(gen0 = cpu_create(vm,CPU_TYPE_MIPS64,0))) {
      vm_error(vm,"unable to create CPU0!\n");
      return(-1);
   }

   cpu0 = CPU_MIPS64(gen0);

   /* Add this CPU to the system CPU group */
   cpu_group_add(vm->cpu_group,gen0);
   vm->boot_cpu = gen0;

   /* Initialize the IRQ routing vectors */
   vm->set_irq = mips64_vm_set_irq;
   vm->clear_irq = mips64_vm_clear_irq;

   /* Mark the Network IO interrupt as high priority */
   cpu0->irq_idle_preempt[MSFC1_NETIO_IRQ] = TRUE;
   cpu0->irq_idle_preempt[MSFC1_GT64K_IRQ] = TRUE;

   /* Copy some parameters from VM to CPU0 (idle PC, ...) */
   cpu0->idle_pc = vm->idle_pc;

   if (vm->timer_irq_check_itv)
      cpu0->timer_irq_check_itv = vm->timer_irq_check_itv;

   /*
    * On the MSFC1, bit 33 of physical addresses is used to bypass L2 cache.
    * We clear it systematically.
    */
   cpu0->addr_bus_mask = MSFC1_ADDR_BUS_MASK;

   /* Remote emulator control */
   dev_remote_control_init(vm,0x16000000,0x1000);

   /* Bootflash */
   dev_bootflash_init(vm,"bootflash",MSFC1_BOOTFLASH_ADDR,(8 * 1048576));

   /* NVRAM and calendar */
   dev_nvram_init(vm,"nvram",MSFC1_NVRAM_ADDR,
                  vm->nvram_size*1024,&vm->conf_reg);

   /* Bit-bucket zone */
   dev_zero_init(vm,"zero",MSFC1_BITBUCKET_ADDR,0xc00000);

   /* Initialize the NPE board */
   if (msfc1_init_hw(router) == -1)
      return(-1);

   /* Initialize RAM */
   vm_ram_init(vm,0x00000000ULL);

   /* Initialize ROM */
   if (!vm->rom_filename) {
      /* use embedded ROM */
      dev_rom_init(vm,"rom",MSFC1_ROM_ADDR,vm->rom_size*1048576,
                   mips64_microcode,mips64_microcode_len);
   } else {
      /* use alternate ROM */
      dev_ram_init(vm,"rom",TRUE,TRUE,NULL,FALSE,
                   MSFC1_ROM_ADDR,vm->rom_size*1048576);
   }

   /* Byte swapping */
   dev_bswap_init(vm,"mem_bswap",MSFC1_BSWAP_ADDR,1024*1048576,0x00000000ULL);

   /* PCI IO space */
   if (!(vm->pci_io_space = pci_io_data_init(vm,MSFC1_PCI_IO_ADDR)))
      return(-1);

   /* Initialize the Port Adapters */
   if (msfc1_init_platform_pa(router) == -1)
      return(-1);

   /* Verify the check list */
   if (msfc1_checklist(router) == -1)
      return(-1);

   /* Midplane FPGA */
   if (dev_msfc1_mpfpga_init(router,MSFC1_MPFPGA_ADDR,0x1000) == -1)
      return(-1);

   /* IO FPGA */
   if (dev_msfc1_iofpga_init(router,MSFC1_IOFPGA_ADDR,0x1000) == -1)
      return(-1);

   /* Show device list */
   msfc1_show_hardware(router);
   return(0);
}

/* Boot the IOS image */
static int msfc1_boot_ios(msfc1_t *router)
{   
   vm_instance_t *vm = router->vm;
   cpu_mips_t *cpu;

   if (!vm->boot_cpu)
      return(-1);

   /* Suspend CPU activity since we will restart directly from ROM */
   vm_suspend(vm);

   /* Check that CPU activity is really suspended */
   if (cpu_group_sync_state(vm->cpu_group) == -1) {
      vm_error(vm,"unable to sync with system CPUs.\n");
      return(-1);
   }

   /* Reset the boot CPU */
   cpu = CPU_MIPS64(vm->boot_cpu);
   mips64_reset(cpu);

   /* Load IOS image */
   if (mips64_load_elf_image(cpu,vm->ios_image,
                             (vm->ghost_status == VM_GHOST_RAM_USE),
                             &vm->ios_entry_point) < 0)
   {
      vm_error(vm,"failed to load Cisco IOS image '%s'.\n",vm->ios_image);
      return(-1);
   }

   /* Launch the simulation */
   printf("\nMSFC1 '%s': starting simulation (CPU0 PC=0x%llx), "
          "JIT %sabled.\n",
          vm->name,cpu->pc,vm->jit_use ? "en":"dis");

   vm_log(vm,"MSFC1_BOOT",
          "starting instance (CPU0 PC=0x%llx,idle_pc=0x%llx,JIT %s)\n",
          cpu->pc,cpu->idle_pc,vm->jit_use ? "on":"off");
   
   /* Start main CPU */
   if (vm->ghost_status != VM_GHOST_RAM_GENERATE) {
      vm->status = VM_STATUS_RUNNING;
      cpu_start(vm->boot_cpu);
   } else {
      vm->status = VM_STATUS_SHUTDOWN;
   }
   return(0);
}

/* Initialize a MSFC1 instance */
int msfc1_init_instance(msfc1_t *router)
{
   vm_instance_t *vm = router->vm;
   m_uint32_t rom_entry_point;
   cpu_mips_t *cpu0;

   /* Initialize the MSFC1 platform */
   if (msfc1_init_platform(router) == -1) {
      vm_error(vm,"unable to initialize the platform hardware.\n");
      return(-1);
   }

   /* Load IOS configuration file */
   if (vm->ios_config != NULL) {
      vm_nvram_push_config(vm,vm->ios_config);
      vm->conf_reg &= ~0x40;
   }

   /* Load ROM (ELF image or embedded) */
   cpu0 = CPU_MIPS64(vm->boot_cpu);
   rom_entry_point = (m_uint32_t)MIPS_ROM_PC;
   
   if ((vm->rom_filename != NULL) &&
       (mips64_load_elf_image(cpu0,vm->rom_filename,0,&rom_entry_point) < 0))
   {
      vm_error(vm,"unable to load alternate ROM '%s', "
               "fallback to embedded ROM.\n\n",vm->rom_filename);
      vm->rom_filename = NULL;
   }

   /* Load symbol file */
   if (vm->sym_filename) {
      mips64_sym_load_file(cpu0,vm->sym_filename);
      cpu0->sym_trace = 1;
   }

   return(msfc1_boot_ios(router));
}

/* Stop a MSFC1 instance */
int msfc1_stop_instance(msfc1_t *router)
{
   vm_instance_t *vm = router->vm;

   printf("\nMSFC1 '%s': stopping simulation.\n",vm->name);
   vm_log(vm,"MSFC1_STOP","stopping simulation.\n");

   /* Stop all CPUs */
   if (vm->cpu_group != NULL) {
      vm_stop(vm);
      
      if (cpu_group_sync_state(vm->cpu_group) == -1) {
         vm_error(vm,"unable to sync with system CPUs.\n");
         return(-1);
      }
   }

   /* Free resources that were used during execution to emulate hardware */
   msfc1_free_hw_ressources(router);
   vm_hardware_shutdown(vm);
   return(0);
}
