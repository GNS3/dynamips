/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Virtual console TTY.
 *
 * "Interactive" part idea by Mtve.
 * TCP console added by Mtve.
 * Serial console by Peter Ross (suxen_drol@hotmail.com)
 */

#include "dynamips_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <termios.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include <arpa/telnet.h>
#include <arpa/inet.h>

#include "utils.h"
#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "mips64_exec.h"
#include "ppc32_exec.h"
#include "device.h"
#include "memory.h"
#include "dev_vtty.h"

#ifdef USE_UNSTABLE
#include "tcb.h"
#endif

#ifndef SOL_TCP
#define SOL_TCP 6
#endif

/* VTTY list */
static pthread_mutex_t vtty_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static vtty_t *vtty_list = NULL;
static pthread_t vtty_thread;

#define VTTY_LIST_LOCK()   pthread_mutex_lock(&vtty_list_mutex);
#define VTTY_LIST_UNLOCK() pthread_mutex_unlock(&vtty_list_mutex);

static struct termios tios,tios_orig;

static int ctrl_code_ok = 1;
static int telnet_message_ok = 1;

/* Allow the user to disable the CTRL code for the monitor interface */
void vtty_set_ctrlhandler(int n)
{
  ctrl_code_ok = n;
}
/* Allow the user to disable the telnet message for AUX and CONSOLE */
void vtty_set_telnetmsg(int n)
{
  telnet_message_ok = n;
}

/* Send Telnet command: WILL TELOPT_ECHO */
static void vtty_telnet_will_echo(int fd)
{
   u_char cmd[] = { IAC, WILL, TELOPT_ECHO };
   write(fd,cmd,sizeof(cmd));
}

/* Send Telnet command: Suppress Go-Ahead */
static void vtty_telnet_will_suppress_go_ahead(int fd)
{
   u_char cmd[] = { IAC, WILL, TELOPT_SGA };
   write(fd,cmd,sizeof(cmd));
}

/* Send Telnet command: Don't use linemode */
static void vtty_telnet_dont_linemode(int fd)
{
   u_char cmd[] = { IAC, DONT, TELOPT_LINEMODE };
   write(fd,cmd,sizeof(cmd));
}

/* Send Telnet command: does the client support terminal type message? */
static void vtty_telnet_do_ttype(int fd)
{
   u_char cmd[] = { IAC, DO, TELOPT_TTYPE };
   write(fd,cmd,sizeof(cmd));
}

/* Restore TTY original settings */
static void vtty_term_reset(void)
{
   tcsetattr(STDIN_FILENO,TCSANOW,&tios_orig);
}

