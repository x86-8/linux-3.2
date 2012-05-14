/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   Original APM BIOS checking by Stephen Rothwell, May 1994
 *   (sfr@canb.auug.org.au)
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Get APM BIOS information
 */

#include "boot.h"

/**
 @brief	APM 설정 여부를 확인하여, 만약 APM(Advanced Power Management) 구성을 사용한다면,
	 그 설정을 boot_params.apm_bios_info 에 입력한다.\n
	 만약, APM 구성을 사용하지 않는다면 실패로 간주한다.
 @return	성공시:0, 실패시:-1
 */
int query_apm_bios(void)
{
	struct biosregs ireg, oreg;

	/* APM BIOS installation check */
	initregs(&ireg);
	ireg.ah = 0x53;
	intcall(0x15, &ireg, &oreg);
	// 출처: http://www.delorie.com/djgpp/doc/rbinter/id/01/14.html
	//
	// INT 15 - Advanced Power Management v1.0+ - INSTALLATION CHECK
	//
	//	AX = 5300h
	//	BX = device ID of system BIOS (0000h)
	// Return: CF clear if successful
	//	    AH = major version (BCD)
	//	    AL = minor version (BCD)
	//	    BX = 504Dh ("PM")
	//	    CX = flags
	//			Bitfields for APM flags:
	//			Bit(s)	Description	)
	//			 0	16-bit protected mode interface supported
	//			 1	32-bit protected mode interface supported
	//			 2	CPU idle call reduces processor speed
	//			 3	BIOS power management disabled
	//			 4	BIOS power management disengaged (APM v1.1)
	//			 5-7	reserved
	//	CF set on error
	//	    AH = error code (06h,09h,86h)

	if (oreg.flags & X86_EFLAGS_CF)
		return -1;		/* No APM BIOS */

	if (oreg.bx != 0x504d)		/* "PM" signature */
		return -1;

	if (!(oreg.cx & 0x02))		/* 32 bits supported? */
		return -1;

	/* Disconnect first, just in case */
	ireg.al = 0x04;
	intcall(0x15, &ireg, NULL);
	// INT 15 - Advanced Power Management v1.0+ - DISCONNECT INTERFACE
	//
	//	AX = 5304h
	//	BX = device ID of system BIOS (0000h)
	// Return: CF clear if successful
	//	CF set on error
	//	    AH = error code (03h,09h) (see #00473)

	/* 32-bit connect */
	ireg.al = 0x03;
	intcall(0x15, &ireg, &oreg);
	// INT 15 - Advanced Power Management v1.0+ - CONNECT 32-BIT PROTMODE INTERFACE
	//
	//	AX = 5303h
	//	BX = device ID of system BIOS (0000h)
	// Return: CF clear if successful
//		    AX = real-mode segment base address of protected-mode 32-bit code
//			segment
//		    EBX = offset of entry point
//		    CX = real-mode segment base address of protected-mode 16-bit code
//			segment
//		    DX = real-mode segment base address of protected-mode 16-bit data
//			segment
//		    ---APM v1.1---
//		    SI = APM BIOS code segment length
//		    DI = APM BIOS data segment length
//		CF set on error
//		    AH = error code (02h,05h,07h,08h,09h)

	boot_params.apm_bios_info.cseg        = oreg.ax;
	boot_params.apm_bios_info.offset      = oreg.ebx;
	boot_params.apm_bios_info.cseg_16     = oreg.cx;
	boot_params.apm_bios_info.dseg        = oreg.dx;
	boot_params.apm_bios_info.cseg_len    = oreg.si;
	boot_params.apm_bios_info.cseg_16_len = oreg.hsi;
	boot_params.apm_bios_info.dseg_len    = oreg.di;

	if (oreg.flags & X86_EFLAGS_CF)
		return -1;

	/* Redo the installation check as the 32-bit connect;
	   some BIOSes return different flags this way... */

	ireg.al = 0x00;
	intcall(0x15, &ireg, &oreg);

	if ((oreg.eflags & X86_EFLAGS_CF) || oreg.bx != 0x504d) {
		/* Failure with 32-bit connect, try to disconect and ignore */
		ireg.al = 0x04;
		intcall(0x15, &ireg, NULL);
		return -1;
	}

	boot_params.apm_bios_info.version = oreg.ax;
	boot_params.apm_bios_info.flags   = oreg.cx;
	return 0;
}

