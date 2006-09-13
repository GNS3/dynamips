/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __PARSER_H__
#define __PARSER_H__

#include <sys/types.h>

/* Parser Errors */
enum {
   PARSER_ERROR_NOMEM = 1,
   PARSER_ERROR_UNEXP_QUOTE,    /* Unexpected quote in a word */
   PARSER_ERROR_UNEXP_EOL,      /* Unexpected end of line */
};

/* Token */
typedef struct parser_token parser_token_t;
struct parser_token {
   char *value;
   struct parser_token *next;
};

/* Get a description given an error code */
char *parser_strerror(int error);

/* Free a token list */
void parser_free_tokens(parser_token_t *tok_list);

/* Dump a token list */
void parser_dump_tokens(parser_token_t *tok_list);

/* Map a token list to an array */
char **parser_map_array(parser_token_t *tok_list,int tok_count);

/* Tokenize a string */
int parser_tokenize(char *str,struct parser_token **tokens,int *tok_count);

#endif
