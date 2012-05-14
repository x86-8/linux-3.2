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
 * VESA text modes
 */

#include "boot.h"
#include "video.h"
#include "vesa.h"

/* VESA information */
static struct vesa_general_info vginfo;		///< General VESA Graphic Mode 정보
static struct vesa_mode_info vminfo;		///< VESA Graphic Mode  정보를 포함한다.

static __videocard video_vesa;

#ifndef _WAKEUP
static void vesa_store_mode_params_graphics(void);
#else /* _WAKEUP */
static inline void vesa_store_mode_params_graphics(void) {}
#endif /* _WAKEUP */

static int vesa_probe(void)
{
	struct biosregs ireg, oreg;
	u16 mode;
	addr_t mode_ptr;
	struct mode_info *mi;
	int nmodes = 0;

	video_vesa.modes = GET_HEAP(struct mode_info, 0); // 정렬만 한다.

	initregs(&ireg);
	ireg.ax = 0x4f00;
	ireg.di = (size_t)&vginfo;
	intcall(0x10, &ireg, &oreg);

	if (oreg.ax != 0x004f ||
	    vginfo.signature != VESA_MAGIC ||
	    vginfo.version < 0x0102)
	  return 0;	/* Not present */ // SuperVGA 지원안하면 리턴

	set_fs(vginfo.video_mode_ptr.seg);
	mode_ptr = vginfo.video_mode_ptr.off;

	while ((mode = rdfs16(mode_ptr)) != 0xffff) { // 0xffff가 끝, 지원하는 모드들의 주소
		mode_ptr += 2;

		if (!heap_free(sizeof(struct mode_info))) // heap영역에 남은 공간이 있는지 확인한다.
			break;	/* Heap full, can't save mode info */ 

		if (mode & ~0x1ff) // 0x1ff를 초과하면 continue.
			continue;
		// 1~0x1ff값이 온다.
		memset(&vminfo, 0, sizeof vminfo); /* Just in case... */
		// 각각의 비디오 모드에 대한 자세한 정보를 얻는다.
		ireg.ax = 0x4f01;
		ireg.cx = mode;
		ireg.di = (size_t)&vminfo;
		intcall(0x10, &ireg, &oreg);

		if (oreg.ax != 0x004f) // al에 4f가 오면 지원함, ah==0 success
			continue;
		// 자세한 정보를 넣어준다.
		if ((vminfo.mode_attr & 0x15) == 0x05) {
			/* Text Mode, TTY BIOS supported,
			   supported by hardware */
			mi = GET_HEAP(struct mode_info, 1);
			mi->mode  = mode + VIDEO_FIRST_VESA;
			mi->depth = 0; /* text */
			mi->x     = vminfo.h_res;
			mi->y     = vminfo.v_res;
			nmodes++;
		} else if ((vminfo.mode_attr & 0x99) == 0x99 &&
			   (vminfo.memory_layout == 4 ||
			    vminfo.memory_layout == 6) &&
			   vminfo.memory_planes == 1) {
#ifdef CONFIG_FB_BOOT_VESA_SUPPORT
			/* Graphics mode, color, linear frame buffer
			   supported.  Only register the mode if
			   if framebuffer is configured, however,
			   otherwise the user will be left without a screen. */
			mi = GET_HEAP(struct mode_info, 1);
			mi->mode = mode + VIDEO_FIRST_VESA;
			mi->depth = vminfo.bpp;
			mi->x = vminfo.h_res;
			mi->y = vminfo.v_res;
			nmodes++;
#endif
		}
	}

	return nmodes;
}

/**
 @brief	VESA 관련된 값 확인 후, 확인된 값들을 boot_params 에 입력한다.
 @return	성공시:-1, 실패시: 0
 */
static int vesa_set_mode(
		struct mode_info *mode
		)
{
	struct biosregs ireg, oreg;
	int is_graphic;		// Graphic Mode : 1, Text Mode : 0
	u16 vesa_mode = mode->mode - VIDEO_FIRST_VESA; // - 0x200

	memset(&vminfo, 0, sizeof vminfo); /* Just in case... */
	// vesa vga 정보를 얻어온다.
	initregs(&ireg);
	ireg.ax = 0x4f01;
	ireg.cx = vesa_mode;
	ireg.di = (size_t)&vminfo;
	intcall(0x10, &ireg, &oreg);

	if (oreg.ax != 0x004f)
		return -1;

	// 프레임 버퍼가 가능하면 vesa_mode를 바꾸고 아니면 vesa_mode는 인자로 받은 mode
	if ((vminfo.mode_attr & 0x15) == 0x05) { // 하위 4비트중 하드웨어에서 지원하는 모드 & bios 출력 지원 이 켜있는가?
		/* It's a supported text mode */
		is_graphic = 0;
#ifdef CONFIG_FB_BOOT_VESA_SUPPORT // 커널에서 VESA 옵션이 켜있으면
	} else if ((vminfo.mode_attr & 0x99) == 0x99) { // 프레임 버퍼를 세팅할수 있는 비트들이 켜있으면
		/* It's a graphics mode with linear frame buffer */
		is_graphic = 1;
		vesa_mode |= 0x4000; /* Request linear frame buffer */
#endif
	} else {
		return -1;	/* Invalid mode */
	}

	/*
	 ireg.ax 4f = VESA
	 ireg.ax 02 = Video Mode Set
	 intcall 0x10 : Video Function
	 */
	// 비디오 모드 세팅
	initregs(&ireg);
	ireg.ax = 0x4f02;
	ireg.bx = vesa_mode;
	intcall(0x10, &ireg, &oreg);

	if (oreg.ax != 0x004f)
		return -1;

	graphic_mode = is_graphic;
	if (!is_graphic) {
	  /* Text mode */ // 일반 text 모드
		force_x = mode->x;
		force_y = mode->y;
		do_restore = 1;
	} else {
		/* Graphics mode */
		vesa_store_mode_params_graphics();
	}

	return 0;
}


