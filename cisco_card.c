/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco card routines and definitions.
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
#include "cisco_card.h"

/* Get cisco card type description */
char *cisco_card_get_type_desc(int dev_type)
{
   switch(dev_type) {
      case CISCO_CARD_TYPE_PA:
         return("Port Adapter (PA)");
      case CISCO_CARD_TYPE_NM:
         return("Network Module (NM)");
      case CISCO_CARD_TYPE_WIC:
         return("WAN Interface Card (WIC)");
      default:
         return("Unknown");
   }
}

/* Set EEPROM definition for the specified Cisco card */
int cisco_card_set_eeprom(vm_instance_t *vm,struct cisco_card *card,
                          const struct cisco_eeprom *eeprom)
{
   if (!eeprom)
      return(0);

   if (cisco_eeprom_copy(&card->eeprom,eeprom) == -1) {
      vm_error(vm,"cisco_card_set_eeprom: no memory (eeprom=%p).\n",eeprom);
      return(-1);
   }
   
   return(0);
}

/* Unset EEPROM definition */
int cisco_card_unset_eeprom(struct cisco_card *card)
{
   cisco_eeprom_free(&card->eeprom);
   return(0);
}

/* Check if a card has a valid EEPROM defined */
int cisco_card_check_eeprom(struct cisco_card *card)
{
   return(cisco_eeprom_valid(&card->eeprom));
}

/* Create a card structure */
static inline struct cisco_card *cisco_card_create(u_int card_type)
{
   struct cisco_card *card;

   if ((card = malloc(sizeof(*card))) != NULL) {
      memset(card,0,sizeof(*card));
      card->card_type = card_type;
   }
   
   return card;
}

/* Find a NIO binding */
static struct cisco_nio_binding *
cisco_card_find_nio_binding(struct cisco_card *card,u_int port_id)
{   
   struct cisco_nio_binding *nb;

   if (!card)
      return NULL;

   for(nb=card->nio_list;nb;nb=nb->next)
      if (nb->port_id == port_id)
         return nb;

   return NULL;
}

/* Remove all NIO bindings */
static void 
cisco_card_remove_all_nio_bindings(vm_instance_t *vm,struct cisco_card *card)
{
   struct cisco_nio_binding *nb,*next;

   for(nb=card->nio_list;nb;nb=next) {
      next = nb->next;

      /* tell the slot driver to stop using this NIO */
      if (card->driver)
         card->driver->card_unset_nio(vm,card,nb->port_id);

      /* unreference NIO object */
      netio_release(nb->nio->name);
      free(nb);
   }

   card->nio_list = NULL;
}

/* Enable all NIO for the specified card */
static inline
void cisco_card_enable_all_nio(vm_instance_t *vm,struct cisco_card *card)
{
   struct cisco_nio_binding *nb;

   if (card && card->driver && card->drv_info)      
      for(nb=card->nio_list;nb;nb=nb->next)
         card->driver->card_set_nio(vm,card,nb->port_id,nb->nio);
}

/* Disable all NIO for the specified card */
static inline
void cisco_card_disable_all_nio(vm_instance_t *vm,struct cisco_card *card)
{
   struct cisco_nio_binding *nb;

   if (card && card->driver && card->drv_info)
      for(nb=card->nio_list;nb;nb=nb->next)
         card->driver->card_unset_nio(vm,card,nb->port_id);
}

/* Initialize a card */
static inline
int cisco_card_init(vm_instance_t *vm,struct cisco_card *card,u_int id)
{  
   size_t len;

   /* Check that a device type is defined for this card */
   if (!card || !card->dev_type || !card->driver)
      return(-1);

   /* Allocate device name */
   len = strlen(card->dev_type) + 10;
   if (!(card->dev_name = malloc(len))) {
      vm_error(vm,"unable to allocate device name.\n");
      return(-1);
   }

   snprintf(card->dev_name,len,"%s(%u)",card->dev_type,id);

   /* Initialize card driver */
   if (card->driver->card_init(vm,card) == -1) {
      vm_error(vm,"unable to initialize card type '%s' (id %u)\n",
               card->dev_type,id);
      return(-1);
   }

   return(0);
}

