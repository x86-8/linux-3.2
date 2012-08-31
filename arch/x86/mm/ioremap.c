/*
 * Re-map IO memory to kernel address space so that we can access it.
 * This is needed for high PCI addresses that aren't mapped in the
 * 640k-1MB IO memory area on PC's
 *
 * (C) Copyright 1995 1996 Linus Torvalds
 */

#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mmiotrace.h>

#include <asm/cacheflush.h>
#include <asm/e820.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/pgalloc.h>
#include <asm/pat.h>

#include "physaddr.h"

/*
 * Fix up the linear direct mapping of the kernel to avoid cache attribute
 * conflicts.
 */
int ioremap_change_attr(unsigned long vaddr, unsigned long size,
			       unsigned long prot_val)
{
	unsigned long nrpages = size >> PAGE_SHIFT;
	int err;

	switch (prot_val) {
	case _PAGE_CACHE_UC:
	default:
		err = _set_memory_uc(vaddr, nrpages);
		break;
	case _PAGE_CACHE_WC:
		err = _set_memory_wc(vaddr, nrpages);
		break;
	case _PAGE_CACHE_WB:
		err = _set_memory_wb(vaddr, nrpages);
		break;
	}

	return err;
}

/*
 * Remap an arbitrary physical address space into the kernel virtual
 * address space. Needed when the kernel wants to access high addresses
 * directly.
 *
 * NOTE! We need to allow non-page-aligned mappings too: we will obviously
 * have to convert them into an offset in a page-aligned mapping, but the
 * caller shouldn't need to know that small detail.
 */
static void __iomem *__ioremap_caller(resource_size_t phys_addr,
		unsigned long size, unsigned long prot_val, void *caller)
{
	unsigned long offset, vaddr;
	resource_size_t pfn, last_pfn, last_addr;
	const resource_size_t unaligned_phys_addr = phys_addr;
	const unsigned long unaligned_size = size;
	struct vm_struct *area;
	unsigned long new_prot_val;
	pgprot_t prot;
	int retval;
	void __iomem *ret_addr;

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	/* 마지막 주소가 시작주소보다 작거나 사이즈가 0이면 */
	if (!size || last_addr < phys_addr)
		return NULL;

	if (!phys_addr_valid(phys_addr)) {
		printk(KERN_WARNING "ioremap: invalid physical address %llx\n",
		       (unsigned long long)phys_addr);
		WARN_ON_ONCE(1);
		return NULL;
	}

	/*
	 * Don't remap the low PCI/ISA area, it's always mapped..
	 */
	/* 16M이하 영역이면 remap하지 않는다. */
	if (is_ISA_range(phys_addr, last_addr))
		return (__force void __iomem *)phys_to_virt(phys_addr);

	/*
	 * Don't allow anybody to remap normal RAM that we're using..
	 */
	last_pfn = last_addr >> PAGE_SHIFT;
	/* 시작주소부터 페이지 단위로 io자원에 System RAM으로 된 부분이 있는지 검사 한다. */
	for (pfn = phys_addr >> PAGE_SHIFT; pfn <= last_pfn; pfn++) {
		int is_ram = page_is_ram(pfn);

		if (is_ram && pfn_valid(pfn) && !PageReserved(pfn_to_page(pfn)))
			return NULL;
		WARN_ON_ONCE(is_ram);
	}

	/*
	 * Mappings have to be page-aligned
	 */
	/* 하위 12비트와 그 윗부분을 각각 mask한다. */
	offset = phys_addr & ~PAGE_MASK;
	phys_addr &= PHYSICAL_PAGE_MASK;
	size = PAGE_ALIGN(last_addr+1) - phys_addr;
	/* 메모리 타입영역 할당? */
	retval = reserve_memtype(phys_addr, (u64)phys_addr + size,
						prot_val, &new_prot_val);
	if (retval) {
		printk(KERN_ERR "ioremap reserve_memtype failed %d\n", retval);
		return NULL;
	}

	if (prot_val != new_prot_val) {
		if (!is_new_memtype_allowed(phys_addr, size,
					    prot_val, new_prot_val)) {
			printk(KERN_ERR
		"ioremap error for 0x%llx-0x%llx, requested 0x%lx, got 0x%lx\n",
				(unsigned long long)phys_addr,
				(unsigned long long)(phys_addr + size),
				prot_val, new_prot_val);
			goto err_free_memtype;
		}
		prot_val = new_prot_val;
	}

	switch (prot_val) {
	case _PAGE_CACHE_UC:
	default:
		prot = PAGE_KERNEL_IO_NOCACHE;
		break;
	case _PAGE_CACHE_UC_MINUS:
		prot = PAGE_KERNEL_IO_UC_MINUS;
		break;
	case _PAGE_CACHE_WC:
		prot = PAGE_KERNEL_IO_WC;
		break;
	case _PAGE_CACHE_WB:
		prot = PAGE_KERNEL_IO;
		break;
	}

	/*
	 * Ok, go for it..
	 */
	area = get_vm_area_caller(size, VM_IOREMAP, caller);
	if (!area)
		goto err_free_memtype;
	area->phys_addr = phys_addr;
	vaddr = (unsigned long) area->addr;

	if (kernel_map_sync_memtype(phys_addr, size, prot_val))
		goto err_free_area;

	if (ioremap_page_range(vaddr, vaddr + size, phys_addr, prot))
		goto err_free_area;

	ret_addr = (void __iomem *) (vaddr + offset);
	mmiotrace_ioremap(unaligned_phys_addr, unaligned_size, ret_addr);

	/*
	 * Check if the request spans more than any BAR in the iomem resource
	 * tree.
	 */
	WARN_ONCE(iomem_map_sanity_check(unaligned_phys_addr, unaligned_size),
		  KERN_INFO "Info: mapping multiple BARs. Your kernel is fine.");

	return ret_addr;
err_free_area:
	free_vm_area(area);
err_free_memtype:
	free_memtype(phys_addr, phys_addr + size);
	return NULL;
}

