#include <linux/gfp.h>
#include <linux/initrd.h>
#include <linux/ioport.h>
#include <linux/swap.h>
#include <linux/memblock.h>

#include <asm/cacheflush.h>
#include <asm/e820.h>
#include <asm/init.h>
#include <asm/page.h>
#include <asm/page_types.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/proto.h>

unsigned long __initdata pgt_buf_start;
unsigned long __meminitdata pgt_buf_end;
unsigned long __meminitdata pgt_buf_top;

int after_bootmem;
/* 꼼수스럽다. */
int direct_gbpages
#ifdef CONFIG_DIRECT_GBPAGES
				= 1
#endif
;

static void __init find_early_table_space(unsigned long end, int use_pse,
					  int use_gbpages)
{
	unsigned long puds, pmds, ptes, tables, start = 0, good_end = end;
	phys_addr_t base;
	/* 올림한 PUD 갯수 */
	puds = (end + PUD_SIZE - 1) >> PUD_SHIFT;
	/* pud갯수*8바이트 를 4KB단위로 올림 = tables */
	tables = roundup(puds * sizeof(pud_t), PAGE_SIZE);

	if (use_gbpages) {
		unsigned long extra;
		/* extra = PUD로 짜른 나머지값 */
		extra = end - ((end>>PUD_SHIFT) << PUD_SHIFT);
		/* 총 올림한 pmd갯수 */
		pmds = (extra + PMD_SIZE - 1) >> PMD_SHIFT;
	} else
		pmds = (end + PMD_SIZE - 1) >> PMD_SHIFT;

	tables += roundup(pmds * sizeof(pmd_t), PAGE_SIZE);

	if (use_pse) {
		unsigned long extra;

		extra = end - ((end>>PMD_SHIFT) << PMD_SHIFT);
#ifdef CONFIG_X86_32
		extra += PMD_SIZE;
#endif
		/* 올림한 pte 총값 */
		ptes = (extra + PAGE_SIZE - 1) >> PAGE_SHIFT;
	} else
		ptes = (end + PAGE_SIZE - 1) >> PAGE_SHIFT;
	/* 테이블은 페이지로 차지할 메모리 공간이다. 계속 더해준다 */
	tables += roundup(ptes * sizeof(pte_t), PAGE_SIZE);

#ifdef CONFIG_X86_32
	/* for fixmap */
	tables += roundup(__end_of_fixed_addresses * sizeof(pte_t), PAGE_SIZE);
#endif
	good_end = max_pfn_mapped << PAGE_SHIFT;

	base = memblock_find_in_range(start, good_end, tables, PAGE_SIZE);
	if (base == MEMBLOCK_ERROR)
		panic("Cannot find space for the kernel page tables");

	pgt_buf_start = base >> PAGE_SHIFT;
	pgt_buf_end = pgt_buf_start;
	pgt_buf_top = pgt_buf_start + (tables >> PAGE_SHIFT);

	printk(KERN_DEBUG "kernel direct mapping tables up to %lx @ %lx-%lx\n",
		end, pgt_buf_start << PAGE_SHIFT, pgt_buf_top << PAGE_SHIFT);
}

void __init native_pagetable_reserve(u64 start, u64 end)
{
	memblock_x86_reserve_range(start, end, "PGTABLE");
}

struct map_range {
	unsigned long start;
	unsigned long end;
	unsigned page_size_mask;
};

#ifdef CONFIG_X86_32
#define NR_RANGE_MR 3
#else /* CONFIG_X86_64 */
#define NR_RANGE_MR 5
#endif
/* mr블록에 인자로 들어온 값을 등록한다. */
static int __meminit save_mr(struct map_range *mr, int nr_range,
			     unsigned long start_pfn, unsigned long end_pfn,
			     unsigned long page_size_mask)
{
	if (start_pfn < end_pfn) {
		if (nr_range >= NR_RANGE_MR)
			panic("run out of range for init_memory_mapping\n");
		mr[nr_range].start = start_pfn<<PAGE_SHIFT;
		mr[nr_range].end   = end_pfn<<PAGE_SHIFT;
		mr[nr_range].page_size_mask = page_size_mask;
		nr_range++;
	}

	return nr_range;
}

/*
 * Setup the direct mapping of the physical memory at PAGE_OFFSET.
 * This runs before bootmem is initialized and gets pages directly from
 * the physical memory. To access them they are temporarily mapped.
 */
