/*
 * Handle the memory map.
 * The functions here do the job until bootmem takes over.
 *
 *  Getting sanitize_e820_map() in sync with i386 version by applying change:
 *  -  Provisions for empty E820 memory regions (reported by certain BIOSes).
 *     Alex Achenbach <xela@slit.de>, December 2002.
 *  Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/crash_dump.h>
#include <linux/export.h>
#include <linux/bootmem.h>
#include <linux/pfn.h>
#include <linux/suspend.h>
#include <linux/acpi.h>
#include <linux/firmware-map.h>
#include <linux/memblock.h>

#include <asm/e820.h>
#include <asm/proto.h>
#include <asm/setup.h>

/*
 * The e820 map is the map that gets modified e.g. with command line parameters
 * and that is also registered with modifications in the kernel resource tree
 * with the iomem_resource as parent.
 *
 * The e820_saved is directly saved after the BIOS-provided memory map is
 * copied. It doesn't get modified afterwards. It's registered for the
 * /sys/firmware/memmap interface.
 *
 * That memory map is not modified and is used as base for kexec. The kexec'd
 * kernel should get the same memory map as the firmware provides. Then the
 * user can e.g. boot the original kernel with mem=1G while still booting the
 * next kernel with full memory.
 */
struct e820map e820;
struct e820map e820_saved;

/* For PCI or other memory-mapped resources */
unsigned long pci_mem_start = 0xaeedbabe;
#ifdef CONFIG_PCI
EXPORT_SYMBOL(pci_mem_start);
#endif

/*
 * This function checks if any part of the range <start,end> is mapped
 * with type.
 */
int e820_any_mapped(u64 start, u64 end, unsigned type)
{
	int i;
	/**< e820맵을 이잡듯이 뒤지면서 그 공간이 type에 속하면 1 */
	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		if (type && ei->type != type)
			continue;
		if (ei->addr >= end || ei->addr + ei->size <= start)
			continue;
		/* ei->addr < end && ei->end > start
		 * e820맵의 블럭이 인자로 넘어온 블럭과 일부가 겹치고
		 * type이 동일하면 참을 리턴
		 */
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(e820_any_mapped);

/*
 * This function checks if the entire range <start,end> is mapped with type.
 *
 * Note: this function only works correct if the e820 table is sorted and
 * not-overlapping, which is the case
 */
int __init e820_all_mapped(u64 start, u64 end, unsigned type)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		if (type && ei->type != type)
			continue;
		/* is the region (part) in overlap with the current region ?*/
		if (ei->addr >= end || ei->addr + ei->size <= start)
			continue;

		/* if the region is at the beginning of <start,end> we move
		 * start to the end of the region since it's ok until there
		 */
		if (ei->addr <= start)
			start = ei->addr + ei->size;
		/*
		 * if start is now at or beyond end, we're done, full
		 * coverage
		 */
		if (start >= end)
			return 1;
	}
	return 0;
}

/*
 * Add a memory region to the kernel e820 map.
 */
/* e820은 system상의 메모리의 사용가능, 예약, 사용불가등의 타입을 가지며
 * start는 물리주소다.
 */
static void __init __e820_add_region(struct e820map *e820x, u64 start, u64 size,
					 int type)
{
	int x = e820x->nr_map;

	if (x >= ARRAY_SIZE(e820x->map)) {
		printk(KERN_ERR "Ooops! Too many entries in the memory map!\n");
		return;
	}
	/* e820 맵(엔트리 모음) 끝에 하나를 추가한다. */
	e820x->map[x].addr = start;
	e820x->map[x].size = size;
	e820x->map[x].type = type;
	e820x->nr_map++;
}

void __init e820_add_region(u64 start, u64 size, int type)
{
	__e820_add_region(&e820, start, size, type);
}
/* e820 타입을 출력 */
static void __init e820_print_type(u32 type)
{
	switch (type) {
	case E820_RAM:
	case E820_RESERVED_KERN:
		printk(KERN_CONT "(usable)");
		break;
	case E820_RESERVED:
		printk(KERN_CONT "(reserved)");
		break;
	case E820_ACPI:
		printk(KERN_CONT "(ACPI data)");
		break;
	case E820_NVS:
		printk(KERN_CONT "(ACPI NVS)");
		break;
	case E820_UNUSABLE:
		printk(KERN_CONT "(unusable)");
		break;
	default:
		printk(KERN_CONT "type %u", type);
		break;
	}
}

void __init e820_print_map(char *who)
{
	int i;
	/* e820맵을 출력 : dmesg 참조 */
	for (i = 0; i < e820.nr_map; i++) {
		printk(KERN_INFO " %s: %016Lx - %016Lx ", who,
		       (unsigned long long) e820.map[i].addr,
		       (unsigned long long)
		       (e820.map[i].addr + e820.map[i].size));
		e820_print_type(e820.map[i].type);
		printk(KERN_CONT "\n");
	}
}

