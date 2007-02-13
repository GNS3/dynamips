/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco 2600 routines and definitions (EEPROM,...).
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
#include "ppc32_mem.h"
#include "pci_io.h"
#include "cisco_eeprom.h"
#include "dev_mpc860.h"
#include "dev_rom.h"
#include "dev_c2600.h"
#include "dev_vtty.h"
#include "registry.h"

/* ======================================================================== */
/* EEPROM definitions                                                       */
/* ======================================================================== */

/* Cisco 2600 mainboard EEPROM */
static m_uint16_t eeprom_c2600_mb_data[] = {
   0x0101, 0x0404, 0x0000, 0x0000, 0x4320, 0x00FF, 0x0091, 0x0020,
   0x0000, 0x0000, 0x0000, 0x0000, 0x3030, 0x3000, 0x0030, 0x3030,
   0x3002, 0x0200, 0x0000, 0x0000, 0x00FF, 0xFFFF, 0x5006, 0x490B,
   0x1709, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

struct c2600_mb_id {
   char *name;
   char *mb_driver;
   m_uint16_t id;
   int supported;
};

struct c2600_mb_id c2600_mainboard_id[] = {
   { "2610"   , "CISCO2600-MB-1E"  , 0x0091, TRUE  },
   { "2611"   , "CISCO2600-MB-2E"  , 0x0092, TRUE  },
   { "2620"   , "CISCO2600-MB-1FE" , 0x0094, TRUE  },
   { "2621"   , "CISCO2600-MB-2FE" , 0x00a2, TRUE  },
   { "2610XM" , "CISCO2600-MB-1FE" , 0x036a, TRUE  },
   { "2611XM" , "CISCO2600-MB-2FE" , 0x036b, FALSE },
   { "2620XM" , "CISCO2600-MB-1FE" , 0x036c, TRUE  },
   { "2621XM" , "CISCO2600-MB-2FE" , 0x036d, FALSE },
   { "2650XM" , "CISCO2600-MB-1FE" , 0x036e, TRUE  },
   { "2651XM" , "CISCO2600-MB-2FE" , 0x036f, FALSE },
   { NULL     , NULL               , 0x0000, 0     },
};

/* ======================================================================== */
/* Network Module Drivers                                                   */
/* ======================================================================== */
static struct c2600_nm_driver *nm_drivers[] = {
   &dev_c2600_mb1e_eth_driver,
   &dev_c2600_mb2e_eth_driver,
   &dev_c2600_mb1fe_eth_driver,
   &dev_c2600_mb2fe_eth_driver,

   &dev_c2600_nm_1e_driver,
   &dev_c2600_nm_4e_driver,
   &dev_c2600_nm_1fe_tx_driver,
   &dev_c2600_nm_16esw_driver,
   NULL,
};

/* ======================================================================== */
/* Cisco 2600 router instances                                              */
/* ======================================================================== */

/* Read a byte from the NVRAM */
static inline m_uint8_t nvram_read_byte(u_char *base,u_int offset)
{
   m_uint8_t *ptr;

   ptr = (m_uint8_t *)base + (offset << 2);
   return(*ptr);
}

/* Write a byte to the NVRAM */
static inline void nvram_write_byte(u_char *base,u_int offset,m_uint8_t val)
{
   m_uint8_t *ptr;

   ptr = (m_uint8_t *)base + (offset << 2);
   *ptr = val;
}

/* Read a 16-bit value from NVRAM */
static m_uint16_t nvram_read16(u_char *base,u_int offset)
{
   m_uint16_t val;
   val =  nvram_read_byte(base,offset) << 8;
   val |= nvram_read_byte(base,offset+1);
   return(val);
}

/* Write a 16-bit value to NVRAM */
static void nvram_write16(u_char *base,u_int offset,m_uint16_t val)
{
   nvram_write_byte(base,offset,val >> 8);
   nvram_write_byte(base,offset+1,val & 0xFF);
}

/* Read a 32-bit value from NVRAM */
static m_uint32_t nvram_read32(u_char *base,u_int offset)
{
   m_uint32_t val;
   val =  nvram_read_byte(base,offset)   << 24;
   val |= nvram_read_byte(base,offset+1) << 16;
   val |= nvram_read_byte(base,offset+2) << 8;
   val |= nvram_read_byte(base,offset+3);
   return(val);
}

/* Write a 32-bit value to NVRAM */
static void nvram_write32(u_char *base,u_int offset,m_uint32_t val)
{
   nvram_write_byte(base,offset,val >> 24);
   nvram_write_byte(base,offset+1,val >> 16);
   nvram_write_byte(base,offset+2,val >> 8);
   nvram_write_byte(base,offset+3,val & 0xFF);
}

/* Read a buffer from NVRAM */
static void nvram_memcpy_from(u_char *base,u_int offset,u_char *data,u_int len)
{
   u_int i;

   for(i=0;i<len;i++) {
      *data = nvram_read_byte(base,offset+i);
      data++;
   }
}

/* Write a buffer from NVRAM */
static void nvram_memcpy_to(u_char *base,u_int offset,u_char *data,u_int len)
{
   u_int i;

   for(i=0;i<len;i++) {
      nvram_write_byte(base,offset+i,*data);
      data++;
   }
}

/* Directly extract the configuration from the NVRAM device */
ssize_t c2600_nvram_extract_config(vm_instance_t *vm,char **buffer)
{
   u_char *base_ptr;
   u_int ios_ptr,cfg_ptr,end_ptr;
   m_uint32_t start,nvlen;
   m_uint16_t magic1,magic2;
   struct vdevice *nvram_dev;
   off_t nvram_size;
   int fd;

   if ((nvram_dev = dev_get_by_name(vm,"nvram")))
      dev_sync(nvram_dev);

   fd = vm_mmap_open_file(vm,"nvram",&base_ptr,&nvram_size);

   if (fd == -1)
      return(-1);

   ios_ptr = vm->nvram_rom_space;
   end_ptr = nvram_size;

   if ((ios_ptr + 0x30) >= end_ptr) {
      vm_error(vm,"NVRAM file too small\n");
      return(-1);
   }

   magic1 = nvram_read16(base_ptr,ios_ptr+0x06);
   magic2 = nvram_read16(base_ptr,ios_ptr+0x08);

   if ((magic1 != 0xF0A5) || (magic2 != 0xABCD)) {
      vm_error(vm,"unable to find IOS magic numbers (0x%x,0x%x)!\n",
               magic1,magic2);
      return(-1);
   }

   start = nvram_read32(base_ptr,ios_ptr+0x10) + 1;
   nvlen = nvram_read32(base_ptr,ios_ptr+0x18);

   printf("START = 0x%8.8x, LEN = 0x%8.8x\n",start,nvlen);
   printf("END   = 0x%8.8x\n",nvram_read32(base_ptr,ios_ptr+0x14));

   if (!(*buffer = malloc(nvlen+1))) {
      vm_error(vm,"unable to allocate config buffer (%u bytes)\n",nvlen);
      return(-1);
   }

   cfg_ptr = ios_ptr + start + 0x08;

   if ((cfg_ptr + nvlen) > end_ptr) {
      vm_error(vm,"NVRAM file too small\n");
      return(-1);
   }

   nvram_memcpy_from(base_ptr,cfg_ptr,*buffer,nvlen-1);
   (*buffer)[nvlen-1] = 0;
   return(nvlen-1);
}

/* Compute NVRAM checksum */
static m_uint16_t c2600_nvram_cksum(u_char *base_ptr,u_int offset,size_t count)
{
   m_uint32_t sum = 0;

   while(count > 1) {
      sum = sum + nvram_read16(base_ptr,offset);
      offset += 2;
      count -= sizeof(m_uint16_t);
   }

   if (count > 0) 
      sum = sum + ((nvram_read16(base_ptr,offset) & 0xFF) << 8);

   while(sum>>16)
      sum = (sum & 0xffff) + (sum >> 16);

   return(~sum);
}

/* Directly push the IOS configuration to the NVRAM device */
int c2600_nvram_push_config(vm_instance_t *vm,char *buffer,size_t len)
{
   m_uint32_t cfg_offset,cklen,tmp,ios_ptr,cfg_ptr;
   m_uint16_t cksum;
   u_char *base_ptr;
   int fd;

   fd = vm_mmap_create_file(vm,"nvram",vm->nvram_size*4096,&base_ptr);

   if (fd == -1)
      return(-1);

   cfg_offset = 0x2c;
   ios_ptr = vm->nvram_rom_space;
   cfg_ptr = ios_ptr + cfg_offset;

   /* Write IOS tag, uncompressed config... */
   nvram_write16(base_ptr,ios_ptr+0x06,0xF0A5);
   nvram_write16(base_ptr,ios_ptr+0x08,0xABCD);
   nvram_write16(base_ptr,ios_ptr+0x0a,0x0001);
   nvram_write16(base_ptr,ios_ptr+0x0c,0x0000);
   nvram_write16(base_ptr,ios_ptr+0x0e,0x0c04);

   /* Store file contents to NVRAM */
   nvram_memcpy_to(base_ptr,cfg_ptr,buffer,len);

   /* Write config addresses + size */
   tmp = cfg_offset - 0x08;

   nvram_write32(base_ptr,ios_ptr+0x10,tmp);
   nvram_write32(base_ptr,ios_ptr+0x14,tmp + len);
   nvram_write32(base_ptr,ios_ptr+0x18,len);

   /* Compute the checksum */
   cklen = (vm->nvram_size*1024) - (vm->nvram_rom_space + 0x08);
   cksum = c2600_nvram_cksum(base_ptr,ios_ptr+0x08,cklen);
   nvram_write16(base_ptr,ios_ptr+0x0c,cksum);

   vm_mmap_close_file(fd,base_ptr,vm->nvram_size*4096);
   return(0);
}

/* Check for empty config */
int c2600_nvram_check_empty_config(vm_instance_t *vm)
{
   struct vdevice *dev;
   m_uint64_t addr;
   m_uint32_t len;

   if (!(dev = dev_get_by_name(vm,"nvram")))
      return(-1);

   addr = dev->phys_addr + (vm->nvram_rom_space << 2);
   len  = dev->phys_len - (vm->nvram_rom_space << 2);

   while(len > 0) {
      if (physmem_copy_u32_from_vm(vm,addr) != 0)
         return(0);

      addr += sizeof(m_uint32_t);
      len  -= sizeof(m_uint32_t);
   }

   /* Empty NVRAM */
   vm->conf_reg |= 0x0040;
   printf("NVRAM is empty, setting config register to 0x%x\n",vm->conf_reg);
   return(0);
}

/* Create a new router instance */
c2600_t *c2600_create_instance(char *name,int instance_id)
{
   c2600_t *router;

   if (!(router = malloc(sizeof(*router)))) {
      fprintf(stderr,"C2600 '%s': Unable to create new instance!\n",name);
      return NULL;
   }

   memset(router,0,sizeof(*router));

   if (!(router->vm = vm_create(name,instance_id,VM_TYPE_C2600))) {
      fprintf(stderr,"C2600 '%s': unable to create VM instance!\n",name);
      goto err_vm;
   }

   c2600_init_defaults(router);
   router->vm->hw_data = router;
   return router;

 err_vm:
   free(router);
   return NULL;
}

/* Free resources used by a router instance */
static int c2600_free_instance(void *data,void *arg)
{
   vm_instance_t *vm = data;
   c2600_t *router;
   int i;

   if (vm->type == VM_TYPE_C2600) {
      router = VM_C2600(vm);

      /* Stop all CPUs */
      if (vm->cpu_group != NULL) {
         vm_stop(vm);
      
         if (cpu_group_sync_state(vm->cpu_group) == -1) {
            vm_error(vm,"unable to sync with system CPUs.\n");
            return(FALSE);
         }
      }

      /* Remove NIO bindings */
      for(i=0;i<C2600_MAX_NM_BAYS;i++)
         c2600_nm_remove_all_nio_bindings(router,i);

      /* Shutdown all Network Modules */
      c2600_nm_shutdown_all(router);

      /* Free mainboard EEPROM */
      cisco_eeprom_free(&router->mb_eeprom);

      /* Free all resources used by VM */
      vm_free(vm);

      /* Free the router structure */
      free(router);
      return(TRUE);
   }

   return(FALSE);
}

/* Delete a router instance */
int c2600_delete_instance(char *name)
{
   return(registry_delete_if_unused(name,OBJ_TYPE_VM,
                                    c2600_free_instance,NULL));
}

/* Delete all router instances */
int c2600_delete_all_instances(void)
{
   return(registry_delete_type(OBJ_TYPE_VM,c2600_free_instance,NULL));
}

/* Save configuration of a C2600 instance */
void c2600_save_config(c2600_t *router,FILE *fd)
{
   vm_instance_t *vm = router->vm;
   struct c2600_nio_binding *nb;
   struct c2600_nm_bay *bay;
   int i;

   /* General settings */
   fprintf(fd,"c2600 create %s %u\n",vm->name,vm->instance_id);
   fprintf(fd,"c2600 set_chassis %s %s\n",vm->name,router->mainboard_type);

   /* VM configuration */
   vm_save_config(vm,fd);

   /* Network Module settings */
   for(i=0;i<C2600_MAX_NM_BAYS;i++) {
      if (!(bay = c2600_nm_get_info(router,i)))
         continue;

      if (bay->dev_type) {
         fprintf(fd,"c2600 add_nm_binding %s %u %s\n",
                 vm->name,i,bay->dev_type);
      }

      for(nb=bay->nio_list;nb;nb=nb->next) {
         fprintf(fd,"c2600 add_nio_binding %s %u %u %s\n",
                 vm->name,i,nb->port_id,nb->nio->name);
      }
   }

   fprintf(fd,"\n");
}

/* Save configurations of all C2600 instances */
static void c2600_reg_save_config(registry_entry_t *entry,void *opt,int *err)
{
   vm_instance_t *vm = entry->data;
   c2600_t *router = VM_C2600(vm);

   if (vm->type == VM_TYPE_C2600)
      c2600_save_config(router,(FILE *)opt);
}

void c2600_save_config_all(FILE *fd)
{
   registry_foreach_type(OBJ_TYPE_VM,c2600_reg_save_config,fd,NULL);
}

/* Find Cisco 2600 Mainboard info */
static struct c2600_mb_id *c2600_get_mb_info(char *mainboard_type)
{   
   int i;

   for(i=0;c2600_mainboard_id[i].name;i++)
      if (!strcmp(c2600_mainboard_id[i].name,mainboard_type))
         return(&c2600_mainboard_id[i]);

   return NULL;
}

/* Show all available mainboards */
void c2600_mainboard_show_drivers(void)
{
   int i;

   printf("Available C2600 chassis drivers:\n");

   for(i=0;c2600_mainboard_id[i].name;i++)
      printf("  * %s %s\n",
             c2600_mainboard_id[i].name,
             !c2600_mainboard_id[i].supported ? "(NOT WORKING)" : "");

   printf("\n");
}

/* Set NM EEPROM definition */
int c2600_nm_set_eeprom(c2600_t *router,u_int nm_bay,
                        const struct cisco_eeprom *eeprom)
{
   if (nm_bay == 0)
      return(0);

   if (nm_bay != 1) {
      vm_error(router->vm,"c2600_nm_set_eeprom: invalid NM Bay %u.\n",nm_bay);
      return(-1);
   }
   
   if (cisco_eeprom_copy(&router->nm_bay[nm_bay].eeprom,eeprom) == -1) {
      vm_error(router->vm,"c2600_nm_set_eeprom: no memory.\n");
      return(-1);
   }
   
   return(0);
}

/* Unset NM EEPROM definition (empty bay) */
int c2600_nm_unset_eeprom(c2600_t *router,u_int nm_bay)
{
   if (nm_bay == 0)
      return(0);

   if (nm_bay != 1) {
      vm_error(router->vm,"c2600_nm_set_eeprom: invalid NM Bay %u.\n",nm_bay);
      return(-1);
   }
   
   cisco_eeprom_free(&router->nm_bay[nm_bay].eeprom);
   return(0);
}

/* Check if a bay has a port adapter */
int c2600_nm_check_eeprom(c2600_t *router,u_int nm_bay)
{
   if (nm_bay != 1)
      return(FALSE);

   return(cisco_eeprom_valid(&router->nm_bay[nm_bay].eeprom));
}

/* Get bay info */
struct c2600_nm_bay *c2600_nm_get_info(c2600_t *router,u_int nm_bay)
{
   if (nm_bay >= C2600_MAX_NM_BAYS)
      return NULL;

   return(&router->nm_bay[nm_bay]);
}

/* Get NM type */
char *c2600_nm_get_type(c2600_t *router,u_int nm_bay)
{
   struct c2600_nm_bay *bay;

   bay = c2600_nm_get_info(router,nm_bay);
   return((bay != NULL) ? bay->dev_type : NULL);
}

/* Get driver info about the specified slot */
void *c2600_nm_get_drvinfo(c2600_t *router,u_int nm_bay)
{
   struct c2600_nm_bay *bay;

   bay = c2600_nm_get_info(router,nm_bay);
   return((bay != NULL) ? bay->drv_info : NULL);
}

/* Set driver info for the specified slot */
int c2600_nm_set_drvinfo(c2600_t *router,u_int nm_bay,void *drv_info)
{
   struct c2600_nm_bay *bay;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   bay->drv_info = drv_info;
   return(0);
}

/* Get a NM driver */
static struct c2600_nm_driver *c2600_nm_get_driver(char *dev_type)
{
   int i;

   for(i=0;nm_drivers[i];i++)
      if (!strcmp(nm_drivers[i]->dev_type,dev_type))
         return nm_drivers[i];

   return NULL;
}

/* Add a NM binding */
int c2600_nm_add_binding(c2600_t *router,char *dev_type,u_int nm_bay)
{   
   struct c2600_nm_driver *nm_driver;
   struct c2600_nm_bay *bay;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   /* check that this bay is empty */
   if (bay->dev_type != NULL) {
      vm_error(router->vm,"a NM already exists in slot %u.\n",nm_bay);
      return(-1);
   }

   /* find the NM driver */
   if (!(nm_driver = c2600_nm_get_driver(dev_type))) {
      vm_error(router->vm,"unknown NM type '%s'.\n",dev_type);
      return(-1);
   }

   bay->dev_type = nm_driver->dev_type;
   bay->nm_driver = nm_driver;
   return(0);  
}

/* Remove a NM binding */
int c2600_nm_remove_binding(c2600_t *router,u_int nm_bay)
{   
   struct c2600_nm_bay *bay;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   /* stop if this bay is still active */
   if (bay->drv_info != NULL) {
      vm_error(router->vm,"slot %u still active.\n",nm_bay);
      return(-1);
   }

   /* check that this bay is not empty */
   if (bay->dev_type == NULL) {
      vm_error(router->vm,"slot %u is empty.\n",nm_bay);
      return(-1);
   }
   
   /* remove all NIOs bindings */ 
   c2600_nm_remove_all_nio_bindings(router,nm_bay);

   bay->dev_type  = NULL;
   bay->nm_driver = NULL;
   return(0);
}

/* Find a NIO binding */
struct c2600_nio_binding *
c2600_nm_find_nio_binding(c2600_t *router,u_int nm_bay,u_int port_id)
{   
   struct c2600_nio_binding *nb;
   struct c2600_nm_bay *bay;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return NULL;

   for(nb=bay->nio_list;nb;nb=nb->next)
      if (nb->port_id == port_id)
         return nb;

   return NULL;
}

/* Add a network IO binding */
int c2600_nm_add_nio_binding(c2600_t *router,u_int nm_bay,u_int port_id,
                             char *nio_name)
{
   struct c2600_nio_binding *nb;
   struct c2600_nm_bay *bay;
   netio_desc_t *nio;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   /* check that a NIO is not already bound to this port */
   if (c2600_nm_find_nio_binding(router,nm_bay,port_id) != NULL) {
      vm_error(router->vm,"a NIO already exists for interface %u/%u.\n",
               nm_bay,port_id);
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
               "for interface %u/%u.\n",nm_bay,port_id);
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
int c2600_nm_remove_nio_binding(c2600_t *router,u_int nm_bay,u_int port_id)
{
   struct c2600_nio_binding *nb;
   struct c2600_nm_bay *bay;
   
   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   if (!(nb = c2600_nm_find_nio_binding(router,nm_bay,port_id)))
      return(-1);   /* no nio binding for this slot/port */

   /* tell the NM driver to stop using this NIO */
   if (bay->nm_driver)
      bay->nm_driver->nm_unset_nio(router,nm_bay,port_id);

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

/* Remove all NIO bindings for the specified NM */
int c2600_nm_remove_all_nio_bindings(c2600_t *router,u_int nm_bay)
{  
   struct c2600_nio_binding *nb,*next;
   struct c2600_nm_bay *bay;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   for(nb=bay->nio_list;nb;nb=next) {
      next = nb->next;

      /* tell the NM driver to stop using this NIO */
      if (bay->nm_driver)
         bay->nm_driver->nm_unset_nio(router,nm_bay,nb->port_id);

      /* unreference NIO object */
      netio_release(nb->nio->name);
      free(nb);
   }

   bay->nio_list = NULL;
   return(0);
}

/* Enable a Network IO descriptor for a Network Module */
int c2600_nm_enable_nio(c2600_t *router,u_int nm_bay,u_int port_id)
{
   struct c2600_nio_binding *nb;
   struct c2600_nm_bay *bay;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   /* check that we have an NIO binding for this interface */
   if (!(nb = c2600_nm_find_nio_binding(router,nm_bay,port_id)))
      return(-1);

   /* check that the driver is defined and successfully initialized */
   if (!bay->nm_driver || !bay->drv_info)
      return(-1);

   return(bay->nm_driver->nm_set_nio(router,nm_bay,port_id,nb->nio));
}

/* Disable Network IO descriptor of a Network Module */
int c2600_nm_disable_nio(c2600_t *router,u_int nm_bay,u_int port_id)
{
   struct c2600_nm_bay *bay;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   /* check that the driver is defined and successfully initialized */
   if (!bay->nm_driver || !bay->drv_info)
      return(-1);

   return(bay->nm_driver->nm_unset_nio(router,nm_bay,port_id));
}

/* Enable all NIO of the specified NM */
int c2600_nm_enable_all_nio(c2600_t *router,u_int nm_bay)
{
   struct c2600_nio_binding *nb;
   struct c2600_nm_bay *bay;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   /* check that the driver is defined and successfully initialized */
   if (!bay->nm_driver || !bay->drv_info)
      return(-1);

   for(nb=bay->nio_list;nb;nb=nb->next)
      bay->nm_driver->nm_set_nio(router,nm_bay,nb->port_id,nb->nio);

   return(0);
}

/* Disable all NIO of the specified NM */
int c2600_nm_disable_all_nio(c2600_t *router,u_int nm_bay)
{
   struct c2600_nio_binding *nb;
   struct c2600_nm_bay *bay;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   /* check that the driver is defined and successfully initialized */
   if (!bay->nm_driver || !bay->drv_info)
      return(-1);

   for(nb=bay->nio_list;nb;nb=nb->next)
      bay->nm_driver->nm_unset_nio(router,nm_bay,nb->port_id);

   return(0);
}

/* Initialize a Network Module */
int c2600_nm_init(c2600_t *router,u_int nm_bay)
{   
   struct c2600_nm_bay *bay;
   size_t len;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   /* Check that a device type is defined for this bay */
   if (!bay->dev_type || !bay->nm_driver) {
      vm_error(router->vm,"trying to init empty slot %u.\n",nm_bay);
      return(-1);
   }

   /* Allocate device name */
   len = strlen(bay->dev_type) + 10;
   if (!(bay->dev_name = malloc(len))) {
      vm_error(router->vm,"unable to allocate device name.\n");
      return(-1);
   }

   snprintf(bay->dev_name,len,"%s(%u)",bay->dev_type,nm_bay);

   /* Initialize NM driver */
   if (bay->nm_driver->nm_init(router,bay->dev_name,nm_bay) == 1) {
      vm_error(router->vm,"unable to initialize NM %u.\n",nm_bay);
      return(-1);
   }

   /* Enable all NIO */
   c2600_nm_enable_all_nio(router,nm_bay);
   return(0);
}

/* Shutdown a Network Module */
int c2600_nm_shutdown(c2600_t *router,u_int nm_bay)
{
   struct c2600_nm_bay *bay;

   if (!(bay = c2600_nm_get_info(router,nm_bay)))
      return(-1);

   /* Check that a device type is defined for this bay */   
   if (!bay->dev_type || !bay->nm_driver) {
      vm_error(router->vm,"trying to shut down empty slot %u.\n",nm_bay);
      return(-1);
   }

   /* Disable all NIO */
   c2600_nm_disable_all_nio(router,nm_bay);

   /* Shutdown the NM driver */
   if (bay->drv_info && (bay->nm_driver->nm_shutdown(router,nm_bay) == -1)) {
      vm_error(router->vm,"unable to shutdown NM %u.\n",nm_bay);
      return(-1);
   }

   free(bay->dev_name);
   bay->dev_name = NULL;
   bay->drv_info = NULL;
   return(0);
}

/* Shutdown all NM of a router */
int c2600_nm_shutdown_all(c2600_t *router)
{
   int i;

   for(i=0;i<C2600_MAX_NM_BAYS;i++) {
      if (!router->nm_bay[i].dev_type) 
         continue;

      c2600_nm_shutdown(router,i);
   }

   return(0);
}

/* Show info about all NMs */
int c2600_nm_show_all_info(c2600_t *router)
{
   struct c2600_nm_bay *bay;
   int i;

   for(i=0;i<C2600_MAX_NM_BAYS;i++) {
      if (!(bay = c2600_nm_get_info(router,i)) || !bay->nm_driver)
         continue;

      if (bay->nm_driver->nm_show_info != NULL)
         bay->nm_driver->nm_show_info(router,i);
   }

   return(0);
}

/* Maximum number of tokens in a NM description */
#define NM_DESC_MAX_TOKENS  8

/* Create a Network Module (command line) */
int c2600_cmd_nm_create(c2600_t *router,char *str)
{
   char *tokens[NM_DESC_MAX_TOKENS];
   int i,count,res;
   u_int nm_bay;

   /* A port adapter description is like "1:NM-1FE" */
   if ((count = m_strsplit(str,':',tokens,NM_DESC_MAX_TOKENS)) != 2) {
      vm_error(router->vm,"unable to parse NM description '%s'.\n",str);
      return(-1);
   }

   /* Parse the NM bay id */
   nm_bay = atoi(tokens[0]);

   /* Add this new NM to the current NM list */
   res = c2600_nm_add_binding(router,tokens[1],nm_bay);

   /* The complete array was cleaned by strsplit */
   for(i=0;i<NM_DESC_MAX_TOKENS;i++)
      free(tokens[i]);

   return(res);
}

/* Add a Network IO descriptor binding (command line) */
int c2600_cmd_add_nio(c2600_t *router,char *str)
{
   char *tokens[NM_DESC_MAX_TOKENS];
   int i,count,nio_type,res=-1;
   u_int nm_bay,port_id;
   netio_desc_t *nio;
   char nio_name[128];

   /* A port adapter description is like "1:3:tap:tap0" */
   if ((count = m_strsplit(str,':',tokens,NM_DESC_MAX_TOKENS)) < 3) {
      vm_error(router->vm,"unable to parse NIO description '%s'.\n",str);
      return(-1);
   }

   /* Parse the NM bay */
   nm_bay = atoi(tokens[0]);

   /* Parse the NM port id */
   port_id = atoi(tokens[1]);

   /* Autogenerate a NIO name */
   snprintf(nio_name,sizeof(nio_name),"c2600-i%u/%u/%u",
            router->vm->instance_id,nm_bay,port_id);

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
            vm_error(router->vm,
                     "invalid number of arguments for Generic Eth NIO '%s'\n",
                     str);
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
      vm_error(router->vm,"unable to create NETIO "
              "descriptor for NM slot %u\n",nm_bay);
      goto done;
   }

   if (c2600_nm_add_nio_binding(router,nm_bay,port_id,nio_name) == -1) {
      vm_error(router->vm,"unable to add NETIO binding for slot %u\n",nm_bay);
      netio_release(nio_name);
      netio_delete(nio_name);
      goto done;
   }
   
   netio_release(nio_name);
   res = 0;

 done:
   /* The complete array was cleaned by strsplit */
   for(i=0;i<NM_DESC_MAX_TOKENS;i++)
      free(tokens[i]);

   return(res);
}

/* Show the list of available NM drivers */
void c2600_nm_show_drivers(void)
{
   int i;

   printf("Available C2600 Network Module drivers:\n");

   for(i=0;nm_drivers[i];i++) {
      printf("  * %s %s\n",
             nm_drivers[i]->dev_type,
             !nm_drivers[i]->supported ? "(NOT WORKING)" : "");
   }
   
   printf("\n");
}

/* Set the base MAC address of the chassis */
static int c2600_burn_mac_addr(c2600_t *router,n_eth_addr_t *addr)
{
   int i;

   for(i=0;i<3;i++) {
      router->vm->chassis_cookie[i+1] = addr->eth_addr_byte[i*2] << 8;
      router->vm->chassis_cookie[i+1] |= addr->eth_addr_byte[(i*2)+1];
   }

   return(0);
}

/* Set mainboard type */
int c2600_mainboard_set_type(c2600_t *router,char *mainboard_type)
{
   struct c2600_mb_id *mb_info;

   if (router->vm->status == VM_STATUS_RUNNING) {
      vm_error(router->vm,"unable to change mainboard type when online.\n");
      return(-1);
   }

   if (!(mb_info = c2600_get_mb_info(mainboard_type))) {
      vm_error(router->vm,"unknown mainboard '%s'\n",mainboard_type);
      return(-1);
   }

   router->mainboard_type = mainboard_type;

   /* Set the cookie */
   memcpy(router->vm->chassis_cookie,
          eeprom_c2600_mb_data,sizeof(eeprom_c2600_mb_data));

   router->vm->chassis_cookie[6] = mb_info->id;

   /* Set the chassis base MAC address */
   c2600_burn_mac_addr(router,&router->mac_addr);
   return(0);
}

/* Set chassis MAC address */
int c2600_chassis_set_mac_addr(c2600_t *router,char *mac_addr)
{
   if (parse_mac_addr(&router->mac_addr,mac_addr) == -1) {
      vm_error(router->vm,"unable to parse MAC address '%s'.\n",mac_addr);
      return(-1);
   }

   /* Set the chassis base MAC address */
   c2600_burn_mac_addr(router,&router->mac_addr);
   return(0);
}

/* Initialize a Cisco 2600 */
static int c2600_init(c2600_t *router)
{   
   vm_instance_t *vm = router->vm;

   /* Create the PCI bus */
   if (!(vm->pci_bus[0] = pci_bus_create("PCI0",0))) {
      vm_error(vm,"unable to create PCI data.\n");
      return(-1);
   }
   
   /* Create the PCI controller */
   if (dev_c2600_pci_init(vm,"c2600_pci",C2600_PCICTRL_ADDR,0x1000,
                          vm->pci_bus[0]) == -1)
      return(-1);

   /* Bind PCI bus to slots 0 and 1 */
   router->nm_bay[0].pci_map = vm->pci_bus[0];
   router->nm_bay[1].pci_map = vm->pci_bus[0];

   vm->elf_machine_id = C2600_ELF_MACHINE_ID;
   return(0);
}

/* Show C2600 hardware info */
void c2600_show_hardware(c2600_t *router)
{
   vm_instance_t *vm = router->vm;

   printf("C2600 instance '%s' (id %d):\n",vm->name,vm->instance_id);

   printf("  VM Status  : %d\n",vm->status);
   printf("  RAM size   : %u Mb\n",vm->ram_size);
   printf("  NVRAM size : %u Kb\n",vm->nvram_size);
   printf("  IOS image  : %s\n\n",vm->ios_image);

   if (vm->debug_level > 0) {
      dev_show_list(vm);
      pci_dev_show_list(vm->pci_bus[0]);
      pci_dev_show_list(vm->pci_bus[1]);
      printf("\n");
   }
}

/* Initialize default parameters for a C2600 */
void c2600_init_defaults(c2600_t *router)
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

   c2600_init_eeprom_groups(router);
   c2600_mainboard_set_type(router,C2600_DEFAULT_MAINBOARD);
   c2600_burn_mac_addr(router,&router->mac_addr);

   vm->ram_mmap          = C2600_DEFAULT_RAM_MMAP;
   vm->ram_size          = C2600_DEFAULT_RAM_SIZE;
   vm->rom_size          = C2600_DEFAULT_ROM_SIZE;
   vm->nvram_size        = C2600_DEFAULT_NVRAM_SIZE;
   vm->conf_reg_setup    = C2600_DEFAULT_CONF_REG;
   vm->clock_divisor     = C2600_DEFAULT_CLOCK_DIV;
   vm->nvram_rom_space   = C2600_NVRAM_ROM_RES_SIZE;
   router->nm_iomem_size = C2600_DEFAULT_IOMEM_SIZE;

   vm->pcmcia_disk_size[0] = C2600_DEFAULT_DISK0_SIZE;
   vm->pcmcia_disk_size[1] = C2600_DEFAULT_DISK1_SIZE;

   /* Enable NVRAM operations to load/store configs */
   vm->nvram_extract_config = c2600_nvram_extract_config;
   vm->nvram_push_config = c2600_nvram_push_config;
}

/* Set an IRQ */
static void c2600_set_irq(vm_instance_t *vm,u_int irq)
{
   c2600_t *router = VM_C2600(vm);
   cpu_ppc_t *cpu = CPU_PPC32(vm->boot_cpu);
   
   switch(irq) {
      case C2600_VTIMER_IRQ:
         mpc860_set_pending_irq(router->mpc_data,30);
         break;
      case C2600_DUART_IRQ:
         mpc860_set_pending_irq(router->mpc_data,29);
         break;
      case C2600_NETIO_IRQ:
         mpc860_set_pending_irq(router->mpc_data,25);
         break;
      case C2600_PA_MGMT_IRQ:
         mpc860_set_pending_irq(router->mpc_data,27);
         break;

      /* IRQ test */
      case 255:
         mpc860_set_pending_irq(router->mpc_data,24);
         break;
   }

   if (cpu->irq_idle_preempt[irq])
      cpu_idle_break_wait(cpu->gen);
}

/* Clear an IRQ */
static void c2600_clear_irq(vm_instance_t *vm,u_int irq)
{
   c2600_t *router = VM_C2600(vm);

   switch(irq) {
      case C2600_VTIMER_IRQ:
         mpc860_clear_pending_irq(router->mpc_data,30);
         break;
      case C2600_DUART_IRQ:
         mpc860_clear_pending_irq(router->mpc_data,29);
         break;
      case C2600_NETIO_IRQ:
         mpc860_clear_pending_irq(router->mpc_data,25);
         break;
      case C2600_PA_MGMT_IRQ:
         mpc860_clear_pending_irq(router->mpc_data,27);
         break;

      /* IRQ test */
      case 255:
         mpc860_clear_pending_irq(router->mpc_data,24);
         break;
   }
}

/* Initialize the C2600 Platform */
int c2600_init_platform(c2600_t *router)
{
   vm_instance_t *vm = router->vm;
   struct c2600_mb_id *mb_info;
   struct c2600_nm_bay *nm_bay;
   vm_obj_t *obj;
   cpu_ppc_t *cpu;
   cpu_gen_t *gen;
   int i;

   /* Copy config register setup into "active" config register */
   vm->conf_reg = vm->conf_reg_setup;

   /* Create Console and AUX ports */
   vm_init_vtty(vm);

   /* Create a CPU group */
   vm->cpu_group = cpu_group_create("System CPU");

   /* Initialize the virtual PowerPC processor */
   if (!(gen = cpu_create(vm,CPU_TYPE_PPC32,0))) {
      vm_error(vm,"unable to create CPU!\n");
      return(-1);
   }

   cpu = CPU_PPC32(gen);

   /* Add this CPU to the system CPU group */
   cpu_group_add(vm->cpu_group,gen);
   vm->boot_cpu = gen;

   /* Set processor ID */
   ppc32_set_pvr(cpu,0x00500202);

   /* Mark the Network IO interrupt as high priority */
   cpu->irq_idle_preempt[C2600_NETIO_IRQ] = TRUE;
   cpu->irq_idle_preempt[C2600_DUART_IRQ] = TRUE;

   /* Copy some parameters from VM to CPU (idle PC, ...) */
   cpu->idle_pc = vm->idle_pc;

   if (vm->timer_irq_check_itv)
      cpu->timer_irq_check_itv = vm->timer_irq_check_itv;

   /* Remote emulator control */
   dev_remote_control_init(vm,0xf6000000,0x1000);

   /* MPC860 */
   if (dev_mpc860_init(vm,"MPC860",C2600_MPC860_ADDR,0x10000) == -1)
      return(-1);

   if (!(obj = vm_object_find(router->vm,"MPC860")))
      return(-1);

   router->mpc_data = obj->data;

   /* IO FPGA */
   if (dev_c2600_iofpga_init(router,C2600_IOFPGA_ADDR,0x10000) == -1)
      return(-1);

   /* Initialize the chassis */
   if (c2600_init(router) == -1)
      return(-1);

   /* Initialize RAM */
   vm_ram_init(vm,0x00000000ULL);

   /* Initialize ROM */
   if (!vm->rom_filename) {
      /* use embedded ROM */
      dev_rom_init(vm,"rom",C2600_ROM_ADDR,512*1024,
                   ppc32_microcode,ppc32_microcode_len);
   } else {
      /* use alternate ROM */
      dev_ram_init(vm,"rom",TRUE,TRUE,NULL,FALSE,C2600_ROM_ADDR,512*1024);
   }

   /* RAM aliasing */
   dev_create_ram_alias(vm,"ram_alias","ram",0x80000000,vm->ram_size*1048576);

   /* NVRAM */
   dev_ram_init(vm,"nvram",TRUE,FALSE,NULL,FALSE,
                C2600_NVRAM_ADDR,vm->nvram_size*4096);
   c2600_nvram_check_empty_config(vm);

   /* Bootflash */
   dev_bootflash_init(vm,"flash0",C2600_FLASH_ADDR,8*1048576);
   dev_bootflash_init(vm,"flash1",C2600_FLASH_ADDR+0x800000,8*1048576);

   /* Initialize the NS16552 DUART */
   dev_ns16552_init(vm,C2600_DUART_ADDR,0x1000,0,C2600_DUART_IRQ,
                    vm->vtty_con,vm->vtty_aux);

   /* Initialize the mainboard ports */
   if ((mb_info = c2600_get_mb_info(router->mainboard_type)) != NULL)
      c2600_nm_add_binding(router,mb_info->mb_driver,0);

   /* Initialize Network Modules */
   for(i=0;i<C2600_MAX_NM_BAYS;i++) {
      nm_bay = &router->nm_bay[i];

      if (!nm_bay->dev_type) 
         continue;

      if (c2600_nm_init(router,i) == -1) {
         vm_error(vm,"unable to create Network Module \"%s\"\n",
                  nm_bay->dev_type);
         return(-1);
      }
   }

   /* Show device list */
   c2600_show_hardware(router);
   return(0);
}

static struct ppc32_bat_prog bat_array[] = {
   { PPC32_IBAT_IDX, 0, 0xfff0001e, 0xfff00001 },
   { PPC32_IBAT_IDX, 1, 0x00001ffe, 0x00000001 },
   { PPC32_IBAT_IDX, 2, 0x00000000, 0xee3e0072 },
   { PPC32_IBAT_IDX, 3, 0x80001ffe, 0x80000001 },

   { PPC32_DBAT_IDX, 0, 0x80001ffe, 0x80000042 },
   { PPC32_DBAT_IDX, 1, 0x00001ffe, 0x0000002a },
   { PPC32_DBAT_IDX, 2, 0x40007ffe, 0x4000002a },
   { PPC32_DBAT_IDX, 3, 0xfc0007fe, 0xfc00002a },
   { -1, -1, 0, 0 },
};

/* Boot the IOS image */
int c2600_boot_ios(c2600_t *router)
{   
   vm_instance_t *vm = router->vm;
   cpu_ppc_t *cpu;

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
   cpu = CPU_PPC32(vm->boot_cpu);
   ppc32_reset(cpu);

   /* Adjust stack pointer */
   cpu->gpr[1] |= 0x80000000;

   /* Load BAT registers */
   printf("Loading BAT registers\n");
   ppc32_load_bat_array(CPU_PPC32(vm->boot_cpu),bat_array);

   /* IRQ routing */
   vm->set_irq = c2600_set_irq;
   vm->clear_irq = c2600_clear_irq;

   /* Load IOS image */
   if (ppc32_load_elf_image(cpu,vm->ios_image,
                            (vm->ghost_status == VM_GHOST_RAM_USE),
                            &vm->ios_entry_point) < 0) 
   {
      vm_error(vm,"failed to load Cisco IOS image '%s'.\n",vm->ios_image);
      return(-1);
   }

   /* Launch the simulation */
   printf("\nC2600 '%s': starting simulation (CPU0 IA=0x%8.8x), "
          "JIT %sabled.\n",
          vm->name,cpu->ia,vm->jit_use ? "en":"dis");

   vm_log(vm,"C2600_BOOT",
          "starting instance (CPU0 PC=0x%8.8x,idle_pc=0x%8.8x,JIT %s)\n",
          cpu->ia,cpu->idle_pc,vm->jit_use ? "on":"off");

   /* Start main CPU */
   if (vm->ghost_status != VM_GHOST_RAM_GENERATE) {
      vm->status = VM_STATUS_RUNNING;
      cpu_start(vm->boot_cpu);
   } else {
      vm->status = VM_STATUS_SHUTDOWN;
   }
   return(0);
}

/* Initialize a Cisco 2600 instance */
int c2600_init_instance(c2600_t *router)
{   
   vm_instance_t *vm = router->vm;
   m_uint32_t rom_entry_point;
   cpu_ppc_t *cpu0;

   if (!vm->ios_image) {
      vm_error(vm,"no Cisco IOS image defined.");
      return(-1);
   }

   /* Initialize the C2600 platform */
   if (c2600_init_platform(router) == -1) {
      vm_error(vm,"unable to initialize the platform hardware.\n");
      return(-1);
   }

   /* Load IOS configuration file */
   if (vm->ios_config != NULL) {
      vm_nvram_push_config(vm,vm->ios_config);
      vm->conf_reg &= ~0x40;
   }

   /* Load ROM (ELF image or embedded) */
   cpu0 = CPU_PPC32(vm->boot_cpu);
   rom_entry_point = (m_uint32_t)PPC32_ROM_START;

   if ((vm->rom_filename != NULL) &&
       (ppc32_load_elf_image(cpu0,vm->rom_filename,0,&rom_entry_point) < 0))
   {
      vm_error(vm,"unable to load alternate ROM '%s', "
               "fallback to embedded ROM.\n\n",vm->rom_filename);
      vm->rom_filename = NULL;
   }

   return(c2600_boot_ios(router));
}

/* Stop a Cisco 2600 instance */
int c2600_stop_instance(c2600_t *router)
{
   vm_instance_t *vm = router->vm;

   printf("\nC2600 '%s': stopping simulation.\n",vm->name);
   vm_log(vm,"C2600_STOP","stopping simulation.\n");

   /* Stop all CPUs */
   if (vm->cpu_group != NULL) {
      vm_stop(vm);
      
      if (cpu_group_sync_state(vm->cpu_group) == -1) {
         vm_error(vm,"unable to sync with system CPUs.\n");
         return(-1);
      }
   }

   /* Free resources that were used during execution to emulate hardware */
   c2600_nm_shutdown_all(router);
   vm_hardware_shutdown(vm);
   return(0);
}
