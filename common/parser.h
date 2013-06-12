/*
 * Cisco router simulation platform.
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

/* Parser states */
enum {   
   PARSER_STATE_DONE,
   PARSER_STATE_SKIP,
   PARSER_STATE_BLANK,
   PARSER_STATE_STRING,
   PARSER_STATE_QUOTED_STRING,
};

/* Token */
typedef struct parser_token parser_token_t;
struct parser_token {
   char *value;
   struct parser_token *next;
};

/* Parser context */
typedef struct parser_context parser_context_t;
struct parser_context {
   /* Token list */
   parser_token_t *tok_head,*tok_last;
   int tok_count;

   /* Temporary token */
   char *tmp_tok;
   size_t tmp_tot_len,tmp_cur_len;

   /* Parser state and error */
   int state,error;

   /* Number of consumed chars */
   size_t consumed_len;
};

/* Get a description given an error code */
char *parser_strerror(parser_context_t *ctx);

/* Dump a token list */
void parser_dump_tokens(parser_context_t *ctx);

/* Map a token list to an array */
char **parser_map_array(parser_context_t *ctx);

/* Initialize parser context */
void parser_context_init(parser_context_t *ctx);

/* Free memory used by a parser context */
void parser_context_free(parser_context_t *ctx);

/* Send a buffer to the tokenizer */
int parser_scan_buffer(parser_context_t *ctx,char *buf,size_t buf_size);

/* Tokenize a string */
int parser_tokenize(char *str,struct parser_token **tokens,int *tok_count);

#endif
