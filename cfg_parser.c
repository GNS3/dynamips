/*
 * Copyright (c) 2002-2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * cfg_parser.c: Simple parser for configuration files.
 */

static const char rcsid[] = "$Id$";

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "mempool.h"
#include "utils.h"
#include "net.h"
#include "cfg_lexer.h"
#include "cfg_parser.h"

#define DEBUG_PARSER 0

/* ---- Types recognized by parser ---------------------------------------- */

/* Type: ulong */
cfg_type_t cfg_type_ulong = {
   "unsigned long", parse_ulong, parser_print_ulong, NULL
};

/* Type: double */
cfg_type_t cfg_type_double = {
   "double", parse_double, parser_print_double, NULL
};

/* Type: range of values */
cfg_type_t cfg_type_range = {
   "range", parse_range, parser_print_range, NULL
};

/* Type: IP Protcol ('tcp','icmp',17,...) */
cfg_type_t cfg_type_ip_protocol = {
   "IP Protocol", parse_ip_protocol, parser_print_ip_protocol, NULL
};

/* Type: IPv4 address */
cfg_type_t cfg_type_ipaddr = {
   "IPv4 address", parse_ipaddr, parser_print_ipaddr, NULL
};

/* Type: IPv6 address */
cfg_type_t cfg_type_ipv6addr = {
   "IPv6 address", parse_ipv6addr, parser_print_ipv6addr, NULL
};

/* Type: IPv4 CIDR prefix */
cfg_type_t cfg_type_cidr = {
   "IPv4 CIDR prefix", parse_cidr, parser_print_cidr, NULL
};

/* Type: IPv6 CIDR prefix */
cfg_type_t cfg_type_cidrv6 = {
   "IPv6 CIDR Prefix", parse_cidrv6, parser_print_cidrv6, NULL
};

#if 0
/* Type: MAC Address (at form HHHH.HHHH.HHHH) */
cfg_type_t cfg_type_mac_addr = {
   "MAC address", parse_mac_addr, parser_print_mac_addr, NULL
};
#endif

/* Type: string */
cfg_type_t cfg_type_string = {
   "string", parse_string, parser_print_string, NULL
};

/* ---------------------------------------------------------------------- */

/* Parse a clause */
static int parse_clause(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item);

/* Parse an item */
static int parse_item(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item_list);


/* Parse an unsigned long */
int parse_ulong(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   u_long value;
   char *invalid;

   value = strtoul(token_get_value(),&invalid,0);
   if (*invalid == 0) {
      CFG_NODE_ULONG(node) = value;
      return(0);
   }

   return(-1);
}

/* Parse a double */
int parse_double(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   double value;
   char *invalid;

   value = strtod(token_get_value(),&invalid);
   if (*invalid == 0) {
      CFG_NODE_DOUBLE(node) = value;
      return(0);
   }

   return(-1);
}

/* Parse a range of values */
int parse_range(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   char *err,*sl,*tmp;
   int start,end,res = -1;

   /* Find separator */
   if ((sl = strchr(token_get_value(),'-')) == NULL)
   {
      /* We have a single value to parse */
      start = strtoul(token_get_value(),&err,0);
      if (*err != 0)
         return(-1);

      CFG_NODE_RANGE(node).start = start;
      CFG_NODE_RANGE(node).end = start;
      return(0);
   }

   /* We have a range */
   if ((tmp = strdup(token_get_value())) == NULL)
      return(-1);
   
   sl = strchr(tmp,'-');
   *sl = 0;

   /* Parse start value */
   start = strtoul(tmp,&err,0);
   if (*err != 0)
      goto free_tmp;
   
   /* Parse end value */
   end = strtoul(sl+1,&err,0);
   if (*err != 0)
      goto free_tmp;

   CFG_NODE_RANGE(node).start = start;
   CFG_NODE_RANGE(node).end = end;

   res = 0;
   
 free_tmp:
   free(tmp);
   return(res);
}

/* Parse a string specifying an IP protocol */
int parse_ip_protocol(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   struct protoent *p;
   u_long pnum;
   char *err;

   /* Try to find protocol number by its name */
   if ((p = getprotobyname(token_get_value())) != NULL) {
      CFG_NODE_ULONG(node) = p->p_proto;
      return(0);
   }

   /* Try to parse directly a number */
   pnum = strtoul(token_get_value(),&err,0);
   if (*err == 0) 
   {
      if (pnum >= IPPROTO_MAX)
         return(-1);

      CFG_NODE_ULONG(node) = pnum;
      return(0);
   }

   return(-1);
}

