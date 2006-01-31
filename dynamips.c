/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Many thanks to Nicolas Szalay for his patch
 * for the command line parsing and virtual machine 
 * settings (RAM, ROM, NVRAM, ...)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>

#include ARCH_INC_FILE

#include "rbtree.h"
#include "cp0.h"
#include "memory.h"
#include "cpu.h"
#include "device.h"
#include "mips64_exec.h"
#include "dev_c7200.h"
#include "dev_c7200_bay.h"
#include "dev_vtty.h"
#include "ptask.h"
#include "atm.h"
#include "crc.h"
#include "net_io.h"
#include "net_io_bridge.h"

#ifdef GEN_ETH
#include "gen_eth.h"
#endif

#ifdef PROFILE
#include "profiler.h"
#endif

/* Default name for logfile */
#define LOGFILE_DEFAULT_NAME  "pred_log0.txt"

/* Software version */
static const char *sw_version = "0.2.3c-"JIT_ARCH;

/* Log file */
FILE *log_file = NULL;

/* Instruction block trace (produces tons of logs!) */
int insn_itrace = 0;

/* JIT use */
int jit_use = JIT_SUPPORT;

/* VM flags */
volatile int vm_save_state = 0;
volatile int vm_running = 0;

/* Cisco 7200 router instance */
c7200_t c7200_router;

/* By default, use embedded ROM */
char *rom_filename = NULL;

/* RAM size (in Mb, by default 256) */
u_int ram_size = 256;

/* ROM size (in Mb, by default 4) */
u_int rom_size = 4;

/* NVRAM size (in Kb, by default 128) */
u_int nvram_size = 128;

/* Config register */
u_int conf_reg = 0x2102;

/* Clock divisor (see cp0.c) */
u_int clock_divisor = 2;

/* Port Adapter descriptions */
static char *pa_desc[MAX_PA_BAYS];
static int pa_index = 0;

/* Console port VTTY type and parameters */
int vtty_con_type = VTTY_TYPE_TERM;
int vtty_con_tcp_port;

/* AUX port VTTY type and parameters */
int vtty_aux_type = VTTY_TYPE_NONE;
int vtty_aux_tcp_port;

/* Symbols */
rbtree_tree *sym_tree = NULL;

/* ELF entry point */
m_uint32_t ios_entry_point;

/* Symbol lookup */
struct symbol *sym_lookup(m_uint64_t addr)
{
   return(rbtree_lookup(sym_tree,&addr));
}

/* Insert a new symbol */
struct symbol *sym_insert(char *name,m_uint64_t addr)
{
   struct symbol *sym;
   size_t len;

   len = strlen(name);

   if (!(sym = malloc(len + sizeof(*sym))))
      return NULL;
   
   memcpy(sym->name,name,len+1);
   sym->addr = addr;

   if (rbtree_insert(sym_tree,sym,sym) == -1) {
      free(sym);
      return NULL;
   }

   return sym;
}

/* Symbol comparison function */
static int sym_compare(m_uint64_t *a1,struct symbol *sym)
{
   if (*a1 > sym->addr)
      return(1);

   if (*a1 < sym->addr)
      return(-1);

   return(0);
}

/* Create the symbol tree */
int sym_create_tree(void)
{
   sym_tree = rbtree_create((tree_fcompare)sym_compare,NULL);
   return(sym_tree ? 0 : -1);
}

/* Generic signal handler */
void signal_gen_handler(int sig)
{
   switch(sig) {
      case SIGHUP:
         insn_itrace = 1 - insn_itrace;
         printf("Instruction block trace %sabled\n",
                insn_itrace ? "en" : "dis");
         break;

      case SIGQUIT:
         /* save VM context */
         vm_save_state = TRUE;
         vm_running = FALSE;
         break;

      default:
         fprintf(stderr,"Unhandled signal %d\n",sig);
   }
}

/* Setups signals */
static void setup_signals(void)
{
   struct sigaction act;

   memset(&act,0,sizeof(act));
   act.sa_handler = signal_gen_handler;
   act.sa_flags = SA_RESTART;
   sigaction(SIGHUP,&act,NULL);
   sigaction(SIGQUIT,&act,NULL);
}

