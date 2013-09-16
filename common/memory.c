/*
 * Cisco router simulation platform.
 * Copyright (c) 2005,2006 Christophe Fillot (cf@utc.fr)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "cpu.h"
#include "vm.h"
#include "dynamips.h"
#include "memory.h"
#include "device.h"

/* Record a memory access */
void memlog_rec_access(cpu_gen_t *cpu,m_uint64_t vaddr,m_uint64_t data,
                       m_uint32_t op_size,m_uint32_t op_type)
{
   memlog_access_t *acc;

   acc = &cpu->memlog_array[cpu->memlog_pos];
   acc->iaddr   = cpu_get_pc(cpu);
   acc->vaddr   = vaddr;
   acc->data    = data;
   acc->op_size = op_size;
   acc->op_type = op_type;
   acc->data_valid = (op_type == MTS_WRITE);

   cpu->memlog_pos = (cpu->memlog_pos + 1) & (MEMLOG_COUNT - 1);
}

/* Show the latest memory accesses */
void memlog_dump(cpu_gen_t *cpu)
{
   memlog_access_t *acc;
   char s_data[64];
   u_int i,pos;
   
   for(i=0;i<MEMLOG_COUNT;i++) {
      pos = cpu->memlog_pos + i;
      pos &= (MEMLOG_COUNT-1);
      acc = &cpu->memlog_array[pos];

      if (cpu_get_pc(cpu)) {
         if (acc->data_valid)
            snprintf(s_data,sizeof(s_data),"0x%llx",acc->data);
         else
            snprintf(s_data,sizeof(s_data),"XXXXXXXX");

         printf("CPU%u: pc=0x%8.8llx, vaddr=0x%8.8llx, "
                "size=%u, type=%s, data=%s\n",
                cpu->id,acc->iaddr,acc->vaddr,acc->op_size,
                (acc->op_type == MTS_READ) ? "read " : "write",
                s_data);
      }
   }
}

/* Update the data obtained by a read access */
void memlog_update_read(cpu_gen_t *cpu,m_iptr_t raddr)
{
   memlog_access_t *acc;

   acc = &cpu->memlog_array[(cpu->memlog_pos-1) & (MEMLOG_COUNT-1)];

   if (acc->op_type == MTS_READ) 
   {
      switch(acc->op_size) {
         case 1:
            acc->data = *(m_uint8_t *)raddr;
            break;
         case 2:
            acc->data = vmtoh16(*(m_uint16_t *)raddr);
            break;
         case 4:
            acc->data = vmtoh32(*(m_uint32_t *)raddr);
            break;
         case 8:
            acc->data = vmtoh64(*(m_uint64_t *)raddr);
            break;
      }

      acc->data_valid = TRUE;
   }
}


/* === Operations on physical memory ====================================== */

/* Get host pointer for the physical address */
static inline void *physmem_get_hptr(vm_instance_t *vm,m_uint64_t paddr,
                                     u_int op_size,u_int op_type,
                                     m_uint64_t *data)
{
   struct vdevice *dev;
   m_uint32_t offset;
   void *ptr;
   int cow;

   if (!(dev = dev_lookup(vm,paddr,FALSE)))
      return NULL;

   if (dev->flags & VDEVICE_FLAG_SPARSE) {
      ptr = (void *)dev_sparse_get_host_addr(vm,dev,paddr,op_type,&cow);
      if (!ptr) return NULL;

      return(ptr + (paddr & VM_PAGE_IMASK));
   }

   if ((dev->host_addr != 0) && !(dev->flags & VDEVICE_FLAG_NO_MTS_MMAP))
      return((void *)dev->host_addr + (paddr - dev->phys_addr));

   if (op_size == 0)
      return NULL;

   offset = paddr - dev->phys_addr;
   return(dev->handler(vm->boot_cpu,dev,offset,op_size,op_type,data));
}

/* Copy a memory block from VM physical RAM to real host */
void physmem_copy_from_vm(vm_instance_t *vm,void *real_buffer,
                          m_uint64_t paddr,size_t len)
{
   m_uint64_t dummy;
   m_uint32_t r;
   u_char *ptr;

   while(len > 0) {
      r = m_min(VM_PAGE_SIZE - (paddr & VM_PAGE_IMASK), len);
      ptr = physmem_get_hptr(vm,paddr,0,MTS_READ,&dummy);
      
      if (likely(ptr != NULL)) {
         memcpy(real_buffer,ptr,r);
      } else {
         r = m_min(len,4);
         switch(r) {
            case 4:
               *(m_uint32_t *)real_buffer = 
                  htovm32(physmem_copy_u32_from_vm(vm,paddr));
               break;
            case 2:
               *(m_uint16_t *)real_buffer =
                  htovm16(physmem_copy_u16_from_vm(vm,paddr));
               break;
            case 1:
               *(m_uint8_t *)real_buffer = physmem_copy_u8_from_vm(vm,paddr);
               break;
         }
      }

      real_buffer += r;
      paddr += r;
      len -= r;
   }
}

