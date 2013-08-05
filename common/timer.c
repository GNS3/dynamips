/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * timer.c: Management of timers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <assert.h>

#include "utils.h"
#include "mempool.h"
#include "hash.h"
#include "timer.h"

/* Lock and unlock access to global structures */
#define TIMER_LOCK()    pthread_mutex_lock(&timer_mutex)
#define TIMER_UNLOCK()  pthread_mutex_unlock(&timer_mutex)

/* Pool of Timer Queues */
static timer_queue_t *timer_queue_pool = NULL;

/* Hash table to map Timer ID to timer entries */
static hash_table_t *timer_id_hash = NULL;

/* Last ID used. */
static timer_id timer_next_id = 1;

/* Mutex to access to global structures (Hash Tables, Pool of queues, ...) */
static pthread_mutex_t timer_mutex = PTHREAD_MUTEX_INITIALIZER;


/* Find a timer by its ID */
static inline timer_entry_t *timer_find_by_id(timer_id id)
{
   return(hash_table_lookup(timer_id_hash,&id));
}

/* Allocate a new ID. Disgusting method but it should work. */
static inline timer_id timer_alloc_id(void)
{
   while(hash_table_lookup(timer_id_hash,&timer_next_id))
      timer_next_id++;

   return(timer_next_id);
}

/* Free an ID */
static inline void timer_free_id(timer_id id)
{
   hash_table_remove(timer_id_hash,&id);
}

/* 
 * Select the queue of the pool that has the lowest criticity level. This
 * is a stupid method.
 */
timer_queue_t *timer_select_queue_from_pool(void)
{
   timer_queue_t *s_queue,*queue;
   int level;

   /* to begin, select the first queue of the pool */
   s_queue = timer_queue_pool;
   level = s_queue->level;

   /* walk through timer queues */
   for(queue=timer_queue_pool->next;queue;queue=queue->next) {
      if (queue->level < level) {
         level = queue->level;
         s_queue = queue;
      }
   }

   /* returns selected queue */
   return s_queue;
}

/* Add a timer in a queue */
static inline void timer_add_to_queue(timer_queue_t *queue,
                                      timer_entry_t *timer)
{
   timer_entry_t *t,*prev = NULL;

   /* Insert after the last timer with the same or earlier time */
   for(t=queue->list;t;t=t->next) {
      if (t->expire > timer->expire) break;
      prev = t;
   }

   /* Add it in linked list */
   timer->next = t;
   timer->prev = prev;
   timer->queue = queue;

   if (timer->next)
      timer->next->prev = timer;

   if (timer->prev)
      timer->prev->next = timer;
   else
      queue->list = timer;

   /* Increment number of timers in queue */
   queue->timer_count++;

   /* Increment criticity level */
   queue->level += timer->level;
}

/* Add a timer in a queue atomically */
static inline void timer_add_to_queue_atomic(timer_queue_t *queue,
                                             timer_entry_t *timer)
{
   TIMERQ_LOCK(queue);
   timer_add_to_queue(queue,timer);
   TIMERQ_UNLOCK(queue);
}

/* Remove a timer from queue */
static inline void timer_remove_from_queue(timer_queue_t *queue,
                                           timer_entry_t *timer)
{
   if (timer->prev)
      timer->prev->next = timer->next;
   else
      queue->list = timer->next;

   if (timer->next)
      timer->next->prev = timer->prev;

   timer->next = timer->prev = NULL;

   /* Decrement number of timers in queue */
   queue->timer_count--;

   /* Decrement criticity level */
   queue->level -= timer->level;
}

/* Remove a timer from a queue atomically */
static inline void 
timer_remove_from_queue_atomic(timer_queue_t *queue,timer_entry_t *timer)
{
   TIMERQ_LOCK(queue);
   timer_remove_from_queue(queue,timer);
   TIMERQ_UNLOCK(queue);
}

/* Free ressources used by a timer */
static inline void timer_free(timer_entry_t *timer,int take_lock)
{
   if (take_lock) TIMER_LOCK();

   /* Remove ID from hash table */
   hash_table_remove(timer_id_hash,&timer->id);

   if (take_lock) TIMER_UNLOCK();

   /* Free memory used by timer */
   free(timer);
}

/* Run timer action */
static inline int timer_exec(timer_entry_t *timer)
{
   return(timer->callback(timer->user_arg,timer));
}

/* Schedule a timer in a queue */
static inline void timer_schedule_in_queue(timer_queue_t *queue,
                                           timer_entry_t *timer)
{
   m_tmcnt_t current,current_adj;

   /* Set new expiration date and clear "run" flag */
   if (timer->flags & TIMER_BOUNDARY) {
      current_adj = m_gettime_adj();
      current = m_gettime();

      timer->expire = current + timer->offset +
         (timer->interval - (current_adj % timer->interval));
   } else
      timer->expire += timer->interval;
   
   timer->flags &= ~TIMER_RUNNING;
   timer_add_to_queue(queue,timer);   
}

