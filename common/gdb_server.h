/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Added by: Sebastian 'topo' Muniz
 *
 * Contact: sebastianmuniz@gmail.com
 *
 * Hypervisor GDB server module support.
 */

#ifndef __GDB_SERVER_H__
#define __GDB_SERVER_H__

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
#include <pthread.h>

#include "utils.h"
#include "cpu.h"
#include "net.h"

#include "gdb_utils.h"
#include "gdb_proto.h"

//#include "vm.h"
/*
#include "dynamips.h"
#include "device.h"
#include "dev_c7200.h"
#include "dev_vtty.h"
#include "registry.h"
#include "hypervisor.h"
#include "gdb_proto.h"
*/


/* Default TCP port */
#define GDB_SERVER_TCP_PORT 1234

/* Maximum listening socket number */
#define GDB_SERVER_MAX_FD   1

/* GDB Server connection */
struct gdb_server_conn {
   pthread_t tid;                    /* Thread identifier */
   int active;                       /* Connection is active ? */
   int client_fd;                    /* Client FD */
   FILE *in,*out;                    /* I/O buffered streams */
   uint gdb_port;                    /* Port to listen for incomming GDB client connections */

   //vm_instance_t *vm;                /* Current VM for this debuggin session */
};

//typedef struct gdb_server_conn gdb_server_conn_t;

// /* start GDB server listener */
int gdb_server_start_listener(vm_instance_t *vm);

/* stop gdb server */
int gdb_server_stopsig(vm_instance_t *vm);

/* Create a new connection */
gdb_server_conn_t *gdb_server_create_conn(vm_instance_t* vm, int client_fd);

/* Thread for servicing connections */
void *gdb_server_thread(void *arg);

/* Remove control socket connections when leaving */
void gdb_server_close_control_sockets();

#endif