/* Initialize real TTY */
static void vtty_term_init(void)
{
   tcgetattr(STDIN_FILENO,&tios);

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

#if HAS_RFC2553
/* Wait for a TCP connection */
static int vtty_tcp_conn_wait(vtty_t *vtty)
{
    struct addrinfo hints,*res,*res0;
    char port_str[20],*addr,*proto;
    int i, nsock;
    int one = 1;
    
    for(i=0;i<VTTY_MAX_FD;i++)
        vtty->fd_array[i] = -1;
    
    memset(&hints,0,sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    snprintf(port_str,sizeof(port_str),"%d",vtty->tcp_port);
    addr = (binding_addr && strlen(binding_addr)) ? binding_addr : NULL;
    
    if (getaddrinfo(addr,port_str,&hints,&res0) != 0) {
        perror("vtty_tcp_waitcon: getaddrinfo");
        return(-1);
    }
    
    nsock = 0;
    for (res=res0;(res && (nsock < VTTY_MAX_FD));res=res->ai_next)
    {
        if ((res->ai_family != PF_INET) && (res->ai_family != PF_INET6))
            continue;
        
        vtty->fd_array[nsock] = socket(res->ai_family,res->ai_socktype,
                                       res->ai_protocol);
        
        if (vtty->fd_array[nsock] < 0)
            continue;
        
        if (setsockopt(vtty->fd_array[nsock],SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one)) < 0)
            perror("vtty_tcp_waitcon: setsockopt(SO_REUSEADDR)");
        
        if (setsockopt(vtty->fd_array[nsock],SOL_SOCKET,SO_KEEPALIVE,&one,sizeof(one)) < 0)
            perror("vtty_tcp_waitcon: setsockopt(SO_KEEPALIVE)");
        
        // Send telnet packets asap. Dont wait to fill packets up
        if (setsockopt(vtty->fd_array[nsock],SOL_TCP,TCP_NODELAY, &one,sizeof(one)) < 0)
            perror("vtty_tcp_waitcon: setsockopt(TCP_NODELAY)");
        
        if ((bind(vtty->fd_array[nsock],res->ai_addr,res->ai_addrlen) < 0) ||
            (listen(vtty->fd_array[nsock],1) < 0))
        {
            close(vtty->fd_array[nsock]);
            vtty->fd_array[nsock] = -1;
            continue;
        }
        
        proto = (res->ai_family == PF_INET6) ? "IPv6" : "IPv4";
        vm_log(vtty->vm,"VTTY","%s: waiting connection on tcp port %d for protocol %s (FD %d)\n",
               vtty->name,vtty->tcp_port,proto,vtty->fd_array[nsock]);
        
        nsock++;
    }

    freeaddrinfo(res0);
    return(nsock);
}
#else
/* Wait for a TCP connection */
static int vtty_tcp_conn_wait(vtty_t *vtty)
{
    struct sockaddr_in serv;
    int i;
    int one = 1;
    
    for(i=0;i<VTTY_MAX_FD;i++)
        vtty->fd_array[i] = -1;
    
    if ((vtty->fd_array[0] = socket(PF_INET,SOCK_STREAM,0)) < 0) {
        perror("vtty_tcp_waitcon: socket");
        return(-1);
    }
    
    if (setsockopt(vtty->fd_array[0],SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one)) < 0) {
        perror("vtty_tcp_waitcon: setsockopt(SO_REUSEADDR)");
        goto error;
    }
    
    if (setsockopt(vtty->fd_array[0],SOL_SOCKET,SO_KEEPALIVE,&one,sizeof(one)) < 0) {
        perror("vtty_tcp_waitcon: setsockopt(SO_KEEPALIVE)");
        goto error;
    }
    
    // Send telnet packets asap. Dont wait to fill packets up
    if (setsockopt(vtty->fd_array[0],SOL_TCP,TCP_NODELAY,&one,sizeof(one)) < 0)
    {
        perror("vtty_tcp_waitcon: setsockopt(TCP_NODELAY)");
        goto error;
    }
    
    memset(&serv,0,sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_ANY);
    serv.sin_port = htons(vtty->tcp_port);
    
    if (bind(vtty->fd_array[0],(struct sockaddr *)&serv,sizeof(serv)) < 0) {
        perror("vtty_tcp_waitcon: bind");
        goto error;
    }
    
    if (listen(vtty->fd_array[0],1) < 0) {
        perror("vtty_tcp_waitcon: listen");
        goto error;
    }
    
    vm_log(vtty->vm,"VTTY","%s: waiting connection on tcp port %d (FD %d)\n",
           vtty->name,vtty->tcp_port,vtty->fd_array[0]);
    
    return(1);
    
error:
    close(vtty->fd_array[0]);
    vtty->fd_array[0] = -1;
    return(-1);
}
#endif

/* Accept a TCP connection */
static int vtty_tcp_conn_accept(vtty_t *vtty, int nsock)
{
   int fd,*fd_slot;
   u_int i;
   
   if (fd_pool_get_free_slot(&vtty->fd_pool,&fd_slot) < 0) {
      vm_error(vtty->vm,"unable to create a new VTTY TCP connection\n");
      return(-1);
   }
   
   if ((fd = accept(vtty->fd_array[nsock],NULL,NULL)) < 0) {
      vm_error(vtty->vm,"vtty_tcp_conn_accept: accept on port %d failed %s\n",
              vtty->tcp_port,strerror(errno));
      return(-1);
   }

   /* Register the new FD */
   *fd_slot = fd;

   vm_log(vtty->vm,"VTTY","%s is now connected (accept_fd=%d,conn_fd=%d)\n",
          vtty->name,vtty->fd_array[nsock],fd);

   /* Adapt Telnet settings */
   if (vtty->terminal_support) {      
      vtty_telnet_do_ttype(fd);
      vtty_telnet_will_echo(fd);
      vtty_telnet_will_suppress_go_ahead(fd);
      vtty_telnet_dont_linemode(fd);
      vtty->input_state = VTTY_INPUT_TEXT;
   }

   if (telnet_message_ok == 1) {
      fd_printf(fd,0,
                "Connected to Dynamips VM \"%s\" (ID %u, type %s) - %s\r\n"
                "Press ENTER to get the prompt.\r\n", 
                vtty->vm->name, vtty->vm->instance_id, vm_get_type(vtty->vm),
                vtty->name);
      /* replay old text */
      for (i = vtty->replay_ptr; i < VTTY_BUFFER_SIZE; i++) {
         if (vtty->replay_buffer[i] != 0) {
            send(fd,&vtty->replay_buffer[i],VTTY_BUFFER_SIZE-i,0);
            break;
         }
      }
      for (i = 0; i < vtty->replay_ptr; i++) {
         if (vtty->replay_buffer[i] != 0) {
            send(fd,&vtty->replay_buffer[i],vtty->replay_ptr-i,0);
            break;
         }
      }
      /* warn if not running */
      if (vtty->vm->status != VM_STATUS_RUNNING)
         fd_printf(fd,0,"\r\n!!! WARNING - VM is not running, will be unresponsive (status=%d) !!!\r\n",vtty->vm->status);
      vtty_flush(vtty);
   }
   return(0);
}

