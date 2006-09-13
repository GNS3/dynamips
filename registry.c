/*
 * IPFlow Collector
 * Copyright (c) 2003 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * registry.c: Object Registry.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>

#include "utils.h"
#include "hash.h"
#include "mempool.h"
#include "registry.h"

#define DEBUG_REGISTRY  0

static registry_t *registry = NULL;

#define REGISTRY_LOCK()    pthread_mutex_lock(&registry->lock)
#define REGISTRY_UNLOCK()  pthread_mutex_unlock(&registry->lock)

/* Initialize registry */
int registry_init(void)
{
   registry_entry_t *p;
   pthread_mutexattr_t attr;
   size_t len;
   int i;

   registry = malloc(sizeof(*registry));
   assert(registry != NULL);

   pthread_mutexattr_init(&attr);
   pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
   pthread_mutex_init(&registry->lock,&attr);

   /* initialize registry memory pool */
   mp_create_fixed_pool(&registry->mp,"registry");

   registry->ht_name_entries = REGISTRY_HT_NAME_ENTRIES;
   registry->ht_type_entries = REGISTRY_MAX_TYPES;

   /* initialize hash table for names, with sentinels */
   len = registry->ht_name_entries * sizeof(registry_entry_t);
   registry->ht_names = mp_alloc(&registry->mp,len);
   assert(registry->ht_names != NULL);

   for(i=0;i<registry->ht_name_entries;i++) {
      p = &registry->ht_names[i];
      p->hname_next = p->hname_prev = p;
   }

   /* initialize hash table for types, with sentinels */
   len = registry->ht_type_entries * sizeof(registry_entry_t);
   registry->ht_types = mp_alloc(&registry->mp,len);
   assert(registry->ht_types != NULL);

   for(i=0;i<registry->ht_type_entries;i++) {
      p = &registry->ht_types[i];
      p->htype_next = p->htype_prev = p;
   }

   return(0);
}

/* Insert a new entry */
static void registry_insert_entry(registry_entry_t *entry)
{      
   registry_entry_t *bucket;
   u_int h_index;

   /* insert new entry in hash table for names */
   h_index = str_hash(entry->name) % registry->ht_name_entries;
   bucket = &registry->ht_names[h_index];

   entry->hname_next = bucket->hname_next;
   entry->hname_prev = bucket;
   bucket->hname_next->hname_prev = entry;
   bucket->hname_next = entry;

   /* insert new entry in hash table for object types */
   bucket = &registry->ht_types[entry->object_type];

   entry->htype_next = bucket->htype_next;
   entry->htype_prev = bucket;
   bucket->htype_next->htype_prev = entry;
   bucket->htype_next = entry;
}

/* Remove a registry entry */
void registry_remove_entry(registry_entry_t *entry)
{
   entry->hname_prev->hname_next = entry->hname_next;
   entry->hname_next->hname_prev = entry->hname_prev;

   entry->htype_prev->htype_next = entry->htype_next;
   entry->htype_next->htype_prev = entry->htype_prev;
   
   mp_free(entry);
}

/* Locate an entry */
static inline registry_entry_t *registry_find_entry(char *name,int object_type)
{
   registry_entry_t *entry,*bucket;
   u_int h_index;

   h_index = str_hash(name) % registry->ht_name_entries;
   bucket = &registry->ht_names[h_index];

   for(entry=bucket->hname_next;entry!=bucket;entry=entry->hname_next)
      if (!strcmp(entry->name,name) && (entry->object_type == object_type))
         return entry;

   return NULL;
}

/* Add a new entry to the registry */
int registry_add(char *name,int object_type,void *data)
{
   registry_entry_t *entry;

   if (!name) 
      return(-1);

   REGISTRY_LOCK();

   /* check if we have already a reference for this name */
   if ((entry = registry_find_entry(name,object_type))) {
      REGISTRY_UNLOCK();
      return(-1);
   }

   /* create a new entry */
   if (!(entry = mp_alloc(&registry->mp,sizeof(*entry)))) {
      REGISTRY_UNLOCK();
      return(-1);
   }

   entry->name = name;
   entry->data = data;
   entry->object_type = object_type;
   entry->ref_count = 1;   /* consider object is referenced by the caller */
   registry_insert_entry(entry);

   REGISTRY_UNLOCK();
   return(0);
}

/* Delete an entry from the registry */
int registry_delete(char *name,int object_type)
{
   registry_entry_t *entry;

   if (!name) return(-1);

   REGISTRY_LOCK();

   if (!(entry = registry_find_entry(name,object_type))) {
      REGISTRY_UNLOCK();
      return(-1);
   }

   /* if the entry is referenced, just decrement ref counter */
   if (--entry->ref_count > 0) {
      REGISTRY_UNLOCK();
      return(0);
   }

   registry_remove_entry(entry);
   REGISTRY_UNLOCK();
   return(0);
}

