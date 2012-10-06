/*
 * Range add and subtract
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sort.h>

#include <linux/range.h>
/* start~end 영역을 range에 추가한다.
 * az는 현재 번호, nr_range는 최대 갯수
 */
int add_range(struct range *range, int az, int nr_range, u64 start, u64 end)
{
	if (start >= end)
		return nr_range;

	/* Out of slots: */
	/* 최대 개수를 넘으면 무시 */
	if (nr_range >= az)
		return nr_range;

	range[nr_range].start = start;
	range[nr_range].end = end;

	/* range 갯수 증가 */
	nr_range++;

	return nr_range;
}
/* 이름 그대로 겹치면 합친다.
 * az는 배열 최대 갯수, nr_range는 앞선 배열 갯수
 */
int add_range_with_merge(struct range *range, int az, int nr_range,
		     u64 start, u64 end)
{
	int i;

	if (start >= end)
		return nr_range;

	/* Try to merge it with old one: */
	/* 앞선 range 갯수만큼 체크하면서 merge */
	for (i = 0; i < nr_range; i++) {
		u64 final_start, final_end;
		u64 common_start, common_end;

		if (!range[i].end) /* end가 0이면 = 예외처리 */
			continue;

		common_start = max(range[i].start, start);
		common_end = min(range[i].end, end);
		if (common_start > common_end)
			continue;
		/* 이전 영역과 겹치면 이전 영역에 합친다. */
		final_start = min(range[i].start, start);
		final_end = max(range[i].end, end);

		range[i].start = final_start;
		range[i].end =  final_end;
		return nr_range;
	}

	/* Need to add it: */
	return add_range(range, az, nr_range, start, end);
}
/* 인자로 들어온 영역을 range에서 제거한다. */
void subtract_range(struct range *range, int az, u64 start, u64 end)
{
	int i, j;

	if (start >= end)
		return;

	for (j = 0; j < az; j++) {
		if (!range[j].end)
			continue;
		/* start~end 안에 포함되면 뺀다. */
		if (start <= range[j].start && end >= range[j].end) {
			range[j].start = 0;
			range[j].end = 0;
			continue;
		}
		/* start~end가 앞쪽으로 겹치면 겹치는 영역을 뺀다. */
		if (start <= range[j].start && end < range[j].end &&
		    range[j].start < end) {
			range[j].start = end;
			continue;
		}

		/* start~end 블럭이 뒤쪽으로 겹치면 역시 뒤쪽을 짜른다 */
		if (start > range[j].start && end >= range[j].end &&
		    range[j].end > start) {
			range[j].end = start;
			continue;
		}
		/* start~end 블럭이 j블럭 안에 포함되면 */
		if (start > range[j].start && end < range[j].end) {
			/* Find the new spare: */
			for (i = 0; i < az; i++) {
				/* end가 0인 쓸모없는 블럭을 찾는다. */
				if (range[i].end == 0)
					break;
			}
			/* 새로 넣을 i블럭을 뺀 영역의 뒤쪽 블럭으로 세팅 */
			if (i < az) {
				range[i].end = range[j].end;
				range[i].start = end;
			} else {
				/* 슬롯 꽉참 */
				printk(KERN_ERR "run of slot in ranges\n");
			}
			/* 앞쪽블럭 재조정 */
			range[j].end = start;
			continue;
		}
	}
}

static int cmp_range(const void *x1, const void *x2)
{
	const struct range *r1 = x1;
	const struct range *r2 = x2;
	s64 start1, start2;

	start1 = r1->start;
	start2 = r2->start;

	return start1 - start2;
}
/* 빈 공간을 없애고 카운트 후(compaction) heap 정렬*/
int clean_sort_range(struct range *range, int az)
{
	int i, j, k = az - 1, nr_range = az;
	/* k=최대값-1 */
	for (i = 0; i < k; i++) {
		/* end에 값이 있으면 패스 */
		if (range[i].end)
			continue;
		/**
		 * 이 루프는 비어있는 엔트리에서만 실행한다.
		 * 비어있는 엔트리를 마지막 엔트리와 swap한다.
		 */
		for (j = k; j > i; j--) {
			if (range[j].end) {
				k = j;
				break;
			}
		}
		if (j == i)
			break;
		range[i].start = range[k].start;
		range[i].end   = range[k].end;
		range[k].start = 0;
		range[k].end   = 0;
		k--;
	}
	/* count it */
	/* 마지막 엔트리 값을 찾는다. (갯수) */
	for (i = 0; i < az; i++) {
		if (!range[i].end) {
			nr_range = i;
			break;
		}
	}

	/* sort them */
	/* 매우 정렬 */
	sort(range, nr_range, sizeof(struct range), cmp_range, NULL);

	return nr_range;
}
/* sort.c에 있다. 현재는 heap sort를 사용한다. 메모리를 적게 사용 */
void sort_range(struct range *range, int nr_range)
{
	/* sort them */
	sort(range, nr_range, sizeof(struct range), cmp_range, NULL);
}
