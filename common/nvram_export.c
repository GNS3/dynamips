/*
 * Cisco C7200 Simulation Platform.
 * Copyright (c) 2013 Flávio J. Saraiva
 *
 * Extract IOS configuration from a NVRAM file (standalone tool)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cpu.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"
#include "dev_c7200.h"
#include "dev_c3745.h"
#include "dev_c3725.h"
#include "dev_c3600.h"
#include "dev_c2691.h"
#include "dev_c2600.h"
#include "dev_c1700.h"
#include "dev_c6msfc1.h"
#include "fs_nvram.h"


struct nvram_format {
   const char *name;
   const char *rom_res_0x200;
   size_t offset;
   size_t size;
   m_uint32_t addr;
   u_int format;
} nvram_formats[] = {
   {"c1700", "C1700", C1700_NVRAM_ROM_RES_SIZE, 0, 0, FS_NVRAM_FORMAT_DEFAULT},
   {"c2600", "C2600", C2600_NVRAM_ROM_RES_SIZE*4, 0, 0, FS_NVRAM_FORMAT_SCALE_4},
   {"c3600", "3600", C3600_NVRAM_ROM_RES_SIZE, 0, 0, FS_NVRAM_FORMAT_DEFAULT},
   {"c7200", "7200", C7200_NVRAM_ROM_RES_SIZE, 0, C7200_NVRAM_ADDR + C7200_NVRAM_ROM_RES_SIZE, FS_NVRAM_FORMAT_ABSOLUTE},
   {"c7200-npe-g2", "7200", C7200_NVRAM_ROM_RES_SIZE, 0, C7200_G2_NVRAM_ADDR + C7200_NVRAM_ROM_RES_SIZE, FS_NVRAM_FORMAT_ABSOLUTE},
   {"c6msfc1", NULL, C6MSFC1_NVRAM_ROM_RES_SIZE, 0, C6MSFC1_NVRAM_ADDR + C6MSFC1_NVRAM_ROM_RES_SIZE, FS_NVRAM_FORMAT_ABSOLUTE_C6},
   {"c2691", NULL, C2691_NVRAM_OFFSET, C2691_NVRAM_SIZE, 0, FS_NVRAM_FORMAT_WITH_BACKUP},
   {"c3725", NULL, C3725_NVRAM_OFFSET, C3725_NVRAM_SIZE, 0, FS_NVRAM_FORMAT_WITH_BACKUP}, // XXX same as c2691
   {"c3745", NULL, C3745_NVRAM_OFFSET, C3745_NVRAM_SIZE, 0, FS_NVRAM_FORMAT_WITH_BACKUP},
   {"c7200-npe-g1", NULL, C7200_NVRAM_ROM_RES_SIZE, 0, C7200_G1_NVRAM_ADDR + C7200_NVRAM_ROM_RES_SIZE, FS_NVRAM_FORMAT_ABSOLUTE}, // XXX didn't find working image
   {NULL, NULL, 0, 0, 0, 0}
};


/** Read file data. */
int read_file(const char *filename, u_char **data, size_t *data_len)
{
   FILE *fd;
   long len;

   // open
   fd = fopen(filename,"rb");
   if (fd == NULL) {
      return(-1);
   }

   // len
   fseek(fd, 0, SEEK_END);
   len = ftell(fd);
   fseek(fd, 0, SEEK_SET);
   if (len < 0 || ferror(fd)) {
      fclose(fd);
      return(-1);
   }

   if (data_len) {
      *data_len = (size_t)len;
   }

    // data
   if (data) {
      *data = (u_char *)malloc((size_t)len);
      if (fread(*data, (size_t)len, 1, fd) != 1) {
         free(*data);
         *data = NULL;
         fclose(fd);
         return(-1);
      }
   }

   // close
   fclose(fd);
   return(0);
}


/** Write file data. */
int write_file(const char *filename, u_char *data, size_t len)
{
   FILE *fp;

   fp = fopen(filename,"wb+");
   if (fp == NULL) {
      return(-1);
   }

   if (fwrite(data, len, 1, fp) != 1) {
      fclose(fp);
      return(-1);
   }

   fclose(fp);
   return(0);
}