/*
 * Sanitize the BIOS e820 map.
 *
 * Some e820 responses include overlapping entries. The following
 * replaces the original e820 map with a new one, removing overlaps,
 * and resolving conflicting memory types in favor of highest
 * numbered type.
 *
 * The input parameter biosmap points to an array of 'struct
 * e820entry' which on entry has elements in the range [0, *pnr_map)
 * valid, and which has space for up to max_nr_map entries.
 * On return, the resulting sanitized e820 map entries will be in
 * overwritten in the same location, starting at biosmap.
 *
 * The integer pointed to by pnr_map must be valid on entry (the
 * current number of valid entries located at biosmap) and will
 * be updated on return, with the new number of valid entries
 * (something no more than max_nr_map.)
 *
 * The return value from sanitize_e820_map() is zero if it
 * successfully 'sanitized' the map entries passed in, and is -1
 * if it did nothing, which can happen if either of (1) it was
 * only passed one map entry, or (2) any of the input map entries
 * were invalid (start + size < start, meaning that the size was
 * so big the described memory range wrapped around through zero.)
 *
 *	Visually we're performing the following
 *	(1,2,3,4 = memory types)...
 *
 *	Sample memory map (w/overlaps):
 *	   ____22__________________
 *	   ______________________4_
 *	   ____1111________________
 *	   _44_____________________
 *	   11111111________________
 *	   ____________________33__
 *	   ___________44___________
 *	   __________33333_________
 *	   ______________22________
 *	   ___________________2222_
 *	   _________111111111______
 *	   _____________________11_
 *	   _________________4______
 *
 *	Sanitized equivalent (no overlap):
 *	   1_______________________
 *	   _44_____________________
 *	   ___1____________________
 *	   ____22__________________
 *	   ______11________________
 *	   _________1______________
 *	   __________3_____________
 *	   ___________44___________
 *	   _____________33_________
 *	   _______________2________
 *	   ________________1_______
 *	   _________________4______
 *	   ___________________2____
 *	   ____________________33__
 *	   ______________________4_
 */

/* 위 그림에서 한줄(행)은 각각의 e820 entry
 * 열은 메모리 주소, 값은 entry의 type이다.
 * sanitize뒤 두번째 그림은 e820 entry들이 겹치지 않는다.
 *
 * 시작주소와 시작주소+크기가 반복되는 2배 배열(change_point)을 만든다.
 * 이를 정렬하면 겹치는 블럭들을 쪼개는 것과 같은 효과를 얻는다.
 * 정렬하고 만약 같다면 시작주소가 시작주소+크기의 앞에 오게한다.
 * 
 * 값이 시작주소라면 overlap_list에 넣고 시작주소+크기라면 리스트에서 뺀다.
 * overlap_list에 여러개가 들어간다면 이 구역에서 중첩이 일어났다는 것이다.
 * 그 구역에서 가장 높은 type 값으로 결정한다.
 * 정상이면 0 리턴
 */

