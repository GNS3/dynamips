/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * MIPS Instruction Lookup Tables.
 */

#ifndef __INSN_LOOKUP_H__
#define __INSN_LOOKUP_H__

#include "utils.h"
#include "hash.h"

/* Forward declaration for instruction lookup table */
typedef struct insn_lookup insn_lookup_t;

/* CBM (Class BitMap) array */
#define CBM_SHIFT       5                 /* log2(32) */
#define CBM_SIZE        (1 << CBM_SHIFT)  /* Arrays of 32-bits Integers */
#define CBM_HASH_SIZE   256               /* Size for Hash Tables */

typedef struct cbm_array cbm_array_t;
struct cbm_array {
   int nr_entries;   /* Number of entries */
   int tab[0];       /* Values... */
};

#define CBM_ARRAY(array,i) ((array)->tab[(i)])
#define CBM_CSIZE(count)   (((count)*sizeof(int))+sizeof(cbm_array_t))

/* callback function prototype for instruction checking */
typedef int (*ilt_check_cbk_t)(void *,int value);
typedef void *(*ilt_get_insn_cbk_t)(int index);

/* RFC (Recursive Flow Classification) arrays */
#define RFC_ARRAY_MAXSIZE  65536
#define RFC_ARRAY_MAXBITS  16
#define RFC_ARRAY_NUMBER   3

typedef struct rfc_array rfc_array_t;
struct rfc_array {
   rfc_array_t *parent0,*parent1;
   int nr_elements;

   /* Number of Equivalent ID */
   int nr_eqid;

   /* Hash Table for Class Bitmaps */
   hash_table_t *cbm_hash;

   /* Array to get Class Bitmaps from IDs */
   cbm_array_t **id2cbm;

   /* Equivalent ID (eqID) array */
   int eqID[0];
};

/* Equivalent Classes */
typedef struct rfc_eqclass rfc_eqclass_t;
struct rfc_eqclass {
   cbm_array_t *cbm;   /* Class Bitmap */
   int eqID;           /* Index associated to this class */
};

/* Instruction lookup table */
struct insn_lookup {
   int nr_insn;    /* Number of instructions */
   int cbm_size;   /* Size of Class Bitmaps */

   ilt_get_insn_cbk_t get_insn;
   ilt_check_cbk_t chk_lo,chk_hi;

   /* RFC tables */
   rfc_array_t *rfct[RFC_ARRAY_NUMBER];
};

/* Instruction lookup */
static forced_inline int ilt_get_index(rfc_array_t *a1,rfc_array_t *a2,
                                       int i1,int i2)
{
   return((a1->eqID[i1]*a2->nr_eqid) + a2->eqID[i2]);
}

static forced_inline int ilt_get_idx(insn_lookup_t *ilt,int a1,int a2,
                                     int i1,int i2)
{
   return(ilt_get_index(ilt->rfct[a1],ilt->rfct[a2],i1,i2));
}

static forced_inline int ilt_lookup(insn_lookup_t *ilt,mips_insn_t insn)
{
   int id_i;

   id_i = ilt_get_idx(ilt,0,1,insn >> 16,insn & 0xFFFF);
   return(ilt->rfct[2]->eqID[id_i]);
}

/* Create an instruction lookup table */
insn_lookup_t *ilt_create(char *table_name,
                          int nr_insn,ilt_get_insn_cbk_t get_insn,
                          ilt_check_cbk_t chk_lo,ilt_check_cbk_t chk_hi);

/* Destroy an instruction lookup table */
void ilt_destroy(insn_lookup_t *ilt);

#endif