/* Copy a memory block to VM physical RAM from real host */
void physmem_copy_to_vm(vm_instance_t *vm,void *real_buffer,
                        m_uint64_t paddr,size_t len)
{
   m_uint64_t dummy;
   m_uint32_t r;
   u_char *ptr;

   while(len > 0) {
      r = m_min(VM_PAGE_SIZE - (paddr & VM_PAGE_IMASK), len);
      ptr = physmem_get_hptr(vm,paddr,0,MTS_WRITE,&dummy);
      
      if (likely(ptr != NULL)) {
         memcpy(ptr,real_buffer,r);
      } else {
         r = m_min(len,4);
         switch(r) {
            case 4:
               physmem_copy_u32_to_vm(vm,paddr,
                                      htovm32(*(m_uint32_t *)real_buffer));
               break;
            case 2:
               physmem_copy_u16_to_vm(vm,paddr,
                                      htovm16(*(m_uint16_t *)real_buffer));
               break;
            case 1:
               physmem_copy_u8_to_vm(vm,paddr,*(m_uint8_t *)real_buffer);
               break;
         }
      }

      real_buffer += r;
      paddr += r;
      len -= r;
   }
}

/* Copy a 32-bit word from the VM physical RAM to real host */
m_uint32_t physmem_copy_u32_from_vm(vm_instance_t *vm,m_uint64_t paddr)
{
   m_uint64_t tmp = 0;
   m_uint32_t *ptr;

   if ((ptr = physmem_get_hptr(vm,paddr,4,MTS_READ,&tmp)) != NULL)
      return(vmtoh32(*ptr));

   return(tmp);
}

/* Copy a 32-bit word to the VM physical RAM from real host */
void physmem_copy_u32_to_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t val)
{
   m_uint64_t tmp = val;
   m_uint32_t *ptr;

   if ((ptr = physmem_get_hptr(vm,paddr,4,MTS_WRITE,&tmp)) != NULL)
      *ptr = htovm32(val);
}

/* Copy a 16-bit word from the VM physical RAM to real host */
m_uint16_t physmem_copy_u16_from_vm(vm_instance_t *vm,m_uint64_t paddr)
{
   m_uint64_t tmp = 0;
   m_uint16_t *ptr;

   if ((ptr = physmem_get_hptr(vm,paddr,2,MTS_READ,&tmp)) != NULL)
      return(vmtoh16(*ptr));

   return(tmp);
}

/* Copy a 16-bit word to the VM physical RAM from real host */
void physmem_copy_u16_to_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint16_t val)
{
   m_uint64_t tmp = val;
   m_uint16_t *ptr;

   if ((ptr = physmem_get_hptr(vm,paddr,2,MTS_WRITE,&tmp)) != NULL)   
      *ptr = htovm16(val);
}

/* Copy a byte from the VM physical RAM to real host */
m_uint8_t physmem_copy_u8_from_vm(vm_instance_t *vm,m_uint64_t paddr)
{
   m_uint64_t tmp = 0;
   m_uint8_t *ptr;

   if ((ptr = physmem_get_hptr(vm,paddr,1,MTS_READ,&tmp)) != NULL)
      return(*ptr);

   return(tmp);
}

/* Copy a 16-bit word to the VM physical RAM from real host */
void physmem_copy_u8_to_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint8_t val)
{
   m_uint64_t tmp = val;
   m_uint8_t *ptr;

   if ((ptr = physmem_get_hptr(vm,paddr,1,MTS_WRITE,&tmp)) != NULL)   
      *ptr = val;
}

/* DMA transfer operation */
void physmem_dma_transfer(vm_instance_t *vm,m_uint64_t src,m_uint64_t dst,
                          size_t len)
{
   m_uint64_t dummy;
   u_char *sptr,*dptr;
   size_t clen,sl,dl;

   while(len > 0) {
      sptr = physmem_get_hptr(vm,src,0,MTS_READ,&dummy);
      dptr = physmem_get_hptr(vm,dst,0,MTS_WRITE,&dummy);

      if (!sptr || !dptr) {
         vm_log(vm,"DMA","unable to transfer from 0x%llx to 0x%llx\n",src,dst);
         return;
      }

      sl = VM_PAGE_SIZE - (src & VM_PAGE_IMASK);
      dl = VM_PAGE_SIZE - (dst & VM_PAGE_IMASK);
      clen = m_min(sl,dl);
      clen = m_min(clen,len);

      memcpy(dptr,sptr,clen);

      src += clen;
      dst += clen;
      len -= clen;
   }
}

/* strlen in VM physical memory */
size_t physmem_strlen(vm_instance_t *vm,m_uint64_t paddr)
{
   struct vdevice *vm_ram;
   size_t len = 0;
   char *ptr;
   
   if ((vm_ram = dev_lookup(vm,paddr,TRUE)) != NULL) {
      ptr = (char *)vm_ram->host_addr + (paddr - vm_ram->phys_addr);
      len = strlen(ptr);
   }

   return(len);
}

/* Physical memory dump (32-bit words) */
void physmem_dump_vm(vm_instance_t *vm,m_uint64_t paddr,m_uint32_t u32_count)
{
   m_uint32_t i;

   for(i=0;i<u32_count;i++) {
      vm_log(vm,"physmem_dump","0x%8.8llx: 0x%8.8x\n",
             paddr+(i<<2),physmem_copy_u32_from_vm(vm,paddr+(i<<2)));
   }
}
