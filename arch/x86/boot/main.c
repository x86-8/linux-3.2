
/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * Main module for the real-mode kernel code
 */

#include "boot.h"

struct boot_params boot_params __attribute__((aligned(16)));	///< 전역 Boot_params 구조체.

char *HEAP = _end;
char *heap_end = _end;		/* Default end of heap = no heap, bss/brk를 지나 힙의 끝 */

/*
 * Copy the header into the boot parameter block.  Since this
 * screws up the old-style command line protocol, adjust by
 * filling in the new-style command line pointer instead.
 */


/**
 @brief	넘어온 header의 boot protocol 정보를 boot_params로 복사한다.
 */
static void copy_boot_params(void)
{
	struct old_cmdline {
		u16 cl_magic;
		u16 cl_offset;
	};
	const struct old_cmdline * const oldcmd =
		(const struct old_cmdline *)OLD_CL_ADDRESS; // 부트 프로토콜 2.01 이하 버전에서 쓰이는 command line 주소

	BUILD_BUG_ON(sizeof boot_params != 4096); // 크기가 변하면 컴파일시 에러를 낸다. BUILD_BUG_ON은 condition이 TRUE면 에러
	memcpy(&boot_params.hdr, &hdr, sizeof hdr); /* 넘어온 헤더를 boot_params로 복사 */

	if (!boot_params.hdr.cmd_line_ptr &&
	    oldcmd->cl_magic == OLD_CL_MAGIC) { // old version command line일때 예외처리
		/* Old-style command line protocol. */
		u16 cmdline_seg;

		/* Figure out if the command line falls in the region
		   of memory that an old kernel would have copied up
		   to 0x90000... */
		if (oldcmd->cl_offset < boot_params.hdr.setup_move_size)
			cmdline_seg = ds(); // ds안에 포함되어 있으면 ds를 사용
		else
			cmdline_seg = 0x9000; // 오프셋이 포함되어 있지 않으면 0x9000세그먼트로 오프셋이 계산되었다고 생각한다.

		boot_params.hdr.cmd_line_ptr =
			(cmdline_seg << 4) + oldcmd->cl_offset; // 구버전이라면 2.02 버전 이상에서 사용하는 command line pointer에 주소를 계산해서 넣어준다.
	}
}

/*
 * Set the keyboard repeat rate to maximum.  Unclear why this
 * is done here; this might be possible to kill off as stale code.
 */
/**
 @brief	Keyboard 키 반복 속도를 최대로 설정한다.
 */
static void keyboard_set_repeat(void)
{
	struct biosregs ireg;
	initregs(&ireg);
	ireg.ax = 0x0305;
	intcall(0x16, &ireg, NULL);
	// 0x16 : Keyboard Interrupt
	// 0x0305 : Set Keyboard rate to Maximum.
}

/*
 * Get Intel SpeedStep (IST) information.
 */
/**
 @brief	Get Intel SpeedStep (IST) information\n
	 CPU Level 5 이하의 장비에서는 그냥 리턴한다.(Old Machine...)\n
	 확인된 IST 정보는 boot_params.ist_info 구조체에 입력된다.
 */
static void query_ist(void)
{
	struct biosregs ireg, oreg;

	/* Some older BIOSes apparently crash on this call, so filter
	   it from machines too old to have SpeedStep at all. */
	if (cpu.level < 6)
		return;

	initregs(&ireg);
	ireg.ax  = 0xe980;	 /* IST Support */
	ireg.edx = 0x47534943;	 /* Request value */
	intcall(0x15, &ireg, &oreg);

	boot_params.ist_info.signature  = oreg.eax;
	boot_params.ist_info.command    = oreg.ebx;
	boot_params.ist_info.event      = oreg.ecx;
	boot_params.ist_info.perf_level = oreg.edx;
}

/*
 * Tell the BIOS what CPU mode we intend to run in.
 */
/**
 @brief	64 비트 모드 사용시, 64bit 사용을 위한 BIOS Mode Pre-Set 을 한다.
 */
static void set_bios_mode(void)
{
#ifdef CONFIG_X86_64
	struct biosregs ireg;

	initregs(&ireg);
	ireg.ax = 0xec00;	// Detecting Target Operating Mode.
	ireg.bx = 2;		// Tell BIOS Long Mode will be on. (64)
	intcall(0x15, &ireg, NULL);
#endif
}


