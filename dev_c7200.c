/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Generic Cisco 7200 routines and definitions (EEPROM,...).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "mips64.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_c7200.h"
#include "dev_c7200_bay.h"
#include "dev_vtty.h"

/* ======================================================================== */
/* CPU EEPROM definitions                                                   */
/* ======================================================================== */

/* NPE-100 */
static unsigned short eeprom_cpu_npe100_data[16] = {
   0x0135, 0x0203, 0xffff, 0xffff, 0x4906, 0x0004, 0x0000, 0x0000,
   0x6000, 0x0000, 0x9901, 0x0600, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-150 */
static unsigned short eeprom_cpu_npe150_data[16] = {
   0x0115, 0x0203, 0xffff, 0xffff, 0x4906, 0x0004, 0x0000, 0x0000,
   0x6000, 0x0000, 0x9901, 0x0600, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-175 */
static unsigned short eeprom_cpu_npe175_data[16] = {
   0x01C2, 0x0203, 0xffff, 0xffff, 0x4906, 0x0004, 0x0000, 0x0000,
   0x6000, 0x0000, 0x9901, 0x0600, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-200 */
static unsigned short eeprom_cpu_npe200_data[16] = {
   0x0169, 0x0200, 0xffff, 0xffff, 0x4909, 0x8902, 0x0000, 0x0000,
   0x6800, 0x0000, 0x9710, 0x2200, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-225 (same as NPE-175) */
static unsigned short eeprom_cpu_npe225_data[16] = {
   0x01C2, 0x0203, 0xffff, 0xffff, 0x4906, 0x0004, 0x0000, 0x0000,
   0x6000, 0x0000, 0x9901, 0x0600, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-300 */
static unsigned short eeprom_cpu_npe300_data[16] = {
   0x01AE, 0x0402, 0xffff, 0xffff, 0x490D, 0x5108, 0x0000, 0x0000,
   0x5000, 0x0000, 0x0012, 0x1000, 0x0000, 0xFFFF, 0xFFFF, 0xFF00,
};

/* NPE-400 */
static unsigned short eeprom_cpu_npe400_data[64] = {
   0x04FF, 0x4001, 0xF841, 0x0100, 0xC046, 0x0320, 0x001F, 0xC802,
   0x8249, 0x14BC, 0x0242, 0x4230, 0xC18B, 0x3131, 0x3131, 0x3131,
   0x3131, 0x0000, 0x0004, 0x0002, 0x0285, 0x1C0F, 0xF602, 0xCB87,
   0x4E50, 0x452D, 0x3430, 0x3080, 0x0000, 0x0000, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-G1 */
static unsigned short eeprom_cpu_npeg1_data[64] = {
   0x04FF, 0x4003, 0x5B41, 0x0200, 0xC046, 0x0320, 0x0049, 0xD00B,
   0x8249, 0x1B4C, 0x0B42, 0x4130, 0xC18B, 0x3131, 0x3131, 0x3131,
   0x3131, 0x0000, 0x0004, 0x0002, 0x0985, 0x1C13, 0xDA09, 0xCB86,
   0x4E50, 0x452D, 0x4731, 0x8000, 0x0000, 0x00FF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/*
 * CPU EEPROM array.
 */
static struct c7200_eeprom c7200_cpu_eeprom[] = {
   { "npe-100", eeprom_cpu_npe100_data, sizeof(eeprom_cpu_npe100_data)/2 },
   { "npe-150", eeprom_cpu_npe150_data, sizeof(eeprom_cpu_npe150_data)/2 },
   { "npe-175", eeprom_cpu_npe175_data, sizeof(eeprom_cpu_npe175_data)/2 },
   { "npe-200", eeprom_cpu_npe200_data, sizeof(eeprom_cpu_npe200_data)/2 },
   { "npe-225", eeprom_cpu_npe225_data, sizeof(eeprom_cpu_npe225_data)/2 },
   { "npe-300", eeprom_cpu_npe300_data, sizeof(eeprom_cpu_npe300_data)/2 },
   { "npe-400", eeprom_cpu_npe400_data, sizeof(eeprom_cpu_npe400_data)/2 },
   { "npe-g1" , eeprom_cpu_npeg1_data , sizeof(eeprom_cpu_npeg1_data)/2 },
   { NULL, NULL, 0 },
};

/* ======================================================================== */
/* Midplane EEPROM definitions                                              */
/* ======================================================================== */

/* Standard Midplane EEPROM contents */
static unsigned short eeprom_midplane_data[32] = {
   0x0106, 0x0101, 0xffff, 0xffff, 0x4906, 0x0303, 0xFFFF, 0xFFFF,
   0xFFFF, 0x0400, 0x0000, 0x0000, 0x4C09, 0x10B0, 0xFFFF, 0x00FF,
   0x0000, 0x0000, 0x6335, 0x8B28, 0x631D, 0x0000, 0x608E, 0x6D1C,
   0x62BB, 0x0000, 0x6335, 0x8B28, 0x0000, 0x0000, 0x6335, 0x8B28,
};

/* VXR Midplane EEPROM contents */
static unsigned short eeprom_vxr_midplane_data[32] = {
   0x0106, 0x0201, 0xffff, 0xffff, 0x4906, 0x0303, 0xFFFF, 0xFFFF,
   0xFFFF, 0x0400, 0x0000, 0x0000, 0x4C09, 0x10B0, 0xFFFF, 0x00FF,
   0x0000, 0x0000, 0x6335, 0x8B28, 0x631D, 0x0000, 0x608E, 0x6D1C,
   0x62BB, 0x0000, 0x6335, 0x8B28, 0x0000, 0x0000, 0x6335, 0x8B28,
};

/* NPE-G1 Midplane EEPROM contents ??? */
static unsigned short eeprom_vxr2_midplane_data[64] = {
   0x04FF, 0x4003, 0x5B41, 0x0200, 0xC046, 0x0320, 0x0049, 0xD00B,
   0x8249, 0x1B4C, 0x0B42, 0x4130, 0xC18B, 0x3131, 0x3131, 0x3131,
   0x3131, 0x0000, 0x0004, 0x0002, 0x0985, 0x1C13, 0xDA09, 0xCB86,
   0x4E50, 0x452D, 0x4731, 0x8000, 0x0000, 0x00FF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
   0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
};

/*
 * Midplane EEPROM array.
 */
static struct c7200_eeprom c7200_midplane_eeprom[] = {
   { "std", eeprom_midplane_data, sizeof(eeprom_midplane_data)/2 },
   { "vxr", eeprom_vxr_midplane_data, sizeof(eeprom_vxr_midplane_data)/2 },
   { "vxr2", eeprom_vxr2_midplane_data, sizeof(eeprom_vxr2_midplane_data)/2 },
   { NULL, NULL, 0 },
};

/* ======================================================================== */
/* PEM EEPROM definitions (for NPE-175 and NPE-225                          */
/* ======================================================================== */

/* NPE-175 */
static unsigned short eeprom_pem_npe175_data[16] = {
   0x01C3, 0x0100, 0xFFFF, 0xFFFF, 0x490D, 0x8A04, 0x0000, 0x0000,
   0x5000, 0x0000, 0x9906, 0x0400, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF,
};

/* NPE-225 */
static unsigned short eeprom_pem_npe225_data[16] = {
   0x01D5, 0x0100, 0xFFFF, 0xFFFF, 0x490D, 0x8A04, 0x0000, 0x0000,
   0x5000, 0x0000, 0x9906, 0x0400, 0x0000, 0xFFFF, 0xFFFF, 0xFFFF,
};

/*
 * PEM EEPROM array.
 */
static struct c7200_eeprom c7200_pem_eeprom[] = {
   { "npe-175", eeprom_pem_npe175_data, sizeof(eeprom_pem_npe175_data)/2 },
   { "npe-225", eeprom_pem_npe225_data, sizeof(eeprom_pem_npe225_data)/2 },
   { NULL, NULL, 0 },
};

/* ======================================================================== */
/* Port Adapter Drivers                                                     */
/* ======================================================================== */
static struct c7200_pa_driver pa_drivers[] = {
   { "C7200-IO-FE"  , 1, dev_c7200_iocard_init, dev_c7200_iocard_set_nio },
   { "PA-FE-TX"     , 1, dev_c7200_pa_fe_tx_init, dev_c7200_pa_fe_tx_set_nio },
   { "PA-4T+"       , 1, dev_c7200_pa_4t_init, dev_c7200_pa_4t_set_nio },
   { "PA-8T"        , 1, dev_c7200_pa_8t_init, dev_c7200_pa_8t_set_nio },
   { "PA-A1"        , 1, dev_c7200_pa_a1_init, dev_c7200_pa_a1_set_nio },
   { "PA-POS-OC3"   , 0, dev_c7200_pa_pos_init, dev_c7200_pa_pos_set_nio },
   { NULL           , 0, NULL },
};

/* ======================================================================== */
/* NPE Drivers                                                              */
/* ======================================================================== */
#define DECLARE_NPE(type) \
   int (c7200_init_##type)(c7200_t *router)
   
DECLARE_NPE(npe100);
DECLARE_NPE(npe150);
DECLARE_NPE(npe175);
DECLARE_NPE(npe200);
DECLARE_NPE(npe225);
DECLARE_NPE(npe300);
DECLARE_NPE(npe400);
DECLARE_NPE(npeg1);

static struct c7200_npe_driver npe_drivers[] = {
   { "npe-100" , c7200_init_npe100, 1, 0, 5,  0, 6 },
   { "npe-150" , c7200_init_npe150, 1, 0, 5,  0, 6 },
   { "npe-175" , c7200_init_npe175, 1, 2, 16, 1, 0 },
   { "npe-200" , c7200_init_npe200, 1, 0, 5,  0, 6 },
   { "npe-225" , c7200_init_npe225, 1, 2, 16, 1, 0 },
   { "npe-300" , c7200_init_npe300, 1, 2, 16, 1, 0 },
   { "npe-400" , c7200_init_npe400, 1, 2, 16, 1, 0 },
   { "npe-g1"  , c7200_init_npeg1, 0, -1, -1, -1, -1 },
   { NULL      , NULL },
};

/* ======================================================================== */

/* Find an EEPROM in the specified array */
struct c7200_eeprom *c7200_get_eeprom(struct c7200_eeprom *eeproms,char *name)
{
   int i;

   for(i=0;eeproms[i].name;i++)
      if (!strcmp(eeproms[i].name,name))
         return(&eeproms[i]);

   return NULL;
}

/* Get an EEPROM for a given NPE model */
struct c7200_eeprom *c7200_get_cpu_eeprom(char *npe_name)
{
   return(c7200_get_eeprom(c7200_cpu_eeprom,npe_name));
}

/* Get an EEPROM for a given midplane model */
struct c7200_eeprom *c7200_get_midplane_eeprom(char *midplane_name)
{
   return(c7200_get_eeprom(c7200_midplane_eeprom,midplane_name));
}

/* Get a PEM EEPROM for a given NPE model */
struct c7200_eeprom *c7200_get_pem_eeprom(char *npe_name)
{
   return(c7200_get_eeprom(c7200_pem_eeprom,npe_name));
}

/* Set the base MAC address of the chassis */
int c7200_set_mac_addr(struct c7200_eeprom *mp_eeprom,m_eth_addr_t *addr)
{
   u_int eeprom_ver;

   eeprom_ver = mp_eeprom->data[0] >> 8;

   if (eeprom_ver != 1) {
      fprintf(stderr,"c7200_set_mac_addr: unable to handle "
              "EEPROM version %u\n",eeprom_ver);
      return(-1);
   }

   mp_eeprom->data[6] = (addr->octet[0] << 8) | (addr->octet[1]);
   mp_eeprom->data[7] = (addr->octet[2] << 8) | (addr->octet[3]);
   mp_eeprom->data[8] = (addr->octet[4] << 8) | (addr->octet[5]);
   return(0);
}

/* Get driver info about the specified slot */
void *c7200_get_slot_drvinfo(c7200_t *router,u_int pa_bay)
{
   if (pa_bay >= MAX_PA_BAYS)
      return NULL;

   return(router->pa_bay[pa_bay].drv_info);
}

/* Set driver info for the specified slot */
int c7200_set_slot_drvinfo(c7200_t *router,u_int pa_bay,void *drv_info)
{
   if (pa_bay >= MAX_PA_BAYS)
      return(-1);
   
   router->pa_bay[pa_bay].drv_info = drv_info;
   return(0);
}

/* Initialize a Port Adapter */
int c7200_pa_init(c7200_t *router,char *dev_type,u_int pa_bay)
{
   char *dev_name;
   size_t len;
   int i;

   len = strlen(dev_type) + 10;
   if (!(dev_name = malloc(len))) {
      fprintf(stderr,"c7200_pa_init: unable to allocate device name.\n");
      return(-1);
   }

   snprintf(dev_name,len,"%s(%u)",dev_type,pa_bay);

   for(i=0;pa_drivers[i].dev_type;i++)
      if (!strcmp(pa_drivers[i].dev_type,dev_type)) {
         router->pa_bay[pa_bay].pa_driver = &pa_drivers[i];
         return(pa_drivers[i].pa_init(router,dev_name,pa_bay));
      }

   fprintf(stderr,"c7200_pa_init: unknown driver '%s'\n",dev_type);
   free(dev_name);
   return(-1);
}

/* Bind a Network IO descriptor to a Port Adapter */
int c7200_pa_bind_nio(c7200_t *router,u_int pa_bay,u_int port_id,
                      netio_desc_t *nio)
{
   struct c7200_pa_driver *pa_driver;

   if ((pa_bay >= MAX_PA_BAYS) || !c7200_get_slot_drvinfo(router,pa_bay))
      return(-1);

   pa_driver = router->pa_bay[pa_bay].pa_driver;
   return(pa_driver->pa_set_nio(router,pa_bay,port_id,nio));
}

/* Maximum number of tokens in a PA description */
#define PA_DESC_MAX_TOKENS  8

/* Create a Port Adapter */
int c7200_pa_create(c7200_t *router,char *str)
{
   char *tokens[PA_DESC_MAX_TOKENS];
   int i,count,res;
   u_int pa_bay;

   /* A port adapter description is like "1:PA-FE-TX" */
   if ((count = strsplit(str,':',tokens,PA_DESC_MAX_TOKENS)) != 2) {
      fprintf(stderr,"c7200_pa_create: unable to parse '%s'\n",str);
      return(-1);
   }

   /* Parse the PA bay id */
   pa_bay = atoi(tokens[0]);

   /* Initialize the PA stuff */
   res = c7200_pa_init(router,tokens[1],pa_bay);

   /* The complete array was cleaned by strsplit */
   for(i=0;i<PA_DESC_MAX_TOKENS;i++)
      free(tokens[i]);

   return(res);
}

/* Create a Port Adapter and bind it to a Network IO descriptor */
int c7200_pa_create_nio(c7200_t *router,char *str)
{
   char *tokens[PA_DESC_MAX_TOKENS];
   int i,count,nio_type,res=-1;
   netio_desc_t *nio;
   u_int pa_bay;
   u_int port_id;

   /* A port adapter description is like "1:3:tap:tap0" */
   if ((count = strsplit(str,':',tokens,PA_DESC_MAX_TOKENS)) < 3) {
      fprintf(stderr,"c7200_pa_create: unable to parse '%s'\n",str);
      return(-1);
   }

   /* Parse the PA bay */
   pa_bay = atoi(tokens[0]);

   /* Parse the PA port id */
   port_id = atoi(tokens[1]);

   /* Create the Network IO descriptor */
   nio_type = netio_get_type(tokens[2]);

   switch(nio_type) {
      case NETIO_TYPE_UNIX:
         if (count != 5) {
            fprintf(stderr,"c7200_pa_create: invalid number of arguments "
                    "for UNIX NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_unix(tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TAP:
         if (count != 4) {
            fprintf(stderr,"c7200_pa_create: invalid number of arguments "
                    "for TAP NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_tap(tokens[3]);
         break;

      case NETIO_TYPE_UDP:
         if (count != 6) {
            fprintf(stderr,"c7200_pa_create: invalid number of arguments "
                    "for UDP NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_udp(atoi(tokens[3]),tokens[4],
                                     atoi(tokens[5]));
         break;

      case NETIO_TYPE_TCP_CLI:
         if (count != 5) {
            fprintf(stderr,"c7200_pa_create: invalid number of arguments "
                    "for TCP CLI NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_tcp_cli(tokens[3],tokens[4]);
         break;

      case NETIO_TYPE_TCP_SER:
         if (count != 4) {
            fprintf(stderr,"c7200_pa_create: invalid number of arguments "
                    "for TCP SER NIO '%s'\n",str);
            goto done;
         }

         nio = netio_desc_create_tcp_ser(tokens[3]);
         break;

      case NETIO_TYPE_NULL:
         nio = netio_desc_create_null();
         break;

#ifdef LINUX_ETH
      case NETIO_TYPE_LINUX_ETH:
         if (count != 4) {
            fprintf(stderr,"c7200_pa_create: invalid number of arguments "
                    "for Linux Eth NIO '%s'\n",str);
            goto done;
         }
         
         nio = netio_desc_create_lnxeth(tokens[3]);
         break;
#endif

#ifdef GEN_ETH
      case NETIO_TYPE_GEN_ETH:
         if (count != 4) {
            fprintf(stderr,"c7200_pa_create: invalid number of arguments "
                    "for Generic Eth NIO '%s'\n",str);
            goto done;
         }
         
         nio = netio_desc_create_geneth(tokens[3]);
         break;
#endif

      default:
         fprintf(stderr,"c7200_pa_create: unknown NETIO type '%s'\n",
                 tokens[2]);
         goto done;
   }

   if (!nio) {
      fprintf(stderr,"c7200_pa_create: unable to create NETIO descriptor "
              "for PA bay %u\n",pa_bay);
      goto done;
   }

   /* Initialize the PA stuff */
   res = c7200_pa_bind_nio(router,pa_bay,port_id,nio);

 done:
   /* The complete array was cleaned by strsplit */
   for(i=0;i<PA_DESC_MAX_TOKENS;i++)
      free(tokens[i]);

   return(res);
}

/* Show the list of available PA drivers */
void c7200_pa_show_drivers(void)
{
   int i;

   printf("Available Port Adapter drivers:\n");

   for(i=0;pa_drivers[i].dev_type;i++) {
      printf("  * %s %s\n",
             pa_drivers[i].dev_type,
             !pa_drivers[i].supported ? "(NOT WORKING)" : "");
   }
   
   printf("\n");
}

/* Get an NPE driver */
struct c7200_npe_driver *c7200_get_npe_driver(char *npe_type)
{
   int i;

   for(i=0;npe_drivers[i].npe_type;i++)
      if (!strcmp(npe_drivers[i].npe_type,npe_type))
         return(&npe_drivers[i]);

   return NULL;
}

/* Show the list of available NPE drivers */
void c7200_npe_show_drivers(void)
{
   int i;

   printf("Available NPE drivers:\n");

   for(i=0;npe_drivers[i].npe_type;i++) {
      printf("  * %s %s\n",
             npe_drivers[i].npe_type,
             !npe_drivers[i].supported ? "(NOT WORKING)" : "");
   }
   
   printf("\n");
}

/* Initialize an NPE-100 board */
int c7200_init_npe100(c7200_t *router)
{
   struct pa_bay_info *bay;
   int i;

   /* Initialize the Galileo GT-64010 PCI controller */
   if (dev_gt64010_init(router->cpu_group,0x14000000ULL,0x1000,
                        C7200_GT64K_IRQ,router->pci_mb) == -1)
      return(-1);

   /* PCI bridges (MB0/MB1, MB0/MB2) */
   dev_dec21050_init(router->pci_mb[0],0,1);
   dev_dec21050_init(router->pci_mb[0],0,2);
   dev_dec21050_init(router->pci_mb[0],0,3);
   dev_dec21050_init(router->pci_mb[0],0,4);

   /* PA bridges for Bays 1 to 6 */
   for(i=0;i<MAX_PA_BAYS;i++) {
      router->pa_bay[i].pci_map = router->pci_mb[0];
      bay = c7200_get_pa_bay_info(i);

      if ((bay != NULL) && (bay->pci_device != -1))
         dev_dec21050_init(router->pci_mb[0],
                           bay->pci_primary_bus,
                           bay->pci_device);
   }

   return(0);
}

/* Initialize an NPE-150 board */
int c7200_init_npe150(c7200_t *router)
{
   struct pa_bay_info *bay;
   int i;

   /* Initialize the Galileo GT-64010 PCI controller */
   if (dev_gt64010_init(router->cpu_group,0x14000000ULL,0x1000,
                        C7200_GT64K_IRQ,router->pci_mb) == -1)
      return(-1);

   /* PCI bridges (MB0/MB1, MB0/MB2) */
   dev_dec21050_init(router->pci_mb[0],0,1);
   dev_dec21050_init(router->pci_mb[0],0,2);
   dev_dec21050_init(router->pci_mb[0],0,3);
   dev_dec21050_init(router->pci_mb[0],0,4);

   /* PA bridges for Bays 1 to 6 */
   for(i=0;i<MAX_PA_BAYS;i++) {
      router->pa_bay[i].pci_map = router->pci_mb[0];
      bay = c7200_get_pa_bay_info(i);

      if ((bay != NULL) && (bay->pci_device != -1))
         dev_dec21050_init(router->pci_mb[0],
                           bay->pci_primary_bus,
                           bay->pci_device);
   }

   /* Packet SRAM: 1 Mb */
   dev_c7200_sram_init(router->cpu_group,"sram0","pred_sram0",
                       0x4b000000ULL,0x80000,router->pci_mb[0],4);
   dev_c7200_sram_init(router->cpu_group,"sram1","pred_sram1",
                       0x4b080000ULL,0x80000,router->pci_mb[0],10);
   return(0);
}

/* Initialize an NPE-175 board */
int c7200_init_npe175(c7200_t *router)
{
   int i;

   /* Initialize the Galileo GT-64120 PCI controller */
   if (dev_gt64120_init(router->cpu_group,0x14000000ULL,0x1000,
                        C7200_GT64K_IRQ,router->pci_mb) == -1)
      return(-1);

   /* PCI bridge for I/O card device on MB0 */
   dev_dec21050_init(router->pci_mb[0],0,1);

   /* PA bridges for Bays 1 to 6 */
   dev_dec21050_init(router->pci_mb[0],0,7);
   dev_dec21050_init(router->pci_mb[0],0,8);
   dev_dec21050_init(router->pci_mb[0],0,9);

   dev_dec21050_init(router->pci_mb[1],0,7);
   dev_dec21050_init(router->pci_mb[1],0,8);
   dev_dec21050_init(router->pci_mb[1],0,9);

   for(i=0;i<MAX_PA_BAYS;i++) {
      if ((i == 0) || (i & 1))
         router->pa_bay[i].pci_map = router->pci_mb[0];
      else
         router->pa_bay[i].pci_map = router->pci_mb[1];
   }

   return(0);
}

/* Initialize an NPE-200 board */
int c7200_init_npe200(c7200_t *router)
{
   struct pa_bay_info *bay;
   int i;

   /* Initialize the Galileo GT-64010 PCI controller */
   if (dev_gt64010_init(router->cpu_group,0x14000000ULL,0x1000,
                        C7200_GT64K_IRQ,router->pci_mb) == -1)
      return(-1);

   /* PCI bridges (MB0/MB1, MB0/MB2) */
   dev_dec21050_init(router->pci_mb[0],0,1);
   dev_dec21050_init(router->pci_mb[0],0,2);
   dev_dec21050_init(router->pci_mb[0],0,3);
   dev_dec21050_init(router->pci_mb[0],0,4);

   /* PA bridges for Bays 1 to 6 */
   for(i=0;i<MAX_PA_BAYS;i++) {
      router->pa_bay[i].pci_map = router->pci_mb[0];
      bay = c7200_get_pa_bay_info(i);

      if ((bay != NULL) && (bay->pci_device != -1))
         dev_dec21050_init(router->pci_mb[0],bay->pci_primary_bus,
                           bay->pci_device);
   }

   /* Packet SRAM: 4 Mb */
   dev_c7200_sram_init(router->cpu_group,"sram0","pred_sram0",
                       0x4b000000ULL,0x200000,router->pci_mb[0],4);
   dev_c7200_sram_init(router->cpu_group,"sram1","pred_sram1",
                       0x4b200000ULL,0x200000,router->pci_mb[0],10);
   return(0);
}

/* Initialize an NPE-225 board */
int c7200_init_npe225(c7200_t *router)
{
   int i;

   /* Initialize the Galileo GT-64120 PCI controller */
   if (dev_gt64120_init(router->cpu_group,0x14000000ULL,0x1000,
                        C7200_GT64K_IRQ,router->pci_mb) == -1)
      return(-1);

   /* PCI bridge for I/O card device on MB0 */
   dev_dec21150_init(router->pci_mb[0],0,1);

   /* PA bridges for Bays 1 to 6 */
   dev_dec21150_init(router->pci_mb[0],0,7);
   dev_dec21150_init(router->pci_mb[0],0,8);
   dev_dec21150_init(router->pci_mb[0],0,9);

   dev_dec21150_init(router->pci_mb[1],0,7);
   dev_dec21150_init(router->pci_mb[1],0,8);
   dev_dec21150_init(router->pci_mb[1],0,9);

   for(i=0;i<MAX_PA_BAYS;i++) {
      if ((i == 0) || (i & 1))
         router->pa_bay[i].pci_map = router->pci_mb[0];
      else
         router->pa_bay[i].pci_map = router->pci_mb[1];
   }

   return(0);
}

/* Initialize an NPE-300 board */
int c7200_init_npe300(c7200_t *router)
{
   struct pci_data *pci_data2[2];
   struct pa_bay_info *bay;

   int i;

   /* Initialize the first Galileo GT-64120A PCI controller */
   if (dev_gt64120_init(router->cpu_group,0x14000000ULL,0x1000,
                        C7200_GT64K_IRQ,router->pci_mb) == -1)
      return(-1);

   /* Initialize the second Galileo GT-64120A PCI controller */
   if (dev_gt64120_init(router->cpu_group,0x15000000ULL,0x1000,
                        -1,pci_data2) == -1)
      return(-1);

   /* 32 Mb of I/O memory */
   dev_create_ram(router->cpu_group,"iomem0","pred_iomem0",
                  0x20000000ULL,32*1048576);

   /* PCI bridge for I/O card device on MB0 */
   dev_dec21150_init(router->pci_mb[0],0,1);
   dev_dec21150_init(router->pci_mb[0],0,2);
   dev_dec21150_init(router->pci_mb[1],0,1);

   /* PA bridges for Bays 1 to 6 */
   for(i=0;i<MAX_PA_BAYS;i++) {
      if ((i == 0) || (i & 1))
         router->pa_bay[i].pci_map = router->pci_mb[0];
      else
         router->pa_bay[i].pci_map = router->pci_mb[1];

      bay = c7200_get_pa_bay_info(i);

      if ((bay != NULL) && (bay->pci_device != -1)) {
         dev_dec21150_init(router->pa_bay[i].pci_map,
                           bay->pci_primary_bus,
                           bay->pci_device);        
      }
   }

   return(0);
}

/* Initialize an NPE-400 board */
int c7200_init_npe400(c7200_t *router)
{
   int i;

   /* Initialize the Galileo GT-64120A PCI controller */
   if (dev_gt64120_init(router->cpu_group,0x14000000ULL,0x1000,
                        C7200_GT64K_IRQ,router->pci_mb) == -1)
      return(-1);

   /* PCI bridge for I/O card device on MB0 */
   dev_dec21050_init(router->pci_mb[0],0,1);

   /* PA bridges for Bays 1 to 6 */
   dev_dec21150_init(router->pci_mb[0],0,7);
   dev_dec21150_init(router->pci_mb[0],0,8);
   dev_dec21150_init(router->pci_mb[0],0,9);

   dev_dec21150_init(router->pci_mb[1],0,7);
   dev_dec21150_init(router->pci_mb[1],0,8);
   dev_dec21150_init(router->pci_mb[1],0,9);

   for(i=0;i<MAX_PA_BAYS;i++) {
      if ((i == 0) || (i & 1))
         router->pa_bay[i].pci_map = router->pci_mb[0];
      else
         router->pa_bay[i].pci_map = router->pci_mb[1];
   }

   return(0);
}

/* Initialize an NPE-G1 board */
int c7200_init_npeg1(c7200_t *router)
{
   /* XXX TO BE DONE */
   dev_sb1_duart_init(router->cpu_group,0x10060000,0x1000);
   return(0);
}

/* Initialize default parameters for a C7200 */
void c7200_init_defaults(c7200_t *router)
{
   memset(router,0,sizeof(*router));
   router->npe_type      = C7200_DEFAULT_NPE_TYPE;
   router->midplane_type = C7200_DEFAULT_MIDPLANE;
   router->ram_size      = C7200_DEFAULT_RAM_SIZE;
   router->rom_size      = C7200_DEFAULT_ROM_SIZE;
   router->nvram_size    = C7200_DEFAULT_NVRAM_SIZE;
   router->conf_reg      = C7200_DEFAULT_CONF_REG;
   router->clock_divisor = C7200_DEFAULT_CLOCK_DIV;
   router->ram_mmap      = C7200_DEFAULT_RAM_MMAP;

   router->vtty_con_type = VTTY_TYPE_TERM;
   router->vtty_aux_type = VTTY_TYPE_NONE;
}

/* Initialize the C7200 Platform */
int c7200_init_platform(c7200_t *router)
{
   cpu_group_t *cpu_group = router->cpu_group;
   cpu_mips_t *cpu;
   m_list_t *item;

   if (!router->npe_type)
      router->npe_type = C7200_DEFAULT_NPE_TYPE;

   if (!router->midplane_type)
      router->midplane_type = C7200_DEFAULT_MIDPLANE;

   if (!(router->npe_driver = c7200_get_npe_driver(router->npe_type))) {
      fprintf(stderr,"c7200_init_platform: unknown NPE type '%s'\n",
              router->npe_type);
      return(-1);
   }

   cpu = cpu_group_find_id(cpu_group,0);

   /*
    * On the C7200, bit 33 of physical addresses is used to bypass L2 cache.
    * We clear it systematically.
    */
   cpu->addr_bus_mask = C7200_ADDR_BUS_MASK;

   /* Basic memory mapping: RAM and ROM */
   mts32_init_kernel_mode(cpu);

   if (router->ram_mmap)
      router->ram_filename = "pred_ram0";
   else
      router->ram_filename = NULL;

   dev_create_ram(cpu_group,"ram",router->ram_filename,
                  0x00000000ULL,router->ram_size*1048576);

   if (!router->rom_filename) {
      /* use embedded ROM */
      dev_rom_init(cpu_group,0x1fc00000ULL,router->rom_size*1048576);
   } else {
      /* use alternate ROM */
      dev_create_ram(cpu_group,"rom","pred_rom0",
                     C7200_ROM_ADDR,router->rom_size*1048576);
   }

   /* Remote emulator control */
   dev_create_remote_control(router,0x11000000,0x1000);

   /* Bootflash */
   dev_bootflash_init(cpu_group,"pred_bootflash0",
                      C7200_BOOTFLASH_ADDR,(8 * 1048576));

   /* NVRAM and calendar */
   dev_nvram_init(cpu_group,"pred_nvram0",
                  C7200_NVRAM_ADDR,router->nvram_size*1024,
                  &router->conf_reg);

   /* Bit-bucket zone */
   dev_zero_init(cpu_group,"zero",C7200_BITBUCKET_ADDR,0xc00000);

   /* Midplane FPGA */
   dev_mpfpga_init(cpu_group,C7200_MPFPGA_ADDR,0x1000);

   /* IO FPGA */
   if (dev_iofpga_init(router,C7200_IOFPGA_ADDR,0x1000) == -1)
      return(-1);

   /* PCI IO space */
   if (!(router->pci_io_space = pci_io_init(cpu_group,0x100000000ULL)))
      return(-1);

   /* Initialize the NPE board */
   if (router->npe_driver->npe_init(router) == -1)
      return(-1);

   /* Initialize Port Adapters */
   for(item=router->pa_desc_list;item;item=item->next)
      if (c7200_pa_create(router,(char *)item->data) == -1) {
         fprintf(stderr,"C7200: Unable to create Port Adapter \"%s\"\n",
                 (char *)item->data);
         return(-1);
      }

   /* By default, initialize a C7200-IO-FE in slot 0 if nothing found */
   if (!router->pa_bay[0].drv_info)
      c7200_pa_init(router,"C7200-IO-FE",0);

   /* Initialize NIO */
   for(item=router->pa_nio_desc_list;item;item=item->next)
      if (c7200_pa_create_nio(router,(char *)item->data) == -1) {
         fprintf(stderr,"C7200: Unable to create NIO \"%s\"\n",
                 (char *)item->data);
         return(-1);
      }

   /* Cirrus Logic PD6729 (PCI-to-PCMCIA host adapter) */
   dev_clpd6729_init(cpu_group,router->pci_mb[0],
                     router->npe_driver->clpd6729_pci_bus,
                     router->npe_driver->clpd6729_pci_dev,
                     router->pci_io_space,0x402,0x403);

   /* Show device list */
   dev_show_list(cpu);
   pci_dev_show_list(router->pci_mb[0]);
   pci_dev_show_list(router->pci_mb[1]);

   /* Map all devices in kernel memory (kseg0/kseg1) */
   mts32_km_map_all_dev(cpu);
   mts_init_memop_vectors(cpu);
   return(0);
}
