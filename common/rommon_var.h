/*
 * Cisco router simulation platform.
 * Copyright (c) 2007 Christophe Fillot (cf@utc.fr)
 *
 * ROMMON Environment Variables.
 */

#ifndef __ROMMON_VAR_H__
#define __ROMMON_VAR_H__

/* ROMMON variable */
struct rommon_var {
   struct rommon_var *next;
   char *name;
   char *value;
};

/* List of ROMMON variables */
struct rommon_var_list {
   char *filename;
   struct rommon_var *var_list;
};

/* Load file containing ROMMON variables */
int rommon_load_file(struct rommon_var_list *rvl);

/* Add a new variable */
int rommon_var_add(struct rommon_var_list *rvl,char *name,char *value);

/* 
 * Add a new variable, specified at the format: var=value.
 * The string is modified.
 */
int rommon_var_add_str(struct rommon_var_list *rvl,char *str);

/* Get the specified variable */
int rommon_var_get(struct rommon_var_list *rvl,char *name,
                   char *buffer,size_t len);

#endif