/* 
 * Parse serial interface descriptor string, return 0 if success
 * string takes the form "device:baudrate:databits:parity:stopbits:hwflow"
 * device is mandatory, other options are optional (default=9600,8,N,1,0).
 */
int vtty_parse_serial_option(vtty_serial_option_t *option, char *optarg)
{
   char *array[6];
   int count;

   if ((count = m_strtok(optarg, ':', array, 6)) < 1) {
      fprintf(stderr,"vtty_parse_serial_option: invalid string\n");
      return(-1);
   }

   if (!(option->device = strdup(array[0]))) {
      fprintf(stderr,"vtty_parse_serial_option: unable to copy string\n");
      return(-1);
   }
   
   option->baudrate = (count>1) ? atoi(array[1]) : 9600;
   option->databits = (count>2) ? atoi(array[2]) : 8;

   if (count > 3) {
      switch(*array[3]) {
         case 'o':
         case 'O': 
            option->parity = 1;  /* odd */
         case 'e':
         case 'E': 
            option->parity = 2;  /* even */
         default:
            option->parity = 0;  /* none */
      }
   } else {
      option->parity = 0;
   }

   option->stopbits = (count>4) ? atoi(array[4]) : 1;
   option->hwflow   = (count>5) ? atoi(array[5]) : 0;
   return(0);
}

#if defined(__CYGWIN__) || defined(SUNOS)
void cfmakeraw(struct termios *termios_p) {
    termios_p->c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|
                            INLCR|IGNCR|ICRNL|IXON);
    termios_p->c_oflag &= ~OPOST;
    termios_p->c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
    termios_p->c_cflag &= ~(CSIZE|PARENB);
    termios_p->c_cflag |= CS8;
}
#endif

/* 
 * Setup serial port, return 0 if success.
 */
static int vtty_serial_setup(vtty_t *vtty, const vtty_serial_option_t *option)
{
   struct termios tio;
   int tio_baudrate;

   if (tcgetattr(vtty->fd_array[0], &tio) != 0) { 
      fprintf(stderr, "error: tcgetattr failed\n");
      return(-1);
   }

   cfmakeraw(&tio);

   tio.c_cflag = 0
      |CLOCAL     // ignore modem control lines
      ;

   tio.c_cflag &= ~CREAD;
   tio.c_cflag |= CREAD;

   switch(option->baudrate) {
      case 50     : tio_baudrate = B50; break;
      case 75     : tio_baudrate = B75; break;
      case 110    : tio_baudrate = B110; break;
      case 134    : tio_baudrate = B134; break;
      case 150    : tio_baudrate = B150; break;
      case 200    : tio_baudrate = B200; break;
      case 300    : tio_baudrate = B300; break;
      case 600    : tio_baudrate = B600; break;
      case 1200   : tio_baudrate = B1200; break;
      case 1800   : tio_baudrate = B1800; break;
      case 2400   : tio_baudrate = B2400; break;
      case 4800   : tio_baudrate = B4800; break;
      case 9600   : tio_baudrate = B9600; break;
      case 19200  : tio_baudrate = B19200; break;
      case 38400  : tio_baudrate = B38400; break;
      case 57600  : tio_baudrate = B57600; break;
#if defined(B76800)
      case 76800  : tio_baudrate = B76800; break;
#endif
      case 115200 : tio_baudrate = B115200; break;
#if defined(B230400)
      case 230400 : tio_baudrate = B230400; break;
#endif
      default:
         fprintf(stderr, "error: unsupported baudrate\n");
         return(-1);
   }

   cfsetospeed(&tio, tio_baudrate);
   cfsetispeed(&tio, tio_baudrate);

   tio.c_cflag &= ~CSIZE; /* clear size flag */
   switch(option->databits) {
      case 5 : tio.c_cflag |= CS5; break;
      case 6 : tio.c_cflag |= CS6; break;
      case 7 : tio.c_cflag |= CS7; break;
      case 8 : tio.c_cflag |= CS8; break;
      default :
         fprintf(stderr, "error: unsupported databits\n");
         return(-1);
   }

   tio.c_iflag &= ~INPCK;  /* clear parity flag */
   tio.c_cflag &= ~(PARENB|PARODD);
   switch(option->parity) {
      case 0 : break;
      case 2 : tio.c_iflag|=INPCK; tio.c_cflag|=PARENB; break;  /* even */
      case 1 : tio.c_iflag|=INPCK; tio.c_cflag|=PARENB|PARODD; break; /* odd */
      default:
         fprintf(stderr, "error: unsupported parity\n");
         return(-1);
   }

   tio.c_cflag &= ~CSTOPB; /* clear stop flag */
   switch(option->stopbits) {
      case 1 : break;
      case 2 : tio.c_cflag |= CSTOPB; break;
      default :
         fprintf(stderr, "error: unsupported stopbits\n");
         return(-1);
   }

#if defined(CRTSCTS)
   tio.c_cflag &= ~CRTSCTS;
#endif
#if defined(CNEW_RTSCTS)
   tio.c_cflag &= ~CNEW_RTSCTS;
#endif
   if (option->hwflow) {
#if defined(CRTSCTS)
      tio.c_cflag |= CRTSCTS;
#else
      tio.c_cflag |= CNEW_RTSCTS;
#endif
   }

   tio.c_cc[VTIME] = 0;
   tio.c_cc[VMIN] = 1; /* block read() until one character is available */

#if 0
   /* not neccessary unless O_NONBLOCK used */
   if (fcntl(vtty->fd_array[0], F_SETFL, 0) != 0) {  /* enable blocking mode */
      fprintf(stderr, "error: fnctl F_SETFL failed\n");
      return(-1);
   }
#endif
  
   if (tcflush(vtty->fd_array[0], TCIOFLUSH) != 0) {
      fprintf(stderr, "error: tcflush failed\n");
      return(-1);
   }
  
   if (tcsetattr(vtty->fd_array[0], TCSANOW, &tio) != 0 ) {
      fprintf(stderr, "error: tcsetattr failed\n");
      return(-1);
   }

   return(0);
}