#ifndef _WAKEUP

/* Switch DAC to 8-bit mode */
/**
 @brief	DAC -> 8bit mode
 */
static void vesa_dac_set_8bits(void)
{
	struct biosregs ireg, oreg;
	u8 dac_size = 6;

	/* If possible, switch the DAC to 8-bit mode */
	if (vginfo.capabilities & 1) {
		initregs(&ireg);
		ireg.ax = 0x4f08;
		ireg.bh = 0x08;
		intcall(0x10, &ireg, &oreg);
		if (oreg.ax == 0x004f)
			dac_size = oreg.bh;
	}

	/* Set the color sizes to the DAC size, and offsets to 0 */
	boot_params.screen_info.red_size   = dac_size;
	boot_params.screen_info.green_size = dac_size;
	boot_params.screen_info.blue_size  = dac_size;
	boot_params.screen_info.rsvd_size  = dac_size;

	boot_params.screen_info.red_pos    = 0;
	boot_params.screen_info.green_pos  = 0;
	boot_params.screen_info.blue_pos   = 0;
	boot_params.screen_info.rsvd_pos   = 0;
}

/* Save the VESA protected mode info */
/**
 @brief	Protected Mode 에서 호출 가능한
 	 VESA 관련 Code 를 boot_params 에 설정한다.
 */
static void vesa_store_pm_info(void)
{
	struct biosregs ireg, oreg;

	/*
	 intcall 0x10 : Video Interrupt
	 ireg.ax=0x4f0a : VESA
	 */
	initregs(&ireg);
	ireg.ax = 0x4f0a;
	intcall(0x10, &ireg, &oreg);

	if (oreg.ax != 0x004f)	// 실패시..
		return;

	boot_params.screen_info.vesapm_seg = oreg.es;
	boot_params.screen_info.vesapm_off = oreg.di;
}

/*
 * Save video mode parameters for graphics mode
 */
/**
 @brief	vesa_probe 로 확인된 모드 설정값을 boot_prarams 전역 구조체에 입력한다.
 */
static void vesa_store_mode_params_graphics(void)
{
	/* Tell the kernel we're in VESA graphics mode */
	boot_params.screen_info.orig_video_isVGA = VIDEO_TYPE_VLFB;

	/* Mode parameters */
	boot_params.screen_info.vesa_attributes = vminfo.mode_attr;
	boot_params.screen_info.lfb_linelength = vminfo.logical_scan;
	boot_params.screen_info.lfb_width = vminfo.h_res;
	boot_params.screen_info.lfb_height = vminfo.v_res;
	boot_params.screen_info.lfb_depth = vminfo.bpp;
	boot_params.screen_info.pages = vminfo.image_planes;
	boot_params.screen_info.lfb_base = vminfo.lfb_ptr;
	memcpy(&boot_params.screen_info.red_size,
	       &vminfo.rmask, 8);

	/* General parameters */
	boot_params.screen_info.lfb_size = vginfo.total_memory;

	if (vminfo.bpp <= 8)
		vesa_dac_set_8bits();

	vesa_store_pm_info();
}

/*
 * Save EDID information for the kernel; this is invoked, separately,
 * after mode-setting.
 */
void vesa_store_edid(void)
{
#ifdef CONFIG_FIRMWARE_EDID
	struct biosregs ireg, oreg;

	/* Apparently used as a nonsense token... */
	memset(&boot_params.edid_info, 0x13, sizeof boot_params.edid_info);

	if (vginfo.version < 0x0200)
		return;		/* EDID requires VBE 2.0+ */

	initregs(&ireg);
	ireg.ax = 0x4f15;		/* VBE DDC */
	/* ireg.bx = 0x0000; */		/* Report DDC capabilities */
	/* ireg.cx = 0;	*/		/* Controller 0 */
	ireg.es = 0;			/* ES:DI must be 0 by spec */
	intcall(0x10, &ireg, &oreg);
// 0x4f15 함수의 지원 여부를 테스트한다.
	if (oreg.ax != 0x004f)
		return;		/* No EDID */

	/* BH = time in seconds to transfer EDD information */
	/* BL = DDC level supported */
// EDID 정보를 얻어온다.
	ireg.ax = 0x4f15;		/* VBE DDC */
	ireg.bx = 0x0001;		/* Read EDID */
	/* ireg.cx = 0; */		/* Controller 0 */
	/* ireg.dx = 0;	*/		/* EDID block number */
	ireg.es = ds();
	ireg.di =(size_t)&boot_params.edid_info; /* (ES:)Pointer to block */
	intcall(0x10, &ireg, &oreg);
#endif /* CONFIG_FIRMWARE_EDID */
}

#endif /* not _WAKEUP */

static __videocard video_vesa =
{
	.card_name	= "VESA",
	.probe		= vesa_probe,
	.set_mode	= vesa_set_mode,
	.xmode_first	= VIDEO_FIRST_VESA,
	.xmode_n	= 0x200,
};
