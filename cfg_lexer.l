%{
/*
 * Copyright (c) 2002-2006 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * A simple lexer for configuration files. 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>

#include "utils.h"
#include "mempool.h"
#include "cfg_lexer.h"
#include "cfg_parser.h"

#define DEBUG_LEXER  0

/* Descriptions of token types */
static const char *token_descriptions[] = {
   "End-Of-File (EOF)", "word", "semicolon", 
   "block start '{'", "block end '}'",
};

/* Description of a file being read by lexer */
typedef struct lexer_file lexer_file_t;
struct lexer_file {
   YY_BUFFER_STATE buffer;
   char *filename;
   int line; 
};

/* Files read by lexer */
static lexer_file_t *lexer_files = NULL;
static int lexer_file_pos = 0;

/* Current file being read */
static lexer_file_t *lexer_cfile = NULL;

/* Temporary buffer */
#define STRING_BUF_SIZE  4096
static char *string_buf,*string_buf_ptr;

/* Current token string */
static char *token_cstring = NULL;

/* Callers */
static int comment_caller,include_caller;
static int string_caller;

/* Parser info, used for memory pool */
static cfg_info_t *parser_info = NULL;

/* Increment line number of current file */
static inline void lexer_inc_line(void)
{
   lexer_cfile->line++;
}

/* Add a character into temporary string buffer */
static inline void lexer_add_tchar(char c)
{
   size_t clen = string_buf_ptr - string_buf;

   if (clen < STRING_BUF_SIZE)
      *string_buf_ptr++ = c;
}

/* Add a string into temporary string buffer */
static inline void lexer_add_tstr(char *str)
{
   size_t slen = strlen(str);
   size_t alen;

   /* Compute available length in buffer */
   alen = STRING_BUF_SIZE - (string_buf_ptr - string_buf);

   /* Truncate what we will copy next */
   if (slen > alen) 
      slen = alen;
   
   /* Copy from specified string */
   memcpy(string_buf_ptr,str,slen);
   string_buf_ptr += slen;
}

/* Initialize lexer */
int lexer_init(cfg_info_t *cfg_parser_info)
{
   size_t len;

   /* 
    * Allocate memory to hold file info in form of a stack. This stack is
    * used to keep track of included files.
    */
   parser_info = cfg_parser_info;

   len = LEXER_MAX_FILES * sizeof(lexer_file_t);
   if (!(lexer_files = mp_alloc(parser_info->mp,len))) {
      fprintf(stderr,"Config Parser: unable to allocate memory for files!\n");
      return(-1);
   }

   lexer_file_pos = -1;
   lexer_cfile = NULL;

   /*
    * Allocate space for temporary string buffer.
    */
   string_buf = mp_alloc(parser_info->mp,(STRING_BUF_SIZE + 1));

   if (!string_buf) {
      fprintf(stderr,"Config Parser: unable to allocate memory for "
		    "string buffer!\n");
      return(-1);
   }

   return(0);
}

/* Open a file */
int lexer_open_file(char *filename)
{
   if (++lexer_file_pos >= LEXER_MAX_FILES)
      return(TOKEN_ERR_INCLUDE_DEPTH);

   if (lexer_cfile)
      lexer_cfile->buffer = YY_CURRENT_BUFFER;

   lexer_cfile = &lexer_files[lexer_file_pos];

   /* Initialize new file: lex buffer, line and filename */
   lexer_cfile->line = 1;
   lexer_cfile->filename = mp_dup_string(parser_info->mp,filename);
 
   if (!lexer_cfile->filename)
      return(TOKEN_ERR_MEMORY);

   /* Create new lex buffer and bind opened file to it */
   if (!(yyin = fopen(filename,"r")))
      return(TOKEN_ERR_FILE);

   yy_switch_to_buffer(yy_create_buffer(yyin,YY_BUF_SIZE));
   return(0);
}

/* Close a file */
int lexer_close_file(void)
{
   if (--lexer_file_pos < 0)
      return(0);

   lexer_cfile = &lexer_files[lexer_file_pos];
   yy_delete_buffer(YY_CURRENT_BUFFER);
   yy_switch_to_buffer(lexer_cfile->buffer);

   return(1);
}

%}

word    [a-zA-Z0-9,\:\@\-_/\\\.\|]+
space   [ \t\r]+

%option noyywrap
%s STRING CCOMMENT ECOMMENT INCLUDE INCLUDE_PATH

%%

<INITIAL>"/*"            {  /* "C" Comment */
                            comment_caller = INITIAL;
                            BEGIN(CCOMMENT);
                         }

<INITIAL>[#!]|"//"       {  /* Comment on a line */
                            comment_caller = INITIAL;
                            BEGIN(ECOMMENT);
                         }

<INITIAL>{word}          {  /* Simple word */
                            token_cstring = yytext; 
                            return(TOKEN_WORD); 
                         }

<INITIAL>\"              {  /* String */ 
                            string_buf_ptr = string_buf; 
                            string_caller = INITIAL;
                            BEGIN(STRING); 
                         }

<INITIAL>\$include       {  /* $include "file" */
                            include_caller = INITIAL;
                            BEGIN(INCLUDE);
                         }

<INITIAL>\{              return(TOKEN_BLOCK_START);
<INITIAL>\}\;            return(TOKEN_BLOCK_END);
<INITIAL>\;              return(TOKEN_SEMICOLON);

<INITIAL>{space}         ;

<INITIAL>\n              lexer_inc_line();

<INITIAL>.               return(TOKEN_ERR_CHAR);

