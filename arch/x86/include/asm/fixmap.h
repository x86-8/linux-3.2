/*
 * fixmap.h: compile-time virtual memory allocation
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998 Ingo Molnar
 *
 * Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 * x86_32 and x86_64 integration by Gustavo F. Padovan, February 2009
 */

#ifndef _ASM_X86_FIXMAP_H
#define _ASM_X86_FIXMAP_H

#ifndef __ASSEMBLY__
#include <linux/kernel.h>
#include <asm/acpi.h>
#include <asm/apicdef.h>
#include <asm/page.h>
#ifdef CONFIG_X86_32
#include <linux/threads.h>
#include <asm/kmap_types.h>
#else
#include <asm/vsyscall.h>
#endif

/*
 * We can't declare FIXADDR_TOP as variable for x86_64 because vsyscall
 * uses fixmaps that relies on FIXADDR_TOP for proper address calculation.
 * Because of this, FIXADDR_TOP x86 integration was left as later work.
 */
#ifdef CONFIG_X86_32
/* used by vmalloc.c, vsyscall.lds.S.
 *
 * Leave one empty page between vmalloc'ed areas and
 * the start of the fixmap.
 */

extern unsigned long __FIXADDR_TOP;
#define FIXADDR_TOP	((unsigned long)__FIXADDR_TOP)
/* 32비트는 메모리 끝에서 한 페이지 뺀 곳이 FIXADDR_TOP이다. */

#define FIXADDR_USER_START     __fix_to_virt(FIX_VDSO)
#define FIXADDR_USER_END       __fix_to_virt(FIX_VDSO - 1)
/* FIXADDR_USER_END는 32비트 메모리의 시작 페이지 */
#else

/* 64비트에서 FIXADDR_TOP = 메모리끝 - 2M - 1페이지(4K) */
#define FIXADDR_TOP	(VSYSCALL_END-PAGE_SIZE)

/* Only covers 32bit vsyscalls currently. Need another set for 64bit. */
#define FIXADDR_USER_START	((unsigned long)VSYSCALL32_VSYSCALL)
#define FIXADDR_USER_END	(FIXADDR_USER_START + PAGE_SIZE)
#endif


/*
 * Here we define all the compile-time 'special' virtual
 * addresses. The point is to have a constant address at
 * compile time, but to set the physical address only
 * in the boot process.
 * for x86_32: We allocate these special addresses
 * from the end of virtual memory (0xfffff000) backwards.
 * Also this lets us do fail-safe vmalloc(), we
 * can guarantee that these special addresses and
 * vmalloc()-ed addresses never overlap.
 *
 * These 'compile-time allocated' memory buffers are
 * fixed-size 4k pages (or larger if used with an increment
 * higher than 1). Use set_fixmap(idx,phys) to associate
 * physical memory with fixmap indices.
 *
 * TLB entries of such buffers will not be flushed across
 * task switches.
 */