/** Export configuration from NVRAM. */
int nvram_export_config(const char *nvram_filename, const char *startup_filename, const char *private_filename)
{
   u_char *data = NULL;
   u_char *startup_config = NULL;
   u_char *private_config = NULL;
   size_t data_len = 0;
   size_t startup_len = 0;
   size_t private_len;
   fs_nvram_t *fs;
   size_t len;
   struct nvram_format *fmt;

   // read nvram
   printf("Reading %s...\n", nvram_filename);
   if (read_file(nvram_filename, &data, &data_len)) {
      perror(nvram_filename);
      return(-1);
   }

   // try each format
   for (fmt = &nvram_formats[0]; ; fmt++) {
      if (fmt->name == NULL) {
         fprintf(stderr,"NVRAM not found\n");
         return(-1);
      }

      if (fmt->rom_res_0x200) {
         len = strlen(fmt->rom_res_0x200);
         if (data_len < 0x200 + len || memcmp(data + 0x200, fmt->rom_res_0x200, len)) {
            continue; // must match
         }
      }

      if (fmt->size > 0) {
         if (data_len < fmt->offset + fmt->size) {
            continue; // must fit
         }
         len = fmt->size;
      }
      else {
         if (data_len < fmt->offset) {
            continue; // must fit
         }
         len = data_len - fmt->offset;
      }

      fs = fs_nvram_open(data + fmt->offset, len, fmt->addr, fmt->format);
      if (fs == NULL) {
         continue; // filesystem not found
      }

      if (fs_nvram_verify(fs, FS_NVRAM_VERIFY_ALL) || fs_nvram_read_config(fs, &startup_config, &startup_len, &private_config, &private_len)) {
         fs_nvram_close(fs);
         fs = NULL;
         continue; // filesystem error
      }

      printf("Found NVRAM format %s\n", fmt->name);
      fs_nvram_close(fs);
      fs = NULL;
      break;
   }

   // write config
   if (startup_filename) {
      printf("Writing startup-config to %s...\n", startup_filename);
      if (write_file(startup_filename, startup_config, startup_len)) {
         perror(startup_filename);
         return(-1);
      }
   }
   if (private_filename) {
      printf("Writing private-config to %s...\n", private_filename);
      if (write_file(private_filename, private_config, private_len)) {
         perror(private_filename);
         return(-1);
      }
   }

   // cleanup
   if (startup_config) {
      free(startup_config);
      startup_config = NULL;
   }
   if (private_config) {
      free(private_config);
      private_config = NULL;
   }
   return(0);
}

int main(int argc,char *argv[])
{
   const char *nvram_filename;
   const char *startup_filename;
   const char *private_filename = NULL;
   struct nvram_format *fmt;

   printf("Cisco NVRAM configuration export.\n");
   printf("Copyright (c) 2013 Flávio J. Saraiva.\n\n");

   if (argc < 3 || argc > 4) {
      fprintf(stderr,"Usage: %s nvram_file config_file [private_file]\n",argv[0]);
      fprintf(stderr,"\n");
      fprintf(stderr,"This tools extracts 'startup-config' and 'private-config' from NVRAM.\n");
      fprintf(stderr,"  nvram_file   - file that contains the NVRAM data\n");
      fprintf(stderr,"                 (on some platforms, NVRAM is simulated inside the ROM)\n");
      fprintf(stderr,"  config_file  - file for 'startup-config'\n");
      fprintf(stderr,"  private_file - file for 'private-config' (optional)\n");
      fprintf(stderr,"\n");
      fprintf(stderr,"Supports:");
      for (fmt = &nvram_formats[0]; fmt->name; fmt++) {
         fprintf(stderr," %s",fmt->name);
      }
      fprintf(stderr,"\n");
      exit(EXIT_FAILURE);
   }

   nvram_filename = argv[1];
   startup_filename = argv[2];
   if (argc > 3)
      private_filename = argv[3];
   
   if (nvram_export_config(nvram_filename,startup_filename,private_filename))
      return(EXIT_FAILURE);

   printf("Done\n");
   return(0);
}
