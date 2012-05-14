#ifndef _ASM_GENERIC_BITOPS___FLS_H_
#define _ASM_GENERIC_BITOPS___FLS_H_

#include <asm/types.h>

/**
 * __fls - find last (most-significant) set bit in a long word
 * @word: the word to search
 *
 * Undefined if no set bit exists, so code should check against 0 first.
 */
/* 최상위->최하위 비트 순으로 1인 비트를 찾는다. */
static __always_inline unsigned long __fls(unsigned long word)
{
	int num = BITS_PER_LONG - 1;

#if BITS_PER_LONG == 64
	if (!(word & (~0ul << 32))) { /* 상위 32비트가 없다면 */
		num -= 32;				  /* 비트 num에서 32를 빼고 */
		word <<= 32;			  /* zero인 32비트도 밀어버린다 */
	}
#endif
	if (!(word & (~0ul << (BITS_PER_LONG-16)))) { /* 더 작은 단위(16)로 최상위 16비트로 올려서 비교.  */
		num -= 16;								  /* 상위 16비트가 없으면 한번에 16을 뺀다 */
		word <<= 16;							  /* 0인 부분도 16비트를 밀어버린다. */
	}
	if (!(word & (~0ul << (BITS_PER_LONG-8)))) {
		num -= 8;
		word <<= 8;
	}
	if (!(word & (~0ul << (BITS_PER_LONG-4)))) {
		num -= 4;
		word <<= 4;
	}
	if (!(word & (~0ul << (BITS_PER_LONG-2)))) {
		num -= 2;
		word <<= 2;
	}
	if (!(word & (~0ul << (BITS_PER_LONG-1))))
		num -= 1;
	return num;					/* 단위를 줄여가며 끝까지 가면 첫번째 1을 알수 있다. */
}

#endif /* _ASM_GENERIC_BITOPS___FLS_H_ */
