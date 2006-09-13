/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Mini-parser.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "utils.h"
#include "parser.h"

#define TOKEN_MAX_SIZE  512

/* Parser states */
enum {
   PARSER_STATE_BLANK,
   PARSER_STATE_STRING,
   PARSER_STATE_QUOTED_STRING,
};

/* Character types */
enum {
   PARSER_CHAR_BLANK,
   PARSER_CHAR_NEWLINE,
   PARSER_CHAR_COMMENT,
   PARSER_CHAR_QUOTE,
   PARSER_CHAR_OTHER,
};

/* Get a description given an error code */
char *parser_strerror(int error)
{
   printf("error = %d\n",error);

   switch(error) {
      case PARSER_ERROR_NOMEM:
         return "insufficient memory";
      case PARSER_ERROR_UNEXP_QUOTE:
         return "unexpected quote";
      case PARSER_ERROR_UNEXP_EOL:
         return "unexpected end of line";
      default:
         return "unknown error";
   }
}

/* Create a new token */
static int token_create(parser_token_t **head,parser_token_t **last,
                        char *value)
{
   parser_token_t *t;

   if (!(t = malloc(sizeof(*t))))
      return(-1);

   if (!(t->value = strdup(value))) {
      free(t);
      return(-1);
   }

   t->next = NULL;

   if (*last) {
      (*last)->next = t;
      *last = t;
   } else {
      *head = *last = t;
   }

   return(0);
}

/* Free a token list */
void parser_free_tokens(parser_token_t *tok_list)
{
   parser_token_t *t,*next;
   
   for(t=tok_list;t;t=next) {
      next = t->next;
      free(t->value);
      free(t);
   }
}

/* Dump a token list */
void parser_dump_tokens(parser_token_t *tok_list)
{
   parser_token_t *t;

   for(t=tok_list;t;t=t->next)
      printf("\"%s\" ",t->value);
}

/* Map a token list to an array */
char **parser_map_array(parser_token_t *tok_list,int tok_count)
{
   char **map;
   int i;

   if (tok_count <= 0)
      return NULL;

   if (!(map = calloc(tok_count,sizeof(char **))))
      return NULL;

   for(i=0;(i<tok_count) && tok_list;i++,tok_list=tok_list->next)
      map[i] = tok_list->value;

   return map;
}

/* Add a character to a buffer */
static int buffer_add_char(char *buffer,int *pos,char c)
{
   if (*pos >= TOKEN_MAX_SIZE)
      return(-1);

   buffer[(*pos)++] = c;
   buffer[*pos] = 0;
   return(0);
}

/* Tokenize a string */
int parser_tokenize(char *str,struct parser_token **tokens,int *tok_count)
{
   char buffer[TOKEN_MAX_SIZE+1];
   parser_token_t *tok_head,*tok_last;
   int i,buf_pos,type;
   int state,error,done;
   size_t len;
   char c;

   len        = strlen(str);
   tok_head   = tok_last = NULL;
   *tokens    = NULL;
   *tok_count = 0;
   state      = PARSER_STATE_BLANK;
   done       = FALSE;
   error      = 0;
   buf_pos    = 0;

   for(i=0;(i<len+1) && !error && !done;i++)
   {
      c = str[i];
      
      /* Determine character type */
      switch(c) {
         case '\n':
         case '\r':
         case 0:
            type = PARSER_CHAR_NEWLINE;
            break;
         case '\t':
         case ' ':
            type = PARSER_CHAR_BLANK;
            break;
         case '!':
         case '#':
            type = PARSER_CHAR_COMMENT;
            break;
         case '"':
            type = PARSER_CHAR_QUOTE;
            break;
         default:
            type = PARSER_CHAR_OTHER;
      }

      /* Basic finite state machine */
      switch(state) {
         case PARSER_STATE_BLANK:
            switch(type) {
               case PARSER_CHAR_BLANK:
                  /* Eat space */
                  break;

               case PARSER_CHAR_NEWLINE:
               case PARSER_CHAR_COMMENT:
                  done = TRUE;
                  break;

               case PARSER_CHAR_QUOTE:
                  state = PARSER_STATE_QUOTED_STRING;
                  buf_pos = 0;
                  break;
                  
               default:
                  /* Begin a new string */
                  state = PARSER_STATE_STRING;                  
                  buf_pos = 0;
                  buffer_add_char(buffer,&buf_pos,c);
            }
            break;

         case PARSER_STATE_STRING:
            switch(type) {
               case PARSER_CHAR_BLANK:
                  if (token_create(&tok_head,&tok_last,buffer) == -1)
                     error = PARSER_ERROR_NOMEM;

                  (*tok_count)++;
                  state = PARSER_STATE_BLANK;
                  break;

               case PARSER_CHAR_NEWLINE:
                  if (token_create(&tok_head,&tok_last,buffer) == -1)
                     error = PARSER_ERROR_NOMEM;

                  (*tok_count)++;
                  done = TRUE;
                  break;

               case PARSER_CHAR_COMMENT:
                  done = TRUE;
                  break;

               case PARSER_CHAR_QUOTE:
                  error = PARSER_ERROR_UNEXP_QUOTE;
                  break;

               default:
                  /* Add the character to the buffer */
                  buffer_add_char(buffer,&buf_pos,c);
            }
            break;

         case PARSER_STATE_QUOTED_STRING:
            switch(type) {
               case PARSER_CHAR_NEWLINE:
                  /* Unterminated string! */
                  error = PARSER_ERROR_UNEXP_EOL;
                  break;

               case PARSER_CHAR_QUOTE:
                  if (token_create(&tok_head,&tok_last,buffer) == -1)
                     error = PARSER_ERROR_NOMEM;

                  (*tok_count)++;
                  state = PARSER_STATE_BLANK;
                  break;

               default:
                  /* Add the character to the buffer */
                  buffer_add_char(buffer,&buf_pos,c);
            }
            break;
      }
   }

   if (error) {
      parser_free_tokens(tok_head);
      return(error);
   }

   *tokens = tok_head;
   return(0);
}

/* Parser tests */
static char *parser_test_str[] = {
   "c7200 show_hardware R1",
   "c7200 show_hardware \"R1\"",
   "   c7200    show_hardware   \"R1\"    ",
   "\"c7200\" \"show_hardware\" \"R1\"",
   "hypervisor set_working_dir \"C:\\Program Files\\Dynamips Test\"",
   "hypervisor # This is a comment set_working_dir \"C:\\Program Files\"",
   "\"c7200\" \"show_hardware\" \"R1",
   NULL,
};

void parser_run_tests(void)
{
   struct parser_token *tok_list;
   int i,res,tok_count;

   for(i=0;parser_test_str[i];i++) {
      res = parser_tokenize(parser_test_str[i],&tok_list,&tok_count);

      printf("\n%d: Test string: [%s] => res=%d\n",
             i,parser_test_str[i],res);
      
      if (tok_list) {
         printf("Tokens: ");
         parser_dump_tokens(tok_list);
         printf("\n");

         parser_free_tokens(tok_list);
      }
   }
}