/* Create a virtual tty */
vtty_t *vtty_create(vm_instance_t *vm,char *name,int type,int tcp_port,
                    const vtty_serial_option_t *option)
{
   vtty_t *vtty;
   int i;

   if (!(vtty = malloc(sizeof(*vtty)))) {
      fprintf(stderr,"VTTY: unable to create new virtual tty.\n");
      return NULL;
   }
   memset(vtty,0,sizeof(*vtty));
   vtty->name = name;
   vtty->type = type;
   vtty->vm   = vm;
   vtty->fd_count = 0;
   pthread_mutex_init(&vtty->lock,NULL);
   vtty->terminal_support = 1;
   vtty->input_state = VTTY_INPUT_TEXT;
   fd_pool_init(&vtty->fd_pool);
   for(i=0;i<VTTY_MAX_FD;i++)
       vtty->fd_array[i] = -1;
    
   switch (vtty->type) {
      case VTTY_TYPE_NONE:
         break;

      case VTTY_TYPE_TERM:
         vtty_term_init();
         vtty->fd_array[0] = STDIN_FILENO;
         break;

      case VTTY_TYPE_TCP:
         vtty->tcp_port = tcp_port;
         vtty->fd_count = vtty_tcp_conn_wait(vtty);
         break;

      case VTTY_TYPE_SERIAL:
         vtty->fd_array[0] = open(option->device, O_RDWR);
         if (vtty->fd_array[0] < 0) {
            fprintf(stderr,"VTTY: open failed\n");
            free(vtty);
            return NULL;
         }
         if (vtty_serial_setup(vtty,option)) {
            fprintf(stderr,"VTTY: setup failed\n");
            close(vtty->fd_array[0]);
            free(vtty);
            return NULL;
         }
         vtty->terminal_support = 0;
         break;

      default:
         fprintf(stderr,"tty_create: bad vtty type %d\n",vtty->type);
         return NULL;
   }

   /* Add this new VTTY to the list */
   VTTY_LIST_LOCK();
   vtty->next = vtty_list;
   vtty->pprev = &vtty_list;

   if (vtty_list != NULL)
      vtty_list->pprev = &vtty->next;

   vtty_list = vtty;
   VTTY_LIST_UNLOCK();
   return vtty;
}

/* Delete a virtual tty */
void vtty_delete(vtty_t *vtty)
{
   int i;

   if (vtty != NULL) {
      if (vtty->pprev != NULL) {
         VTTY_LIST_LOCK();
         if (vtty->next)
            vtty->next->pprev = vtty->pprev;
         *(vtty->pprev) = vtty->next;
         VTTY_LIST_UNLOCK();
      }

      switch(vtty->type) {
           case VTTY_TYPE_TCP:
               
               for(i=0;i<vtty->fd_count;i++)
                   if (vtty->fd_array[i] != -1) {
                       vm_log(vtty->vm,"VTTY","%s: closing FD %d\n",vtty->name,vtty->fd_array[i]);
                       close(vtty->fd_array[i]);
                   }

           fd_pool_free(&vtty->fd_pool);
           vtty->fd_count = 0;
           break;
        
           default:
               
               /* We don't close FD 0 since it is stdin */
               if (vtty->fd_array[0] > 0) {
                   vm_log(vtty->vm,"VTTY","%s: closing FD %d\n",vtty->name,vtty->fd_array[0]);
                   close(vtty->fd_array[0]);
               }
       }
      free(vtty);
   }
}

