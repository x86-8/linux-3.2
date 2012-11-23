/*
 *  linux/arch/x86_64/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000  Pavel Machek <pavel@ucw.cz>
 *  Copyright (C) 2002,2003 Andi Kleen <ak@suse.de>
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/proc_fs.h>
#include <linux/pci.h>
#include <linux/pfn.h>
#include <linux/poison.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>
#include <linux/nmi.h>
#include <linux/gfp.h>

#include <asm/processor.h>
#include <asm/bios_ebda.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/dma.h>
#include <asm/fixmap.h>
#include <asm/e820.h>
#include <asm/apic.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <asm/smp.h>
#include <asm/sections.h>
#include <asm/kdebug.h>
#include <asm/numa.h>
#include <asm/cacheflush.h>
#include <asm/init.h>
#include <asm/uv/uv.h>
#include <asm/setup.h>

static int __init parse_direct_gbpages_off(char *arg)
{
	direct_gbpages = 0;
	return 0;
}
early_param("nogbpages", parse_direct_gbpages_off);

static int __init parse_direct_gbpages_on(char *arg)
{
	direct_gbpages = 1;
	return 0;
}
early_param("gbpages", parse_direct_gbpages_on);

/*
 * NOTE: pagetable_init alloc all the fixmap pagetables contiguous on the
 * physical space so we can cache the place of the first one and move
 * around without checking the pgd every time.
 */

pteval_t __supported_pte_mask __read_mostly = ~_PAGE_IOMAP;
EXPORT_SYMBOL_GPL(__supported_pte_mask);

int force_personality32;

/*
 * noexec32=on|off
 * Control non executable heap for 32bit processes.
 * To control the stack too use noexec=off
 *
 * on	PROT_READ does not imply PROT_EXEC for 32-bit processes (default)
 * off	PROT_READ implies PROT_EXEC
 */
static int __init nonx32_setup(char *str)
{
	if (!strcmp(str, "on"))
		force_personality32 &= ~READ_IMPLIES_EXEC;
	else if (!strcmp(str, "off"))
		force_personality32 |= READ_IMPLIES_EXEC;
	return 1;
}
__setup("noexec32=", nonx32_setup);

/*
 * When memory was added/removed make sure all the processes MM have
 * suitable PGD entries in the local PGD level page.
 */
void sync_global_pgds(unsigned long start, unsigned long end)
{
	unsigned long address;

	for (address = start; address <= end; address += PGDIR_SIZE) {
		const pgd_t *pgd_ref = pgd_offset_k(address);
		struct page *page;
		/* pgd가 없으면 continue */
		if (pgd_none(*pgd_ref))
			continue;

		spin_lock(&pgd_lock);
		list_for_each_entry(page, &pgd_list, lru) {
			pgd_t *pgd;
			spinlock_t *pgt_lock;

			pgd = (pgd_t *)page_address(page) + pgd_index(address);
			/* the pgt_lock only for Xen */
			pgt_lock = &pgd_page_get_mm(page)->page_table_lock;
			spin_lock(pgt_lock);

			if (pgd_none(*pgd))
				set_pgd(pgd, *pgd_ref);
			else
				BUG_ON(pgd_page_vaddr(*pgd)
				       != pgd_page_vaddr(*pgd_ref));

			spin_unlock(pgt_lock);
		}
		spin_unlock(&pgd_lock);
	}
}

/*
 * NOTE: This function is marked __ref because it calls __init function
 * (alloc_bootmem_pages). It's safe to do it ONLY when after_bootmem == 0.
 */
/* 64비트는 NO_BOOTMEM이다. 그래서 nobootmem.c를 참조(모두 include bootmem.h) */
static __ref void *spp_getpage(void)
{
	void *ptr;

	if (after_bootmem)
		ptr = (void *) get_zeroed_page(GFP_ATOMIC | __GFP_NOTRACK);
	else
		ptr = alloc_bootmem_pages(PAGE_SIZE);

	if (!ptr || ((unsigned long)ptr & ~PAGE_MASK)) {
		panic("set_pte_phys: cannot allocate page data %s\n",
			after_bootmem ? "after bootmem" : "");
	}

	pr_debug("spp_getpage %p\n", ptr);

	return ptr;
}

static pud_t *fill_pud(pgd_t *pgd, unsigned long vaddr)
{
	if (pgd_none(*pgd)) {
		pud_t *pud = (pud_t *)spp_getpage();
		pgd_populate(&init_mm, pgd, pud);
		if (pud != pud_offset(pgd, 0))
			printk(KERN_ERR "PAGETABLE BUG #00! %p <-> %p\n",
			       pud, pud_offset(pgd, 0));
	}
	return pud_offset(pgd, vaddr);
}

