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

static pthread_t vtty_thread;

#define VTTY_LIST_LOCK()   pthread_mutex_lock(&vtty_list_mutex);
#define VTTY_LIST_UNLOCK() pthread_mutex_unlock(&vtty_list_mutex);

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
   switch(vtty->type_) {
      case VTTY_TYPE_TERM:
      case VTTY_TYPE_SERIAL:
         return(vtty_term_read(vtty));
      case VTTY_TYPE_TCP:
         return(vtty_tcp_read(vtty,fd_slot));
      default:
         fprintf(stderr,"vtty_read: bad vtty type %d\n",vtty->type_);
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
         if (vm->type_ == VM_TYPE_C7200)
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

          switch(vtty->type_) {
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

         switch(vtty->type_) {
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
         if (!vtty->managed_flush)
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
