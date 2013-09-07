/*
 * Cisco router simulation platform.
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 *
 * Instruction Lookup Tables.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <assert.h>

#include "utils.h"
#include "hash.h"
#include "insn_lookup.h"
#include "dynamips.h"

/* Hash function for a CBM */
static inline u_int cbm_hash_f(void *ccbm)
{
   cbm_array_t *cbm = (cbm_array_t *)ccbm;
   char *p,*s = (char *)(cbm->tab);
   u_int h,g,i;

   for(h=0,i=0,p=s;i<(cbm->nr_entries*sizeof(int));p+=1,i++)
   {
      h = (h << 4) + *p;
      if ((g = h & 0xf0000000)) {
         h = h ^ (g >> 24);
         h = h ^ g;
      }
   }

   return(h);
}

/* Comparison function for 2 CBM */
static inline int cbm_cmp_f(void *b1,void *b2)
{
   cbm_array_t *cbm1 = (cbm_array_t *)b1;
   cbm_array_t *cbm2 = (cbm_array_t *)b2;
   int i;
   
   for(i=0;i<cbm1->nr_entries;i++)
      if (cbm1->tab[i] != cbm2->tab[i])
         return(FALSE);
   
   return(TRUE);
}

/* Set bit corresponding to a rule number in a CBM */
static inline void cbm_set_rule(cbm_array_t *cbm,int rule_id)
{
   CBM_ARRAY(cbm,(rule_id >> CBM_SHIFT)) |= 1 << (rule_id & (CBM_SIZE-1));
}

/* Clear bit corresponding to a rule number in a CBM */
static inline void cbm_unset_rule(cbm_array_t *cbm,int rule_id)
{
   CBM_ARRAY(cbm,(rule_id >> CBM_SHIFT)) &= ~(1 << (rule_id & (CBM_SIZE-1)));
}

/* Returns TRUE if  bit corresponding to a rule number in a CBM is set */
static inline int cbm_check_rule(cbm_array_t *cbm,int rule_id)
{
   return(CBM_ARRAY(cbm,(rule_id >> CBM_SHIFT)) & 
	  (1 << (rule_id & (CBM_SIZE-1))));
}

/* Compute bitwise ANDing of two CBM */
static inline void 
cbm_bitwise_and(cbm_array_t *result,cbm_array_t *a1,cbm_array_t *a2)
{
   int i;

   /* Compute bitwise ANDing */
   for(i=0;i<a1->nr_entries;i++)
      CBM_ARRAY(result,i) = CBM_ARRAY(a1,i) & CBM_ARRAY(a2,i);
}

/* Get first matching rule number */
static inline int cbm_first_match(insn_lookup_t *ilt,cbm_array_t *cbm)
{
   int i;

   for(i=0;i<ilt->nr_insn;i++)
      if (cbm_check_rule(cbm,i)) 
         return(i);
   
   return(-1);
}

/* Create a class bitmap (CBM) */
static cbm_array_t *cbm_create(insn_lookup_t *ilt)
{
   cbm_array_t *array;
   int size;

   size = CBM_CSIZE(ilt->cbm_size);

   /* CBM are simply bit arrays */
   array = malloc(size);
   assert(array);

   memset(array,0,size);
   array->nr_entries = ilt->cbm_size;
   return array;
}

/* Duplicate a class bitmap */
static cbm_array_t *cbm_duplicate(cbm_array_t *cbm)
{
   int size = CBM_CSIZE(cbm->nr_entries);
   cbm_array_t *array;

   array = malloc(size);
   assert(array);
   memcpy(array,cbm,size);
   return array;
}

/* 
 * Get equivalent class corresponding to a class bitmap. Create eqclass 
 * structure if needed (CBM not previously seen).
 */
static rfc_eqclass_t *cbm_get_eqclass(rfc_array_t *rfct,cbm_array_t *cbm)
{
   rfc_eqclass_t *eqcl;
   cbm_array_t *bmp;

   /* Lookup for CBM into hash table */
   if ((eqcl = hash_table_lookup(rfct->cbm_hash,cbm)) == NULL)
   {
      /* Duplicate CBM */
      bmp = cbm_duplicate(cbm);
      assert(bmp);

      /* CBM is not already known */
      eqcl = malloc(sizeof(rfc_eqclass_t));
      assert(eqcl);

      assert(rfct->nr_eqid < rfct->nr_elements);

      /* Get a new equivalent ID */
      eqcl->eqID = rfct->nr_eqid++;
      eqcl->cbm = bmp;
      rfct->id2cbm[eqcl->eqID] = bmp;

      /* Insert it in hash table */
      if (hash_table_insert(rfct->cbm_hash,bmp,eqcl) == -1)
         return NULL;
   }

   return eqcl;
}

