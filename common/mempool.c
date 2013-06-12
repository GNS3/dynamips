/*
 * Copyright (c) 1999-2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * mempool.c: Simple Memory Pools.
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
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>

#include "utils.h"
#include "mempool.h"

/* 
 * Internal function used to allocate a memory block, and do basic operations
 * on it. It does not manipulate pools, so no mutex is needed.
 */
static inline memblock_t *memblock_alloc(size_t size,int zeroed)
{
   memblock_t *block;
   size_t total_size;

   total_size = size + sizeof(memblock_t);
   if (!(block = malloc(total_size)))
      return NULL;

   if (zeroed) 
      memset(block,0,total_size);

   block->tag = MEMBLOCK_TAG;
   block->block_size = size;
   block->prev = block->next = NULL;   
   return block;
}

/* Insert block in linked list */
static inline void memblock_insert(mempool_t *pool,memblock_t *block)
{
   MEMPOOL_LOCK(pool);

   pool->nr_blocks++;
   pool->total_size += block->block_size;

   block->prev = NULL;
   block->next = pool->block_list;

   if (block->next)
      block->next->prev = block;

   pool->block_list = block;

   MEMPOOL_UNLOCK(pool);
}

/* Remove block from linked list */
static inline void memblock_delete(mempool_t *pool,memblock_t *block)
{
   MEMPOOL_LOCK(pool);

   pool->nr_blocks--;
   pool->total_size -= block->block_size;

   if (!block->prev)
      pool->block_list = block->next;
   else
      block->prev->next = block->next;

   if (block->next)
      block->next->prev = block->prev;

   block->next = block->prev = NULL;
   MEMPOOL_UNLOCK(pool);
}

/* Allocate a new block in specified pool (internal function) */
static inline void *mp_alloc_inline(mempool_t *pool,size_t size,int zeroed)
{
   memblock_t *block;

   if (!(block = memblock_alloc(size,zeroed)))
      return NULL;

   block->pool = pool;
   memblock_insert(pool,block);
   return(block->data);
}

/* Allocate a new block in specified pool */
void *mp_alloc(mempool_t *pool,size_t size)
{
   return(mp_alloc_inline(pool,size,TRUE));
}

/* Allocate a new block which will not be zeroed */
void *mp_alloc_n0(mempool_t *pool,size_t size)
{
   return(mp_alloc_inline(pool,size,FALSE));
}

/* Reallocate a block */
void *mp_realloc(void *addr,size_t new_size)
{
   memblock_t *ptr,*block = (memblock_t *)addr - 1;
   mempool_t *pool;
   size_t total_size;

   assert(block->tag == MEMBLOCK_TAG);
   pool = block->pool;

   /* remove this block from list */
   memblock_delete(pool,block);

   /* reallocate block with specified size */
   total_size = new_size + sizeof(memblock_t);

   if (!(ptr = realloc(block,total_size))) {
      memblock_insert(pool,block);
      return NULL;
   }

   ptr->block_size = new_size;
   memblock_insert(pool,ptr);
   return ptr->data;
}

/* Allocate a new memory block and copy data into it */
void *mp_dup(mempool_t *pool,void *data,size_t size)
{
   void *p;

   if ((p = mp_alloc_n0(pool,size)))
      memcpy(p,data,size);

   return p;
}

/* Duplicate specified string and insert it in a memory pool */
char *mp_strdup(mempool_t *pool,char *str)
{
   char *new_str;

   if ((new_str = mp_alloc(pool,strlen(str)+1)) == NULL)
      return NULL;

   strcpy(new_str,str);
   return new_str;
}

/* Free block at specified address */
int mp_free(void *addr)
{
   memblock_t *block = (memblock_t *)addr - 1;
   mempool_t *pool;

   if (addr != NULL) {
      assert(block->tag == MEMBLOCK_TAG);
      pool = block->pool;

      memblock_delete(pool,block);
      memset(block,0,sizeof(memblock_t));
      free(block);
   }

   return(0);
}

/* Free block at specified address and clean pointer */
int mp_free_ptr(void *addr)
{
   void *p;

   assert(addr != NULL);
   p = *(void **)addr;
   *(void **)addr = NULL;
   mp_free(p);
   return(0);
}

/* Free all blocks of specified pool */
void mp_free_all_blocks(mempool_t *pool)
{
   memblock_t *block,*next;

   MEMPOOL_LOCK(pool);

   for(block=pool->block_list;block;block=next) {
      next = block->next;
      free(block);
   }

   pool->block_list = NULL;
   pool->nr_blocks = 0;
   pool->total_size = 0;

   MEMPOOL_UNLOCK(pool);
}

/* Free specified memory pool */
void mp_free_pool(mempool_t *pool)
{
   mp_free_all_blocks(pool);

   if (!(pool->flags & MEMPOOL_FIXED))
      free(pool);
}

/* Create a new pool in a fixed memory area */
mempool_t *mp_create_fixed_pool(mempool_t *mp,char *name)
{
   memset(mp,0,sizeof(*mp));

   if (pthread_mutex_init(&mp->lock,NULL) != 0)
      return NULL;

   mp->name = name;
   mp->block_list = NULL;
   mp->flags = MEMPOOL_FIXED;
   return mp;
}

/* Create a new pool */
mempool_t *mp_create_pool(char *name)
{
   mempool_t *mp = malloc(sizeof(*mp));

   if (!mp || !mp_create_fixed_pool(mp,name)) {
      free(mp);
      return NULL;
   }

   mp->flags = 0;  /* clear "FIXED" flag */
   return mp;
}
