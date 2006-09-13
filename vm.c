/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Virtual machine abstraction.
 *
 * TODO: IRQ Routing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <assert.h>

#include "registry.h"
#include "device.h"
#include "pci_dev.h"
#include "pci_io.h"
#include "vm.h"
#include "dev_vtty.h"

#include ARCH_INC_FILE

#define DEBUG_VM  1

/* Type of VM file naming (0=use VM name, 1=use instance ID) */
int vm_file_naming_type = 0;

/* Initialize a VM object */
void vm_object_init(vm_obj_t *obj)
{
   memset(obj,0,sizeof(*obj));
}

/* Add a VM object to an instance */
void vm_object_add(vm_instance_t *vm,vm_obj_t *obj)
{
   obj->next = vm->vm_object_list;
   obj->pprev = &vm->vm_object_list;

   if (vm->vm_object_list)
      vm->vm_object_list->pprev = &obj->next;
   
   vm->vm_object_list = obj;
}

/* Remove a VM object from an instance */
void vm_object_remove(vm_instance_t *vm,vm_obj_t *obj)
{
   if (obj->next)
      obj->next->pprev = obj->pprev;
   *(obj->pprev) = obj->next;
}

/* Find an object given its name */
vm_obj_t *vm_object_find(vm_instance_t *vm,char *name)
{
   vm_obj_t *obj;

   for(obj=vm->vm_object_list;obj;obj=obj->next)
      if (!strcmp(obj->name,name))
         return obj;

   return NULL;
}

/* Check that a mandatory object is present */
int vm_object_check(vm_instance_t *vm,char *name)
{
   return(vm_object_find(vm,name) ? 0 : -1);
}

/* Shut down all objects of an instance */
void vm_object_free_list(vm_instance_t *vm)
{
   vm_obj_t *obj,*next;

   for(obj=vm->vm_object_list;obj;obj=next) {
      next = obj->next;

      if (obj->shutdown != NULL) {
#if DEBUG_VM
         vm_log(vm,"VM_OBJECT","Shutdown of object \"%s\"\n",obj->name);
#endif
         obj->shutdown(vm,obj->data);
      }
   }

   vm->vm_object_list = NULL;
}

/* Dump the object list of an instance */
void vm_object_dump(vm_instance_t *vm)
{
   vm_obj_t *obj;

   printf("VM \"%s\" (%u) object list:\n",vm->name,vm->instance_id);
   
   for(obj=vm->vm_object_list;obj;obj=obj->next) {
      printf("  - %-15s [data=%p]\n",obj->name,obj->data);
   }

   printf("\n");
}

/* Get VM type */
char *vm_get_type(vm_instance_t *vm)
{
   char *machine;

   switch(vm->type) {
      case VM_TYPE_C3600:
         machine = "c3600";
         break;
      case VM_TYPE_C7200:
         machine = "c7200";
         break;
      default:
         machine = "unknown";
         break;
   }

   return machine;
}

/* Get platform type */
char *vm_get_platform_type(vm_instance_t *vm)
{
   char *machine;

   switch(vm->type) {
      case VM_TYPE_C3600:
         machine = "C3600";
         break;
      case VM_TYPE_C7200:
         machine = "C7200";
         break;
      default:
         machine = "VM";
         break;
   }

   return machine;
}

/* Generate a filename for use by the instance */
char *vm_build_filename(vm_instance_t *vm,char *name)
{
   char *filename,*machine;

   machine = vm_get_type(vm);

   switch(vm_file_naming_type) {
      case 1:
         filename = dyn_sprintf("%s_i%u_%s",machine,vm->instance_id,name);
         break;
      case 0:
      default:
         filename = dyn_sprintf("%s_%s_%s",machine,vm->name,name);
         break;
   }

   assert(filename != NULL);
   return filename;
}

/* Erase lock file */
void vm_release_lock(vm_instance_t *vm,int erase)
{
   if (vm->lock_fd != NULL) {
      fclose(vm->lock_fd);
      vm->lock_fd = NULL;
   }
   
   if (vm->lock_file != NULL) {
      if (erase)
         unlink(vm->lock_file);
      free(vm->lock_file);
      vm->lock_file = NULL;
   }
}