/* Store a character in the FIFO buffer */
static int vtty_store(vtty_t *vtty,u_char c)
{
   u_int nwptr;

   VTTY_LOCK(vtty);
   nwptr = vtty->write_ptr + 1;
   if (nwptr == VTTY_BUFFER_SIZE)
      nwptr = 0;

   if (nwptr == vtty->read_ptr) {
      VTTY_UNLOCK(vtty);
      return(-1);
   }

   vtty->buffer[vtty->write_ptr] = c;
   vtty->write_ptr = nwptr;
   VTTY_UNLOCK(vtty);
   return(0);
}

/* Store arbritary data in the FIFO buffer */
int vtty_store_data(vtty_t *vtty,char *data, int len)
{
   int bytes;

   if (!vtty || !data || len < 0)
      return(-1); // invalid argument

   for (bytes = 0; bytes < len; bytes++) {
      if (vtty_store(vtty,data[bytes]) == -1)
         break;
   }

   vtty->input_pending = TRUE;
   return(bytes);
}

/* Store CTRL+C in buffer */
int vtty_store_ctrlc(vtty_t *vtty)
{
   if (vtty)
      vtty_store(vtty,0x03);
   return(0);
}

/* 
 * Read a character from the terminal.
 */
static int vtty_term_read(vtty_t *vtty)
{
   u_char c;

   if (read(vtty->fd_array[0],&c,1) == 1)
      return(c);

   perror("read from vtty failed");
   return(-1);
}

/* 
 * Read a character from the TCP connection.
 */
static int vtty_tcp_read(vtty_t *vtty,int *fd_slot)
{
   int fd = *fd_slot;
   u_char c;
   
   if (read(fd,&c,1) == 1)
      return(c);

   /* problem with the connection */
   shutdown(fd,2);
   close(fd);      
   *fd_slot = -1;

   /* Shouldn't happen... */
   return(-1);
}

/*
 * Read a character from the virtual TTY.
 *
 * If the VTTY is a TCP connection, restart it in case of error.
 */
static int vtty_read(vtty_t *vtty,int *fd_slot)
{
   switch(vtty->type) {
      case VTTY_TYPE_TERM:
      case VTTY_TYPE_SERIAL:
         return(vtty_term_read(vtty));
      case VTTY_TYPE_TCP:
         return(vtty_tcp_read(vtty,fd_slot));
      default:
         fprintf(stderr,"vtty_read: bad vtty type %d\n",vtty->type);
         return(-1);
   }

   /* NOTREACHED */
   return(-1);
}

/* Remote control for MIPS64 processors */
static int remote_control_mips64(vtty_t *vtty,char c,cpu_mips_t *cpu)
{
   switch(c) {    
      /* Show information about JIT compiled pages */
      case 'b':
         printf("\nCPU0: %u JIT compiled pages [Exec Area Pages: %lu/%lu]\n",
                cpu->compiled_pages,
                (u_long)cpu->exec_page_alloc,
                (u_long)cpu->exec_page_count);
         break;

      /* Non-JIT mode statistics */
      case 'j':
         mips64_dump_stats(cpu);
         break;

      default:
         return(FALSE);
   }

   return(TRUE);
}

/* Remote control for PPC32 processors */
static int remote_control_ppc32(vtty_t *vtty,char c,cpu_ppc_t *cpu)
{
   switch(c) {
      /* Show information about JIT compiled pages */
      case 'b':
         printf("\nCPU0: %u JIT compiled pages [Exec Area Pages: %lu/%lu]\n",
                cpu->compiled_pages,
                (u_long)cpu->exec_page_alloc,
                (u_long)cpu->exec_page_count);
         break;

      /* Non-JIT mode statistics */
      case 'j':
         ppc32_dump_stats(cpu);
         break;

      default:
         return(FALSE);
   }
   
   return(TRUE);
}

