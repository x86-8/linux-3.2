#ifndef _ASM_X86_BUG_H
#define _ASM_X86_BUG_H

#ifdef CONFIG_BUG
#define HAVE_ARCH_BUG

#ifdef CONFIG_DEBUG_BUGVERBOSE

#ifdef CONFIG_X86_32
# define __BUG_C0	"2:\t.long 1b, %c0\n"
#else
# define __BUG_C0	"2:\t.long 1b - 2b, %c0 - 2b\n"
#endif

/* UD2는 undefined instruction */
/* http://sourceware.org/binutils/docs/as/PushSection.html */
/* 정의되지 않은 인스트럭션은 6번 invalid opcode exception - IRQ를 발생시킨다. */
/* 아마도 디버깅을 위해 파일과 라인 정보를 __bug_table에 남기는것 같다. */
/* __FILE__과 __LINE__은 현재 파일과 라인 */
/* unreachable은 실행되지 않는다는걸 컴파일러에게 알려준다. 리턴값이 없어도 에러를 내지 않는다.  http://stackoverflow.com/questions/6031819/emulating-gccs-builtin-unreachable */
#define BUG()							\
do {								\
	asm volatile("1:\tud2\n"				\
		     ".pushsection __bug_table,\"a\"\n"		\
		     __BUG_C0					\
		     "\t.word %c1, 0\n"				\
		     "\t.org 2b+%c2\n"				\
		     ".popsection"				\
		     : : "i" (__FILE__), "i" (__LINE__),	\
		     "i" (sizeof(struct bug_entry)));		\
	unreachable();						\
} while (0)

#else
#define BUG()							\
do {								\
	asm volatile("ud2");					\
	unreachable();						\
} while (0)
#endif

#endif /* !CONFIG_BUG */

#include <asm-generic/bug.h>
#endif /* _ASM_X86_BUG_H */
