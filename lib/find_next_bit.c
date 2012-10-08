/* find_next_bit.c: fallback find next bit implementation
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
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

#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)

#ifndef find_next_bit
/*
 * Find the next set bit in a memory region.
 */
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset)
{
	const unsigned long *p = addr + BITOP_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG-1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset %= BITS_PER_LONG;
	if (offset) {
		tmp = *(p++);
		tmp &= (~0UL << offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG-1)) {
		if ((tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp &= (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size;	/* Nope. */
found_middle:
	return result + __ffs(tmp);
}
EXPORT_SYMBOL(find_next_bit);
#endif

#ifndef find_next_zero_bit
/*
 * This implementation of find_{first,next}_zero_bit was stolen from
 * Linus' asm-alpha/bitops.h.
 */
/**
 * 이 함수는 0인 비트를 찾는다.
 * ffz등과 다른 점은 어떤 연속된 비트 배열도 offset으로 반복해서 사용할 수 있다는 점이다.
 * addr는 시작 주소, size는 검사할 비트 크기, offset는 검사할 시작 비트 오프셋이다.
 * result는 비트 번호가 결과값이다.
 */
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
				 unsigned long offset)
{
	/* offset */
	/* long 단위의 offset(시작지점) */
	const unsigned long *p = addr + BITOP_WORD(offset);
	/* result = 최종 결과값으로, offset이 long 의 크기(64)를 넘어설 경우,
	 * 정렬 처리 되어, index로서 표현될 수 있음(0, 64, 128, 192, ...) */
	unsigned long result = offset & ~(BITS_PER_LONG-1);
	unsigned long tmp;

	/* offset비트가 최대 비트를 넘어서면 최대(size)비트 수를 반환 */
	if (offset >= size)
		return size;
	/* size비트는 검출할(남아있는) 비트 수가 되므로, result를 빼주어
	 * 크기를 줄임. */
	size -= result;
	offset %= BITS_PER_LONG;
	/* 시작 offset값이 있으면 */
	if (offset) {
		/* 값을 읽어와서 */
		tmp = *(p++);
		/* 값 데이터에 offset..0 비트 까지 1로 채우는데,
		 * 나중에 not 연산으로 zero비트 검출을 용이하게 만듦. */
		tmp |= ~0UL >> (BITS_PER_LONG - offset);
		/* size가 64비트보다 작다면, zero 비트가 있는지 검출하지 않고, 
		 * zero bit 인덱스를 구함. */
		if (size < BITS_PER_LONG)
			goto found_first;
		/* size가 64비트보다 큰데도(검출할게 남아있음), 0이 있다는 것은
		 * 중간에 0이 있다는 것이 됨. */
		if (~tmp)
			goto found_middle;
		/* 0 비트 없이 모두 1로 차있다면 다음 검색 */
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	/* 검출은 64비트씩 하기 때문에, 남은 size역시 64비트씩 검사하여, 
	 * zero bit검출 시도 */
	while (size & ~(BITS_PER_LONG-1)) {
		/* 0이 포함되어 있다면 middle로 간다. */
		if (~(tmp = *(p++)))
			goto found_middle;
		/* 다음 값으로 long만큼 비트 수 증감 */
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	/* 남은 size(검출할 비트수)가 없으므로, 현재 bit index인 result를 반환.
	 * result의 값은 초기 인자인 최대(size)비트 수와 같음. */
	if (!size)
		return result;
	/* size가 남아있으므로, tmp 데이터를 갱신하여, zero bit 검출.
	 * 이때의 size는 64보다 작다 */
	tmp = *p;

found_first:
	/* found_first 이전에 offset)..0비트까지 1로 채웠는데,
	 * 64..size만큼을 다시 1로 채워넣음, */
	tmp |= ~0UL << size;
	 /* 0이 없다면, size가 찾는 zero bit(가장 마지막 bit = size번째) */
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size;	/* Nope. */
	 /* 0이 있다면, ffz로 중간의 zero bit검출함. 
	  * (모두 1로 채워져서 검출 용이). */
found_middle:
	/* 0 비트를 검출해서 리턴한다. 0번비트부터 상위비트로 진행된다. */
	return result + ffz(tmp);
}
EXPORT_SYMBOL(find_next_zero_bit);
#endif

#ifndef find_first_bit
/*
 * Find the first set bit in a memory region.
 */
unsigned long find_first_bit(const unsigned long *addr, unsigned long size)
{
	const unsigned long *p = addr;
	unsigned long result = 0;
	unsigned long tmp;

	while (size & ~(BITS_PER_LONG-1)) {
		if ((tmp = *(p++)))
			goto found;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;

	tmp = (*p) & (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size;	/* Nope. */
found:
	return result + __ffs(tmp);
}
EXPORT_SYMBOL(find_first_bit);
#endif

#ifndef find_first_zero_bit
/*
 * Find the first cleared bit in a memory region.
 */
unsigned long find_first_zero_bit(const unsigned long *addr, unsigned long size)
{
	const unsigned long *p = addr;
	unsigned long result = 0;
	unsigned long tmp;

	while (size & ~(BITS_PER_LONG-1)) {
		if (~(tmp = *(p++)))
			goto found;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;

	tmp = (*p) | (~0UL << size);
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size;	/* Nope. */
found:
	return result + ffz(tmp);
}
EXPORT_SYMBOL(find_first_zero_bit);
#endif

#ifdef __BIG_ENDIAN

/* include/linux/byteorder does not support "unsigned long" type */
static inline unsigned long ext2_swabp(const unsigned long * x)
{
#if BITS_PER_LONG == 64
	return (unsigned long) __swab64p((u64 *) x);
#elif BITS_PER_LONG == 32
	return (unsigned long) __swab32p((u32 *) x);
#else
#error BITS_PER_LONG not defined
#endif
}

/* include/linux/byteorder doesn't support "unsigned long" type */
static inline unsigned long ext2_swab(const unsigned long y)
{
#if BITS_PER_LONG == 64
	return (unsigned long) __swab64((u64) y);
#elif BITS_PER_LONG == 32
	return (unsigned long) __swab32((u32) y);
#else
#error BITS_PER_LONG not defined
#endif
}

#ifndef find_next_zero_bit_le
unsigned long find_next_zero_bit_le(const void *addr, unsigned
		long size, unsigned long offset)
{
	const unsigned long *p = addr;
	unsigned long result = offset & ~(BITS_PER_LONG - 1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	p += BITOP_WORD(offset);
	size -= result;
	offset &= (BITS_PER_LONG - 1UL);
	if (offset) {
		tmp = ext2_swabp(p++);
		tmp |= (~0UL >> (BITS_PER_LONG - offset));
		if (size < BITS_PER_LONG)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}

	while (size & ~(BITS_PER_LONG - 1)) {
		if (~(tmp = *(p++)))
			goto found_middle_swap;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = ext2_swabp(p);
found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size; /* Nope. Skip ffz */
found_middle:
	return result + ffz(tmp);

found_middle_swap:
	return result + ffz(ext2_swab(tmp));
}
EXPORT_SYMBOL(find_next_zero_bit_le);
#endif

#ifndef find_next_bit_le
unsigned long find_next_bit_le(const void *addr, unsigned
		long size, unsigned long offset)
{
	const unsigned long *p = addr;
	unsigned long result = offset & ~(BITS_PER_LONG - 1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	p += BITOP_WORD(offset);
	size -= result;
	offset &= (BITS_PER_LONG - 1UL);
	if (offset) {
		tmp = ext2_swabp(p++);
		tmp &= (~0UL << offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}

	while (size & ~(BITS_PER_LONG - 1)) {
		tmp = *(p++);
		if (tmp)
			goto found_middle_swap;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = ext2_swabp(p);
found_first:
	tmp &= (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size; /* Nope. */
found_middle:
	return result + __ffs(tmp);

found_middle_swap:
	return result + __ffs(ext2_swab(tmp));
}
EXPORT_SYMBOL(find_next_bit_le);
#endif

#endif /* __BIG_ENDIAN */
