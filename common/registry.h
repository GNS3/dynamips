/*
 * IPFlow Collector
 * Copyright (c) 2003 Christophe Fillot.
 * E-mail: cf@utc.fr
 * 
 * registry.h: Object Registry.
 */

#ifndef __REGISTRY_H__
#define __REGISTRY_H__  1

static const char rcsid_registry[] = "$Id$";

#include <sys/types.h>
#include <sys/time.h>
#include <pthread.h>

#include "mempool.h"

#define REGISTRY_HT_NAME_ENTRIES  1024
#define REGISTRY_MAX_TYPES  256

/* Object types for Registry */
enum {
   OBJ_TYPE_VM,          /* Virtual machine */
   OBJ_TYPE_NIO,         /* Network IO descriptor */
   OBJ_TYPE_NIO_BRIDGE,  /* Network IO bridge */
   OBJ_TYPE_FRSW,        /* Frame-Relay switch */
   OBJ_TYPE_ATMSW,       /* ATM switch */
   OBJ_TYPE_ATM_BRIDGE,  /* ATM bridge */
   OBJ_TYPE_ETHSW,       /* Ethernet switch */
   OBJ_TYPE_STORE,       /* Hypervisor store */
};

/* Registry entry */
typedef struct registry_entry registry_entry_t;
struct registry_entry {
   char *name;
   void *data;
   int object_type;
   int ref_count;
   registry_entry_t *hname_next,*hname_prev;
   registry_entry_t *htype_next,*htype_prev;
};

/* Registry info */
typedef struct registry registry_t;
struct registry {
   pthread_mutex_t lock;
   mempool_t mp;
   int ht_name_entries,ht_type_entries;
   registry_entry_t *ht_names;            /* Hash table for names */
   registry_entry_t *ht_types;            /* Hash table for types */
};

/* Registry "foreach" callback */
typedef void (*registry_foreach)(registry_entry_t *entry,void *opt_arg,
                                 int *err);

/* Registry "exec" callback */
typedef int (*registry_exec)(void *data,void *opt_arg);

/* Initialize registry */
int registry_init(void);

/* Remove a registry entry */
void registry_remove_entry(registry_entry_t *entry);

/* Add a new entry to the registry */
int registry_add(char *name,int object_type,void *data);

/* Delete an entry from the registry */
int registry_delete(char *name,int object_type);

/* Find an entry (increment reference count) */
void *registry_find(char *name,int object_type);

/* Check if entry exists (does not change reference count) */
void *registry_exists(char *name,int object_type);

/* Release a reference of an entry (decrement the reference count) */
int registry_unref(char *name,int object_type);

/* 
 * Execute action on an object if its reference count is less or equal to
 * the specified count.
 */
int registry_exec_refcount(char *name,int object_type,int max_ref,int reg_del,
                           registry_exec obj_action,void *opt_arg);

/* Delete object if unused */
int registry_delete_if_unused(char *name,int object_type,
                              registry_exec obj_destructor,
                              void *opt_arg);

/* Execute a callback function for all objects of specified type */
int registry_foreach_type(int object_type,registry_foreach cb,
                          void *opt,int *err);

/* Delete all objects of the specified type */
int registry_delete_type(int object_type,registry_exec cb,void *opt);

/* Dump the registry */
void registry_dump(void);

#endif