int __init sanitize_e820_map(struct e820entry *biosmap, int max_nr_map,
			     u32 *pnr_map)
{
	struct change_member {
		struct e820entry *pbios; /* pointer to original bios entry */
		unsigned long long addr; /* address for this change point */
	};
	static struct change_member change_point_list[2*E820_X_MAX] __initdata;
	static struct change_member *change_point[2*E820_X_MAX] __initdata;
	static struct e820entry *overlap_list[E820_X_MAX] __initdata;
	static struct e820entry new_bios[E820_X_MAX] __initdata;
	struct change_member *change_tmp;
	unsigned long current_type, last_type;
	unsigned long long last_addr;
	int chgidx, still_changing;
	int overlap_entries;
	int new_bios_entry;
	int old_nr, new_nr, chg_nr;
	int i;

	/* if there's only one memory region, don't bother */
	if (*pnr_map < 2)
		return -1;

	old_nr = *pnr_map;			/* e820 엔트리 개수 */
	BUG_ON(old_nr > max_nr_map);

	/* bail out if we find any unreasonable addresses in bios map */
	/* 사이즈가 -일때 예외처리 */
	for (i = 0; i < old_nr; i++)
		if (biosmap[i].addr + biosmap[i].size < biosmap[i].addr)
			return -1;

	/* create pointers for initial change-point information (for sorting) */
	for (i = 0; i < 2 * old_nr; i++)
		change_point[i] = &change_point_list[i];

	/* record all known change-points (starting and ending addresses),
	   omitting those that are for empty memory regions */
	chgidx = 0;
	for (i = 0; i < old_nr; i++)	{
		if (biosmap[i].size != 0) {
		  /* 원본 e820 배열을 시작주소, 시작주소+크기가 반복되는 배열로 확장한다. */
			change_point[chgidx]->addr = biosmap[i].addr; /* e820 엔트리 값(메모리 주소) */
			change_point[chgidx++]->pbios = &biosmap[i];  /* e820 엔트리 원본 인터 */
			change_point[chgidx]->addr = biosmap[i].addr + biosmap[i].size; /* e820 엔트리 값 + 크기 */
			change_point[chgidx++]->pbios = &biosmap[i];
		}
	}
	chg_nr = chgidx; // 엔트리수 * 2

	/* sort change-point list by memory addresses (low -> high) */
	still_changing = 1;
	while (still_changing)	{
		still_changing = 0;
		for (i = 1; i < chg_nr; i++)  {
			unsigned long long curaddr, lastaddr;
			unsigned long long curpbaddr, lastpbaddr;

			curaddr = change_point[i]->addr; /* 현재 값 */
			lastaddr = change_point[i - 1]->addr; /* 이전 값*/
			curpbaddr = change_point[i]->pbios->addr; /* 현재 원본 값 */
			lastpbaddr = change_point[i - 1]->pbios->addr; /* 이전 원본 값 */

			/*
			 * swap entries, when:
			 *
			 * curaddr > lastaddr or
			 * curaddr == lastaddr and curaddr == curpbaddr and
			 * lastaddr != lastpbaddr
			 */
			/* e820 타입은 아래와 같다. 1 == 사용가능
			 * Values for System Memory Map address type:
			 * 01hmemory, available to OS
			 * 02hreserved, not available (e.g. system ROM, memory-mapped device)
			 * 03hACPI Reclaim Memory (usable by OS after reading ACPI *tables)
			 * 04hACPI NVS Memory (OS is required to save this memory between NVS sessions)
			 * 
			 * curaddr < lastaddr : 오름차순으로 정렬한다. (1,2,3....)
			 * curaddr == curpbaddr : 현재값이 시작주소다.
			 * lastaddr != lastpbaddr : 이전값이 시작주소+크기다.
			 * low -> high로 정렬하고 같을경우 뒤에 나오는 루틴을 위해 시작주소가 앞에 오게 한다.
			 */
			if (curaddr < lastaddr ||
			    (curaddr == lastaddr && curaddr == curpbaddr &&
			     lastaddr != lastpbaddr)) {
				change_tmp = change_point[i];
				change_point[i] = change_point[i-1];
				change_point[i-1] = change_tmp;
				still_changing = 1;
			}
		}
	}

	/* create a new bios memory map, removing overlaps */
	overlap_entries = 0;	 /* number of entries in the overlap table */
	new_bios_entry = 0;	 /* index for creating new bios map entries */
	last_type = 0;		 /* start with undefined memory type */
	last_addr = 0;		 /* start with 0 as last starting address */

	/* loop through change-points, determining affect on the new bios map */
	for (chgidx = 0; chgidx < chg_nr; chgidx++) {
		/* keep track of all overlapping bios entries */
	 /* 시작주소인가? 그렇다면 overlap_list에 넣는다
	 * overlap_list에 많이 쌓인다는 것은 주소가 많이 겹치는 것을 의미한다
	 */
		if (change_point[chgidx]->addr ==
		    change_point[chgidx]->pbios->addr) {
			/*
			 * add map entry to overlap list (> 1 entry
			 * implies an overlap)
			 */
			overlap_list[overlap_entries++] =
				change_point[chgidx]->pbios;
		} else {
			/*
			 * remove entry from list (order independent,
			 * so swap with last)
			 */
			/* 시작주소+크기 부분이라면 동일한 e820 엔트리의 시작주소를 overlap_list에서 제거한다.
			 * 시작주소와 시작주소+크기는 한쌍이다. 리스트에 들어왔다가 결국 제거된다.
			 */
			for (i = 0; i < overlap_entries; i++) {
				if (overlap_list[i] ==
				    change_point[chgidx]->pbios)
					overlap_list[i] =
						overlap_list[overlap_entries-1];
			}
			overlap_entries--;
		}
		/*
		 * if there are overlapping entries, decide which
		 * "type" to use (larger value takes precedence --
		 * 1=usable, 2,3,4,4+=unusable)
		 */
		/* overlap_list에서 가장 높은 값을 취한다.
		 * 여러개의 영역이 중첩되었다면 가장 높은 type을 취한다.
         */
		current_type = 0;
		for (i = 0; i < overlap_entries; i++)
			if (overlap_list[i]->type > current_type)
				current_type = overlap_list[i]->type;
		/*
		 * continue building up new bios map based on this
		 * information
		 */
		/* 같은 타입이라면 넘기고 같은 엔트리에 넣는다.
		 * 기존의 e820엔트리를 처리하면서 new_bios 엔트리에 쓰는데
		 * 두부분의 if로 나뉜다.
		 * 첫번째 if는 크기를 넣고 엔트리를 증가시킨다.
		 * 두번째 if는 엔트리 시작주소를 넣는다.
		 */
		if (current_type != last_type)	{
		  /* 처음이거나 이상한값(0)이면 넘긴다. */
			if (last_type != 0)	 {
				new_bios[new_bios_entry].size =
					change_point[chgidx]->addr - last_addr;
				/*
				 * move forward only if the new size
				 * was non-zero
				 */
				/* 같은값이 와서 크기가 0이 아니라면 엔트리를 증가시킨다. */
				if (new_bios[new_bios_entry].size != 0)
					/*
					 * no more space left for new
					 * bios entries ?
					 */
					if (++new_bios_entry >= max_nr_map)
						break;
			}
			/* 새 엔트리 주소와 type을 넣는다.  */
			if (current_type != 0)	{
				new_bios[new_bios_entry].addr =
					change_point[chgidx]->addr;
				new_bios[new_bios_entry].type = current_type;
				last_addr = change_point[chgidx]->addr; /* last_addr은 계속 증가한다. */
			}
			last_type = current_type;
		}
	}
	/* retain count for new bios entries */
	new_nr = new_bios_entry;

	/* copy new bios mapping into original location */
	/* 새로운 e820 엔트리들을 기존의 주소에 쓴다. */
	memcpy(biosmap, new_bios, new_nr * sizeof(struct e820entry));
	*pnr_map = new_nr;			/* 엔트리 개수 */

	return 0;
}

