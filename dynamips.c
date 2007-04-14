/*
 * Cisco router simulation platform.
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
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>
#include <getopt.h>

#include "dynamips.h"
#include "cpu.h"
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
#include "dev_msfc1.h"
#include "ppc32_vmtest.h"
#include "dev_vtty.h"
#include "ptask.h"
#include "timer.h"
#include "registry.h"
#include "hypervisor.h"
#include "net_io.h"
#include "net_io_bridge.h"
#include "net_io_filter.h"
#include "crc.h"
#include "atm.h"
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

/* Software version */
const char *sw_version = DYNAMIPS_VERSION"-"JIT_ARCH;

/* Software version tag */
const char *sw_version_tag = "2007030219";

/* Hypervisor */
int hypervisor_mode = 0;
int hypervisor_tcp_port = 0;

/* Log file */
char *log_file_name = NULL;
FILE *log_file = NULL;

/* VM flags */
volatile int vm_save_state = 0;

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
static void show_usage(int argc,char *argv[],int platform)
{
   u_int def_ram_size,def_rom_size,def_nvram_size;
   u_int def_conf_reg,def_clock_div;
   u_int def_disk0_size = 0,def_disk1_size = 0;
   u_int def_nm_iomem_size = 0;

   switch(platform) {
      case VM_TYPE_C7200:
         def_ram_size   = C7200_DEFAULT_RAM_SIZE;
         def_rom_size   = C7200_DEFAULT_ROM_SIZE;
         def_nvram_size = C7200_DEFAULT_NVRAM_SIZE;
         def_conf_reg   = C7200_DEFAULT_CONF_REG;
         def_clock_div  = C7200_DEFAULT_CLOCK_DIV;
         def_disk0_size = C7200_DEFAULT_DISK0_SIZE;
         def_disk1_size = C7200_DEFAULT_DISK1_SIZE;
         break;
      case VM_TYPE_C3600:
         def_ram_size   = C3600_DEFAULT_RAM_SIZE;
         def_rom_size   = C3600_DEFAULT_ROM_SIZE;
         def_nvram_size = C3600_DEFAULT_NVRAM_SIZE;
         def_conf_reg   = C3600_DEFAULT_CONF_REG;
         def_clock_div  = C3600_DEFAULT_CLOCK_DIV;
         def_disk0_size = C3600_DEFAULT_DISK0_SIZE;
         def_disk1_size = C3600_DEFAULT_DISK1_SIZE;
         def_nm_iomem_size = C3600_DEFAULT_IOMEM_SIZE;
         break;
      case VM_TYPE_C2691:
         def_ram_size   = C2691_DEFAULT_RAM_SIZE;
         def_rom_size   = C2691_DEFAULT_ROM_SIZE;
         def_nvram_size = C2691_DEFAULT_NVRAM_SIZE;
         def_conf_reg   = C2691_DEFAULT_CONF_REG;
         def_clock_div  = C2691_DEFAULT_CLOCK_DIV;
         def_disk0_size = C2691_DEFAULT_DISK0_SIZE;
         def_disk1_size = C2691_DEFAULT_DISK1_SIZE;
         def_nm_iomem_size = C2691_DEFAULT_IOMEM_SIZE;
         break;
      case VM_TYPE_C3725:
         def_ram_size   = C3725_DEFAULT_RAM_SIZE;
         def_rom_size   = C3725_DEFAULT_ROM_SIZE;
         def_nvram_size = C3725_DEFAULT_NVRAM_SIZE;
         def_conf_reg   = C3725_DEFAULT_CONF_REG;
         def_clock_div  = C3725_DEFAULT_CLOCK_DIV;
         def_disk0_size = C3725_DEFAULT_DISK0_SIZE;
         def_disk1_size = C3725_DEFAULT_DISK1_SIZE;
         def_nm_iomem_size = C3725_DEFAULT_IOMEM_SIZE;
         break;
      case VM_TYPE_C3745:
         def_ram_size   = C3745_DEFAULT_RAM_SIZE;
         def_rom_size   = C3745_DEFAULT_ROM_SIZE;
         def_nvram_size = C3745_DEFAULT_NVRAM_SIZE;
         def_conf_reg   = C3745_DEFAULT_CONF_REG;
         def_clock_div  = C3745_DEFAULT_CLOCK_DIV;
         def_disk0_size = C3745_DEFAULT_DISK0_SIZE;
         def_disk1_size = C3745_DEFAULT_DISK1_SIZE;
         def_nm_iomem_size = C3745_DEFAULT_IOMEM_SIZE;
         break;
      case VM_TYPE_C2600:
         def_ram_size   = C2600_DEFAULT_RAM_SIZE;
         def_rom_size   = C2600_DEFAULT_ROM_SIZE;
         def_nvram_size = C2600_DEFAULT_NVRAM_SIZE;
         def_conf_reg   = C2600_DEFAULT_CONF_REG;
         def_clock_div  = C2600_DEFAULT_CLOCK_DIV;
         def_disk0_size = C2600_DEFAULT_DISK0_SIZE;
         def_disk1_size = C2600_DEFAULT_DISK1_SIZE;
         def_nm_iomem_size = C2600_DEFAULT_IOMEM_SIZE;
         break;
      case VM_TYPE_MSFC1:
         def_ram_size   = MSFC1_DEFAULT_RAM_SIZE;
         def_rom_size   = MSFC1_DEFAULT_ROM_SIZE;
         def_nvram_size = MSFC1_DEFAULT_NVRAM_SIZE;
         def_conf_reg   = MSFC1_DEFAULT_CONF_REG;
         def_clock_div  = MSFC1_DEFAULT_CLOCK_DIV;
         break;
      case VM_TYPE_PPC32_TEST:
         def_ram_size   = PPC32_VMTEST_DEFAULT_RAM_SIZE;
      default:
         fprintf(stderr,"show_usage: invalid platform.\n");
         return;
   }

   printf("Usage: %s [options] <ios_image>\n\n",argv[0]);
   
   printf("Available options:\n"
          "  -H <tcp_port>      : Run in hypervisor mode\n\n"
          "  -P <platform>      : Platform to emulate (7200, 3600, "
          "2691, 3725 or 3745) "
          "(default: 7200)\n\n"
          "  -l <log_file>      : Set logging file (default is %s)\n"
          "  -j                 : Disable the JIT compiler, very slow\n"
          "  --exec-area <size> : Set the exec area size (default: %d Mb)\n"
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
          "  -C <cfg_file>      : Import an IOS configuration file "
          "into NVRAM\n"
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
          "\n",
          LOGFILE_DEFAULT_NAME,MIPS_EXEC_AREA_SIZE,VM_TIMER_IRQ_CHECK_ITV,
          def_ram_size,def_rom_size,def_nvram_size,def_conf_reg,
          def_clock_div,def_disk0_size,def_disk1_size);

   switch(platform) {
      case VM_TYPE_C7200:
         printf("  -t <npe_type>      : Select NPE type (default: \"%s\")\n"
                "  -M <midplane>      : Select Midplane (\"std\" or \"vxr\")\n"
                "  -p <pa_desc>       : Define a Port Adapter\n"
                "  -s <pa_nio>        : Bind a Network IO interface to a "
                "Port Adapter\n",
                C7200_DEFAULT_NPE_TYPE);
         break;

      case VM_TYPE_C3600:
         printf("  -t <chassis_type>  : Select Chassis type "
                "(default: \"%s\")\n"
                "  --iomem-size <val> : IO memory (in percents, default: %u)\n"
                "  -p <nm_desc>       : Define a Network Module\n"
                "  -s <nm_nio>        : Bind a Network IO interface to a "
                "Network Module\n",
                C3600_DEFAULT_CHASSIS,def_nm_iomem_size);
         break;

      case VM_TYPE_C2691:
         printf("  --iomem-size <val> : IO memory (in percents, default: %u)\n"
                "  -p <nm_desc>       : Define a Network Module\n"
                "  -s <nm_nio>        : Bind a Network IO interface to a "
                "Network Module\n",
                def_nm_iomem_size);
         break;

      case VM_TYPE_C3725:
         printf("  --iomem-size <val> : IO memory (in percents, default: %u)\n"
                "  -p <nm_desc>       : Define a Network Module\n"
                "  -s <nm_nio>        : Bind a Network IO interface to a "
                "Network Module\n",
                def_nm_iomem_size);
         break;

      case VM_TYPE_C3745:
         printf("  --iomem-size <val> : IO memory (in percents, default: %u)\n"
                "  -p <nm_desc>       : Define a Network Module\n"
                "  -s <nm_nio>        : Bind a Network IO interface to a "
                "Network Module\n",
                def_nm_iomem_size);
         break;

      case VM_TYPE_C2600:
         printf("  --iomem-size <val> : IO memory (in percents, default: %u)\n"
                "  -p <nm_desc>       : Define a Network Module\n"
                "  -s <nm_nio>        : Bind a Network IO interface to a "
                "Network Module\n",
                def_nm_iomem_size);
         break;

      case VM_TYPE_MSFC1:
         printf("  -s <pa_nio>        : Bind a Network IO interface to a "
                "Port Adapter\n");
         break;

   }

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

   switch(platform) {
      case VM_TYPE_C7200:
         printf("<pa_desc> format:\n"
                "   \"slot:pa_driver\"\n"
                "\n");

         printf("<pa_nio> format:\n"
                "   \"slot:port:netio_type{:netio_parameters}\"\n"
                "\n");

         /* Show the possible NPE drivers */
         c7200_npe_show_drivers();

         /* Show the possible PA drivers */
         c7200_pa_show_drivers();
         break;

      case VM_TYPE_C3600:
         printf("<nm_desc> format:\n"
                "   \"slot:nm_driver\"\n"
                "\n");

         printf("<nm_nio> format:\n"
                "   \"slot:port:netio_type{:netio_parameters}\"\n"
                "\n");

         /* Show the possible chassis types for C3600 platform */
         c3600_chassis_show_drivers();

         /* Show the possible NM drivers */
         c3600_nm_show_drivers();
         break;

      case VM_TYPE_C2691:
         printf("<nm_desc> format:\n"
                "   \"slot:nm_driver\"\n"
                "\n");

         printf("<nm_nio> format:\n"
                "   \"slot:port:netio_type{:netio_parameters}\"\n"
                "\n");

         /* Show the possible NM drivers */
         c2691_nm_show_drivers();
         break;

      case VM_TYPE_C3725:
         printf("<nm_desc> format:\n"
                "   \"slot:nm_driver\"\n"
                "\n");

         printf("<nm_nio> format:\n"
                "   \"slot:port:netio_type{:netio_parameters}\"\n"
                "\n");

         /* Show the possible NM drivers */
         c3725_nm_show_drivers();
         break;

      case VM_TYPE_C3745:
         printf("<nm_desc> format:\n"
                "   \"slot:nm_driver\"\n"
                "\n");

         printf("<nm_nio> format:\n"
                "   \"slot:port:netio_type{:netio_parameters}\"\n"
                "\n");

         /* Show the possible NM drivers */
         c3745_nm_show_drivers();
         break;

      case VM_TYPE_C2600:
         printf("<nm_desc> format:\n"
                "   \"slot:nm_driver\"\n"
                "\n");

         printf("<nm_nio> format:\n"
                "   \"slot:port:netio_type{:netio_parameters}\"\n"
                "\n");

         /* Show the possible chassis types for C2600 platform */
         c2600_mainboard_show_drivers();

         /* Show the possible NM drivers */
         c2600_nm_show_drivers();
         break;
   }
   
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

/* Determine the platform (Cisco 3600, 7200). Default is Cisco 7200 */
static int cli_get_platform_type(int argc,char *argv[])
{
   int vm_type = VM_TYPE_C7200;
   char *str;

   if ((str = cli_find_option(argc,argv,"-P"))) {
      if (!strcmp(str,"3600"))
         vm_type = VM_TYPE_C3600;
      else if (!strcmp(str,"7200"))
         vm_type = VM_TYPE_C7200;
      else if (!strcmp(str,"2691"))
         vm_type = VM_TYPE_C2691;
      else if (!strcmp(str,"3725"))
         vm_type = VM_TYPE_C3725;
      else if (!strcmp(str,"3745"))
         vm_type = VM_TYPE_C3745;
      else if (!strcmp(str,"2600"))
         vm_type = VM_TYPE_C2600;
      else if (!strcmp(str,"MSFC1"))
         vm_type = VM_TYPE_MSFC1;
      else if (!strcmp(str,"PPC32_TEST"))
         vm_type = VM_TYPE_PPC32_TEST;
      else
         fprintf(stderr,"Invalid platform type '%s'\n",str);
   }

   return(vm_type);
}

/* Command Line long options */
#define OPT_DISK0_SIZE  0x100
#define OPT_DISK1_SIZE  0x101
#define OPT_EXEC_AREA   0x102
#define OPT_IDLE_PC     0x103
#define OPT_TIMER_ITV   0x104
#define OPT_VM_DEBUG    0x105
#define OPT_IOMEM_SIZE  0x106
#define OPT_SPARSE_MEM  0x107

static struct option cmd_line_lopts[] = {
   { "disk0"      , 1, NULL, OPT_DISK0_SIZE },
   { "disk1"      , 1, NULL, OPT_DISK1_SIZE },
   { "exec-area"  , 1, NULL, OPT_EXEC_AREA },
   { "idle-pc"    , 1, NULL, OPT_IDLE_PC },
   { "timer-itv"  , 1, NULL, OPT_TIMER_ITV },
   { "vm-debug"   , 1, NULL, OPT_VM_DEBUG },
   { "iomem-size" , 1, NULL, OPT_IOMEM_SIZE },
   { "sparse-mem" , 0, NULL, OPT_SPARSE_MEM },
   { NULL         , 0, NULL, 0 },
};

/* Parse specific options for the Cisco 7200 platform */
static int cli_parse_c7200_options(vm_instance_t *vm,int option)
{
   c7200_t *router;

   router = VM_C7200(vm);

   switch(option) {
      /* NPE type */
      case 't':
         c7200_npe_set_type(router,optarg);
         break;

      /* Midplane type */
      case 'M':
         c7200_midplane_set_type(router,optarg);
         break;

      /* Set the base MAC address */
      case 'm':
         if (!c7200_midplane_set_mac_addr(router,optarg))
            printf("MAC address set to '%s'.\n",optarg);
         break;

      /* PA settings */
      case 'p':
         return(c7200_cmd_pa_create(router,optarg));

      /* PA NIO settings */
      case 's':
         return(c7200_cmd_add_nio(router,optarg));

      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Parse specific options for the Cisco 3600 platform */
static int cli_parse_c3600_options(vm_instance_t *vm,int option)
{
   c3600_t *router;

   router = VM_C3600(vm);

   switch(option) {
      /* chassis type */
      case 't':
         c3600_chassis_set_type(router,optarg);
         break;

      /* IO memory reserved for NMs (in percents!) */
      case OPT_IOMEM_SIZE:
         router->nm_iomem_size = 0x8000 | atoi(optarg);
         break;

      /* NM settings */
      case 'p':
         return(c3600_cmd_nm_create(router,optarg));

      /* NM NIO settings */
      case 's':
         return(c3600_cmd_add_nio(router,optarg));

      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Parse specific options for the Cisco 2691 platform */
static int cli_parse_c2691_options(vm_instance_t *vm,int option)
{
   c2691_t *router;

   router = VM_C2691(vm);

   switch(option) {
      /* IO memory reserved for NMs (in percents!) */
      case OPT_IOMEM_SIZE:
         router->nm_iomem_size = 0x8000 | atoi(optarg);
         break;

      /* NM settings */
      case 'p':
         return(c2691_cmd_nm_create(router,optarg));

      /* NM NIO settings */
      case 's':
         return(c2691_cmd_add_nio(router,optarg));

      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Parse specific options for the Cisco 3725 platform */
static int cli_parse_c3725_options(vm_instance_t *vm,int option)
{
   c3725_t *router;

   router = VM_C3725(vm);

   switch(option) {
      /* IO memory reserved for NMs (in percents!) */
      case OPT_IOMEM_SIZE:
         router->nm_iomem_size = 0x8000 | atoi(optarg);
         break;

      /* NM settings */
      case 'p':
         return(c3725_cmd_nm_create(router,optarg));

      /* NM NIO settings */
      case 's':
         return(c3725_cmd_add_nio(router,optarg));

      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Parse specific options for the Cisco 3745 platform */
static int cli_parse_c3745_options(vm_instance_t *vm,int option)
{
   c3745_t *router;

   router = VM_C3745(vm);

   switch(option) {
      /* IO memory reserved for NMs (in percents!) */
      case OPT_IOMEM_SIZE:
         router->nm_iomem_size = 0x8000 | atoi(optarg);
         break;

      /* NM settings */
      case 'p':
         return(c3745_cmd_nm_create(router,optarg));

      /* NM NIO settings */
      case 's':
         return(c3745_cmd_add_nio(router,optarg));

      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Parse specific options for the Cisco 2600 platform */
static int cli_parse_c2600_options(vm_instance_t *vm,int option)
{
   c2600_t *router;

   router = VM_C2600(vm);

   switch(option) {
      /* IO memory reserved for NMs (in percents!) */
      case OPT_IOMEM_SIZE:
         router->nm_iomem_size = 0x8000 | atoi(optarg);
         break;

      /* Mainboard type */
      case 't':
         c2600_mainboard_set_type(router,optarg);
         break;

      /* NM settings */
      case 'p':
         return(c2600_cmd_nm_create(router,optarg));

      /* NM NIO settings */
      case 's':
         return(c2600_cmd_add_nio(router,optarg));

      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Parse specific options for the MSFC1 platform */
static int cli_parse_msfc1_options(vm_instance_t *vm,int option)
{
   msfc1_t *router;

   router = VM_MSFC1(vm);

   switch(option) {
      /* PA NIO settings */
      case 's':
         return(msfc1_cmd_add_nio(router,optarg));

      /* Unknown option */
      default:
         return(-1);
   }

   return(0);
}

/* Create a router instance */
static vm_instance_t *cli_create_instance(char *name,int platform_type,
                                          int instance_id)
{
   vm_instance_t *vm;
   c7200_t *c7200;
   c3600_t *c3600;
   c2691_t *c2691;
   c3725_t *c3725;
   c3745_t *c3745;
   c2600_t *c2600;
   msfc1_t *msfc1;

   switch(platform_type) {
      case VM_TYPE_C7200:
         if (!(c7200 = c7200_create_instance(name,instance_id))) {
            fprintf(stderr,"C7200: unable to create instance!\n");
            return NULL;
         }
         return(c7200->vm);

      case VM_TYPE_C3600:
         if (!(c3600 = c3600_create_instance(name,instance_id))) {
            fprintf(stderr,"C3600: unable to create instance!\n");
            return NULL;
         }
         return(c3600->vm);

      case VM_TYPE_C2691:
         if (!(c2691 = c2691_create_instance(name,instance_id))) {
            fprintf(stderr,"C2691: unable to create instance!\n");
            return NULL;
         }
         return(c2691->vm);

      case VM_TYPE_C3725:
         if (!(c3725 = c3725_create_instance(name,instance_id))) {
            fprintf(stderr,"C3725: unable to create instance!\n");
            return NULL;
         }
         return(c3725->vm);

      case VM_TYPE_C3745:
         if (!(c3745 = c3745_create_instance(name,instance_id))) {
            fprintf(stderr,"C3745: unable to create instance!\n");
            return NULL;
         }
         return(c3745->vm);

      case VM_TYPE_C2600:
         if (!(c2600 = c2600_create_instance(name,instance_id))) {
            fprintf(stderr,"C2600: unable to create instance!\n");
            return NULL;
         }
         return(c2600->vm);

      case VM_TYPE_MSFC1:
         if (!(msfc1 = msfc1_create_instance(name,instance_id))) {
            fprintf(stderr,"MSFC1: unable to create instance!\n");
            return NULL;
         }
         return(msfc1->vm);

      case VM_TYPE_PPC32_TEST:
         if (!(vm = ppc32_vmtest_create_instance(name,instance_id))) {
            fprintf(stderr,"PPC32_TEST: unable to create instance!\n");
            return NULL;
         }
         return(vm);

      default:
         fprintf(stderr,"Unknown platform type '%d'!\n",platform_type);
         return NULL;
   }
}

/* Parse the command line */
static int parse_std_cmd_line(int argc,char *argv[],int *platform)
{
   char *options_list = 
      "r:o:n:c:m:l:C:i:jt:p:s:k:T:U:A:B:a:f:E:b:S:R:M:eXP:N:G:g:";
   vm_instance_t *vm;
   int instance_id;
   int res,option;
   char *str;

   /* Get the instance ID */
   instance_id = 0;

   /* Use the old VM file naming type */
   vm_file_naming_type = 1;

   if ((str = cli_find_option(argc,argv,"-i"))) {
      instance_id = atoi(str);
      printf("Instance ID set to %d.\n",instance_id);
   }

   if ((str = cli_find_option(argc,argv,"-N")))
      vm_file_naming_type = atoi(str);

   /* Get the platform type */
   *platform = cli_get_platform_type(argc,argv);

   /* Create the default instance */
   if (!(vm = cli_create_instance("default",*platform,instance_id)))
      exit(EXIT_FAILURE);

   opterr = 0;

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

         /* Execution area size */
         case OPT_EXEC_AREA:
            vm->exec_area_size = atoi(optarg);
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

         /* Config Register */
         case 'c':
            vm->conf_reg_setup = strtol(optarg, NULL, 0);
            printf("Config. Register set to 0x%x.\n",vm->conf_reg_setup);
            break;

         /* IOS configuration file */
         case 'C':
            vm_ios_set_config(vm,optarg);
            break;

         /* Use physical memory to emulate RAM (no-mapped file) */
         case 'X':
            vm->ram_mmap = 0;
            break;

         /* Use a ghost file to simulate RAM */           
         case 'G':
            vm->ghost_ram_filename = strdup(optarg);
            vm->ghost_status = VM_GHOST_RAM_USE;
            break;

         /* Generate a ghost RAM image */
         case 'g':
            vm->ghost_ram_filename = strdup(optarg);
            vm->ghost_status = VM_GHOST_RAM_GENERATE;
            break;

         /* Use sparse memory */
         case OPT_SPARSE_MEM:
            vm->sparse_mem = TRUE;
            break;

         /* Alternate ROM */
         case 'R':
            vm->rom_filename = optarg;
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
               exit(EXIT_FAILURE);
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
            if (!(log_file_name = strdup(optarg))) {
               fprintf(stderr,"Unable to set log file name.\n");
               exit(EXIT_FAILURE);
            }
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
               exit(EXIT_FAILURE);
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
               exit(EXIT_FAILURE);
            }
            break;

         /* Virtual ATM switch */
         case 'a':
            if (atmsw_start(optarg) == -1)
               exit(EXIT_FAILURE);
            break;

         /* Virtual Frame-Relay switch */
         case 'f':
            if (frsw_start(optarg) == -1)
               exit(EXIT_FAILURE);
            break;

         /* Virtual Ethernet switch */
         case 'E':
            if (ethsw_start(optarg) == -1)
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
            show_usage(argc,argv,*platform);
            exit(EXIT_FAILURE);

         /* Parse options specific to the platform */
         default:
            res = 0;

            switch(vm->type) {
               case VM_TYPE_C7200:
                  res = cli_parse_c7200_options(vm,option);
                  break;
               case VM_TYPE_C3600:
                  res = cli_parse_c3600_options(vm,option);
                  break;              
               case VM_TYPE_C2691:
                  res = cli_parse_c2691_options(vm,option);
                  break;
               case VM_TYPE_C3725:
                  res = cli_parse_c3725_options(vm,option);
                  break;
              case VM_TYPE_C3745:
                  res = cli_parse_c3745_options(vm,option);
                  break;
              case VM_TYPE_C2600:
                  res = cli_parse_c2600_options(vm,option);
                  break;
              case VM_TYPE_MSFC1:
                  res = cli_parse_msfc1_options(vm,option);
                  break;
            }

            if (res == -1)
               exit(EXIT_FAILURE);
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
      show_usage(argc,argv,*platform);
      exit(EXIT_FAILURE);
   }

   vm_release(vm);
   return(0);
}

/* 
 * Run in hypervisor mode with a config file if the "-H" option 
 * is present in command line.
 */
static int run_hypervisor(int argc,char *argv[])
{
   char *options_list = "H:l:hN:";
   int i,option;

   for(i=1;i<argc;i++)
      if (!strcmp(argv[i],"-H")) {
         hypervisor_mode = 1;
         break;
      }

   /* standard mode with one instance */
   if (!hypervisor_mode)
      return(FALSE);

   opterr = 0;
   while((option = getopt(argc,argv,options_list)) != -1) {
      switch(option)
      {
         /* Hypervisor TCP port */
         case 'H':
            hypervisor_tcp_port = atoi(optarg);
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

         /* VM file naming type */
         case 'N':
            vm_file_naming_type = atoi(optarg);
            break;
            
         /* Oops ! */
         case '?':
            show_usage(argc,argv,VM_TYPE_C7200);
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
   c7200_delete_all_instances();
   c3600_delete_all_instances();
   c2691_delete_all_instances();
   c3725_delete_all_instances();
   c3745_delete_all_instances();
   c2600_delete_all_instances();
   msfc1_delete_all_instances();
   ppc32_vmtest_delete_all_instances();

   /* Delete ATM and Frame-Relay switches + bridges */
   netio_bridge_delete_all();
   atmsw_delete_all();
   frsw_delete_all();
   ethsw_delete_all();

   /* Delete all NIO descriptors */
   netio_delete_all();

   printf("Shutdown completed.\n");
}

int main(int argc,char *argv[])
{
   vm_instance_t *vm;
   int platform,res;

   /* Default emulation: Cisco 7200 */
   platform = VM_TYPE_C7200;

#ifdef PROFILE
   atexit(profiler_savestat);
#endif

   printf("Cisco Router Simulation Platform (version %s)\n",sw_version);
   printf("Copyright (c) 2005-2007 Christophe Fillot.\n");
   printf("Build date: %s %s\n\n",__DATE__,__TIME__);

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
   if (!run_hypervisor(argc,argv))
      parse_std_cmd_line(argc,argv,&platform);

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

      switch(platform) {
         case VM_TYPE_C7200:
            res = c7200_init_instance(VM_C7200(vm));
            break;
         case VM_TYPE_C3600:
            res = c3600_init_instance(VM_C3600(vm));
            break;      
         case VM_TYPE_C2691:
            res = c2691_init_instance(VM_C2691(vm));
            break;
         case VM_TYPE_C3725:
            res = c3725_init_instance(VM_C3725(vm));
            break;
         case VM_TYPE_C3745:
            res = c3745_init_instance(VM_C3745(vm));
            break;
         case VM_TYPE_C2600:
            res = c2600_init_instance(VM_C2600(vm));
            break;
         case VM_TYPE_MSFC1:
            res = msfc1_init_instance(VM_MSFC1(vm));
            break;
         case VM_TYPE_PPC32_TEST:
            res = ppc32_vmtest_init_instance(vm);
            break;
         default:
            res = -1;
      }

      if (res == -1) {
         fprintf(stderr,"Unable to initialize router instance.\n");
         exit(EXIT_FAILURE);
      }

#if (DEBUG_INSN_PERF_CNT > 0) || (DEBUG_BLOCK_PERF_CNT > 0)
      {
         m_uint64_t counter,prev = 0,delta;
         while(vm->status == VM_STATUS_RUNNING) {
            counter = cpu_get_perf_counter(vm->boot_cpu);
            delta = counter - prev;
            prev = counter;
            printf("delta = %llu\n",delta);
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
      hypervisor_tcp_server(hypervisor_tcp_port);
   }

   dynamips_reset();
   close_log_file();
   return(0);
}
