/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Virtual console TTY.
 */

#ifndef __DEV_VTTY_H__
#define __DEV_VTTY_H__

#include <sys/types.h>

/* 4 Kb should be enough for a keyboard buffer */
#define VTTY_BUFFER_SIZE  4096

enum {
   VTTY_TYPE_NONE = 0,
   VTTY_TYPE_TERM,
   VTTY_TYPE_TCP,
};

/* Virtual TTY structure */
typedef struct virtual_tty vtty_t;
struct virtual_tty {
   char *name;
   int type;
   int fd;
   int tcp_port;
   u_char buffer[VTTY_BUFFER_SIZE];
   u_int read_ptr,write_ptr;
};

/* create a virtual tty */
vtty_t *vtty_create(char *name,int type,int tcp_port);

/* read a character (until one is available) and store it in buffer */
int vtty_read_and_store(vtty_t *vtty);

/* read a character from the buffer (-1 if the buffer is empty) */
int vtty_get_char(vtty_t *vtty);

/* print a character to vtty */
void vtty_put_char(vtty_t *vtty, char ch);

/* returns TRUE if a character is available in buffer */
int vtty_is_char_avail(vtty_t *vtty);

#endif
