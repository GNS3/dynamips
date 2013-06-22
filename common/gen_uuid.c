/*  
 * Copyright (c) 2008 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * UUID handling.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "gen_uuid.h"

/* Our local UUID */
static uuid_t local_uuid;

/* Get local ID */
void gen_uuid_get_local(uuid_t dst)
{
   uuid_copy(dst,local_uuid);
}

/* Compare UUID */
int gen_uuid_compare(uuid_t id)
{
   return(uuid_compare(local_uuid,id));
}

/* Initialize UUID */
void gen_uuid_init(void)
{
   char buffer[40];

   uuid_generate(local_uuid);
   uuid_unparse(local_uuid,buffer);
   printf("Local UUID: %s\n\n",buffer);
}