/* Shutdown card */
static int cisco_card_shutdown(vm_instance_t *vm,struct cisco_card *card)
{
   /* Check that a device type is defined for this card */
   if (!card || !card->dev_type || !card->driver)
      return(-1);

   /* Shutdown the NM driver */
   if (card->drv_info && (card->driver->card_shutdown(vm,card) == -1)) {
      vm_error(vm,"unable to shutdown card type '%s' (slot %u/%u)\n",
               card->dev_type,card->slot_id,card->subslot_id);
      return(-1);
   }

   free(card->dev_name);
   card->dev_name = NULL;
   card->drv_info = NULL;
   return(0);
}

/* Show info for the specified card */
static int cisco_card_show_info(vm_instance_t *vm,struct cisco_card *card)
{
   /* Check that a device type is defined for this card */
   if (!card || !card->driver || !card->driver->card_show_info)
      return(-1);

   card->driver->card_show_info(vm,card);
   return(0);
}

/* Save config for the specified card */
static int cisco_card_save_config(vm_instance_t *vm,struct cisco_card *card,
                                  FILE *fd)
{
   struct cisco_nio_binding *nb;

   fprintf(fd,"vm add_slot_binding %s %u %u %s\n",
           vm->name,card->slot_id,card->subslot_id,card->dev_type);

   for(nb=card->nio_list;nb;nb=nb->next) {
      fprintf(fd,"vm add_nio_binding %s %u %u %s\n",
              vm->name,card->slot_id,nb->orig_port_id,nb->nio->name);
   }

   return(0);
}

/* Find a driver in a driver array */
static struct cisco_card_driver *
cisco_card_find_driver(struct cisco_card_driver **array,char *dev_type)
{
   int i;

   for(i=0;array[i]!=NULL;i++)
      if (!strcmp(array[i]->dev_type,dev_type))
         return array[i];

   return NULL;
}

/* ======================================================================== */
/* High level routines for managing VM slots.                               */
/* ======================================================================== */

/* Get slot info */
struct cisco_card *vm_slot_get_card_ptr(vm_instance_t *vm,u_int slot_id)
{
   if (slot_id >= vm->nr_slots)
      return NULL;

   return(vm->slots[slot_id]);
}

/* Get info for a slot/port (with sub-cards) */
static int vm_slot_get_info(vm_instance_t *vm,u_int slot_id,u_int port_id,
                            struct cisco_card ***rc,u_int *real_port_id)
{
   struct cisco_card *card;
   u_int wic_id,card_type;

   if (slot_id >= VM_MAX_SLOTS) {
      *rc = NULL;
      return(-1);
   }

   *rc = &vm->slots[slot_id];
   card = vm->slots[slot_id];

   card_type = (card != NULL) ? card->card_type : CISCO_CARD_TYPE_UNDEF;

   switch(card_type) {
      /* 
       * Handle WICs which are sub-slots for Network Modules (NM).
       * Numbering: wic #0 => port_id = 0x10
       *            wic #1 => port_id = 0x20
       */
      case CISCO_CARD_TYPE_NM:
         wic_id = port_id >> 4;

         if (wic_id >= (CISCO_CARD_MAX_WIC+1)) {
            vm_error(vm,"Invalid wic_id %u (slot %u)\n",wic_id,slot_id);
            return(-1);
         }

         if (wic_id >= 0x01) {
            /* wic card */
            *rc = &card->sub_slots[wic_id - 1];
            *real_port_id = port_id & 0x0F;
         } else {
            /* main card */
            *real_port_id = port_id;
         }
         return(0);

      /* No translation for Cisco 7200 Port Adapters and WICs */
      case CISCO_CARD_TYPE_PA:
      case CISCO_CARD_TYPE_WIC:
         *real_port_id = port_id;
         return(0);

      /* Not initialized yet */
      default:
         *real_port_id = port_id;
         return(0);
   }
}