/* Parse an IPv4 Address */
int parse_ipaddr(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   n_ip_addr_t ip_addr;

   if (ip_aton(&ip_addr,token_get_value()) == -1)
      return(-1);

   CFG_NODE_IPADDR(node) = ip_addr;
   return(0);
}

/* Parse an IPv6 Address */
int parse_ipv6addr(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   return((ipv6_aton(&CFG_NODE_IPV6ADDR(node),
                     token_get_value()) <= 0)? -1 : 0);
}

/* Parse an IPv4 CIDR prefix */
int parse_cidr(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   return(ip_parse_cidr(token_get_value(),
                        &(CFG_NODE_CIDR(node).net_addr),
                        &(CFG_NODE_CIDR(node).net_mask)));
}

/* Parse an IPv6 CIDR prefix */
int parse_cidrv6(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   return(ipv6_parse_cidr(token_get_value(),
                          &(CFG_NODE_CIDRV6(node).net_addr),
                          &(CFG_NODE_CIDRV6(node).net_mask)));
}

#if 0
/* Parse a MAC address (HHHH.HHHH.HHHH) */
int parse_mac_addr(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   char *mac_address,*ptr,*err,*s;
   u_long val;
   int i = 0;

   if ((mac_address = strdup(token_get_value())) == NULL)
      return(-1);

   ptr = mac_address;
   while((s = rrt_strsplit2(&ptr,'.')) != NULL) 
   {
      if (i >= 3)
         goto parse_err;

      /* Convert into a binary form */
      val = strtoul(s,&err,16);
      if (*err != 0)
         goto parse_err;

      if (val > 0xFFFF)
         goto parse_err;

      CFG_NODE_MAC_ADDR(node).eth_addr_octet[i*2] = val >> 8;
      CFG_NODE_MAC_ADDR(node).eth_addr_octet[(i*2)+1] = val & 0xFF;
      i++;
   }

   free(mac_address);
   return(0);

 parse_err:
   free(mac_address);
   return(-1);
}
#endif

/* Parse a string */
int parse_string(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   CFG_NODE_STRING(node) = mp_dup_string(info->mp,token_get_value());

   if (!CFG_NODE_STRING(node))
      return(-1);

   return(0);
}

/* Parse a token from a list */
int parse_list(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   cfg_parser_list_t *list;
   cfg_type_t *type;

   type = (cfg_type_t *)item->action;
   list = (cfg_parser_list_t *)type->arg;
  
   while(list->name != NULL) {
      if (!strcmp(list->name,token_get_value())) {
         CFG_NODE_ULONG(node) = list->value;
         return(0);
      }
      list++;
   }
   return(-1);
}

/* Print an unsigned long */
void parser_print_ulong(cfg_node_t *node,cfg_type_t *type)
{
   printf("%lu",CFG_NODE_ULONG(node));
}

/* Print a double */
void parser_print_double(cfg_node_t *node,cfg_type_t *type)
{
   printf("%g",CFG_NODE_DOUBLE(node));
}

/* Print a range of values */
void parser_print_range(cfg_node_t *node,cfg_type_t *type)
{
   u_long start,end;

   start = CFG_NODE_RANGE(node).start;
   end = CFG_NODE_RANGE(node).end;

   if (start == end) 
      printf("%lu",start);
   else
      printf("%lu-%lu",start,end);
}

/* Print an IP protocol */
void parser_print_ip_protocol(cfg_node_t *node,cfg_type_t *type)
{
   /* Just print number */
   printf("%lu",CFG_NODE_ULONG(node));
}

/* Print an IPv4 Address */
void parser_print_ipaddr(cfg_node_t *node,cfg_type_t *type)
{
   char buffer[32];
   printf("%s",ip_ntoa(buffer,node->value.val_ipaddr));
}

/* Print an IPv6 Address */
void parser_print_ipv6addr(cfg_node_t *node,cfg_type_t *type)
{
   char buffer[INET6_ADDRSTRLEN+1];
   printf("%s",ipv6_ntoa(buffer,&CFG_NODE_IPV6ADDR(node)));
}