static int __init __append_e820_map(struct e820entry *biosmap, int nr_map)
{
	while (nr_map) {
		u64 start = biosmap->addr;
		u64 size = biosmap->size;
		u64 end = start + size;
		u32 type = biosmap->type;

		/* Overflow in 64 bits? Ignore the memory map. */
		if (start > end)
			return -1;

		e820_add_region(start, size, type);

		biosmap++;
		nr_map--;
	}
	return 0;
}

/*
 * Copy the BIOS e820 map into a safe place.
 *
 * Sanity-check it while we're at it..
 *
 * If we're lucky and live on a modern system, the setup code
 * will have given us a memory map that we can use to properly
 * set up memory.  If we aren't, we'll fake a memory map.
 */
static int __init append_e820_map(struct e820entry *biosmap, int nr_map)
{
	/* Only one memory region (or negative)? Ignore it */
	if (nr_map < 2)
		return -1;

	return __append_e820_map(biosmap, nr_map);
}

static u64 __init __e820_update_range(struct e820map *e820x, u64 start,
					u64 size, unsigned old_type,
					unsigned new_type)
{
	u64 end;
	unsigned int i;
	u64 real_updated_size = 0;
	/* 타입이 달라야한다. */
	BUG_ON(old_type == new_type);
	if (size > (ULLONG_MAX - start))
		size = ULLONG_MAX - start;

	end = start + size;	/* end값 계산 */
	printk(KERN_DEBUG "e820 update range: %016Lx - %016Lx ",
		       (unsigned long long) start,
		       (unsigned long long) end);
	e820_print_type(old_type); /* usable, reserved 등의 타입 출력 */
	printk(KERN_CONT " ==> "); /* old_type ==> new_type 출력 */
	e820_print_type(new_type);
	printk(KERN_CONT "\n");

	for (i = 0; i < e820x->nr_map; i++) {
		struct e820entry *ei = &e820x->map[i];
		u64 final_start, final_end;
		u64 ei_end;

		if (ei->type != old_type)
			continue;
		/* old type일때만 한다. */
		ei_end = ei->addr + ei->size;
		/* totally covered by new range? */
		/* 변경할 엔트리가 ei 엔트리를 포함하는 경우 new type로 변경  */
		if (ei->addr >= start && ei_end <= end) {
			ei->type = new_type;
			real_updated_size += ei->size;
			continue;
		}

		/* new range is totally covered? */
		/* 변경할 엔트리가 ei엔트리에 포함되는 경우
		 * oldtype엔트리와 new type 엔트리를 쪼개서 원래의 ei엔트리 사이즈를 쪼개서 맨 뒤에다 
		 * new entry, ei_end - end 사이즈 만큼의 ei를 추가해준다.
		 */
		if (ei->addr < start && ei_end > end) {
			__e820_add_region(e820x, start, size, new_type);
			__e820_add_region(e820x, end, ei_end - end, ei->type);
			ei->size = start - ei->addr;
			real_updated_size += size;
			continue;
		}

		/* partially covered */
		/* 부분적으로 겹치는 경우, 겹쳐지는 영역 선택 */
		final_start = max(start, ei->addr);
		final_end = min(end, ei_end);
		/* 겹쳐지지 않는 경우는 패스 */
		if (final_start >= final_end)
			continue;
		/* 겹치는 부분을 new type으로 등록해준다. */
		__e820_add_region(e820x, final_start, final_end - final_start,
				  new_type);

		real_updated_size += final_end - final_start;

		/*
		 * left range could be head or tail, so need to update
		 * size at first.
		 */
		/* old type의 겹치지 않는 영역의 사이즈 조절 */
		ei->size -= final_end - final_start;
		/* ei 엔트리 = head, size값만 변경 */
		if (ei->addr < final_start)
			continue;
		/* ei 엔트리 = tail, 시작주소 값도 변경 */
		ei->addr = final_end;
	}
	return real_updated_size; /* 실제로 변경된 값 */
}

u64 __init e820_update_range(u64 start, u64 size, unsigned old_type,
			     unsigned new_type)
{
	return __e820_update_range(&e820, start, size, old_type, new_type);
}

static u64 __init e820_update_range_saved(u64 start, u64 size,
					  unsigned old_type, unsigned new_type)
{
	return __e820_update_range(&e820_saved, start, size, old_type,
				     new_type);
}