/* Check that an instance lock file doesn't already exist */
int vm_get_lock(vm_instance_t *vm)
{
   char pid_str[32];
   struct flock lock;

   vm->lock_file = vm_build_filename(vm,"lock");

   if (!(vm->lock_fd = fopen(vm->lock_file,"w"))) {
      fprintf(stderr,"Unable to create lock file \"%s\".\n",vm->lock_file);
      return(-1);
   }
   
   memset(&lock,0,sizeof(lock));
   lock.l_type   = F_WRLCK;
   lock.l_whence = SEEK_SET;
   lock.l_start  = 0;
   lock.l_len    = 0;
   
   if (fcntl(fileno(vm->lock_fd),F_SETLK,&lock) == -1) {
      if (fcntl(fileno(vm->lock_fd),F_GETLK,&lock) == 0) {
         snprintf(pid_str,sizeof(pid_str),"%ld",(long)lock.l_pid);
      } else {
         strcpy(pid_str,"unknown");
      }

      fprintf(stderr,
              "\nAn emulator instance (PID %s) is already running with "
              "identifier %u.\n"
              "If this is not the case, please erase file \"%s\".\n\n",
              pid_str,vm->instance_id,vm->lock_file);
      vm_release_lock(vm,FALSE);
      return(-1);
   }

   /* write the emulator PID */
   fprintf(vm->lock_fd,"%ld\n",(u_long)getpid());
   return(0);
}

/* Log a message */
void vm_flog(vm_instance_t *vm,char *module,char *format,va_list ap)
{
   if (vm->log_fd)
      m_flog(vm->log_fd,module,format,ap);
}

/* Log a message */
void vm_log(vm_instance_t *vm,char *module,char *format,...)
{ 
   va_list ap;

   va_start(ap,format);
   vm_flog(vm,module,format,ap);
   va_end(ap);
}

/* Close the log file */
int vm_close_log(vm_instance_t *vm)
{
   if (vm->log_fd)
      fclose(vm->log_fd);

   free(vm->log_file);

   vm->log_file = NULL;
   vm->log_fd = NULL;
   return(0);
}

/* Create the log file */
int vm_create_log(vm_instance_t *vm)
{
   vm_close_log(vm);

   if (!(vm->log_file = vm_build_filename(vm,"log.txt")))
      return(-1);

   if (!(vm->log_fd = fopen(vm->log_file,"w"))) {
      fprintf(stderr,"VM %s: unable to create log file '%s'\n",
              vm->name,vm->log_file);
      free(vm->log_file);
      vm->log_file = NULL;
      return(-1);
   }

   return(0);
}

/* Error message */
void vm_error(vm_instance_t *vm,char *format,...)
{ 
   char buffer[2048];
   va_list ap;

   va_start(ap,format);
   vsnprintf(buffer,sizeof(buffer),format,ap);
   va_end(ap);

   fprintf(stderr,"%s '%s': %s",vm_get_platform_type(vm),vm->name,buffer);
}

/* Create a new VM instance */
vm_instance_t *vm_create(char *name,int instance_id,int machine_type)
{
   vm_instance_t *vm;

   if (!(vm = malloc(sizeof(*vm)))) {
      fprintf(stderr,"VM %s: unable to create new instance!\n",name);
      return NULL;
   }
   
   memset(vm,0,sizeof(*vm));
   vm->instance_id    = instance_id;
   vm->type           = machine_type;
   vm->status         = VM_STATUS_HALTED;
   vm->jit_use        = JIT_SUPPORT;
   vm->vtty_con_type  = VTTY_TYPE_TERM;
   vm->vtty_aux_type  = VTTY_TYPE_NONE;
   vm->timer_irq_check_itv = VM_TIMER_IRQ_CHECK_ITV;

   if (!(vm->name = strdup(name))) {
      fprintf(stderr,"VM %s: unable to store instance name!\n",name);
      goto err_name;
   }

   /* create lock file */
   if (vm_get_lock(vm) == -1)
      goto err_lock;
   
   /* create log file */
   if (vm_create_log(vm) == -1)
      goto err_log;

   if (registry_add(vm->name,OBJ_TYPE_VM,vm) == -1) {
      fprintf(stderr,"VM: Unable to store instance '%s' in registry!\n",
              vm->name);
      goto err_reg_add;
   }

   return vm;

 err_reg_add:
   vm_close_log(vm);
 err_log:
   free(vm->lock_file);
 err_lock:
   free(vm->name);
 err_name:
   free(vm);
   return NULL;
}

