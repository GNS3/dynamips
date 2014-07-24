/*------------------------------------------------------------------
 * gdbproto.c  --  GDB protocol handling
 *  
 *------------------------------------------------------------------
 */
/*
 *
 *    The following gdb commands are supported:
 * 
 * command          function                               Return value
 * 
 *    g             return the value of the CPU registers  hex data or ENN
 *    G             set the value of the CPU registers     OK or ENN
 * 
 *    mAA..AA,LLLL  Read LLLL bytes at address AA..AA      hex data or ENN
 *    MAA..AA,LLLL: Write LLLL bytes at address AA.AA      OK or ENN
 * 
 *    c             Resume at current address              SNN   ( signal NN)
 *    cAA..AA       Continue at address AA..AA             SNN
 * 
 *    s             Step one instruction                   SNN
 *    sAA..AA       Step one instruction from AA..AA       SNN
 * 
 *    k             kill
 *
 *    ?             What was the last sigval ?             SNN   (signal NN)
 * 
 * All commands and responses are sent with a packet which includes a 
 * checksum.  A packet consists of 
 * 
 * $<packet info>#<checksum>.
 * 
 * where
 * <packet info> :: <characters representing the command or response>
 * <checksum>    :: < two hex digits computed as modulo 256 sum of <packetinfo>>
 * 
 * When a packet is received, it is first acknowledged with either '+' or '-'.
 * '+' indicates a successful transfer.  '-' indicates a failed transfer.
 * 
 * Example:
 * 
 * Host:                  Reply:
 * $m0,10#2a               +$00010203040506070809101112131415#42
 * 
 ****************************************************************************/

#include "gdb_proto.h"

unsigned char const hexchars[] = "0123456789abcdef";


boolean gdb_debug = FALSE;

void gdb_init_debug_context(vm_instance_t *vm)
{
    gdb_debug_context_t *ctx;

    if (!(ctx = malloc(sizeof(*ctx))))
       return;

    // Set the GDB context for the current VM
    vm->gdb_ctx = ctx;
    
    // Initialize GDB context structure members
    memset(ctx, 0, sizeof(gdb_debug_context_t));

    ctx->in      = vm->gdb_conn->in;
    ctx->out     = vm->gdb_conn->out;
    ctx->putchar = fputc;
    ctx->getchar = fgetc;
    ctx->flush   = fflush;
    ctx->vm = vm;

}

/*
 * gethexnum - parse a hex string returning a value
 *
 * This is a low-level routine for converting hex values into binary. It
 * is used by the 'parse' routines to actually perform the conversion.
 *
 * Entry : srcstr	- Pointer to string to convert
 *	   retstr	- Pointer to cell which will contain the address 
 *			  of the first byte not converted
 *			- Pointer to cell to return value
 */

boolean gethexnum (char *srcstr, char **retstr, int *retvalue)
{
  char *str = srcstr;
  unsigned long value = 0;	// TODO: Correct the type of this var to an internal valid type

  /* Convert all of the digits until we get a non-hex digit */

  while (*str && (((*str >= 'a') && (*str <= 'f')) ||
		  ((*str >= '0') && (*str <= '9'))))
    {
      value =
	value * 16 + (*str <= '9' ? (*str++ - '0') : (*str++ - 'a' + 10));
    }

  /* Return failure if we are still pointing at the start */

  if (str == srcstr)
    {
      return (FALSE);
    }

  /* Set up the return values and return success */

  *retvalue = value;
  *retstr = str;
  return (TRUE);
}

/*
 * parsehexnum - Parse a single hex number
 *
 * This routine is used to convert a hex value into binary. It uses gethexnum
 * to perform the actual conversion, and ignores any invalid characters at
 * the end of the string.
 */

boolean parsehexnum (char *srcstr, int *retvalue)
{
  char *dummy;

  return (gethexnum (srcstr, &dummy, retvalue));
}