/**
 * ioremap_nocache     -   map bus memory into CPU space
 * @offset:    bus address of the memory
 * @size:      size of the resource to map
 *
 * ioremap_nocache performs a platform specific sequence of operations to
 * make bus memory CPU accessible via the readb/readw/readl/writeb/
 * writew/writel functions and the other mmio helpers. The returned
 * address is not guaranteed to be usable directly as a virtual
 * address.
 *
 * This version of ioremap ensures that the memory is marked uncachable
 * on the CPU as well as honouring existing caching rules from things like
 * the PCI bus. Note that there are other caches and buffers on many
 * busses. In particular driver authors should read up on PCI writes
 *
 * It's useful if some control registers are in such an area and
 * write combining or read caching is not desirable:
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap_nocache(resource_size_t phys_addr, unsigned long size)
{
	/*
	 * Ideally, this should be:
	 *	pat_enabled ? _PAGE_CACHE_UC : _PAGE_CACHE_UC_MINUS;
	 *
	 * Till we fix all X drivers to use ioremap_wc(), we will use
	 * UC MINUS.
	 */
	unsigned long val = _PAGE_CACHE_UC_MINUS;

	return __ioremap_caller(phys_addr, size, val,
				__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_nocache);

/**
 * ioremap_wc	-	map memory into CPU space write combined
 * @offset:	bus address of the memory
 * @size:	size of the resource to map
 *
 * This version of ioremap ensures that the memory is marked write combining.
 * Write combining allows faster writes to some hardware devices.
 *
 * Must be freed with iounmap.
 */
void __iomem *ioremap_wc(resource_size_t phys_addr, unsigned long size)
{
	if (pat_enabled)
		return __ioremap_caller(phys_addr, size, _PAGE_CACHE_WC,
					__builtin_return_address(0));
	else
		return ioremap_nocache(phys_addr, size);
}
EXPORT_SYMBOL(ioremap_wc);