/* Schedule a timer */
static int timer_schedule(timer_entry_t *timer)
{
   timer_queue_t *queue;

   /* Select the least used queue of the pool */
   if (!(queue = timer_select_queue_from_pool())) {
      fprintf(stderr,
              "timer_schedule: no pool available for timer with ID %llu",
              timer->id);
      return(-1);
   }

   /* Reschedule it in queue */
   TIMERQ_LOCK(queue);
   timer_schedule_in_queue(queue,timer);
   TIMERQ_UNLOCK(queue);
   return(0);
}

/* Timer loop */
static void *timer_loop(timer_queue_t *queue)
{
   struct timespec t_spc;
   timer_entry_t *timer;
   m_tmcnt_t c_time;

   /* Set signal properties */
   m_signal_block(SIGINT);
   m_signal_block(SIGQUIT);
   m_signal_block(SIGTERM);

   for(;;)
   {
      /* Prevent asynchronous access problems */
      TIMERQ_LOCK(queue);

      /* We need to check "running" flags to know if we must stop */
      if (!queue->running) {
         TIMERQ_UNLOCK(queue);
         break;
      }

      /* Get first event */
      timer = queue->list;

      /* 
       * If we have timers in queue, we setup a timer to wait for first one.
       * In all cases, thread is woken up when a reschedule occurs.
       */
      if (timer) {
         t_spc.tv_sec = timer->expire / 1000;
         t_spc.tv_nsec = (timer->expire % 1000) * 1000000;
         pthread_cond_timedwait(&queue->schedule,&queue->lock,&t_spc);
      }
      else {
         /* We just wait for reschedule since we don't have any timer */
         pthread_cond_wait(&queue->schedule,&queue->lock);
      }

      /* We need to check "running" flags to know if we must stop */
      if (!queue->running) {
         TIMERQ_UNLOCK(queue);
         break;
      }

      /* 
       * Now, we need to find why we were woken up. So, we compare current
       * time with first timer to see if we must execute action associated
       * with it.
       */
      c_time = m_gettime();

      /* Get first event */
      timer = queue->list;

      /* If there is nothing to do for now, wait again */
      if ((timer == NULL) || (timer->expire > c_time)) {
         TIMERQ_UNLOCK(queue);
         continue;
      }

      /* 
       * We have a timer to manage. Remove it from queue and mark it as
       * running.
       */
      timer_remove_from_queue(queue,timer);
      timer->flags |= TIMER_RUNNING;
      
      /* Execute user function and reschedule timer if required */
      if (timer_exec(timer))
         timer_schedule_in_queue(queue,timer);

      TIMERQ_UNLOCK(queue);
   }

   return NULL;
}

/* Remove a timer */
int timer_remove(timer_id id)
{
   timer_queue_t *queue = NULL;
   timer_entry_t *timer;

   TIMER_LOCK();
   
   /* Find timer */
   if (!(timer = timer_find_by_id(id))) {
      TIMER_UNLOCK();
      return(-1);
   }

   /* If we have a queue, remove timer from it atomically */
   if (timer->queue) {
      queue = timer->queue;
      timer_remove_from_queue_atomic(queue,timer);
   }

   /* Release timer ID */
   timer_free_id(id);

   /* Free memory used by timer */
   free(timer);
   TIMER_UNLOCK();
   
   /* Signal to this queue that it has been modified */
   if (queue)
      pthread_cond_signal(&queue->schedule);
   return(0);
}

/* Enable a timer */
static timer_id timer_enable(timer_entry_t *timer)
{
   /* Allocate a new ID */
   TIMER_LOCK();
   timer->id = timer_alloc_id();

   /* Insert ID in hash table */
   if (hash_table_insert(timer_id_hash,&timer->id,timer) == -1) {
      TIMER_UNLOCK();
      free(timer);
      return(0);
   }

   /* Schedule event */
   if (timer_schedule(timer) == -1) {
      timer_free(timer,FALSE);
      timer = NULL;
      TIMER_UNLOCK();
      return(0);
   }

   /* Returns timer ID */
   TIMER_UNLOCK();      
   pthread_cond_signal(&timer->queue->schedule);
   return(timer->id);
}

/* Create a new timer */
timer_id timer_create_entry(m_tmcnt_t interval,int boundary,int level,
                            timer_proc callback,void *user_arg)
{
   timer_entry_t *timer;

   /* Allocate memory for new timer entry */
   if (!(timer = malloc(sizeof(*timer))))
      return(0);

   timer->interval = interval;
   timer->offset = 0;
   timer->callback = callback;
   timer->user_arg = user_arg;
   timer->flags = 0;
   timer->level = level;

   /* Set expiration delay */
   if (boundary) {
      timer->flags |= TIMER_BOUNDARY;
   } else
      timer->expire = m_gettime();

   return(timer_enable(timer));
}