static pmd_t *fill_pmd(pud_t *pud, unsigned long vaddr)
{
	/* pud가 없으면 페이지 할당
	 * pud가 있으면 그 값(주소)을 리턴한다.
	 */
	if (pud_none(*pud)) {
		pmd_t *pmd = (pmd_t *) spp_getpage();
		pud_populate(&init_mm, pud, pmd);
		if (pmd != pmd_offset(pud, 0))
			printk(KERN_ERR "PAGETABLE BUG #01! %p <-> %p\n",
			       pmd, pmd_offset(pud, 0));
	}
	return pmd_offset(pud, vaddr);
}

static pte_t *fill_pte(pmd_t *pmd, unsigned long vaddr)
{
	if (pmd_none(*pmd)) {
		pte_t *pte = (pte_t *) spp_getpage();
		pmd_populate_kernel(&init_mm, pmd, pte);
		if (pte != pte_offset_kernel(pmd, 0))
			printk(KERN_ERR "PAGETABLE BUG #02!\n");
	}
	return pte_offset_kernel(pmd, vaddr);
}
/* vaddr에 해당하는 pte 엔트리를 pud부터 찾아서 new_pte로 세팅한다. */
void set_pte_vaddr_pud(pud_t *pud_page, unsigned long vaddr, pte_t new_pte)
{
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	/* 가상주소의 pud 주소를 구한다. */
	pud = pud_page + pud_index(vaddr);
	/* pud에서 vaddr의 pmd 엔트리를 가져온다.
	 * 해당 엔트리가 비었으면 할당하지만 지금은 할당이 다 되어 있기 때문에
	 * spp_getpage는 호출되지 않을 것이다.
	 */
	pmd = fill_pmd(pud, vaddr);
	/* pte 엔트리 주소 역시 구한다. */
	pte = fill_pte(pmd, vaddr);
	/* 해당 pte 엔트리에 인자로 받은 new_pte를 넣는다. */
	set_pte(pte, new_pte);

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	/* TLB를 비운다. */
	__flush_tlb_one(vaddr);
}
/* 가상주소의 해당 pte 엔트리를 pteval로 세팅 */
void set_pte_vaddr(unsigned long vaddr, pte_t pteval)
{
	pgd_t *pgd;
	pud_t *pud_page;

	pr_debug("set_pte_vaddr %lx to %lx\n", vaddr, native_pte_val(pteval));
	/* 가상 주소에 해당하는 pgd 주소(direct mapping된 가상주소)를 구한다. */
	pgd = pgd_offset_k(vaddr);
	/* 해당 PGD가 비어있으면 에러 */
	if (pgd_none(*pgd)) {
		printk(KERN_ERR
			"PGD FIXMAP MISSING, it should be setup in head.S!\n");
		return;
	}
	pud_page = (pud_t*)pgd_page_vaddr(*pgd);
	/* pteval 값으로 엔트리 세팅 */
	set_pte_vaddr_pud(pud_page, vaddr, pteval);
}

pmd_t * __init populate_extra_pmd(unsigned long vaddr)
{
	pgd_t *pgd;
	pud_t *pud;

	pgd = pgd_offset_k(vaddr);
	pud = fill_pud(pgd, vaddr);
	return fill_pmd(pud, vaddr);
}

pte_t * __init populate_extra_pte(unsigned long vaddr)
{
	pmd_t *pmd;

	pmd = populate_extra_pmd(vaddr);
	return fill_pte(pmd, vaddr);
}

/*
 * Create large page table mappings for a range of physical addresses.
 */
static void __init __init_extra_mapping(unsigned long phys, unsigned long size,
						pgprot_t prot)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	BUG_ON((phys & ~PMD_MASK) || (size & ~PMD_MASK));
	for (; size; phys += PMD_SIZE, size -= PMD_SIZE) {
		pgd = pgd_offset_k((unsigned long)__va(phys));
		if (pgd_none(*pgd)) {
			pud = (pud_t *) spp_getpage();
			set_pgd(pgd, __pgd(__pa(pud) | _KERNPG_TABLE |
						_PAGE_USER));
		}
		pud = pud_offset(pgd, (unsigned long)__va(phys));
		if (pud_none(*pud)) {
			pmd = (pmd_t *) spp_getpage();
			set_pud(pud, __pud(__pa(pmd) | _KERNPG_TABLE |
						_PAGE_USER));
		}
		pmd = pmd_offset(pud, phys);
		BUG_ON(!pmd_none(*pmd));
		set_pmd(pmd, __pmd(phys | pgprot_val(prot)));
	}
}

