/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Generic Hash Tables.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

#include "utils.h"
#include "hash.h"

/* Compare two strings */
int str_equal(void *s1, void *s2)
{
   return(strcmp((char *)s1, (char *)s2) == 0);
}

/* Hash function for a string */
u_int str_hash(void *str)
{
   char *p,*s = (char *)str;
   u_int h,g;

   for(h=0,p=s;*p!='\0';p+=1)
   {
      h = (h << 4) + *p;
      if ((g = h & 0xf0000000)) {
         h = h ^ (g >> 24);
         h = h ^ g;
      }
   }

   return(h);
}

/* Compare two integers (yes, it's stupid) */
int int_equal(void *i1, void *i2)
{
   return(((int)(long)i1) == (int)(long)i2);
}

/* Hash function for an integer (see above) */
u_int int_hash(void *i)
{
   u_int val = (u_int)(long)i;
   return(val ^ (val >> 16));
}

/* Compare two u64 (yes, it's stupid) */
int u64_equal(void *i1, void *i2)
{
   return((*(m_uint64_t *)i1) == (*(m_uint64_t *)i2));
}

/* Hash function for an u64 (see above) */
u_int u64_hash(void *i)
{
   m_uint64_t val = *(m_uint64_t *)i;
   return((u_int)(val ^ (val >> 32)));
}

/* Compare 2 pointers */
int ptr_equal(void *i1,void *i2)
{
   return((int)(i1 == i2));
}

/* Hash function for a pointer (see above) */
u_int ptr_hash(void *i)
{
   m_uint64_t val = (m_uint64_t)(m_iptr_t)i;
   return((u_int)((val & 0xFFFF) ^ ((val >> 24) & 0xFFFF) ^ 
                  ((val >> 48) & 0xFFFF)));
}

/* Free memory used by a node */
static inline void hash_node_free(hash_node_t *node)
{
   free(node);
}

/* Allocate memory for a new node */
static hash_node_t *hash_node_alloc(hash_table_t *ht,void *key,void *value)
{
   hash_node_t *node;

   node = malloc(sizeof(*node));
   assert(node!=NULL);
   node->key = key;
   node->value = value;
   node->next = NULL;
   return node;
}

/* Create a new hash table */
hash_table_t *hash_table_create(hash_fcompute hash_func,hash_fcompare key_cmp,
                                int hash_size)
{
   hash_table_t *ht;

   if (!hash_func || (hash_size <= 0))
      return NULL;   

   ht = malloc(sizeof(*ht));
   assert(ht!=NULL);

   memset(ht,0,sizeof(*ht));
   ht->hash_func = hash_func;
   ht->key_cmp = key_cmp;
   ht->size = hash_size;
   ht->nodes = calloc(ht->size,sizeof(hash_node_t *));
   assert(ht->nodes!=NULL);
   return ht;
}

/* Delete an existing Hash Table */
void hash_table_delete(hash_table_t *ht)
{
   hash_node_t *node, *node_next;
   u_int hash_val;

   if (!ht)
      return;

   for (hash_val = 0; hash_val < ht->size; hash_val++) {
      for (node = ht->nodes[hash_val]; node; node = node_next) {
         node_next = node->next;
         hash_node_free(node);
      }
      ht->nodes[hash_val] = NULL;
   }
   free(ht->nodes);
   free(ht);
}

/* Insert a new (key,value). If key already exists in table, replace value */
int hash_table_insert(hash_table_t *ht,void *key,void *value)
{
   hash_node_t *node;
   u_int hash_val;

   assert(ht!=NULL);

   hash_val = ht->hash_func(key) % ht->size;

   for(node=ht->nodes[hash_val];node;node=node->next)
      if (ht->key_cmp(node->key,key)) {
         node->value = value;
         return(0);
      }

   node = hash_node_alloc(ht,key,value);
   node->next = ht->nodes[hash_val];
   ht->nodes[hash_val] = node;
   ht->nnodes++;
   return(0);
}

/* Remove a pair (key,value) from an hash table */
void *hash_table_remove(hash_table_t *ht,void *key)
{
   hash_node_t **node,*tmp;
   u_int hash_val;
   void *value;

   assert(ht!=NULL);

   hash_val = ht->hash_func(key) % ht->size;

   for(node=&ht->nodes[hash_val];*node;node=&(*node)->next)
      if (ht->key_cmp((*node)->key,key)) {
         tmp = *node;
         value = tmp->value;
         *node = tmp->next;

         hash_node_free(tmp);
         return(value);
      }

   return NULL;
}

/* Hash Table Lookup */
void *hash_table_lookup(hash_table_t *ht,void *key)
{
   hash_node_t *node;
   u_int hash_val;

   assert(ht!=NULL);

   hash_val = ht->hash_func(key) % ht->size;

   for(node=ht->nodes[hash_val];node;node=node->next)
      if (ht->key_cmp(node->key,key))
         return node->value;

   return NULL;
}

/* Hash Table Lookup - key direct comparison */
void *hash_table_lookup_dcmp(hash_table_t *ht,void *key)
{
   hash_node_t *node;
   u_int hash_val;

   assert(ht!=NULL);

   hash_val = ht->hash_func(key) % ht->size;

   for(node=ht->nodes[hash_val];node;node=node->next)
      if (node->key == key)
         return node->value;

   return NULL;
}

/* Call the specified function for each node found in hash table */
int hash_table_foreach(hash_table_t *ht,hash_fforeach user_fn,void *opt_arg)
{
   hash_node_t *node;
   int i;

   assert(ht!=NULL);

   for(i=0;i<ht->size;i++)
      for(node=ht->nodes[i];node;node=node->next)
         user_fn(node->key,node->value,opt_arg);
   
   return(0);
}