void __iomem *ioremap_cache(resource_size_t phys_addr, unsigned long size)
{
	return __ioremap_caller(phys_addr, size, _PAGE_CACHE_WB,
				__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_cache);

void __iomem *ioremap_prot(resource_size_t phys_addr, unsigned long size,
				unsigned long prot_val)
{
	return __ioremap_caller(phys_addr, size, (prot_val & _PAGE_CACHE_MASK),
				__builtin_return_address(0));
}
EXPORT_SYMBOL(ioremap_prot);

/**
 * iounmap - Free a IO remapping
 * @addr: virtual address from ioremap_*
 *
 * Caller must ensure there is only one unmapping for the same pointer.
 */
void iounmap(volatile void __iomem *addr)
{
	struct vm_struct *p, *o;

	if ((void __force *)addr <= high_memory)
		return;

	/*
	 * __ioremap special-cases the PCI/ISA range by not instantiating a
	 * vm_area and by simply returning an address into the kernel mapping
	 * of ISA space.   So handle that here.
	 */
	if ((void __force *)addr >= phys_to_virt(ISA_START_ADDRESS) &&
	    (void __force *)addr < phys_to_virt(ISA_END_ADDRESS))
		return;

	addr = (volatile void __iomem *)
		(PAGE_MASK & (unsigned long __force)addr);

	mmiotrace_iounmap(addr);

	/* Use the vm area unlocked, assuming the caller
	   ensures there isn't another iounmap for the same address
	   in parallel. Reuse of the virtual address is prevented by
	   leaving it in the global lists until we're done with it.
	   cpa takes care of the direct mappings. */
	read_lock(&vmlist_lock);
	for (p = vmlist; p; p = p->next) {
		if (p->addr == (void __force *)addr)
			break;
	}
	read_unlock(&vmlist_lock);

	if (!p) {
		printk(KERN_ERR "iounmap: bad address %p\n", addr);
		dump_stack();
		return;
	}

	free_memtype(p->phys_addr, p->phys_addr + get_vm_area_size(p));

	/* Finally remove it */
	o = remove_vm_area((void __force *)addr);
	BUG_ON(p != o || o == NULL);
	kfree(p);
}
EXPORT_SYMBOL(iounmap);

/*
 * Convert a physical pointer to a virtual kernel pointer for /dev/mem
 * access
 */
void *xlate_dev_mem_ptr(unsigned long phys)
{
	void *addr;
	unsigned long start = phys & PAGE_MASK;

	/* If page is RAM, we can use __va. Otherwise ioremap and unmap. */
	if (page_is_ram(start >> PAGE_SHIFT))
		return __va(phys);

	addr = (void __force *)ioremap_cache(start, PAGE_SIZE);
	if (addr)
		addr = (void *)((unsigned long)addr | (phys & ~PAGE_MASK));

	return addr;
}

void unxlate_dev_mem_ptr(unsigned long phys, void *addr)
{
	if (page_is_ram(phys >> PAGE_SHIFT))
		return;

	iounmap((void __iomem *)((unsigned long)addr & PAGE_MASK));
	return;
}

static int __initdata early_ioremap_debug;

static int __init early_ioremap_debug_setup(char *str)
{
	early_ioremap_debug = 1;

	return 0;
}
early_param("early_ioremap_debug", early_ioremap_debug_setup);

static __initdata int after_paging_init;

/*
 * 1페이지 크기만큼 pte변수를 생성
 * 64비트에서는 512개
 */
static pte_t bm_pte[PAGE_SIZE/sizeof(pte_t)] __page_aligned_bss;

static inline pmd_t * __init early_ioremap_pmd(unsigned long addr)
{
	/* Don't assume we're using swapper_pg_dir at this point */
	// 가상주소 영역으로 바꾼다.
	pgd_t *base = __va(read_cr3());
	pgd_t *pgd = &base[pgd_index(addr)];
	pud_t *pud = pud_offset(pgd, addr);
	pmd_t *pmd = pmd_offset(pud, addr);
	return pmd; // pmd 값(lv2) 를 구해 리턴
}

/* pte엔트리[addr의엔트리 번호] */
static inline pte_t * __init early_ioremap_pte(unsigned long addr)
{
	return &bm_pte[pte_index(addr)];
}

bool __init is_early_ioremap_ptep(pte_t *ptep)
{
	return ptep >= &bm_pte[0] && ptep < &bm_pte[PAGE_SIZE/sizeof(pte_t)];
}

/** 
 *  slot 4개 (long) (FIX_BTMAPS_SLOTS : 64 * 4)
 */
static unsigned long slot_virt[FIX_BTMAPS_SLOTS] __initdata;

void __init early_ioremap_init(void)
{
	pmd_t *pmd;
	int i;

	if (early_ioremap_debug)
		printk(KERN_INFO "early_ioremap_init()\n");

	/* slot_virt  배열(4번)만큼 실행  */
	for (i = 0; i < FIX_BTMAPS_SLOTS; i++)
		/* 0이면 0번째 BTMAP 가상주소 1이면 64번째 페이지 가상주소.... */
		slot_virt[i] = __fix_to_virt(FIX_BTMAP_BEGIN - NR_FIX_BTMAPS*i);

	// FIX_BTMAP_BEGIN의 pmd를 구한다.
	pmd = early_ioremap_pmd(fix_to_virt(FIX_BTMAP_BEGIN));
	// 비트맵 페이지 초기화
	memset(bm_pte, 0, sizeof(bm_pte));
	pmd_populate_kernel(&init_mm, pmd, bm_pte);

	/*
	 * The boot-ioremap range spans multiple pmds, for which
	 * we are not prepared:
	 */
#define __FIXADDR_TOP (-PAGE_SIZE)
	// 시작과 끝 검사
	BUILD_BUG_ON((__fix_to_virt(FIX_BTMAP_BEGIN) >> PMD_SHIFT)
		     != (__fix_to_virt(FIX_BTMAP_END) >> PMD_SHIFT));
#undef __FIXADDR_TOP
	if (pmd != early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END))) {
		WARN_ON(1);
		printk(KERN_WARNING "pmd %p != %p\n",
		       pmd, early_ioremap_pmd(fix_to_virt(FIX_BTMAP_END)));
		printk(KERN_WARNING "fix_to_virt(FIX_BTMAP_BEGIN): %08lx\n",
			fix_to_virt(FIX_BTMAP_BEGIN));
		printk(KERN_WARNING "fix_to_virt(FIX_BTMAP_END):   %08lx\n",
			fix_to_virt(FIX_BTMAP_END));

		printk(KERN_WARNING "FIX_BTMAP_END:       %d\n", FIX_BTMAP_END);
		printk(KERN_WARNING "FIX_BTMAP_BEGIN:     %d\n",
		       FIX_BTMAP_BEGIN);
	}
}

