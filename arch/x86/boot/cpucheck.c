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
 * Check for obligatory CPU features and abort if the features are not
 * present.  This code should be compilable as 16-, 32- or 64-bit
 * code, so be very careful with types and inline assembly.
 *
 * This code should not contain any messages; that requires an
 * additional wrapper.
 *
 * As written, this code is not safe for inclusion into the kernel
 * proper (after FPU initialization, in particular).
 */

#ifdef _SETUP
# include "boot.h"
#endif
#include <linux/types.h>
#include <asm/processor-flags.h>
#include <asm/required-features.h>
#include <asm/msr-index.h>

struct cpu_features cpu;			///< CPU 정보들이 저장된다.
static u32 cpu_vendor[3];			///< Vender ID
static u32 err_flags[NCAPINTS];		///< Error Flags Array.

static const int req_level = CONFIG_X86_MINIMUM_CPU_FAMILY;

static const u32 req_flags[NCAPINTS] =
{
	REQUIRED_MASK0,
	REQUIRED_MASK1,
	0, /* REQUIRED_MASK2 not implemented in this file */
	0, /* REQUIRED_MASK3 not implemented in this file */
	REQUIRED_MASK4,
	0, /* REQUIRED_MASK5 not implemented in this file */
	REQUIRED_MASK6,
	0, /* REQUIRED_MASK7 not implemented in this file */
};

#define A32(a, b, c, d) (((d) << 24)+((c) << 16)+((b) << 8)+(a))


/**
 @brief	Check Vender ID for Authentic AMD(AMD)
  @return	맞다면 1, 아니면 0
 */
static int is_amd(void)
{
	return cpu_vendor[0] == A32('A', 'u', 't', 'h') &&
	       cpu_vendor[1] == A32('e', 'n', 't', 'i') &&
	       cpu_vendor[2] == A32('c', 'A', 'M', 'D');
}


/**
 @brief	Check Vender ID for Centaur Hauls(Via)
  @return	맞다면 1, 아니면 0
 */
static int is_centaur(void)
{
	return cpu_vendor[0] == A32('C', 'e', 'n', 't') &&
	       cpu_vendor[1] == A32('a', 'u', 'r', 'H') &&
	       cpu_vendor[2] == A32('a', 'u', 'l', 's');
}


/**
 @brief	Check Vender ID for Genuine TM x86(Transmeta)
 @return	맞다면 1, 아니면 0
 */
static int is_transmeta(void)
{
	return cpu_vendor[0] == A32('G', 'e', 'n', 'u') &&
	       cpu_vendor[1] == A32('i', 'n', 'e', 'T') &&
	       cpu_vendor[2] == A32('M', 'x', '8', '6');
}


/**
 @brief	Architecture 에서 fpu 기능이 사용가능한지/아닌지를 리턴한다.
 @return	fpu 사용이 가능하면 1, 사용 불가능하면 0
 */
static int
has_fpu(void)
{
	u16 fcw = -1, fsw = -1;
	u32 cr0;

	// cr0 : Control Register 0(msw:Machine state word)
	asm("movl %%cr0,%0" : "=r" (cr0));

	// EM : Emulation(Coprocessor:전문적인 수치 연산)
	// TS : Task Switching
	if (cr0 & (X86_CR0_EM|X86_CR0_TS)) {
		cr0 &= ~(X86_CR0_EM|X86_CR0_TS);
		asm volatile("movl %0,%%cr0" : : "r" (cr0));
	}

	// f가 붙으면 coprocess 명령임.
	// fnstsw : fpu state word
	// fnstcw : fpu control word
	asm volatile("fninit ; fnstsw %0 ; fnstcw %1"
		     : "+m" (fsw), "+m" (fcw));

	return fsw == 0 && (fcw & 0x103f) == 0x003f;
}


/**
 @brief	mask 로 설정된 Flag bit 가
	 현재 Architecture 에서 사용 가능한지/아닌지를 리턴한다.

 @return	사용가능할 경우:1, 사용불가능할 경우:0
 */
