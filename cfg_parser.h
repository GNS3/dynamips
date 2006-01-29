/*
 * Copyright (c) 2002-2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * cfg_parser.h: Simple parser fo configuration files.
 */

#ifndef __CFG_PARSER_H__
#define __CFG_PARSER_H__  1

#include "mempool.h"
#include "net.h"
#include "utils.h"

/* Prefixes for a node */
#define CFG_PREFIX_NO    "no"     /* used with CFG_FLAG_NEGATE */
#define CFG_PREFIX_CLEAR "clear"  /* not used yet */
#define CFG_PREFIX_SHOW  "show"   /* not used yet */

enum {
   CFG_NODE_PREFIX_NO = 1,
   CFG_NODE_PREFIX_CLEAR,
   CFG_NODE_PREFIX_SHOW,
};

/* Forward declarations... */
typedef struct cfg_type cfg_type_t;
typedef struct cfg_item cfg_item_t;
typedef struct cfg_node cfg_node_t;
typedef struct cfg_info cfg_info_t;

/* Parser list item */
typedef struct cfg_parser_list {
   char *name;
   int value;
}cfg_parser_list_t;

/* Parser Type */
struct cfg_type {
   char *name;
   
   /* 
    * A type has two requirements: 
    *
    *    - A parse function, used to convert token into an usable node value
    *      (see "value" union in "struct cfg_node").
    *
    *    - A print function, mainly for debugging.
    */
   int (*parse)(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);
   void (*print)(cfg_node_t *node,cfg_type_t *type);

   /* User argument */
   void *arg;
};

/* Flags for an item */
#define CFG_FLAG_INVALID   -1
#define CFG_FLAG_CLAUSE    0x01
#define CFG_FLAG_MULTI     0x02  /* Element can be present multiple times */
#define CFG_FLAG_LIST      0x04  /* Value can be a list */
#define CFG_FLAG_NOVALUE   0x08  /* No value is mandatory */
#define CFG_FLAG_PREFIX    0x10  /* Prefixed command */

/* Parser item */
struct cfg_item {
   char *keyword;   /* Name of item ("router") */
   int flags;       /* Flags */
   void *action;    /* Sense is given by flags */
   int prefix;
};

/* Node */
struct cfg_node {
   char *keyword;
   int prefix;
   char *filename;
   int line;
   cfg_type_t *type;

   union {
      u_long val_ulong;
      double val_double;

      struct {
         u_long start,end;
      }val_range;

      char *val_string;             /* String */
      n_ip_addr_t val_ipaddr;       /* IPv4 Address */
      n_ipv6_addr_t val_ipv6addr;   /* IPv6 Address */
      n_ip_network_t val_cidr;      /* IPv4 CIDR Prefix */
      n_ipv6_network_t val_cidrv6;  /* IPv6 CIDR Prefix */
      n_eth_addr_t val_macaddr;     /* MAC Address */
      void *val_utype;              /* Unspecified Type */
   }value;

   cfg_node_t *parent;                     /* Parent node */
   cfg_node_t *snode_first,*snode_last;    /* List of sub-nodes */
   cfg_node_t *next;                       /* Next node */
};

#define CFG_NODE_ULONG(node)     ((node)->value.val_ulong)
#define CFG_NODE_DOUBLE(node)    ((node)->value.val_double)
#define CFG_NODE_RANGE(node)     ((node)->value.val_range)
#define CFG_NODE_STRING(node)    ((node)->value.val_string)
#define CFG_NODE_IPADDR(node)    ((node)->value.val_ipaddr)
#define CFG_NODE_IPV6ADDR(node)  ((node)->value.val_ipv6addr)
#define CFG_NODE_CIDR(node)      ((node)->value.val_cidr)
#define CFG_NODE_CIDRV6(node)    ((node)->value.val_cidrv6)
#define CFG_NODE_MAC_ADDR(node)  ((node)->value.val_macaddr)
#define CFG_NODE_UTYPE(node)     ((node)->value.val_utype)
#define CFG_NODE_DATE(node)      ((node)->value.val_date.date)

#define CFG_TYPE_LIST(name,desc,list) \
   cfg_type_t cfg_type_##name = { \
      (desc), parse_list, parser_print_list, (list) \
   }

#define cfg_node_foreach(iterator,node) \
   if ((node) != NULL) \
      for((iterator)=(node)->snode_first;(iterator); \
          (iterator)=(iterator)->next)