/*
 * parse2hexnum - Parse two hex numbers
 *
 * This routine converts a string of two numbers, seperated by commas,
 * into two binary values. Note that if either of the values can not
 * be returned, this routine will return failure and not update either
 * return value.
 */
boolean parse2hexnum (char *srcstr, int *retvalue1, int *retvalue2)
{
  char *str;
  int value1, value2;

  if (!gethexnum (srcstr, &str, &value1) || (*str++ != ',') ||
      !gethexnum (str, &str, &value2))
    {
      return (FALSE);
    }

  *retvalue1 = value1;
  *retvalue2 = value2;
  return (TRUE);
}

/* 
 * scan for the sequence $<data>#<checksum>
 */
boolean getpacket (gdb_debug_context_t * ctx)
{
  unsigned char checksum;
  unsigned char xmitcsum = -1;
  int i;
  int count;
  int ch = -1;

  do
    {
      /* wait around for the start character, ignore all other characters */
      while (ch != '$')
	{
	  ch = GETCHAR (ctx);

	  if (ch == -1)
	    return (FALSE);
	}

      checksum = 0;
      count = 0;

      /* now, read until a # or end of buffer is found */
      while (count < BUFMAX-1)
	{
	  ch = GETCHAR (ctx);
	  if (ch == -1)
	    return (FALSE);
	  if (ch == '#')
	    break;
	  checksum = checksum + ch;
	  ctx->scratchbuf[count++] = ch;
	}
      ctx->scratchbuf[count] = 0;
      gdb_expand (ctx->scratchbuf, ctx->inbuf);

      if (ch == '#')
	{
	  ch = GETCHAR (ctx);
	  if (ch == -1)
	    return (FALSE);
	  xmitcsum = chartohex (ch) << 4;

	  ch = GETCHAR (ctx);
	  if (ch == -1)
	    return (FALSE);
	  xmitcsum += chartohex (ch);
	  if ((gdb_debug) && (checksum != xmitcsum))
	    {
	      gdb_printf
		("bad checksum.  My count = 0x%x, sent=0x%x. buf=%s\n",
		 checksum, xmitcsum, ctx->inbuf);
	    }

	  if (checksum != xmitcsum)
	    PUTCHAR (ctx, '-');	/* failed checksum */
	  else
	    {
	      PUTCHAR (ctx, '+');	/* successful transfer */
	      /* if a sequence char is present, reply the sequence ID */
	      if (ctx->inbuf[2] == ':')
		{
		  PUTCHAR (ctx, ctx->inbuf[0]);
		  PUTCHAR (ctx, ctx->inbuf[1]);
		  /* remove sequence chars from buffer */
		  count = strlen (ctx->inbuf);
		  for (i = 3; i <= count; i++)
		    ctx->inbuf[i - 3] = ctx->inbuf[i];
		}
	    }
	}
      FLUSH (ctx);

    }
  while (checksum != xmitcsum);

  if (gdb_debug)
     gdb_printf("-> %s\n", ctx->inbuf);
         
  return (TRUE);
}

/* 
 * send the packet in buffer.  The host get's one chance to read it.  
 * This routine does not wait for a positive acknowledge.
 */

void
putpacket (gdb_debug_context_t * ctx)
{
  unsigned char checksum;
  int count;
  char ch;

  gdb_compress (ctx->outbuf, ctx->scratchbuf);

  /*  $<packet info>#<checksum>. */

  PUTCHAR (ctx, '$');
  checksum = 0;
  count = 0;

  while ((ch = ctx->scratchbuf[count]))
    {
      PUTCHAR (ctx, ch);
      checksum += ch;
      count += 1;
    }

  PUTCHAR (ctx, '#');
  PUTCHAR (ctx, tohexchar (checksum >> 4));
  PUTCHAR (ctx, tohexchar (checksum));
  
  //if (gdb_debug)
     //gdb_printf("<- %s\n", ctx->outbuf);
         
  FLUSH (ctx);
}

/* 
 * convert the memory pointed to by mem into hex, placing result in buf
 * return a pointer to the last char put in buf (null)
 */
