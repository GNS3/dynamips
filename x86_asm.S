/* 
 * Cisco 7200 Simulator.
 * X86 Assembly-optimized routines.
 *
 * Copyright (c) 2006 Christophe Fillot (cf@utc.fr)
 */

#define DYNAMIPS_ASM
	
#include "asmdefs.h"
#include "memory.h"

#if defined(CYGWIN) || defined(__APPLE__)
#define _P(f) _##f
#else
#define _P(f) f
#endif
	
/*
 * Increment the count register. When value in compare register is hit,
 * trigger the timer interrupt.
 */
.globl _P(mips64_inc_cp0_cnt_asm)
_P(mips64_inc_cp0_cnt_asm):
	movl	CP0_VCNT_OFS(%edi), %ecx
	incl	%ecx
	movl	%ecx, CP0_VCNT_OFS(%edi)
	cmpl	%ecx, CP0_VCMP_OFS(%edi)
	jne	1f
	movl	%edi, %eax
	call	_P(mips64_trigger_timer_irq)
1:
	ret

/* 
 * MTS32 Load Word (LW) fast version.
 *
 * Inputs:
 *   %edi      : cpu instance
 *   %ebx      : target register
 *   %edx      : virtual address (%ecx is high 32-bit word)
 */
.globl _P(mts32_lw_asm)
_P(mts32_lw_asm):
	pushl	%esi
	movl	$((1 << (MTS32_LEVEL2_BITS + MTS32_OFFSET_BITS)) - 1), %ecx
	
	/* compute L1 pos */
	movl	%edx, %eax
	shr	$(MTS32_LEVEL2_BITS + MTS32_OFFSET_BITS), %eax
	movl	MTS_L1_OFS(%edi), %esi
	movl	(%esi,%eax,4), %esi
	
	/* %esi = L1 entry */
	movl	%esi, %eax
	andl	$MTS_ACC_MASK, %eax
	jnz	mts32_lw_asm_spec_acc

	/* L2 entry chained ? */
	movl	%esi, %eax
	andl	$MTS_CHAIN_MASK, %eax
	jz	1f

	/* load L2 entry */
	andl	$0xfffffff0, %esi
	movl	%edx, %eax
	shr	$MTS32_OFFSET_BITS, %eax
	andl    $((1 << MTS32_LEVEL2_BITS) - 1), %eax
	movl	$((1 << MTS32_OFFSET_BITS) - 1), %ecx
	movl	(%esi,%eax,4), %esi

	/* %esi = L2 entry */
	movl	%esi, %eax
	andl	$MTS_ACC_MASK, %eax
	jnz	mts32_lw_asm_spec_acc
	
1:
	/* device access ? */
	movl	%esi, %eax
	andl	$MTS_DEV_MASK, %eax
	jnz	mts32_lw_asm_dev_acc

	/* raw memory access */
	andl	$0xfffffff0, %esi
	andl	%edx, %ecx
	addl	%ecx, %esi

mts32_lw_asm_load_val:
	/* %esi = host address */
	movl	(%esi), %eax
	bswap	%eax
	cdq

	/* %edx:%eax = sign-extended value */
	lea	CPU_GPR_OFS(%edi,%ebx,8), %esi
	movl	%eax, (%esi)
	movl	%edx, 4(%esi)
		
	popl	%esi
	xorl	%eax, %eax
	ret

mts32_lw_asm_dev_acc:
	subl	$8, %esp
	movl	%esp, %eax

	pushl	%eax       /* data */
	pushl	$MTS_READ  /* op_type = read */
	pushl	$4         /* op_size = 4 bytes */
	
	/* %esi = entry, %ecx = shift, %edx = vaddr */
	movl	%esi, %eax
	andl	$MTS_DEVID_MASK, %eax
	shr	$MTS_DEVID_SHIFT, %eax
	andl	%ecx, %edx
	andl	$MTS_DEVOFF_MASK, %esi
	addl	%edx, %esi
	
	pushl	%esi       /* haddr */
	pushl	%eax       /* dev_id */
	pushl	%edi       /* cpu */

	/* call device access routine */	
	call	_P(dev_access)
	addl	$32, %esp

	/* %eax = haddr if raw access */
	movl	%eax, %esi
	testl	%esi ,%esi
	jnz	mts32_lw_asm_load_val

	movl	-8(%esp), %eax
	cdq
	lea	CPU_GPR_OFS(%edi,%ebx,8), %esi
	movl	%eax, (%esi)
	movl	%edx, 4(%esi)
	
	popl	%esi
	xorl	%eax, %eax
	ret
	
mts32_lw_asm_spec_acc:
	/* %eax = mask */
	subl	$12, %esp
	movl	%esp, %ecx

	movl	$0, (%esp)      /* clear exception */
	
	pushl	%ecx            /* exception */
	addl	$4, %ecx
	pushl	%ecx            /* data */

	pushl	$4              /* op_size = 4 */
	pushl	$MTS_READ       /* op_type = read */
	pushl	$MIPS_MEMOP_LW  /* op_code = LW */
	pushl	%eax            /* mask */
	
	pushl	%edx            /* vaddr(lo) */
	movl	%edx, %eax
	cdq
	pushl	%edx            /* vaddr(hi) */
	pushl	%edi            /* cpu */
	call	_P(mts_access_special)
	addl	$((9*4)+12), %esp

	/* exception ? */
	movl	-12(%esp), %eax
	testl	%eax, %eax
	jnz	mts32_lw_asm_end
	
	/* save data */
	movl	-8(%esp), %eax
	cdq
	lea	CPU_GPR_OFS(%edi,%ebx,8), %esi
	movl	%eax, (%esi)
	movl	%edx, 4(%esi)
	xorl	%eax, %eax
mts32_lw_asm_end:	
	popl	%esi
	ret

/* 
 * MTS64 Load Word (LW) fast version.
 *
 * Inputs:
 *   %edi      : cpu instance
 *   %ebx      : target register
 *   %ecx:edx  : virtual address
 */
.globl _P(mts64_lw_asm)
_P(mts64_lw_asm):
	/* Load entry from MTS64 cache */
	movl	%edx, %eax
	shr	$MTS64_HASH_SHIFT, %edx
	andl	$MTS64_HASH_MASK, %edx
	lea	CPU_MTS64_CACHE_OFS(%edi,%edx,4), %esi

	/* %esi = entry pointer */
	test	%esi, %esi
	jz	mts64_lw_slow_lookup

	/* Load entry start address in %eax */
	lea	(MTS64_ENTRY_START_OFS+4)(%esi), %eax
	
	ret
	
mts64_lw_device_access:		
	ret
	
mts64_lw_slow_lookup:
	
	ret
