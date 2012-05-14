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
 * Simple command-line parser for early boot.
 */

#include "boot.h"
// C가 공백(0x20)이하면 true를 리턴
static inline int myisspace(u8 c)
{
	return c <= ' ';	/* Close enough approximation */
}

/*
 * Find a non-boolean option, that is, "option=argument".  In accordance
 * with standard Linux practice, if this option is repeated, this returns
 * the last instance on the command line.
 *
 * Returns the length of the argument (regardless of if it was
 * truncated to fit in the buffer), or -1 on not found.
 */
int __cmdline_find_option(u32 cmdline_ptr, const char *option, char *buffer, int bufsize)
{
	addr_t cptr;
	char c;
	int len = -1;
	const char *opptr = NULL;
	char *bufptr = buffer;
	enum {
		st_wordstart,	/* Start of word/after whitespace */
		st_wordcmp,	/* Comparing this word */
		st_wordskip,	/* Miscompare, skip */
		st_bufcpy	/* Copying this to buffer */
	} state = st_wordstart;

	if (!cmdline_ptr || cmdline_ptr >= 0x100000) // cmdline 포인터가 없거나 1M이상이면 에러
		return -1;	/* No command line, or inaccessible */

	cptr = cmdline_ptr & 0xf; // cmdline_ptr의 오프셋
	set_fs(cmdline_ptr >> 4); // cmdline_ptr의 세그먼트 값
 // cmdline에서 옵션을 찾는다.
	while (cptr < 0x10000 && (c = rdfs8(cptr++))) {
		switch (state) {
		case st_wordstart:
			if (myisspace(c)) // 맨 처음 혹은 스페이스(<32)가 여러개 오면 break 해버린다.
				break;

			/* else */
			state = st_wordcmp; // 구분자 후에 문자가 있으면 wordcmp로 상태 변환
			opptr = option; // 찾을 옵션
			/* fall through */
			// 아래로 계속된다.
		case st_wordcmp:
			if (c == '=' && !*opptr) {
				len = 0; // 길이 리셋
				bufptr = buffer;
				state = st_bufcpy; // 일치한다. bufcpy로 상태전환
			} else if (myisspace(c)) {
				state = st_wordstart; // "="이 없다. 다시 비교
			} else if (c != *opptr++) {
				state = st_wordskip; // 일치하지 않음
			}
			break; // 문자가 일치한다. cmp 상태 그대로 다시 비교

		case st_wordskip: // skip은 문자열이 일치하지 않은 상태
			if (myisspace(c)) // 구분자가 오면 새로 비교 시작
				state = st_wordstart;
			break;

		case st_bufcpy:
			if (myisspace(c)) {
				state = st_wordstart; // 구분자가 오기전까지 복사
			} else {
				if (len < bufsize-1)
					*bufptr++ = c; // 버퍼 크기를 넘지 않으면 복사
				len++;
			}
			break;
		}
	}

	if (bufsize)
		*bufptr = '\0'; // 버퍼 끝은 NUL

	return len; // 옵션 길이를 리턴. 일치하는게 없으면 -1
}

/*
 * Find a boolean option (like quiet,noapic,nosmp....)
 *
 * Returns the position of that option (starts counting with 1)
 * or 0 on not found
 */
// 찾으면 위치를 반환하고 못찾으면 0을 반환
int __cmdline_find_option_bool(u32 cmdline_ptr, const char *option)
{
	addr_t cptr;
	char c;
	int pos = 0, wstart = 0;
	const char *opptr = NULL;
	enum {
		st_wordstart,	/* Start of word/after whitespace */
		st_wordcmp,	/* Comparing this word */
		st_wordskip,	/* Miscompare, skip */
	} state = st_wordstart;

	if (!cmdline_ptr || cmdline_ptr >= 0x100000) // command line 포인터가 1M이 넘으면 에러
		return -1;	/* No command line, or inaccessible */

	cptr = cmdline_ptr & 0xf;
	set_fs(cmdline_ptr >> 4); // 세그먼트와 포인터를 분리

	while (cptr < 0x10000) {
		c = rdfs8(cptr++);
		pos++;

		switch (state) {
		case st_wordstart:
			if (!c)
				return 0; // Null이면 리턴
			else if (myisspace(c))
				break;

			state = st_wordcmp;
			opptr = option;
			wstart = pos;
			/* fall through */

		case st_wordcmp:
			if (!*opptr)
				if (!c || myisspace(c))
					return wstart;
				else
					state = st_wordskip;
			else if (!c)
				return 0;
			else if (c != *opptr++)
				state = st_wordskip;
			break;

		case st_wordskip:
			if (!c)
				return 0;
			else if (myisspace(c))
				state = st_wordstart;
			break;
		}
	}

	return 0;	/* Buffer overrun */
}