/* 
 * Shutdown hardware resources used by a VM.
 * The CPU must have been stopped.
 */
int vm_hardware_shutdown(vm_instance_t *vm)
{  
   int i;

   if ((vm->status == VM_STATUS_HALTED) || !vm->cpu_group) {
      vm_log(vm,"VM","trying to shutdown an inactive VM.\n");
      return(-1);
   }

   vm_log(vm,"VM","shutdown procedure engaged.\n");

   /* Mark the VM as halted */
   vm->status = VM_STATUS_HALTED;

   /* Disable NVRAM operations */
   vm->nvram_extract_config = NULL;
   vm->nvram_push_config = NULL;

   /* Free the object list */
   vm_object_free_list(vm);

   /* Free resources used by PCI busses */
   vm_log(vm,"VM","removing PCI busses.\n");
   pci_io_data_remove(vm,vm->pci_io_space);
   pci_bus_remove(vm->pci_bus[0]);
   pci_bus_remove(vm->pci_bus[1]);
   vm->pci_bus[0] = vm->pci_bus[1] = NULL;

   /* Free the PCI bus pool */
   for(i=0;i<VM_PCI_POOL_SIZE;i++) {
      if (vm->pci_bus_pool[i] != NULL) {
         pci_bus_remove(vm->pci_bus_pool[i]);
         vm->pci_bus_pool[i] = NULL;
      }
   }     

   /* Delete the VTTY for Console and AUX ports */   
   vm_log(vm,"VM","deleting VTTY.\n");
   vm_delete_vtty(vm);

   /* Delete system CPU group */
   vm_log(vm,"VM","deleting system CPUs.\n");
   cpu_group_delete(vm->cpu_group);
   vm->cpu_group = NULL;
   vm->boot_cpu = NULL;

   vm_log(vm,"VM","shutdown procedure completed.\n");
   return(0);
}

/* Free resources used by a VM */
void vm_free(vm_instance_t *vm)
{
   if (vm != NULL) {
      /* Free hardware resources */
      vm_hardware_shutdown(vm);

      /* Close log file */
      vm_close_log(vm);

      /* Remove the lock file */
      vm_release_lock(vm,TRUE);

      /* Free various elements */
      free(vm->sym_filename);
      free(vm->ios_image);
      free(vm->ios_config);
      free(vm->rom_filename);
      free(vm->name);
      free(vm);
   }
}

/* Get an instance given a name */
vm_instance_t *vm_acquire(char *name)
{
   return(registry_find(name,OBJ_TYPE_VM));
}

/* Release a VM (decrement reference count) */
int vm_release(vm_instance_t *vm)
{
   return(registry_unref(vm->name,OBJ_TYPE_VM));
}

/* Initialize VTTY */
int vm_init_vtty(vm_instance_t *vm)
{
   /* Create Console and AUX ports */
   vm->vtty_con = vtty_create(vm,"Console port",
                              vm->vtty_con_type,vm->vtty_con_tcp_port,
                              &vm->vtty_con_serial_option);

   vm->vtty_aux = vtty_create(vm,"AUX port",
                              vm->vtty_aux_type,vm->vtty_aux_tcp_port,
                              &vm->vtty_aux_serial_option);
   return(0);
}

/* Delete VTTY */
void vm_delete_vtty(vm_instance_t *vm)
{
   vtty_delete(vm->vtty_con);
   vtty_delete(vm->vtty_aux);
   vm->vtty_con = vm->vtty_aux = NULL;
}

