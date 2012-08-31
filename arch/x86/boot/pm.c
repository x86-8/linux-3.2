/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Prepare the machine for transition to protected mode.
 */

#include "boot.h"
#include <asm/segment.h>

/*
 * Invoke the realmode switch hook if present; otherwise
 * disable all interrupts.
 */
/**
 @brief	Realmode switch hook 사용 여부를 확인하여 사용하면 realmode_hook을 호출하고 아니면 모든 Interrupt 를 Disable 한다.
 */


static void realmode_switch_hook(void)
{
	/* 보호모드로 가기 전에 특정 함수를 호출할수 있다. */
	if (boot_params.hdr.realmode_swtch) {
		asm volatile("lcallw *%0"
			     : : "m" (boot_params.hdr.realmode_swtch)
			     : "eax", "ebx", "ecx", "edx");
	} else {
		/* 보통은 이쪽이다. */
		asm volatile("cli");	// Interrupt Clear.
		outb(0x80, 0x70); /* Disable NMI */	// Non-Maskable Interrupt
		io_delay();
	}
}

/*
 * Disable all interrupts at the legacy PIC.
 */
// pic(IRQ)를 마스크한다.
static void mask_all_interrupts(void)
{
	outb(0xff, 0xa1);	/* Mask all interrupts on the secondary PIC */
	io_delay();
	outb(0xfb, 0x21);	/* Mask all but cascade on the primary PIC */
	io_delay();
}

/*
 * Reset IGNNE# if asserted in the FPU.
 */
// 부동소수점 장치를 clear, reset 한다.
static void reset_coprocessor(void)
{
	outb(0, 0xf0);
	io_delay();
	outb(0, 0xf1);
	io_delay();
}

/*
 * Set up the GDT
 */

struct gdt_ptr {
	u16 len;
	u32 ptr;
} __attribute__((packed));

static void setup_gdt(void)
{
	/* There are machines which are known to not boot with the GDT
	   being 8-byte unaligned.  Intel recommends 16 byte alignment. */
	/* 16비트 segment 단위 (0x10)로 align */
	static const u64 boot_gdt[] __attribute__((aligned(16))) = {
		/* CS: code, read/execute, 4 GB, base 0 */
		/* CS는 2부터 시작한다. DS=3, TSS=4 */
		[GDT_ENTRY_BOOT_CS] = GDT_ENTRY(0xc09b, 0, 0xfffff),
		/* DS: data, read/write, 4 GB, base 0 */
		[GDT_ENTRY_BOOT_DS] = GDT_ENTRY(0xc093, 0, 0xfffff),
		/* TSS: 32-bit tss, 104 bytes, base 4096 */
		/* We only have a TSS here to keep Intel VT happy;
		   we don't actually use it for anything. */
		[GDT_ENTRY_BOOT_TSS] = GDT_ENTRY(0x0089, 4096, 103),
	};
	/* Xen HVM incorrectly stores a pointer to the gdt_ptr, instead
	   of the gdt_ptr contents.  Thus, make it static so it will
	   stay in memory, at least long enough that we switch to the
	   proper kernel GDT. */
	static struct gdt_ptr gdt;
	// gdt까지 세팅끝
	gdt.len = sizeof(boot_gdt)-1;
	gdt.ptr = (u32)&boot_gdt + (ds() << 4); /* 임시 gdt 위치 */

	asm volatile("lgdtl %0" : : "m" (gdt));
}

/*
 * Set up the IDT
 */
// idt레지스터를 0으로 초기화한다.
static void setup_idt(void)
{
	static const struct gdt_ptr null_idt = {0, 0};
	asm volatile("lidtl %0" : : "m" (null_idt));
}

/*
 * Actual invocation sequence
 * 32비트 보호모드로 넘아가기 위한 a20, idt, gdt 설정
 */
void go_to_protected_mode(void)
{
	/* Hook before leaving real mode, also disables interrupts */
	realmode_switch_hook();	/* realmode switch옵션이 on이면 핸들러 호출, 아니면 인터럽트 금지 */

	/* Enable the A20 gate */
	if (enable_a20()) {	/* a20게이트를 켠다. 실패하면 에러출력 & 정지 */
		puts("A20 gate not responding, unable to boot...\n");
		die();
	}

	/* Reset coprocessor (IGNNE#) */
	reset_coprocessor();	/* 부동소수점 장치 reset */

	/* Mask all interrupts in the PIC */
	mask_all_interrupts();	/* pic의 인터럽트까지 금지 */

	/* Actual transition to protected mode... */
	/* idt 초기화 */
	setup_idt();
	/* 보호모드로 가기 전에 임시 gdt 설정 */
	setup_gdt();
	/* 보호모드로 넘어간 후에 갈 곳인 code32_start는
	 * 부트로더가 읽어들인 1M상의 커널 공간이다.
	 * 이동하면 1M이하의 setup 코드는 끝이다.
	 * boot_params는 여기까지 초기화하면서 저장한 파라미터들 & header
	 */
	protected_mode_jump(boot_params.hdr.code32_start,
						(u32)&boot_params + (ds() << 4));
}