/* Allocate an array for Recursive Flow Classification */
static rfc_array_t *rfc_alloc_array(int nr_elements)
{
   rfc_array_t *array;
   int total_size;

   /* Compute size of memory chunk needed to store the array */
   total_size = (nr_elements * sizeof(int)) + sizeof(rfc_array_t);
   array = malloc(total_size);
   assert(array);
   memset(array,0,total_size);
   array->nr_elements = nr_elements;
   
   /* Initialize hash table for Class Bitmaps */
   array->cbm_hash = hash_table_create(cbm_hash_f,cbm_cmp_f,CBM_HASH_SIZE);
   assert(array->cbm_hash);

   /* Initialize table for converting ID to CBM */
   array->id2cbm = calloc(nr_elements,sizeof(cbm_array_t *));
   assert(array->id2cbm);

   return(array);
}

/* Free value of cbm_hash */
static void rfc_free_array_cbm_hash_value(void *key,void *value,void *opt_arg)
{
   free(value); // rfc_eqclass_t *
}

/* Free an array for Recursive Flow Classification */
static void rfc_free_array(rfc_array_t *array)
{
   int i;

   assert(array);

   /* Free hash table for Class Bitmaps */
   if (array->cbm_hash) {
      hash_table_foreach(array->cbm_hash, rfc_free_array_cbm_hash_value, array);
      hash_table_delete(array->cbm_hash);
      array->cbm_hash = NULL;
   }

   /* Free table for converting ID to CBM */
   if (array->id2cbm) {
      for (i = 0; i < array->nr_elements; i++) {
         if (array->id2cbm[i])
            free(array->id2cbm[i]);
      }
      free(array->id2cbm);
      array->id2cbm = NULL;
   }

   /* Free array */
   free(array);
}

/* Check an instruction with specified parameter */
static void rfc_check_insn(insn_lookup_t *ilt,cbm_array_t *cbm,
                           ilt_check_cbk_t pcheck,int value)
{
   void *p;
   int i;

   for(i=0;i<ilt->nr_insn;i++) {
      p = ilt->get_insn(i);

      if (pcheck(p,value)) 
         cbm_set_rule(cbm,i);
      else
         cbm_unset_rule(cbm,i);
   }
}

/* RFC Chunk preprocessing: phase 0 */
static rfc_array_t *rfc_phase_0(insn_lookup_t *ilt,ilt_check_cbk_t pcheck)
{
   rfc_eqclass_t *eqcl;
   rfc_array_t *rfct;
   cbm_array_t *bmp;
   int i;

   /* allocate a temporary class bitmap */
   bmp = cbm_create(ilt);
   assert(bmp);

   /* Allocate a new RFC array of 16-bits entries */
   rfct = rfc_alloc_array(RFC_ARRAY_MAXSIZE);
   assert(rfct);

   for(i=0;i<RFC_ARRAY_MAXSIZE;i++)
   {
      /* determine all instructions that match this value */
      rfc_check_insn(ilt,bmp,pcheck,i);

      /* get equivalent class for this bitmap */
      eqcl = cbm_get_eqclass(rfct,bmp);
      assert(eqcl);

      /* fill the RFC table */
      rfct->eqID[i] = eqcl->eqID;
   }

   free(bmp);
   return rfct;
}

/* RFC Chunk preprocessing: phase j (j > 0) */
static rfc_array_t *rfc_phase_j(insn_lookup_t *ilt,rfc_array_t *p0,
                                rfc_array_t *p1)
{
   rfc_eqclass_t *eqcl;
   rfc_array_t *rfct;
   cbm_array_t *bmp;
   int nr_elements;
   int index = 0;
   int i,j;

   /* allocate a temporary class bitmap */
   bmp = cbm_create(ilt);
   assert(bmp);

   /* compute number of elements */
   nr_elements = p0->nr_eqid * p1->nr_eqid;

   /* allocate a new RFC array */
   rfct = rfc_alloc_array(nr_elements);
   assert(rfct);
   rfct->parent0 = p0;
   rfct->parent1 = p1;

   /* make a cross product between p0 and p1 */
   for(i=0;i<p0->nr_eqid;i++)
      for(j=0;j<p1->nr_eqid;j++)
      {
         /* compute bitwise AND */
         cbm_bitwise_and(bmp,p0->id2cbm[i],p1->id2cbm[j]);

         /* get equivalent class for this bitmap */
         eqcl = cbm_get_eqclass(rfct,bmp);
         assert(eqcl);

         /* fill RFC table */
         rfct->eqID[index++] = eqcl->eqID;
      }

   free(bmp);
   return rfct;
}

