/*
 *	Routines to indentify additional cpu features that are scattered in
 *	cpuid space.
 */
#include <linux/cpu.h>

#include <asm/pat.h>
#include <asm/processor.h>

#include <asm/apic.h>

struct cpuid_bit {
	u16 feature;
	u8 reg;
	u8 bit;
	u32 level;
	u32 sub_leaf;
};

enum cpuid_regs {
	CR_EAX = 0,
	CR_ECX,
	CR_EDX,
	CR_EBX
};

void __cpuinit init_scattered_cpuid_features(struct cpuinfo_x86 *c)
{
	u32 max_level;
	u32 regs[4];
	const struct cpuid_bit *cb;

	static const struct cpuid_bit __cpuinitconst cpuid_bits[] = {
		{ X86_FEATURE_DTS,		CR_EAX, 0, 0x00000006, 0 },
		{ X86_FEATURE_IDA,		CR_EAX, 1, 0x00000006, 0 },
		{ X86_FEATURE_ARAT,		CR_EAX, 2, 0x00000006, 0 },
		{ X86_FEATURE_PLN,		CR_EAX, 4, 0x00000006, 0 },
		{ X86_FEATURE_PTS,		CR_EAX, 6, 0x00000006, 0 },
		{ X86_FEATURE_APERFMPERF,	CR_ECX, 0, 0x00000006, 0 },
		{ X86_FEATURE_EPB,		CR_ECX, 3, 0x00000006, 0 },
		{ X86_FEATURE_XSAVEOPT,		CR_EAX,	0, 0x0000000d, 1 },
		{ X86_FEATURE_CPB,		CR_EDX, 9, 0x80000007, 0 },
		{ X86_FEATURE_NPT,		CR_EDX, 0, 0x8000000a, 0 },
		{ X86_FEATURE_LBRV,		CR_EDX, 1, 0x8000000a, 0 },
		{ X86_FEATURE_SVML,		CR_EDX, 2, 0x8000000a, 0 },
		{ X86_FEATURE_NRIPS,		CR_EDX, 3, 0x8000000a, 0 },
		{ X86_FEATURE_TSCRATEMSR,	CR_EDX, 4, 0x8000000a, 0 },
		{ X86_FEATURE_VMCBCLEAN,	CR_EDX, 5, 0x8000000a, 0 },
		{ X86_FEATURE_FLUSHBYASID,	CR_EDX, 6, 0x8000000a, 0 },
		{ X86_FEATURE_DECODEASSISTS,	CR_EDX, 7, 0x8000000a, 0 },
		{ X86_FEATURE_PAUSEFILTER,	CR_EDX,10, 0x8000000a, 0 },
		{ X86_FEATURE_PFTHRESHOLD,	CR_EDX,12, 0x8000000a, 0 },
		{ 0, 0, 0, 0, 0 }
	};
	/*	순서대로 u16 feature;	u8 reg;	u8 bit;	u32 level;	u32 sub_leaf; */

	for (cb = cpuid_bits; cb->feature; cb++) {

		/* Verify that the level is valid */

		/* 해당 레벨의 0 or 0x8000.. 최대값을 구한다.  */
		max_level = cpuid_eax(cb->level & 0xffff0000);
		/* 0x00000000 또는 0x80000000로 얻어온 최대 지원 level값이 
		 * 현재 요청할 cpuid 레벨보다 낮거나, 완전히 벗어나는 경우(0xFFFF)
		 * cpuid를 요청하지 않도록 처리 */
		if (max_level < cb->level ||
		    max_level > (cb->level | 0xffff))
			continue;

		cpuid_count(cb->level, cb->sub_leaf, &regs[CR_EAX],
			    &regs[CR_EBX], &regs[CR_ECX], &regs[CR_EDX]);
 /* c->bit가 TRUE면 FEATURE의 비트도 1 */
		if (regs[cb->reg] & (1 << cb->bit))
			set_cpu_cap(c, cb->feature);
	}
}
