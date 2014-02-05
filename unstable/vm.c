/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Virtual machine abstraction.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <assert.h>
#include <glob.h>

#include "registry.h"
#include "device.h"
#include "pci_dev.h"
#include "pci_io.h"
#include "cpu.h"
#include "vm.h"
#include "tcb.h"
#include "mips64_jit.h"
#include "dev_vtty.h"

#include MIPS64_ARCH_INC_FILE

#define DEBUG_VM  1

#define VM_GLOCK()   pthread_mutex_lock(&vm_global_lock)
#define VM_GUNLOCK() pthread_mutex_unlock(&vm_global_lock)

/* Type of VM file naming (0=use VM name, 1=use instance ID) */
int vm_file_naming_type = 0;

/* Platform list */
static struct vm_platform_list *vm_platforms = NULL;

/* Pool of ghost images */
static vm_ghost_image_t *vm_ghost_pool = NULL;

/* Global lock for VM manipulation */
static pthread_mutex_t vm_global_lock = PTHREAD_MUTEX_INITIALIZER;

/* Free all chunks used by a VM */
static void vm_chunk_free_all(vm_instance_t *vm);

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

   obj->shutdown(vm,obj->data);
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

/* Rebuild the object list pointers */
__unused static void vm_object_rebuild_list(vm_instance_t *vm)
{
   vm_obj_t **obj;

   for(obj=&vm->vm_object_list;*obj;obj=&(*obj)->next)
      (*obj)->pprev = obj;
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
   return vm->platform->name;
}

/* Get log name */
static char *vm_get_log_name(vm_instance_t *vm)
{
   if (vm->platform->log_name != NULL)
      return vm->platform->log_name;

   /* default value */
   return "VM";
}

