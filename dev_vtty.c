/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Virtual console TTY.
 *
 * "Interactive" part idea by Mtve.
 * TCP console added by Mtve.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include <arpa/telnet.h>

#include "mips64.h"
#include "cp0.h"
#include "cpu.h"
#include "dynamips.h"
#include "mips64_exec.h"
#include "device.h"
#include "memory.h"
#include "dev_vtty.h"

static struct termios tios,tios_orig;

/* Send Telnet command: WILL TELOPT_ECHO */
static void vtty_telnet_will_echo(vtty_t *vtty)
{
   u_char cmd[] = { IAC, WILL, TELOPT_ECHO, 0 };
   write(vtty->fd,cmd,sizeof(cmd));
}

/* Send Telnet command: Suppress Go-Ahead */
static void vtty_telnet_will_suppress_go_ahead(vtty_t *vtty)
{
   u_char cmd[] = { IAC, WILL, TELOPT_SGA, 0 };
   write(vtty->fd,cmd,sizeof(cmd));
}

/* Send Telnet command: Don't use linemode */
static void vtty_telnet_dont_linemode(vtty_t *vtty)
{
   u_char cmd[] = { IAC, DONT, TELOPT_LINEMODE, 0 };
   write(vtty->fd,cmd,sizeof(cmd));
}

/* Restore TTY original settings */
static void vtty_term_reset(void)
{
   tcsetattr(STDIN_FILENO,TCSANOW,&tios_orig);
}

/* Initialize real TTY */
static void vtty_term_init(void)
{
   tcgetattr(STDIN_FILENO, &tios);

   memcpy(&tios_orig,&tios,sizeof(struct termios));
   atexit(vtty_term_reset);

   tios.c_cc[VTIME] = 0;
   tios.c_cc[VMIN] = 1;

   /* Disable Ctrl-C, Ctrl-S, Ctrl-Q and Ctrl-Z */
   tios.c_cc[VINTR] = 0;
   tios.c_cc[VSTART] = 0;
   tios.c_cc[VSTOP] = 0;
   tios.c_cc[VSUSP] = 0;

   tios.c_lflag &= ~(ICANON|ECHO);
   tios.c_iflag &= ~ICRNL;
   tcsetattr(STDIN_FILENO, TCSANOW, &tios);
   tcflush(STDIN_FILENO,TCIFLUSH);
}

/* Wait for a TCP connection */
static void vtty_tcp_waitcon(vtty_t *vtty)
{
   struct sockaddr_in serv;
   int sockfd;
   int one = 1;

   if ((sockfd = socket(PF_INET,SOCK_STREAM,0)) < 0) {
      fprintf(stderr,"vtty_tcp_waitcon: socket failed %s\n",strerror(errno));
      exit(EXIT_FAILURE);
   }

   if (setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one)) < 0) {
      fprintf(stderr,"vtty_tcp_waitcon: setsockopt SO_REUSEPORT failed %s\n",
              strerror(errno));
      exit(EXIT_FAILURE);
   }

   memset(&serv,0,sizeof(serv));
   serv.sin_family = AF_INET;
   serv.sin_addr.s_addr = htonl(INADDR_ANY);
   serv.sin_port = htons(vtty->tcp_port);

   if (bind(sockfd,(struct sockaddr *)&serv,sizeof(serv)) < 0) {
      fprintf(stderr,"vtty_tcp_waitcon: bind on port %d failed %s\n",
              vtty->tcp_port,strerror(errno));
      exit(EXIT_FAILURE);
   }

   if (listen(sockfd,1) < 0) {
      fprintf(stderr,"vtty_tcp_waitcon: listen on port %d failed %s\n",
              vtty->tcp_port,strerror(errno));
      exit(EXIT_FAILURE);
   }

   fprintf(stderr,"Waiting connection to %s on tcp port %d\n",
           vtty->name,vtty->tcp_port);

   if ((vtty->fd = accept(sockfd,NULL,NULL)) < 0) {
      fprintf(stderr,"vtty_tcp_waitcon: accept on port %d failed %s\n",
              vtty->tcp_port,strerror(errno));
      exit(EXIT_FAILURE);
   }

   fprintf(stderr,"%s is now connected.\n",vtty->name);
   close(sockfd);

   /* Adapt Telnet settings */
   vtty_telnet_will_echo(vtty);
   vtty_telnet_will_suppress_go_ahead(vtty);
   vtty_telnet_dont_linemode(vtty);
}