void __init init_extra_mapping_wb(unsigned long phys, unsigned long size)
{
	__init_extra_mapping(phys, size, PAGE_KERNEL_LARGE);
}

void __init init_extra_mapping_uc(unsigned long phys, unsigned long size)
{
	__init_extra_mapping(phys, size, PAGE_KERNEL_LARGE_NOCACHE);
}

/*
 * The head.S code sets up the kernel high mapping:
 *
 *   from __START_KERNEL_map to __START_KERNEL_map + size (== _end-_text)
 *
 * phys_addr holds the negative offset to the kernel, which is added
 * to the compile time generated pmds. This results in invalid pmds up
 * to the point where we hit the physaddr 0 mapping.
 *
 * We limit the mappings to the region from _text to _brk_end.  _brk_end
 * is rounded up to the 2MB boundary. This catches the invalid pmds as
 * well, as they are located before _text:
 */
/* 커널영역중 커널이 사용중이 영역 이외의 부분을 0으로 초기화한다. */
void __init cleanup_highmap(void)
{
	/* 커널 text mapping 영역 */
	unsigned long vaddr = __START_KERNEL_map;
	/* 커널영역 시작 + 커널 영역 최대  크기
	 * 이 값은 512M or limit값인데 지금은 최대값인 512M 일 것이다. */
	unsigned long vaddr_end = __START_KERNEL_map + (max_pfn_mapped << PAGE_SHIFT);
	/* brk_end를 PMD_SIZE단위로 올림 (2M boundary) */
	unsigned long end = roundup((unsigned long)_brk_end, PMD_SIZE) - 1;
	/* 설정한 커널 pmd 테이블 주소를 가져온다. */
	pmd_t *pmd = level2_kernel_pgt;
	/* 커널 영역의(0xfff...800...) 끝까지 pmd 엔트리를 하나씩 탐색한다. */
	for (; vaddr + PMD_SIZE - 1 < vaddr_end; pmd++, vaddr += PMD_SIZE) {
		if (pmd_none(*pmd)) /* pmd 엔트리가 연결안되었으면 패스 */
			continue;
		/* vaddr부터 16M이하의 메모리(_text)와 커널이 사용중인 영역끝(brk_end)의
		 * 엔트리를 0으로 초기화한다
		 * 캐스팅을 안하면 메모리 주소가 음수가 되기 때문에 unsigned long으로 캐스팅
		 */
		if (vaddr < (unsigned long) _text || vaddr > end)
			set_pmd(pmd, __pmd(0)); /* 0인 pmd 엔트리로 현재 pmd를 세팅 */
	}
}

static __ref void *alloc_low_page(unsigned long *phys)
{
	/* pfn= 이번에 할당할 페이지 번호 */
	unsigned long pfn = pgt_buf_end++;
	void *adr;
	/* bootmem이 1이면 */
	if (after_bootmem) {
		adr = (void *)get_zeroed_page(GFP_ATOMIC | __GFP_NOTRACK);
		*phys = __pa(adr);

		return adr;
	}
	/* 처음엔 이쪽으로 물리메모리와 ioremap으로 연결 */
	if (pfn >= pgt_buf_top)
		panic("alloc_low_page: ran out of memory");
	/* pfn이 가리키는 가상 메모리와 매핑해준다. */
	adr = early_memremap(pfn * PAGE_SIZE, PAGE_SIZE);
	/* 할당받은 페이지 클리어 */
	clear_page(adr);
	*phys  = pfn * PAGE_SIZE;
	return adr;
}
/* __va한 가상주소를 물리주소로 바꿔 remap하고 그 가상주소를 리턴한다. */
static __ref void *map_low_page(void *virt)
{
	void *adr;
	unsigned long phys, left;

	if (after_bootmem)
		return virt;
	/* direct mapping 된 것의 물리주소 */
	phys = __pa(virt);
	/* left는 페이지의 오프셋 혹은 플래그 */
	left = phys & (PAGE_SIZE - 1);
	/* 하위 12비트를 버린 phys(물리주소)를 fixed_addr의 가상주소로 remap 한다. */
	adr = early_memremap(phys & PAGE_MASK, PAGE_SIZE);
	/* 매핑한 가상주소에 오프셋을 더해준다. */
	adr = (void *)(((unsigned long)adr) | left);

	return adr;
}
/* unmap 하고 슬롯을 discard 한다. */
static __ref void unmap_low_page(void *adr)
{
	if (after_bootmem)
		return;

	early_iounmap((void *)((unsigned long)adr & PAGE_MASK), PAGE_SIZE);
}