/* Print a CIDR prefix */
void parser_print_cidr(cfg_node_t *node,cfg_type_t *type)
{
   char buffer[32];

   printf("%s/%d",
          ip_ntoa(buffer,node->value.val_cidr.net_addr),
          ip_bits_mask(node->value.val_cidr.net_mask));
}

/* Print an IPv6 CIDR prefix */
void parser_print_cidrv6(cfg_node_t *node,cfg_type_t *type)
{
   char buffer[INET6_ADDRSTRLEN+1];
   
   printf("%s/%d",
          ipv6_ntoa(buffer,&(CFG_NODE_CIDRV6(node).net_addr)),
          CFG_NODE_CIDRV6(node).net_mask);
}

/* Print a MAC address */
void parser_print_mac_addr(cfg_node_t *node,cfg_type_t *type)
{
   n_eth_addr_t *addr;

   addr = &CFG_NODE_MAC_ADDR(node);

   printf("%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x",
          addr->eth_addr_byte[0],addr->eth_addr_byte[1],
          addr->eth_addr_byte[2],addr->eth_addr_byte[3],
          addr->eth_addr_byte[4],addr->eth_addr_byte[5]);
}

/* Print a string */
void parser_print_string(cfg_node_t *node,cfg_type_t *type)
{
   printf("%s",node->value.val_string);
}

/* Print a token from a list */
void parser_print_list(cfg_node_t *node,cfg_type_t *type)
{
   cfg_parser_list_t *l,*list = (cfg_parser_list_t *)type->arg;

   for(l=list;l->name!=NULL;l++)
      if (l->value == CFG_NODE_ULONG(node)) {
         printf("%s",l->name);
         break;
      }  
}

/* ---------------------------------------------------------------------- */

static char *prefix_array[] = {
   NULL,
   CFG_PREFIX_NO,
   CFG_PREFIX_CLEAR,
   CFG_PREFIX_SHOW,
   NULL,
};

/* Is the current word a prefix ? */
static int word_is_prefix(void)
{
   int i;

   for(i=1;prefix_array[i];i++)
      if (!strcmp(token_get_value(),prefix_array[i]))
         return(i);

   return(0);
}

/* Find a keyword in an array of items, with a prefix */
static cfg_item_t *parser_find_keyword(cfg_item_t *items,
                                       int prefix,char *keyword)
{
   int ipref;

   for(;items->flags!=CFG_FLAG_INVALID;items++)
   {
      ipref = (items->flags & CFG_FLAG_PREFIX) ? items->prefix : 0;

      if (ipref != prefix)
         continue;

      if (!strcmp(items->keyword,keyword))
         return items;
   }

   return NULL;
}

/* Create a new parser node and store keyword */
cfg_node_t *parser_create_key_node(mempool_t *mp,char *keyword)
{
   cfg_node_t *node;

   if (!(node = mp_alloc(mp,sizeof(*node))))
      return NULL;

   if (!(node->keyword = mp_dup_string(mp,keyword)))
      return NULL;

   return node;
}

/* Create a new string node */
cfg_node_t *parser_create_str_node(mempool_t *mp,char *keyword,char *str)
{
   cfg_node_t *node;

   if (!(node = parser_create_key_node(mp,keyword)))
      return NULL;

   if (!(CFG_NODE_STRING(node) = mp_dup_string(mp,str)))
      return NULL;

   node->type = &cfg_type_string;
   return node;
}

/* Create a new u_long node */
cfg_node_t *parser_create_ulong_node(mempool_t *mp,char *keyword,u_long value)
{
   cfg_node_t *node;

   if (!(node = parser_create_key_node(mp,keyword)))
      return NULL;

   node->type = &cfg_type_ulong;
   CFG_NODE_ULONG(node) = value;
   return node;
}

/* Create a new parser node and store keyword */
static cfg_node_t *parser_create_node(cfg_info_t *info,char *keyword)
{
   cfg_node_t *node;

   node = parser_create_key_node(info->mp,keyword);
   if (node) token_set_file_info(node);
   return node;
}

/* Find a node given a keyword */
cfg_node_t *parser_find_node(cfg_node_t *node,char *keyword)
{
   cfg_node_t *iterator;

   cfg_node_foreach(iterator,node)
      if (!strcmp(iterator->keyword,keyword))
         return iterator;
   
   return NULL;
}

