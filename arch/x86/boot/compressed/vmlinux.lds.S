

OUTPUT_FORMAT(CONFIG_OUTPUT_FORMAT, CONFIG_OUTPUT_FORMAT, CONFIG_OUTPUT_FORMAT)

#undef i386

#include <asm/cache.h>
#include <asm/page_types.h>

#ifdef CONFIG_X86_64
OUTPUT_ARCH(i386:x86-64)
ENTRY(startup_64)
#else
OUTPUT_ARCH(i386)
ENTRY(startup_32)
#endif

SECTIONS
{
	/* Be careful parts of head_64.S assume startup_32 is at
	 * address 0.
	 */
	. = 0;
	.head.text : {				/* 재배치후에는 이부분은 덮어쓴다.. */
		_head = . ;
		HEAD_TEXT 				/* #define HEAD_TEXT *(.head.text) */
		_ehead = . ;
	}
	.rodata..compressed : {		/* 압축 커널부분 */
		*(.rodata..compressed)
	}
	.text :	{					/* 안전한 커널코드 */
		_text = .; 	/* Text */
		*(.text)
		*(.text.*)
		_etext = . ;
	}
	.rodata : {
		_rodata = . ;
		*(.rodata)	 /* read-only data */
		*(.rodata.*)
		_erodata = . ;
	}
	.got : {
		_got = .;
		KEEP(*(.got.plt)) /* 제거하면 안되는 영역 */
		KEEP(*(.got))
		_egot = .;
	}
	.data :	{
		_data = . ;
		*(.data)
		*(.data.*)
		_edata = . ;
	}
	. = ALIGN(L1_CACHE_BYTES); /* cache line 크기로 정렬 */
	.bss : {
		_bss = . ;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		. = ALIGN(8);	/* For convenience during zeroing */
		_ebss = .;
	}
/* 64비트에서만 pgtable 추가 */
#ifdef CONFIG_X86_64
       . = ALIGN(PAGE_SIZE);
       .pgtable : {
		_pgtable = . ;
		*(.pgtable)
		_epgtable = . ;
	}
#endif
	_end = .;
}