/* Translate a port ID (for sub-cards) */
static u_int 
vm_slot_translate_port_id(vm_instance_t *vm,u_int slot_id,u_int port_id,
                          struct cisco_card **rc)
{
   struct cisco_card **tmp;
   u_int real_port_id = 0;

   vm_slot_get_info(vm,slot_id,port_id,&tmp,&real_port_id);
   *rc = *tmp;
   return(real_port_id);
}

/* Check if a slot has an active card */
int vm_slot_active(vm_instance_t *vm,u_int slot_id,u_int port_id)
{
   struct cisco_card **rc;
   u_int real_port_id;

   if (vm_slot_get_info(vm,slot_id,port_id,&rc,&real_port_id) == -1)
      return(FALSE);

   if ((*rc == NULL) || ((*rc)->dev_type == NULL))
      return(FALSE);

   return(TRUE);
}

/* Set a flag for a card */
int vm_slot_set_flag(vm_instance_t *vm,u_int slot_id,u_int port_id,u_int flag)
{
   struct cisco_card **rc;
   u_int real_port_id;

   if (vm_slot_get_info(vm,slot_id,port_id,&rc,&real_port_id) == -1)
      return(FALSE);

   if (*rc == NULL)
      return(FALSE);

   (*rc)->card_flags |= flag;
   return(TRUE);
}

/* Add a slot binding */
int vm_slot_add_binding(vm_instance_t *vm,char *dev_type,
                        u_int slot_id,u_int port_id)
{     
   struct cisco_card_driver *driver,**drv_array;
   struct cisco_card **rc,*card,*nc,*parent;
   u_int real_port_id,card_type,card_id;

   if (vm_slot_get_info(vm,slot_id,port_id,&rc,&real_port_id) == -1)
      return(-1);

   /* check that this bay is empty */
   if (*rc != NULL) {
      if ((*rc)->card_flags & CISCO_CARD_FLAG_OVERRIDE) {
         vm_slot_remove_binding(vm,slot_id,port_id);
      } else {
         vm_error(vm,"a card already exists in slot %u/%u (%s)\n",
                  slot_id,port_id,(*rc)->dev_type);
         return(-1);
      }
   }

   card = vm->slots[slot_id];

   if (!card || (card == *rc)) {
      /* Main slot */
      drv_array = vm->slots_drivers;
      card_type = vm->slots_type;
      card_id   = slot_id;
      parent    = NULL;
   } else {
      /* Subslot */
      if (!card->driver->card_get_sub_info) {
         vm_error(vm,"no sub-slot possible for slot %u/%u.\n",slot_id,port_id);
         return(-1);
      }

      if (card->driver->card_get_sub_info(vm,card,port_id,
                                          &drv_array,&card_type) == -1)
      {
         vm_error(vm,"no sub-slot info for slot %u/%u.\n",slot_id,port_id);
         return(-1);
      }

      card_id = port_id;
      parent  = card;
   }

   assert(drv_array != NULL);

   /* Find the card driver */
   if (!(driver = cisco_card_find_driver(drv_array,dev_type))) {
      vm_error(vm,"unknown card type '%s' for slot %u/%u.\n",
               dev_type,slot_id,port_id);
      return(-1);
   }

   /* Allocate new card info */
   if (!(nc = cisco_card_create(card_type)))
      return(-1);

   nc->slot_id    = slot_id;
   nc->subslot_id = port_id;
   nc->card_id    = card_id;
   nc->dev_type   = driver->dev_type;
   nc->driver     = driver;
   nc->parent     = parent;
   *rc = nc;
   return(0);  
}

