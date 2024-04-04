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

   if ((fd = open(argv[1],O_RDONLY)) == -1) {
      perror("open");
      goto err_open;
   }

   if (elf_version(EV_CURRENT) == EV_NONE) {
      fprintf(stderr,"elf_version: library out of date\n");
      goto err_elf_version;
   }

   if (!(img_elf = elf_begin(fd,ELF_C_READ,NULL))) {
      fprintf(stderr,"elf_begin: %s\n",elf_errmsg(elf_errno()));
      goto err_elf_begin;
   }

   if (!(phdr = elf32_getphdr(img_elf))) {
      fprintf(stderr,"elf32_getphdr: %s\n",elf_errmsg(elf_errno()));
      goto err_elf32_getphdr;
   }

   if (!(ehdr = elf32_getehdr(img_elf))) {
      fprintf(stderr,"elf32_getehdr: %s\n",elf_errmsg(elf_errno()));
      goto err_elf32_getehdr;
   }

   if (!(fd_out = fopen(argv[2],"w"))) {
      fprintf(stderr,"Unable to create file \"%s\"\n",argv[2]);
      goto err_fopen;
   }

   printf("Extracting ROM from ELF file '%s'...\n",argv[1]);
   bfd = fdopen(fd,"r");

   if (!bfd) {
      perror("fdopen");
      goto err_fdopen;
   }

   for(i=0;i<ehdr->e_phnum;i++,phdr++)
   {
      if (fseek(bfd,phdr->p_offset,SEEK_SET) != 0) {
         perror("fseek");
         goto err_fseek;
      }

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

   fclose(bfd);
   fclose(fd_out);
   elf_end(img_elf);
   return(0);

err_fseek:
   fclose(bfd); // instead of close(fd)
   fclose(fd_out);
   elf_end(img_elf);
   return(EXIT_FAILURE);
err_fdopen:
   fclose(fd_out);
err_fopen:
err_elf32_getehdr:
err_elf32_getphdr:
   elf_end(img_elf);
err_elf_begin:
err_elf_version:
   close(fd);
err_open:
   return(EXIT_FAILURE);
}
