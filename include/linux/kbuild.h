#ifndef __LINUX_KBUILD_H
#define __LINUX_KBUILD_H

// DEFINE으로 변환하면
// #define 심볼명 멤버주소 /* 멤버이름 */

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

// OFFSET 매크로는 sym, str, mem을 받아서 str 구조체의 mem 멤버의 주소를 sym으로 정의한다.
#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem))

#define COMMENT(x) \
	asm volatile("\n->#" x)

#endif