/* Get a string from specified node */
char *parser_get_string(cfg_node_t *node,char *keyword,cfg_node_t **term)
{
   cfg_node_t *val = parser_find_node(node,keyword);
   
   if (val) {
      if (term) *term = val;
      return CFG_NODE_STRING(val);
   } else
      return NULL;
}

/* Insert a node in sub-list */
void parser_insert_node(cfg_node_t *node,cfg_node_t *subnode)
{
   subnode->next = NULL;
   subnode->parent = node;

   if (node->snode_first == NULL)
      node->snode_first = node->snode_last = subnode;
   else {
      node->snode_last->next = subnode;
      node->snode_last = subnode;
   }
}

/* Print spaces given level */
static void parser_print_spaces(int level)
{
   int i;
   
   for(i=0;i<level;i++)
      printf("   ");
}

/* Print nodes */
static void parser_print_node(cfg_node_t *node,int level,int file_info)
{
   cfg_node_t *subnode;

   /* print a clause */
   if (node->type == NULL)
   {
      parser_print_spaces(level);

      if (CFG_NODE_STRING(node) != NULL)
      {
         if (file_info)
            printf("%s %s { ! [%s:%d]\n",
                    node->keyword,CFG_NODE_STRING(node),
                    node->filename,node->line);
         else
            printf("%s %s {\n",node->keyword,CFG_NODE_STRING(node));
      }
      else
      {
         if (file_info)
            printf("%s { ! [%s:%d]\n",node->keyword,node->filename,node->line);
         else
            printf("%s {\n",node->keyword);
      }

      cfg_node_foreach(subnode,node)
         parser_print_node(subnode,level+1,file_info);

      parser_print_spaces(level);
      printf("};\n");
      return;
   }

   /* we want to print a keyword/value pair */
   parser_print_spaces(level);

   printf("%s ",node->keyword);

   /* if we have a list, expand it */
   if (node->snode_first != NULL) 
   {
      printf("{\n"); 

      cfg_node_foreach(subnode,node)
      {
         parser_print_spaces(level+1);
         subnode->type->print(subnode,subnode->type);

         if (file_info)
            printf("; ! [%s:%d]\n",subnode->filename,subnode->line);
         else
            printf(";\n");
      }

      parser_print_spaces(level);
      printf("};\n");
   }
   else {
      node->type->print(node,node->type);

      if (file_info)
         printf("; ! [%s:%d]\n",node->filename,node->line);
      else
         printf(";\n");
   }
}

/* Print node hierarchy */
void parser_print_hierarchy(cfg_node_t *root,int file_info)
{
   cfg_node_t *node;

   cfg_node_foreach(node,root)
      parser_print_node(node,0,file_info);
}

/* Execute an action for each node matching specified keyword */
void parser_foreach_node(cfg_node_t *node,void (*iterator)(cfg_node_t *node))
{
   cfg_node_t *subnode;

   cfg_node_foreach(subnode,node)
      iterator(subnode);
}

/* Parse a type */
static int parse_type(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   cfg_type_t *type = (cfg_type_t *)item->action;

   if (!type->parse) {
      token_error("Parser Error: parse_type called with no action in item.");
      return(-1);
   }

   if (type->parse(info,node,item) == -1)
      return(-1);

   node->type = type;
   node->keyword = item->keyword;       /* no need to duplicate this */
   return(0);
}

/* Parse a single value for a list */
static int parse_single_value(cfg_info_t *info,cfg_node_t *node,
                              cfg_item_t *item)
{
   cfg_node_t *new_node;

   /* Create a new node to store this value */
   if ((new_node = parser_create_node(info,item->keyword)) == NULL) {
      token_error("unable to create new node for keyword '%s'.",item->keyword);
      return(-1);
   }
   
   /* Current token contains the value */
   if (parse_type(info,new_node,item) == -1) {
      token_error("unable to parse value for keyword '%s'.",item->keyword);
      return(-1);
   }

   parser_insert_node(node,new_node);

   /* Now, we need to have a semi-colon */
   if (token_consume_type(TOKEN_SEMICOLON) == -1)
      return(-1);

   return(0);
}

