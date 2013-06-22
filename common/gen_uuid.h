/*  
 * Copyright (c) 2008 Christophe Fillot.
 * E-mail: cf@utc.fr
 *
 * UUID handling.
 */

#ifndef __GEN_UUID_H__
#define __GEN_UUID_H__  1

#include <uuid/uuid.h>

/* Get local ID */
void gen_uuid_get_local(uuid_t dst);

/* Compare UUID */
int gen_uuid_compare(uuid_t id);

/* Initialize UUID */
void gen_uuid_init(void);

#endif