static unsigned long __meminit
phys_pte_init(pte_t *pte_page, unsigned long addr, unsigned long end,
	      pgprot_t prot)
{
	unsigned pages = 0;
	unsigned long last_map_addr = end;
	int i;

	pte_t *pte = pte_page + pte_index(addr);

	for(i = pte_index(addr); i < PTRS_PER_PTE; i++, addr += PAGE_SIZE, pte++) {
		/* 할당할 주소를 넘어가면 초기화 */
		if (addr >= end) {
			if (!after_bootmem) {
				for(; i < PTRS_PER_PTE; i++, pte++)
					set_pte(pte, __pte(0));
			}
			break;
		}

		/*
		 * We will re-use the existing mapping.
		 * Xen for example has some special requirements, like mapping
		 * pagetable pages as RO. So assume someone who pre-setup
		 * these mappings are more intelligent.
		 */
		/* 값이 있으면 패스, 기존 매핑을 그대로 쓴다. */
		if (pte_val(*pte)) {
			pages++;
			continue;
		}

		if (0)
			printk("   pte=%p addr=%lx pte=%016lx\n",
			       pte, addr, pfn_pte(addr >> PAGE_SHIFT, PAGE_KERNEL).pte);
		/* 값이 없다면 매핑할 주소를 속성(prot)와 함께 엔트리에 넣는다. */
		pages++;
		set_pte(pte, pfn_pte(addr >> PAGE_SHIFT, prot));
		/* 마지막 매핑 값을 증가시킨다. */
		last_map_addr = (addr & PAGE_MASK) + PAGE_SIZE;
	}

	update_page_count(PG_LEVEL_4K, pages); /* 업데이트한 페이지 수 업데이트 */

	return last_map_addr;	/* 마지막 주소 증가 */
}