/**
 @brief	heap 초기화.
        전역 변수 heap_end 에 heap 영역의 끝을 입력한다.
	stack과 충돌하면 경계로 설정한다.
 */
static void
init_heap(void)
{
	char *stack_end;

	/* 헤더의 플래그에서 CAN_USE_HEAP (0x80) 비트를 체크해서 */
	/* setup 뒤의 heap 사이즈를 알 수 있다. */
	if (boot_params.hdr.loadflags & CAN_USE_HEAP) {
		/* esp - STACK_SIZE(512)
		 * main 에서 바로 실행되는 함수이므로.
		 * esp에는 Stack 영역의 시작 주소가 할당되어 있다.
		 * 현재스택 위치 - 사용할 스택 크기 = stack_end
		 */
		/* stack_end = esp - 512 */
		asm("leal %P1(%%esp),%0"
			: "=r" (stack_end)
			: "i" (-STACK_SIZE)
		    );

		/* heap_end_ptr은 stack/heap end에서
		 * -512이기 때문에 더해준다. (0x200 = 512bytes)
		 */
		heap_end = (char *)
			((size_t)boot_params.hdr.heap_end_ptr + 0x200);
		/* 지정한 heap_end가 stack을 침범/초과하면 힙을 스택의 경계로 설정한다. */
		if (heap_end > stack_end)
			heap_end = stack_end;
	} else {
		/* USE_HEAP을 지원하지 않는 옛 부트 프로토콜 버전 */
		/* Boot protocol 2.00 only, no heap available */
		puts("WARNING: Ancient bootloader, some functionality "
		     "may be limited!\n");
	}
}

/**
 * @brief 이 부분은 파라미터 복사, 화면 저장,등 간단한 초기화 부분이다.
 *        protected 모드로 넘어가기 전까지의 코드이다.
 */
void main(void)
{
	/* First, copy the boot header into the "zeropage" */
	copy_boot_params(); // 부트 파라미터 복사

	/* Initialize the early-boot console */
	/* earlyprintk 옵션 확인 및 시리얼 초기화 */
	console_init();
	/* Debug 옵션 확인 */
	if (cmdline_find_option_bool("debug"))
		puts("early console in setup code\n");

	/* End of heap check */
	/* heap_end 설정 */
	init_heap();

	/* Make sure we have all the proper CPU support */
	/* 이 커널을 사용할수있는 CPU가 아니면 멈춘다 */
	if (validate_cpu()) {
		puts("Unable to boot - please use a kernel appropriate "
		     "for your CPU.\n");
		die();		/* 장렬히 전사 */
	}

	/* Tell the BIOS what CPU mode we intend to run in. */
	/* 64비트면 BIOS에 인터럽트를 64비트 사용을 알린다. */
	set_bios_mode();


	/* Detect memory layout */
	/* e820, e801, 88로 메모리 정보 수집
	 * e820 정보는 boot_params.e820_map에 엔트리 정보가
	 * boot_params.e820_entries에는 엔트리 갯수가 들어간다.
	 */
	detect_memory();	

	/* Set keyboard repeat rate (why?) */
	/* 키반복 속도 최대로 설정 - 느리면 답답하다. */
	keyboard_set_repeat();

	/* Query MCA information */
	query_mca();		/* MCA 정보 수집 */

	/* Query Intel SpeedStep (IST) information */
	query_ist();		/* 인텔 스피드 스텝 정보 수집 */

	/* Query APM information */
#if defined(CONFIG_APM) || defined(CONFIG_APM_MODULE)
	/* 옵션이 있다면 APM 정보를 수집
	 * 요즘은 ACPI를 사용하기 때문에 사용되지 않을 것이다.
	 */
	query_apm_bios();
#endif

	/* Query EDD information */
#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
	/* EDD 정보를 수집한다. 역시 config에 없으면 확인하지 않는다 */
	query_edd();
#endif

	/* Set the video mode */
	/* 비디오 관련 초기화, 화면 저장 */
	set_video();

	/* Do the last things and invoke protected mode */
	go_to_protected_mode();


}