/* Create a virtual tty */
vtty_t *vtty_create(char *name,int type,int tcp_port)
{
   vtty_t *vtty;

   if (!(vtty = malloc(sizeof(*vtty)))) {
      fprintf(stderr,"VTTY: unable to create new virtual tty.\n");
      return NULL;
   }

   memset(vtty,0,sizeof(*vtty));
   vtty->name = name;
   vtty->type = type;

   switch (vtty->type) {
      case VTTY_TYPE_NONE:
         vtty->fd = -1;
         break;

      case VTTY_TYPE_TERM:
         vtty_term_init();
         vtty->fd = STDIN_FILENO;
         break;

      case VTTY_TYPE_TCP:
         vtty->tcp_port = tcp_port;
         vtty->fd = -1; /* will wait for connection on the first read */
         break;

      default:
         fprintf(stderr,"tty_create: bad vtty type %d\n",vtty->type);
         return NULL;
   }

   return vtty;
}

/* Store a character in the FIFO buffer */
static int vtty_store(vtty_t *vtty,char c)
{
   u_int nwptr;

   nwptr = vtty->write_ptr + 1;
   if (nwptr == VTTY_BUFFER_SIZE)
      nwptr = 0;

   if (nwptr == vtty->read_ptr)
      return(-1);

   vtty->buffer[vtty->write_ptr] = c;
   vtty->write_ptr = nwptr;
   return(0);
}

/*
 * Read a character from the virtual TTY.
 *
 * If the VTTY is a TCP connection, restart it in case of error.
 */
static u_char vtty_read(vtty_t *vtty)
{
   u_char c;

   assert(vtty->type != VTTY_TYPE_NONE);

   for(;;) {
      if (read(vtty->fd,&c,1) == 1)
         return(c);

      m_log("VTTY","read failed: %s\n",strerror(errno));

      switch(vtty->type) {
         case VTTY_TYPE_TERM:
            perror("read from terminal failed");
            exit(EXIT_FAILURE);

         case VTTY_TYPE_TCP:
            close(vtty->fd);
            vtty_tcp_waitcon(vtty);
            break;

         default:
            fprintf(stderr,"vtty_read: bad vtty type %d\n",vtty->type);
            exit(EXIT_FAILURE);
      }
   }

   /* NOTREACHED */
   return(0);
}

