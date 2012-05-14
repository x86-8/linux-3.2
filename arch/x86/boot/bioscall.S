/* -----------------------------------------------------------------------
 *
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2 or (at your
 *   option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * "Glove box" for BIOS calls.  Avoids the constant problems with BIOSes
 * touching registers they shouldn't be.
 */

 /**
 @brief	void intcall(u8 int_no, const struct biosregs *ireg, struct biosregs *oreg);
 	ireg 값들을 레지스터에 Set 하고,
 	oreg
 */

	.code16gcc
	.text
	.globl	intcall
	.type	intcall, @function	/* @function : fastcall */
intcall:
	/* Self-modify the INT instruction.  Ugly, but works. */
	/* 첫번째 인자 : ax, 두번째 인자:dx, 세번째 인자:cx */
	/* 컴파일러 마다 사용하는 레지스터가 다름... 주의 */
	cmpb	%al, 3f		/* al: INT 번호. Compare with 3f and al. al:Register(8bit) */
	je	1f				/* */
	movb	%al, 3f		/* 3f에 al 값을 넣어준다. */
	jmp	1f				/* Synchronize pipeline */
1:
	/* Save state */
	pushfl			/* pushfl : push flags: 4byte */
	pushw	%fs		/* push word : fs(세그먼트 레지스터): 2byte */
	pushw	%gs		/* gs: 세그먼트 레지스터: 2byte */
	pushal			/* push All 범용 레지스터(ax. bx. cx, dx, si, di, bp, sp): 각각 4byte */

	/* Copy input state to stack frame */
	/* 인자 값 저장을 위한 공간(최대 파라미터 크기:44Byte) 확보 후, */
	/* 확보된 공간에 함수호출 때 넘겨진 인자값들을 복사한다. */
	subw	$44, %sp
	movw	%dx, %si
	movw	%sp, %di
	movw	$11, %cx
	rep; movsd

	/* Pop full state from the stack */
	popal
	popw	%gs
	popw	%fs
	popw	%es
	popw	%ds
	popfl

	/* Actual INT */
	.byte	0xcd		/* INT opcode */
3:	.byte	0

	/* Push full state to the stack */
	pushfl
	pushw	%ds
	pushw	%es
	pushw	%fs
	pushw	%gs
	pushal

	/* Re-establish C environment invariants */
	cld					/* Clear Direction flag */
	movzwl	%sp, %esp
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es

	/* Copy output state from stack frame */
	movw	68(%esp), %di	/* Original %cx == 3rd argument */
	andw	%di, %di		/* CX(3번째 인자)의 유무를 확인한다. */
	jz	4f					/* CX(3번째 인자)가 없으면 4: 로 점프 */
	movw	%sp, %si
	movw	$11, %cx
	rep; movsd
4:	addw	$44, %sp

	/* Restore state and return */
	popal
	popw	%gs
	popw	%fs
	popfl
	retl
	.size	intcall, .-intcall