/* Remove a slot binding */
int vm_slot_remove_binding(vm_instance_t *vm,u_int slot_id,u_int port_id)
{
   struct cisco_card **rc,*sc;
   u_int i,real_port_id;

   if (vm_slot_get_info(vm,slot_id,port_id,&rc,&real_port_id) == -1)
      return(-1);

   if (*rc == NULL)
      return(-1);

   if ((*rc)->drv_info != NULL) {
      vm_error(vm,"slot %u/%u is still active\n",slot_id,port_id);
      return(-1);
   }

   for(i=0;i<CISCO_CARD_MAX_SUBSLOTS;i++) {
      if ((sc = (*rc)->sub_slots[i]) != NULL) {
         vm_error(vm,"sub-slot %u/%u is still active\n",
                  slot_id,sc->subslot_id);
         return(-1);
      }
   }

   /* Remove all NIOs bindings */ 
   vm_slot_remove_all_nio_bindings(vm,slot_id);

   /* Free the card info structure */
   free(*rc);
   *rc = NULL;
   return(0);
}

/* Add a network IO binding */
int vm_slot_add_nio_binding(vm_instance_t *vm,u_int slot_id,u_int port_id,
                            char *nio_name)
{
   struct cisco_nio_binding *nb;
   struct cisco_card *card,*rc;
   u_int real_port_id;
   netio_desc_t *nio;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(-1);

   /* Get the real card (in case this is a sub-slot) */
   real_port_id = vm_slot_translate_port_id(vm,slot_id,port_id,&rc);

   if (rc == NULL)
      return(-1);

   /* check that a NIO is not already bound to this port */
   if (cisco_card_find_nio_binding(rc,real_port_id) != NULL) {
      vm_error(vm,"a NIO already exists for interface %u/%u.\n",
               slot_id,port_id);
      return(-1);
   }

   /* acquire a reference on the NIO object */
   if (!(nio = netio_acquire(nio_name))) {
      vm_error(vm,"unable to find NIO '%s'.\n",nio_name);
      return(-1);
   }

   /* create a new binding */
   if (!(nb = malloc(sizeof(*nb)))) {
      vm_error(vm,"unable to create NIO binding for interface %u/%u.\n",
               slot_id,port_id);
      netio_release(nio_name);
      return(-1);
   }

   memset(nb,0,sizeof(*nb));
   nb->nio          = nio;
   nb->port_id      = real_port_id;
   nb->orig_port_id = port_id;

   nb->next = rc->nio_list;
   if (nb->next) nb->next->prev = nb;
   rc->nio_list = nb;
   return(0);
}

/* Remove a NIO binding */
int vm_slot_remove_nio_binding(vm_instance_t *vm,u_int slot_id,u_int port_id)
{
   struct cisco_nio_binding *nb;
   struct cisco_card *card,*rc;
   u_int real_port_id;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(-1);

   /* Get the real card (in case this is a sub-slot) */
   real_port_id = vm_slot_translate_port_id(vm,slot_id,port_id,&rc);

   if (rc == NULL)
      return(-1);

   /* no nio binding for this slot/port ? */
   if (!(nb = cisco_card_find_nio_binding(rc,real_port_id)))
      return(-1);

   /* tell the NM driver to stop using this NIO */
   if (rc->driver)
      rc->driver->card_unset_nio(vm,rc,port_id);

   /* remove this entry from the double linked list */
   if (nb->next)
      nb->next->prev = nb->prev;

   if (nb->prev) {
      nb->prev->next = nb->next;
   } else {
      rc->nio_list = nb->next;
   }

   /* unreference NIO object */
   netio_release(nb->nio->name);
   free(nb);
   return(0);
}

/* Remove all NIO bindings for the specified slot (sub-slots included) */
int vm_slot_remove_all_nio_bindings(vm_instance_t *vm,u_int slot_id)
{
   struct cisco_card *card,*sc;
   int i;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(-1);

   /* Remove NIO bindings for the main slot */
   cisco_card_remove_all_nio_bindings(vm,card);

   /* Remove NIO bindings for all sub-slots */
   for(i=0;i<CISCO_CARD_MAX_SUBSLOTS;i++) {
      if ((sc = card->sub_slots[i]) != NULL)
         cisco_card_remove_all_nio_bindings(vm,sc);
   }

   return(0);
}