/* make e820 not cover the range */
u64 __init e820_remove_range(u64 start, u64 size, unsigned old_type,
			     int checktype)
{
	int i;
	u64 end;
	u64 real_removed_size = 0;

	if (size > (ULLONG_MAX - start))
		size = ULLONG_MAX - start;

	end = start + size;
	printk(KERN_DEBUG "e820 remove range: %016Lx - %016Lx ",
		       (unsigned long long) start,
		       (unsigned long long) end);
	if (checktype)
		e820_print_type(old_type);
	printk(KERN_CONT "\n");

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		u64 final_start, final_end;
		u64 ei_end;
		/* RAM타입인지 아닌지 체크*/
		if (checktype && ei->type != old_type)
			continue;

		ei_end = ei->addr + ei->size;
		/* totally covered? */
		/* 검색중인 블럭이 더 작으면 지워버린다.  */
		if (ei->addr >= start && ei_end <= end) {
			real_removed_size += ei->size;
			memset(ei, 0, sizeof(struct e820entry));
			continue;
		}

		/* new range is totally covered? */
		/* 검색중인 블럭이 remove할 블럭보다 더 크면 하나는 사이즈를 줄이고
		 * 뒤쪽에는 하나를 등록해서 remove 영역은 공백으로 만든다.
		 */
		if (ei->addr < start && ei_end > end) {
			e820_add_region(end, ei_end - end, ei->type);
			ei->size = start - ei->addr;
			real_removed_size += size;
			continue;
		}

		/* partially covered */
		final_start = max(start, ei->addr);
		final_end = min(end, ei_end);
		if (final_start >= final_end)
			continue;
		real_removed_size += final_end - final_start;

		/*
		 * left range could be head or tail, so need to update
		 * size at first.
		 */
		ei->size -= final_end - final_start;
		if (ei->addr < final_start)
			continue;
		ei->addr = final_end;
	}
	return real_removed_size;
}

void __init update_e820(void)
{
	u32 nr_map;

	nr_map = e820.nr_map;
	/* sanitize 결과 에러나면 리턴 */
	if (sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &nr_map))
		return;
	e820.nr_map = nr_map;
	printk(KERN_INFO "modified physical RAM map:\n");
	e820_print_map("modified"); /* 수정되었다고 표시 */
}
static void __init update_e820_saved(void)
{
	u32 nr_map;

	nr_map = e820_saved.nr_map;
	if (sanitize_e820_map(e820_saved.map, ARRAY_SIZE(e820_saved.map), &nr_map))
		return;
	e820_saved.nr_map = nr_map;
}
#define MAX_GAP_END 0x100000000ull
/*
 * Search for a gap in the e820 memory space from start_addr to end_addr.
 */
__init int e820_search_gap(unsigned long *gapstart, unsigned long *gapsize,
		unsigned long start_addr, unsigned long long end_addr)
{
	unsigned long long last;
	int i = e820.nr_map;
	int found = 0;

	last = (end_addr && end_addr < MAX_GAP_END) ? end_addr : MAX_GAP_END;

	while (--i >= 0) {
		unsigned long long start = e820.map[i].addr;
		unsigned long long end = start + e820.map[i].size;

		if (end < start_addr)
			continue;

		/*
		 * Since "last" is at most 4GB, we know we'll
		 * fit in 32 bits if this condition is true
		 */
		if (last > end) {
			unsigned long gap = last - end;

			if (gap >= *gapsize) {
				*gapsize = gap;
				*gapstart = end;
				found = 1;
			}
		}
		if (start < last)
			last = start;
	}
	return found;
}

/*
 * Search for the biggest gap in the low 32 bits of the e820
 * memory space.  We pass this space to PCI to assign MMIO resources
 * for hotplug or unconfigured devices in.
 * Hopefully the BIOS let enough space left.
 */
__init void e820_setup_gap(void)
{
	unsigned long gapstart, gapsize;
	int found;

	gapstart = 0x10000000;
	gapsize = 0x400000;
	found  = e820_search_gap(&gapstart, &gapsize, 0, MAX_GAP_END);

#ifdef CONFIG_X86_64
	if (!found) {
		gapstart = (max_pfn << PAGE_SHIFT) + 1024*1024;
		printk(KERN_ERR
	"PCI: Warning: Cannot find a gap in the 32bit address range\n"
	"PCI: Unassigned devices with 32bit resource registers may break!\n");
	}
#endif

	/*
	 * e820_reserve_resources_late protect stolen RAM already
	 */
	pci_mem_start = gapstart;

	printk(KERN_INFO
	       "Allocating PCI resources starting at %lx (gap: %lx:%lx)\n",
	       pci_mem_start, gapstart, gapsize);
}

/**
 * Because of the size limitation of struct boot_params, only first
 * 128 E820 memory entries are passed to kernel via
 * boot_params.e820_map, others are passed via SETUP_E820_EXT node of
 * linked list of struct setup_data, which is parsed here.
 */
