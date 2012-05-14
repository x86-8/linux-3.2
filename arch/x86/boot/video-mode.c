/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007-2008 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * arch/i386/boot/video-mode.c
 *
 * Set the video mode.  This is separated out into a different
 * file in order to be shared with the ACPI wakeup code.
 */

#include "boot.h"
#include "video.h"
#include "vesa.h"

/*
 * Common variables
 */
int adapter;			/* 0=CGA/MDA/HGC, 1=EGA, 2=VGA+ */
u16 video_segment;
int force_x, force_y;	/* Don't query the BIOS for cols/rows */

int do_restore;		/* Screen contents changed during mode flip */
int graphic_mode;	/* Graphic mode with linear frame buffer */

/* Probe the video drivers and have them generate their mode lists. */
void probe_cards(int unsafe)
{
	struct card_info *card;
	static u8 probed[2];
	
	/* unsafe(0,1)에 해당하는 것은 한번만 한다. */
	if (probed[unsafe])
		return;

	probed[unsafe] = 1;
	/* 
	 * 비디오 카드 프로필은 __videocard 매크로를 사용해 card_info 구조체를 나타내며
	 * 모두 setup.ld의 .videocards 섹션에 모인다. (bios, vesa, vga)
	 * setup의 비디오 카드 프로필 만큼 탐색한다. (현재 3개)
	 */
	for (card = video_cards; card < video_cards_end; card++) {
		/* unsafe에 해당하는 그래픽 카드만 탐색
		 * unsafe가 0이면 그래픽(vga, vesa), 1이면 bios
		 */
		if (card->unsafe == unsafe) {
			if (card->probe)
				card->nmodes = card->probe(); /* 해당 카드의 probe 함수 호출, 지원하는 모드 수를 반환 */
			else
				card->nmodes = 0;
		}
	}
}

/* Test if a mode is defined */ // 해당 모드가 define 되어있으면 1을 리턴한다.
int mode_defined(u16 mode)
{
	struct card_info *card;
	struct mode_info *mi;
	int i;

	for (card = video_cards; card < video_cards_end; card++) {
		mi = card->modes;
		for (i = 0; i < card->nmodes; i++, mi++) {
			if (mi->mode == mode)
				return 1;
		}
	}

	return 0;
}

/* Set mode (without recalc) */
/**
 @brief
 @return	실패시:-1
 */
static int raw_set_mode(u16 mode, u16 *real_mode)
{
	int nmode, i;
	struct card_info *card;
	struct mode_info *mi;

	/* Drop the recalc bit if set */
	mode &= ~VIDEO_RECALC;

	/* Scan for mode based on fixed ID, position, or resolution */
	nmode = 0;
	for (card = video_cards; card < video_cards_end; card++) {
		mi = card->modes;
		for (i = 0; i < card->nmodes; i++, mi++) {
			int visible = mi->x || mi->y;

			if ((mode == nmode && visible) ||
			    mode == mi->mode ||
			    mode == (mi->y << 8)+mi->x) {
				*real_mode = mi->mode;
				return card->set_mode(mi);
			}

			if (visible)
				nmode++;
		}
	}

	/* Nothing found?  Is it an "exceptional" (unprobed) mode? */
	for (card = video_cards; card < video_cards_end; card++) {
		if (mode >= card->xmode_first &&
		    mode < card->xmode_first+card->xmode_n) {
			struct mode_info mix;
			*real_mode = mix.mode = mode;
			mix.x = mix.y = 0;
			return card->set_mode(&mix);
		}
	}

	/* Otherwise, failure... */
	return -1;
}

/*
 * Recalculate the vertical video cutoff (hack!)
 */
/**
 @brief
 */
static void vga_recalc_vertical(void)
{
	unsigned int font_size, rows;
	u16 crtc;
	u8 pt, ov;

	/*
	 0x485 : Point Height of Size (2Byte)
	 0x484 : Rows on the screen (2Byte)
	 */
	set_fs(0);
	font_size = rdfs8(0x485); /* BIOS: font size (pixels) */
	rows = force_y ? force_y : rdfs8(0x484)+1; /* Text rows */

	rows *= font_size;	/* Visible scan lines */
	rows--;			/* ... minus one */

	crtc = vga_crtc();

	/*
	 CPU 에서 장치에 접근하는 방법은
	 Index Register Address, Data Register Address 을 이용하여 접근한다.
	 Index Register Address 에는 장치에서 관리하는 Register 중, 몇 번째 Register
	 에 접근할지 에 대한 정보(Index)가 입력되며, 1 Byte 의 크기(0 ~ 255) 까지의
	 값이 할당가능하다.
	 Data Register 에는 지정된 Index 번째에 해당하는 데이터가 입력된다.
	 Index Register Address 와 Data Register Address 는 항상 쌍으로 구성되며,
	 Index Register Address + 1 의 주소가 Data Register Address 가 된다
	 */
	pt = in_idx(crtc, 0x11);
	pt &= ~0x80;		/* Unlock CR0-7 */ // 최상위 비트를 끈다.
	out_idx(pt, crtc, 0x11); // 쓰기가능하게 해준다.
//	 11h R- native VGA with bit7=1 in end horizontal blanking (03h):
//	            end vertical retrace
// 11h -W end vertical retrace
//           bit7  : VGA: protection bit
//                =0 enable write access to 00h-07h
//                =1 read only regs 00h-07h with the exception
//                   of bit4 in 07h. ET4000: protect 35h also.
// rows의 아래 8비트를 먼저 써주고 8번 9번 비트를 위치에 맞게 OR해 넣는다.
	out_idx((u8)rows, crtc, 0x12); /* Lower height register */

	ov = in_idx(crtc, 0x07); /* Overflow register */
	ov &= 0xbd; // 1011 1101 
	ov |= (rows >> (8-1)) & 0x02; // bit 8 of vertical display end
	ov |= (rows >> (9-6)) & 0x40; // bit 9 of "
	out_idx(ov, crtc, 0x07);
}

/* Set mode (with recalc if specified) */
int set_mode(u16 mode)
{
	int rv;
	u16 real_mode;

	/* Very special mode numbers... */
	if (mode == VIDEO_CURRENT_MODE)
		return 0;	/* Nothing to do... */
	else if (mode == NORMAL_VGA)
		mode = VIDEO_80x25;
	else if (mode == EXTENDED_VGA)
		mode = VIDEO_8POINT;

	rv = raw_set_mode(mode, &real_mode); // real_mode에는 실제 세팅된 비디오 모드가 온다.
	if (rv)
		return rv;

	if (mode & VIDEO_RECALC)
		vga_recalc_vertical(); // CRT controller에 rows를 넣어준다.

	/* Save the canonical mode number for the kernel, not
	   an alias, size specification or menu position */
#ifndef _WAKEUP
	boot_params.hdr.vid_mode = real_mode;
#endif
	return 0;
}
