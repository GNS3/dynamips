/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 * Patched by Jeremy Grossmann for the GNS3 project (www.gns3.net)
 *
 * Many thanks to Nicolas Szalay for his patch
 * for the command line parsing and virtual machine 
 * settings (RAM, ROM, NVRAM, ...)
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>
#include <getopt.h>

#include "dynamips.h"
#include "gen_uuid.h"
#include "cpu.h"
#include "vm.h"

#ifdef USE_UNSTABLE
#include "tcb.h"
#endif

#include "mips64_exec.h"
#include "mips64_jit.h"
#include "ppc32_exec.h"
#include "ppc32_jit.h"
#include "dev_c7200.h"
#include "dev_c3600.h"
#include "dev_c2691.h"
#include "dev_c3725.h"
#include "dev_c3745.h"
#include "dev_c2600.h"
#include "dev_c1700.h"
#include "dev_c6msfc1.h"
#include "dev_c6sup1.h"
#include "ppc32_vmtest.h"
#include "dev_vtty.h"
#include "ptask.h"
#include "timer.h"
#include "plugin.h"
#include "registry.h"
#include "hypervisor.h"
#include "net_io.h"
#include "net_io_bridge.h"
#include "net_io_filter.h"
#include "crc.h"
#include "atm.h"
#include "atm_bridge.h"
#include "frame_relay.h"
#include "eth_switch.h"
#ifdef GEN_ETH
#include "gen_eth.h"
#endif
#ifdef PROFILE
#include "profiler.h"
#endif

/* Default name for logfile */
#define LOGFILE_DEFAULT_NAME  "dynamips_log.txt"

/* Operating system name */
const char *os_name = STRINGIFY(OSNAME);

/* Software version */
const char *sw_version = DYNAMIPS_VERSION"-"JIT_ARCH;

/* Software version tag */
const char *sw_version_tag = "2014092320";

/* Hypervisor */
int hypervisor_mode = 0;
int hypervisor_tcp_port = 0;
char *hypervisor_ip_address = NULL;

/* Log file */
char *log_file_name = NULL;
FILE *log_file = NULL;

/* VM flags */
volatile int vm_save_state = 0;

/* Default platform */
static char *default_platform = "7200";

/* Binding address (NULL means any or 0.0.0.0) */
char *binding_addr = NULL;