/* 64비트는 5번 메모리를 쪼갠다.
 * 처음 2M와 두번째 1G로 올림 해서 쪼개본다. start가 0이 오면 패스되는데
 * 세번째는 GB단위로 end까지 쪼갠다. 그다음에 2MB와 4KB단위로 쪼갠다.
 */
unsigned long __init_refok init_memory_mapping(unsigned long start,
					       unsigned long end)
{
	unsigned long page_size_mask = 0;
	unsigned long start_pfn, end_pfn;
	unsigned long ret = 0;
	unsigned long pos;

	struct map_range mr[NR_RANGE_MR];
	int nr_range, i;
	int use_pse, use_gbpages;
	/* 출력 */
	printk(KERN_INFO "init_memory_mapping: %016lx-%016lx\n", start, end);

#if defined(CONFIG_DEBUG_PAGEALLOC) || defined(CONFIG_KMEMCHECK)
	/*
	 * For CONFIG_DEBUG_PAGEALLOC, identity mapping will use small pages.
	 * This will simplify cpa(), which otherwise needs to support splitting
	 * large pages into small in interrupt context, etc.
	 */
	use_pse = use_gbpages = 0;
#else
	use_pse = cpu_has_pse;
	use_gbpages = direct_gbpages;
#endif

	/* Enable PSE if available */
	/* PSE가 가능하면 on */
	if (cpu_has_pse)
		set_in_cr4(X86_CR4_PSE);

	/* Enable PGE if available */
	/* CPU가 PGE를 지원하면 켜주고 mask해도 켤수있게 한다.  */
	if (cpu_has_pge) {
		set_in_cr4(X86_CR4_PGE);
		__supported_pte_mask |= _PAGE_GLOBAL;
	}
	/* 지원하면 해당번째(enum) 비트를 켠다. */
	/* 테이블 엔트리의 PAE비트가 1이면 1G
	 * 0일때 PSE가 0이면 4KB, 1이면 2M 페이징이다
	 */
	if (use_gbpages)
		page_size_mask |= 1 << PG_LEVEL_1G;
	if (use_pse)
		page_size_mask |= 1 << PG_LEVEL_2M;
	/* map_range를 0으로 초기화 */
	memset(mr, 0, sizeof(mr));
	nr_range = 0;

	/* head if not big page alignment ? */
	start_pfn = start >> PAGE_SHIFT; /* 시작 페이지 프레임 */
	pos = start_pfn << PAGE_SHIFT;	 /* 페이지 단위로 버림된 오프셋 */
#ifdef CONFIG_X86_32
	/*
	 * Don't use a large page for the first 2/4MB of memory
	 * because there are often fixed size MTRRs in there
	 * and overlapping MTRRs into large pages can cause
	 * slowdowns.
	 */
	if (pos == 0)		/* 32비트면 0이어도 end_pfn는 1이다. */
		end_pfn = 1<<(PMD_SHIFT - PAGE_SHIFT);
	else
		end_pfn = ((pos + (PMD_SIZE - 1))>>PMD_SHIFT)
				 << (PMD_SHIFT - PAGE_SHIFT);
#else /* CONFIG_X86_64 */
	/* PMD로 올림후 페이지 번호 구함
	 * PMD단위(512개의 PTE엔트리)로 페이지 갯수를 구한다
	 */
	end_pfn = ((pos + (PMD_SIZE - 1)) >> PMD_SHIFT)
		<< (PMD_SHIFT - PAGE_SHIFT); /*21-12=9*/
#endif
	/* PMD 한블럭보다 작으면 작은 값을 취하고
	 * 한블럭이 넘으면 PMD 한 블럭씩 처리한다.
	 */
	if (end_pfn > (end >> PAGE_SHIFT))
		end_pfn = end >> PAGE_SHIFT;
	/* 아래는 정상적인 경우 save_mr을 실행
	 * 0이면 서로 같기때문에 save_mr이 실행되지 않는다.
	 */
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn, 0);
		pos = end_pfn << PAGE_SHIFT;
	}

	/* big page (2M) range */
	/* 0이 들어왔다면 또 0, 아니면 위에서 끝난값 */
	start_pfn = ((pos + (PMD_SIZE - 1))>>PMD_SHIFT)
			 << (PMD_SHIFT - PAGE_SHIFT);
