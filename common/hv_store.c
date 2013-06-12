/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Hypervisor store.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "utils.h"
#include "base64.h"
#include "registry.h"
#include "hypervisor.h"

struct store_object {
   char *name;       /* Object name */
   void *data;       /* Data pointer */
   size_t len;       /* Data length */
};

/* Create an object of the given name */
static struct store_object *so_create(char *name,void *data,size_t len)
{
   struct store_object *so;

   if (!(so = malloc(sizeof(*so))))
      return NULL;

   if (!(so->name = strdup(name)))
      goto err_name;

   so->data = data;
   so->len  = len;

   if (registry_add(so->name,OBJ_TYPE_STORE,so) == -1)
      goto err_registry;

   return so;

 err_registry:
   free(so->name);
 err_name:
   free(so);
   return NULL;
}

/* Delete a store object */
static void so_delete(struct store_object *so)
{
   if (so != NULL) {
      free(so->name);
      free(so->data);
      free(so);
   }
}

/* Free resources used by a store object (registry callback) */
static int so_reg_delete(void *data,void *arg)
{
   so_delete((struct store_object *)data);
   return(TRUE);
}

/* Delete a store object from the registry */
static int so_delete_from_registry(char *name)
{
   return(registry_delete_if_unused(name,OBJ_TYPE_STORE,so_reg_delete,NULL));
}

/* Write an object (data provided in base64 encoding) */
static int cmd_write(hypervisor_conn_t *conn,int argc,char *argv[])
{   
   struct store_object *so;
   u_char *buffer;
   ssize_t len;

   /* Convert base64 input to standard text */
   if (!(buffer = malloc(3 * strlen(argv[1]))))
      goto err_alloc_base64;

   if ((len = base64_decode(buffer,(u_char *)argv[1],0)) < 0)
      goto err_decode_base64;

   if (!(so = so_create(argv[0],buffer,len))) {
      free(buffer);
      hypervisor_send_reply(conn,HSC_ERR_CREATE,1,"unable to store object");
      return(-1);
   }

   registry_unref(so->name,OBJ_TYPE_STORE);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);

 err_decode_base64:
   free(buffer);
 err_alloc_base64:
   hypervisor_send_reply(conn,HSC_ERR_CREATE,1,"unable to decode base64");
   return(-1);
}

/* Read an object and return data in base64 encoding */ 
static int cmd_read(hypervisor_conn_t *conn,int argc,char *argv[])
{
   struct store_object *so;
   u_char *buffer;

   if (!(so = hypervisor_find_object(conn,argv[0],OBJ_TYPE_STORE)))
      return(-1);

   /* 
    * Convert data to base64. base64 is about 1/3 larger than input,
    * let's be on the safe side with twice longer.
    */
   if (!(buffer = malloc(so->len * 2)))
      goto err_alloc_base64;

   base64_encode(buffer,so->data,so->len);

   registry_unref(so->name,OBJ_TYPE_STORE);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"%s",buffer);
   free(buffer);
   return(0);

 err_alloc_base64:
   registry_unref(so->name,OBJ_TYPE_STORE);

   hypervisor_send_reply(conn,HSC_ERR_CREATE,1,
                         "unable to encode data in base64",
                         argv[0]);
   return(-1);
}

/* Delete an object from the store */
static int cmd_delete(hypervisor_conn_t *conn,int argc,char *argv[])
{
   if (so_delete_from_registry(argv[0]) < 0) {
      hypervisor_send_reply(conn,HSC_ERR_DELETE,1,"unable to delete object %s",
                            argv[0]);
      return(-1);
   } else {
      hypervisor_send_reply(conn,HSC_INFO_OK,1,"object %s deleted",argv[0]);
      return(0);
   }
}

/* Delete all objects from the store */
static int cmd_delete_all(hypervisor_conn_t *conn,int argc,char *argv[])
{
   registry_delete_type(OBJ_TYPE_STORE,so_reg_delete,NULL);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"object store deleted");
   return(0);
}

/* Show info about a store object */
static void cmd_show_obj_list(registry_entry_t *entry,void *opt,int *err)
{
   hypervisor_conn_t *conn = opt;
   struct store_object *so = entry->data;

   hypervisor_send_reply(conn,HSC_INFO_MSG,0,"%s (len=%ld)",
                         entry->name,(long)so->len);
}

/* Object list */
static int cmd_obj_list(hypervisor_conn_t *conn,int argc,char *argv[])
{
   int err = 0;
   registry_foreach_type(OBJ_TYPE_STORE,cmd_show_obj_list,conn,&err);
   hypervisor_send_reply(conn,HSC_INFO_OK,1,"OK");
   return(0);
}

/* "store" commands */
static hypervisor_cmd_t store_cmd_array[] = {
   { "write", 2, 2, cmd_write, NULL },
   { "read", 1, 1, cmd_read, NULL },
   { "delete", 1, 1, cmd_delete, NULL },
   { "delete_all", 0, 0, cmd_delete_all, NULL },
   { "list", 0, 0, cmd_obj_list, NULL },
   { NULL, -1, -1, NULL, NULL },
};

/* Hypervisor store initialization */
int hypervisor_store_init(void)
{
   hypervisor_module_t *module;

   module = hypervisor_register_module("object_store",NULL);
   assert(module != NULL);

   hypervisor_register_cmd_array(module,store_cmd_array);
   return(0);
}