static int
has_eflag(
		u32 mask	///< 확인하고자 하는 Flag.
		)
{
	u32 f0, f1;

	asm("pushfl ; "		/* pushfl : push Long type flags */
	    "pushfl ; "
	    "popl %0 ; "
	    "movl %0,%1 ; "
	    "xorl %2,%1 ; "
	    "pushl %1 ; "
	    "popfl ; "
	    "pushfl ; "
	    "popl %1 ; "
	    "popfl"
	    : "=&r" (f0), "=&r" (f1)	/* Output */
	    : "ri" (mask));		/* Input */

	// f0: 최초 저장되어 있는 long type flag
	// f1: (f0 ^ mask) 값을 push & pop.. 함으로써 cpu에서 해당 flag가 사용가능한지를 파악한다.
	return !!((f0^f1) & mask);
}


/**
 @brief	CPUID detection flag 사용여부를 확인해서 사용이 가능하면
	 CPUID detection flag를 이용해 CPU Level, vendor, Feature Information
	 등의 정보를 cpu 전역 구조체에 입력한다.
 */
static void
get_flags(void)
{
	u32 max_intel_level, max_amd_level;
	u32 tfms;

	if (has_fpu())
		set_bit(X86_FEATURE_FPU, cpu.flags);

	/* CPUID detection flag 사용 가능하면... */
	/* EAX에 0을 입력하면 Vender ID 가 나온다.*/
	if (has_eflag(X86_EFLAGS_ID)) {
		asm("cpuid"
		    : "=a" (max_intel_level),
		      "=b" (cpu_vendor[0]),
		      "=d" (cpu_vendor[1]),
		      "=c" (cpu_vendor[2])
		    : "a" (0));

		if (max_intel_level >= 0x00000001 &&
		    max_intel_level <= 0x0000ffff) {
			asm("cpuid"
			    : "=a" (tfms),		/* Level & Model */
			      "=c" (cpu.flags[4]),	/* Feature Information */
			      "=d" (cpu.flags[0])	/* Feature Information */
			    : "a" (0x00000001)
			    : "ebx");
			cpu.level = (tfms >> 8) & 15;
			cpu.model = (tfms >> 4) & 15;
			if (cpu.level >= 6)
				cpu.model += ((tfms >> 16) & 0xf) << 4;
		}

		asm("cpuid"
		    : "=a" (max_amd_level)
		    : "a" (0x80000000)
		    : "ebx", "ecx", "edx");

		if (max_amd_level >= 0x80000001 &&
		    max_amd_level <= 0x8000ffff) {
			u32 eax = 0x80000001;
			asm("cpuid"
			    : "+a" (eax),
			      "=c" (cpu.flags[6]),
			      "=d" (cpu.flags[1])
			    : : "ebx");
		}
	}
}

/* Returns a bitmask of which words we have error bits in */
/**
 @brief cpu.flags 에 설정된 사용가능한 Flag 들과 req_flags 에 설정된 사용해야하는
	 flag 를 검사한다.
	 만약, req_flag 에 설정된 Flag 값이 cpu.flags 에 없다면 err에 Set 된다.
 @return err_flag가 설정된 err
 */
static int
check_flags(void)
{
	u32 err;
	int i;

	err = 0;
	for (i = 0; i < NCAPINTS; i++) {
		err_flags[i] = req_flags[i] & ~cpu.flags[i];
		if (err_flags[i])
			err |= 1 << i;
	}

	return err;
}

/*
 * Returns -1 on error.
 *
 * *cpu_level is set to the current CPU level; *req_level to the required
 * level.  x86-64 is considered level 64 for this purpose.
 *
 * *err_flags_ptr is set to the flags error array if there are flags missing.
 */
/**
 @brief	CPU의 지원여부를 확인한다.\n
	 확인하는 항목들은 req_flags 에 정의된 항목들을 기준으로 검사하며,\n
	 CPU Vendor 에 따른 예외 항목들도 존재한다.\n
	 모든 req_flag 항목들을 만족한다면 성공, 하나라도 만족하지 못한다면 실패로 간주한다.
	 CPU 의 정보들은 전역변수 cpu 에 저장된다.\n
	 Default:level=3
 @return 성공시:0, 실패시:-1
 */
