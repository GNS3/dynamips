/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
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
#include <libelf.h>

#include "utils.h"

/* Extract ROM code+data from an ELF file and convert it into a C array */
int main(int argc,char *argv[])
{   
   unsigned char buffer[8];
   m_uint32_t vaddr,start;
   Elf32_Ehdr *ehdr;
   Elf32_Phdr *phdr;
   Elf *img_elf;
   size_t len,clen;
   int i,j,fd;
   FILE *bfd,*fd_out;

   if (argc != 4) {
      fprintf(stderr,"Usage: %s <input_file> <output_file> <addr>\n",argv[0]);
      exit(EXIT_FAILURE);
   }

   start = strtoul(argv[3],NULL,0);

   if ((fd = open(argv[1],O_RDONLY)) == -1)
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

   if (!(fd_out = fopen(argv[2],"w"))) {
      fprintf(stderr,"Unable to create file \"%s\"\n",argv[2]);
      exit(EXIT_FAILURE);
   }

   ehdr = elf32_getehdr(img_elf);
   phdr = elf32_getphdr(img_elf);

   printf("Extracting ROM from ELF file '%s'...\n",argv[1]);
   bfd = fdopen(fd,"r");

   if (!bfd) {
      perror("load_elf_image: fdopen");
      return(-1);
   }

   for(i=0;i<ehdr->e_phnum;i++,phdr++)
   {
      fseek(bfd,phdr->p_offset,SEEK_SET);

      vaddr = (m_uint64_t)phdr->p_vaddr;
      len = phdr->p_filesz;

      if (vaddr != start)
         continue;

      while(len > 0)
      {
         clen = fread(buffer,1,sizeof(buffer),bfd);

         if (clen == 0)
            break;

         fprintf(fd_out,"   ");

         for(j=0;j<clen;j++)
            fprintf(fd_out,"0x%2.2x, ",buffer[j]);

         fprintf(fd_out,"\n");
         len -= clen;
      }
   }

   fclose(fd_out);
   return(0);
}