/* Load a raw image into the simulated memory */
int load_raw_image(cpu_mips_t *cpu,char *filename,m_uint64_t vaddr)
{   
   struct stat file_info;
   size_t len,clen;
   void *haddr;
   FILE *bfd;

   if (!(bfd = fopen(filename,"r"))) {
      perror("fopen");
      return(-1);
   }

   if (fstat(fileno(bfd),&file_info) == -1) {
      perror("stat");
      return(-1);
   }

   len = file_info.st_size;

   printf("Loading RAW file '%s' at virtual address 0x%llx (size=%lu)\n",
          filename,vaddr,(u_long)len);

   while(len > 0)
   {
      haddr = cpu->mem_op_lookup(cpu,vaddr);
   
      if (!haddr) {
         fprintf(stderr,"load_raw_image: invalid load address 0x%llx\n",
                 vaddr);
         return(-1);
      }

      if (len > MIPS_MIN_PAGE_SIZE)
         clen = MIPS_MIN_PAGE_SIZE;
      else
         clen = len;

      clen = fread((u_char *)haddr,clen,1,bfd);

      if (clen != 1)
         break;
      
      vaddr += MIPS_MIN_PAGE_SIZE;
      len -= clen;
   }
   
   fclose(bfd);
   return(0);
}

/* Load an ELF image into the simulated memory */
int load_elf_image(cpu_mips_t *cpu,char *filename,m_uint32_t *entry_point)
{
   m_uint64_t vaddr;
   void *haddr;
   Elf32_Ehdr *ehdr;
   Elf32_Phdr *phdr;
   Elf *img_elf;
   size_t len,clen;
   int i,fd;
   FILE *bfd;
   
   if ((fd = open(filename,O_RDONLY)) == -1)
      return(-1);

   if (elf_version(EV_CURRENT) == EV_NONE) {
      fprintf(stderr,"load_elf_image: library out of date\n");
      return(-1);
   }

   if (!(img_elf = elf_begin(fd,ELF_C_READ,NULL))) {
      fprintf(stderr,"load_elf_image: elf_begin: %s\n",
              elf_errmsg(elf_errno()));
      return(-1);
   }

   if (!(phdr = elf32_getphdr(img_elf))) {
      fprintf(stderr,"load_elf_image: elf32_getphdr: %s\n",
              elf_errmsg(elf_errno()));
      return(-1);
   }

   ehdr = elf32_getehdr(img_elf);
   phdr = elf32_getphdr(img_elf);

   printf("Loading ELF file '%s'...\n",filename);
   bfd = fdopen(fd,"rb");

   if (!bfd) {
      perror("load_elf_image: fdopen");
      return(-1);
   }

   for(i=0;i<ehdr->e_phnum;i++,phdr++)
   {
      fseek(bfd,phdr->p_offset,SEEK_SET);

      vaddr = (m_uint64_t)phdr->p_vaddr;
      len = phdr->p_filesz;

      printf("   * Adding section at virtual address 0x%llx\n",vaddr);

      while(len > 0)
      {
         haddr = cpu->mem_op_lookup(cpu,vaddr);
   
         if (!haddr) {
            fprintf(stderr,"load_elf_image: invalid load address 0x%llx\n",
                    vaddr);
            return(-1);
         }

         if (len > MIPS_MIN_PAGE_SIZE)
            clen = MIPS_MIN_PAGE_SIZE;
         else
            clen = len;

         clen = fread((u_char *)haddr,clen,1,bfd);

         if (clen != 1)
            break;

         vaddr += MIPS_MIN_PAGE_SIZE;
         len -= clen;
      }
   }

   printf("ELF entry point: 0x%x\n",ehdr->e_entry);

   if (entry_point)
      *entry_point = ehdr->e_entry;

   return(0);
}

/* Load a symbol file */
int load_sym_file(char *filename)
{
   char buffer[4096],func_name[128];
   m_uint64_t addr;
   char sym_type;
   FILE *fd;

   if ((!sym_tree) && (sym_create_tree() == -1)) {
      fprintf(stderr,"Unable to create symbol tree.\n");
      return(-1);
   }

   if (!(fd = fopen(filename,"r"))) {
      perror("load_sym_file: fopen");
      return(-1);
   }

   while(!feof(fd)) {
      fgets(buffer,sizeof(buffer),fd);

      if (sscanf(buffer,"%llx %c %s",&addr,&sym_type,func_name) == 3) {
         sym_insert(func_name,addr);
      }
   }

   fclose(fd);
   return(0);
}

