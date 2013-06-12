#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc,char *argv[])
{
   unsigned char buffer[8];
   FILE *fd_in,*fd_out;
   size_t len;
   int i;

   if (argc != 3) {
      fprintf(stderr,"Usage: %s <input_file> <output_file>\n",argv[0]);
      exit(EXIT_FAILURE);
   }

   if (!(fd_in = fopen(argv[1],"r"))) {
      fprintf(stderr,"Unable to open file \"%s\"\n",argv[1]);
      exit(EXIT_FAILURE);
   }

   if (!(fd_out = fopen(argv[2],"w"))) {
      fprintf(stderr,"Unable to create file \"%s\"\n",argv[2]);
      exit(EXIT_FAILURE);
   }
   
   while(!feof(fd_in)) {
      len = fread(buffer,1,sizeof(buffer),fd_in);
      if (len == 0) break;

      fprintf(fd_out,"   ");
      for(i=0;i<len;i++)
         fprintf(fd_out,"0x%2.2x, ",buffer[i]);

      fprintf(fd_out,"\n");
   }

   fclose(fd_in);
   fclose(fd_out);
   return(0);
}
