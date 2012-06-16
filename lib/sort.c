/*
 * A fast, small, non-recursive O(nlog n) sort for the Linux kernel
 *
 * Jan 23 2005  Matt Mackall <mpm@selenic.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sort.h>
#include <linux/slab.h>

static void u32_swap(void *a, void *b, int size)
{
	u32 t = *(u32 *)a;
	*(u32 *)a = *(u32 *)b;
	*(u32 *)b = t;
}

static void generic_swap(void *a, void *b, int size)
{
	char t;

	do {
		t = *(char *)a;
		*(char *)a++ = *(char *)b;
		*(char *)b++ = t;
	} while (--size > 0);
}

/**
 * sort - sort an array of elements
 * @base: pointer to data to sort
 * @num: number of elements
 * @size: size of each element
 * @cmp_func: pointer to comparison function
 * @swap_func: pointer to swap function or NULL
 *
 * This function does a heapsort on the given array. You may provide a
 * swap_func function optimized to your element type.
 *
 * Sorting time is O(n log n) both on average and worst-case. While
 * qsort is about 20% faster on average, it suffers from exploitable
 * O(n*n) worst-case behavior and extra memory requirements that make
 * it less suitable for kernel use.
 */
/* heap 정렬은 속도도 빠르고 메모리를 적게 쓴다. */
void sort(void *base, size_t num, size_t size,
	  int (*cmp_func)(const void *, const void *),
	  void (*swap_func)(void *, void *, int size))
{
	/* pre-scale counters for performance */
	/* i는 처음 검색할 중간 배열주소, n은 총 바이트 크기 */
	int i = (num/2 - 1) * size, n = num * size, c, r;

	if (!swap_func)		/* 스왑 함수가 없으면 대입 */
		swap_func = (size == 4 ? u32_swap : generic_swap);

	/* heapify */
	for ( ; i >= 0; i -= size) { /* 중간값부터 검색 */
		for (r = i; r * 2 + size < n; r  = c) { /* 자식을 타고 들어간다. */
			c = r * 2 + size;		/* 다음 값은(c)는 왼쪽 자식값 */
			if (c < n - size &&		/* 끝값이 아니고 오른쪽 더 큰 자식을 선택  */
					cmp_func(base + c, base + c + size) < 0)
				c += size;
			if (cmp_func(base + r, base + c) >= 0) /* 부모가 자식들보다 크면 중단 */
				break;
			swap_func(base + r, base + c, size); /* 큰 자식이 부모가 됨 */
		}
	}

	/* sort */
	for (i = n - size; i > 0; i -= size) { /* 끝부터 앞으로 정렬 */
		/* 가장 먼저 큰 값이 들어있기 때문에 swap해서 마지막에 가장 큰값을 채운다. */
		swap_func(base, base + i, size);
		for (r = 0; r * 2 + size < i; r = c) { /* 역시 자식을 타고 들어간다. */
			c = r * 2 + size;	       /* 왼쪽 자식 */
			if (c < i - size &&	       /* 오른쪽 자식과 비교, c=큰자식 */
					cmp_func(base + c, base + c + size) < 0)
				c += size;
			if (cmp_func(base + r, base + c) >= 0) /* 부모가 더 크면 중단 */
				break;
			swap_func(base + r, base + c, size); /* 큰 자식을 위로 올리고 swap하고 자식을 계속 탄다. */
		}
	}
}

EXPORT_SYMBOL(sort);

#if 0
/* a simple boot-time regression test */

int cmpint(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

static int sort_test(void)
{
	int *a, i, r = 1;

	a = kmalloc(1000 * sizeof(int), GFP_KERNEL);
	BUG_ON(!a);

	printk("testing sort()\n");

	for (i = 0; i < 1000; i++) {
		r = (r * 725861) % 6599;
		a[i] = r;
	}

	sort(a, 1000, sizeof(int), cmpint, NULL);

	for (i = 0; i < 999; i++)
		if (a[i] > a[i+1]) {
			printk("sort() failed!\n");
			break;
		}

	kfree(a);

	return 0;
}

module_init(sort_test);
#endif