/* Find an entry (increment the reference count) */
void *registry_find(char *name,int object_type)
{
   registry_entry_t *entry;
   void *data;

   if (!name) return NULL;

   REGISTRY_LOCK();

   if ((entry = registry_find_entry(name,object_type))) {
      entry->ref_count++;
      data = entry->data;
   } else
      data = NULL;

   REGISTRY_UNLOCK();
   return data;
}

/* Check if entry exists (does not change reference count) */
void *registry_exists(char *name,int object_type)
{
   registry_entry_t *entry;
   void *data = NULL;
   
   if (!name) 
      return NULL;

   REGISTRY_LOCK();
   entry = registry_find_entry(name,object_type);
   if (entry) 
      data = entry->data;
   REGISTRY_UNLOCK();
   return data;
}

/* Release a reference of an entry (decrement the reference count) */
int registry_unref(char *name,int object_type)
{
   registry_entry_t *entry;
   int res = -1;

   if (!name) return(-1);

   REGISTRY_LOCK();

   if ((entry = registry_find_entry(name,object_type)))
   {
      entry->ref_count--;

#if DEBUG_REGISTRY
      printf("Registry: object %s: ref_count = %d after unref.\n",
             name, entry->ref_count);
#endif

      if (entry->ref_count < 0) {
         fprintf(stderr,"Registry: object %s (type %d): negative ref_count.\n",
                 name, object_type);
      } else
         res = 0;
   }

   REGISTRY_UNLOCK();
   return(res);
}

/* 
 * Execute action on an object if its reference count is less or equal to
 * the specified count.
 */
int registry_exec_refcount(char *name,int object_type,int max_ref,int reg_del,
                           registry_exec obj_action,void *opt_arg)
{
   registry_entry_t *entry;
   int res = -1;
   int status;

   if (!name) return(-1);

   REGISTRY_LOCK();

   entry = registry_find_entry(name,object_type);

   if (entry) 
   {
      if (entry->ref_count <= max_ref)
      {
         status = TRUE;

         if (obj_action != NULL)
            status = obj_action(entry->data,opt_arg);

         if (reg_del && status)
            registry_remove_entry(entry);

         res = 1;
      } else
         res = 0;
   }

   REGISTRY_UNLOCK();
   return(res);
}

/* Delete object if unused */
int registry_delete_if_unused(char *name,int object_type,
                              registry_exec obj_destructor,void *opt_arg)
{
   return(registry_exec_refcount(name,object_type,0,TRUE,
                                 obj_destructor,opt_arg));
}

/* Execute a callback function for all objects of specified type */
int registry_foreach_type(int object_type,registry_foreach cb,
                          void *opt,int *err)
{
   registry_entry_t *p,*bucket,*next;
   int count = 0;

   REGISTRY_LOCK();

   bucket = &registry->ht_types[object_type];

   for(p=bucket->htype_next;p!=bucket;p=next) {
      next = p->htype_next;
      if (cb) cb(p,opt,err);
      count++;
   }

   REGISTRY_UNLOCK();
   return(count);
}

/* Delete all objects of the specified type */
int registry_delete_type(int object_type,registry_exec cb,void *opt)
{
   registry_entry_t *p,*bucket,*next;
   int count = 0;
   int status;

   REGISTRY_LOCK();

   bucket = &registry->ht_types[object_type];

   for(p=bucket->htype_next;p!=bucket;p=next) {
      next = p->htype_next;

      if (p->ref_count == 0) {
         status = TRUE;
         
         if (cb != NULL) 
            status = cb(p->data,opt);

         if (status) {
            registry_remove_entry(p);
            count++;
         }
      } else {
         fprintf(stderr,"registry_delete_type: object \"%s\" (type %d) still "
                 "referenced (count=%d)\n",p->name,object_type,p->ref_count);
      }
   }

   REGISTRY_UNLOCK();
   return(count);
}

/* Dump the registry */
void registry_dump(void)
{
   registry_entry_t *p,*bucket;
   int i;

   REGISTRY_LOCK();

   printf("Registry dump:\n");

   printf("  Objects (from name hash table):\n");

   /* dump hash table of names */
   for(i=0;i<registry->ht_name_entries;i++)
   {
      bucket = &registry->ht_names[i];

      for(p=bucket->hname_next;p!=bucket;p=p->hname_next)
         printf("     %s (type %d, ref_count=%d)\n",
                p->name,p->object_type,p->ref_count);
   }

   printf("\n  Objects classed by types:\n");

   /* dump hash table of types */
   for(i=0;i<registry->ht_type_entries;i++)
   {         
      printf("     Type %d: ",i);

      bucket = &registry->ht_types[i];
      for(p=bucket->htype_next;p!=bucket;p=p->htype_next)
         printf("%s(%d) ",p->name,p->ref_count);
         
      printf("\n");
   }

   REGISTRY_UNLOCK();
}