#define cfg_node_foreach_keyword(iterator,node,key) \
   for((iterator)=(node)->snode_first;(iterator);(iterator)=(iterator)->next) \
      if (!strcmp((key),(iterator)->keyword))

static inline int cfg_node_prefix_none(cfg_node_t *node)
{
   return(node->prefix == 0);
}

static inline int cfg_node_prefix_negate(cfg_node_t *node)
{
   return(node->prefix == CFG_NODE_PREFIX_NO);
}


/*
 * The following structure keeps all important info used by parser:
 *    - Memory pool holding nodes, strings, etc.
 *    - Hash table for filenames used (with include, etc.)
 *    - The root node.
 */
struct cfg_info {
   mempool_t *mp;           /* Memory pool */
   cfg_item_t *root_item;   /* Root item */
   cfg_node_t root_node;    /* Root node */
};

/* Common types */
extern cfg_type_t cfg_type_ulong;
extern cfg_type_t cfg_type_double;
extern cfg_type_t cfg_type_range;
extern cfg_type_t cfg_type_ip_protocol;
extern cfg_type_t cfg_type_ipaddr;
extern cfg_type_t cfg_type_ipv6addr;
extern cfg_type_t cfg_type_cidr;
extern cfg_type_t cfg_type_cidrv6;
extern cfg_type_t cfg_type_mac_addr;
extern cfg_type_t cfg_type_string;
extern cfg_type_t cfg_type_date;
extern cfg_type_t cfg_type_time;

/* Parse an unsigned long */
int parse_ulong(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Parse a double */
int parse_double(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Parse a range of values */
int parse_range(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Parse an IP protocol */
int parse_ip_protocol(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Parse an IPv4 Address */
int parse_ipaddr(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Parse an IPv6 Address */
int parse_ipv6addr(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Parse an IPv4 CIDR Prefix */
int parse_cidr(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Parse an IPv6 CIDR Prefix */
int parse_cidrv6(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Parse a MAC address */
//int parse_mac_addr(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Parse a string */
int parse_string(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Parse a token in a list */
int parse_list(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Print an unsigned long */
void parser_print_ulong(cfg_node_t *node,cfg_type_t *type);

/* Print a double */
void parser_print_double(cfg_node_t *node,cfg_type_t *type);

/* Print a range of values */
void parser_print_range(cfg_node_t *node,cfg_type_t *type);

/* Print IP Protocol */
void parser_print_ip_protocol(cfg_node_t *node,cfg_type_t *type);

/* Print an IPv4 Address */
void parser_print_ipaddr(cfg_node_t *node,cfg_type_t *type);

/* Print an IPv6 Address */
void parser_print_ipv6addr(cfg_node_t *node,cfg_type_t *type);

/* Print an IPv4 CIDR prefix */
void parser_print_cidr(cfg_node_t *node,cfg_type_t *type);

/* Print an IPv6 CIDR prefix */
void parser_print_cidrv6(cfg_node_t *node,cfg_type_t *type);

/* Print a MAC address */
void parser_print_mac_addr(cfg_node_t *node,cfg_type_t *type);

/* Print a string */
void parser_print_string(cfg_node_t *node,cfg_type_t *type);

/* Print a token from a list */
void parser_print_list(cfg_node_t *node,cfg_type_t *type);

/* Create a new parser node and store keyword */
cfg_node_t *parser_create_key_node(mempool_t *mp,char *keyword);

/* Create a new string node */
cfg_node_t *parser_create_str_node(mempool_t *mp,char *keyword,char *str);

/* Create a new u_long node */
cfg_node_t *parser_create_ulong_node(mempool_t *mp,char *keyword,u_long value);

/* Find a node given a keyword */
cfg_node_t *parser_find_node(cfg_node_t *node,char *keyword);

/* Get a string from specified node */
char *parser_get_string(cfg_node_t *node,char *keyword,cfg_node_t **term);

/* Insert a node in sub-list */
void parser_insert_node(cfg_node_t *node,cfg_node_t *subnode);

/* Print node hierarchy */
void parser_print_hierarchy(cfg_node_t *root,int file_info);

/* Free all memory used by parser */
void parser_free(cfg_info_t *parse_info);

/* Initialize parser */
int parser_init(cfg_info_t *parse_info);

/* Parse a file */
int parser_read_file(cfg_info_t *parse_info,char *filename,cfg_item_t *root);

#endif