/* Process remote control char */
static void remote_control(vtty_t *vtty,u_char c)
{
   vm_instance_t *vm = vtty->vm;
   cpu_gen_t *cpu0;
  
   cpu0 = vm->boot_cpu;

   /* Specific commands for the different CPU models */
   if (cpu0) {
      switch(cpu0->type) {
         case CPU_TYPE_MIPS64:
            if (remote_control_mips64(vtty,c,CPU_MIPS64(cpu0)))
               return;
            break;
         case CPU_TYPE_PPC32:
            if (remote_control_ppc32(vtty,c,CPU_PPC32(cpu0)))
               return;
            break;
      }
   }

   switch(c) {
      /* Show the object list */
      case 'o':
         vm_object_dump(vm);
         break;
  
      /* Stop the MIPS VM */
      case 'q':
         vm->status = VM_STATUS_SHUTDOWN;
         break;
  
      /* Reboot the C7200 */
      case 'k':
#if 0
         if (vm->type == VM_TYPE_C7200)
            c7200_boot_ios(VM_C7200(vm));
#endif
         break;
  
      /* Show the device list */
      case 'd':
         dev_show_list(vm);
         pci_dev_show_list(vm->pci_bus[0]);
         pci_dev_show_list(vm->pci_bus[1]);
         break;

      /* Show info about Port Adapters or Network Modules */
      case 'p':
         vm_slot_show_all_info(vm);
         break;
  
      /* Dump the MIPS registers */
      case 'r':
         if (cpu0) cpu0->reg_dump(cpu0);
         break;

      /* Dump the latest memory accesses */
      case 'm':
         if (cpu0) memlog_dump(cpu0);
         break;      
         
      /* Suspend CPU emulation */
      case 's':
         vm_suspend(vm);
         break;
  
      /* Resume CPU emulation */
      case 'u':
         vm_resume(vm);
         break;
  
      /* Dump the MMU information */
      case 't':
         if (cpu0) cpu0->mmu_dump(cpu0);
         break;
  
      /* Dump the MMU information (raw mode) */
      case 'z':
         if (cpu0) cpu0->mmu_raw_dump(cpu0);
         break;

      /* Memory translation cache statistics */
      case 'l':
         if (cpu0) cpu0->mts_show_stats(cpu0);
         break;

      /* Extract the configuration from the NVRAM */
      case 'c':
         vm_ios_save_config(vm);
         break;
  
      /* Determine an idle pointer counter */
      case 'i':
         if (cpu0)
            cpu0->get_idling_pc(cpu0);
         break;
  
      /* Experimentations / Tests */
      case 'x':

#if 0
         if (cpu0) {
            /* IRQ triggering */
            vm_set_irq(vm,6);
            //CPU_MIPS64(cpu0)->irq_disable = TRUE;
         }
#endif
#ifdef USE_UNSTABLE
         tsg_show_stats();
#endif
         break;

      case 'y':
         if (cpu0) {
            /* IRQ clearing */
            vm_clear_irq(vm,6);
         }
         break;

      /* Twice Ctrl + ']' (0x1d, 29), or Alt-Gr + '*' (0xb3, 179) */
      case 0x1d:
      case 0xb3:
         vtty_store(vtty,c);
         break;
         
      default:
         printf("\n\nInstance %s (ID %d)\n\n",vm->name,vm->instance_id);
         
         printf("o     - Show the VM object list\n"
                "d     - Show the device list\n"
                "r     - Dump CPU registers\n"
                "t     - Dump MMU information\n"
                "z     - Dump MMU information (raw mode)\n"
                "m     - Dump the latest memory accesses\n"
                "s     - Suspend CPU emulation\n"
                "u     - Resume CPU emulation\n"
                "q     - Quit the emulator\n"
                "k     - Reboot the virtual machine\n"
                "b     - Show info about JIT compiled pages\n"
                "l     - MTS cache statistics\n"
                "c     - Write IOS configuration to disk\n"
                "j     - Non-JIT mode statistics\n"
                "i     - Determine an idling pointer counter\n"
                "x     - Experimentations (can crash the box!)\n"
                "^]    - Send ^]\n"
                "Other - This help\n");
   }
}
  
  
/* Read a character (until one is available) and store it in buffer */
static void vtty_read_and_store(vtty_t *vtty,int *fd_slot)
{
   int c;
   
   /* wait until we get a character input */
   c = vtty_read(vtty,fd_slot);
  
   /* if read error, do nothing */
   if (c < 0) return;

   /* If something was read, make sure the handler is informed */
   vtty->input_pending = TRUE;  

   if (!vtty->terminal_support) {
      vtty_store(vtty,c);
      return;
   }
  
   switch(vtty->input_state) {
      case VTTY_INPUT_TEXT :
         switch(c) {
            case 0x1b:
               vtty->input_state = VTTY_INPUT_VT1;
               return;

            /* Ctrl + ']' (0x1d, 29), or Alt-Gr + '*' (0xb3, 179) */
            case 0x1d:
            case 0xb3:
               if (ctrl_code_ok == 1) {
                 vtty->input_state = VTTY_INPUT_REMOTE;
               } else {
                 vtty_store(vtty,c);
               }
               return;
            case IAC :
               vtty->input_state = VTTY_INPUT_TELNET;
               return;
            case 0:  /* NULL - Must be ignored - generated by Linux telnet */
            case 10: /* LF (Line Feed) - Must be ignored on Windows platform */
               return;
            default:
               /* Store a standard character */
               vtty_store(vtty,c);
               return;
         }
         
      case VTTY_INPUT_VT1 :
         switch(c) {
            case 0x5b:
               vtty->input_state = VTTY_INPUT_VT2;
               return;
            default:
               vtty_store(vtty,0x1b);
               vtty_store(vtty,c);
         }
         vtty->input_state = VTTY_INPUT_TEXT;
         return;
  
      case VTTY_INPUT_VT2 :
         switch(c) {
            case 0x41:   /* Up Arrow */
               vtty_store(vtty,16);
               break;
            case 0x42:   /* Down Arrow */
               vtty_store(vtty,14);
               break;
            case 0x43:   /* Right Arrow */
               vtty_store(vtty,6);
               break;
            case 0x44:   /* Left Arrow */
               vtty_store(vtty,2);
               break;
            default:
               vtty_store(vtty,0x5b);
               vtty_store(vtty,0x1b);
               vtty_store(vtty,c);
               break;
         }
         vtty->input_state = VTTY_INPUT_TEXT;
         return;
  
      case VTTY_INPUT_REMOTE :
         remote_control(vtty, c);
         vtty->input_state = VTTY_INPUT_TEXT;
         return;
  
      case VTTY_INPUT_TELNET :
         vtty->telnet_cmd = c;
         switch(c) {
            case WILL:
            case WONT:
            case DO:
            case DONT:
               vtty->input_state = VTTY_INPUT_TELNET_IYOU;
               return;
            case SB :
               vtty->telnet_cmd = c;
               vtty->input_state = VTTY_INPUT_TELNET_SB1;
               return;
            case SE:
               break;
            case IAC :
               vtty_store(vtty, IAC);
               break;
         }
         vtty->input_state = VTTY_INPUT_TEXT;
         return;
  
      case VTTY_INPUT_TELNET_IYOU :
         vtty->telnet_opt = c;
         /* if telnet client can support ttype, ask it to send ttype string */
         if ((vtty->telnet_cmd == WILL) && 
             (vtty->telnet_opt == TELOPT_TTYPE)) 
         {
            vtty_put_char(vtty, IAC);
            vtty_put_char(vtty, SB);
            vtty_put_char(vtty, TELOPT_TTYPE);
            vtty_put_char(vtty, TELQUAL_SEND);
            vtty_put_char(vtty, IAC);
            vtty_put_char(vtty, SE);
         }
         vtty->input_state = VTTY_INPUT_TEXT;
         return;
  
      case VTTY_INPUT_TELNET_SB1 :
         vtty->telnet_opt = c;
         vtty->input_state = VTTY_INPUT_TELNET_SB2;
         return;
  
      case VTTY_INPUT_TELNET_SB2 :
         vtty->telnet_qual = c;
         if ((vtty->telnet_opt == TELOPT_TTYPE) && 
             (vtty->telnet_qual == TELQUAL_IS))
            vtty->input_state = VTTY_INPUT_TELNET_SB_TTYPE;
         else
            vtty->input_state = VTTY_INPUT_TELNET_NEXT;
         return;
  
      case VTTY_INPUT_TELNET_SB_TTYPE :
         /* parse ttype string: first char is sufficient */
         /* if client is xterm or vt, set the title bar */
         if ((c == 'x') || (c == 'X') || (c == 'v') || (c == 'V')) {
            fd_printf(*fd_slot,0,"\033]0;%s\07", vtty->vm->name);
         }
         vtty->input_state = VTTY_INPUT_TELNET_NEXT;
         return;
  
      case VTTY_INPUT_TELNET_NEXT :
         /* ignore all chars until next IAC */
         if (c == IAC)
            vtty->input_state = VTTY_INPUT_TELNET;
         return;
   }
}

