/*
 *  linux/boot/head.S
 *
 *  Copyright (C) 1991, 1992, 1993  Linus Torvalds
 */

/*
 *  head.S contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00001000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory. [According to comments etc elsewhere on a compressed
 * kernel it will end up at 0x1000 + 1Mb I hope so as I assume this. - AC]
 *
 * Page 0 is deliberately kept safe, since System Management Mode code in 
 * laptops may need to access the BIOS data stored there.  This is also
 * useful for future device drivers that either access the BIOS via VM86 
 * mode.
 */

/*
 * High loaded stuff by Hans Lermen & Werner Almesberger, Feb. 1996
 */
	.code32
	.text

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/pgtable_types.h>
#include <asm/page_types.h>
#include <asm/boot.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>
#include <asm/asm-offsets.h>
	/* 압축된 커널의 첫부분은 .head.text 섹션이다. */
	__HEAD 						/* #define __HEAD .section ".head.text","ax" */
	.code32
ENTRY(startup_32)
	cld
	/*
	 * Test KEEP_SEGMENTS flag to see if the bootloader is asking
	 * us to not reload segments
	 */
	/* esi는 파라미터 시작주소 [esi+BP_loadflags]
	 * BP_loadflags= <hdr.loadflags 주소>
	 */
	testb $(1<<6), BP_loadflags(%esi)
	jnz 1f /* bit 6이 켜있으면 세그먼트를 __KERNEL_DS로 초기화하지 않는다. (1f로 점프) */

	cli
	movl	$(__KERNEL_DS), %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %ss
1:

/*
 * Calculate the delta between where we were compiled to run
 * at and where we were actually loaded at.  This can only be done
 * with a short local call on x86.  Nothing  else will tell us what
 * address we are running at.  The reserved chunk of the real-mode
 * data at 0x1e4 (defined as a scratch field) are used as the stack
 * for this calculation. Only 4 bytes are needed.
 */
		/* 시작주소를 얻기 위해 esi(&boot_params)를 참조한다. */
		/* 이 뒤쪽에서 계산을 통해 ebx는 커널의 시작위치, ebp는 (16M정렬한곳) + z_extract_offset으로 쓰인다. */

	leal	(BP_scratch+4)(%esi), %esp /* 스택 임시 설정 */
	call	1f
1:	popl	%ebp
	subl	$1b, %ebp /* ebp=커널본체의 시작주소 */

/* setup a stack and make sure cpu supports long mode. */
	movl	$boot_stack_end, %eax
	addl	%ebp, %eax
	movl	%eax, %esp /* 로드된 위치+head_64 끝부분으로 스택 재설정 */

	call	verify_cpu /* cpu 확인 */
	testl	%eax, %eax
	jnz	no_longmode /* longmode를 지원하지 않으면 무한루프 */

/*
 * Compute the delta between where we were compiled to run at
 * and where the code will actually run at.
 *
 * %ebp contains the address we are loaded at by the boot loader and %ebx
 * contains the address where we should move the kernel image temporarily
 * for safe in-place decompression.
 */
		/* 재배치 가능하면 동적으로 계산하고 아니면 고정된 값을 넣는다. */
		/* ebp는 현재 실행되는 주소 ebx에는 코드 복사후(16M) 압축풀곳을 넣는다. */
#ifdef CONFIG_RELOCATABLE
	movl	%ebp, %ebx
	movl	BP_kernel_alignment(%esi), %eax /* 16M 압축푼 커널이 위치할 곳 */
	decl	%eax /* 16M -1 */
	addl	%eax, %ebx /* 시작주소 + (15.999..M) */
	notl	%eax /* 16M이상 비트는 On */
	andl	%eax, %ebx /* 시작주소를 16M 로 정렬 */
#else
	movl	$LOAD_PHYSICAL_ADDR, %ebx /* 16M 주소로 정렬 */
#endif

	/* Target address to relocate to for decompression */
	addl	$z_extract_offset, %ebx /* ebx=16M + 압출풀 위치(z_extract_offset) */
		/* 이 부분은 boot/compressed/Makefile에서 mkpiggy가 압축된 섹션(.rodata..compressed)의 위치를 piggy.S에 출력한다. */

