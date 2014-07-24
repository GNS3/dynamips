/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Added by: Sebastian 'topo' Muniz
 *
 * Contact: sebastianmuniz@gmail.com
 *
 * GDB server routines.
 */

#include "gdb_server.h"

#include "vm.h"

int fd_count;
int fd_array[GDB_SERVER_MAX_FD];
                  
/* Start tcp listener for the GDB stub */
int gdb_server_start_listener(vm_instance_t *vm)
{
   struct sockaddr_storage remote_addr;
   socklen_t      remote_len;
   int            i,
                  res,
                  clnt,
                  fd_max;
   struct timeval tv;
   fd_set         fds;

   char ip_addr[] = {"0.0.0.0"}; /* TODO: Allow listening on a specified interface */

   fd_count = ip_listen(ip_addr, vm->gdb_port, SOCK_STREAM,
                        GDB_SERVER_MAX_FD, fd_array); // only one connection

   if (fd_count <= 0)
      return -1; // Error

   /* Start accepting connections */
   printf("GDB Server listening on port %d.\n", vm->gdb_port);

   vm->gdb_server_running = TRUE;

   while(vm->gdb_server_running) {
      FD_ZERO(&fds);
      fd_max = -1;

      for(i=0;i<fd_count;i++) {
         if (fd_array[i] != -1) {
            FD_SET(fd_array[i],&fds);
            if (fd_array[i] > fd_max)
               fd_max = fd_array[i];
         }
      }

      /* Wait for incoming connections */
      tv.tv_sec  = 0;
      tv.tv_usec = 500 * 1000;  /* 500 ms */
      res = select(fd_max+1,&fds,NULL,NULL,&tv);

      if (res == -1) {
         if (errno == EINTR)
            continue;
         else
            perror("gdb_tcp_server: select");
      }

      // TODO: Remove this hack if we want to add 'somehow' support for multiple GDB clients
      /* Accept connections on signaled sockets */
      //for(i=0;i<fd_count;i++) {
         i = 0; // REMOVE THIS LINE !!!!!!!!!!

         if (fd_array[i] == -1)
            continue;
         
         if (!FD_ISSET(fd_array[i], &fds))
            continue;

         remote_len = sizeof(remote_addr);
         clnt = accept(fd_array[i], (struct sockaddr *)&remote_addr,
                       &remote_len);

         if (clnt < 0) {
            perror("gdb_tcp_server: accept");
            continue;
         }
            
         /* create a new connection and start a thread to handle it */
         if (!gdb_server_create_conn(vm, clnt)) {
            fprintf(stderr,"gdb_tcp_server: unable to create new "
                    "connection for FD %d\n",clnt);
            close(clnt);
         }
         else
         {
//             printf("breaking OK from gdb_server_create_conn()\n");
            break;
         }
      //}

      /* Walk through the connection list to eliminate dead connections */
//       hypervisor_close_conn_list(TRUE);
   }   

   return 0; // success
}

void gdb_server_close_control_sockets()
{
   int i;

   /* Close all control sockets */
   printf("GDB Server: closing control sockets.\n");
   
   for(i=0; i<fd_count; i++)
   {
      if (fd_array[i] != -1)
      {
         shutdown(fd_array[i],2);
         close(fd_array[i]);
      }
   }

   /* Close all remote client connections */
//    printf("GDB server: closing remote client connections.\n");
//    gdb_server_close_conn_list(FALSE);
}

/* Stop GDB server from sighandler */
int gdb_server_stopsig(vm_instance_t *vm)
{
   if (gdb_debug)
        printf("Got STOPSIG for GDB server\n");
   vm->gdb_server_running = FALSE;
   return(0);
}