/* Display the command line use */
static void show_usage(int argc,char *argv[])
{
   printf("Usage: %s [options] <ios_image>\n\n",argv[0]);
   
   printf("Available options:\n"
          "  -r <ram_size>   : Set the virtual RAM size (default is %d Mb)\n"
          "  -o <rom_size>   : Set the virtual ROM size (default is %d Mb)\n"
          "  -n <nvram_size> : Set the NVRAM size (default is %d Kb)\n"
          "  -l <log_file>   : Set logging file (default is %s)\n"
          "  -C <cfg_file>   : Import an IOS configuration file into NVRAM\n"
          "  -R <rom_file>   : Load an alternate ROM (default is embedded)\n"
          "  -s <sym_file>   : Load symbol file\n"
          "  -c <conf_reg>   : Set the configuration register "
          "(default is 0x%04x)\n"
          "  -m <mac_addr>   : Set the MAC address of the chassis "
          "(IOS chooses default)\n"
          "  -k <clock_div>  : Set the clock divisor (default is %d)\n"
          "  -T <port>       : Console is on TCP <port> "
          "(default is on the terminal)\n"
          "  -A <port>       : AUX is on TCP <port> (default is no AUX port)\n"
          "  -i              : Instruction block trace, very slow\n"
          "  -j              : Disable the JIT compiler, very slow\n"
          "  -t <npe_type>   : Select NPE type\n"
          "  -M <midplane>   : Select Midplane (\"std\" or \"vxr\")\n"
          "  -p <pa_desc>    : Define a Port Adapter\n"
          "  -a <cfg_file>   : Virtual ATM switch configuration file\n"
          "  -b <cfg_file>   : Virtual bridge configuration file\n"
          "  -e              : Show network device list\n"
          "\n",
          ram_size,rom_size,nvram_size,LOGFILE_DEFAULT_NAME,
          conf_reg,clock_divisor);

   printf("<pa_desc> format:\n"
          "   \"slot:pa_driver:netio_type{:netio_parameters}\"\n"
          "\n");
   
   /* Show the possible NPE drivers */
   c7200_npe_show_drivers();

   /* Show the possible PA drivers */
   c7200_pa_show_drivers();

   /* Show the possible NETIO types */
   netio_show_types();
}

