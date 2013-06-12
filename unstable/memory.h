/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#ifndef __MEMORY_H__
#define __MEMORY_H__

#include <sys/types.h>
#include "utils.h"

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
#define MTS_DEVOFF_MASK   0x03ffffff

/* Memory access flags */
#define MTS_ACC_AE  0x00000002   /* Address Error */
#define MTS_ACC_T   0x00000004   /* TLB Exception */
#define MTS_ACC_U   0x00000006   /* Unexistent */

/* Macro for easy hash computing */
#define MTS_SHR(v,sr) ((v) >> (sr))

/* Hash table size for MTS64 (default: [shift:16,bits:12]) */
#define MTS64_HASH_SHIFT1  12
#define MTS64_HASH_SHIFT2  20
#define MTS64_HASH_BITS    8
#define MTS64_HASH_SIZE    (1 << MTS64_HASH_BITS)
#define MTS64_HASH_MASK    (MTS64_HASH_SIZE - 1)

/* MTS64 hash on virtual addresses */
#define MTS64_SHR(v,i)    (MTS_SHR((v),MTS64_HASH_SHIFT##i))
#define MTS64_HASH(vaddr) ((MTS64_SHR(vaddr,1) ^ MTS64_SHR(vaddr,2)) & MTS64_HASH_MASK)

/* Hash table size for MTS32 (default: [shift:15,bits:15]) */
#define MTS32_HASH_SHIFT1  12
#define MTS32_HASH_SHIFT2  20
#define MTS32_HASH_BITS    8
#define MTS32_HASH_SIZE    (1 << MTS32_HASH_BITS)
#define MTS32_HASH_MASK    (MTS32_HASH_SIZE - 1)

/* MTS32 hash on virtual addresses */
#define MTS32_SHR(v,i)    (MTS_SHR((v),MTS32_HASH_SHIFT##i))
#define MTS32_HASH(vaddr) ((MTS32_SHR(vaddr,1) ^ MTS32_SHR(vaddr,2)) & MTS32_HASH_MASK)

/* Number of entries per chunk */
#define MTS64_CHUNK_SIZE   256
#define MTS32_CHUNK_SIZE   256

/* MTS64: chunk definition */
struct mts64_chunk {
   mts64_entry_t entry[MTS64_CHUNK_SIZE];
   struct mts64_chunk *next;
   u_int count;
};

/* MTS32: chunk definition */
struct mts32_chunk {
   mts32_entry_t entry[MTS32_CHUNK_SIZE];
   struct mts32_chunk *next;
   u_int count;
};

/* Record a memory access */
void memlog_rec_access(cpu_gen_t *cpu,m_uint64_t vaddr,m_uint64_t data,
                       m_uint32_t op_size,m_uint32_t op_type);

/* Show the last memory accesses */
void memlog_dump(cpu_gen_t *cpu);

/* Update the data obtained by a read access */
void memlog_update_read(cpu_gen_t *cpu,m_iptr_t raddr);

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

/* Copy a byte from the VM physical RAM to real host */
m_uint8_t physmem_copy_u8_from_vm(vm_instance_t *vm,m_uint64_t paddr);

/* Copy a 16-bit word to the VM physical RAM from real host */
void physmem_copy_u8_to_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint8_t val);

/* DMA transfer operation */
void physmem_dma_transfer(vm_instance_t *vm,m_uint64_t src,m_uint64_t dst,
                          size_t len);

/* strlen in VM physical memory */
size_t physmem_strlen(vm_instance_t *vm,m_uint64_t paddr);

/* Physical memory dump (32-bit words) */
void physmem_dump_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t u32_count);

#endif
