/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Generic Hash Tables.
 */

#ifndef __HASH_H__
#define __HASH_H__  1

#include <sys/types.h>
#include "utils.h"

/* Key computation function */
typedef u_int (*hash_fcompute)(void *key);

/* Comparison function for 2 keys */
typedef int (*hash_fcompare)(void *key1,void *key2);

/* User function to call when using hash_table_foreach */
typedef void (*hash_fforeach)(void *key,void *value,void *opt_arg);

/* Hash element (pair key,value) */
typedef struct hash_node hash_node_t;
struct hash_node {
   void *key, *value;
   hash_node_t *next;
};

/* Hash Table definition */
typedef struct hash_table hash_table_t;
struct hash_table {
   int size,nnodes;
   hash_node_t **nodes;
   hash_fcompute hash_func;
   hash_fcompare key_cmp;
};

#define hash_string_create(hash_size) \
   hash_table_create(str_hash,str_equal,hash_size)

#define hash_int_create(hash_size) \
   hash_table_create(int_hash,int_equal,hash_size)

#define hash_u64_create(hash_size) \
   hash_table_create(u64_hash,u64_equal,hash_size)

#define hash_ptr_create(hash_size) \
   hash_table_create(ptr_hash,ptr_equal,hash_size)

#define HASH_TABLE_FOREACH(i,ht,hn) \
   for(i=0;i<ht->size;i++) \
      for(hn=ht->nodes[i];hn;hn=hn->next)

/* Create a new hash table */
hash_table_t *hash_table_create(hash_fcompute hash_func,hash_fcompare key_cmp,
                                int hash_size);

/* Delete an existing Hash Table */
void hash_table_delete(hash_table_t *ht);

/* Insert a new (key,value). If key already exist in table, replace value */
int hash_table_insert(hash_table_t *ht,void *key,void *value);

/* Remove a pair (key,value) from an hash table */
void *hash_table_remove(hash_table_t *ht,void *key);

/* Hash Table Lookup */
void *hash_table_lookup(hash_table_t *ht,void *key);

/* Call the specified function for each node found in hash table */
int hash_table_foreach(hash_table_t *ht,hash_fforeach user_fn,void *opt_arg);

/* Hash Table Lookup - key direct comparison */
void *hash_table_lookup_dcmp(hash_table_t *ht,void *key);

/* Hash Functions for strings */
int str_equal(void *s1,void *s2);
u_int str_hash(void *str);

/* Hash Functions for integers */
int int_equal(void *i1,void *i2);
u_int int_hash(void *i);

/* Hash Functions for u64 */
int u64_equal(void *i1,void *i2);
u_int u64_hash(void *i);

/* Hash Function for pointers */
int ptr_equal(void *i1,void *i2);
u_int ptr_hash(void *i);

#endif
