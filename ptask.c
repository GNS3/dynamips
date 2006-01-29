/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Periodic tasks centralization. Used for TX part of network devices.
 */

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
#include <sys/time.h>
#include <sys/types.h>
#include <pthread.h>

#include "ptask.h"

static pthread_t ptask_thread;
static pthread_mutex_t ptask_mutex = PTHREAD_MUTEX_INITIALIZER;
static ptask_t *ptask_list = NULL;
static u_int ptask_sleep_time = 10;

#define PTASK_LOCK() pthread_mutex_lock(&ptask_mutex)
#define PTASK_UNLOCK() pthread_mutex_unlock(&ptask_mutex)

/* Periodic task thread */
static void *ptask_run(void *arg)
{
   ptask_t *task;

   for(;;) {
      PTASK_LOCK();
      for(task=ptask_list;task;task=task->next)
         task->cbk(task->object,task->arg);
      PTASK_UNLOCK();
      usleep(ptask_sleep_time*1000);
   }

   return NULL;
}

/* Add a new task */
int ptask_add(ptask_callback cbk,void *object,void *arg)
{
   ptask_t *task;

   if (!(task = malloc(sizeof(*task)))) {
      fprintf(stderr,"ptask_add: unable to add new task.\n");
      return(-1);
   }

   memset(task,0,sizeof(*task));
   task->cbk = cbk;
   task->object = object;
   task->arg = arg;

   PTASK_LOCK();
   task->next = ptask_list;
   ptask_list = task;
   PTASK_UNLOCK();
   return(0);
}

/* Initialize ptask module */
int ptask_init(u_int sleep_time)
{
   if (sleep_time)
      ptask_sleep_time = sleep_time;

   if (pthread_create(&ptask_thread,NULL,ptask_run,NULL) != 0) {
      fprintf(stderr,"ptask_init: unable to create thread.\n");
      return(-1);
   }

   return(0);
}