static unsigned long __meminit
phys_pmd_init(pmd_t *pmd_page, unsigned long address, unsigned long end,
	      unsigned long page_size_mask, pgprot_t prot)
{
	unsigned long pages = 0;
	unsigned long last_map_addr = end;

	int i = pmd_index(address);

	for (; i < PTRS_PER_PMD; i++, address += PMD_SIZE) {
		unsigned long pte_phys;
		pmd_t *pmd = pmd_page + pmd_index(address);
		pte_t *pte;
		pgprot_t new_prot = prot;

		if (address >= end) {
			if (!after_bootmem) {
				for (; i < PTRS_PER_PMD; i++, pmd++)
					set_pmd(pmd, __pmd(0));
			}
			break;
		}
		/* PMD가 null이면 락걸고 매핑하고 할당 */
		if (pmd_val(*pmd)) {
			if (!pmd_large(*pmd)) {
				spin_lock(&init_mm.page_table_lock);
				pte = map_low_page((pte_t *)pmd_page_vaddr(*pmd));
				/* 물리주소 초기화 혹은 그대로 쓴다.  */
				last_map_addr = phys_pte_init(pte, address,
								end, prot);
				unmap_low_page(pte);
				spin_unlock(&init_mm.page_table_lock);
				continue;
			}
			/*
			 * If we are ok with PG_LEVEL_2M mapping, then we will
			 * use the existing mapping,
			 *
			 * Otherwise, we will split the large page mapping but
			 * use the same existing protection bits except for
			 * large page, so that we don't violate Intel's TLB
			 * Application note (317080) which says, while changing
			 * the page sizes, new and old translations should
			 * not differ with respect to page frame and
			 * attributes.
			 */
			/* PTE가 없으면 page 카운트 증가 */
			if (page_size_mask & (1 << PG_LEVEL_2M)) {
				pages++;
				continue;
			}
			new_prot = pte_pgprot(pte_clrhuge(*(pte_t *)pmd));
		}
		/* 2MB 페이징이면 PMD가 끝이기 때문에 할당할 필요가 없다.
		 * 물리주소 대입만 한다.
		 */
		if (page_size_mask & (1<<PG_LEVEL_2M)) {
			pages++;
			spin_lock(&init_mm.page_table_lock);
			set_pte((pte_t *)pmd,
				pfn_pte(address >> PAGE_SHIFT,
					__pgprot(pgprot_val(prot) | _PAGE_PSE)));
			spin_unlock(&init_mm.page_table_lock);
			last_map_addr = (address & PMD_MASK) + PMD_SIZE;
			continue;
		}
		/* 4KB면 페이징에 사용할 페이지 할당. */
		pte = alloc_low_page(&pte_phys);
		/* 초기화 후 last 값 증가 */
		last_map_addr = phys_pte_init(pte, address, end, new_prot);
		unmap_low_page(pte);

		spin_lock(&init_mm.page_table_lock);
		/* 일반적인 모양의 set pmd  */
		pmd_populate_kernel(&init_mm, pmd, __va(pte_phys));
		spin_unlock(&init_mm.page_table_lock);
	}
	update_page_count(PG_LEVEL_2M, pages); /* 카운트 증가 */
	return last_map_addr;
}
/* pud 페이지의 포인터를 받아서  */
static unsigned long __meminit
phys_pud_init(pud_t *pud_page, unsigned long addr, unsigned long end,
			 unsigned long page_size_mask)
{
	unsigned long pages = 0;
	unsigned long last_map_addr = end;
	/* 시작주소의 pud 인덱스 */
	int i = pud_index(addr);
	/* PUD 수만큼 반복
	 * pud index(i) 증가, PUD 이상 addr만 남기고 1 PUD 주소 증가
	 */
	for (; i < PTRS_PER_PUD; i++, addr = (addr & PUD_MASK) + PUD_SIZE) {
		unsigned long pmd_phys;
		pud_t *pud = pud_page + pud_index(addr);
		pmd_t *pmd;
		pgprot_t prot = PAGE_KERNEL;
		/* 끝났으면 break! */
		if (addr >= end)
			break;
		/* bootmem이 안되었고 해당 주소가 type 0 이 아니면 pud 엔트리 초기화 */
		if (!after_bootmem &&
				!e820_any_mapped(addr, addr+PUD_SIZE, 0)) {
			/* 해당 pud 엔트리 초기화 */
			set_pud(pud, __pud(0));
			continue;
		}
		/* PUD 값이 NULL이면 */
		if (pud_val(*pud)) {
			if (!pud_large(*pud)) {
				/* pmd를 매핑 한다. */
				pmd = map_low_page(pmd_offset(pud, 0));
				last_map_addr = phys_pmd_init(pmd, addr, end,
							 page_size_mask, prot);
				unmap_low_page(pmd);
				__flush_tlb_all();
				continue;
			}
			/*
			 * If we are ok with PG_LEVEL_1G mapping, then we will
			 * use the existing mapping.
			 *
			 * Otherwise, we will split the gbpage mapping but use
			 * the same existing protection  bits except for large
			 * page, so that we don't violate Intel's TLB
			 * Application note (317080) which says, while changing
			 * the page sizes, new and old translations should
			 * not differ with respect to page frame and
			 * attributes.
			 */
			/* 1G면 패스 */
			if (page_size_mask & (1 << PG_LEVEL_1G)) {
				pages++;
				continue;
			}
			prot = pte_pgprot(pte_clrhuge(*(pte_t *)pud));
		}
		/* 역시 1G 페이징이면 이곳이 마지막이라 할당하지 않는다. */
		if (page_size_mask & (1<<PG_LEVEL_1G)) {
			pages++;
			spin_lock(&init_mm.page_table_lock);
			set_pte((pte_t *)pud,
				pfn_pte(addr >> PAGE_SHIFT, PAGE_KERNEL_LARGE));
			spin_unlock(&init_mm.page_table_lock);
			last_map_addr = (addr & PUD_MASK) + PUD_SIZE;
			continue;
		}
		/* pmd를 위해 페이지 할당 */
		pmd = alloc_low_page(&pmd_phys);
		/* PMD 엔트리들 초기화 */
		last_map_addr = phys_pmd_init(pmd, addr, end, page_size_mask,
					      prot);
		unmap_low_page(pmd);

		spin_lock(&init_mm.page_table_lock);
		/* 흔한 페이징 */
		pud_populate(&init_mm, pud, __va(pmd_phys));
		spin_unlock(&init_mm.page_table_lock);
	}
	__flush_tlb_all();
	/* 1G 페이징 카운트 */
	update_page_count(PG_LEVEL_1G, pages);

	return last_map_addr;
}
/* pgd부터 순회 방문 하면서 테이블을 초기화하고 필요하면 테이블을 할당한다. */
unsigned long __meminit
kernel_physical_mapping_init(unsigned long start,
			     unsigned long end,
			     unsigned long page_size_mask)
{
	bool pgd_changed = false;
	unsigned long next, last_map_addr = end;
	unsigned long addr;
	start = (unsigned long)__va(start);
	end = (unsigned long)__va(end);
	addr = start;

	for (; start < end; start = next) {
		/* 커널의 해당 pgd 오프셋을 구한다. */
		pgd_t *pgd = pgd_offset_k(start);
		unsigned long pud_phys;
		pud_t *pud;
		/* PGD단위로(512G) 증가 (나머지 버림) */
		next = (start + PGDIR_SIZE) & PGDIR_MASK;
		/* 다음 엔트리 크기가 end값보다 크면 end */
		if (next > end)
			next = end;
		/* 해당 엔트리 값(pud)이 있으면 그대로 쓴다. */
		if (pgd_val(*pgd)) {
			/* pud 엔트리들이 있는 물리 메모리를 mapping 한다. */
			pud = map_low_page((pud_t *)pgd_page_vaddr(*pgd));
			last_map_addr = phys_pud_init(pud, __pa(start),
						 __pa(end), page_size_mask);
			unmap_low_page(pud);
			continue;
		}
		/* 페이지가 없으면 pgt_buffer로 할당 */
		pud = alloc_low_page(&pud_phys);
		last_map_addr = phys_pud_init(pud, __pa(start), __pa(next),
						 page_size_mask);
		unmap_low_page(pud);

		spin_lock(&init_mm.page_table_lock);
		pgd_populate(&init_mm, pgd, __va(pud_phys));
		spin_unlock(&init_mm.page_table_lock);
		pgd_changed = true;
	}

	if (pgd_changed)
		sync_global_pgds(addr, end);

	__flush_tlb_all();

	return last_map_addr;
}