/*
 * Prepare for entering 64 bit mode
 */

	/* Load new GDT with the 64bit segments using 32bit descriptor */
	leal	gdt(%ebp), %eax  /* head_64.S 끝에 미리 준비해둔 gdt(+시작주소) */
	movl	%eax, gdt+2(%ebp)
	lgdt	gdt(%ebp)

	/* Enable PAE mode */
	movl	$(X86_CR4_PAE), %eax
	movl	%eax, %cr4 /* PAE를 켜서 큰 페이징 크기가 4M->2M이 된다. */

 /*
  * Build early 4G boot pagetable
  */
	/* Initialize Page tables to 0 */
	leal	pgtable(%ebx), %edi /* head_64 끝(pgtable) + 압축 풀 주소(ebx) */
	xorl	%eax, %eax
	movl	$((4096*6)/4), %ecx /* 4k 페이지 테이블 블럭 6개를 초기화 */
	rep	stosl					/* 16M + z_extract_offset 의 6페이지(24KB)를 청소한다. */
	
	/* Long mode에서 2M-long mode로 4G 메모리를 위한 page table을 세팅한다. */
		/* 0에서 4GB까지 가상주소와 물리주소를 1:1로 매핑한다. */
	/* Build Level 4 */
	leal	pgtable + 0(%ebx), %edi /* LV4는 엔트리 하나(8bytes) 충분하다. */
	leal	0x1007 (%edi), %eax /* LV4에서는 한페이지만 설정. 7은 비트 플래그로 U/S, R/W, P비트를 set 한다. +0x1000(4096) 주소를 넣는다. PML4E는 일단 하나만 할당한다. */
	movl	%eax, 0(%edi) /* 다음 블럭 주소를 지정 */

	/* Build Level 3 */
	leal	pgtable + 0x1000(%ebx), %edi /* 0x1000 == 4096 */
	leal	0x1007(%edi), %eax
	movl	$4, %ecx /* PDPE(PDPT)는 4개(32 bytes) 할당 */
1:	movl	%eax, 0x00(%edi)
	addl	$0x00001000, %eax /* 0x2000, 0x3000.... 네개 세팅 (하위 4바이트) */
	addl	$8, %edi /* 64비트 크기 */
	decl	%ecx 
	jnz	1b

	/* Build Level 2 */
	leal	pgtable + 0x2000(%ebx), %edi
	movl	$0x00000183, %eax /*   1,1000,0011 G=1, PS=1, R/W=1, P=1 ; PS로 페이지 크기는 2M이다. 당연히 LV1은 없다.(21 bits) */
	movl	$2048, %ecx /* 2048 * 8 = 16k PDE는 2048개 할당 */
1:	movl	%eax, 0(%edi)
	addl	$0x00200000, %eax /* 2M * 2048 = 4G 영역 오프셋을 지정 */
	addl	$8, %edi
	decl	%ecx
	jnz	1b
	/* LV4=1페이지, LV3=1페이지, LV2는 4페이지로 물리-가상 메로리간의 4G 영역의 1:1 매핑이 끝났다. */

	/* Enable the boot page tables */
	leal	pgtable(%ebx), %eax
	movl	%eax, %cr3 /* 세팅한 페이지 테이블로 설정 */

	/* Enable Long mode in EFER (Extended Feature Enable Register) */
	movl	$MSR_EFER, %ecx
	rdmsr
	btsl	$_EFER_LME, %eax /* long mode enable */
	wrmsr /* 롱모드(64bits) 켬 */

	/*
	 * Setup for the jump to 64bit mode
	 *
	 * When the jump is performend we will be in long mode but
	 * in 32bit compatibility mode with EFER.LME = 1, CS.L = 0, CS.D = 1
	 * (and in turn EFER.LMA = 1).	To jump into 64bit mode we use
	 * the new gdt/idt that has __KERNEL_CS with CS.L = 1.
	 * We place all of the values on our mini stack so lret can
	 * used to perform that far jump.
	 */
	pushl	$__KERNEL_CS
	leal	startup_64(%ebp), %eax
	pushl	%eax

	/* Enter paged protected Mode, activating Long Mode */
	movl	$(X86_CR0_PG | X86_CR0_PE), %eax /* Enable Paging and Protected mode */
	movl	%eax, %cr0

	/* Jump from 32bit compatibility mode into 64bit mode. */
	lret /* 64비트로 점프 */