int main(int argc,char *argv[])
{
   char *options_list = "r:o:n:c:m:l:C:ijt:p:k:T:A:a:b:s:R:M:e";
   char *log_file_name = NULL;
   char *ios_image_name;
   char *ios_cfg_file = NULL;
   char *mac_addr = NULL;
   int option;
   cpu_mips_t *cpu0;
   m_uint32_t rom_entry_point;

#ifdef PROFILE
   atexit(profiler_savestat);
#endif

   printf("Cisco 7200 Simulation Platform (version %s)\n",sw_version);
   printf("Copyright (c) 2005,2006 Christophe Fillot.\n\n");

   memset(&c7200_router,0,sizeof(c7200_router));

   /* Initialize CRC functions */
   crc_init();

   /* Initialize ATM code */
   atm_init();

   /* Command line arguments : early try */
   opterr = 0;

   while((option = getopt(argc,argv,options_list)) != -1) {
      switch(option)
      {
         /* RAM size */
         case 'r':
            ram_size = strtol(optarg, NULL, 10);
            printf("Virtual RAM size set to %d MB.\n",ram_size);
            break;

         /* ROM size */
         case 'o':
            rom_size = strtol(optarg, NULL, 10);
            printf("Virtual ROM size set to %d MB.\n",rom_size);
            break;

         /* NVRAM size */
         case 'n':
            nvram_size = strtol(optarg, NULL, 10);
            printf("NVRAM size set to %d KB.\n", nvram_size);
            break;

         /* Config Register */
         case 'c':
            conf_reg = strtol(optarg, NULL, 0);
            printf("Config. Register set to 0x%x.\n",conf_reg);
            break;

         /* Set the base MAC address */
         case 'm':
            mac_addr = optarg;
            printf("MAC address set to '%s'.\n",mac_addr);
            break;

         /* Log file */
         case 'l':
            if (!(log_file_name = malloc(strlen(optarg)+1))) {
               fprintf(stderr,"Unable to set log file name.\n");
               exit(EXIT_FAILURE);
            }
            strcpy(log_file_name, optarg);
            printf("Log file: writing to %s\n",log_file_name);
            break;

         /* IOS configuration file */
         case 'C':
            ios_cfg_file = optarg;
            break;

         /* Alternate ROM */
         case 'R':
            rom_filename = optarg;
            break;

         /* Symbol file */
         case 's':
            load_sym_file(optarg);
            break;
            
         /* Instruction block trace */
         case 'i':
            insn_itrace = TRUE;
            break;
	  
         /* Disable JIT */
         case 'j':
            jit_use = FALSE;
            break;

         /* NPE type */
         case 't':
            c7200_router.npe_type = optarg;
            break;

         /* Midplane type */
         case 'M':
            c7200_router.midplane_type = optarg;
            break;
            
         /* PA settings */
         case 'p':
            if (pa_index == MAX_PA_BAYS)
               fprintf(stderr,"All PA slots are filled.\n");               
            else
               pa_desc[pa_index++] = optarg;
            break;

         /* Clock divisor */
         case 'k':
            clock_divisor = atoi(optarg);

            if (!clock_divisor) {
               fprintf(stderr,"Invalid Clock Divisor specified!\n");
               exit(EXIT_FAILURE);
            }

            printf("Using a clock divisor of %d.\n",clock_divisor);
            break;

         /* TCP server for Console Port */
         case 'T':
            vtty_con_type = VTTY_TYPE_TCP;
            vtty_con_tcp_port = atoi(optarg);
            break;

         /* TCP server for AUX Port */
         case 'A':
            vtty_aux_type = VTTY_TYPE_TCP;
            vtty_aux_tcp_port = atoi(optarg);
            break;

         /* Virtual ATM switch */
         case 'a':
            if (atmsw_start(optarg) == -1)
               exit(EXIT_FAILURE);
            break;

         /* Virtual bridge */
         case 'b':
            if (netio_bridge_start(optarg) == -1)
               exit(EXIT_FAILURE);
            break;

#ifdef GEN_ETH
         /* Ethernet device list */
         case 'e':
            gen_eth_show_dev_list();
            exit(EXIT_SUCCESS);           
#endif            

         /* Oops ! */
         case '?':
            show_usage(argc,argv);
            exit(EXIT_FAILURE);
      }
   }

   /* Last argument, this is the IOS filename */
   if (optind == (argc - 1)) {
      /* setting IOS image file	*/
      ios_image_name = argv[optind];
      printf("IOS image file: %s\n\n", ios_image_name);
   } else { 
      /* IOS missing */
      fprintf(stderr,"Please specify an IOS image filename\n");
      show_usage(argc,argv);
      exit(EXIT_FAILURE);
   }

   /* Set the default value of the log file name */
   if (!log_file_name) {
      if (!(log_file_name = malloc(strlen(LOGFILE_DEFAULT_NAME)+1))) {
         fprintf(stderr,"Unable to set log file name.\n");
         exit(EXIT_FAILURE);
      }
      strcpy(log_file_name,LOGFILE_DEFAULT_NAME);
   }

   if (!(log_file = fopen(log_file_name,"w"))) {
      fprintf(stderr,"Unable to create log file.\n");
      exit(EXIT_FAILURE);
   }

   /* Periodic tasks initialization */
   if (ptask_init(0) == -1)
      exit(EXIT_FAILURE);

   /* Create instruction lookup tables */
   mips64_jit_create_ilt();
   mips64_exec_create_ilt();

   /* Create a CPU group */
   sys_cpu_group = cpu_group_create("System CPU Group");

   /* Initialize the virtual MIPS processor */
   if (!(cpu0 = cpu_create(0))) {
      fprintf(stderr,"Unable to create CPU0!\n");
      exit(EXIT_FAILURE);
   }

   /* Add this CPU to the system CPU group */
   cpu_group_add(sys_cpu_group,cpu0);
   c7200_router.cpu_group = sys_cpu_group;

   /* Initialize the C7200 platform */
   if (c7200_init_platform(&c7200_router,pa_desc,mac_addr) == -1) {
      fprintf(stderr,"Unable to initialize the C7200 platform hardware.\n");
      exit(EXIT_FAILURE);
   }

   /* Load IOS configuration file */
   if (ios_cfg_file != NULL) {
      dev_nvram_push_config(sys_cpu_group,ios_cfg_file);
      conf_reg &= ~0x40;
   }

   /* Load ROM (ELF image or embedded) */
   rom_entry_point = 0xbfc00000;

   if (rom_filename) {
      if (load_elf_image(cpu0,rom_filename,&rom_entry_point) < 0) {
         fprintf(stderr,"Unable to load alternate ROM '%s', "
                 "fallback to embedded ROM.\n\n",rom_filename);
         rom_filename = NULL;
      }
   }

   /* Load IOS image */
   if (load_elf_image(cpu0,ios_image_name,&ios_entry_point) < 0) {
      fprintf(stderr,"Cisco IOS load failed.\n");
      exit(EXIT_FAILURE);
   }

   cpu0->pc = sign_extend(rom_entry_point,32);
   cpu0->cp0.reg[MIPS_CP0_PRID] = 0x2012ULL;
   cpu0->cp0.reg[MIPS_CP0_CONFIG] = 0x00c08ff0ULL;

   setup_signals();

   /* Launch the simulation */
   printf("\nStarting simulation (CPU0 PC=0x%llx), JIT is %sabled.\n",
          cpu0->pc,jit_use ? "en":"dis");

   cpu_start(cpu0);

   /* Run until all CPU of the system CPU group are halted */
   while(!cpu_group_check_state(sys_cpu_group,MIPS_CPU_HALTED))
      usleep(200000);

   printf("\n\nSimulation halted (CPU0 PC=0x%llx).\n\n",cpu0->pc);
   return(0);
}
