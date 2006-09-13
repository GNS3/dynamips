/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 *
 * Produces assembly definitions.
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

#include "rbtree.h"
#include "mips64.h"
#include "cp0.h"
#include "memory.h"
#include "cpu.h"
#include "device.h"

#define OUTPUT "asmdefs.h"

int main(int argc,char *argv[])
{
   FILE *fd;

   if (!(fd = fopen(OUTPUT,"w"))) {
      fprintf(stderr,"%s: unable to create output file.\n",argv[0]);
      exit(EXIT_FAILURE);
   }

   fprintf(fd,"#define CP0_VCNT_OFS  %lu\n",
           OFFSET(cpu_mips_t,cp0_virt_cnt_reg));

   fprintf(fd,"#define CP0_VCMP_OFS  %lu\n",
           OFFSET(cpu_mips_t,cp0_virt_cmp_reg));

   fprintf(fd,"#define MTS_L1_OFS    %lu\n",
           OFFSET(cpu_mips_t,mts_l1_ptr));

   fprintf(fd,"#define CPU_GPR_OFS   %lu\n",
           OFFSET(cpu_mips_t,gpr));

   fprintf(fd,"#define CPU_MTS64_CACHE_OFS   %lu\n",
           OFFSET(cpu_mips_t,mts64_cache));

   fprintf(fd,"#define MTS64_ENTRY_START_OFS   %lu\n",
           OFFSET(mts64_entry_t,start));

   fprintf(fd,"#define MIPS_MEMOP_LW  %d\n",MIPS_MEMOP_LW);

   fclose(fd);
   return(0);
}