/* Enable a Network IO descriptor for the specified slot */
int vm_slot_enable_nio(vm_instance_t *vm,u_int slot_id,u_int port_id)
{
   struct cisco_nio_binding *nb;
   struct cisco_card *card,*rc;
   u_int real_port_id;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(-1);

   /* Get the real card (in case this is a sub-slot) */
   real_port_id = vm_slot_translate_port_id(vm,slot_id,port_id,&rc);

   if (rc == NULL)
      return(-1);

   /* no nio binding for this slot/port ? */
   if (!(nb = cisco_card_find_nio_binding(rc,real_port_id)))
      return(-1);

   /* check that the driver is defined and successfully initialized */
   if (!rc->driver || !rc->drv_info)
      return(-1);

   return(rc->driver->card_set_nio(vm,rc,port_id,nb->nio));
}

/* Disable Network IO descriptor for the specified slot */
int vm_slot_disable_nio(vm_instance_t *vm,u_int slot_id,u_int port_id)
{
   struct cisco_nio_binding *nb;
   struct cisco_card *card,*rc;
   u_int real_port_id;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(-1);

   /* Get the real card (in case this is a sub-slot) */
   real_port_id = vm_slot_translate_port_id(vm,slot_id,port_id,&rc);

   if (rc == NULL)
      return(-1);

   /* no nio binding for this slot/port ? */
   if (!(nb = cisco_card_find_nio_binding(rc,real_port_id)))
      return(-1);

   /* check that the driver is defined and successfully initialized */
   if (!rc->driver || !rc->drv_info)
      return(-1);

   return(rc->driver->card_unset_nio(vm,rc,port_id));
}

/* Enable all NIO for the specified slot (sub-slots included) */
int vm_slot_enable_all_nio(vm_instance_t *vm,u_int slot_id)
{
   struct cisco_card *card;
   int i;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(-1);

   /* Enable slot NIOs */
   cisco_card_enable_all_nio(vm,card);

   /* Enable NIO of sub-slots */
   for(i=0;i<CISCO_CARD_MAX_SUBSLOTS;i++)
      cisco_card_enable_all_nio(vm,card->sub_slots[i]);

   return(0);
}

/* Disable all NIO for the specified slot (sub-slots included) */
int vm_slot_disable_all_nio(vm_instance_t *vm,u_int slot_id)
{
   struct cisco_card *card;
   int i;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(-1);

   /* Disable slot NIOs */
   cisco_card_disable_all_nio(vm,card);

   /* Disable NIO of sub-slots */
   for(i=0;i<CISCO_CARD_MAX_SUBSLOTS;i++)
      cisco_card_disable_all_nio(vm,card->sub_slots[i]);

   return(0);
}

/* Initialize the specified slot (sub-slots included) */
int vm_slot_init(vm_instance_t *vm,u_int slot_id)
{
   struct cisco_card *card;
   int i;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(0);

   /* Initialize card main module */
   cisco_card_init(vm,card,slot_id);

   /* Initialize sub-slots */
   for(i=0;i<CISCO_CARD_MAX_SUBSLOTS;i++)
      cisco_card_init(vm,card->sub_slots[i],slot_id);

   /* Enable all NIO */
   vm_slot_enable_all_nio(vm,slot_id);
   return(0);
}

/* Initialize all slots of a VM */
int vm_slot_init_all(vm_instance_t *vm)
{
   int i;

   for(i=0;i<vm->nr_slots;i++) {
      if (vm_slot_init(vm,i) == -1) {
         vm_error(vm,"unable to initialize slot %u\n",i);
         return(-1);
      }
   }

   return(0);
}

/* Shutdown the specified slot (sub-slots included) */
int vm_slot_shutdown(vm_instance_t *vm,u_int slot_id)
{
   struct cisco_card *card;
   int i;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(-1);

   /* Disable all NIO */
   vm_slot_disable_all_nio(vm,slot_id);

   /* Shutdown sub-slots */
   for(i=0;i<CISCO_CARD_MAX_SUBSLOTS;i++)
      cisco_card_shutdown(vm,card->sub_slots[i]);

   /* Shutdown card main module */
   cisco_card_shutdown(vm,card);
   return(0);
}

