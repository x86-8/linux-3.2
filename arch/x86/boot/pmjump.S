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
# eax에는 점프할 위치 (bzImage일때는 0x100000 (1M))
GLOBAL(protected_mode_jump)
	movl	%edx, %esi		# Pointer to boot_params table # boot parameter 주소
	xorl	%ebx, %ebx # 상위 16비트를 0
	movw	%cs, %bx
	shll	$4, %ebx # cs의 오프셋
	addl	%ebx, 2f # long jump를 위해 현재 세그먼트를 <<4해서 오프셋으로 바꾸어 점프할 주소에 더한다. (상대주소)
	jmp	1f			# Short jump to serialize on 386/486
1:

	movw	$__BOOT_DS, %cx # gdt내의 주소
	movw	$__BOOT_TSS, %di

	movl	%cr0, %edx
	orb	$X86_CR0_PE, %dl	# Protected mode # 보호모드를 켠다.
	movl	%edx, %cr0

	# Transition to 32-bit mode
	.byte	0x66, 0xea		# ljmpl opcode # 66은 operand prefix다. 32비트 주소로 넘어가기 위한 점프코드와 오퍼랜드가 붙는다.
2:	.long	in_pm32			# offset
	.word	__BOOT_CS		# segment
ENDPROC(protected_mode_jump)

	.code32
	.section ".text32","ax"
GLOBAL(in_pm32)
	# Set up data segments for flat 32-bit mode # 세그먼트들을 0으로 초기화 / flat은 선형으로 이어진 메모리를 말한다.
	movl	%ecx, %ds /* __BOOT_DS (ds 세그먼트가 위치한 gdt상의 오프셋) */
	movl	%ecx, %es
	movl	%ecx, %fs
	movl	%ecx, %gs
	movl	%ecx, %ss
	# The 32-bit code sets up its own stack, but this way we do have
	# a valid stack if some debugging hack wants to use it.
	addl	%ebx, %esp /* 스택 += (cs<<4) */
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

	# Set up LDTR to make Intel VT happy # LDT를 0으로 만든다.
	lldt	%cx
		;; eax는 hdr.code32_start 이 코드가 끝나고 제어를 넘길 곳(커널본체가 로드된곳)이 들어있다.
	jmpl	*%eax			# Jump to the 32-bit entrypoint # setup를 끝내고 커널로 점프한다.
ENDPROC(in_pm32)