int check_cpu(
		int *cpu_level_ptr,	///< Current CPU level
		int *req_level_ptr, ///< Required level
		u32 **err_flags_ptr	///< error flag array
		)
{
	int err;	// check_flags() 시 발생한 에러 비트들을 저장.

	memset(&cpu.flags, 0, sizeof cpu.flags);
	cpu.level = 3;

	// 486 이상 버전에서만 X86_EFLAGS_AC(0x00040000) flag를 지원한다.
	if (has_eflag(X86_EFLAGS_AC))
		cpu.level = 4;

	get_flags();
	err = check_flags();

	// 64 비트 운영체제의 여부를 확인한다.
	// LONG MODE 를 지원한다면, 64비트 운영체제이다.
	// AMD, Intel 공용.
	if (test_bit(X86_FEATURE_LM, cpu.flags))
		cpu.level = 64;

	// AMD 확인(SSE, SSE2 명령어 Set를 지원하지 않는 AMD CPU들에 대한...)
	// 옵테론(AMD64) 이후 부터는 SSE, SSE2 명령어 Set 기본으로 지원.
	if (err == 0x01 &&
	    !(err_flags[0] &
	      ~((1 << X86_FEATURE_XMM)|(1 << X86_FEATURE_XMM2))) &&
	    is_amd()) {
		/* SSE, SSE2: 명령어 Set */
		/* If this is an AMD and we're only missing SSE+SSE2, try to
		   turn them on */

		u32 ecx = MSR_K7_HWCR;	// 0xc0010015, Pass.....
		u32 eax, edx;

		// rdmsr:Read from model specific register
		// Loads the contents of a 64-bit model specific register (MSR)
		// specified in the ECX register into registers EDX:EAX.
		// 참조 : http://faydoc.tripod.com/cpu/rdmsr.htm
		asm("rdmsr" : "=a" (eax), "=d" (edx) : "c" (ecx));

		eax &= ~(1 << 15);	// Why?

		// wrmsr:Write to Model specific register
		// Writes the contents of registers EDX:EAX into the
		// 64-bit model specific register (MSR) specified in
		// the ECX register.
		asm("wrmsr" : : "a" (eax), "d" (edx), "c" (ecx));

		get_flags();	/* Make sure it really did something */
		err = check_flags();
	}
	// VIA CPU 확인(SSE, SSE2 명령어 Set를 지원하지 않는 AMD CPU들에 대한...)
	else if (err == 0x01 &&
		   !(err_flags[0] & ~(1 << X86_FEATURE_CX8)) &&
		   is_centaur() && cpu.model >= 6) {
		/* If this is a VIA C3, we might have to enable CX8
		   explicitly */

		u32 ecx = MSR_VIA_FCR;
		u32 eax, edx;

		asm("rdmsr" : "=a" (eax), "=d" (edx) : "c" (ecx));
		eax |= (1<<1)|(1<<7);
		asm("wrmsr" : : "a" (eax), "d" (edx), "c" (ecx));

		set_bit(X86_FEATURE_CX8, cpu.flags);
		err = check_flags();
	}
	// Transmeta CPU 확인(SSE, SSE2 명령어 Set를 지원하지 않는 AMD CPU들에 대한...)
	else if (err == 0x01 && is_transmeta()) {
		/* Transmeta might have masked feature bits in word 0 */

		u32 ecx = 0x80860004;
		u32 eax, edx;
		u32 level = 1;

		asm("rdmsr" : "=a" (eax), "=d" (edx) : "c" (ecx));
		asm("wrmsr" : : "a" (~0), "d" (edx), "c" (ecx));
		asm("cpuid"
		    : "+a" (level), "=d" (cpu.flags[0])
		    : : "ecx", "ebx");
		asm("wrmsr" : : "a" (eax), "d" (edx), "c" (ecx));

		err = check_flags();
	}

	if (err_flags_ptr)
		*err_flags_ptr = err ? err_flags : NULL;
	if (cpu_level_ptr)
		*cpu_level_ptr = cpu.level;
	if (req_level_ptr)
		*req_level_ptr = req_level;

	return (cpu.level < req_level || err) ? -1 : 0;
}