/* Shutdown all slots of a VM */
int vm_slot_shutdown_all(vm_instance_t *vm)
{
   int i;

   for(i=0;i<vm->nr_slots;i++)
      vm_slot_shutdown(vm,i);

   return(0);
}

/* Show info about the specified slot (sub-slots included) */
int vm_slot_show_info(vm_instance_t *vm,u_int slot_id)
{   
   struct cisco_card *card;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(-1);

   cisco_card_show_info(vm,card);
   return(0);
}

/* Show info about all slots */
int vm_slot_show_all_info(vm_instance_t *vm)
{
   int i;

   for(i=0;i<vm->nr_slots;i++)
      vm_slot_show_info(vm,i);

   return(0);
}

/* Check if the specified slot has a valid EEPROM defined */
int vm_slot_check_eeprom(vm_instance_t *vm,u_int slot_id,u_int port_id)
{
   struct cisco_card *card,*rc;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(FALSE);

   /* Get the real card (in case this is a sub-slot) */
   vm_slot_translate_port_id(vm,slot_id,port_id,&rc);
   
   if (rc == NULL)
      return(FALSE);

   return(cisco_card_check_eeprom(rc));
}

/* Returns the EEPROM data of the specified slot */
struct cisco_eeprom *
vm_slot_get_eeprom(vm_instance_t *vm,u_int slot_id,u_int port_id)
{
   struct cisco_card *card,*rc;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return NULL;

   /* Get the real card (in case this is a sub-slot) */
   vm_slot_translate_port_id(vm,slot_id,port_id,&rc);

   if (rc == NULL)
      return NULL;

   return(&rc->eeprom);
}

/* Save config for the specified slot (sub-slots included) */
int vm_slot_save_config(vm_instance_t *vm,u_int slot_id,FILE *fd)
{
   struct cisco_card *card;
   int i;

   if (!(card = vm_slot_get_card_ptr(vm,slot_id)))
      return(-1);

   /* Main slot info */
   cisco_card_save_config(vm,card,fd);

   /* Shutdown sub-slots */
   for(i=0;i<CISCO_CARD_MAX_SUBSLOTS;i++)
      cisco_card_save_config(vm,card->sub_slots[i],fd);

   return(0);
}

/* Save config for all slots */
int vm_slot_save_all_config(vm_instance_t *vm,FILE *fd)
{
   int i;

   for(i=0;i<vm->nr_slots;i++)
      vm_slot_save_config(vm,i,fd);

   return(0);
}

/* Show slot drivers */
int vm_slot_show_drivers(vm_instance_t *vm)
{
   char *slot_type;
   int i;

   if (!vm->slots_drivers)
      return(-1);

   slot_type = cisco_card_get_type_desc(vm->slots_type);

   printf("Available %s %s drivers:\n",vm->platform->log_name,slot_type);

   for(i=0;vm->slots_drivers[i];i++) {
      printf("  * %s %s\n",
             vm->slots_drivers[i]->dev_type,
             !vm->slots_drivers[i]->supported ? "(NOT WORKING)" : "");
   }
   
   printf("\n");
   return(0);
}

/* Maximum number of tokens in a slot description */
#define SLOT_DESC_MAX_TOKENS  8

/* Create a Network Module (command line) */
int vm_slot_cmd_create(vm_instance_t *vm,char *str)
{
   char *tokens[SLOT_DESC_MAX_TOKENS];
   int i,count,res;
   u_int slot_id,port_id;

   /* A port adapter description is like "1:0:NM-1FE" */
   count = m_strsplit(str,':',tokens,SLOT_DESC_MAX_TOKENS);

   if ((count < 2) || (count > 3)) {
      vm_error(vm,"unable to parse slot description '%s'.\n",str);
      return(-1);
   }

   /* Parse the slot id */
   slot_id = atoi(tokens[0]);

   /* Parse the sub-slot id */
   if (count == 3)
      port_id = atoi(tokens[1]);
   else
      port_id = 0;

   /* Add this new slot to the current slot list */
   res = vm_slot_add_binding(vm,tokens[count-1],slot_id,port_id);

   /* The complete array was cleaned by strsplit */
   for(i=0;i<SLOT_DESC_MAX_TOKENS;i++)
      free(tokens[i]);

   return(res);
}