<CCOMMENT>[^*\n]*        /* Eat anything that's not a '*' */
<CCOMMENT>"*"+[^*/\n]*   /* Eat up '*'s not followed by '/'s */
<CCOMMENT>\n             lexer_inc_line();
<CCOMMENT>"*"+"/"        BEGIN(comment_caller);
<CCOMMENT><<EOF>>        return(TOKEN_ERR_COMMENT);

<ECOMMENT>\n             {  /* End of Line Comment */
                            lexer_inc_line(); 
                            BEGIN(comment_caller); 
                         }

<ECOMMENT>.              ;  /* skip character */

<STRING>\"               {  /* End of String */
                            BEGIN(string_caller); 
                            *string_buf_ptr = '\0'; 
                            token_cstring = string_buf;
                            return(TOKEN_WORD);
                         }

<STRING><<EOF>>          return(TOKEN_ERR_STRING);
<STRING>\n               return(TOKEN_ERR_STRING);

<STRING>\\n              lexer_add_tchar('\n');
<STRING>\\t              lexer_add_tchar('\t');
<STRING>\\r              lexer_add_tchar('\r');
<STRING>\\b              lexer_add_tchar('\b');
<STRING>\\f              lexer_add_tchar('\f');

<STRING>\\.              lexer_add_tchar(yytext[1]);
<STRING>\\\n             lexer_inc_line();

<STRING>[^\\\n\"]+       lexer_add_tstr(yytext);

<INCLUDE>[ \t]*          /* Eat the whitespaces */
<INCLUDE>\"              BEGIN(INCLUDE_PATH);
<INCLUDE>[^ \"\t\n]+     {
                            int res = lexer_open_file(yytext);

                            if (res != 0) 
                               return(res);

                            BEGIN(include_caller);
                         }

<INCLUDE_PATH>.+\"       {
                            int res;

                            yytext[strlen(yytext)-1] = '\0';
                            res = lexer_open_file(yytext);

                            if (res != 0) 
                               return(res);

                            BEGIN(include_caller);
                         }

<INCLUDE_PATH>.          return(TOKEN_ERR_INCLUDE_PATH);

<<EOF>>                  {
                            if (!lexer_close_file())
                               yyterminate();
                         }
%%

/* Read a token */
static int token_read(void)
{
   int res = yylex();

#if DEBUG_LEXER
   if (res == TOKEN_WORD)
      m_log("Lexer","%s:%d: token_read: type=%d, token_cstring='%s'",
	    lexer_cfile->filename,lexer_cfile->line,res,token_cstring);
   else
      m_log("Lexer","%s:%d: token_read: type=%d, description='%s'",
	    lexer_cfile->filename,lexer_cfile->line,
            res, (res < TOKEN_ERROR) ? token_descriptions[res] : "error");
#endif

   return(res);
}

/* Print an error prefixed by filename and line */
void token_error(char *fmt,...)
{
   char buffer[4096];
   va_list ap;

   va_start(ap,fmt);
   vsnprintf(buffer,sizeof(buffer),fmt,ap);
   va_end(ap);

   if (lexer_cfile) {
      fprintf(stderr,"[%s:%d] %s\n",
              lexer_cfile->filename,lexer_cfile->line,buffer);
   } else {
      fprintf(stderr,"%s\n",buffer);
   }
}

/* Print unexpected EOF */
void token_print_unexp_eof(void)
{
   token_error("unexpected end-of-file (EOF) encountered.");
}

/* Print a lexer error */
static int token_lex_error(int type,int eof_error)
{
   if (eof_error && (type == TOKEN_END)) {
      token_print_unexp_eof();
      return(-1);
   }

   /* No error... */
   if (type < TOKEN_ERROR)
      return(0);

   /* Print a specific string for each error */
   switch(type)
   {
      case TOKEN_ERR_CHAR:
         token_error("invalid character '%c'.",yytext[0]);
         break;
      case TOKEN_ERR_STRING:
         token_error("unterminated string.");
         break;
      case TOKEN_ERR_COMMENT:
         token_error("unterminated comment.");
         break;
      case TOKEN_ERR_MEMORY:
         token_error("insufficient memory to store parsing info.");
         break;
      case TOKEN_ERR_FILE:
         token_error("unable to open file '%s'.",yytext);
         break;
      case TOKEN_ERR_INCLUDE_DEPTH:
         token_error("includes nested too deeply.");
         break;
      case TOKEN_ERR_INCLUDE_PATH:
         token_error("filename path specified incorrectly in "
		     "include statement.");
         break;
      case TOKEN_ERR_CFG_VAR:
         token_error("unknown configuration variable '%s'.",yytext+2);
         break;
      default:
         token_error("unknown error code %d.",type);
   }

   return(-1);
}

/* Consume a given type of token */
int token_consume_type(int type)
{
   int res = token_read();

   if (token_lex_error(res,TRUE) == -1)
      return(-1);

   if (type != res) {
      token_error("expected %s but got a %s.",
		  token_descriptions[type],token_descriptions[res]);
      return(-1);
   }
   
   return(0);
}

/* Consume a word */
char *token_consume_word(void)
{
   int res = token_read();

   if (token_lex_error(res,TRUE) == -1)
      return NULL;

   if (res != TOKEN_WORD) {
      token_error("expected a word but got a %s.",token_descriptions[res]);
      return NULL;
   }
   
   return token_cstring;
}

/* Consume a token */
int token_consume(void)
{
   int res = token_read();

   if (token_lex_error(res,FALSE) == -1)
      return(-1);

   return(res);
}

/* Get current token contents */
char *token_get_value(void)
{
   return(token_cstring);
}

/* Set filename and line for specified node */
void token_set_file_info(cfg_node_t *node)
{
   node->filename = lexer_cfile->filename;
   node->line = lexer_cfile->line;
}