enum fixed_addresses {
#ifdef CONFIG_X86_32
	FIX_HOLE,
	FIX_VDSO,
#else
	VSYSCALL_LAST_PAGE,			/* VSYSCALL 끝부분 */
								/* VSYSCALL의 크기(8M)를 페이지 크기(4K)로 나눈다. 여기서는 LAST_PAGE=0 FIRST_PAGE=2047이 될것이다. */
								/* VSYSCALL_FIRST/LAST_PAGE 는 페이지(PTE) 단위, VSYSCALL_START,END 는 PMD(2M) 단위 */
	VSYSCALL_FIRST_PAGE = VSYSCALL_LAST_PAGE
			    + ((VSYSCALL_END-VSYSCALL_START) >> PAGE_SHIFT) - 1,
	VVAR_PAGE,
	VSYSCALL_HPET,
#endif
	FIX_DBGP_BASE,
	FIX_EARLYCON_MEM_BASE,
#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	FIX_OHCI1394_BASE,
#endif
#ifdef CONFIG_X86_LOCAL_APIC
	FIX_APIC_BASE,	/* local (CPU) APIC) -- required for SMP or not */
#endif
#ifdef CONFIG_X86_IO_APIC
	FIX_IO_APIC_BASE_0,
	FIX_IO_APIC_BASE_END = FIX_IO_APIC_BASE_0 + MAX_IO_APICS - 1,
#endif
#ifdef CONFIG_X86_VISWS_APIC
	FIX_CO_CPU,	/* Cobalt timer */
	FIX_CO_APIC,	/* Cobalt APIC Redirection Table */
	FIX_LI_PCIA,	/* Lithium PCI Bridge A */
	FIX_LI_PCIB,	/* Lithium PCI Bridge B */
#endif
#ifdef CONFIG_X86_F00F_BUG
	FIX_F00F_IDT,	/* Virtual mapping for IDT */
#endif
#ifdef CONFIG_X86_CYCLONE_TIMER
	FIX_CYCLONE_TIMER, /*cyclone timer register*/
#endif
#ifdef CONFIG_X86_32
	FIX_KMAP_BEGIN,	/* reserved pte's for temporary kernel mappings */
	FIX_KMAP_END = FIX_KMAP_BEGIN+(KM_TYPE_NR*NR_CPUS)-1,
#ifdef CONFIG_PCI_MMCONFIG
	FIX_PCIE_MCFG,
#endif
#endif
#ifdef CONFIG_PARAVIRT
	FIX_PARAVIRT_BOOTMAP,
#endif
	FIX_TEXT_POKE1,	/* reserve 2 pages for text_poke() */
	FIX_TEXT_POKE0, /* first page is last, because allocation is backward */
#ifdef	CONFIG_X86_MRST
	FIX_LNW_VRTC,
#endif
	__end_of_permanent_fixed_addresses,
	/* 이곳이 FIXADDR의 끝이다. 아래쪽은 부팅시만 사용 */
	/*
	 * 256 temporary boot-time mappings, used by early_ioremap(),
	 * before ioremap() is functional.
	 *
	 * If necessary we round it up to the next 256 pages boundary so
	 * that we can have a single pgd entry and a single pte table:
	 */
	/* 비트맵이 아니다. 이곳은 boot-time 을 위한 공간이다. */
#define NR_FIX_BTMAPS		64
#define FIX_BTMAPS_SLOTS	4
#define TOTAL_FIX_BTMAPS	(NR_FIX_BTMAPS * FIX_BTMAPS_SLOTS)
	/* 이 루틴은 같은 PTE에 <부팅 매핑> 페이지(256)들이 같은 PTE블럭에 연속적으로 할당가능한지 체크한다.
	 * 공간이 충분하다면 연속해서 할당하고 모자라면 PTE블럭을 넘겨서 정렬한후 할당한다.
	 *
	 * (x ^ (x + small_block - 1)) & large_block 에서
	 * x는 현재값, small_block은 할당할 값, large_block은 구분되는 블럭이다.
	 * small_block이 large_block에 들어갈수 있다면 값이 넘치지 않으면 and도 0이 된다.
	 * x가 원래 large_block의 비트값을 가지는 경우도 있기에 자신과 xor해서 체크한다.
	 * 자신과 xor하면 값이 변하지 않으면 0이 된다.
	 * large_block 단위로 넘치지 않았다면 xor결과 0이 되고 and 또한 0이 된다.
	 * (large_block은 2의 승수)
	 *
	 * 결과가 0이 아니면 x + block - (x & (block - 1)) 은 블럭단위를 증가시키고 
	 * (x & (block - 1))로 block보다 작은 값을 구한다.
	 * x의 block단위안에서 값을 빼면 블럭 단위로 정렬된다.
	 *
	 * 0이면 small_block이 들어가기에 공간이 충분하다. 주소를 그대로 사용한다.
	 **/
	FIX_BTMAP_END =
	 (__end_of_permanent_fixed_addresses ^
	  (__end_of_permanent_fixed_addresses + TOTAL_FIX_BTMAPS - 1)) &
	 -PTRS_PER_PTE
	 ? __end_of_permanent_fixed_addresses + TOTAL_FIX_BTMAPS -
	   (__end_of_permanent_fixed_addresses & (TOTAL_FIX_BTMAPS - 1))
	 : __end_of_permanent_fixed_addresses,
	FIX_BTMAP_BEGIN = FIX_BTMAP_END + TOTAL_FIX_BTMAPS - 1,
#ifdef CONFIG_X86_32
	FIX_WP_TEST,
#endif
#ifdef CONFIG_INTEL_TXT
	FIX_TBOOT_BASE,
#endif
	__end_of_fixed_addresses
};