/* Compute RFC phase 0 */
static void ilt_phase_0(insn_lookup_t *ilt,int idx,ilt_check_cbk_t pcheck)
{
   rfc_array_t *rfct;

   rfct = rfc_phase_0(ilt,pcheck);
   assert(rfct);
   ilt->rfct[idx] = rfct;
}

/* Compute RFC phase j */
static void ilt_phase_j(insn_lookup_t *ilt,int p0,int p1,int res)
{
   rfc_array_t *rfct;

   rfct = rfc_phase_j(ilt,ilt->rfct[p0],ilt->rfct[p1]);
   assert(rfct);
   ilt->rfct[res] = rfct;
}

/* Postprocessing */
static void ilt_postprocessing(insn_lookup_t *ilt)
{
   rfc_array_t *rfct = ilt->rfct[2];
   int i;

   for(i=0;i<rfct->nr_elements;i++)
      rfct->eqID[i] = cbm_first_match(ilt,rfct->id2cbm[rfct->eqID[i]]);
}

/* Instruction lookup table compilation */
static void ilt_compile(insn_lookup_t *ilt)
{  
   ilt_phase_0(ilt,0,ilt->chk_hi);
   ilt_phase_0(ilt,1,ilt->chk_lo);
   ilt_phase_j(ilt,0,1,2);
   ilt_postprocessing(ilt);
}

/* Dump an instruction lookup table */
__unused static int ilt_dump(char *table_name,insn_lookup_t *ilt)
{
   rfc_array_t *rfct;
   char *filename;
   FILE *fd;
   int i,j;
   
   filename = dyn_sprintf("ilt_dump_%s_%s.txt",sw_version_tag,table_name);
   assert(filename != NULL);

   fd = fopen(filename,"w");
   assert(fd != NULL);
   
   fprintf(fd,"ILT %p: nr_insn=%d, cbm_size=%d\n",
         ilt,ilt->nr_insn,ilt->cbm_size);

   for(i=0;i<RFC_ARRAY_NUMBER;i++) {
      rfct = ilt->rfct[i];
      
      fprintf(fd,"RFCT %d: nr_elements=%d, nr_eqid=%d\n",
              i,rfct->nr_elements,rfct->nr_eqid);
      
      for(j=0;j<rfct->nr_elements;j++)
         fprintf(fd,"  (0x%4.4x,0x%4.4x) = 0x%4.4x\n",i,j,rfct->eqID[j]);
   }
   
   fclose(fd);
   free(filename);
   return(0);
}

/* Write the specified RFC array to disk */
static void ilt_store_rfct(FILE *fd,int id,rfc_array_t *rfct)
{
   /* Store RFC array ID + number of elements */
   fwrite(&id,sizeof(id),1,fd);
   fwrite(&rfct->nr_elements,sizeof(rfct->nr_elements),1,fd);
   fwrite(&rfct->nr_eqid,sizeof(rfct->nr_eqid),1,fd);

   fwrite(rfct->eqID,sizeof(int),rfct->nr_elements,fd);
}

/* Write the full instruction lookup table */
static void ilt_store_table(FILE *fd,insn_lookup_t *ilt)
{
   int i;

   for(i=0;i<RFC_ARRAY_NUMBER;i++)
      if (ilt->rfct[i] != NULL)
         ilt_store_rfct(fd,i,ilt->rfct[i]);
}

