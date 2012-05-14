/* Slow paths of read/write spinlocks. */

#include <linux/linkage.h>
#include <asm/alternative-asm.h>
#include <asm/frame.h>
#include <asm/rwlock.h>
		/* 콜링 콘벤션에서 첫번째 인자 */
#ifdef CONFIG_X86_32
# define __lock_ptr eax
#else
# define __lock_ptr rdi
#endif

ENTRY(__write_lock_failed)
	CFI_STARTPROC
	FRAME
0:	LOCK_PREFIX
	/* 뺀값 복구 */
	WRITE_LOCK_ADD($RW_LOCK_BIAS) (%__lock_ptr)
1:	rep; nop
		/* rep nop는 pause다 */
	cmpl	$WRITE_LOCK_CMP, (%__lock_ptr)
	jne	1b
	LOCK_PREFIX
	WRITE_LOCK_SUB($RW_LOCK_BIAS) (%__lock_ptr)
	/* 기다리면서 락 획득 시도 */
	jnz	0b
	ENDFRAME	
	ret
	CFI_ENDPROC
END(__write_lock_failed)

ENTRY(__read_lock_failed)
	/* 디버깅 드워프 정보 */
	CFI_STARTPROC
	FRAME
	/* lock */
0:	LOCK_PREFIX
/* 첫번째 인자(eax or rdi)를 증가. read의 원상복구를 의미한다. */
	READ_LOCK_SIZE(inc) (%__lock_ptr)
/* rep nop == pause = cpu에 idle 상태를 유도한다. */
1:	rep; nop
	READ_LOCK_SIZE(cmp) $1, (%__lock_ptr)
	js	1b
	LOCK_PREFIX
	READ_LOCK_SIZE(dec) (%__lock_ptr)
	js	0b
	ENDFRAME
	ret
	CFI_ENDPROC
END(__read_lock_failed)