ENDPROC(startup_32)

no_longmode:
	/* This isn't an x86-64 CPU so hang */
1:
	hlt
	jmp     1b

#include "../../kernel/verify_cpu.S"

	/*
	 * Be careful here startup_64 needs to be at a predictable
	 * address so I can export it in an ELF header.  Bootloaders
	 * should look at the ELF header to find this address, as
	 * it may change in the future.
	 */
	.code64
	.org 0x200
ENTRY(startup_64)
	/*
	 * We come here either from startup_32 or directly from a
	 * 64bit bootloader.  If we come here from a bootloader we depend on
	 * an identity mapped page table being provied that maps our
	 * entire text+data+bss and hopefully all of memory.
	 */

	/* Setup data segments. */
	xorl	%eax, %eax
	movl	%eax, %ds /* 세그먼트 초기화  */
	movl	%eax, %es
	movl	%eax, %ss
	movl	%eax, %fs
	movl	%eax, %gs
	lldt	%ax /* ldt 초기화 */
	movl    $0x20, %eax
	ltr	%ax /* tss 설정 */

	/*
	 * Compute the decompressed kernel start address.  It is where
	 * we were loaded at aligned to a 2M boundary. %rbp contains the
	 * decompressed kernel start address.
	 *
	 * If it is a relocatable kernel then decompress and run the kernel
	 * from load address aligned to 2MB addr, otherwise decompress and
	 * run the kernel from LOAD_PHYSICAL_ADDR
	 *
	 * We cannot rely on the calculation done in 32-bit mode, since we
	 * may have been invoked via the 64-bit entry point.
	 */

	/* Start with the delta to where the kernel will run at. */
		/* 압축푼 커널 위치를 구한다. 이 코드 끝에서 rbp로 점프한다. */
#ifdef CONFIG_RELOCATABLE
	leaq	startup_32(%rip) /* - $startup_32 */, %rbp  /* 64비트에 들어왔기 때문에 시작주소를 다시 구한다. */
	movl	BP_kernel_alignment(%rsi), %eax
	decl	%eax
	addq	%rax, %rbp
	notq	%rax
	andq	%rax, %rbp /* 16M 정렬 */
#else
	movq	$LOAD_PHYSICAL_ADDR, %rbp /* rbp=16M 정렬 */
#endif

	/* Target address to relocate to for decompression */
	leaq	z_extract_offset(%rbp), %rbx /* 옮길곳이다. z_extract_offset는 압축원본과 풀린크기의 차에 약간의 공간을 더해준 것이다. */
		/* rbp + 압축차이면 결국 16M부터 압축풀린 크기만큼(+a) 뒷 주소이다. */

	/* Set up the stack */
	leaq	boot_stack_end(%rbx), %rsp /* 옮길곳의 스택 지정 */

	/* Zero EFLAGS */
	pushq	$0
	popfq /* 플래그 초기화 */

/*
 * Copy the compressed kernel to the end of our buffer
 * where decompression in place becomes safe.
 * 압축된 커널을 커널 끝에 복사한다.
 * 옮길때 1M -> 16M로 바로 옮기는게 아니라 z_extract_offset만큼 띄우게 된다.
 * 그리고 z_extract_offset의 공간, 재배치 이전의 .head.text, 압축 커널의 .rodata..compressed 공간은 압축 풀면서 덮어쓴다.
 */

	pushq	%rsi
	leaq	(_bss-8)(%rip), %rsi /* 원본 위치 = 현재 실행되는 코드중 필요한 영역들을 포함한 마지막 주소 */
	leaq	(_bss-8)(%rbx), %rdi /* 옮길 위치 = 압축풀린크기+약간의 공간 (16M + z_extract_offset + _bss) */
	movq	$_bss /* - $startup_32 */, %rcx /* _bss의 위치는 .text, .rodata, .got, .data 영역의 크기. */
	shrq	$3, %rcx /* count ; 8byte로 나눈다 */
	std /* direction flag를 set해서 스트링 명령의 si,di를 감소 */
	rep	movsq /* 현재 1M쯤에 위치한 코드를 16M부터 압축풀릴크기만큼 뒤에 역순으로 복사 */
	cld
	popq	%rsi

