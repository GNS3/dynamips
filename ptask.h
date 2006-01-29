/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Periodic tasks centralization.
 */

#ifndef __PTASK_H__
#define __PTASK_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "utils.h"

/* periodic task callback prototype */
typedef int (*ptask_callback)(void *object,void *arg);

/* periodic task definition */
typedef struct ptask ptask_t;
struct ptask {
   ptask_t *next;
   ptask_callback cbk;
   void *object,*arg;
};

/* Add a new task */
int ptask_add(ptask_callback cbk,void *object,void *arg);

/* Initialize ptask module */
int ptask_init(u_int sleep_time);

#endif
