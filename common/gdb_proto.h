
#ifndef __GDB_PROTO__
#define __GDB_PROTO__

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>

#include "gdb_utils.h"
#include "gdb_cmd.h"
#include "gdb_server.h"

#include "vm.h"

#define GDB_EXIT_STOP_VM             0
#define GDB_EXIT_DONT_STOP_VM        1
#define GDB_CONT_RESUME_VM           2
#define GDB_CONT_DONT_RESUME_VM      3

enum {
    GDB_SIG_0       = 0x0,
    GDB_SIGHUP      = 0x00000001,
    GDB_SIGINT      = 0x00000002,
    GDB_SIGQUIT     = 0x00000003,
    GDB_SIGILL      = 0x00000004,
    GDB_SIGTRAP     = 0x00000005,
    GDB_SIGABRT     = 0x00000006,
    GDB_SIGEMT      = 0x00000007,
    GDB_SIGFPE      = 0x00000008,
    GDB_SIGKILL     = 0x00000009,
    GDB_SIGBUS      = 0x0000000A,
    GDB_SIGSEGV     = 0x0000000B,
    GDB_SIGSYS      = 0x0000000C,
    GDB_SIGPIPE     = 0x0000000D,
    GDB_SIGALRM     = 0x0000000E,
    GDB_SIGTERM     = 0x0000000F,
    GDB_SIGURG      = 0x00000010,
    GDB_SIGSTOP     = 0x00000011,
    GDB_SIGTSTP     = 0x00000012,
    GDB_SIGCONT     = 0x00000013,
    GDB_SIGCHLD     = 0x00000014,
    GDB_SIGTTIN     = 0x00000015,
    GDB_SIGTTOU     = 0x00000016,
    GDB_SIGIO       = 0x00000017,
    GDB_SIGXCPU     = 0x00000018,
    GDB_SIGXFSZ     = 0x00000019,
    GDB_SIGVTALRM   = 0x0000001A,
    GDB_SIGPROF     = 0x0000001B,
    GDB_SIGWINCH    = 0x0000001C,
    GDB_SIGLOST     = 0x0000001D,
    GDB_SIGUSR1     = 0x0000001E,
    GDB_SIGUSR2     = 0x0000001F,
    GDB_SIGPWR      = 0x00000020,
    GDB_SIGPOLL     = 0x00000021,
    GDB_SIGWIND     = 0x00000022,
    GDB_SIGPHONE    = 0x00000023,
    GDB_SIGWAITING  = 0x00000024,
    GDB_SIGLWP      = 0x00000025,
    GDB_SIGDANGER   = 0x00000026,
    GDB_SIGGRANT    = 0x00000027,
    GDB_SIGRETRACT  = 0x00000028,
    GDB_SIGMSG      = 0x00000029,
    GDB_SIGSOUND    = 0x0000002A,
    GDB_SIGSAK      = 0x0000002B,
    GDB_SIGPRIO     = 0x0000002C,
    GDB_SIGN_UNKNOWN= 143
};

/* Define the size of the buffers used for communications with the remote
 * GDB. This value must match the value used by GDB, or the protocol will
 * break.
 */
#define BUFMAX 600 // IDA's size is 512 by default

/* Local storage */
extern boolean gdb_debug;

struct debug_context
{
    int (*getchar)(FILE *stream);               /* Get character routine */
    int (*putchar)(int c, FILE *stream);        /* Put character routine */
    int (*flush)(FILE *stream);                 /* Flush output */
    char inbuf[BUFMAX];                         /* Current input command */
    char outbuf[BUFMAX];                        /* Current output command */
    char scratchbuf[BUFMAX];                    /* Scratch buffer for compression */
    int savelevel;                              /* Saved interrupt level of GDB */
    int signal;                                 /* Signal number of exception */
                             
    FILE *in,*out;                              /* I/O buffered streams */
    vm_instance_t *vm;                          /* Current VM for this debuggin session */
};

void gdb_init_debug_context(vm_instance_t *vm);

int gdb_interface(gdb_debug_context_t* ctx);

void gdb_compress (char *src, char *dest);

void gdb_expand (char *src, char *dest);

char tohexchar (unsigned char c);

int chartohex (unsigned char ch);

int gdb_printf (const char *fmt, ...);

char* mem2hex(char *mem, char *buf, int count);

char* hex2mem(char *buf, char *mem, int count);

boolean gethexnum(char *srcstr, char **retstr, int *retvalue);

boolean parse2hexnum(char *srcstr, int *retvalue1, int *retvalue2);

boolean parsehexnum(char *srcstr, int *retvalue);

#endif