/*
 * Jump to the relocated address.
 * 커널을 16M로 옮겨서 점프한다. 옛 PCI, ISA 장치들은 16M이하밖에 억세스를 못하는 제한이 있음.
 */
	leaq	relocated(%rbx), %rax /* 복사한쪽의 relocated로 점프 */
	jmp	*%rax

		/* 이 부분을 일반 .text 섹션이다. 
		* 커널의 배치는 .head.text, .rodata..compressed, .text 순으로 배치된다.
		* 점프하기 전의 .head.text(16M+z_extract_offset)부터 압축커널이 있는 .rodata..compressed는 덮어쓰여질수 있지만 아래의 .text 부분은 안전하다.
		*/

	.text
relocated:

/*
 * Clear BSS (stack is currently empty)
 */
	xorl	%eax, %eax
	leaq    _bss(%rip), %rdi /* bss 시작 */
	leaq    _ebss(%rip), %rcx /* bss 끝 */
	subq	%rdi, %rcx
	shrq	$3, %rcx
	rep	stosq /* bss(heap, stack) 초기화 */

/*
 * Adjust our own GOT
 */
	leaq	_got(%rip), %rdx /* global offset table */
	leaq	_egot(%rip), %rcx
1:
	cmpq	%rcx, %rdx /* GOT에 옮긴 주소(bx)를 더하기 위한 반복문이다. got의 모든 영역의 base주소에 ebx를 더한다. */
	jae	2f
	addq	%rbx, (%rdx)
	addq	$8, %rdx
	jmp	1b
2:
	
/*
 * Do the decompression, and jump to the new kernel..
 */
	pushq	%rsi			/* Save the real mode argument */
	movq	%rsi, %rdi		/* real mode address */ /* boot_params 주소 */
	leaq	boot_heap(%rip), %rsi	/* malloc area for uncompression */ /* bss에 heap 예약한 부분 */
	leaq	input_data(%rip), %rdx  /* input_data */ /* 압축된 커널 vmlinux.bin.gz */
	movl	$z_input_len, %ecx	/* input_len */ /* 압축 크기 */
	movq	%rbp, %r8		/* output target address */ 
	call	decompress_kernel
	popq	%rsi

/*
 * Jump to the decompressed kernel.
 */
	jmp	*%rbp					/* 압축을 푼 위치로 점프 */
		/* .data와 .bss등 영역은 링크 스크립트에서 뒤쪽 영역으로 배치된다. 고로 압축 푸는 과정에서도 안전하다. */
	.data
gdt:
	.word	gdt_end - gdt
	.long	gdt
	.word	0
	.quad	0x0000000000000000	/* NULL descriptor */
	.quad	0x00af9a000000ffff	/* __KERNEL_CS */
	.quad	0x00cf92000000ffff	/* __KERNEL_DS */
	.quad	0x0080890000000000	/* TS descriptor */
	.quad   0x0000000000000000	/* TS continued */
gdt_end:

/*
 * Stack and heap for uncompression
 */
	.bss
	.balign 4
boot_heap:
	.fill BOOT_HEAP_SIZE, 1, 0
boot_stack:
	.fill BOOT_STACK_SIZE, 1, 0
boot_stack_end:

/*
 * Space for page tables (not in .bss so not zeroed)
 */
	.section ".pgtable","a",@nobits
	.balign 4096
pgtable:
	.fill 6*4096, 1, 0