#ifndef CONFIG_NUMA
void __init initmem_init(void)
{
	memblock_x86_register_active_regions(0, 0, max_pfn);
}
#endif

void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
#ifdef CONFIG_ZONE_DMA
	max_zone_pfns[ZONE_DMA] = MAX_DMA_PFN; // ZONE_DMA = 0, MAX_DMA_PFN = 2^12
#endif
	max_zone_pfns[ZONE_DMA32] = MAX_DMA32_PFN; // ZONE_DMA32 = 1, MAX_DMA32_PFN =  2^20
	max_zone_pfns[ZONE_NORMAL] = max_pfn; // ZONE_NORMAL = 2, max_pfn = e820_end_of_ram_pfn();

	sparse_memory_present_with_active_regions(MAX_NUMNODES); // MAX_NUMNODES = 2^6_init();

	/*
	 * clear the default setting with node 0
	 * note: don't use nodes_clear here, that is really clearing when
	 *	 numa support is not compiled in, and later node_set_state
	 *	 will not set it back.
	 */
  /* FIXME: N_NORMAL_MEMORY는 기본으로 0으로 설정된 상태인데??? */
	node_clear_state(0, N_NORMAL_MEMORY);

	free_area_init_nodes(max_zone_pfns);
}

/*
 * Memory hotplug specific functions
 */
#ifdef CONFIG_MEMORY_HOTPLUG
/*
 * After memory hotplug the variables max_pfn, max_low_pfn and high_memory need
 * updating.
 */
static void  update_end_of_memory_vars(u64 start, u64 size)
{
	unsigned long end_pfn = PFN_UP(start + size);

	if (end_pfn > max_pfn) {
		max_pfn = end_pfn;
		max_low_pfn = end_pfn;
		high_memory = (void *)__va(max_pfn * PAGE_SIZE - 1) + 1;
	}
}

/*
 * Memory is added always to NORMAL zone. This means you will never get
 * additional DMA/DMA32 memory.
 */
int arch_add_memory(int nid, u64 start, u64 size)
{
	struct pglist_data *pgdat = NODE_DATA(nid);
	struct zone *zone = pgdat->node_zones + ZONE_NORMAL;
	unsigned long last_mapped_pfn, start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int ret;

	last_mapped_pfn = init_memory_mapping(start, start + size);
	if (last_mapped_pfn > max_pfn_mapped)
		max_pfn_mapped = last_mapped_pfn;

	ret = __add_pages(nid, zone, start_pfn, nr_pages);
	WARN_ON_ONCE(ret);

	/* update max_pfn, max_low_pfn and high_memory */
	update_end_of_memory_vars(start, size);

	return ret;
}
EXPORT_SYMBOL_GPL(arch_add_memory);

#endif /* CONFIG_MEMORY_HOTPLUG */

static struct kcore_list kcore_vsyscall;