/* Parse a list of values */
static int parse_list_values(cfg_info_t *info,cfg_node_t *node,
                             cfg_item_t *item)
{
   cfg_node_t *subnode;
   int token_id;

   for(;;)
   {
      if ((token_id = token_consume()) == -1)
         return(-1);

      /* if we are at EOF, this is an error */
      if (token_id == TOKEN_END) {
         token_print_unexp_eof();
         return(-1);
      }

      /* if we have an end of block, quit */
      if (token_id == TOKEN_BLOCK_END)
         break;

      if ((subnode = parser_create_node(info,item->keyword)) == NULL) {
         token_error("unable to create new node for keyword '%s'.",
                     item->keyword);
         return(-1);
      }

      /* we have a "no" keyword to remove an item from the list */
      if (!strcmp(token_get_value(),CFG_PREFIX_NO))
      {
         subnode->prefix = CFG_NODE_PREFIX_NO;
         
         if (!token_consume_word()) {
            token_error("expected a value.");
            return(-1);
         }
      }

      /* parse value */
      if (parse_type(info,subnode,item) == -1) {
         token_error("unable to parse %s value.",
                     ((cfg_type_t *)item->action)->name);
         return(-1);
      }

      parser_insert_node(node,subnode);

      /* now, we need to have a semi-colon */
      if (token_consume_type(TOKEN_SEMICOLON) == -1)
         return(-1);
   }

   return(0);
}

/* Parse a value */
static int parse_value(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item,
                       int prefix)
{
   cfg_node_t *subnode;
   int token_id;

   /* Check that keyword is not already present with the same prefix */
   if (parser_find_node(node,item->keyword) && (node->prefix == prefix)) {
      token_error("keyword '%s' already defined.",item->keyword);
      return(-1);
   }

   /* Create a new node */
   if (!(subnode = parser_create_node(info,item->keyword))) {
      token_error("unable to create new node '%s'.",item->keyword);
      return(-1);
   }

   subnode->prefix = prefix;

   /* 
    * If we have a list, parse each element and add it in node.
    * 
    * With a prefix, like "no", we cannot have a list (however, we can 
    * negate values within the list itself).
    */
   if (item->flags & CFG_FLAG_LIST)
   {
      /* internal error, this is not allowed */
      if (subnode->prefix) {
         token_error("internal error: list of values are not allowed "
                     "with \"%s\" keyword (keyword '%s').",
                     prefix_array[prefix],item->keyword);
         return(-1);
      }

      parser_insert_node(node,subnode);
      subnode->type = (cfg_type_t *)item->action;

      if ((token_id = token_consume()) == -1)
         return(-1);

      /* Parse a list */
      if (token_id == TOKEN_BLOCK_START)
         return(parse_list_values(info,subnode,item));
      
      /* Parse a single value */
      if (token_id == TOKEN_WORD)
         return(parse_single_value(info,subnode,item));

      /* We have a bad token type as value */
      token_error("expected a value after keyword '%s'.",item->keyword);
      return(-1);
   }

   /* 
    * We have a single value (item does not accept a list of values).
    *
    * If we don't have an action, consider this is a keyword not 
    * followed by a value.
    */
   if (item->action)
   {
      if (!token_consume_word())
         return(-1);

      if (parse_type(info,subnode,item) == -1) {
         token_error("unable to parse value for keyword '%s'.",item->keyword);
         return(-1);
      }
   }

   parser_insert_node(node,subnode);

   /* Now, we need to have a semi-colon */
   if (token_consume_type(TOKEN_SEMICOLON) == -1)
      return(-1);

   return(0);
}

/* Parse a clause */
static int parse_clause(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item)
{
   cfg_node_t *subnode = NULL,*iterator;
   int new_node = FALSE;
   char *value;

   /* If we must have a value, consume a word or a string */
   if (!(item->flags & CFG_FLAG_NOVALUE))
   {
      if (!token_consume_word())
         return(-1);

      /* Save value */
      value = token_get_value();

      if (item->flags & CFG_FLAG_MULTI) 
      {
         cfg_node_foreach(iterator,node)
            if (!strcmp(iterator->keyword,item->keyword) &&
                !strcmp(CFG_NODE_STRING(iterator),value) &&
                !iterator->prefix)
            {
               subnode = iterator;
               break;
            }
      }
      else
         subnode = parser_find_node(node,item->keyword);

      if (!subnode)
      {
         if (!(subnode = parser_create_node(info,item->keyword))) {
            token_error("unable to create new node for clause '%s'",
                        item->keyword);
            return(-1);
         }

         if (!(CFG_NODE_STRING(subnode) = mp_dup_string(info->mp,value))) {
            token_error("unable to get memory for clause name (%s)",
                        token_get_value());
            return(-1);
         }

         new_node = TRUE;
      }
   }
   else
   {
      if (!(item->flags & CFG_FLAG_MULTI))
         subnode = parser_find_node(node,item->keyword);

      if (!subnode)
      {
         if (!(subnode = parser_create_node(info,item->keyword))) {
            token_error("unable to create new node for clause '%s'",
                        item->keyword);
            return(-1);
            
         }
         new_node = TRUE;
      }
   }

   if (new_node)
      parser_insert_node(node,subnode);

   /* Now, consume block start "{" */
   if (token_consume_type(TOKEN_BLOCK_START) == -1)
      return(-1);

   return(parse_item(info,subnode,(cfg_item_t *)item->action));
}