char *
mem2hex (char *mem, char *buf, int count)
{
  int i;
  unsigned char ch;

  for (i = 0; i < count; i++)
    {
      ch = *mem++;
      *buf++ = tohexchar (ch >> 4);
      *buf++ = tohexchar (ch);
    }
  *buf = 0;
  return (buf);
}

/*
 * convert the hex array pointed to by buf into binary to be placed in mem
 * return a pointer to the character AFTER the last byte written
 */

char *hex2mem (char *buf, char *mem, int count)
{
   int i;
   unsigned char ch;

   for (i = 0; i < count; i++)
   {
      ch = chartohex (*buf++) << 4;
      ch = ch + chartohex (*buf++);
      *mem++ = ch;
   }
   return (mem);
}

/*
 * This function does all command procesing for interfacing to gdb.
 */
int gdb_interface (gdb_debug_context_t * ctx)
{
  //int length;

  // FIXME: add debug support
  if (gdb_debug)
    gdb_printf ("GDB debug information not implemented!\n");
         //gdb_show_exception_info(ctx);

  /* 
   * Indicate that we've gone back to debug mode
   */
  snprintf(ctx->outbuf, sizeof(ctx->outbuf), "T%02xthread:%02x;", ctx->signal, 1);
  putpacket(ctx);

  while (getpacket (ctx))
  {
      ctx->outbuf[0] = 0;
         
      switch (ctx->inbuf[0])
	{          
	  /* Tell the gdb client our signal number */
	case '?':

	  //if (gdb_debug)
	    //gdb_printf ("Signal %02x\n", ctx->signal);

          snprintf(ctx->outbuf, sizeof(ctx->outbuf), "T%02xthread:%02x;", GDB_SIGTRAP, 1);
          /* Remove all the breakpoints when this query is issued,
           * because gdb is doing and initial connect and the state
           * should be cleaned up.
           */
          //gdb_breakpoint_remove_all();
	  break;

	  /* cAA..AA    Continue at address AA..AA(optional) */
	case 'c':
        {
          m_int32_t address = 0;
          
          parsehexnum(&ctx->inbuf[1], &address);
    
	  gdb_cmd_proc_continue (ctx, (m_uint64_t)address, 0);	/* continue instruction execution */
          
	  return (GDB_CONT_RESUME_VM);
        } 
	  /* toggle debug flag */
	case 'd':
	  gdb_debug = !(gdb_debug);
	  break;
          
        case 'D':
          /* Detach packet */
          //gdb_breakpoint_remove_all(); // TODO: implement this command.
          gdb_cmd_proc_continue(ctx, 0, 0);
          
          strcpy(ctx->outbuf, "OK");
          putpacket(ctx);
          
          if (gdb_debug)
             gdb_printf("Dynamips: Detached GDBstub.\n");
             
          return (GDB_EXIT_DONT_STOP_VM);

	  /* Return the value of the CPU registers */
	case 'g':
	  switch(ctx->vm->boot_cpu->type)
          {
             case CPU_TYPE_MIPS64:
               get_cpu_regs_mips64(ctx);
               break;
             case CPU_TYPE_PPC32:
               get_cpu_regs_ppc32(ctx);
               break;
          }
	  break;

	  /* Set the value of the CPU registers - return OK */
	case 'G':
	  gdb_cmd_set_cpu_regs (ctx);
	  break;

	  /* k: Kill the current debugger session */
	case 'k':
          if (gdb_debug)
             gdb_printf("Dynamips: Terminated via GDBstub.\n");
          return (GDB_EXIT_STOP_VM);

	  /* mAA..AA,LLLL  Read LLLL bytes at address AA..AA */
	case 'm':
	  gdb_cmd_read_mem (ctx);
	  break;

	  /* MAA..AA,LLLL: Write LLLL bytes at address AA.AA return OK */
	case 'M':
	  gdb_cmd_write_mem (ctx);
	  break;
          
          /* pAA: return the content of register number AA */
        case 'p':
	  switch(ctx->vm->boot_cpu->type)
          {
             case CPU_TYPE_MIPS64:
               gdb_cmd_get_cpu_reg_mips64(ctx);
               break;
             case CPU_TYPE_PPC32:
               gdb_cmd_get_cpu_reg_ppc32(ctx);
               break;
          }
          break;
        
	  /* PAA..AA=LLLL: Set register AA..AA to value LLLL */
	case 'P':
	  switch(ctx->vm->boot_cpu->type)
          {
             case CPU_TYPE_MIPS64:
               gdb_cmd_set_cpu_reg_mips64(ctx);
               break;
             case CPU_TYPE_PPC32:
               gdb_cmd_set_cpu_reg_ppc32(ctx);
               break;
          }
	  break;

          
        case 'q':
        case 'Q':
        {
            int query_cpu=0;
            if (strcmp(ctx->inbuf + 1, "C") == 0) {
                /* "Current thread" remains vague in the spec, so always return
                 *  the first CPU (gdb returns the first thread). */
                strcpy(ctx->outbuf, "QC1");
                //putpacket(ctx);
                break;
                
            } else if (strcmp(ctx->inbuf + 1, "fThreadInfo") == 0) {
                //s->query_cpu = first_cpu;
                query_cpu = 1;
                goto report_cpuinfo;
                
            } else if (strcmp(ctx->inbuf + 1, "sThreadInfo") == 0) {
                    
            report_cpuinfo:
            
                if (query_cpu) {
                    snprintf(ctx->outbuf, sizeof(ctx->outbuf), "m%x", 
                                query_cpu/*gdb_id(s->query_cpu)*/);
                    //putpacket(ctx);
                    //s->query_cpu = s->query_cpu->next_cpu;
                } else {
                    strcpy(ctx->outbuf, "l");
                    //putpacket(ctx);
                }
                break;
                
            } else if (strncmp(ctx->inbuf + 1, "ThreadExtraInfo,", 16) == 0) {
                unsigned long long thread;
                unsigned int len;
                char mem_buf[BUFMAX];
                
                thread = strtoull(ctx->inbuf + 1 +16, (char **)&ctx->inbuf + 1, 16);
                
                //cpu_synchronize_state(env, 0);
                len = snprintf((char *)mem_buf, sizeof(mem_buf),
                           "CPU#%d [%s]", ctx->vm->instance_id,
                           ctx->vm->status == VM_STATUS_RUNNING ? "running" : "halted ");
                mem2hex(ctx->outbuf, mem_buf, len);
                //putpacket(ctx);
                break;
            }/*
            else if (strncmp(ctx->inbuf + 1, "Rcmd,", 5) == 0) {
                int len = strlen(ctx->inbuf + 1 + 5);

                if ((len % 2) != 0) {
                    strcpy(ctx->outbuf, "E01");
                    putpacket(ctx);
                    break;
                }
                hextomem(mem_buf, ctx->inbuf + 1 + 5, len);
                len = len / 2;
                mem_buf[len++] = 0;
                qemu_chr_read(s->mon_chr, mem_buf, len);
                strcpy(ctx->outbuf, "OK");
                putpacket(ctx);
                break;
            }*/
            /*
            else if (strncmp(ctx->inbuf + 1, "Supported", 9) == 0) {
                snprintf(ctx->outbuf, sizeof(ctx->outbuf), "PacketSize=%x", MAX_PACKET_LENGTH);
                //putpacket(ctx);
                break;
            }
            */
            break;
         }
	  /* sAA..AA   Step one instruction from AA..AA(optional) */
	case 's':
        {
          m_int32_t address = 0;
          
          parsehexnum(&ctx->inbuf[1], &address);
          
	  gdb_cmd_proc_continue (ctx, (m_uint64_t)address, 1);	/* step one instruction */
          
          return (GDB_CONT_DONT_RESUME_VM);
        }

        case 'v':
            if (strncmp(ctx->inbuf + 1, "Cont", 4) == 0) {
                m_int32_t  res = 1,
                           thread = 0,
                           stepping = 0;
                           
                int action, signal;
                    
                char* pInbuf = ctx->inbuf;
                
                pInbuf += 5;
                
                if (*pInbuf == '?') {
                    strcpy(ctx->outbuf,  "vCont;c;C;s;S");
                    break;
                }
                
                if (*pInbuf++ != ';') {
                    break;
                }
                action = *pInbuf++;
                signal = 0;
                    
                switch(action)
                {
                case 'C':
                {
                    stepping = 0;
                    signal = strtoul(pInbuf, (char**)pInbuf, 16);
                    break;
                }
                case 'S':
                {
                    stepping = 1;
                    signal = strtoul(pInbuf, (char**)pInbuf, 16);
                    break;
                }
                case 'c':
                {
                    stepping = 0;
                    break;
                }
                case 's':
                {
                    stepping = 1;
                    break;
                }
                default:
                    res = 0;
                    break;
                } // end switch
                
                thread = 0;
                
                if (*pInbuf == ':') {
                    thread = strtoull(pInbuf + 1, (char**)pInbuf, 16);
                }

                if (res) {
                    // TODO: implement "with signal" commands
                    gdb_cmd_proc_continue(ctx, 0, stepping);

                    if (stepping) {
                        return (GDB_CONT_DONT_RESUME_VM);
                    } else {
                        return (GDB_CONT_RESUME_VM);
                    }
                }
            }
            break;

	  /* ZT,AA..AA,LLLL: Insert a type T breakpoint or watchpoint 
	     starting at address AA..AA and covering the next LL bytes */
	case 'Z':
           gdb_cmd_insert_breakpoint(ctx);
           break;
           
	  /* zT,AA..AA,LLLL: Remove a type T breakpoint or watchpoint 
	     starting at address AA..AA and covering the next LL bytes */
	case 'z':
           gdb_cmd_remove_breakpoint(ctx);
           break;

	} /* switch */

      /* reply to the request and flush the caches */
      putpacket (ctx);
    }
    
    return (GDB_EXIT_STOP_VM);
}

