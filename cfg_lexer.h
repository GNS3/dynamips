/*
 * Copyright (c) 2002-2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * cfg_lexer.h: A simple lexer for configuration files. 
 */

#ifndef __CFG_LEXER_H__
#define __CFG_LEXER_H__  1

#include "cfg_parser.h"

/* Maximum depth for files */
#define LEXER_MAX_FILES   64

/* Error base */
#define TOKEN_ERROR  128

/* Types of tokens */
enum {
   /* End of parsing */
   TOKEN_END = 0,

   /* Valid token types */
   TOKEN_WORD,
   TOKEN_SEMICOLON,
   TOKEN_BLOCK_START,
   TOKEN_BLOCK_END,
   TOKEN_EQUAL,

   /* Error codes returned by yylex() */
   TOKEN_ERR_CHAR = TOKEN_ERROR,
   TOKEN_ERR_STRING,
   TOKEN_ERR_COMMENT,
   TOKEN_ERR_MEMORY,
   TOKEN_ERR_FILE,
   TOKEN_ERR_INCLUDE_DEPTH,
   TOKEN_ERR_INCLUDE_PATH,
   TOKEN_ERR_CFG_VAR,
}token_types;

/* Initialize lexer */
int lexer_init(cfg_info_t *cfg_parser_info);

/* Open a file */
int lexer_open_file(char *filename);

/* Close a file */
int lexer_close_file(void);

/* Print unexpected EOF */
void token_print_unexp_eof(void);

/* Print an error prefixed by filename and line */
void token_error(char *fmt,...);

/* Consume a given type of token */
int token_consume_type(int type);

/* Consume a word */
char *token_consume_word(void);

/* Consume a token */
int token_consume(void);

/* Get current token contents */
char *token_get_value(void);

/* Set filename and line for specified node */
void token_set_file_info(cfg_node_t *node);

/* Environment file simple parser */
int token_read_envfile(char *filename);

#endif