void __init mem_init(void)
{
	long codesize, reservedpages, datasize, initsize;
	unsigned long absent_pages;

	pci_iommu_alloc();

	/* clear_bss() already clear the empty_zero_page */

	reservedpages = 0;

	/* this will put all low memory onto the freelists */
#ifdef CONFIG_NUMA
	totalram_pages = numa_free_all_bootmem();
#else
	totalram_pages = free_all_bootmem();
#endif

	absent_pages = absent_pages_in_range(0, max_pfn);
	reservedpages = max_pfn - totalram_pages - absent_pages;
	after_bootmem = 1;

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	/* Register memory areas for /proc/kcore */
	kclist_add(&kcore_vsyscall, (void *)VSYSCALL_START,
			 VSYSCALL_END - VSYSCALL_START, KCORE_OTHER);

	printk(KERN_INFO "Memory: %luk/%luk available (%ldk kernel code, "
			 "%ldk absent, %ldk reserved, %ldk data, %ldk init)\n",
		nr_free_pages() << (PAGE_SHIFT-10),
		max_pfn << (PAGE_SHIFT-10),
		codesize >> 10,
		absent_pages << (PAGE_SHIFT-10),
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		initsize >> 10);
}

#ifdef CONFIG_DEBUG_RODATA
const int rodata_test_data = 0xC3;
EXPORT_SYMBOL_GPL(rodata_test_data);

int kernel_set_to_readonly;

void set_kernel_text_rw(void)
{
	unsigned long start = PFN_ALIGN(_text);
	unsigned long end = PFN_ALIGN(__stop___ex_table);

	if (!kernel_set_to_readonly)
		return;

	pr_debug("Set kernel text: %lx - %lx for read write\n",
		 start, end);

	/*
	 * Make the kernel identity mapping for text RW. Kernel text
	 * mapping will always be RO. Refer to the comment in
	 * static_protections() in pageattr.c
	 */
	set_memory_rw(start, (end - start) >> PAGE_SHIFT);
}

void set_kernel_text_ro(void)
{
	unsigned long start = PFN_ALIGN(_text);
	unsigned long end = PFN_ALIGN(__stop___ex_table);

	if (!kernel_set_to_readonly)
		return;

	pr_debug("Set kernel text: %lx - %lx for read only\n",
		 start, end);

	/*
	 * Set the kernel identity mapping for text RO.
	 */
	set_memory_ro(start, (end - start) >> PAGE_SHIFT);
}

void mark_rodata_ro(void)
{
	unsigned long start = PFN_ALIGN(_text);
	unsigned long rodata_start =
		((unsigned long)__start_rodata + PAGE_SIZE - 1) & PAGE_MASK;
	unsigned long end = (unsigned long) &__end_rodata_hpage_align;
	unsigned long text_end = PAGE_ALIGN((unsigned long) &__stop___ex_table);
	unsigned long rodata_end = PAGE_ALIGN((unsigned long) &__end_rodata);
	unsigned long data_start = (unsigned long) &_sdata;

	printk(KERN_INFO "Write protecting the kernel read-only data: %luk\n",
	       (end - start) >> 10);
	set_memory_ro(start, (end - start) >> PAGE_SHIFT);

	kernel_set_to_readonly = 1;

	/*
	 * The rodata section (but not the kernel text!) should also be
	 * not-executable.
	 */
	set_memory_nx(rodata_start, (end - rodata_start) >> PAGE_SHIFT);

	rodata_test();

#ifdef CONFIG_CPA_DEBUG
	printk(KERN_INFO "Testing CPA: undo %lx-%lx\n", start, end);
	set_memory_rw(start, (end-start) >> PAGE_SHIFT);

	printk(KERN_INFO "Testing CPA: again\n");
	set_memory_ro(start, (end-start) >> PAGE_SHIFT);
#endif

	free_init_pages("unused kernel memory",
			(unsigned long) page_address(virt_to_page(text_end)),
			(unsigned long)
				 page_address(virt_to_page(rodata_start)));
	free_init_pages("unused kernel memory",
			(unsigned long) page_address(virt_to_page(rodata_end)),
			(unsigned long) page_address(virt_to_page(data_start)));
}

#endif

int kern_addr_valid(unsigned long addr)
{
	unsigned long above = ((long)addr) >> __VIRTUAL_MASK_SHIFT;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	if (above != 0 && above != -1UL)
		return 0;

	pgd = pgd_offset_k(addr);
	if (pgd_none(*pgd))
		return 0;

	pud = pud_offset(pgd, addr);
	if (pud_none(*pud))
		return 0;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return 0;

	if (pmd_large(*pmd))
		return pfn_valid(pmd_pfn(*pmd));

	pte = pte_offset_kernel(pmd, addr);
	if (pte_none(*pte))
		return 0;

	return pfn_valid(pte_pfn(*pte));
}

