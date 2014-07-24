/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Hypervisor routines.
 */

#ifndef __HYPERVISOR_H__
#define __HYPERVISOR_H__

/* Default TCP port */
#define HYPERVISOR_TCP_PORT 7200

/* Maximum listening socket number */
#define HYPERVISOR_MAX_FD   10

/* Maximum tokens per line */
#define HYPERVISOR_MAX_TOKENS  16

/* Hypervisor status codes */
#define HSC_INFO_OK         100  /* ok */
#define HSC_INFO_MSG        101  /* informative message */
#define HSC_INFO_DEBUG      102  /* debugging message */
#define HSC_ERR_PARSING     200  /* parse error */
#define HSC_ERR_UNK_MODULE  201  /* unknown module */
#define HSC_ERR_UNK_CMD     202  /* unknown command */
#define HSC_ERR_BAD_PARAM   203  /* bad number of parameters */
#define HSC_ERR_INV_PARAM   204  /* invalid parameter */
#define HSC_ERR_BINDING     205  /* binding error */
#define HSC_ERR_CREATE      206  /* unable to create object */
#define HSC_ERR_DELETE      207  /* unable to delete object */
#define HSC_ERR_UNK_OBJ     208  /* unknown object */
#define HSC_ERR_START       209  /* unable to start object */
#define HSC_ERR_STOP        210  /* unable to stop object */
#define HSC_ERR_FILE        211  /* file error */
#define HSC_ERR_BAD_OBJ     212  /* Bad object */
#define HSC_ERR_RENAME      213  /* unable to rename object */
#define HSC_ERR_NOT_FOUND   214  /* not found (generic) */
#define HSC_ERR_UNSPECIFIED 215  /* unspecified error (generic) */

typedef struct hypervisor_conn hypervisor_conn_t;
typedef struct hypervisor_cmd hypervisor_cmd_t;
typedef struct hypervisor_module hypervisor_module_t;

/* Hypervisor connection */
struct hypervisor_conn {
   pthread_t tid;                    /* Thread identifier */
   volatile int active;              /* Connection is active ? */
   int client_fd;                    /* Client FD */
   FILE *in,*out;                    /* I/O buffered streams */
   hypervisor_module_t *cur_module;  /* Module of current command */
   hypervisor_conn_t *next,**pprev;
};

/* Hypervisor command handler */
typedef int (*hypervisor_cmd_handler)(hypervisor_conn_t *conn,int argc,
                                      char *argv[]);

/* Hypervisor command */
struct hypervisor_cmd {
   char *name;
   int min_param,max_param;
   hypervisor_cmd_handler handler;
   hypervisor_cmd_t *next;
};

/* Hypervisor module */
struct hypervisor_module {
   char *name;
   void *opt;
   hypervisor_cmd_t *cmd_list;
   hypervisor_module_t *next;
};

/* Hypervisor NIO initialization */
extern int hypervisor_nio_init(void);

/* Hypervisor NIO bridge initialization */
extern int hypervisor_nio_bridge_init(void);

/* Hypervisor Frame-Relay switch initialization */
extern int hypervisor_frsw_init(void);

/* Hypervisor ATM switch initialization */
extern int hypervisor_atmsw_init(void);

/* Hypervisor ATM bridge initialization */
extern int hypervisor_atm_bridge_init(void);

/* Hypervisor Ethernet switch initialization */
extern int hypervisor_ethsw_init(void);

/* Hypervisor VM initialization */
extern int hypervisor_vm_init(void);

/* Hypervisor VM debugging initialization */
extern int hypervisor_vm_debug_init(void);

/* Hypervisor VM GDB debugging initialization */
extern int hypervisor_vm_gdb_debug_init(void);

/* Hypervisor store initialization */
extern int hypervisor_store_init(void);

/* Send a reply */
int hypervisor_send_reply(hypervisor_conn_t *conn,int code,int done,
                          char *format,...);

/* Find a module */
hypervisor_module_t *hypervisor_find_module(char *name);

/* Find a command in a module */
hypervisor_cmd_t *hypervisor_find_cmd(hypervisor_module_t *module,char *name);

/* Find an object in the registry */
void *hypervisor_find_object(hypervisor_conn_t *conn,char *name,int obj_type);

/* Find a VM in the registry */
void *hypervisor_find_vm(hypervisor_conn_t *conn,char *name);

/* Register a module */
hypervisor_module_t *hypervisor_register_module(char *name,void *opt);

/* Register a list of commands */
int hypervisor_register_cmd_list(hypervisor_module_t *module,
                                 hypervisor_cmd_t *cmd_list);

/* Register an array of commands */
int hypervisor_register_cmd_array(hypervisor_module_t *module,
                                  hypervisor_cmd_t *cmd_array);

/* Stop hypervisor from sighandler */
int hypervisor_stopsig(void);

/* Hypervisor TCP server */
int hypervisor_tcp_server(char *ip_addr,int tcp_port);

#endif