/* Get MAC address MSB */
u_int vm_get_mac_addr_msb(vm_instance_t *vm)
{
   if (vm->platform->get_mac_addr_msb != NULL)
      return(vm->platform->get_mac_addr_msb());
   
   /* default value */
   return(0xC6);
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

/* Get the amount of host virtual memory used by a VM */
size_t vm_get_vspace_size(vm_instance_t *vm)
{
   struct vdevice *dev;
   size_t hsize = 0;

   /* Add memory used by CPU (exec area) */
   /* XXX TODO */

   /* Add memory used by devices */
   for(dev=vm->dev_list;dev;dev=dev->next)
      hsize += dev_get_vspace_size(dev);

   return(hsize);
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

   if (vm->log_fd) {
      va_start(ap,format);
      vm_flog(vm,module,format,ap);
      va_end(ap);
   }
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
   if (vm->log_file_enabled) {
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
   }

   return(0);
}

/* Reopen the log file */
int vm_reopen_log(vm_instance_t *vm)
{
   if (vm->log_file_enabled) {
      vm_close_log(vm);

      if (!(vm->log_file = vm_build_filename(vm,"log.txt")))
         return(-1);

      if (!(vm->log_fd = fopen(vm->log_file,"a"))) {
         fprintf(stderr,"VM %s: unable to reopen log file '%s'\n",
                 vm->name,vm->log_file);
         free(vm->log_file);
         vm->log_file = NULL;
         return(-1);
      }
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

   fprintf(stderr,"%s '%s': %s",vm_get_log_name(vm),vm->name,buffer);
}

/* Create a new VM instance */
static vm_instance_t *vm_create(char *name,int instance_id,
                                vm_platform_t *platform)
{
   vm_instance_t *vm;

   if (!(vm = malloc(sizeof(*vm)))) {
      fprintf(stderr,"VM %s: unable to create new instance!\n",name);
      return NULL;
   }
   
   memset(vm,0,sizeof(*vm));

   if (!(vm->name = strdup(name))) {
      fprintf(stderr,"VM %s: unable to store instance name!\n",name);
      goto err_name;
   }

   vm->instance_id          = instance_id;
   vm->platform             = platform;
   vm->status               = VM_STATUS_HALTED;
   vm->jit_use              = JIT_SUPPORT;
   vm->exec_blk_direct_jump = TRUE;
   vm->vtty_con_type        = VTTY_TYPE_TERM;
   vm->vtty_aux_type        = VTTY_TYPE_NONE;
   vm->timer_irq_check_itv  = VM_TIMER_IRQ_CHECK_ITV;
   vm->log_file_enabled     = TRUE;
   vm->rommon_vars.filename = vm_build_filename(vm,"rommon_vars");

   if (!vm->rommon_vars.filename)
      goto err_rommon;

   /* XXX */
   rommon_load_file(&vm->rommon_vars);

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

   m_log("VM","VM %s created.\n",vm->name);
   return vm;

 err_reg_add:
   vm_close_log(vm);
 err_log:
   free(vm->lock_file);
 err_lock:
   free(vm->rommon_vars.filename);
 err_rommon:
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

   /* Remove the IRQ routing vectors */
   vm->set_irq = NULL;
   vm->clear_irq = NULL;

   /* Delete the VTTY for Console and AUX ports */   
   vm_log(vm,"VM","deleting VTTY.\n");
   vm_delete_vtty(vm);

   /* Delete system CPU group */
   vm_log(vm,"VM","deleting system CPUs.\n");
   cpu_group_delete(vm->cpu_group);
   vm->cpu_group = NULL;
   vm->boot_cpu = NULL;

   vm_log(vm,"VM","shutdown procedure completed.\n");
   m_log("VM","VM %s shutdown.\n",vm->name);
   return(0);
}

/* Free resources used by a VM */
void vm_free(vm_instance_t *vm)
{
   if (vm != NULL) {
      /* Free hardware resources */
      vm_hardware_shutdown(vm);

      m_log("VM","VM %s destroyed.\n",vm->name);

      /* Close log file */
      vm_close_log(vm);

      /* Remove the lock file */
      vm_release_lock(vm,TRUE);

      /* Free all chunks */
      vm_chunk_free_all(vm);

      /* Free various elements */
      free(vm->rommon_vars.filename);
      free(vm->ghost_ram_filename);
      free(vm->sym_filename);
      free(vm->ios_image);
      free(vm->ios_startup_config);
      free(vm->ios_private_config);
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

/* Initialize RAM */
int vm_ram_init(vm_instance_t *vm,m_uint64_t paddr)
{
   m_uint32_t len;

   len = vm->ram_size * 1048576;

   if (vm->ghost_status == VM_GHOST_RAM_USE) {
      return(dev_ram_ghost_init(vm,"ram",vm->sparse_mem,vm->ghost_ram_filename,
                                paddr,len));
   }

   return(dev_ram_init(vm,"ram",vm->ram_mmap,
                       (vm->ghost_status != VM_GHOST_RAM_GENERATE),
                       vm->ghost_ram_filename,vm->sparse_mem,paddr,len));
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
   for(i=0;i<VM_DEVICE_MAX;i++)
      if (!vm->dev_array[i])
         break;

   if (i == VM_DEVICE_MAX) {
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

   if (!dev || !dev->pprev)
      return(-1);

   /* Remove the device from the linked list */
   if (dev->next)
      dev->next->pprev = dev->pprev;

   *(dev->pprev) = dev->next;

   /* Remove the device from the device array */
   for(i=0;i<VM_DEVICE_MAX;i++)
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

/* Suspend a VM instance */
int vm_suspend(vm_instance_t *vm)
{
   if (vm->status == VM_STATUS_RUNNING) {
      cpu_group_save_state(vm->cpu_group);
      cpu_group_set_state(vm->cpu_group,CPU_STATE_SUSPENDED);
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

/* Create a new chunk */
static vm_chunk_t *vm_chunk_create(vm_instance_t *vm)
{
   vm_chunk_t *chunk;
   size_t area_len;
   
   if (!(chunk = malloc(sizeof(*chunk))))
      return NULL;

   area_len = VM_CHUNK_AREA_SIZE * VM_PAGE_SIZE;

   if (!(chunk->area = m_memalign(VM_PAGE_SIZE,area_len))) {
      free(chunk);
      return NULL;
   }

   chunk->page_alloc = 0;
   chunk->page_total = VM_CHUNK_AREA_SIZE;

   chunk->next = vm->chunks;
   vm->chunks = chunk;
   return chunk;
}

/* Free a chunk */
static void vm_chunk_free(vm_chunk_t *chunk)
{
   free(chunk->area);
   free(chunk);
}

/* Free all chunks used by a VM */
static void vm_chunk_free_all(vm_instance_t *vm)
{
   vm_chunk_t *chunk,*next;

   for(chunk=vm->chunks;chunk;chunk=next) {
      next = chunk->next;
      vm_chunk_free(chunk);
   }

   vm->chunks = NULL;
}

/* Allocate an host page */
void *vm_alloc_host_page(vm_instance_t *vm)
{
   vm_chunk_t *chunk = vm->chunks;
   void *ptr;

   if (!chunk || (chunk->page_alloc == chunk->page_total)) {
      chunk = vm_chunk_create(vm);
      if (!chunk) return NULL;
   }

   ptr = chunk->area + (chunk->page_alloc * VM_PAGE_SIZE);
   chunk->page_alloc++;
   return(ptr);
}

/* Free resources used by a ghost image */
static void vm_ghost_image_free(vm_ghost_image_t *img)
{
   if (img) {
      if (img->fd != -1) {
         close(img->fd);

         if (img->area_ptr != NULL)
            memzone_unmap(img->area_ptr,img->file_size);
      }

      free(img->filename);
      free(img);
   }
}

/* Find a specified ghost image in the pool */
static vm_ghost_image_t *vm_ghost_image_find(char *filename)
{
   vm_ghost_image_t *img;

   for(img=vm_ghost_pool;img;img=img->next)
      if (!strcmp(img->filename,filename))
         return img;

   return NULL;
}

/* Load a new ghost image */
static vm_ghost_image_t *vm_ghost_image_load(char *filename)
{
   vm_ghost_image_t *img;

   if (!(img = calloc(1,sizeof(*img))))
      return NULL;

   img->fd = -1;

   if (!(img->filename = strdup(filename))) {
      vm_ghost_image_free(img);
      return NULL;
   }

   img->fd = memzone_open_file_ro(img->filename,&img->area_ptr,&img->file_size);

   if (img->fd == -1) {
      vm_ghost_image_free(img);
      return NULL;
   }

   m_log("GHOST","loaded ghost image %s (fd=%d) at addr=%p (size=0x%llx)\n",
         img->filename,img->fd,img->area_ptr,(long long)img->file_size);
         
   return img;
}

/* Get a ghost image */
int vm_ghost_image_get(char *filename,u_char **ptr,int *fd)
{
   vm_ghost_image_t *img;

   VM_GLOCK();

   /* Do we already have this image in the pool ? */
   if ((img = vm_ghost_image_find(filename)) != NULL) {
      img->ref_count++;
      *ptr = img->area_ptr;
      *fd  = img->fd;
      VM_GUNLOCK();
      return(0);
   }

   /* Load the ghost file and add it into the pool */
   if (!(img = vm_ghost_image_load(filename))) {
      VM_GUNLOCK();
      fprintf(stderr,"Unable to load ghost image %s\n",filename);
      return(-1);
   }
   
   img->ref_count = 1;
   *ptr = img->area_ptr;
   *fd  = img->fd;

   img->next = vm_ghost_pool;
   vm_ghost_pool = img;   
   VM_GUNLOCK();

   m_log("GHOST","loaded image %s successfully.\n",filename);
   return(0);
}

/* Release a ghost image */
int vm_ghost_image_release(int fd)
{
   vm_ghost_image_t **img,*next;

   VM_GLOCK();

   for(img=&vm_ghost_pool;*img;img=&(*img)->next) {
      if ((*img)->fd == fd) {
         assert((*img)->ref_count > 0);

         (*img)->ref_count--;

         if ((*img)->ref_count == 0) {
            m_log("GHOST","unloaded ghost image %s (fd=%d) at "
                  "addr=%p (size=0x%llx)\n",
                  (*img)->filename,(*img)->fd,(*img)->area_ptr,
                  (long long)(*img)->file_size);

            next = (*img)->next;
            vm_ghost_image_free(*img);
            *img = next;
         }

         VM_GUNLOCK();
         return(0);
      }
   }
   
   VM_GUNLOCK();
   return(-1);
}

/* Open a VM file and map it in memory */
int vm_mmap_open_file(vm_instance_t *vm,char *name,
                      u_char **ptr,off_t *fsize)
{
   char *filename;
   int fd;

   if (!(filename = vm_build_filename(vm,name))) {
      fprintf(stderr,"vm_mmap_open_file: unable to create filename (%s)\n",
              name);
      return(-1);
   }

   if ((fd = memzone_open_file(filename,ptr,fsize)) == -1)
      fprintf(stderr,"vm_mmap_open_file: unable to open file '%s' (%s)\n",
              filename,strerror(errno));

   free(filename);
   return(fd);
}

/* Open/Create a VM file and map it in memory */
int vm_mmap_create_file(vm_instance_t *vm,char *name,size_t len,u_char **ptr)
{
   char *filename;
   int fd;

   if (!(filename = vm_build_filename(vm,name))) {
      fprintf(stderr,"vm_mmap_create_file: unable to create filename (%s)\n",
              name);
      return(-1);
   }

   if ((fd = memzone_create_file(filename,len,ptr)) == -1)
      fprintf(stderr,"vm_mmap_create_file: unable to open file '%s' (%s)\n",
              filename,strerror(errno));

   free(filename);
   return(fd);
}

/* Close a memory mapped file */
int vm_mmap_close_file(int fd,u_char *ptr,size_t len)
{
   if (ptr != NULL)
      memzone_unmap(ptr,len);
   
   if (fd != -1)
      close(fd);
   
   return(0);
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
   free(vm->ios_startup_config);
   vm->ios_startup_config = NULL;

   free(vm->ios_private_config);
   vm->ios_private_config = NULL;
}

/* Set Cisco IOS configuration files to use (NULL to keep existing data) */
int vm_ios_set_config(vm_instance_t *vm,const char *startup_filename,const char *private_filename)
{
   char *startup_file = NULL;
   char *private_file = NULL;

   if (startup_filename) {
      startup_file = strdup(startup_filename);
      if (startup_file == NULL)
         goto err_memory;
   }

   if (private_filename) {
      private_file = strdup(private_filename);
      if (private_file == NULL)
         goto err_memory;
   }

   vm_ios_unset_config(vm);
   vm->ios_startup_config = startup_file;
   vm->ios_private_config = private_file;
   return(0);
err_memory:
   free(startup_file);
   free(private_file);
   return(-1);
}

/* Extract IOS configuration from NVRAM and write it to a file */
int vm_nvram_extract_config(vm_instance_t *vm,char *filename)
{
   u_char *cfg_buffer = NULL;
   size_t cfg_len;
   FILE *fd;

   if (!vm->platform->nvram_extract_config)
      return(-1);

   /* Extract the IOS configuration */
   if ((vm->platform->nvram_extract_config(vm,&cfg_buffer,&cfg_len,NULL,NULL)) || 
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

/* Read IOS configuraton from the files and push it to NVRAM (NULL to keep existing data) */
int vm_nvram_push_config(vm_instance_t *vm,const char *startup_filename,const char *private_filename)
{
   u_char *startup_config = NULL;
   u_char *private_config = NULL;
   size_t startup_len = 0;
   size_t private_len = 0;
   int res = -1;

   /* Read configuration */
   if (startup_filename) {
      if (m_read_file(startup_filename, &startup_config, &startup_len))
         goto cleanup;
   }

   if (private_filename) {
      if (m_read_file(private_filename, &private_config, &private_len))
         goto cleanup;
   }

   /* Push it! */
   res = vm->platform->nvram_push_config(vm, startup_config, startup_len, private_config, private_len);

cleanup:
   free(startup_config);
   free(private_config);
   return(res);
}

/* Save general VM configuration into the specified file */
void vm_save_config(vm_instance_t *vm,FILE *fd)
{
   fprintf(fd,"vm create %s %u %s\n",
           vm->name,vm->instance_id,vm->platform->name);

   if (vm->ios_image)
      fprintf(fd,"vm set_ios %s %s\n",vm->name,vm->ios_image);

   fprintf(fd,"vm set_ram %s %u\n",vm->name,vm->ram_size);
   fprintf(fd,"vm set_nvram %s %u\n",vm->name,vm->nvram_size);
   fprintf(fd,"vm set_ram_mmap %s %u\n",vm->name,vm->ram_mmap);
   fprintf(fd,"vm set_clock_divisor %s %u\n",vm->name,vm->clock_divisor);
   fprintf(fd,"vm set_conf_reg %s 0x%4.4x\n",vm->name,vm->conf_reg_setup);

   if (vm->vtty_con_type == VTTY_TYPE_TCP)
      fprintf(fd,"vm set_con_tcp_port %s %d\n",
              vm->name,vm->vtty_con_tcp_port);

   if (vm->vtty_aux_type == VTTY_TYPE_TCP)
      fprintf(fd,"vm set_aux_tcp_port %s %d\n",
              vm->name,vm->vtty_aux_tcp_port);

   /* Save slot config */
   vm_slot_save_all_config(vm,fd);
}

/* Find a platform */
vm_platform_t *vm_platform_find(char *name)
{
   struct vm_platform_list *p;

   for(p=vm_platforms;p;p=p->next)
      if (!strcmp(p->platform->name,name))
         return(p->platform);

   return NULL;
}

/* Find a platform given its CLI name */
vm_platform_t *vm_platform_find_cli_name(char *name)
{
   struct vm_platform_list *p;

   for(p=vm_platforms;p;p=p->next)
      if (!strcmp(p->platform->cli_name,name))
         return(p->platform);

   return NULL;
}

/* Destroy vm_platforms */
static void destroy_vm_platforms(void)
{
   struct vm_platform_list *p, *next;

   for (p = vm_platforms; p ;p = next) {
      next = p->next;
      free(p);
   }
   vm_platforms = NULL;
}

/* Register a platform */
int vm_platform_register(vm_platform_t *platform)
{
   struct vm_platform_list *p;

   if (vm_platform_find(platform->name) != NULL) {
      fprintf(stderr,"vm_platform_register: platform '%s' already exists.\n",
              platform->name);
      return(-1);
   }

   if (!(p = malloc(sizeof(*p)))) {
      fprintf(stderr,"vm_platform_register: unable to record platform.\n");
      return(-1);
   }

   if (!vm_platforms) {
      atexit(destroy_vm_platforms);
   }

   p->platform = platform;
   p->next = vm_platforms;
   vm_platforms = p;
   return(0);
}

/* Create an instance of the specified type */
vm_instance_t *vm_create_instance(char *name,int instance_id,char *type)
{
   vm_platform_t *platform;
   vm_instance_t *vm = NULL;

   if (!(platform = vm_platform_find(type))) {
      fprintf(stderr,"VM %s: unknown platform '%s'\n",name,type);
      goto error;
   }

   /* Create a generic VM instance */
   if (!(vm = vm_create(name,instance_id,platform)))
      goto error;

   /* Initialize specific parts */
   if (vm->platform->create_instance(vm) == -1)
      goto error;

   return vm;

 error:
   fprintf(stderr,"VM %s: unable to create instance!\n",name);
   vm_free(vm);
   return NULL;
}

/* Free resources used by a VM instance */
static int vm_reg_delete_instance(void *data,void *arg)
{
   vm_instance_t *vm = data;
   return(vm->platform->delete_instance(vm));
}

/* Delete a VM instance */
int vm_delete_instance(char *name)
{
   return(registry_delete_if_unused(name,OBJ_TYPE_VM,
                                    vm_reg_delete_instance,NULL));
}

/* Rename a VM instance */
int vm_rename_instance(vm_instance_t *vm, char *name)
{
   char *old_name;
   char *old_lock_file = NULL;
   FILE *old_lock_fd = NULL;
   glob_t globbuf;
   size_t i;
   char *pattern = NULL;
   char *filename;
   int do_rename = 0;

   if (name == NULL || vm == NULL)
      goto err_invalid; /* invalid argument */

   if (vm->status != VM_STATUS_HALTED)
      goto err_not_stopped; /* VM is not stopped */

   if (strcmp(vm->name, name) == 0)
      return(0); /* same name, done */

   if (registry_exists(name,OBJ_TYPE_VM))
      goto err_exists; /* name already exists */

   old_name = vm->name;
   vm->name = NULL;

   if(!(vm->name = strdup(name)))
      goto err_strdup; /* out of memory */

   /* get new lock */
   do_rename = ( vm_file_naming_type != 1 );
   if (do_rename) {
      old_lock_file = vm->lock_file;
      old_lock_fd = vm->lock_fd;
      vm->lock_file = NULL;
      vm->lock_fd = NULL;

      if (vm_get_lock(vm) == -1)
         goto err_lock;
   }

   if (registry_rename(old_name,vm->name,OBJ_TYPE_VM))
      goto err_registry; /* failed to rename */

   vm_log(vm,"VM","renamed from '%s' to '%s'",old_name,vm->name);

   /* rename files (best effort) */
   if (do_rename) {
      fclose(old_lock_fd);
      unlink(old_lock_file);
      free(old_lock_file);

      vm_close_log(vm);

      if ((pattern = dyn_sprintf("%s_%s_*",vm_get_type(vm),old_name)) == NULL)
         goto skip_rename;

      if (glob(pattern, GLOB_NOSORT, NULL, &globbuf) != 0)
         goto skip_rename;

      for (i = 0; i < globbuf.gl_pathc; i++) {
         if ((filename = dyn_sprintf("%s_%s_%s",vm_get_type(vm),vm->name,globbuf.gl_pathv[i] + strlen(pattern) - 1)) == NULL)
            break; /* out of memory */

         rename(globbuf.gl_pathv[i], filename);
         free(filename);
      }
      globfree(&globbuf);
 skip_rename:
      free(pattern);

      vm_reopen_log(vm);
   }

   free(old_name);
   return(0); // done

 err_registry:
 err_lock:
 err_strdup:
   free(vm->name);
   vm->name = old_name;

   if (do_rename) {
      vm_release_lock(vm,TRUE);
      vm->lock_file = old_lock_file;
      vm->lock_fd = old_lock_fd;
   }
 err_exists:
 err_not_stopped:
 err_invalid:
   return(-1);
}

/* Initialize a VM instance */
int vm_init_instance(vm_instance_t *vm)
{
   return(vm->platform->init_instance(vm));
}

/* Stop a VM instance */
int vm_stop_instance(vm_instance_t *vm)
{
   return(vm->platform->stop_instance(vm));
}

/* Delete all VM instances */
int vm_delete_all_instances(void)
{
   return(registry_delete_type(OBJ_TYPE_VM,vm_reg_delete_instance,NULL));
}

/* Save configurations of all VM instances */
static void vm_reg_save_config(registry_entry_t *entry,void *opt,int *err)
{
   vm_instance_t *vm = entry->data;
   FILE *fd = opt;
   
   vm_save_config(vm,fd);

   /* Save specific platform options */
   if (vm->platform->save_config != NULL)
      vm->platform->save_config(vm,fd);
}

/* Save all VM configs */
int vm_save_config_all(FILE *fd)
{   
   registry_foreach_type(OBJ_TYPE_VM,vm_reg_save_config,fd,NULL);
   return(0);
}

/* OIR to start a slot/subslot */
int vm_oir_start(vm_instance_t *vm,u_int slot,u_int subslot)
{
   if (vm->platform->oir_start != NULL)
      return(vm->platform->oir_start(vm,slot,subslot));

   /* OIR not supported */
   return(-1);
}

/* OIR to stop a slot/subslot */
int vm_oir_stop(vm_instance_t *vm,u_int slot,u_int subslot)
{
   if (vm->platform->oir_stop != NULL)
      return(vm->platform->oir_stop(vm,slot,subslot));

   /* OIR not supported */
   return(-1);
}

/* Set the JIT translation sharing group */
int vm_set_tsg(vm_instance_t *vm,int group)
{
   if (vm->status == VM_STATUS_RUNNING)
      return(-1);

   vm->tsg = group;
   return(0);
}



