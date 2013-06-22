/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * timer.h: Management of timers.
 */

#ifndef __TIMER_H__
#define __TIMER_H__  1

#include <sys/types.h>
#include <pthread.h>
#include "utils.h"

/* Default number of Timer Queues */
#define TIMERQ_NUMBER   10

/* Timer definitions */
typedef m_uint64_t timer_id;

typedef struct timer_entry timer_entry_t;
typedef struct timer_queue timer_queue_t;

/* Defines callback function format */
typedef int (*timer_proc)(void *,timer_entry_t *);

/* Timer flags */
#define TIMER_DELETED    1
#define TIMER_RUNNING    2
#define TIMER_BOUNDARY   4

/* Number of entries in hash table */
#define TIMER_HASH_SIZE  512

/* Timer properties */
struct timer_entry {
   long interval;                   /* Interval in msecs */
   m_tmcnt_t expire,offset;         /* Next execution date */
   timer_proc callback;             /* User callback function */
   void *user_arg;                  /* Optional user data */
   int flags;                       /* Flags */
   timer_id id;                     /* Unique identifier */
   int level;                       /* Criticity level */

   timer_queue_t *queue;            /* Associated Timer Queue */
   timer_entry_t *prev,*next;       /* Double linked-list */
};

/* Timer Queue */
struct timer_queue {
   timer_entry_t *list;             /* List of timers */
   pthread_mutex_t lock;            /* Mutex for concurrent accesses */
   pthread_cond_t schedule;         /* Scheduling condition */
   pthread_t thread;                /* Thread running timer loop */
   int running;                     /* Running flag */
   int timer_count;                 /* Number of timers */
   int level;                       /* Sum of criticity levels */
   timer_queue_t *next;             /* Next Timer Queue (for pools) */
};

/* Lock and unlock access to a timer queue */
#define TIMERQ_LOCK(queue)    pthread_mutex_lock(&(queue)->lock)
#define TIMERQ_UNLOCK(queue)  pthread_mutex_unlock(&(queue)->lock)

/* Remove a timer */
int timer_remove(timer_id id);

/* Create a new timer */
timer_id timer_create_entry(m_tmcnt_t interval,int boundary,int level,
                            timer_proc callback,void *user_arg);

/* Create a timer on boundary, with an offset */
timer_id timer_create_with_offset(m_tmcnt_t interval,m_tmcnt_t offset,
                                  int level,timer_proc callback,
                                  void *user_arg);

/* Set a new interval for a timer */
int timer_set_interval(timer_id id,long interval);

/* Create a new timer queue */
timer_queue_t *timer_create_queue(void);

/* Flush queues */
void timer_flush_queues(void);

/* Add a specified number of queues to the pool */
int timer_pool_add_queues(int nr_queues);

/* Initialize timer sub-system */
int timer_init(void);

#endif