/* Add a Network IO descriptor binding (command line) */
int vm_slot_cmd_add_nio(vm_instance_t *vm,char *str)
{
   char *tokens[SLOT_DESC_MAX_TOKENS];
   int i,count,nio_type,res=-1;
   u_int slot_id,port_id;
   netio_desc_t *nio;
   char nio_name[128];

   /* A NIO binding description is like "1:3:tap:tap0" */
   if ((count = m_strsplit(str,':',tokens,SLOT_DESC_MAX_TOKENS)) < 3) {
      vm_error(vm,"unable to parse NIO description '%s'.\n",str);
      return(-1);
   }

   /* Parse the slot id */
   slot_id = atoi(tokens[0]);

   /* Parse the port id */
   port_id = atoi(tokens[1]);

   /* Autogenerate a NIO name */
   snprintf(nio_name,sizeof(nio_name),"%s-i%u/%u/%u",
            vm_get_type(vm),vm->instance_id,slot_id,port_id);

   /* Create the Network IO descriptor */
   nio = NULL;
   nio_type = netio_get_type(tokens[2]);

   switch(nio_type) {
      case NETIO_TYPE_UNIX:
         if (count != 5) {
            vm_error(vm,"invalid number of arguments for UNIX NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_unix(nio_name,tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_VDE:
         if (count != 5) {
            vm_error(vm,"invalid number of arguments for VDE NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_vde(nio_name,tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TAP:
         if (count != 4) {
            vm_error(vm,"invalid number of arguments for TAP NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_tap(nio_name,tokens[3]);
         break;

      case NETIO_TYPE_UDP:
         if (count != 6) {
            vm_error(vm,"invalid number of arguments for UDP NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_udp(nio_name,atoi(tokens[3]),
                                     tokens[4],atoi(tokens[5]));
         break;

      case NETIO_TYPE_TCP_CLI:
         if (count != 5) {
            vm_error(vm,"invalid number of arguments for TCP CLI NIO '%s'\n",
                     str);
            goto done;
         }

         nio = netio_desc_create_tcp_cli(nio_name,tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TCP_SER:
         if (count != 4) {
            vm_error(vm,"invalid number of arguments for TCP SER NIO '%s'\n",
                     str);
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
            vm_error(vm,"invalid number of arguments for Linux Eth NIO '%s'\n",
                     str);
            goto done;
         }
         
         nio = netio_desc_create_lnxeth(nio_name,tokens[3]);
         break;
#endif

#ifdef GEN_ETH
      case NETIO_TYPE_GEN_ETH:
         if (count != 4) {
            vm_error(vm,
                     "invalid number of arguments for Generic Eth NIO '%s'\n",
                     str);
            goto done;
         }
         
         nio = netio_desc_create_geneth(nio_name,tokens[3]);
         break;
#endif

      default:
         vm_error(vm,"unknown NETIO type '%s'\n",tokens[2]);
         goto done;
   }

   if (!nio) {
      vm_error(vm,"unable to create NETIO descriptor for slot %u\n",slot_id);
      goto done;
   }

   if (vm_slot_add_nio_binding(vm,slot_id,port_id,nio_name) == -1) {
      vm_error(vm,"unable to add NETIO binding for slot %u\n",slot_id);
      netio_release(nio_name);
      netio_delete(nio_name);
      goto done;
   }
   
   netio_release(nio_name);
   res = 0;

 done:
   /* The complete array was cleaned by strsplit */
   for(i=0;i<SLOT_DESC_MAX_TOKENS;i++)
      free(tokens[i]);

   return(res);
}
