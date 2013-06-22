/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * Plugins.
 */

#ifndef __PLUGIN_H__
#define __PLUGIN_H__

struct plugin {
   char *filename;
   void *dl_handle;
   struct plugin *next;
};

typedef int (*plugin_init_t)(void);

/* Find a symbol address */
void *plugin_find_symbol(struct plugin *plugin,char *symbol);

/* Load a plugin */
struct plugin *plugin_load(char *filename);

#endif