/* Create a timer on boundary, with an offset */
timer_id timer_create_with_offset(m_tmcnt_t interval,m_tmcnt_t offset,
                                  int level,timer_proc callback,void *user_arg)
{
   timer_entry_t *timer;

   /* Allocate memory for new timer entry */
   if (!(timer = malloc(sizeof(*timer))))
      return(0);

   timer->interval = interval;
   timer->offset = 0;
   timer->callback = callback;
   timer->user_arg = user_arg;
   timer->flags = 0;
   timer->level = level;
   timer->flags |= TIMER_BOUNDARY;

   return(timer_enable(timer));
}

/* Set a new interval for a timer */
int timer_set_interval(timer_id id,long interval)
{
   timer_queue_t *queue;
   timer_entry_t *timer;

   TIMER_LOCK();

   /* Locate timer */
   if (!(timer = timer_find_by_id(id))) {
      TIMER_UNLOCK();
      return(-1);
   }

   queue = timer->queue;

   TIMERQ_LOCK(queue);

   /* Compute new expiration date */
   timer->interval = interval;
   timer->expire = m_gettime() + (m_tmcnt_t)interval;

   timer_remove_from_queue(queue,timer);
   timer_schedule_in_queue(queue,timer);

   TIMERQ_UNLOCK(queue);
   TIMER_UNLOCK();

   /* Reschedule */
   pthread_cond_signal(&queue->schedule);
   return(0);
}

/* Create a new timer queue */
timer_queue_t *timer_create_queue(void)
{
   timer_queue_t *queue;

   /* Create new queue structure */
   if (!(queue = malloc(sizeof(*queue))))
      return NULL;

   queue->running = TRUE;
   queue->list = NULL;
   queue->level = 0;

   /* Create mutex */
   if (pthread_mutex_init(&queue->lock,NULL))
      goto err_mutex;

   /* Create condition */
   if (pthread_cond_init(&queue->schedule,NULL))
      goto err_cond;

   /* Create thread */
   if (pthread_create(&queue->thread,NULL,(void *(*)(void *))timer_loop,queue))
      goto err_create;

   return queue;

err_create:
   pthread_cond_destroy(&queue->schedule);
err_cond:
   pthread_mutex_destroy(&queue->lock);
err_mutex:
   free(queue);
   return NULL;
}

/* Flush queues */
void timer_flush_queues(void)
{
   timer_entry_t *timer,*next_timer;
   timer_queue_t *queue,*next_queue;
   pthread_t thread;

   TIMER_LOCK();

   for(queue=timer_queue_pool;queue;queue=next_queue)
   {
      TIMERQ_LOCK(queue);
      next_queue = queue->next;
      thread = queue->thread;

      /* mark queue as not running */
      queue->running = FALSE;

      /* suppress all timers */
      for(timer=queue->list;timer;timer=next_timer) {
         next_timer = timer->next;
         timer_free_id(timer->id);
         free(timer);
      }

      /* signal changes to the queue thread */
      pthread_cond_signal(&queue->schedule);

      TIMERQ_UNLOCK(queue);

      /* wait for thread to terminate */
      pthread_join(thread,NULL);

      pthread_cond_destroy(&queue->schedule);
      pthread_mutex_destroy(&queue->lock);
      free(queue);
   }
   timer_queue_pool = NULL;

   TIMER_UNLOCK();
}

/* Add a specified number of queues to the pool */
int timer_pool_add_queues(int nr_queues)
{
   timer_queue_t *queue;
   int i;

   for(i=0;i<nr_queues;i++)
   {
      if (!(queue = timer_create_queue()))
         return(-1);

      TIMER_LOCK();
      queue->next = timer_queue_pool;
      timer_queue_pool = queue;
      TIMER_UNLOCK();
   }

   return(0);
}

/* Terminate timer sub-sytem */
static void timer_terminate(void)
{
   timer_flush_queues();

   assert(timer_id_hash);
   hash_table_delete(timer_id_hash);
   timer_id_hash = NULL;
}

/* Initialize timer sub-system */
int timer_init(void)
{
   /* Initialize hash table which maps ID to timer entries */
   assert(!timer_id_hash);
   if (!(timer_id_hash = hash_u64_create(TIMER_HASH_SIZE))) {
      fprintf(stderr,"timer_init: unable to create hash table.");
      return(-1);
   }

   /* Initialize default queues. If this fails, try to continue. */
   if (timer_pool_add_queues(TIMERQ_NUMBER) == -1) {
      fprintf(stderr,
              "timer_init: unable to initialize at least one timer queue.");
   }

   atexit(timer_terminate);

   return(0);
}
