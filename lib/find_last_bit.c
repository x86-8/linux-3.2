/* find_last_bit.c: fallback find next bit implementation
 *
 * Copyright (C) 2008 IBM Corporation
 * Written by Rusty Russell <rusty@rustcorp.com.au>
 * (Inspired by David Howell's find_next_bit implementation)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <asm/types.h>
#include <asm/byteorder.h>

#ifndef find_last_bit
/* addr은 비트 배열, size는 비트 크기
 * find_last_bit는 __fls의 word버전이라 할 수 있다.
 * 긴 비트열에서 set(=1) 된 비트 번호를 리턴한다.
 */
unsigned long find_last_bit(const unsigned long *addr, unsigned long size)
{
	unsigned long words;
	unsigned long tmp;

	/* Start at final word. */
	/* 마지막 long word 번호 */
	words = size / BITS_PER_LONG;

	/* Partial final word? */
	/* long bits수로 나누어 떨어지지 않는 조각난 부분이 있으면 
	 * 그 부분에 1인 비트가 있는지 mask 해본다. 있으면 found
	 */
	if (size & (BITS_PER_LONG-1)) {
		tmp = (addr[words] & (~0UL >> (BITS_PER_LONG /* 마지막 word 와 비트 조각 개수만큼 mask (하위n비트) */
					 - (size & (BITS_PER_LONG-1)))));
		if (tmp)
			goto found;
	}

	while (words) {
		tmp = addr[--words];
		if (tmp) {				/* 이 word에 1인 비트가 있는가? */
found:
			return words * BITS_PER_LONG + __fls(tmp); /* 앞선 words 비트수 + 1인 비트 번호 */
		}
	}

	/* Not found */
	return size;
}
EXPORT_SYMBOL(find_last_bit);

#endif
