/*
 * Cisco 7200 (Predator) simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __MEMORY_H__
#define __MEMORY_H__

#ifndef DYNAMIPS_ASM
#include <sys/types.h>
#include "utils.h"
#endif

/* MTS operation */
#define MTS_READ  0
#define MTS_WRITE 1

/* 0.5GB value */
#define MTS_SIZE_512M     0x20000000

/* MTS flag bits: D (device), ACC (memory access), C (chain) */
#define MTS_FLAG_BITS     4
#define MTS_FLAG_MASK     0x0000000fUL

/* Masks for MTS entries */
#define MTS_CHAIN_MASK    0x00000001
#define MTS_ACC_MASK      0x00000006
#define MTS_DEV_MASK      0x00000008
#define MTS_ADDR_MASK     (~MTS_FLAG_MASK)

/* Device ID mask and shift, device offset mask */
#define MTS_DEVID_MASK    0xfc000000
#define MTS_DEVID_SHIFT   26
#define MTS_DEVOFF_MASK   0x03fffff0

/* Memory access flags */
#define MTS_ACC_OK  0x00000000   /* Access OK */
#define MTS_ACC_AE  0x00000002   /* Address Error */
#define MTS_ACC_T   0x00000004   /* TLB Exception */
#define MTS_ACC_U   0x00000006   /* Unexistent */

/* 32-bit Virtual Address seen by MTS */
#define MTS32_LEVEL1_BITS  10
#define MTS32_LEVEL2_BITS  10
#define MTS32_OFFSET_BITS  12

/* Each level-1 entry covers 4 Mb */
#define MTS32_LEVEL1_SIZE  (1 << (MTS32_LEVEL2_BITS + MTS32_OFFSET_BITS))
#define MTS32_LEVEL1_MASK  (MTS32_LEVEL1_SIZE - 1)

/* Each level-2 entry covers 4 Kb */
#define MTS32_LEVEL2_SIZE  (1 << MTS32_OFFSET_BITS)
#define MTS32_LEVEL2_MASK  (MTS32_LEVEL2_SIZE - 1)

/* Hash table size for MTS64 (default: [shift:16,bits:12]) */
#define MTS64_HASH_SHIFT   15
#define MTS64_HASH_BITS    15
#define MTS64_HASH_SIZE    (1 << MTS64_HASH_BITS)
#define MTS64_HASH_MASK    (MTS64_HASH_SIZE - 1)

/* MTS64 hash on virtual addresses */
#define MTS64_HASH(vaddr) (((vaddr) >> MTS64_HASH_SHIFT) & MTS64_HASH_MASK)

/* Number of entries per chunk */
#define MTS64_CHUNK_SIZE   256

#ifndef DYNAMIPS_ASM
/* MTS32: Level 1 & 2 arrays */
typedef struct mts32_l1_array mts32_l1_array_t;
struct mts32_l1_array {
   m_iptr_t entry[1 << MTS32_LEVEL1_BITS];
};

typedef struct mts32_l2_array mts32_l2_array_t;
struct mts32_l2_array {
   m_iptr_t entry[1 << MTS32_LEVEL2_BITS];
   mts32_l2_array_t *next;
};

/* MTS64: chunk definition */
struct mts64_chunk {
   mts64_entry_t entry[MTS64_CHUNK_SIZE];
   struct mts64_chunk *next;
   u_int count;
};

/* Show the last memory accesses */
void memlog_dump(cpu_mips_t *cpu);

/* Allocate an L1 array */
mts32_l1_array_t *mts32_alloc_l1_array(m_iptr_t val);

/* Allocate an L2 array */
mts32_l2_array_t *mts32_alloc_l2_array(cpu_mips_t *cpu,m_iptr_t val);

/* Initialize an empty MTS32 subsystem */
int mts32_init_empty(cpu_mips_t *cpu);

/* Free memory used by MTS32 */
void mts32_shutdown(cpu_mips_t *cpu);

/* Map a physical address to the specified virtual address */
void mts32_map(cpu_mips_t *cpu,m_uint64_t vaddr,
               m_uint64_t paddr,m_uint32_t len,
               int cache_access);

/* Unmap a memory zone */
void mts32_unmap(cpu_mips_t *cpu,m_uint64_t vaddr,m_uint32_t len,
                 m_uint32_t val);

/* Map all devices for kernel mode */
void mts32_km_map_all_dev(cpu_mips_t *cpu);

/* Initialize the MTS64 subsystem for the specified CPU */
int mts64_init(cpu_mips_t *cpu);

/* Free memory used by MTS64 */
void mts64_shutdown(cpu_mips_t *cpu);

/* Show MTS64 detailed information (debugging only!) */
void mts64_show_stats(cpu_mips_t *cpu);

/* Initialize memory access vectors */
void mts_init_memop_vectors(cpu_mips_t *cpu);

/* Shutdown MTS subsystem */
void mts_shutdown(cpu_mips_t *cpu);

/* Copy a memory block from VM physical RAM to real host */
void physmem_copy_from_vm(vm_instance_t *vm,void *real_buffer,
                          m_uint64_t paddr,size_t len);

/* Copy a memory block to VM physical RAM from real host */
void physmem_copy_to_vm(vm_instance_t *vm,void *real_buffer,
                        m_uint64_t paddr,size_t len);

/* Copy a 32-bit word from the VM physical RAM to real host */
m_uint32_t physmem_copy_u32_from_vm(vm_instance_t *vm,m_uint64_t paddr);

/* Copy a 32-bit word to the VM physical RAM from real host */
void physmem_copy_u32_to_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t val);

/* Copy a 16-bit word from the VM physical RAM to real host */
m_uint16_t physmem_copy_u16_from_vm(vm_instance_t *vm,m_uint64_t paddr);

/* Copy a 16-bit word to the VM physical RAM from real host */
void physmem_copy_u16_to_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint16_t val);

/* DMA transfer operation */
void physmem_dma_transfer(vm_instance_t *vm,m_uint64_t src,m_uint64_t dst,
                          size_t len);

/* strlen in VM physical memory */
size_t physmem_strlen(vm_instance_t *vm,m_uint64_t paddr);

/* Physical memory dump (32-bit words) */
void physmem_dump_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t u32_count);

#endif /* DYNAMIPS_ASM */

#endif