/* Parse an item */
static int parse_item(cfg_info_t *info,cfg_node_t *node,cfg_item_t *item_list)
{
   cfg_item_t *item;
   int token_id,res;
   int prefix;

   for(;;)
   {
      /* Consume token (and print error found by Lex if necessary) */
      if ((token_id = token_consume()) == -1)
         return(-1);

      /* If we have no token anymore, ensure that we are not in a clause */
      if (!token_id) {
         if (node->parent) {
            token_print_unexp_eof();
            return(-1);
         }

         return(0);
      }
      
      /* If we have an end of block "};", returns to upper level */
      if (token_id == TOKEN_BLOCK_END) 
      {
         if (!node->parent) {
            token_error("unexpected block end.");
            return(-1);
         }

         return(0);
      }

      /* Now, we need to have a keyword */
      if (token_id != TOKEN_WORD) {
         token_error("a word was expected.");
         return(-1);
      }
      
      /* Do we have a special prefix ("no", "show", etc) ? */
      if (!(prefix = word_is_prefix()))
      {
         item = parser_find_keyword(item_list,0,token_get_value());

         /* standard statement: search keyword in current item list */
         if (!item) {
            token_error("unknown keyword '%s'.",token_get_value());
            return(-1);
         }

         /* execute action specified by flags */
         if (item->flags & CFG_FLAG_CLAUSE)
            res = parse_clause(info,node,item);
         else
            res = parse_value(info,node,item,0);

         if (res == -1)
            return(-1);
      } 
      else 
      {
         /* 
          * we have a prefix, skip it and read the keyword following it. 
          */
         if (!token_consume_word())
            return(-1);
         
         item = parser_find_keyword(item_list,prefix,token_get_value());
        
         if (!item) {
            token_error("unknown keyword '%s' with prefix \"%s\".",
                        token_get_value(),prefix_array[prefix]);
            return(-1);
         }

         /* parse a value (CLAUSES are not allowed here) */
         if (parse_value(info,node,item,prefix) == -1)
            return(-1);
      }
   }

   return(0);
}

/* Free all memory used by parser */
void parser_free(cfg_info_t *parse_info)
{
   /* Free memory pools and all blocks hold by it */
   if (parse_info && parse_info->mp)
      mp_free(parse_info->mp);
}

/* Initialize parser */
int parser_init(cfg_info_t *parse_info)
{
   /* Clean all for safety */
   memset(parse_info,0,sizeof(cfg_info_t));
   
   /* Create a new mempool */
   if (!(parse_info->mp = mp_create_pool("Configuration Parser")))
      return(-1);
   
   /* Initialize lexer */
   if (lexer_init(parse_info) == -1)
      return(-1);

   return(0);
}

/* Parse a file */
int parser_read_file(cfg_info_t *parse_info,char *filename,cfg_item_t *root)
{
   int res;

   /* Initialize parser */
   if (parser_init(parse_info) == -1) {
      fprintf(stderr,"Config Parser: unable to initialize parser.\n");
      return(-1);
   }

   /* Open file */
   if (lexer_open_file(filename)) {
      fprintf(stderr,"Config Parser: unable to read file '%s'\n",filename);
      return(-1);
   }

   /* Parse file */
   parse_info->root_item = root;
   res = parse_item(parse_info,&parse_info->root_node,parse_info->root_item);

#if DEBUG_PARSER
   /* Only for debugging purposes */
   parser_print_hierarchy(&parse_info->root_node,TRUE);
#endif
   
   return(res);
}
