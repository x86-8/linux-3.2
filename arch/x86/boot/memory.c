/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Memory detection code
 */

#include "boot.h"

#define SMAP	0x534d4150	/* ASCII "SMAP" */


/**
 @brief	e820 Interrupt 를 통한 현재 Memory map 구성을 확인하고 e820 정보를 e820_map에 입력한다.
 @return	성공시: 확인된 memory map 구성 갯수, 실패시:0
 */
static int detect_memory_e820(void)
{
	int count = 0;
	struct biosregs ireg, oreg;
	struct e820entry *desc = boot_params.e820_map;
	static struct e820entry buf; /* static so it is zeroed */

	initregs(&ireg);
	ireg.ax  = 0xe820;		// 0xE820
	// 0xE820 : returns a memory map of all the installed RAM,
	// and of physical memory ranges reserved by the BIOS.
	// http://www.uruk.org/orig-grub/mem64mb.html
	ireg.cx  = sizeof buf;
	ireg.edx = SMAP;	/* 필요한 signature */
	ireg.di  = (size_t)&buf;

	/*
	 * Note: at least one BIOS is known which assumes that the
	 * buffer pointed to by one e820 call is the same one as
	 * the previous call, and only changes modified fields.  Therefore,
	 * we use a temporary buffer and copy the results entry by entry.
	 *
	 * This routine deliberately does not try to account for
	 * ACPI 3+ extended attributes.  This is because there are
	 * BIOSes in the field which report zero for the valid bit for
	 * all ranges, and we don't currently make any use of the
	 * other attribute bits.  Revisit this if we see the extended
	 * attribute bits deployed in a meaningful way in the future.
	 */

	do {
		intcall(0x15, &ireg, &oreg);
		// 0x15: ax(e820)
		// &ireg : Input Register
		// &oreg : Ouput Register
		ireg.ebx = oreg.ebx; /* for next iteration... */
		// ebx : Offset 주소.
		// Input 의 경우 시작 Offset, Output의 경우 다음 Offset.
		// 만약 Offset이 0 이면 끝을 의미한다.

		/* BIOSes which terminate the chain with CF = 1 as opposed
		   to %ebx = 0 don't always report the SMAP signature on
		   the final, failing, probe. */
		if (oreg.eflags & X86_EFLAGS_CF)	// Error 발생시, CF Flag가 Set이 된다.
			break;

		/* Some BIOSes stop returning SMAP in the middle of
		   the search loop.  We don't know exactly how the BIOS
		   screwed up the map at that point, we might have a
		   partial map, the full map, or complete garbage, so
		   just return failure. */
		// 정상적인 검색 루프에서는 Return 값으로 oreg.eax 에 SMAP 값이 입력되어야 한다.
		// 일부 BIOS 에서는 다른 값들이 올 수도 있지만 SMAP 이 아닌 경우,
		// 모두 Error 로 간주한다.
		if (oreg.eax != SMAP) {
			count = 0;
			break;
		}

		*desc++ = buf; 	/* e820 엔트리를 e820_map에 입력 */
		count++;
		// ireg.ebx = Next Search Memory Offset
	} while (ireg.ebx && count < ARRAY_SIZE(boot_params.e820_map));

	return boot_params.e820_entries = count; /* 총 갯수 */
}

/**
 @brief	0xe801 Interrupt 명령을 통한 Memory map을 확인한다.\n
	 확인된 Memory map 정보는 boot_params 에 저장된다.\n
	 확인하는 사항은 전체 메모리 맵 구성의 크기이다.\n
	 확인 가능한 최대 메모리 크기는 4G + 16M 이다.\n
	 최대 확인 가능한 메모리 크기까지만 표현한다.\n
	 결과값은 boot_params.alt_mem_k 에 저장된다.
 @return	성공시:0, 실패시:-1
 */
static int detect_memory_e801(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ax = 0xe801;
	// 0xe801 : 0xe820(2002), 0xe801(1994) 2002 년 이전의 CPU는 e801
	// 명령으로만 Memory map 을 확인할 수 있으므로...
	intcall(0x15, &ireg, &oreg);
	// 연산 결과
	// ax : Kbyte 단위의 1 ~ 15 * 1024 사이의 크기 값.(표현가능한 크기: 1Kbyte ~ 15 Mbyte)
	// bx : 64KByte 단위의 크기값. (최대 4GB)

	if (oreg.eflags & X86_EFLAGS_CF)
		return -1;

	/* Do we really need to do this? */
	if (oreg.cx || oreg.dx) {
		oreg.ax = oreg.cx;
		oreg.bx = oreg.dx;
	}

	if (oreg.ax > 15*1024) {
		return -1;	/* Bogus! */
	} else if (oreg.ax == 15*1024) {
		boot_params.alt_mem_k = (oreg.bx << 6) + oreg.ax;
	} else {
		/*
		 * This ignores memory above 16MB if we have a memory
		 * hole there.  If someone actually finds a machine
		 * with a memory hole at 16MB and no support for
		 * 0E820h they should probably generate a fake e820
		 * map.
		 */
		boot_params.alt_mem_k = oreg.ax;
	}

	return 0;
}

/**
 @brief	0x88 Interrupt 명령으로 메모리\n
	 1Kbyte 단위의 메모리 크기를 확인한다.\n
	 확인 가능한 최대 메모리 크기는 64M + 1M 이다.\n
	 확인된 결과값은 boot_params.screen_info.ext_mem_k 에 저장된다.
 @return	성공시:0, 실패시:-1
 */
static int detect_memory_88(void)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ah = 0x88;
	intcall(0x15, &ireg, &oreg);

	boot_params.screen_info.ext_mem_k = oreg.ax;

	return -(oreg.eflags & X86_EFLAGS_CF); /* 0 or -1 */
}


/*
 @brief	Memory Map 확인을 한다.\n
	 e820, e801, 88 Interrupt 명령을 통해서 memory map 확인을 한다.\n
	 확인된 메모리 정보는 boot_params 에 저장되며, 세가지 명령 중 하나라도 성공한다면\n
	 성공으로 간주한다.
 @return	성공시:0, 실패시:-1
 */
int detect_memory(void)
{
	int err = -1;

	if (detect_memory_e820() > 0)
		err = 0;

	if (!detect_memory_e801())
		err = 0;

	if (!detect_memory_88())
		err = 0;

	return err;
}
