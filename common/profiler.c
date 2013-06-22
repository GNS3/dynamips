/*
 * Contribution of Mtve.
 */

#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#define THREADED 1

#if THREADED
#include <pthread.h>
#endif

/*
 * Call to profiling routine .mcount is automatically inserted by gcc -p.
 *
 * However, standard .mcount from (g)libc is not working well for me
 * with optimized (-O3 -fomit-frame-pointer) threaded code,
 * at least because it doesn't save all registers.
 *
 * So here is another square wheel.  It works only on IA32 (i386).
 *
 * Theory:
 *   .mcount is called like this
 *
 * 08048479 <some_func>:
 * 8048679:       55                      push    %ebp         # can be
 * 804847a:       89 e5                   movl    %esp,%ebp    # absent
 * 804847c:       83 ec 1c                subl    $28,%esp
 * 804847f:       55                      pushl   %ebp
 * 8048480:       57                      pushl   %edi
 * 8048481:       56                      pushl   %esi
 * 8048482:       53                      pushl   %ebx
 * 8048483:       e8 94 fe ff ff          call    .mcount
 * 08048488 <some_func_x>
 *
 * So in the entrance of .mcount we have in stack
 *
 * %esp -> some_func_x (dword), where mcount should return
 *         saved registers (4 dwords in example)
 *         stack frame (28 bytes in example)
 *         some_func_callee (dword)
 *
 * We will:
 * - check if the code of some_func matches this pattern
 * - find some_func address and depth of stack
 * - modify stack by replacing some_func_callee address to ours
 * - collect statistic
 */

/* better to be a prime number */
#define FUNCSMAX 32749

#define CSTACKSIZE 256

#if THREADED
/* better to be a prime number */
#define THREADSMAX 37
#else
#define THREADSMAX 1
#endif

static struct {
   int       addr;
   int       enters;
   int       exits;
   int       aways;
   int       rets;
   long long timetotal;
   long long timeoutside;
} arr[FUNCSMAX + 1];

static struct {
#if THREADED
   pthread_t tid;
#endif
   int       depth;
   int       ret[CSTACKSIZE];
   int       func[CSTACKSIZE];
} cstack[THREADSMAX];

#define core() (*(char *)0 = 0)

#if __GNUC__ > 2

#define NOPROF __attribute__ ((no_instrument_function))

/* forward declaration of all functions */
static inline long long curtime() NOPROF;
static inline int findaddr() NOPROF;
static inline int findthread() NOPROF;
static void stat_enter() NOPROF;
static void stat_exit() NOPROF;
static void stat_away() NOPROF;
static void stat_ret() NOPROF;
void profiler__asm_enter_stub() NOPROF;
void profiler__asm_exit_stub() NOPROF;
void profiler__c_enter() NOPROF;
void profiler__c_exit() NOPROF;
void profiler_savestat() NOPROF;

#else
#warning be sure to compile profiler.c WITHOUT -p flag
#endif

static inline long long curtime(void)
{
   long long t;

   asm volatile(".byte 15;.byte 49" : "=A"(t)); /* RDTSC */
   return t;
}

static inline int findaddr(int addr)
{
   int i,j;

   i = j = addr % FUNCSMAX;
   do {
      if (arr[i].addr == addr) {
         return i;
      } else if (arr[i].addr == 0) {
         arr[i].addr = addr;
         return i;
      }
      i = (i+1) % FUNCSMAX;
   } while (i != j);
   core(); /* increase FUNCSMAX */
   return(FUNCSMAX);
}

static inline int findthread(void)
{
#if THREADED
   int i,j;
   pthread_t k = pthread_self();

   i = j = (int)k % THREADSMAX;
   do {
      if (cstack[i].tid == k) {
         return i;
      } else if (cstack[i].tid == 0) {
         cstack[i].tid = k;
         return i;
      }
      i = (i+1) % THREADSMAX;
   } while (i != j);
   core(); /* increase THREADSMAX */
#endif
   return(0);
}

static void stat_enter(int slot)
{
   arr[slot].enters++;
   arr[slot].timetotal -= curtime();
}

static void stat_exit(int slot)
{
   arr[slot].exits++;
   arr[slot].timetotal += curtime();
}

static void stat_away(int slot)
{
   arr[slot].aways++;
   arr[slot].timeoutside -= curtime();
}

static void stat_ret(int slot)
{
   arr[slot].rets++;
   arr[slot].timeoutside += curtime();
}

void profiler__asm_enter(void);
void profiler__asm_exit(void);

#define A __asm__

/*
 * that't really weird but compatible with both gcc2 and gcc3
 *
 * things i don't want to care of
 * - what size on stack pusha/pops use
 * - what current function framing is
 */
void profiler__asm_enter_stub(void)
{
   A("   .globl .mcount             ");
   A("   .globl profiler__asm_enter ");
   A("profiler__asm_enter:          ");
   A(".mcount:                      ");
   A("   pushl %eax                 "); /* save %eax                         */
   A("   movl  %esp,%eax            "); /*   %eax = old %esp - 4             */
   A("   pusha                      "); /*   save all registers              */
   A("   push  %eax                 "); /*     push parameter to stack       */
   A("   call  profiler__c_enter    "); /*     call c routine                */
   A("   pop   %eax                 "); /*     clear parameter from stack    */
   A("   popa                       "); /*   restore all registers           */
   A("   pop   %eax                 "); /* restore %eax                      */
   A("   ret                        "); /* return                            */
}