//
// CISCO compress routine
//
void
gdb_compress (char *src, char *dest)
{
  char previous = 0;
  int repeat = 0;

  // FIXME: do this right
  strcpy (dest, src);
  return;

  do
    {
      if ((*src == previous) && (repeat != 255))
	{
	  repeat++;
	}
      else
	{
	  if (repeat > 3)
	    {
	      dest = dest - repeat;
	      *dest++ = '*';
	      *dest++ = tohexchar (repeat >> 4);
	      *dest++ = tohexchar (repeat);
	    }
	  repeat = 0;
	}
      *dest++ = *src;
      previous = *src;
    }
  while (*src++);
}

//
// CISCO expand routine
//
void
gdb_expand (char *src, char *dest)
{
  int i;
  int repeat;

  // FIXME: do this right
  strcpy (dest, src);
  return;

  do
    {
      if (*src == '*')
	{
	  repeat = (chartohex (src[1]) << 4) + chartohex (src[2]);
	  for (i = 0; i < repeat; i++)
	    {
	      *dest++ = *(src - 1);
	    }
	  src += 2;
	}
      else
	{
	  *dest++ = *src;
	}
    }
  while (*src++);
}

char
tohexchar (unsigned char c)
{
  c &= 0x0f;

  return (hexchars[c]);
}

int
chartohex (unsigned char ch)
{
  if ((ch >= 'A') && (ch <= 'F'))
    return (ch - 'A' + 10);
  if ((ch >= 'a') && (ch <= 'f'))
    return (ch - 'a' + 10);
  if ((ch >= '0') && (ch <= '9'))
    return (ch - '0');
  return (0);
}

int
gdb_printf (const char *fmt, ...)
{
  va_list ap;
  int res;

  va_start (ap, fmt);
  res = vprintf (fmt, ap);
  va_end (ap);

  return res;
}
