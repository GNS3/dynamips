/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Plugin management.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>
#include <dlfcn.h>

#include "utils.h"
#include "plugin.h"

/* Plugin list */
static struct plugin *plugin_list = NULL;

/* Find a symbol address */
void *plugin_find_symbol(struct plugin *plugin,char *symbol)
{
   return((plugin != NULL) ? dlsym(plugin->dl_handle,symbol) : NULL);
}

/* Initialize a plugin */
static int plugin_init(struct plugin *plugin)
{
   plugin_init_t init;

   if (!(init = plugin_find_symbol(plugin,"init")))
      return(-1);

   return(init());
}

/* Load a plugin */
struct plugin *plugin_load(char *filename)
{
   struct plugin *p;

   if (!(p = malloc(sizeof(*p))))
      return NULL;

   memset(p,0,sizeof(*p));

   if (!(p->filename = strdup(filename)))
      goto err_strdup;

   if (!(p->dl_handle = dlopen(filename,RTLD_LAZY))) {
      fprintf(stderr,"plugin_load(\"%s\"): %s\n",filename,dlerror());
      goto err_dlopen;
   }

   if (plugin_init(p) == -1)
      goto err_init;

   p->next = plugin_list;
   plugin_list = p;
   return p;

 err_init:
   dlclose(p->dl_handle);
 err_dlopen:
   free(p->filename);
 err_strdup:
   free(p);
   return NULL;
}