void profiler__asm_exit_stub(void)
{
   A("profiler__asm_exit:           ");
   A("   pushl $0xdeadbeaf          "); /* placeholder to return address     */
   A("   pushl %eax                 "); /* save %eax                         */
   A("   movl  %esp,%eax            "); /*   %eax = addr of placeholder - 4  */
   A("   pusha                      "); /*   save all registers              */
   A("   pushl %eax                 "); /*     push parameter to stack       */
   A("   call  profiler__c_exit     "); /*     call C routine                */
   A("   popl  %eax                 "); /*     clear parameter from stack    */
   A("   popa                       "); /*   restore all registers           */
   A("   popl  %eax                 "); /* restore %eax                      */
   A("   ret                        "); /* return                            */
}

void profiler__c_enter(int *sp_1)
{
   unsigned char *pc;
   int stdepth = 2, i, thr, slot, gcc2 = 1;

   if (sizeof(int) != 4)
      core(); /* sizeof int != 4 */
   if (sizeof(long long) != 8)
      core(); /* sizeof long long != 8 */
   if (sizeof(void *) != 4)
      core(); /* sizeof pointer != 4 */

   pc = (char *)(sp_1[1]);

   pc -= 5;
   if (*pc != 0xe8) /* call <relative> */
      core(); /* called not by 0xe8 */
   if ((int)pc + 5 + *(int *)(pc+1) != (int)profiler__asm_enter)
      core(); /* call points not to .mcount */

   if (pc[-1] == 0x53) /* push %ebx */
      pc--, stdepth++;
   if (pc[-1] == 0x56) /* push %esi */
      pc--, stdepth++;
   if (pc[-1] == 0x57) /* push %edi */
      pc--, stdepth++;
   if (pc[-1] == 0x55) /* push %ebp */
      pc--, stdepth++;

   if (pc[-6]==0x81 && pc[-5]==0xec && pc[-2]==0 && pc[-1]==0) {
      /* sub <dword>,%esp */
      stdepth += *(int *)(pc - 4)/4;
      pc -= 6;
   } else if (pc[-3]==0x83 && pc[-2]==0xec && pc[-1]%4==0) {
      /* sub <byte>,%esp */
      stdepth += pc[-1]/4;
      pc -= 3;
   } else
      gcc2 = 0;

   while (pc[-1] >= 0x50 && pc[-1] <= 0x57) /* push %e[reg] */
      pc--, stdepth++;

   /* "pushl %ebp; movl %esp,%ebp;" */
   if (pc[-3]==0x55 && pc[-2]==0x89 && pc[-1]==0xe5) {
      stdepth++;
      pc -= 3;
   } else if(!gcc2)
      core(); /* unknown prologue, examine  x/10i pc-10 */

   /*
    * Now we know that it's standard prologue, so we modify the stack
    */
   thr = findthread();
   slot = findaddr((int)pc);

   i = cstack[thr].depth++;
   if(i >= CSTACKSIZE)
      core(); /* call stack overflow */

   cstack[thr].func[i] = slot;
   cstack[thr].ret[i] = sp_1[stdepth];
   sp_1[stdepth] = (int)profiler__asm_exit;

   if (i > 0)
      stat_away(cstack[thr].func[i - 1]);
   stat_enter(slot);
}

void profiler__c_exit(int *sp)
{
   int i, thr;

   thr = findthread();
   i = --cstack[thr].depth;
   if (i < 0)
      core(); /* call stack underflow */
   sp[1] = cstack[thr].ret[i];

   stat_exit(cstack[thr].func[i]);
   if (i > 0)
      stat_ret(cstack[thr].func[i - 1]);
#if THREADED
   else
      cstack[thr].tid = 0; /* free this stack */
#endif
}

#ifndef PROFILE_FILE
#error define PROFILE_FILE where to save statistic
#endif

static void mywrite(int fd,char *str)
{
   int len, i;

   for (len = strlen(str); len > 0; str += i, len -= i)
      if ((i = write(fd,str,len)) < 0)
         return;
}

void profiler_savestat(void)
{
   int i, fd;
   char buf[1024];

   fd = open(PROFILE_FILE,O_CREAT | O_TRUNC | O_WRONLY,0666);
   if (fd < 0) {
      mywrite(2,"open " PROFILE_FILE " failed - ");
      mywrite(2,strerror(errno));
      mywrite(2,"\n");
      return;
   }

   snprintf(buf,sizeof(buf),"\nProfiling statistic %s at time %lld:\n"
            "\n%8s %10s %10s %10s %10s %20s %20s\n",PROFILE_FILE,curtime(),
            "Function","Enters","Exits","Aways","Returns",
            "Cycles_Total","Cycles_Inside");
   mywrite(fd,buf);

   for (i = 0; i < FUNCSMAX; i++)
      if (arr[i].addr) {
         snprintf(buf,sizeof(buf),"%08x %10d %10d %10d %10d %20lld %20lld\n",
                  arr[i].addr,arr[i].enters,arr[i].exits,
                  arr[i].aways,arr[i].rets,
                  arr[i].timetotal + (arr[i].enters-arr[i].exits) * curtime(),
                  arr[i].timetotal - arr[i].timeoutside + curtime() *
                  (arr[i].enters-arr[i].exits-arr[i].aways+arr[i].rets));
         mywrite(fd,buf);
      }

   close(fd);
}