void __init parse_e820_ext(struct setup_data *sdata)
{
	int entries;
	struct e820entry *extmap;

	entries = sdata->len / sizeof(struct e820entry);
	extmap = (struct e820entry *)(sdata->data);
	__append_e820_map(extmap, entries);
	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
	printk(KERN_INFO "extended physical RAM map:\n");
	e820_print_map("extended");
}

#if defined(CONFIG_X86_64) || \
	(defined(CONFIG_X86_32) && defined(CONFIG_HIBERNATION))
/**
 * Find the ranges of physical addresses that do not correspond to
 * e820 RAM areas and mark the corresponding pages as nosave for
 * hibernation (32 bit) or software suspend and suspend to RAM (64 bit).
 *
 * This function requires the e820 map to be sorted and without any
 * overlapping entries and assumes the first e820 area to be RAM.
 */
void __init e820_mark_nosave_regions(unsigned long limit_pfn)
{
	int i;
	unsigned long pfn;

	pfn = PFN_DOWN(e820.map[0].addr + e820.map[0].size);
	for (i = 1; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		if (pfn < PFN_UP(ei->addr))
			register_nosave_region(pfn, PFN_UP(ei->addr));

		pfn = PFN_DOWN(ei->addr + ei->size);
		if (ei->type != E820_RAM && ei->type != E820_RESERVED_KERN)
			register_nosave_region(PFN_UP(ei->addr), pfn);

		if (pfn >= limit_pfn)
			break;
	}
}
#endif

#ifdef CONFIG_HIBERNATION
/**
 * Mark ACPI NVS memory region, so that we can save/restore it during
 * hibernation and the subsequent resume.
 */
static int __init e820_mark_nvs_memory(void)
{
	int i;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		if (ei->type == E820_NVS)
			suspend_nvs_register(ei->addr, ei->size);
	}

	return 0;
}
core_initcall(e820_mark_nvs_memory);
#endif

/*
 * pre allocated 4k and reserved it in memblock and e820_saved
 */
u64 __init early_reserve_e820(u64 startt, u64 sizet, u64 align)
{
	u64 size = 0;
	u64 addr;
	u64 start;

	for (start = startt; ; start += size) {
		start = memblock_x86_find_in_range_size(start, &size, align);
		if (start == MEMBLOCK_ERROR)
			return 0;
		if (size >= sizet)
			break;
	}

#ifdef CONFIG_X86_32
	if (start >= MAXMEM)
		return 0;
	if (start + size > MAXMEM)
		size = MAXMEM - start;
#endif

	addr = round_down(start + size - sizet, align);
	if (addr < start)
		return 0;
	memblock_x86_reserve_range(addr, addr + sizet, "new next");
	e820_update_range_saved(addr, sizet, E820_RAM, E820_RESERVED);
	printk(KERN_INFO "update e820_saved for early_reserve_e820\n");
	update_e820_saved();

	return addr;
}

#ifdef CONFIG_X86_32
# ifdef CONFIG_X86_PAE
#  define MAX_ARCH_PFN		(1ULL<<(36-PAGE_SHIFT))
# else
#  define MAX_ARCH_PFN		(1ULL<<(32-PAGE_SHIFT))
# endif
#else /* CONFIG_X86_32 */
# define MAX_ARCH_PFN MAXMEM>>PAGE_SHIFT
#endif

/*
 * Find the highest page frame number we have available
 */
static unsigned long __init e820_end_pfn(unsigned long limit_pfn, unsigned type)
{
	int i;
	unsigned long last_pfn = 0;
	unsigned long max_arch_pfn = MAX_ARCH_PFN;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];
		unsigned long start_pfn;
		unsigned long end_pfn;

		if (ei->type != type)
			continue;
		/* 시작 페이지 넘버와 끝 페이지 넘버를 구한다. */
		start_pfn = ei->addr >> PAGE_SHIFT;
		end_pfn = (ei->addr + ei->size) >> PAGE_SHIFT;

		/* 64비트에서는 limit는 1 << 34 */
		/* 한계를 넘어서면 무시한다. */
		if (start_pfn >= limit_pfn)
			continue;
		/* limit를 넘어서는 부분 역시 무시. limit로 설정후 루프를 끝낸다. */
		if (end_pfn > limit_pfn) {
			last_pfn = limit_pfn;
			break;
		}
		/* 최상위값 갱신 */
		if (end_pfn > last_pfn)
			last_pfn = end_pfn;
	}
	/* 아키텍쳐의 최대값을 넘어서는지 체크 */
	if (last_pfn > max_arch_pfn)
		last_pfn = max_arch_pfn;
	/* max page frame number 출력 */
	printk(KERN_INFO "last_pfn = %#lx max_arch_pfn = %#lx\n",
			 last_pfn, max_arch_pfn);
	return last_pfn;
}
unsigned long __init e820_end_of_ram_pfn(void)
{
	return e820_end_pfn(MAX_ARCH_PFN, E820_RAM);
}

unsigned long __init e820_end_of_low_ram_pfn(void)
{
	return e820_end_pfn(1UL<<(32 - PAGE_SHIFT), E820_RAM);
}

static void early_panic(char *msg)
{
	early_printk(msg);
	panic(msg);
}