extern void reserve_top_address(unsigned long reserve);

#define FIXADDR_SIZE	(__end_of_permanent_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_BOOT_SIZE	(__end_of_fixed_addresses << PAGE_SHIFT)
#define FIXADDR_START		(FIXADDR_TOP - FIXADDR_SIZE)
#define FIXADDR_BOOT_START	(FIXADDR_TOP - FIXADDR_BOOT_SIZE)

extern int fixmaps_set;

extern pte_t *kmap_pte;
extern pgprot_t kmap_prot;
extern pte_t *pkmap_page_table;

void __native_set_fixmap(enum fixed_addresses idx, pte_t pte);
void native_set_fixmap(enum fixed_addresses idx,
		       phys_addr_t phys, pgprot_t flags);

#ifndef CONFIG_PARAVIRT
static inline void __set_fixmap(enum fixed_addresses idx,
				phys_addr_t phys, pgprot_t flags)
{
	native_set_fixmap(idx, phys, flags);
}
#endif

#define set_fixmap(idx, phys)				\
	__set_fixmap(idx, phys, PAGE_KERNEL)

/*
 * Some hardware wants to get fixmapped without caching.
 */
#define set_fixmap_nocache(idx, phys)			\
	__set_fixmap(idx, phys, PAGE_KERNEL_NOCACHE)

#define clear_fixmap(idx)			\
	__set_fixmap(idx, 0, __pgprot(0))
/* PAGE_SHIFT = 12 = 4K */
/* __fix_to_virt는 페이지 번호를 가상주소로
 * __virt_to_fix는 가상주소를 페이지 번호로 바꾼다 */
#define __fix_to_virt(x)	(FIXADDR_TOP - ((x) << PAGE_SHIFT))	
#define __virt_to_fix(x)	((FIXADDR_TOP - ((x)&PAGE_MASK)) >> PAGE_SHIFT)

extern void __this_fixmap_does_not_exist(void);

/*
 * 'index to address' translation. If anyone tries to use the idx
 * directly without translation, we catch the bug with a NULL-deference
 * kernel oops. Illegal ranges of incoming indices are caught too.
 */
static __always_inline unsigned long fix_to_virt(const unsigned int idx)
{
	/*
	 * this branch gets completely eliminated after inlining,
	 * except when someone tries to use fixaddr indices in an
	 * illegal way. (such as mixing up address types or using
	 * out-of-range indices).
	 *
	 * If it doesn't get removed, the linker will complain
	 * loudly with a reasonably clear error message..
	 */
	if (idx >= __end_of_fixed_addresses)
		__this_fixmap_does_not_exist();

	return __fix_to_virt(idx);
}

static inline unsigned long virt_to_fix(const unsigned long vaddr)
{
	BUG_ON(vaddr >= FIXADDR_TOP || vaddr < FIXADDR_START);
	return __virt_to_fix(vaddr);
}

/* Return an pointer with offset calculated */
static __always_inline unsigned long
__set_fixmap_offset(enum fixed_addresses idx, phys_addr_t phys, pgprot_t flags)
{
	__set_fixmap(idx, phys, flags);
	return fix_to_virt(idx) + (phys & (PAGE_SIZE - 1));
}

#define set_fixmap_offset(idx, phys)			\
	__set_fixmap_offset(idx, phys, PAGE_KERNEL)

#define set_fixmap_offset_nocache(idx, phys)			\
	__set_fixmap_offset(idx, phys, PAGE_KERNEL_NOCACHE)

#endif /* !__ASSEMBLY__ */
#endif /* _ASM_X86_FIXMAP_H */