#ifdef CONFIG_X86_32
	/* end의 페이지 넘버
	 * 32비트는 2MB 단위 하지만 64비트는 1GB 단위
	 */
	end_pfn = (end>>PMD_SHIFT) << (PMD_SHIFT - PAGE_SHIFT);
#else /* CONFIG_X86_64 */
	/* PUD단위(1G)로 올림한 페이지 넘버를 구한다.
	 * start값이 0이라면 여기서도 실행되지 않는다
	 */
	end_pfn = ((pos + (PUD_SIZE - 1))>>PUD_SHIFT)
			 << (PUD_SHIFT - PAGE_SHIFT);
	/* 마지막 블럭이면 작은값 */
	if (end_pfn > ((end>>PMD_SHIFT)<<(PMD_SHIFT - PAGE_SHIFT)))
		end_pfn = ((end>>PMD_SHIFT)<<(PMD_SHIFT - PAGE_SHIFT));
#endif

	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask & (1<<PG_LEVEL_2M));
		pos = end_pfn << PAGE_SHIFT;
	}
	/* 0으로 들어와도 GB(PUD)단위로 쪼개서 mr 배열에 넣는다. */
#ifdef CONFIG_X86_64
	/* big page (1G) range */
	start_pfn = ((pos + (PUD_SIZE - 1))>>PUD_SHIFT)
			 << (PUD_SHIFT - PAGE_SHIFT);
	end_pfn = (end >> PUD_SHIFT) << (PUD_SHIFT - PAGE_SHIFT);
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask &
				 ((1<<PG_LEVEL_2M)|(1<<PG_LEVEL_1G)));
		pos = end_pfn << PAGE_SHIFT; /* GB단위의 마지막값 */
	}

	/* tail is not big page (1G) alignment */
	/* 1GB로 짤리지 못한 메모리를 PMD(2M)값으로 쪼갠다. */
	start_pfn = ((pos + (PMD_SIZE - 1))>>PMD_SHIFT)
			 << (PMD_SHIFT - PAGE_SHIFT);
	end_pfn = (end >> PMD_SHIFT) << (PMD_SHIFT - PAGE_SHIFT);
	if (start_pfn < end_pfn) {
		nr_range = save_mr(mr, nr_range, start_pfn, end_pfn,
				page_size_mask & (1<<PG_LEVEL_2M));
		pos = end_pfn << PAGE_SHIFT; /* 역시 2MB 단위의 마지막값 */
	}
#endif

	/* tail is not big page (2M) alignment */
	/* 2M으로 짤리지 못한 값을 Page 크기 값으로 쪼갠다. */
	start_pfn = pos>>PAGE_SHIFT;
	end_pfn = end>>PAGE_SHIFT;
	nr_range = save_mr(mr, nr_range, start_pfn, end_pfn, 0);

	/* try to merge same page size and continuous */
	/* 배열이 2개 이상일때 이어지는 주소를 합친다. */
	for (i = 0; nr_range > 1 && i < nr_range - 1; i++) {
		unsigned long old_start;
		if (mr[i].end != mr[i+1].start ||
		    mr[i].page_size_mask != mr[i+1].page_size_mask)
			continue;
		/* i와 end 다음 start가 이어지고 page_size가 동일하면 아래 코드를 실행 */
		/* move it */
		old_start = mr[i].start;
		/* 메모리를 복사 */
		memmove(&mr[i], &mr[i+1],
			(nr_range - 1 - i) * sizeof(struct map_range));
		mr[i--].start = old_start; /* start값을 이전값으로 해서 merge한다. */
		nr_range--;		   /* count는 감소 */
	}
	/* 갯수 페이지 레벨 별로 프린트 */
	for (i = 0; i < nr_range; i++)
		printk(KERN_DEBUG " %010lx - %010lx page %s\n",
				mr[i].start, mr[i].end,
			(mr[i].page_size_mask & (1<<PG_LEVEL_1G))?"1G":(
			 (mr[i].page_size_mask & (1<<PG_LEVEL_2M))?"2M":"4k"));

	/*
	 * Find space for the kernel direct mapping tables.
	 *
	 * Later we should allocate these tables in the local node of the
	 * memory mapped. Unfortunately this is done currently before the
	 * nodes are discovered.
	 */
	/* 초기화되지 않았으면 실행된다. */
	if (!after_bootmem)
		find_early_table_space(end, use_pse, use_gbpages);

	for (i = 0; i < nr_range; i++)
		ret = kernel_physical_mapping_init(mr[i].start, mr[i].end,
						   mr[i].page_size_mask);