/* Generic signal handler */
void signal_gen_handler(int sig)
{
   switch(sig) {
      case SIGHUP:
         /* For future use */
         break;

      case SIGQUIT:
         /* save VM context */
         vm_save_state = TRUE;
         break;

      /* Handle SIGPIPE by ignoring it */
      case SIGPIPE:
         fprintf(stderr,"Error: unwanted SIGPIPE.\n");
         break;
		 
      case SIGINT:
         /* CTRL+C has been pressed */
         if (hypervisor_mode)
            hypervisor_stopsig();
         else {
            /* In theory, this shouldn't happen thanks to VTTY settings */
            vm_instance_t *vm;

            if ((vm = vm_acquire("default")) != NULL) {
               /* Only forward ctrl-c if user has requested local terminal */
               if (vm->vtty_con_type == VTTY_TYPE_TERM) {
                  vtty_store_ctrlc(vm->vtty_con);
               } else {
                  vm_stop(vm);
               }
               vm_release(vm);
            } else {
               fprintf(stderr,"Error: Cannot acquire instance handle.\n");
            }
         }
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
   sigaction(SIGINT,&act,NULL);
   sigaction(SIGPIPE,&act,NULL);
}

/* Create general log file */
static void create_log_file(void)
{
   /* Set the default value of the log file name */
   if (!log_file_name) {
      if (!(log_file_name = strdup(LOGFILE_DEFAULT_NAME))) {
         fprintf(stderr,"Unable to set log file name.\n");
         exit(EXIT_FAILURE);
      }
   }

   if (!(log_file = fopen(log_file_name,"w"))) {
      fprintf(stderr,"Unable to create log file (%s).\n",strerror(errno));
      exit(EXIT_FAILURE);
   }
}

/* Close general log file */
static void close_log_file(void)
{
   if (log_file) fclose(log_file);
   free(log_file_name);

   log_file = NULL;
   log_file_name = NULL;
}

/* Display the command line use */
static void show_usage(vm_instance_t *vm,int argc,char *argv[])
{
   printf("Usage: %s [options] <ios_image>\n\n",argv[0]);
   
   printf("Available options:\n"
          "  -H [<ip_address>:]<tcp_port> : Run in hypervisor mode\n\n"
          "  -P <platform>      : Platform to emulate (7200, 3600, "
          "2691, 3725, 3745, 2600 or 1700) "
          "(default: 7200)\n\n"
          "  -l <log_file>      : Set logging file (default is %s)\n"
          "  -j                 : Disable the JIT compiler, very slow\n"
          "  --idle-pc <pc>     : Set the idle PC (default: disabled)\n"
          "  --timer-itv <val>  : Timer IRQ interval check (default: %u)\n"
          "\n"
          "  -i <instance>      : Set instance ID\n"
          "  -r <ram_size>      : Set the virtual RAM size (default: %u Mb)\n"
          "  -o <rom_size>      : Set the virtual ROM size (default: %u Mb)\n"
          "  -n <nvram_size>    : Set the NVRAM size (default: %d Kb)\n"
          "  -c <conf_reg>      : Set the configuration register "
          "(default: 0x%04x)\n"
          "  -m <mac_addr>      : Set the MAC address of the chassis\n"
          "                       (default: automatically generated)\n"
          "  -C, --startup-config <file> : Import IOS configuration file into NVRAM\n"
          "  --private-config <file> : Import IOS configuration file into NVRAM\n"
          "  -X                 : Do not use a file to simulate RAM (faster)\n"
          "  -G <ghost_file>    : Use a ghost file to simulate RAM\n"
          "  -g <ghost_file>    : Generate a ghost RAM file\n"
          "  --sparse-mem       : Use sparse memory\n"
          "  -R <rom_file>      : Load an alternate ROM (default: embedded)\n"
          "  -k <clock_div>     : Set the clock divisor (default: %d)\n"
          "\n"
          "  -T <port>          : Console is on TCP <port>\n"
          "  -U <si_desc>       : Console in on serial interface <si_desc>\n"
          "                       (default is on the terminal)\n"
          "\n"
          "  -A <port>          : AUX is on TCP <port>\n"
          "  -B <si_desc>       : AUX is on serial interface <si_desc>\n"
          "                       (default is no AUX port)\n"
          "\n"
          "  --disk0 <size>     : Set PCMCIA ATA disk0: size "
          "(default: %u Mb)\n"
          "  --disk1 <size>     : Set PCMCIA ATA disk1: size "
          "(default: %u Mb)\n"
          "\n"
          "  --noctrl           : Disable ctrl+] monitor console\n"
          "  --notelnetmsg      : Disable message when using tcp console/aux\n"
          "  --filepid filename : Store dynamips pid in a file\n"
          "\n",
          LOGFILE_DEFAULT_NAME,VM_TIMER_IRQ_CHECK_ITV,
          vm->ram_size,vm->rom_size,vm->nvram_size,vm->conf_reg_setup,
          vm->clock_divisor,vm->pcmcia_disk_size[0],vm->pcmcia_disk_size[1]);

   if (vm->platform->cli_show_options != NULL)
      vm->platform->cli_show_options(vm);

   printf("\n"
#if DEBUG_SYM_TREE
          "  -S <sym_file>      : Load a symbol file\n"
#endif
          "  -a <cfg_file>      : Virtual ATM switch configuration file\n"
          "  -f <cfg_file>      : Virtual Frame-Relay switch configuration "
          "file\n"
          "  -E <cfg_file>      : Virtual Ethernet switch configuration file\n"
          "  -b <cfg_file>      : Virtual bridge configuration file\n"
          "  -e                 : Show network device list of the "
          "host machine\n"
          "\n");

   printf("<si_desc> format:\n"
          "   \"device{:baudrate{:databits{:parity{:stopbits{:hwflow}}}}}}\"\n"
          "\n");

   switch(vm->slots_type) {
      case CISCO_CARD_TYPE_PA:
         printf("<pa_desc> format:\n"
                "   \"slot:sub_slot:pa_driver\"\n"
                "\n");

         printf("<pa_nio> format:\n"
                "   \"slot:port:netio_type{:netio_parameters}\"\n"
                "\n");
         break;

      case CISCO_CARD_TYPE_NM:
         printf("<nm_desc> format:\n"
                "   \"slot:sub_slot:nm_driver\"\n"
                "\n");

         printf("<nm_nio> format:\n"
                "   \"slot:port:netio_type{:netio_parameters}\"\n"
                "\n");
         break;

      case CISCO_CARD_TYPE_WIC:
         printf("<wic_desc> format:\n"
                "   \"slot:wic_driver\"\n"
                "\n");

         printf("<wic_nio> format:\n"
                "   \"slot:port:netio_type{:netio_parameters}\"\n"
                "\n");
         break;
   }

   if (vm->platform->show_spec_drivers != NULL)
      vm->platform->show_spec_drivers();

   /* Show possible slot drivers */
   vm_slot_show_drivers(vm);
   
   /* Show the possible NETIO types */
   netio_show_types();
}

/* Find an option in the command line */
static char *cli_find_option(int argc,char *argv[],char *opt)
{
   int i;

   for(i=1;i<argc;i++) {
      if (!strncmp(argv[i],opt,2)) {
         if (argv[i][2] != 0)
            return(&argv[i][2]);
         else {
            if (argv[i+1] != NULL)
               return(argv[i+1]);
            else {
               fprintf(stderr,"Error: option '%s': no argument specified.\n",
                       opt);
               exit(EXIT_FAILURE);
            }
         }
      }
   }

   return NULL;
}

/* Load plugins */
static void cli_load_plugins(int argc,char *argv[])
{
   char *str;
   int i;

   for(i=1;i<argc;i++) {
      if (!strncmp(argv[i],"-L",2)) {
         if (argv[i][2] != 0)
            str = &argv[i][2];
         else {
            if (argv[i+1] != NULL)
               str = argv[i+1];
            else {
               fprintf(stderr,"Plugin error: no argument specified.\n");
               exit(EXIT_FAILURE);
            }
         }

         if (!plugin_load(str))
            fprintf(stderr,"Unable to load plugin '%s'!\n",str);
      }
   }
}

/* Determine the platform (Cisco 3600, 7200). Default is Cisco 7200 */
static vm_platform_t *cli_get_platform_type(int argc,char *argv[])
{
   vm_platform_t *platform;
   char *str;

   if (!(str = cli_find_option(argc,argv,"-P")))
      str = default_platform;

   if (!(platform = vm_platform_find_cli_name(str)))
         fprintf(stderr,"Invalid platform type '%s'\n",str);

   return platform;
}

static struct option cmd_line_lopts[] = {
   { "disk0"      , 1, NULL, OPT_DISK0_SIZE },
   { "disk1"      , 1, NULL, OPT_DISK1_SIZE },
   { "idle-pc"    , 1, NULL, OPT_IDLE_PC },
   { "timer-itv"  , 1, NULL, OPT_TIMER_ITV },
   { "vm-debug"   , 1, NULL, OPT_VM_DEBUG },
   { "iomem-size" , 1, NULL, OPT_IOMEM_SIZE },
   { "sparse-mem" , 0, NULL, OPT_SPARSE_MEM },
   { "noctrl"     , 0, NULL, OPT_NOCTRL },
   { "notelnetmsg", 0, NULL, OPT_NOTELMSG },
   { "filepid"    , 1, NULL, OPT_FILEPID },
   { "startup-config", 1, NULL, OPT_STARTUP_CONFIG_FILE },
   { "private-config", 1, NULL, OPT_PRIVATE_CONFIG_FILE },
   { NULL         , 0, NULL, 0 },
};

/* Create a router instance */
static vm_instance_t *cli_create_instance(char *name,char *platform_name,
                                          int instance_id)
{
   vm_instance_t *vm;

   vm = vm_create_instance(name,instance_id,platform_name);
  
   if (vm == NULL) {
      fprintf(stderr,"%s: unable to create instance %s!\n",platform_name,name);
      return NULL;
   }

   return vm;
}

/* Parse the command line */
static int parse_std_cmd_line(int argc,char *argv[])
{
   char *options_list = 
      "r:o:n:c:m:l:C:i:jt:p:s:k:T:U:A:B:a:f:E:b:S:R:M:eXP:N:G:g:L:I:";
   vm_platform_t *platform;
   vm_instance_t *vm = NULL;
   int instance_id;
   int option;
   char *str;
   FILE *pid_file = NULL; // For saving the pid if requested

   /* Get the instance ID */
   instance_id = 0;

   /* Use the old VM file naming type */
   vm_file_naming_type = 1;

   cli_load_plugins(argc,argv);

   if ((str = cli_find_option(argc,argv,"-i"))) {
      instance_id = atoi(str);
      printf("Instance ID set to %d.\n",instance_id);
   }

   if ((str = cli_find_option(argc,argv,"-N")))
      vm_file_naming_type = atoi(str);

   /* Get the platform type */
   if (!(platform = cli_get_platform_type(argc,argv)))
      goto exit_failure;

   /* Create the default instance */
   if (!(vm = cli_create_instance("default",platform->name,instance_id)))
      goto exit_failure;

   opterr = 0;
   
   vtty_set_ctrlhandler(1); /* By default allow ctrl ] */
   vtty_set_telnetmsg(1);   /* By default allow telnet message */

   while((option = getopt_long(argc,argv,options_list,
                               cmd_line_lopts,NULL)) != -1) 
   {
      switch(option)
      {
         /* Instance ID (already managed) */
         case 'i':
            break;

         /* Platform (already managed) */
         case 'P':
            break;

         /* RAM size */
         case 'r':
            vm->ram_size = strtol(optarg, NULL, 10);
            printf("Virtual RAM size set to %d MB.\n",vm->ram_size);
            break;

         /* ROM size */
         case 'o':
            vm->rom_size = strtol(optarg, NULL, 10);
            printf("Virtual ROM size set to %d MB.\n",vm->rom_size);
            break;

         /* NVRAM size */
         case 'n':
            vm->nvram_size = strtol(optarg, NULL, 10);
            printf("NVRAM size set to %d KB.\n",vm->nvram_size);
            break;

         /* PCMCIA disk0 size */
         case OPT_DISK0_SIZE:
            vm->pcmcia_disk_size[0] = atoi(optarg);
            printf("PCMCIA ATA disk0 size set to %u MB.\n",
                   vm->pcmcia_disk_size[0]);
            break;

         /* PCMCIA disk1 size */
         case OPT_DISK1_SIZE:
            vm->pcmcia_disk_size[1] = atoi(optarg);
            printf("PCMCIA ATA disk1 size set to %u MB.\n",
                   vm->pcmcia_disk_size[1]);
            break;

         case OPT_NOCTRL:
            vtty_set_ctrlhandler(0); /* Ignore ctrl ] */
            printf("Block ctrl+] access to monitor console.\n");
            break;

         /* Config Register */
         case 'c':
            vm->conf_reg_setup = strtol(optarg, NULL, 0);
            printf("Config. Register set to 0x%x.\n",vm->conf_reg_setup);
            break;

         /* IOS startup configuration file */
         case 'C':
         case OPT_STARTUP_CONFIG_FILE:
            vm_ios_set_config(vm,optarg,vm->ios_private_config);
            break;

         /* IOS private configuration file */
         case OPT_PRIVATE_CONFIG_FILE:
            vm_ios_set_config(vm,vm->ios_startup_config,optarg);
            break;

         /* Use physical memory to emulate RAM (no-mapped file) */
         case 'X':
            vm->ram_mmap = 0;
            break;

         /* Use a ghost file to simulate RAM */           
         case 'G':
            free(vm->ghost_ram_filename);
            vm->ghost_ram_filename = strdup(optarg);
            vm->ghost_status = VM_GHOST_RAM_USE;
            break;

         /* Generate a ghost RAM image */
         case 'g':
            free(vm->ghost_ram_filename);
            vm->ghost_ram_filename = strdup(optarg);
            vm->ghost_status = VM_GHOST_RAM_GENERATE;
            break;

         /* Use sparse memory */
         case OPT_SPARSE_MEM:
            vm->sparse_mem = TRUE;
            break;

         /* Alternate ROM */
         case 'R':
            free(vm->rom_filename);
            vm->rom_filename = strdup(optarg);
            break;

         case OPT_NOTELMSG:
            vtty_set_telnetmsg(0); /* disable telnet greeting */
            printf("Prevent telnet message on AUX/CONSOLE connecte.\n");
            break;

         case OPT_FILEPID:
            if ((pid_file = fopen(optarg,"w"))) {
              fprintf(pid_file,"%d",getpid());
              fclose(pid_file);
            } else {
              printf("Unable to save to %s.\n",optarg);
            }
            break;
			
         /* Idle PC */
         case OPT_IDLE_PC:
            vm->idle_pc = strtoull(optarg,NULL,0);
            printf("Idle PC set to 0x%llx.\n",vm->idle_pc);
            break;

         /* Timer IRQ check interval */
         case OPT_TIMER_ITV:
            vm->timer_irq_check_itv = atoi(optarg);
            break;

         /* Clock divisor */
         case 'k':
            vm->clock_divisor = atoi(optarg);

            if (!vm->clock_divisor) {
               fprintf(stderr,"Invalid Clock Divisor specified!\n");
               goto exit_failure;
            }

            printf("Using a clock divisor of %d.\n",vm->clock_divisor);
            break;

         /* Disable JIT */
         case 'j':
            vm->jit_use = FALSE;
            break;

         /* VM debug level */
         case OPT_VM_DEBUG:
            vm->debug_level = atoi(optarg);
            break;

         /* Log file */
         case 'l':
            if (!(log_file_name = realloc(log_file_name, strlen(optarg)+1))) {
               fprintf(stderr,"Unable to set log file name.\n");
               goto exit_failure;
            }
            strcpy(log_file_name, optarg);
            printf("Log file: writing to %s\n",log_file_name);
            break;

#if DEBUG_SYM_TREE
         /* Symbol file */
         case 'S':
            vm->sym_filename = strdup(optarg);
            break;
#endif

         /* TCP server for Console Port */
         case 'T':
            vm->vtty_con_type = VTTY_TYPE_TCP;
            vm->vtty_con_tcp_port = atoi(optarg);
            break;

         /* Serial interface for Console port */
         case 'U':
            vm->vtty_con_type = VTTY_TYPE_SERIAL;
            if (vtty_parse_serial_option(&vm->vtty_con_serial_option,optarg)) {
               fprintf(stderr,
                       "Invalid Console serial interface descriptor!\n");
               goto exit_failure;
            }
            break;

         /* TCP server for AUX Port */
         case 'A':
            vm->vtty_aux_type = VTTY_TYPE_TCP;
            vm->vtty_aux_tcp_port = atoi(optarg);
            break;

         /* Serial interface for AUX port */
         case 'B':
            vm->vtty_aux_type = VTTY_TYPE_SERIAL;
            if (vtty_parse_serial_option(&vm->vtty_aux_serial_option,optarg)) {
               fprintf(stderr,"Invalid AUX serial interface descriptor!\n");
               goto exit_failure;
            }
            break;

         /* Port settings */
         case 'p':
            vm_slot_cmd_create(vm,optarg);
            break;

         /* NIO settings */
         case 's':
            vm_slot_cmd_add_nio(vm,optarg);
            break;

         /* Virtual ATM switch */
         case 'a':
            if (atmsw_start(optarg) == -1)
               goto exit_failure;
            break;

         /* Virtual ATM bridge */
         case 'M':
            if (atm_bridge_start(optarg) == -1)
               goto exit_failure;
            break;

         /* Virtual Frame-Relay switch */
         case 'f':
            if (frsw_start(optarg) == -1)
               goto exit_failure;
            break;

         /* Virtual Ethernet switch */
         case 'E':
            if (ethsw_start(optarg) == -1)
               goto exit_failure;
            break;

         /* Virtual bridge */
         case 'b':
            if (netio_bridge_start(optarg) == -1)
               goto exit_failure;
            break;

#ifdef GEN_ETH
         /* Ethernet device list */
         case 'e':
            gen_eth_show_dev_list();
            goto exit_success;
#endif            

         /* Load plugin (already handled) */
         case 'L':
            break;

         /* Oops ! */
         case '?':
            show_usage(vm,argc,argv);
            goto exit_failure;

         /* Parse options specific to the platform */
         default:
            if (vm->platform->cli_parse_options != NULL)
               /* If you get an option wrong, say which option is was */
               /* Wont be pretty for a long option, but it will at least help */
               if (vm->platform->cli_parse_options(vm,option) == -1) {
                 printf("Flag not recognised: -%c\n",(char)option);
                 goto exit_failure;
               }
      }
   }

   /* Last argument, this is the IOS filename */
   if (optind == (argc - 1)) {
      /* setting IOS image file	*/
      vm_ios_set_image(vm,argv[optind]);
      printf("IOS image file: %s\n\n",vm->ios_image);
   } else { 
      /* IOS missing */
      fprintf(stderr,"Please specify an IOS image filename\n");
      show_usage(vm,argc,argv);
      goto exit_failure;
   }

   vm_release(vm);
   return(0);

exit_success:
   if (vm) {
      vm_release(vm);
      vm_delete_instance("default");
   }
   exit(EXIT_SUCCESS);
   return(-1);
exit_failure:
   if (vm) {
      vm_release(vm);
      vm_delete_instance("default");
   }
   exit(EXIT_FAILURE);
   return(-1);
}

/* 
 * Run in hypervisor mode with a config file if the "-H" option 
 * is present in command line.
 */
static int run_hypervisor(int argc,char *argv[])
{
   char *options_list = "H:l:hN:L:";
   int i,option;
   char *index;
   size_t len;  
   FILE *pid_file = NULL; // For saving the pid if requested

   vtty_set_ctrlhandler(1); /* By default allow ctrl ] */
   vtty_set_telnetmsg(1);   /* By default allow telnet message */

   for(i=1;i<argc;i++)
      if (!strcmp(argv[i],"-H")) {
         hypervisor_mode = 1;
         break;
      }

   /* standard mode with one instance */
   if (!hypervisor_mode)
      return(FALSE);

   cli_load_plugins(argc,argv);

   opterr = 0;

   /* New long options are sometimes appropriate for hypervisor mode */
   while((option = getopt_long(argc,argv,options_list,
                               cmd_line_lopts,NULL)) != -1) {
      switch(option)
      {
         /* Hypervisor TCP port */
         case 'H':
            index = strrchr(optarg,':');

            if (!index) {
               hypervisor_tcp_port = atoi(optarg);
            } else {
               len = index - optarg;
               hypervisor_ip_address = realloc(hypervisor_ip_address, len + 1);

               if (!hypervisor_ip_address) {
                  fprintf(stderr,"Unable to set hypervisor IP address!\n");
                  exit(EXIT_FAILURE);
               }

               memcpy(hypervisor_ip_address,optarg,len);
               hypervisor_ip_address[len] = '\0';
               hypervisor_tcp_port = atoi(index + 1);
            }
            break;

         /* Log file */
         case 'l':
            if (!(log_file_name = realloc(log_file_name, strlen(optarg)+1))) {
               fprintf(stderr,"Unable to set log file name!\n");
               exit(EXIT_FAILURE);
            }
            strcpy(log_file_name, optarg);
            printf("Log file: writing to %s\n",log_file_name);
            break;

         /* VM file naming type */
         case 'N':
            vm_file_naming_type = atoi(optarg);
            break;

         /* Load plugin (already handled) */
         case 'L':
            break;

         case OPT_NOCTRL:
            vtty_set_ctrlhandler(0); /* Ignore ctrl ] */
            printf("Block ctrl+] access to monitor console.\n");
            break;

         case OPT_NOTELMSG:
            vtty_set_telnetmsg(0); /* disable telnet greeting */
            printf("Prevent telnet message on AUX/CONSOLE connecte.\n");
            break;

         case OPT_FILEPID:
            if ((pid_file = fopen(optarg,"w"))) {
              fprintf(pid_file,"%d",getpid());
              fclose(pid_file);
            } else {
              printf("Unable to save to %s.\n",optarg);
            }
            break;

         /* Oops ! */
         case '?':
            //show_usage(argc,argv,VM_TYPE_C7200);
            exit(EXIT_FAILURE);
      }
   }

   return(TRUE);
}

/* Delete all objects */
void dynamips_reset(void)
{
   printf("Shutdown in progress...\n");

   /* Delete all virtual router instances */
   vm_delete_all_instances();

   /* Delete ATM and Frame-Relay switches + bridges */
   netio_bridge_delete_all();
   atmsw_delete_all();
   atm_bridge_delete_all();
   frsw_delete_all();
   ethsw_delete_all();

   /* Delete all NIO descriptors */
   netio_delete_all();

   m_log("GENERAL","reset done.\n");

   printf("Shutdown completed.\n");
}

/* Default platforms */
static int (*platform_register[])(void) = {
   c7200_platform_register,
   c3600_platform_register,
   c3725_platform_register,
   c3745_platform_register,
   c2691_platform_register,
   c2600_platform_register,
   c1700_platform_register,
   c6sup1_platform_register,
   c6msfc1_platform_register,
#ifdef USE_UNSTABLE
   ppc32_vmtest_platform_register,
#endif
   NULL,
};

/* Register default platforms */
static void register_default_platforms(void)
{
   int i;

   for(i=0;platform_register[i];i++)
      platform_register[i]();
}

/* Destroy variables generated from the standard command line */
static void destroy_cmd_line_vars(void)
{
   if (log_file_name) {
      free(log_file_name);
      log_file_name = NULL;
   }
   if (hypervisor_ip_address) {
      free(hypervisor_ip_address);
      hypervisor_ip_address = NULL;
   }
}

int main(int argc,char *argv[])
{
   vm_instance_t *vm;

#ifdef PROFILE
   atexit(profiler_savestat);
#endif

#ifdef USE_UNSTABLE
   printf("Cisco Router Simulation Platform (version %s/%s unstable)\n",
          sw_version,os_name);
#else
   printf("Cisco Router Simulation Platform (version %s/%s stable)\n",
          sw_version,os_name);
#endif

   printf("Copyright (c) 2005-2011 Christophe Fillot.\n");
   printf("Build date: %s %s\n\n",__DATE__,__TIME__);

   gen_uuid_init();

   /* Register platforms */
   register_default_platforms();

   /* Initialize timers */
   timer_init();

   /* Initialize object registry */
   registry_init();
   
   /* Initialize ATM module (for HEC checksums) */
   atm_init();

   /* Initialize CRC functions */
   crc_init();

   /* Initialize NetIO code */
   netio_rxl_init();

   /* Initialize NetIO packet filters */
   netio_filter_load_all();

   /* Initialize VTTY code */
   vtty_init();
   
   /* Parse standard command line */
   atexit(destroy_cmd_line_vars);
   if (!run_hypervisor(argc,argv))
      parse_std_cmd_line(argc,argv);

   /* Create general log file */
   create_log_file();

   /* Periodic tasks initialization */
   if (ptask_init(0) == -1)
      exit(EXIT_FAILURE);

   /* Create instruction lookup tables */
   mips64_jit_create_ilt();
   mips64_exec_create_ilt();
   ppc32_jit_create_ilt();
   ppc32_exec_create_ilt();
   
   setup_signals();

   if (!hypervisor_mode) {
      /* Initialize the default instance */
      vm = vm_acquire("default");
      assert(vm != NULL);

      if (vm_init_instance(vm) == -1) {
         fprintf(stderr,"Unable to initialize router instance.\n");
         exit(EXIT_FAILURE);
      }

#if (DEBUG_INSN_PERF_CNT > 0) || (DEBUG_BLOCK_PERF_CNT > 0)
      {
         m_uint32_t counter,prev = 0,delta;
         while(vm->status == VM_STATUS_RUNNING) {
            counter = cpu_get_perf_counter(vm->boot_cpu);
            delta = counter - prev;
            prev = counter;
            printf("delta = %u\n",delta);
            sleep(1);
         }
      }
#else
      /* Start instance monitoring */
      vm_monitor(vm);
#endif

      /* Free resources used by instance */
      vm_release(vm);
   } else {
      hypervisor_tcp_server(hypervisor_ip_address,hypervisor_tcp_port);
   }

   dynamips_reset();
   close_log_file();
   return(0);
}