/* Read a character from the buffer (-1 if the buffer is empty) */
int vtty_get_char(vtty_t *vtty)
{
   u_char c;

   VTTY_LOCK(vtty);
   
   if (vtty->read_ptr == vtty->write_ptr) {
      VTTY_UNLOCK(vtty);
      return(-1);
   }
   
   c = vtty->buffer[vtty->read_ptr++];

   if (vtty->read_ptr == VTTY_BUFFER_SIZE)
      vtty->read_ptr = 0;

   VTTY_UNLOCK(vtty);
   return(c);
}

/* Returns TRUE if a character is available in buffer */
int vtty_is_char_avail(vtty_t *vtty)
{
   int res;

   VTTY_LOCK(vtty);
   res = (vtty->read_ptr != vtty->write_ptr);
   VTTY_UNLOCK(vtty);
   return(res);
}

/* Put char to vtty */
void vtty_put_char(vtty_t *vtty, char ch)
{
   switch(vtty->type) {
      case VTTY_TYPE_NONE:
         break;

      case VTTY_TYPE_TERM:
      case VTTY_TYPE_SERIAL:
         if (write(vtty->fd_array[0],&ch,1) != 1) {
            vm_log(vtty->vm,"VTTY","%s: put char 0x%x failed (%s)\n",
                   vtty->name,(int)ch,strerror(errno));
         }
         break;

      case VTTY_TYPE_TCP:
         fd_pool_send(&vtty->fd_pool,&ch,1,0);
         break;

      default:
         vm_error(vtty->vm,"vtty_put_char: bad vtty type %d\n",vtty->type);
         exit(1);
   }

   /* store char for replay */
   vtty->replay_buffer[vtty->replay_ptr] = ch;

   ++vtty->replay_ptr;
   if (vtty->replay_ptr == VTTY_BUFFER_SIZE)
      vtty->replay_ptr = 0;
}

