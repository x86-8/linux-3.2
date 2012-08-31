/* ----------------------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * The actual transition into protected mode
 */

#include <asm/boot.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>
#include <linux/linkage.h>

	.text
	.code16

/*
 * void protected_mode_jump(u32 entrypoint, u32 bootparams);
 */
/* 이 소스는 보호모드로 넘어가기 위한 어셈 코드다.
 * eax에는 점프할 위치가 들어있다. bzImage일때 1M(0x100000)
 */
GLOBAL(protected_mode_jump)
	/* boot parameter 주소 */
	movl	%edx, %esi		# Pointer to boot_params table
	xorl	%ebx, %ebx		# 상위 16비트를 0
	movw	%cs, %bx
	shll	$4, %ebx		# cs의 오프셋
/* long jump를 위해 현재 세그먼트를 <<4해서
 * 오프셋으로 바꾸어 점프할 주소에 더한다. (상대주소)
 */
	addl	%ebx, 2f
	jmp	1f			# Short jump to serialize on 386/486
1:

	movw	$__BOOT_DS, %cx		# gdt내의 주소
	movw	$__BOOT_TSS, %di

	movl	%cr0, %edx
	orb	$X86_CR0_PE, %dl	# Protected mode # 보호모드를 켠다.
	movl	%edx, %cr0

	# Transition to 32-bit mode
/*
 * 66은 operand prefix다.
 * 32비트 주소로 넘어가기 위한 점프코드(0xea)와 오퍼랜드(in_pm32)가 붙는다.
 */
	.byte	0x66, 0xea		# ljmpl opcode
2:	.long	in_pm32			# offset
	.word	__BOOT_CS		# segment
ENDPROC(protected_mode_jump)

	.code32
	.section ".text32","ax"
GLOBAL(in_pm32)
	# Set up data segments for flat 32-bit mode
	/*
	 * __BOOT_DS (ds 세그먼트가 위치한 gdt상의 오프셋)
	 * 세그먼트들을 0으로 초기화 / flat은 선형으로 이어진 메모리를 말한다.
	 */
	movl	%ecx, %ds
	movl	%ecx, %es
	movl	%ecx, %fs
	movl	%ecx, %gs
	movl	%ecx, %ss
	# The 32-bit code sets up its own stack, but this way we do have
	# a valid stack if some debugging hack wants to use it.
	/* 스택 += (cs<<4) */
	addl	%ebx, %esp

	/* 보호모드에서 intel vt를 위해 완전한 초기화를 해준다. (ldt, tr) */
	# Set up TR to make Intel VT happy
	ltr	%di

	# Clear registers to allow for future extensions to the
	# 32-bit boot protocol
	xorl	%ecx, %ecx
	xorl	%edx, %edx
	xorl	%ebx, %ebx
	xorl	%ebp, %ebp
	xorl	%edi, %edi

	# Set up LDTR to make Intel VT happy
	# LDT 0을 select (아마도 NULL selector)
	lldt	%cx

	# Jump to the 32-bit entrypoint
	/* 커널 본체가 로드된 1M상의 코드로 점프한다. (hdr.code32_start) */
	jmpl	*%eax
ENDPROC(in_pm32)
