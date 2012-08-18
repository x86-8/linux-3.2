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
 * Get the MCA system description table
 */

#include "boot.h"

/**
 @brief	MCA: MircoChannel Architecture http://blog.naver.com/PostView.nhn?blogId=jandark25&logNo=60064229440&redirect=Dlog&widgetTypeCall=true\n
	 MCA: IBM사가 ISA버스의 후속규격으로 정한 버스(컴퓨터 내부의 데이터 전송로)규격.\n
	 boot_params.sys_desc_table 에 테이블 정보가 복사된다.
 @return	성공시:0, 실패시:-1
 */
int query_mca(void)
{
	struct biosregs ireg, oreg;
	u16 len;

	initregs(&ireg);
	ireg.ah = 0xc0;
	intcall(0x15, &ireg, &oreg);
	// INT 15,C0 - Return System Configuration Parameters (PS/2 only)
	// AH = C0
	// on return:
	// CF = 0 if successful
	//   = 1 if error
	// AH = when CF set, 80h for PC & PCjr, 86h for XT
	//	 (BIOS after 11/8/82) and AT (BIOS after 1/10/84)
	// ES:BX = pointer to system descriptor table in ROM of the format:
    // Offset Size          Description
	//   00   word   length of descriptor (8 minimum)
	//   02   byte   model byte (same as F000:FFFE, not reliable)
	//   03   byte   secondary model byte
	//   04   byte   BIOS revision level (zero based)
	//   05   byte   feature information, see below
	//   06   dword  reserved


	if (oreg.eflags & X86_EFLAGS_CF)
		return -1;	/* No MCA present */

	set_fs(oreg.es);
	len = rdfs16(oreg.bx);
	// len = es:bx

	if (len > sizeof(boot_params.sys_desc_table))
		len = sizeof(boot_params.sys_desc_table);

	copy_from_fs(&boot_params.sys_desc_table, oreg.bx, len);
	return 0;
}