/* Bind a device to a virtual machine */
int vm_bind_device(vm_instance_t *vm,struct vdevice *dev)
{
   struct vdevice **cur;
   u_int i;

   /* 
    * Add this device to the device array. The index in the device array
    * is used by the MTS subsystem.
    */
   for(i=0;i<MIPS64_DEVICE_MAX;i++)
      if (!vm->dev_array[i])
         break;

   if (i == MIPS64_DEVICE_MAX) {
      fprintf(stderr,"VM%u: vm_bind_device: device table full.\n",
              vm->instance_id);
      return(-1);
   }

   vm->dev_array[i] = dev;
   dev->id = i;

   /*
    * Add it to the linked-list (devices are ordered by physical addresses).
    */
   for(cur=&vm->dev_list;*cur;cur=&(*cur)->next)
      if ((*cur)->phys_addr > dev->phys_addr)
         break;

   dev->next = *cur;
   if (*cur) (*cur)->pprev = &dev->next;
   dev->pprev = cur;
   *cur = dev;
   return(0);
}

/* Unbind a device from a virtual machine */
int vm_unbind_device(vm_instance_t *vm,struct vdevice *dev)
{
   u_int i;

   if (!dev->pprev)
      return(-1);

   /* Remove the device from the linked list */
   if (dev->next)
      dev->next->pprev = dev->pprev;

   *(dev->pprev) = dev->next;

   /* Remove the device from the device array */
   for(i=0;i<MIPS64_DEVICE_MAX;i++)
      if (vm->dev_array[i] == dev) {
         vm->dev_array[i] = NULL;
         break;
      }

   /* Clear device list info */
   dev->next = NULL;
   dev->pprev = NULL;
   return(0);
}

/* Map a device at the specified physical address */
int vm_map_device(vm_instance_t *vm,struct vdevice *dev,m_uint64_t base_addr)
{
#if 0   
   /* Suspend VM activity */
   vm_suspend(vm);

   if (cpu_group_sync_state(vm->cpu_group) == -1) {
      fprintf(stderr,"VM%u: unable to sync with system CPUs.\n",
              vm->instance_id);
      return(-1);
   }
#endif

   /* Unbind the device if it was already active */
   vm_unbind_device(vm,dev);

   /* Map the device at the new base address and rebuild MTS */
   dev->phys_addr = base_addr;
   vm_bind_device(vm,dev);
   cpu_group_rebuild_mts(vm->cpu_group);

#if 0
   vm_resume(vm);
#endif
   return(0);
}

/* Set an IRQ for a VM */
void vm_set_irq(vm_instance_t *vm,u_int irq)
{
   if (vm->boot_cpu->irq_disable) {
      vm->boot_cpu->irq_pending = 0;
      return;
   }

   /* TODO: IRQ routing */
   mips64_set_irq(vm->boot_cpu,irq);

   if (vm->boot_cpu->irq_idle_preempt[irq])
      mips64_idle_break_wait(vm->boot_cpu);
}

/* Clear an IRQ for a VM */
void vm_clear_irq(vm_instance_t *vm,u_int irq)
{
   /* TODO: IRQ routing */
   mips64_clear_irq(vm->boot_cpu,irq);
}

/* Suspend a VM instance */
int vm_suspend(vm_instance_t *vm)
{
   if (vm->status == VM_STATUS_RUNNING) {
      cpu_group_save_state(vm->cpu_group);
      cpu_group_set_state(vm->cpu_group,MIPS_CPU_SUSPENDED);
      vm->status = VM_STATUS_SUSPENDED;
   }
   return(0);
}

/* Resume a VM instance */
int vm_resume(vm_instance_t *vm)
{
   if (vm->status == VM_STATUS_SUSPENDED) {
      cpu_group_restore_state(vm->cpu_group);
      vm->status = VM_STATUS_RUNNING;
   }
   return(0);
}

/* Stop an instance */
int vm_stop(vm_instance_t *vm)
{
   cpu_group_stop_all_cpu(vm->cpu_group);
   vm->status = VM_STATUS_SHUTDOWN;
   return(0);
}

