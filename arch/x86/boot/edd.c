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
 * Get EDD BIOS disk information
 */

#include "boot.h"
#include <linux/edd.h>

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)

/*
 * Read the MBR (first sector) from a specific device. MBR을 읽어온다. 에러나면 -1
 */
static int read_mbr(u8 devno, void *buf)
{
	struct biosregs ireg, oreg;

	initregs(&ireg);
	ireg.ax = 0x0201;		/* Legacy Read, one sector */
	ireg.cx = 0x0001;		/* Sector 0-0-1 */
	ireg.dl = devno;
	ireg.bx = (size_t)buf;

	intcall(0x13, &ireg, &oreg);

	return -(oreg.eflags & X86_EFLAGS_CF); /* 0 or -1 */
}
// disk signature를 반환하고 magic number(0xaa55)를 체크해서 맞으면 0을 리턴한다. (-1이면 에러)
static u32 read_mbr_sig(u8 devno, struct edd_info *ei, u32 *mbrsig)
{
	int sector_size;
	char *mbrbuf_ptr, *mbrbuf_end;
	u32 buf_base, mbr_base;
	extern char _end[];
	u16 mbr_magic;

	sector_size = ei->params.bytes_per_sector;
	if (!sector_size)
		sector_size = 512; /* Best available guess */

	/* Produce a naturally aligned buffer on the heap */
	buf_base = (ds() << 4) + (u32)&_end;
	mbr_base = (buf_base+sector_size-1) & ~(sector_size-1);
	mbrbuf_ptr = _end + (mbr_base-buf_base);
	mbrbuf_end = mbrbuf_ptr + sector_size;

	/* Make sure we actually have space on the heap... */
	if (!(boot_params.hdr.loadflags & CAN_USE_HEAP))
		return -1;
	if (mbrbuf_end > (char *)(size_t)boot_params.hdr.heap_end_ptr)
		return -1;

	memset(mbrbuf_ptr, 0, sector_size);
	if (read_mbr(devno, mbrbuf_ptr))
		return -1;

	*mbrsig = *(u32 *)&mbrbuf_ptr[EDD_MBR_SIG_OFFSET];
	mbr_magic = *(u16 *)&mbrbuf_ptr[510];

	/* check for valid MBR magic */
	return mbr_magic == 0xAA55 ? 0 : -1;
}


// EDD를 지원하는지 체크하고 디스크 정보를 읽어온다.
static int get_edd_info(u8 devno, struct edd_info *ei)
{
	struct biosregs ireg, oreg;

	memset(ei, 0, sizeof *ei);

	/* Check Extensions Present */

	initregs(&ireg);
	ireg.ah = 0x41;
	ireg.bx = EDDMAGIC1; 
	ireg.dl = devno;
	intcall(0x13, &ireg, &oreg);

	if (oreg.eflags & X86_EFLAGS_CF)
		return -1;	/* No extended information */

	if (oreg.bx != EDDMAGIC2)
		return -1;

	ei->device  = devno;
	ei->version = oreg.ah;		 /* EDD version number */
	ei->interface_support = oreg.cx; /* EDD functionality subsets */

	/* Extended Get Device Parameters */

	ei->params.length = sizeof(ei->params);
	ireg.ah = 0x48;
	ireg.si = (size_t)&ei->params;
	intcall(0x13, &ireg, &oreg);

	/* Get legacy CHS parameters */

	/* Ralf Brown recommends setting ES:DI to 0:0 */
	ireg.ah = 0x08;
	ireg.es = 0;
	intcall(0x13, &ireg, &oreg);

	if (!(oreg.eflags & X86_EFLAGS_CF)) {
		ei->legacy_max_cylinder = oreg.ch + ((oreg.cl & 0xc0) << 2);
		ei->legacy_max_head = oreg.dh;
		ei->legacy_sectors_per_track = oreg.cl & 0x3f;
	}

	return 0;
}

/**
 @brief 
  edd= 파라미터를 파싱하고 디스크의 정보(CHS,EDD)를 얻어와서 eddbuf에 넣고 디스크마다 MBR의 volumID(signature)를 얻고 magic number를 체크한다.
 */
void query_edd(void)
{
	char eddarg[8];
	int do_mbr = 1;
#ifdef CONFIG_EDD_OFF
	int do_edd = 0;
#else
	int do_edd = 1;
#endif
	int be_quiet;
	int devno;
	struct edd_info ei, *edp;
	u32 *mbrptr;

	if (cmdline_find_option("edd", eddarg, sizeof eddarg) > 0) {
		// check options
		// edd=skipmbr
		// edd=skip
		if (!strcmp(eddarg, "skipmbr") || !strcmp(eddarg, "skip")) {
			do_edd = 1;
			do_mbr = 0;
		}

		// check options
		// edd=off
		else if (!strcmp(eddarg, "off"))
			do_edd = 0;

		// check options
		// edd=on
		else if (!strcmp(eddarg, "on"))
			do_edd = 1;
	}

	be_quiet = cmdline_find_option_bool("quiet");

	edp    = boot_params.eddbuf;
	mbrptr = boot_params.edd_mbr_sig_buffer;

	if (!do_edd)
		return;

	/* Bugs in OnBoard or AddOnCards Bios may hang the EDD probe,
	 * so give a hint if this happens.
	 */

	if (!be_quiet)
		printf("Probing EDD (edd=off to disable)... ");

	
	for (devno = 0x80; devno < 0x80+EDD_MBR_SIG_MAX; devno++) { // 16
		/*
		 * Scan the BIOS-supported hard disks and query EDD
		 * information...
		 */
		if (!get_edd_info(devno, &ei)
		    && boot_params.eddbuf_entries < EDDMAXNR) {
			memcpy(edp, &ei, sizeof ei);
			edp++;
			boot_params.eddbuf_entries++;
		}

		if (do_mbr && !read_mbr_sig(devno, &ei, mbrptr++)) // mbr을 스킵하지 않고 magic number가 맞으면 실행
		  boot_params.edd_mbr_sig_buf_entries = devno-0x80+1; // 마지막부팅가능한 하드 (1부터시작)
	}

	if (!be_quiet)
		printf("ok\n");
}

#endif
