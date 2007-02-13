/*
 * Cisco C7200 Simulation Platform.
 * Copyright (c) 2005,2006 Christophe Fillot.  All rights reserved.
 *
 * Extract IOS configuration from a NVRAM file (standalone tool)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "cpu.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_c7200.h"
#include "dev_c3600.h"

#define PLATFORM_C7200	0x37323030
#define PLATFORM_C3600	0x33363030

/* Export configuration from 7200 NVRAM */
int dev_nvram_7200_export_config(FILE *nvram_fd,FILE *cfg_fd)
{
   m_uint32_t tag,start,end,len,clen,nvlen;
   char buffer[512];
   int res = -1;

   fseek(nvram_fd,C7200_NVRAM_ROM_RES_SIZE+6,SEEK_SET);
   fread(&tag,sizeof(tag),1,nvram_fd);
   if (ntohl(tag) != 0xF0A5ABCD) {
      fprintf(stderr,"NVRAM: Unable to find IOS tag (tag=0x%8.8x)!\n",
              ntohl(tag));
      goto done;
   }

   fseek(nvram_fd,0x06,SEEK_CUR);
   fread(&start,sizeof(start),1,nvram_fd);
   fread(&end,sizeof(end),1,nvram_fd);
   fread(&nvlen,sizeof(nvlen),1,nvram_fd);
   start = htonl(start) + 1;
   end   = htonl(end);
   nvlen = htonl(nvlen);

   if ((start <= C7200_NVRAM_ADDR) || (end <= C7200_NVRAM_ADDR) || 
       (end <= start)) 
   {
      fprintf(stderr,"NVRAM: invalid configuration markers "
              "(start=0x%x,end=0x%x).\n",start,end);
      goto done;
   }
   
   clen = len = end - start;
   if ((clen + 1) != nvlen) {
      fprintf(stderr,"NVRAM: invalid configuration size (0x%x)\n",nvlen);
      goto done;
   }

   start -= C7200_NVRAM_ADDR;
   fseek(nvram_fd,start,SEEK_SET);

   while(len > 0) {
      if (len > sizeof(buffer))
         clen = sizeof(buffer);
      else
         clen = len;

      fread(buffer,clen,1,nvram_fd);
      fwrite(buffer,clen,1,cfg_fd);
      len -= clen;
   }

   res = 0;
 done:
   return res;
}

/* Export configuration from 3600 NVRAM */
int dev_nvram_3600_export_config(FILE *nvram_fd,FILE *cfg_fd)
{
   m_uint32_t tag,start,end,len,clen,nvlen;
   char buffer[512];
   int res = -1;

   fseek(nvram_fd,C3600_NVRAM_ROM_RES_SIZE+6,SEEK_SET);
   fread(&tag,sizeof(tag),1,nvram_fd);
   if (ntohl(tag) != 0xF0A5ABCD) {
      fprintf(stderr,"NVRAM: Unable to find IOS tag (tag=0x%8.8x)!\n",
              ntohl(tag));
      goto done;
   }

   fseek(nvram_fd,0x06,SEEK_CUR);
   fread(&start,sizeof(start),1,nvram_fd);
   fread(&end,sizeof(end),1,nvram_fd);
   fread(&nvlen,sizeof(nvlen),1,nvram_fd);
   start = htonl(start) + 1 + C3600_NVRAM_ADDR + C3600_NVRAM_ROM_RES_SIZE + 8;
   end   = htonl(end) + C3600_NVRAM_ADDR + C3600_NVRAM_ROM_RES_SIZE + 8;
   nvlen = htonl(nvlen);

   if ((start <= C3600_NVRAM_ADDR) || (end <= C3600_NVRAM_ADDR) || 
       (end <= start)) 
   {
      fprintf(stderr,"NVRAM: invalid configuration markers "
              "(start=0x%x,end=0x%x).\n",start,end);
      goto done;
   }
   
   clen = len = end - start;
   if ((clen + 1) != nvlen) {
      fprintf(stderr,"NVRAM: invalid configuration size (0x%x)\n",nvlen);
      goto done;
   }

   start -= C3600_NVRAM_ADDR;
   fseek(nvram_fd,start,SEEK_SET);

   while(len > 0) {
      if (len > sizeof(buffer))
         clen = sizeof(buffer);
      else
         clen = len;

      fread(buffer,clen,1,nvram_fd);
      fwrite(buffer,clen,1,cfg_fd);
      len -= clen;
   }

   res = 0;
 done:
   return res;
}

/* Export configuration from NVRAM */
int dev_nvram_export_config(char *nvram_filename,char *cfg_filename)
{
   m_uint32_t platform;
   FILE *nvram_fd,*cfg_fd;
   char platform_string[5];
   int res = -1;

   if (!(nvram_fd = fopen(nvram_filename,"r"))) {
      fprintf(stderr,"Unable to open NVRAM file '%s'!\n",nvram_filename);
      return(-1);
   }

   if (!(cfg_fd = fopen(cfg_filename,"w"))) {
      fprintf(stderr,"Unable to create config file '%s'!\n",cfg_filename);
      return(-1);
   }

   fseek(nvram_fd,0x200,SEEK_SET);
   fread(&platform,sizeof(platform),1,nvram_fd);

   switch(ntohl(platform)) {
      case PLATFORM_C7200:
	 res = dev_nvram_7200_export_config(nvram_fd,cfg_fd);
	 break;
      case PLATFORM_C3600:
	 res = dev_nvram_3600_export_config(nvram_fd,cfg_fd);
	 break;
      default:
	 memcpy(platform_string,&platform,4);
	 platform_string[4] = '\0';
	 fprintf(stderr,"NVRAM: unknown platform %s (platform=0x%x)!\n",
	         platform_string,ntohl(platform));
   }
 
   fclose(nvram_fd);
   fclose(cfg_fd);
   return(res);
}

int main(int argc,char *argv[])
{
   printf("Cisco 7200/3600 NVRAM configuration export.\n");
   printf("Copyright (c) 2006 Christophe Fillot.\n\n");

   if (argc != 3) {
      fprintf(stderr,"Usage: %s nvram_file config_file\n",argv[0]);
      exit(EXIT_FAILURE);
   }

   if (!dev_nvram_export_config(argv[1],argv[2]))
      printf("Configuration written to %s\n",argv[2]);

   return(0);
}