/* Create a new connection */
gdb_server_conn_t *gdb_server_create_conn(vm_instance_t *vm, int client_fd)
{
   gdb_server_conn_t *conn;
   
   if (!(conn = malloc(sizeof(*conn))))
      goto err_malloc;

   // Set the connection information for the current VM
   vm->gdb_conn = conn;
   
   // Initialize GDB connection structure members
   memset(conn,0,sizeof(gdb_server_conn_t));
   conn->active    = TRUE;
   conn->client_fd = client_fd;

   /* Open input buffered stream */
   if (!(conn->in = fdopen(client_fd,"r"))) {
      perror("gdb_server_create_conn: fdopen/in");
      goto err_fd_in;
   }

   /* Open output buffered stream */
   if (!(conn->out = fdopen(client_fd,"w"))) {
      perror("gdb_server_create_conn: fdopen/out");
      goto err_fd_out;
   }

   /* Set line buffering */
   setlinebuf(conn->in);
   setlinebuf(conn->out);

   /* Create the managing thread */
   if (pthread_create(&conn->tid, NULL, gdb_server_thread, vm) != 0)
      goto err_thread;

   /* Add it to the connection list */
//    gdb_server_add_conn(conn); // TODO: Add this function
   return conn;

 err_thread:
   fclose(conn->out);
 err_fd_out:
   fclose(conn->in);
 err_fd_in:
   free(conn);
 err_malloc:
   return NULL;
}

/* This function calls select to wait for data to read from */
/* one of the sockets passed as a parameter.                */
/* If more than 3 seconds elapses, it returns.              */
/* Return value flags. These indicate the readiness of      */
/* each socket for read.                                    */
int waittoread(int s1)
{
   fd_set fds;
   struct timeval timeout;
   int rc, result;

      /* Set time limit. */
   timeout.tv_sec = 0;
   timeout.tv_usec = 100;
      /* Create a descriptor set containing our two sockets.  */
   FD_ZERO(&fds);
   FD_SET(s1, &fds);
   
   rc = select(sizeof(fds)*8, &fds, NULL, NULL, &timeout);
   
   if (rc==-1) {
      perror("select failed");
      return -1;
   }

   result = 0;
   if (rc > 0)
   {
      if (FD_ISSET(s1, &fds))
        result = 1;
   }

   return result;
}

/* Thread for servicing connections */
void *gdb_server_thread(void *arg)
{
    vm_instance_t *vm = (vm_instance_t *)arg;

    printf("GDB Server: thread is now activated...\n");

    gdb_init_debug_context(vm);
    
    while (vm->gdb_conn->active && vm->gdb_server_running)
    {
      if (vm->status == VM_STATUS_RUNNING)
      {
          //cpu_idle_loop(vm->boot_cpu);
          
          // wait around for the start character, ignore all other characters
          if (!waittoread(vm->gdb_conn->client_fd))
             continue;

	  switch(GETCHAR(vm->gdb_ctx))
          {
             // User requested GDB Stub attention to commands.
	     case 037:
             case 003:
                  {
                     vm->gdb_ctx->signal = GDB_SIGTRAP;
                     vm_suspend (vm);
                     break;
                  }
             default:
	       continue;
          }

          
      }

      // Enter GDB command processing loop and exit after all the user 
      // commands have been processed.
      switch (gdb_interface(vm->gdb_ctx))
      {
        case GDB_EXIT_STOP_VM:
        {
          // User requested to leave the loop (either killed the session or
          // something worng happened.
          vm_stop(vm);
          vm->gdb_conn->active = FALSE;
          break;
        }
        case GDB_EXIT_DONT_STOP_VM:
        {
          // User detached from the debugging session.
          vm->gdb_conn->active = FALSE;
          break;
        }
        case GDB_CONT_RESUME_VM:
        {
          // A 'continue' command was received.
          vm_resume(vm);
          break;
        }
        case GDB_CONT_DONT_RESUME_VM:
        {
          // A 'single step' command was received.
          vm->gdb_ctx->signal = GDB_SIGTRAP;
          break;
        }
      }

    }
    
    free(vm->gdb_ctx);
    free(vm->gdb_conn);
    printf("GDB Server: thread is now deactivated...\n");

    return NULL;
}