/*
 * A pseudo VMA to allow ptrace access for the vsyscall page.  This only
 * covers the 64bit vsyscall page now. 32bit has a real VMA now and does
 * not need special handling anymore:
 */
static struct vm_area_struct gate_vma = {
	.vm_start	= VSYSCALL_START,
	.vm_end		= VSYSCALL_START + (VSYSCALL_MAPPED_PAGES * PAGE_SIZE),
	.vm_page_prot	= PAGE_READONLY_EXEC,
	.vm_flags	= VM_READ | VM_EXEC
};

struct vm_area_struct *get_gate_vma(struct mm_struct *mm)
{
#ifdef CONFIG_IA32_EMULATION
	if (!mm || mm->context.ia32_compat)
		return NULL;
#endif
	return &gate_vma;
}

int in_gate_area(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma = get_gate_vma(mm);

	if (!vma)
		return 0;

	return (addr >= vma->vm_start) && (addr < vma->vm_end);
}

/*
 * Use this when you have no reliable mm, typically from interrupt
 * context. It is less reliable than using a task's mm and may give
 * false positives.
 */
int in_gate_area_no_mm(unsigned long addr)
{
	return (addr >= VSYSCALL_START) && (addr < VSYSCALL_END);
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == (long)vma->vm_mm->context.vdso)
		return "[vdso]";
	if (vma == &gate_vma)
		return "[vsyscall]";
	return NULL;
}

#ifdef CONFIG_X86_UV
unsigned long memory_block_size_bytes(void)
{
	if (is_uv_system()) {
		printk(KERN_INFO "UV: memory block size 2GB\n");
		return 2UL * 1024 * 1024 * 1024;
	}
	return MIN_MEMORY_BLOCK_SIZE;
}
#endif

#ifdef CONFIG_SPARSEMEM_VMEMMAP
/*
 * Initialise the sparsemem vmemmap using huge-pages at the PMD level.
 */
static long __meminitdata addr_start, addr_end;
static void __meminitdata *p_start, *p_end;
static int __meminitdata node_start;

int __meminit
vmemmap_populate(struct page *start_page, unsigned long size, int node)
{
	unsigned long addr = (unsigned long)start_page;
	unsigned long end = (unsigned long)(start_page + size);
	unsigned long next;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	for (; addr < end; addr = next) {
		void *p = NULL;

		pgd = vmemmap_pgd_populate(addr, node);
		if (!pgd)
			return -ENOMEM;

		pud = vmemmap_pud_populate(pgd, addr, node);
		if (!pud)
			return -ENOMEM;

		if (!cpu_has_pse) {
			next = (addr + PAGE_SIZE) & PAGE_MASK;
			pmd = vmemmap_pmd_populate(pud, addr, node);

			if (!pmd)
				return -ENOMEM;

			p = vmemmap_pte_populate(pmd, addr, node);

			if (!p)
				return -ENOMEM;

			addr_end = addr + PAGE_SIZE;
			p_end = p + PAGE_SIZE;
		} else {
			next = pmd_addr_end(addr, end);

			pmd = pmd_offset(pud, addr);
			if (pmd_none(*pmd)) {
				pte_t entry;

				p = vmemmap_alloc_block_buf(PMD_SIZE, node);
				if (!p)
					return -ENOMEM;

				entry = pfn_pte(__pa(p) >> PAGE_SHIFT,
						PAGE_KERNEL_LARGE);
				set_pmd(pmd, __pmd(pte_val(entry)));

				/* check to see if we have contiguous blocks */
				if (p_end != p || node_start != node) {
					if (p_start)
						printk(KERN_DEBUG " [%lx-%lx] PMD -> [%p-%p] on node %d\n",
						       addr_start, addr_end-1, p_start, p_end-1, node_start);
					addr_start = addr;
					node_start = node;
					p_start = p;
				}

				addr_end = addr + PMD_SIZE;
				p_end = p + PMD_SIZE;
			} else
				vmemmap_verify((pte_t *)pmd, node, addr, next);
		}

	}
	sync_global_pgds((unsigned long)start_page, end);
	return 0;
}

void __meminit vmemmap_populate_print_last(void)
{
	if (p_start) {
		printk(KERN_DEBUG " [%lx-%lx] PMD -> [%p-%p] on node %d\n",
			addr_start, addr_end-1, p_start, p_end-1, node_start);
		p_start = NULL;
		p_end = NULL;
		node_start = 0;
	}
}
#endif
