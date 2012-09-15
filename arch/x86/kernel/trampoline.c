#include <linux/io.h>
#include <linux/memblock.h>

#include <asm/trampoline.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>

unsigned char *x86_trampoline_base;

/* smp 커널에서 현재까지 한개의 CPU로 했었음.
 * 
*/
void __init setup_trampolines(void)
{
	phys_addr_t mem;
	size_t size = PAGE_ALIGN(x86_trampoline_end - x86_trampoline_start);

	/* Has to be in very low memory so we can execute real-mode AP code. */
	/* 기본메모리(1M)에서 size만큼 메모리 블럭을 할당 */
	mem = memblock_find_in_range(0, 1<<20, size, PAGE_SIZE);
	/* 할당못하면 패닉 */
	if (mem == MEMBLOCK_ERROR)
		panic("Cannot allocate trampoline\n");
	/* 가상메모리 주소 대입 */
	x86_trampoline_base = __va(mem);
	/* 해당 메모리 블럭을 TRAMPOLINE으로 예약 */
	memblock_x86_reserve_range(mem, mem + size, "TRAMPOLINE");

	printk(KERN_DEBUG "Base memory trampoline at [%p] %llx size %zu\n",
	       x86_trampoline_base, (unsigned long long)mem, size);
	/* 커널의 trampoline코드를 1M이하 영역에 복사한다.
	 * 나중에 16비트부터 64비트 모드까지 올린다.
	 */
	memcpy(x86_trampoline_base, x86_trampoline_start, size);
}

/*
 * setup_trampolines() gets called very early, to guarantee the
 * availability of low memory.  This is before the proper kernel page
 * tables are set up, so we cannot set page permissions in that
 * function.  Thus, we use an arch_initcall instead.
 */
static int __init configure_trampolines(void)
{
	size_t size = PAGE_ALIGN(x86_trampoline_end - x86_trampoline_start);

	set_memory_x((unsigned long)x86_trampoline_base, size >> PAGE_SHIFT);
	return 0;
}
arch_initcall(configure_trampolines);