static int userdef __initdata;

/* "mem=nopentium" disables the 4MB page tables. */
static int __init parse_memopt(char *p)
{
	u64 mem_size;

	if (!p)
		return -EINVAL;

	/* 32비트면 nopentium 옵션이 오면 PSE 비트를 끈다. 컴퓨터 < 586
	 * 64비트는 no pentium이 있을수 없기에 무시된다.
	 */
	if (!strcmp(p, "nopentium")) {
#ifdef CONFIG_X86_32
		setup_clear_cpu_cap(X86_FEATURE_PSE);
		return 0;
#else
		printk(KERN_WARNING "mem=nopentium ignored! (only supported on x86_32)\n");
		return -EINVAL;
#endif
	}

	userdef = 1;
	mem_size = memparse(p, &p);
	/* don't remove all of memory when handling "mem={invalid}" param */
	if (mem_size == 0)
		return -EINVAL;
	e820_remove_range(mem_size, ULLONG_MAX - mem_size, E820_RAM, 1);

	return 0;
}
/* early_param으로 등록해준다. */
early_param("mem", parse_memopt);

static int __init parse_memmap_opt(char *p)
{
	char *oldp;
	u64 start_at, mem_size;

	if (!p)
		return -EINVAL;

	if (!strncmp(p, "exactmap", 8)) {
#ifdef CONFIG_CRASH_DUMP
		/*
		 * If we are doing a crash dump, we still need to know
		 * the real mem size before original memory map is
		 * reset.
		 */
		saved_max_pfn = e820_end_of_ram_pfn();
#endif
		e820.nr_map = 0;
		userdef = 1;
		return 0;
	}

	oldp = p;
	mem_size = memparse(p, &p);
	if (p == oldp)
		return -EINVAL;

	userdef = 1;
	if (*p == '@') {
		start_at = memparse(p+1, &p);
		e820_add_region(start_at, mem_size, E820_RAM);
	} else if (*p == '#') {
		start_at = memparse(p+1, &p);
		e820_add_region(start_at, mem_size, E820_ACPI);
	} else if (*p == '$') {
		start_at = memparse(p+1, &p);
		e820_add_region(start_at, mem_size, E820_RESERVED);
	} else
		e820_remove_range(mem_size, ULLONG_MAX - mem_size, E820_RAM, 1);

	return *p == '\0' ? 0 : -EINVAL;
}
early_param("memmap", parse_memmap_opt);

void __init finish_e820_parsing(void)
{
	/* 유저가 정의한 mem=이나 memmap= 옵션으로 메모리에 변동사항이 생기면
	 * userdef가 1이 e820 맵의 변동사항에 대한 sanitize를 한번 더 한다. */
	if (userdef) {
		u32 nr = e820.nr_map;

		if (sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &nr) < 0)
			early_panic("Invalid user supplied memory map");
		e820.nr_map = nr;

		printk(KERN_INFO "user-defined physical RAM map:\n");
		e820_print_map("user");
	}
}

static inline const char *e820_type_to_string(int e820_type)
{
	switch (e820_type) {
	case E820_RESERVED_KERN:
	case E820_RAM:	return "System RAM";
	case E820_ACPI:	return "ACPI Tables";
	case E820_NVS:	return "ACPI Non-volatile Storage";
	case E820_UNUSABLE:	return "Unusable memory";
	default:	return "reserved";
	}
}

/*
 * Mark e820 reserved areas as busy for the resource manager.
 */
static struct resource __initdata *e820_res;
void __init e820_reserve_resources(void)
{
	int i;
	struct resource *res;
	u64 end;

	res = alloc_bootmem(sizeof(struct resource) * e820.nr_map);
	e820_res = res;
	for (i = 0; i < e820.nr_map; i++) {
		end = e820.map[i].addr + e820.map[i].size - 1;
		if (end != (resource_size_t)end) {
			res++;
			continue;
		}
		res->name = e820_type_to_string(e820.map[i].type);
		res->start = e820.map[i].addr;
		res->end = end;

		res->flags = IORESOURCE_MEM;

		/*
		 * don't register the region that could be conflicted with
		 * pci device BAR resource and insert them later in
		 * pcibios_resource_survey()
		 */
		if (e820.map[i].type != E820_RESERVED || res->start < (1ULL<<20)) {
			res->flags |= IORESOURCE_BUSY;
			insert_resource(&iomem_resource, res);
		}
		res++;
	}

	for (i = 0; i < e820_saved.nr_map; i++) {
		struct e820entry *entry = &e820_saved.map[i];
		firmware_map_add_early(entry->addr,
			entry->addr + entry->size - 1,
			e820_type_to_string(entry->type));
	}
}

/* How much should we pad RAM ending depending on where it is? */
static unsigned long ram_alignment(resource_size_t pos)
{
	unsigned long mb = pos >> 20;

	/* To 64kB in the first megabyte */
	if (!mb)
		return 64*1024;

	/* To 1MB in the first 16MB */
	if (mb < 16)
		return 1024*1024;

	/* To 64MB for anything above that */
	return 64*1024*1024;
}

