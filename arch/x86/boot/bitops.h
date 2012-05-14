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
 * Very simple bitops for the boot code.
 */

#ifndef BOOT_BITOPS_H
#define BOOT_BITOPS_H
#define _LINUX_BITOPS_H		/* Inhibit inclusion of <linux/bitops.h> */

/**
 @brief 특정 Register(혹은 Register 그룹) 의 시작 주소 *addr 에서 부터 nr(Index)\n
	 번째에 위치한 bit 가 Set 되어 있는지를 확인한다
 @return Set 되어 있을 경우:0, Set 안되어 있을 경우:1
 */
static inline int
constant_test_bit(
		int nr,				///< 확인하고자 하는 bit 가 위치한 Index
		const void *addr	///< 확인하고자 하는 Register들의 시작 주소
		)
{
	const u32 *p = (const u32 *)addr;
	// 1UL = unsigned long type 의 정수 '1'을 나타낸다.
	//
	return ((1UL << (nr & 31)) & (p[nr >> 5])) != 0;
}


/**
 @brief 특정 Register(혹은 Register 그룹) 의 시작 주소 *addr 에서 부터 nr(Index)
	 번째에 위치한 bit 가 Set 되어 있는지를 확인한다
 @return Set 되어 있을 경우:0, Set 안되어 있을 경우:1
 */
static inline int
variable_test_bit(
		int nr, 			///< 확인하고자 하는 bit 가 위치한 Index
		const void *addr	///< 확인하고자 하는 Register들의 시작 주소
		)
{
	u8 v;
	const u32 *p = (const u32 *)addr;

	// btl : 검사하고자 하는 bit의 값으로, carry flag를 Set 한다.
	// setc: carry flag 가 On(1)/Off(0)를 리턴한다.
	asm("btl %2,%1; setc %0"
			: "=qm" (v)
			: "m" (*p), "Ir" (nr)
			);
	return v;
}

/**
 @brief nr의 상수 여부를 확인 후,
	 상수이면 constant_test_bit(nr, addr)
	 비상수이면 variable_test_bit(nr, addr) 을 수행한다.
	 addr 주소에서 nr 번째의 bit의 Set 여부를 리턴한다.
 @return Set 되어 있을 시:0, Set 안되어 있을 시:1
 */
// __builtin_constant_p(x) : x가 상수이면 1, 아니면 0
#define	test_bit(nr,addr) \
(__builtin_constant_p(nr) ? \
 constant_test_bit((nr),(addr)) : \
 variable_test_bit((nr),(addr)))


/**
 @brief	*addr 에 해당하는 Register 의 nr 번째(index) 의 값을 1로 Set 한다.
	 연산 종료 후, Set 이전의 *addr Register의 nr 번째(Index) 값은
	 해당 Register으 CF Flag로 복사 된다.
 */
static inline void
set_bit(
		int nr, 	//< 변경하고자 하는 bit가 위치한 레지스터의 Index 값(비트번호)
		void *addr	//< 변경하고자 하는 레지스터
		)
{
	// btsl: src 비트를 1로 Set 한다.
	// carry bit 로 src bit 를 복사하고 src 비트는 1로 set한다.
	asm("btsl %1,%0"
			: "+m" (*(u32 *)addr)	// Output
			: "Ir" (nr)				// Input
			);
}

#endif /* BOOT_BITOPS_H */