/* Load an RFC array from disk */
static int ilt_load_rfct(FILE *fd,insn_lookup_t *ilt)
{
   u_int id,nr_elements,nr_eqid;
   rfc_array_t *rfct;
   size_t len;

   /* Read ID and number of elements */
   if ((fread(&id,sizeof(id),1,fd) != 1) ||
       (fread(&nr_elements,sizeof(nr_elements),1,fd) != 1) ||
       (fread(&nr_eqid,sizeof(nr_eqid),1,fd) != 1))
      return(-1);
      
   if ((id >= RFC_ARRAY_NUMBER) || (nr_elements > RFC_ARRAY_MAXSIZE))
      return(-1);

   /* Allocate the RFC array with the eqID table */
   len = sizeof(*rfct) + (nr_elements * sizeof(int));

   if (!(rfct = malloc(len)))
      return(-1);

   memset(rfct,0,sizeof(*rfct));
   rfct->nr_elements = nr_elements;
   rfct->nr_eqid = nr_eqid;
   
   /* Read the equivalent ID array */
   if (fread(rfct->eqID,sizeof(int),nr_elements,fd) != nr_elements) {
      free(rfct);
      return(-1);
   }

   ilt->rfct[id] = rfct;
   return(0);
}

/* Check an instruction table loaded from disk */
static int ilt_check_cached_table(insn_lookup_t *ilt)
{
   int i;

   /* All arrays must have been loaded */
   for(i=0;i<RFC_ARRAY_NUMBER;i++)
      if (!ilt->rfct[i])
         return(-1);

   return(0);
}

/* Load a full instruction table from disk */
static insn_lookup_t *ilt_load_table(FILE *fd)
{
   insn_lookup_t *ilt;
   int i;
   
   if (!(ilt = malloc(sizeof(*ilt))))
      return NULL;

   memset(ilt,0,sizeof(*ilt));
   fseek(fd,0,SEEK_SET);

   for(i=0;i<RFC_ARRAY_NUMBER;i++) {
      if (ilt_load_rfct(fd,ilt) == -1)
         return NULL;
   }

   if (ilt_check_cached_table(ilt) == -1)
      return NULL;

   return ilt;
}

/* Build a filename for a cached ILT table on disk */
static char *ilt_build_filename(char *table_name)
{
   return(dyn_sprintf("ilt_%s_%s",sw_version_tag,table_name));
}

/* Try to load a cached ILT table from disk */
static insn_lookup_t *ilt_cache_load(char *table_name)
{
   insn_lookup_t *ilt;
   char *filename;
   FILE *fd;

   if (!(filename = ilt_build_filename(table_name)))
      return NULL;

   if (!(fd = fopen(filename,"rb"))) {
      free(filename);
      return NULL;
   }

   ilt = ilt_load_table(fd);
   fclose(fd);
   free(filename);
   return ilt;
}

/* Store the specified ILT table on disk for future use (cache) */
static int ilt_cache_store(char *table_name,insn_lookup_t *ilt)
{
   char *filename;
   FILE *fd;

   if (!(filename = ilt_build_filename(table_name)))
      return(-1);

   if (!(fd = fopen(filename,"wb"))) {
      free(filename);
      return(-1);
   }

   ilt_store_table(fd,ilt);
   fclose(fd);
   free(filename);
   return(0);
}

/* Create an instruction lookup table */
insn_lookup_t *ilt_create(char *table_name,
                          int nr_insn,ilt_get_insn_cbk_t get_insn,
                          ilt_check_cbk_t chk_lo,ilt_check_cbk_t chk_hi)
{
   insn_lookup_t *ilt;
   
   /* Try to load a cached table from disk */
   if ((ilt = ilt_cache_load(table_name))) {
      printf("ILT: loaded table \"%s\" from cache.\n",table_name);
      return ilt;
   }

   /* We have to build the full table... */
   ilt = malloc(sizeof(*ilt));
   assert(ilt);
   memset(ilt,0,sizeof(*ilt));

   ilt->cbm_size = normalize_size(nr_insn,CBM_SIZE,CBM_SHIFT);
   ilt->nr_insn  = nr_insn;
   ilt->get_insn = get_insn;
   ilt->chk_lo   = chk_lo;
   ilt->chk_hi   = chk_hi;

   /* Compile the instruction opcodes */
   ilt_compile(ilt);
   
   /* Store the result on disk for future exec */
   ilt_cache_store(table_name,ilt);
   return(ilt);
}

/* Destroy an instruction lookup table */
void ilt_destroy(insn_lookup_t *ilt)
{
   int i;

   assert(ilt);

   /* Free instruction opcodes */
   for (i = 0; i < RFC_ARRAY_NUMBER; i++) {
      if (ilt->rfct[i])
         rfc_free_array(ilt->rfct[i]);
   }

   /* Free instruction lookup table */
   free(ilt);
}