#define MAX_RESOURCE_SIZE ((resource_size_t)-1)

void __init e820_reserve_resources_late(void)
{
	int i;
	struct resource *res;

	res = e820_res;
	for (i = 0; i < e820.nr_map; i++) {
		if (!res->parent && res->end)
			insert_resource_expand_to_fit(&iomem_resource, res);
		res++;
	}

	/*
	 * Try to bump up RAM regions to reasonable boundaries to
	 * avoid stolen RAM:
	 */
	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *entry = &e820.map[i];
		u64 start, end;

		if (entry->type != E820_RAM)
			continue;
		start = entry->addr + entry->size;
		end = round_up(start, ram_alignment(start)) - 1;
		if (end > MAX_RESOURCE_SIZE)
			end = MAX_RESOURCE_SIZE;
		if (start >= end)
			continue;
		printk(KERN_DEBUG "reserve RAM buffer: %016llx - %016llx ",
			       start, end);
		reserve_region_with_split(&iomem_resource, start, end,
					  "RAM buffer");
	}
}

char *__init default_machine_specific_memory_setup(void)
{
	char *who = "BIOS-e820";
	u32 new_nr;
	/*
	 * Try to copy the BIOS-supplied E820-map.
	 *
	 * Otherwise fake a memory map; one section from 0k->640k,
	 * the next section from 1mb->appropriate_mem_k
	 */
	new_nr = boot_params.e820_entries; /* 엔트리 개수 */
	/* e820 memory map의 겹치는 부분을 쪼개서 새로운 e820 map 을 생성하고 원래의 
	*  e820 map의 위치에 복사해놓는다. entry 의 type 은 높은 놈으로 들어가며
	*  entry 갯수는 늘어날 것이다.
	*/
	sanitize_e820_map(boot_params.e820_map,
			ARRAY_SIZE(boot_params.e820_map),
			&new_nr);
	boot_params.e820_entries = new_nr; /* 엔트리 개수 */
	/* sanitize한 e820 memory map을 복사한다. */
	if (append_e820_map(boot_params.e820_map, boot_params.e820_entries)
	  < 0) {
		u64 mem_size;
		/* e820을 구할수 없으면 88이나 e801 인터럽트로 메모리 방법을 구해 넣어준다. */
		/* compare results from other methods and take the greater */
		if (boot_params.alt_mem_k
		    < boot_params.screen_info.ext_mem_k) {
			mem_size = boot_params.screen_info.ext_mem_k;
			who = "BIOS-88";
		} else {
			mem_size = boot_params.alt_mem_k;
			who = "BIOS-e801";
		}

		e820.nr_map = 0;
		e820_add_region(0, LOWMEMSIZE(), E820_RAM); /* e820을 구하지 못하면 직접 e820 메모리 영역을 정해준다. 636K 기본메모리(<1M) 영역 */
		e820_add_region(HIGH_MEMORY, mem_size << 10, E820_RAM); /* 1M부터 메모리 용량 (1M단위) 타입은 available (E820_RAM=1) */
	}

	/* In case someone cares... */
	return who;					/* 메모리 구하는 방법 스트링 포엔터 ("BIOS-...") */
}

void __init setup_memory_map(void)
{
	char *who;

	/*	memory_setup는 default_machine_specific_memory_setup 함수 포인터다. */
	who = x86_init.resources.memory_setup();
	memcpy(&e820_saved, &e820, sizeof(struct e820map));
	printk(KERN_INFO "BIOS-provided physical RAM map:\n"); /* dmesg 초반부의 e820 맵 출력 */
	e820_print_map(who);
}

void __init memblock_x86_fill(void)
{
	int i;
	u64 end;

	/*
	 * EFI may have more than 128 entries
	 * We are safe to enable resizing, beause memblock_x86_fill()
	 * is rather later for x86
	 */
	memblock_can_resize = 1;

	for (i = 0; i < e820.nr_map; i++) {
		struct e820entry *ei = &e820.map[i];

		end = ei->addr + ei->size;
		if (end != (resource_size_t)end)
			continue;

		if (ei->type != E820_RAM && ei->type != E820_RESERVED_KERN)
			continue;

		memblock_add(ei->addr, ei->size);
	}

	memblock_analyze();
	memblock_dump_all();
}

void __init memblock_find_dma_reserve(void)
{
#ifdef CONFIG_X86_64
	u64 free_size_pfn;
	u64 mem_size_pfn;
	/*
	 * need to find out used area below MAX_DMA_PFN
	 * need to use memblock to get free size in [0, MAX_DMA_PFN]
	 * at first, and assume boot_mem will not take below MAX_DMA_PFN
	 */
	mem_size_pfn = memblock_x86_memory_in_range(0, MAX_DMA_PFN << PAGE_SHIFT) >> PAGE_SHIFT;
	free_size_pfn = memblock_x86_free_memory_in_range(0, MAX_DMA_PFN << PAGE_SHIFT) >> PAGE_SHIFT;
	set_dma_reserve(mem_size_pfn - free_size_pfn);
#endif
}
