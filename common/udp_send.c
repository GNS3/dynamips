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
   size_t pkt_size;
   int sck;
   FILE *fd;

   if (!(fd = fopen(argv[1],"r"))) {
      perror("fopen");
      exit(EXIT_FAILURE);
   }

   /* Read packet from file */
   pkt_size = fread(pkt,1,MAX_PKT_SIZE,fd);

   /* Connect to remote port */
   if ((sck = udp_connect(atoi(argv[2]),argv[3],atoi(argv[4]))) < 0)
      exit(EXIT_FAILURE);

   /* Send it */
   if (send(sck,pkt,pkt_size,0) < 0)
      exit(EXIT_FAILURE);

   close(sck);
   return(0);
}