#ifdef CONFIG_X86_32
	early_ioremap_page_table_range_init();

	load_cr3(swapper_pg_dir);
#endif

	__flush_tlb_all();

	/*
	 * Reserve the kernel pagetable pages we used (pgt_buf_start -
	 * pgt_buf_end) and free the other ones (pgt_buf_end - pgt_buf_top)
	 * so that they can be reused for other purposes.
	 *
	 * On native it just means calling memblock_x86_reserve_range, on Xen it
	 * also means marking RW the pagetable pages that we allocated before
	 * but that haven't been used.
	 *
	 * In fact on xen we mark RO the whole range pgt_buf_start -
	 * pgt_buf_top, because we have to make sure that when
	 * init_memory_mapping reaches the pagetable pages area, it maps
	 * RO all the pagetable pages, including the ones that are beyond
	 * pgt_buf_end at that time.
	 */
	if (!after_bootmem && pgt_buf_end > pgt_buf_start)
		x86_init.mapping.pagetable_reserve(PFN_PHYS(pgt_buf_start),
				PFN_PHYS(pgt_buf_end));

	if (!after_bootmem)
		early_memtest(start, end);

	return ret >> PAGE_SHIFT;
}


/*
 * devmem_is_allowed() checks to see if /dev/mem access to a certain address
 * is valid. The argument is a physical page number.
 *
 *
 * On x86, access has to be given to the first megabyte of ram because that area
 * contains bios code and data regions used by X and dosemu and similar apps.
 * Access has to be given to non-kernel-ram areas as well, these contain the PCI
 * mmio resources as well as potential bios/acpi data regions.
 */
int devmem_is_allowed(unsigned long pagenr)
{
	if (pagenr <= 256)
		return 1;
	if (iomem_is_exclusive(pagenr << PAGE_SHIFT))
		return 0;
	if (!page_is_ram(pagenr))
		return 1;
	return 0;
}

void free_init_pages(char *what, unsigned long begin, unsigned long end)
{
	unsigned long addr;
	unsigned long begin_aligned, end_aligned;

	/* Make sure boundaries are page aligned */
	begin_aligned = PAGE_ALIGN(begin);
	end_aligned   = end & PAGE_MASK;

	if (WARN_ON(begin_aligned != begin || end_aligned != end)) {
		begin = begin_aligned;
		end   = end_aligned;
	}

	if (begin >= end)
		return;

	addr = begin;

	/*
	 * If debugging page accesses then do not free this memory but
	 * mark them not present - any buggy init-section access will
	 * create a kernel page fault:
	 */
#ifdef CONFIG_DEBUG_PAGEALLOC
	printk(KERN_INFO "debug: unmapping init memory %08lx..%08lx\n",
		begin, end);
	set_memory_np(begin, (end - begin) >> PAGE_SHIFT);
#else
	/*
	 * We just marked the kernel text read only above, now that
	 * we are going to free part of that, we need to make that
	 * writeable and non-executable first.
	 */
	set_memory_nx(begin, (end - begin) >> PAGE_SHIFT);
	set_memory_rw(begin, (end - begin) >> PAGE_SHIFT);

	printk(KERN_INFO "Freeing %s: %luk freed\n", what, (end - begin) >> 10);

	for (; addr < end; addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		memset((void *)addr, POISON_FREE_INITMEM, PAGE_SIZE);
		free_page(addr);
		totalram_pages++;
	}
#endif
}

void free_initmem(void)
{
	free_init_pages("unused kernel memory",
			(unsigned long)(&__init_begin),
			(unsigned long)(&__init_end));
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	/*
	 * end could be not aligned, and We can not align that,
	 * decompresser could be confused by aligned initrd_end
	 * We already reserve the end partial page before in
	 *   - i386_start_kernel()
	 *   - x86_64_start_kernel()
	 *   - relocate_initrd()
	 * So here We can do PAGE_ALIGN() safely to get partial page to be freed
	 */
	free_init_pages("initrd memory", start, PAGE_ALIGN(end));
}
#endif