void __init early_ioremap_reset(void)
{
	after_paging_init = 1;
}

static void __init __early_set_fixmap(enum fixed_addresses idx,
				      phys_addr_t phys, pgprot_t flags)
{
	unsigned long addr = __fix_to_virt(idx); /* 페이지 번호를 거꾸로 가장주소에 매핑 */
	pte_t *pte;

	if (idx >= __end_of_fixed_addresses) { /* 최대값을 넘어가면 실패! */
		BUG();
		return;
	}
	pte = early_ioremap_pte(addr); /* pte 엔트리 주소를 넣어준다. */
	/* flags는 페이지의 속성
	 * 속성이 있으면 세팅
	 * 없으면 클리어
	 */
	if (pgprot_val(flags))
		set_pte(pte, pfn_pte(phys >> PAGE_SHIFT, flags)); /* pte 세팅 */
	else
		pte_clear(&init_mm, addr, pte);
	__flush_tlb_one(addr);		/* tlb 무효화 */
}

static inline void __init early_set_fixmap(enum fixed_addresses idx,
					   phys_addr_t phys, pgprot_t prot)
{
 /* 페이징 초기화 하기 전에는 early다 */
	if (after_paging_init)
		__set_fixmap(idx, phys, prot);
	else
		__early_set_fixmap(idx, phys, prot);
}
/* clear는 해당하는 pte 엔트리의 물리주소와 prot를 0으로 set 해서 clear한다. */
static inline void __init early_clear_fixmap(enum fixed_addresses idx)
{
	if (after_paging_init)
		clear_fixmap(idx);
	else
		__early_set_fixmap(idx, 0, __pgprot(0));
}

