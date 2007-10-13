#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

#include "utils.h"
#include "net.h"

#define MAX_PKT_SIZE  2048

int main(int argc,char *argv[])
{
   char pkt[MAX_PKT_SIZE];
   ssize_t pkt_size;
   int sck = -1;
   FILE *fd;

   /* Wait connection */
   if (ip_listen(NULL,atoi(argv[2]),SOCK_DGRAM,1,&sck) < 1) {
      perror("ip_listen");
      exit(EXIT_FAILURE);
   }

   /* Receive packet and store it */
   if ((pkt_size = recvfrom(sck,pkt,sizeof(pkt),0,NULL,NULL)) < 0) {
      perror("recvfrom");
      exit(EXIT_FAILURE);
   }

   if (!(fd = fopen(argv[1],"w"))) {
      perror("fopen");
      exit(EXIT_FAILURE);
   }

   fwrite(pkt,1,pkt_size,fd);
   fclose(fd);
   close(sck);
   return(0);
}
