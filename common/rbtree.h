/*
 * IPFlow Collector
 * Copyright (c) 2004 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * rbtree.c: Red/Black Trees.
 */

#ifndef __RBTREE_H__
#define __RBTREE_H__   1

static const char rcsid_rbtree[] = "$Id$";

#include <sys/types.h>
#include "mempool.h"

/* Comparison function for 2 keys */
typedef int (*tree_fcompare)(void *key1,void *key2,void *opt);

/* User function to call when using rbtree_foreach */
typedef void (*tree_fforeach)(void *key,void *value,void *opt);

/* Node colors */
enum {
   RBTREE_RED = 0,
   RBTREE_BLACK,
};

/*
 * Description of a node in a Red/Black tree. To be more efficient, keys are
 * stored with a void * pointer, allowing to use different type keys.
 */
typedef struct rbtree_node rbtree_node;
struct rbtree_node {
   /* Key and Value */
   void *key,*value;

   /* Left and right nodes */
   rbtree_node *left,*right;

   /* Parent node */
   rbtree_node *parent;

   /* Node color */
   short color;
};

/*
 * Description of a Red/Black tree. For commodity, a name can be given to the
 * tree. "rbtree_comp" is a pointer to a function, defined by user, which
 * compares keys during node operations.
 */
typedef struct rbtree_tree rbtree_tree;
struct rbtree_tree {
   int node_count;              /* Number of Nodes */
   mempool_t mp;                  /* Memory pool */
   rbtree_node nil;             /* Sentinel */
   rbtree_node *root;           /* Root node */
   tree_fcompare key_cmp;       /* Key comparison function */
   void *opt_data;              /* Optional data for comparison */
};

/* Insert a node in an Red/Black tree */
int rbtree_insert(rbtree_tree *tree,void *key,void *value);

/* Removes a node out of a tree */
void *rbtree_remove(rbtree_tree *tree,void *key);

/* 
 * Lookup for a node corresponding to "key". If node does not exist, 
 * function returns null pointer.
 */
void *rbtree_lookup(rbtree_tree *tree,void *key);

/* Call the specified function for each node */
int rbtree_foreach(rbtree_tree *tree,tree_fforeach user_fn,void *opt);

/* Compute the height of a Red/Black tree */
int rbtree_height(rbtree_tree *tree);

/* Returns the number of nodes */
int rbtree_node_count(rbtree_tree *tree);

/* Purge all nodes */
void rbtree_purge(rbtree_tree *tree);

/* Check tree consistency */
int rbtree_check(rbtree_tree *tree);

/* Create a new Red/Black tree */
rbtree_tree *rbtree_create(tree_fcompare key_cmp,void *opt_data);

/* Delete an Red/Black tree */
void rbtree_delete(rbtree_tree *tree);

#endif