static void __iomem *prev_map[FIX_BTMAPS_SLOTS] __initdata; /* __iomem은 에러검사용 */
static unsigned long prev_size[FIX_BTMAPS_SLOTS] __initdata;

void __init fixup_early_ioremap(void)
{
	int i;

	for (i = 0; i < FIX_BTMAPS_SLOTS; i++) {
		if (prev_map[i]) {
			WARN_ON(1);
			break;
		}
	}

	early_ioremap_init();
}

static int __init check_early_ioremap_leak(void)
{
	int count = 0;
	int i;

	for (i = 0; i < FIX_BTMAPS_SLOTS; i++)
		if (prev_map[i])
			count++;

	if (!count)
		return 0;
	WARN(1, KERN_WARNING
	       "Debug warning: early ioremap leak of %d areas detected.\n",
		count);
	printk(KERN_WARNING
		"please boot with early_ioremap_debug and report the dmesg.\n");

	return 1;
}
late_initcall(check_early_ioremap_leak);




/* 이 함수는 물리메모리를 인자로 받아서 메모리 끝부분에 있는 가상메모리,
 * fixed_addresses의 Boot-time map으로 할당한다.
 * slot은 현재 4개다. 4개의 슬롯중 비어있는 슬롯으로 phys_addr을 할당한다.
 * 리턴값은 들어온 물리주소를 매핑한 가상주소가 담긴 슬롯의 포인터이다.
 */
static void __init __iomem *
__early_ioremap(resource_size_t phys_addr, unsigned long size, pgprot_t prot)
{
	unsigned long offset;
	resource_size_t last_addr;
	unsigned int nrpages;
	enum fixed_addresses idx0, idx;
	int i, slot;

	WARN_ON(system_state != SYSTEM_BOOTING); /* enum으로 선언된 상태 ; 부팅중이 아니면 경고 */

	slot = -1;
	for (i = 0; i < FIX_BTMAPS_SLOTS; i++) { /* boot-time 매핑 슬롯 4개중 하나를 사용하려고 검색 */
	  if (!prev_map[i]) {					 /* slot은 null이 아닌값을 넣는다. */
			slot = i;
			break;
		}
	}

	if (slot < 0) {
		printk(KERN_INFO "early_iomap(%08llx, %08lx) not found slot\n",
			 (u64)phys_addr, size);
		WARN_ON(1);
		return NULL;
	}

	if (early_ioremap_debug) {
		printk(KERN_INFO "early_ioremap(%08llx, %08lx) [%d] => ",
		       (u64)phys_addr, size, slot);
		dump_stack();
	}

	/* Don't allow wraparound or zero size */
	last_addr = phys_addr + size - 1;
	if (!size || last_addr < phys_addr) {
		WARN_ON(1);
		return NULL;
	}

	prev_size[slot] = size;
	/*
	 * Mappings have to be page-aligned
	 */
	offset = phys_addr & ~PAGE_MASK; /* 4KB이하 주소만 떼낸다. */
	phys_addr &= PAGE_MASK;
	size = PAGE_ALIGN(last_addr + 1) - phys_addr;
	/* page 에서 남은 bytes size = 올림정렬 last_addr - 정렬된 phys_addr */

	/*
	 * Mappings have to fit in the FIX_BTMAP area.
	 */
	nrpages = size >> PAGE_SHIFT; /* 채워줄 페이지 갯수 */
	if (nrpages > NR_FIX_BTMAPS) { /* 한개의 BTMAP(64)을 초과하면 안된다. */
		WARN_ON(1);
		return NULL;
	}

	/*
	 * Ok, go for it..
	 */
	idx0 = FIX_BTMAP_BEGIN - NR_FIX_BTMAPS*slot; 
/* 현재 슬롯(한슬롯은 64 페이지)의 첫 페이지 번호
 * BEGIN에서 END는 일반적으로 생각하는 순서의 역순이다.
 * 그래서 -연산을 한다.
 */
   	idx = idx0;
	/* fixmap 공간을 pte와 매핑시킨다. fixmap은 물리적으로 연속된 공간에 할당 받는다. */
	while (nrpages > 0) {
		early_set_fixmap(idx, phys_addr, prot);
		phys_addr += PAGE_SIZE;
		--idx;				/* 페이지번호가 증가 */
		--nrpages;			/* 페이지 갯수 감소 */
	}
	/* slot_virt는 슬롯의 가상주소 */
	if (early_ioremap_debug)
		printk(KERN_CONT "%08lx + %08lx\n", offset, slot_virt[slot]);

	/* 페이징은 4KB단위라서 offset을 더해준다 */
	prev_map[slot] = (void __iomem *)(offset + slot_virt[slot]);
	return prev_map[slot];
}