/* read a character (until one is available) and store it in buffer */
int vtty_read_and_store(vtty_t *vtty)
{
   cpu_mips_t *cpu0;
   u_char c;

   cpu0 = cpu_group_find_id(sys_cpu_group,0);

   /* wait until we get a character input */
   c = vtty_read(vtty);

   switch(c) {
      /* special character handling */
      case 0x1b:   /* ESC */
         c = vtty_read(vtty);

         switch(c) {
            case 0x5b:   /* '[' */
               c  = vtty_read(vtty);

               switch(c) {
                  case 0x41:   /* Up Arrow */
                     vtty_store(vtty,16);
                     return(0);

                  case 0x42:   /* Down Arrow */
                     vtty_store(vtty,14);
                     return(0);

                  case 0x43:   /* Right Arrow */
                     vtty_store(vtty,6);
                     return(0);

                  case 0x44:   /* Left Arrow */
                     vtty_store(vtty,2);
                     return(0);
               }

               break;

            default:
               vtty_store(vtty,0x1b);
               vtty_store(vtty,c);
         }

         return(0);

      /* Ctrl + ']' (0x1d, 29) */
      case 0x1d:
         c  = vtty_read(vtty);

         switch(c) {
            /* Stop the MIPS VM */
            case 'q':
               cpu_group_stop_all_cpu(sys_cpu_group);
               break;

            /* Show the device list */
            case 'd':
               if (cpu0) dev_show_list(cpu0);
               break;

            /* Dump the MIPS registers */
            case 'r':
               if (cpu0) mips64_dump_regs(cpu0);
               break;

            /* Dump the latest memory accesses */
            case 'm':
               if (cpu0) memlog_dump(cpu0);
               break;

            /* Suspend CPU emulation */
            case 's':
               cpu_group_set_state(sys_cpu_group,MIPS_CPU_SUSPENDED);
               break;

            /* Resume CPU emulation */
            case 'u':
               cpu_group_start_all_cpu(sys_cpu_group);
               break;

            /* Dump the MIPS TLB */
            case 't':
               if (cpu0) tlb_dump(cpu0);
               break;

            /* Dump the instruction block tree */
            case 'b':
               if (cpu0) insn_block_dump_tree(cpu0);
               break;

            /* Extract the configuration from the NVRAM */
            case 'c':
               dev_nvram_extract_config(sys_cpu_group,"ios_cfg.txt");
               break;

            /* Non-JIT mode statistics */
            case 'j':
               if (cpu0) mips64_dump_stats(cpu0);
               break;

            /* Experimentations / Tests */
            case 'x':
               if (cpu0) {
                  printf("\nCPU0: hash_lookups: %llu, hash_misses: %llu, "
                         "efficiency: %g%%\n",
                         cpu0->hash_lookups, cpu0->hash_misses,
                         100 - ((double)(cpu0->hash_misses*100)/
                                (double)cpu0->hash_lookups));

                  mips64_jit_dump_hash(cpu0);
               }

            /* Twice Ctrl+] */
            case 29:
               vtty_store(vtty,c);
               break;

            default:
               printf("\n"
                      "d     - Show the device list\n"
                      "r     - Dump MIPS CPU registers\n"
                      "t     - Dump MIPS TLB entries\n"
                      "m     - Dump the latest memory accesses\n"
                      "s     - Suspend CPU emulation\n"
                      "u     - Resume CPU emulation\n"
                      "q     - Quit the emulator\n"
                      "b     - Dump the instruction block tree\n"
                      "c     - Write IOS configuration to disk (ios_cfg.txt)\n"
                      "j     - Non-JIT mode statistics\n"
                      "x     - Experimentations (can crash the box!)\n"
                      "^]    - Send ^]\n"
                      "Other - This help\n");
         }
         return(0);

      case 0:
         break;

      /* store a standard character */
      default:
         vtty_store(vtty,c);
   }

   return(0);
}

/* read a character from the buffer (-1 if the buffer is empty) */
int vtty_get_char(vtty_t *vtty)
{
   char c;

   if (vtty->read_ptr == vtty->write_ptr)
      return(-1);
   
   c = vtty->buffer[vtty->read_ptr++];

   if (vtty->read_ptr == VTTY_BUFFER_SIZE)
      vtty->read_ptr = 0;

   return(c);
}

/* returns TRUE if a character is available in buffer */
int vtty_is_char_avail(vtty_t *vtty)
{
   return((vtty->read_ptr != vtty->write_ptr) ? 1 : 0);
}

/* put char to vtty */
void vtty_put_char(vtty_t *vtty, char ch)
{
   switch(vtty->type) {
      case VTTY_TYPE_NONE:
         break;

      case VTTY_TYPE_TERM:
         printf("%c",ch);
         fflush(stdout);
         break;

      case VTTY_TYPE_TCP:
         if ((vtty->fd != -1) && (write(vtty->fd,&ch,1) != 1))
            m_log("VTTY","put char %d failed %s\n",(int)ch,strerror(errno));
         break;

      default:
         fprintf(stderr,"vtty_put_char: bad vtty type %d\n",vtty->type);
         exit(1);
   }
}
