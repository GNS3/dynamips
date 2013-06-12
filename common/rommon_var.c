/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * ROMMON Environment Variables.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "utils.h"
#include "rommon_var.h"

#define DEBUG_OPEN  0

/* Load file containing ROMMON variables */
int rommon_load_file(struct rommon_var_list *rvl)
{
   char buffer[512];
   FILE *fd;

   if (!rvl->filename)
      return(-1);

   if (!(fd = fopen(rvl->filename,"r"))) {
#if DEBUG_OPEN
      fprintf(stderr,"%s: unable to open file %s (%s)\n",
              __func__,rvl->filename,strerror(errno));
#endif
      return(-1);
   }

   while(!feof(fd)) {
      if (m_fgets(buffer,sizeof(buffer),fd))
         rommon_var_add_str(rvl,buffer);
   }

   fclose(fd);
   return(0);
}

/* Write a file with all ROMMON variables */
int rommon_var_update_file(struct rommon_var_list *rvl)
{
   struct rommon_var *var;
   FILE *fd;

   if (!rvl->filename)
      return(-1);

   if (!(fd = fopen(rvl->filename,"w"))) {
      fprintf(stderr,"%s: unable to create file %s (%s)\n",
              __func__,rvl->filename,strerror(errno));
      return(-1);
   }

   for(var=rvl->var_list;var;var=var->next)
      fprintf(fd,"%s=%s\n",var->name,var->value ? var->value : "");

   fclose(fd);
   return(0);
}

/* Find the specified variable */
struct rommon_var *rommon_var_find(struct rommon_var_list *rvl,char *name)
{
   struct rommon_var *var;

   for(var=rvl->var_list;var;var=var->next)
      if (!strcmp(var->name,name))
         return var;

   return NULL;
}

/* Create a new variable */
static struct rommon_var *rommon_var_create(char *name)
{
   struct rommon_var *var;

   if (!(var = malloc(sizeof(*var))))
      return NULL;

   var->next  = NULL;
   var->value = NULL;
   var->name  = strdup(name);

   if (!var->name) {
      free(var);
      return NULL;
   }

   return var;
}

/* Set value for a variable */
static int rommon_var_set(struct rommon_var *var,char *value)
{
   char *new_value;

   if (!(new_value = strdup(value)))
      return(-1);

   /* free old value */
   if (var->value)
      free(var->value);

   var->value = new_value;
   return(0);
}

/* Add a new variable */
int rommon_var_add(struct rommon_var_list *rvl,char *name,char *value)
{
   struct rommon_var *var;

   /* if the variable already exists, overwrite it */
   if (!(var = rommon_var_find(rvl,name))) {
      var = rommon_var_create(name);
      if (!var) return(-1);

      if (rommon_var_set(var,value) == -1)
         return(-1);

      var->next = rvl->var_list;
      rvl->var_list = var;
   } else {
      rommon_var_set(var,value);
   }
   
   /* synchronize disk file */
   return(rommon_var_update_file(rvl));
}

/* 
 * Add a new variable, specified at the format: var=value.
 * The string is modified.
 */
int rommon_var_add_str(struct rommon_var_list *rvl,char *str)
{
   char *eq_sym;

   if (!(eq_sym = strchr(str,'=')))
      return(-1);
   
   /* The variable cannot be null */
   if (str == eq_sym)
      return(-1);

   *eq_sym = 0;
   return(rommon_var_add(rvl,str,eq_sym+1));
}

/* Get the specified variable */
int rommon_var_get(struct rommon_var_list *rvl,char *name,
                   char *buffer,size_t len)
{
   struct rommon_var *var;
   
   if (!(var = rommon_var_find(rvl,name)) || !var->value)
      return(-1);

   strncpy(buffer,var->value,len-1);
   buffer[len-1] = '\0';
   return(0);
}