/* Put a buffer to vtty */
void vtty_put_buffer(vtty_t *vtty,char *buf,size_t len)
{
   size_t i;

   for(i=0;i<len;i++)
      vtty_put_char(vtty,buf[i]);
   
   vtty_flush(vtty);
}

/* Flush VTTY output */
void vtty_flush(vtty_t *vtty)
{
   switch(vtty->type) {
      case VTTY_TYPE_TERM:
      case VTTY_TYPE_SERIAL:
         if (vtty->fd_array[0] != -1)
            fsync(vtty->fd_array[0]);
         break;         
   }
}

/* VTTY TCP input */
static void vtty_tcp_input(int *fd_slot,void *opt)
{
   vtty_read_and_store((vtty_t *)opt,fd_slot);
}

/* VTTY thread */
static void *vtty_thread_main(void *arg)
{
   vtty_t *vtty;
   struct timeval tv;
   int fd_max,fd_tcp,res;
   fd_set rfds;
   int i;

   for(;;) {
      VTTY_LIST_LOCK();

      /* Build the FD set */
      FD_ZERO(&rfds);
      fd_max = -1;
      for(vtty=vtty_list;vtty;vtty=vtty->next) {

          switch(vtty->type) {
              case VTTY_TYPE_TCP:

                  for(i=0;i<vtty->fd_count;i++)
                      if (vtty->fd_array[i] != -1) {
                          FD_SET(vtty->fd_array[i],&rfds);
                          if (vtty->fd_array[i] > fd_max)
                              fd_max = vtty->fd_array[i];
                      }

                  fd_tcp = fd_pool_set_fds(&vtty->fd_pool,&rfds);
                  fd_max = m_max(fd_tcp,fd_max);
                  break;

              default:
                  if (vtty->fd_array[0] != -1) {
                      FD_SET(vtty->fd_array[0],&rfds);
                      fd_max = m_max(vtty->fd_array[0],fd_max);
                  }
          }

      }
      VTTY_LIST_UNLOCK();

      /* Wait for incoming data */
      tv.tv_sec  = 0;
      tv.tv_usec = 50 * 1000;  /* 50 ms */
      res = select(fd_max+1,&rfds,NULL,NULL,&tv);

      if (res == -1) {
         if (errno != EINTR) {
            perror("vtty_thread: select");
         }
         continue;
      }

      /* Examine active FDs and call user handlers */
      VTTY_LIST_LOCK();
      for(vtty=vtty_list;vtty;vtty=vtty->next) {

         switch(vtty->type) {
            case VTTY_TYPE_TCP:

               /* check incoming connection */
               for(i=0;i<vtty->fd_count;i++) {
                   
                   if (vtty->fd_array[i] == -1)
                       continue;
                   
                   if (!FD_ISSET(vtty->fd_array[i],&rfds))
                       continue;
                   
                   vtty_tcp_conn_accept(vtty, i);
               }

               /* check established connection */
               fd_pool_check_input(&vtty->fd_pool,&rfds,vtty_tcp_input,vtty);
               break;
      
            /* Term, Serial */
            default:
               if (vtty->fd_array[0] != -1 && FD_ISSET(vtty->fd_array[0],&rfds)) {
                  vtty_read_and_store(vtty,&vtty->fd_array[0]);
                  vtty->input_pending = TRUE;
               }
         }
         
         if (vtty->input_pending) {
            if (vtty->read_notifier != NULL)
               vtty->read_notifier(vtty);

            vtty->input_pending = FALSE;
         }

         /* Flush any pending output */
         //if (!vtty->managed_flush) // FIXME: commented by topo to aviud Konsole flushing problem
            vtty_flush(vtty);
      }
      VTTY_LIST_UNLOCK();
   }
   
   return NULL;
}

/* Initialize the VTTY thread */
int vtty_init(void)
{
   if (pthread_create(&vtty_thread,NULL,vtty_thread_main,NULL)) {
      perror("vtty: pthread_create");
      return(-1);
   }

   return(0);
}