/* Monitor an instance periodically */
void vm_monitor(vm_instance_t *vm)
{
   while(vm->status != VM_STATUS_SHUTDOWN)         
      usleep(200000);
}

/* Save the Cisco IOS configuration from NVRAM */
int vm_ios_save_config(vm_instance_t *vm)
{
   char *output;
   int res;
   
   if (!(output = vm_build_filename(vm,"ios_cfg.txt")))
      return(-1);

   res = vm_nvram_extract_config(vm,output);
   free(output);
   return(res);
}

/* Set Cisco IOS image to use */
int vm_ios_set_image(vm_instance_t *vm,char *ios_image)
{
   char *str;

   if (!(str = strdup(ios_image)))
      return(-1);

   if (vm->ios_image != NULL) {
      free(vm->ios_image);
      vm->ios_image = NULL;
   }

   vm->ios_image = str;
   return(0);
}

/* Unset a Cisco IOS configuration file */
void vm_ios_unset_config(vm_instance_t *vm)
{
   if (vm->ios_config != NULL) {
      free(vm->ios_config);
      vm->ios_config = NULL;
   }
}

/* Set Cisco IOS configuration file to use */
int vm_ios_set_config(vm_instance_t *vm,char *ios_config)
{
   char *str;

   if (!(str = strdup(ios_config)))
      return(-1);

   vm_ios_unset_config(vm);
   vm->ios_config = str;
   return(0);  
}

/* Extract IOS configuration from NVRAM and write it to a file */
int vm_nvram_extract_config(vm_instance_t *vm,char *filename)
{
   char *cfg_buffer;
   ssize_t cfg_len;
   FILE *fd;

   if (!vm->nvram_extract_config)
      return(-1);

   /* Extract the IOS configuration */
   if (((cfg_len = vm->nvram_extract_config(vm,&cfg_buffer)) < 0) || 
       (cfg_buffer == NULL))
      return(-1);

   /* Write configuration to the specified filename */
   if (!(fd = fopen(filename,"w"))) {
      vm_error(vm,"unable to create file '%s'\n",filename);
      free(cfg_buffer);
      return(-1);
   }

   fwrite(cfg_buffer,cfg_len,1,fd);

   fclose(fd);
   free(cfg_buffer);
   return(0);
}

/* Read an IOS configuraton from a file and push it to NVRAM */
int vm_nvram_push_config(vm_instance_t *vm,char *filename)
{
   char *cfg_buffer;
   ssize_t len;
   int res;

   if (!vm->nvram_push_config)
      return(-1);

   /* Read configuration */
   if (((len = m_read_file(filename,&cfg_buffer)) <= 0) || !cfg_buffer)
      return(-1);

   /* Push it! */
   res = vm->nvram_push_config(vm,cfg_buffer,len);
   free(cfg_buffer);
   return(res);
}

/* Save general VM configuration into the specified file */
void vm_save_config(vm_instance_t *vm,FILE *fd)
{
   if (vm->ios_image)
      fprintf(fd,"vm set_ios %s %s\n",vm->name,vm->ios_image);

   fprintf(fd,"vm set_ram %s %u\n",vm->name,vm->ram_size);
   fprintf(fd,"vm set_nvram %s %u\n",vm->name,vm->nvram_size);
   fprintf(fd,"vm set_ram_mmap %s %u\n",vm->name,vm->ram_mmap);
   fprintf(fd,"vm set_clock_divisor %s %u\n",vm->name,vm->clock_divisor);
   fprintf(fd,"vm set_conf_reg %s 0x%4.4x\n",vm->name,vm->conf_reg_setup);

   if (vm->vtty_con_type == VTTY_TYPE_TCP)
      fprintf(fd,"vm set_con_tcp_port %s %d\n",vm->name,vm->vtty_con_tcp_port);

   if (vm->vtty_aux_type == VTTY_TYPE_TCP)
      fprintf(fd,"vm set_aux_tcp_port %s %d\n",vm->name,vm->vtty_aux_tcp_port);
}