/* Remap an IO device */
/* 사용자가 원하는 물리공간을 fixed_addresses의 끝부분인
 * slot(256KB 단위) 버퍼에 임시로 매핑시킨다.
 */
void __init __iomem *
early_ioremap(resource_size_t phys_addr, unsigned long size)
{
	return __early_ioremap(phys_addr, size, PAGE_KERNEL_IO);
}

/* Remap memory */
void __init __iomem *
early_memremap(resource_size_t phys_addr, unsigned long size)
{
	return __early_ioremap(phys_addr, size, PAGE_KERNEL);
}

void __init early_iounmap(void __iomem *addr, unsigned long size)
{
	unsigned long virt_addr;
	unsigned long offset;
	unsigned int nrpages;
	enum fixed_addresses idx;
	int i, slot;

	/* 주소가 같은 슬롯을 찾는다 */
	slot = -1;
	for (i = 0; i < FIX_BTMAPS_SLOTS; i++) {
		if (prev_map[i] == addr) {
			slot = i;
			break;
		}
	}
	/* 에러처리 */
	if (slot < 0) {
		printk(KERN_INFO "early_iounmap(%p, %08lx) not found slot\n",
			 addr, size);
		WARN_ON(1);
		return;
	}

	if (prev_size[slot] != size) {
		printk(KERN_INFO "early_iounmap(%p, %08lx) [%d] size not consistent %08lx\n",
			 addr, size, slot, prev_size[slot]);
		WARN_ON(1);
		return;
	}

	if (early_ioremap_debug) {
		printk(KERN_INFO "early_iounmap(%p, %08lx) [%d]\n", addr,
		       size, slot);
		dump_stack();
	}

	virt_addr = (unsigned long)addr;
	/* boot-map 한계를 넘으면 에러 */
	if (virt_addr < fix_to_virt(FIX_BTMAP_BEGIN)) {
		WARN_ON(1);
		return;
	}
	offset = virt_addr & ~PAGE_MASK;		   /* 주소 */
	nrpages = PAGE_ALIGN(offset + size) >> PAGE_SHIFT; /* 페이지 개수 */

	idx = FIX_BTMAP_BEGIN - NR_FIX_BTMAPS*slot; /* BEGIN에서 slot(64)만큼 뺀다 */
	while (nrpages > 0) {
		early_clear_fixmap(idx);
		--idx;
		--nrpages;
	}
	prev_map[slot] = NULL;
}
