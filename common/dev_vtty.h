/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Virtual console TTY.
 */

#ifndef __DEV_VTTY_H__
#define __DEV_VTTY_H__

#include <sys/types.h>
#include <pthread.h>

#include "vm.h"
#include <stdio.h>

#include "rust-dynamips.h"

#define VTTY_LOCK(tty) pthread_mutex_lock(&(tty)->lock);
#define VTTY_UNLOCK(tty) pthread_mutex_unlock(&(tty)->lock);

/* create a virtual tty */
vtty_t *vtty_create(vm_instance_t *vm,char *name,int type,int tcp_port,
                    const vtty_serial_option_t *option);

/* delete a virtual tty */
void vtty_delete(vtty_t *vtty);

/* Store arbritary data in the FIFO buffer */
int vtty_store_data(vtty_t *vtty,char *data, int len);

/* read a character from the buffer (-1 if the buffer is empty) */
int vtty_get_char(vtty_t *vtty);

/* print a character to vtty */
void vtty_put_char(vtty_t *vtty, char ch);

/* Put a buffer to vtty */
void vtty_put_buffer(vtty_t *vtty,char *buf,size_t len);

/* Flush VTTY output */
void vtty_flush(vtty_t *vtty);

/* returns TRUE if a character is available in buffer */
int vtty_is_char_avail(vtty_t *vtty);

/* write CTRL+C to buffer */
int vtty_store_ctrlc(vtty_t *);

/* Initialize the VTTY thread */
int vtty_init(void);

void vtty_set_ctrlhandler(int n);
void vtty_set_telnetmsg(int n);

#endif
