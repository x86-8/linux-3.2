/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *
 *  Memory region support
 *	David Parsons <orc@pell.chi.il.us>, July-August 1999
 *
 *  Added E820 sanitization routine (removes overlapping memory regions);
 *  Brian Moyle <bmoyle@mvista.com>, February 2001
 *
 * Moved CPU detection code to cpu/${cpu}.c
 *    Patrick Mochel <mochel@osdl.org>, March 2002
 *
 *  Provisions for empty E820 memory regions (reported by certain BIOSes).
 *  Alex Achenbach <xela@slit.de>, December 2002.
 *
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/screen_info.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/sfi.h>
#include <linux/apm_bios.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/mca.h>
#include <linux/root_dev.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/edd.h>
#include <linux/iscsi_ibft.h>
#include <linux/nodemask.h>
#include <linux/kexec.h>
#include <linux/dmi.h>
#include <linux/pfn.h>
#include <linux/pci.h>
#include <asm/pci-direct.h>
#include <linux/init_ohci1394_dma.h>
#include <linux/kvm_para.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/delay.h>

#include <linux/kallsyms.h>
#include <linux/cpufreq.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>

#include <linux/percpu.h>
#include <linux/crash_dump.h>
#include <linux/tboot.h>

#include <video/edid.h>

#include <asm/mtrr.h>
#include <asm/apic.h>
#include <asm/trampoline.h>
#include <asm/e820.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/efi.h>
#include <asm/timer.h>
#include <asm/i8259.h>
#include <asm/sections.h>
#include <asm/dmi.h>
#include <asm/io_apic.h>
#include <asm/ist.h>
#include <asm/setup_arch.h>
#include <asm/bios_ebda.h>
#include <asm/cacheflush.h>
#include <asm/processor.h>
#include <asm/bugs.h>

#include <asm/system.h>
#include <asm/vsyscall.h>
#include <asm/cpu.h>
#include <asm/desc.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>

#include <asm/paravirt.h>
#include <asm/hypervisor.h>
#include <asm/olpc_ofw.h>

#include <asm/percpu.h>
#include <asm/topology.h>
#include <asm/apicdef.h>
#include <asm/amd_nb.h>
#ifdef CONFIG_X86_64
#include <asm/numa_64.h>
#endif
#include <asm/mce.h>
#include <asm/alternative.h>
#include <asm/prom.h>

/*
 * end_pfn only includes RAM, while max_pfn_mapped includes all e820 entries.
 * The direct mapping extends to max_pfn_mapped, so that we can directly access
 * apertures, ACPI and other tables without having to play with fixmaps.
 */
unsigned long max_low_pfn_mapped;
unsigned long max_pfn_mapped;

#ifdef CONFIG_DMI
RESERVE_BRK(dmi_alloc, 65536);
#endif


static __initdata unsigned long _brk_start = (unsigned long)__brk_base;
unsigned long _brk_end = (unsigned long)__brk_base;

#ifdef CONFIG_X86_64
int default_cpu_present_to_apicid(int mps_cpu)
{
	return __default_cpu_present_to_apicid(mps_cpu);
}

int default_check_phys_apicid_present(int phys_apicid)
{
	return __default_check_phys_apicid_present(phys_apicid);
}
#endif

#ifndef CONFIG_DEBUG_BOOT_PARAMS
struct boot_params __initdata boot_params;
#else
struct boot_params boot_params;
#endif

/*
 * Machine setup..
 */
static struct resource data_resource = {
	.name	= "Kernel data",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource code_resource = {
	.name	= "Kernel code",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource bss_resource = {
	.name	= "Kernel bss",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};


#ifdef CONFIG_X86_32
/* cpu data as detected by the assembly code in head.S */
struct cpuinfo_x86 new_cpu_data __cpuinitdata = {0, 0, 0, 0, -1, 1, 0, 0, -1};
/* common cpu data for all cpus */
struct cpuinfo_x86 boot_cpu_data __read_mostly = {0, 0, 0, 0, -1, 1, 0, 0, -1};
EXPORT_SYMBOL(boot_cpu_data);
static void set_mca_bus(int x)
{
#ifdef CONFIG_MCA
	MCA_bus = x;
#endif
}

unsigned int def_to_bigsmp;

/* for MCA, but anyone else can use it if they want */
unsigned int machine_id;
unsigned int machine_submodel_id;
unsigned int BIOS_revision;

struct apm_info apm_info;
EXPORT_SYMBOL(apm_info);

#if defined(CONFIG_X86_SPEEDSTEP_SMI) || \
	defined(CONFIG_X86_SPEEDSTEP_SMI_MODULE)
struct ist_info ist_info;
EXPORT_SYMBOL(ist_info);
#else
struct ist_info ist_info;
#endif

#else
/* boot_cpu_data 초기값 */
struct cpuinfo_x86 boot_cpu_data __read_mostly = {
	.x86_phys_bits = MAX_PHYSMEM_BITS,
};
EXPORT_SYMBOL(boot_cpu_data);
#endif


#if !defined(CONFIG_X86_PAE) || defined(CONFIG_X86_64)
unsigned long mmu_cr4_features;
#else
unsigned long mmu_cr4_features = X86_CR4_PAE;
#endif

/* Boot loader ID and version as integers, for the benefit of proc_dointvec */
int bootloader_type, bootloader_version;

/*
 * Setup options
 */
struct screen_info screen_info;
EXPORT_SYMBOL(screen_info);
struct edid_info edid_info;
EXPORT_SYMBOL_GPL(edid_info);

extern int root_mountflags;

unsigned long saved_video_mode;

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char __initdata command_line[COMMAND_LINE_SIZE];
#ifdef CONFIG_CMDLINE_BOOL
static char __initdata builtin_cmdline[COMMAND_LINE_SIZE] = CONFIG_CMDLINE;
#endif

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
struct edd edd;
#ifdef CONFIG_EDD_MODULE
EXPORT_SYMBOL(edd);
#endif
/**
 * copy_edd() - Copy the BIOS EDD information
 *              from boot_params into a safe place.
 *
 */
/* EDD 정보를 복사한다. */
static inline void __init copy_edd(void)
{
     memcpy(edd.mbr_signature, boot_params.edd_mbr_sig_buffer,
	    sizeof(edd.mbr_signature));
     memcpy(edd.edd_info, boot_params.eddbuf, sizeof(edd.edd_info));
     edd.mbr_signature_nr = boot_params.edd_mbr_sig_buf_entries;
     edd.edd_info_nr = boot_params.eddbuf_entries;
}
#else
static inline void __init copy_edd(void)
{
}
#endif
/* align으로 메모리 할당 */
void * __init extend_brk(size_t size, size_t align)
{
	size_t mask = align - 1;
	void *ret;

	BUG_ON(_brk_start == 0);
	BUG_ON(align & mask);	/* 단위가 겹치면 안됨 */

	_brk_end = (_brk_end + mask) & ~mask; /* 단위올림 정렬 */
	BUG_ON((char *)(_brk_end + size) > __brk_limit); /* 한계범위를 초과하면 메세지 출력후 멈춘다. */

	ret = (void *)_brk_end;	/* 할당된 시작위치 */
	_brk_end += size;	/* break를 증가 = size만큼 메모리 할당 */

	memset(ret, 0, size);	/* 할당한 영역의 섬세한 초기화 */

	return ret;
}

#ifdef CONFIG_X86_64
static void __init init_gbpages(void)
{
	/* 1GB 페이징이 가능하면 출력 */
	if (direct_gbpages && cpu_has_gbpages)
		printk(KERN_INFO "Using GB pages for direct mapping\n");
	else
		direct_gbpages = 0;
}
#else
static inline void init_gbpages(void)
{
}
static void __init cleanup_highmap(void)
{
}
#endif

static void __init reserve_brk(void)
{
	/* break 영역을 예약한다. */
	if (_brk_end > _brk_start)
		memblock_x86_reserve_range(__pa(_brk_start), __pa(_brk_end), "BRK");

	/* Mark brk area as locked down and no longer taking any
	   new allocations */
	_brk_start = 0;
}

#ifdef CONFIG_BLK_DEV_INITRD

#define MAX_MAP_CHUNK	(NR_FIX_BTMAPS << PAGE_SHIFT)
static void __init relocate_initrd(void)
{
	/* Assume only end is not page aligned */
	u64 ramdisk_image = boot_params.hdr.ramdisk_image;
	u64 ramdisk_size  = boot_params.hdr.ramdisk_size;
	u64 area_size     = PAGE_ALIGN(ramdisk_size);
	u64 end_of_lowmem = max_low_pfn_mapped << PAGE_SHIFT;
	u64 ramdisk_here;
	unsigned long slop, clen, mapaddr;
	char *p, *q;

	/* We need to move the initrd down into lowmem */
	ramdisk_here = memblock_find_in_range(0, end_of_lowmem, area_size,
					 PAGE_SIZE);

	if (ramdisk_here == MEMBLOCK_ERROR)
		panic("Cannot find place for new RAMDISK of size %lld\n",
			 ramdisk_size);

	/* Note: this includes all the lowmem currently occupied by
	   the initrd, we rely on that fact to keep the data intact. */
	memblock_x86_reserve_range(ramdisk_here, ramdisk_here + area_size, "NEW RAMDISK");
	initrd_start = ramdisk_here + PAGE_OFFSET;
	initrd_end   = initrd_start + ramdisk_size;
	printk(KERN_INFO "Allocated new RAMDISK: %08llx - %08llx\n",
			 ramdisk_here, ramdisk_here + ramdisk_size);

	q = (char *)initrd_start;

	/* Copy any lowmem portion of the initrd */
	if (ramdisk_image < end_of_lowmem) {
		clen = end_of_lowmem - ramdisk_image;
		p = (char *)__va(ramdisk_image);
		memcpy(q, p, clen);
		q += clen;
		ramdisk_image += clen;
		ramdisk_size  -= clen;
	}

	/* Copy the highmem portion of the initrd */
	while (ramdisk_size) {
		slop = ramdisk_image & ~PAGE_MASK;
		clen = ramdisk_size;
		if (clen > MAX_MAP_CHUNK-slop)
			clen = MAX_MAP_CHUNK-slop;
		mapaddr = ramdisk_image & PAGE_MASK;
		p = early_memremap(mapaddr, clen+slop);
		memcpy(q, p+slop, clen);
		early_iounmap(p, clen+slop);
		q += clen;
		ramdisk_image += clen;
		ramdisk_size  -= clen;
	}
	/* high pages is not converted by early_res_to_bootmem */
	ramdisk_image = boot_params.hdr.ramdisk_image;
	ramdisk_size  = boot_params.hdr.ramdisk_size;
	printk(KERN_INFO "Move RAMDISK from %016llx - %016llx to"
		" %08llx - %08llx\n",
		ramdisk_image, ramdisk_image + ramdisk_size - 1,
		ramdisk_here, ramdisk_here + ramdisk_size - 1);
}

static void __init reserve_initrd(void)
{
	/* Assume only end is not page aligned */
	u64 ramdisk_image = boot_params.hdr.ramdisk_image;
	u64 ramdisk_size  = boot_params.hdr.ramdisk_size;
	u64 ramdisk_end   = PAGE_ALIGN(ramdisk_image + ramdisk_size);
	u64 end_of_lowmem = max_low_pfn_mapped << PAGE_SHIFT;

	if (!boot_params.hdr.type_of_loader ||
	    !ramdisk_image || !ramdisk_size)
		return;		/* No initrd provided by bootloader */

	initrd_start = 0;

	if (ramdisk_size >= (end_of_lowmem>>1)) {
		memblock_x86_free_range(ramdisk_image, ramdisk_end);
		printk(KERN_ERR "initrd too large to handle, "
		       "disabling initrd\n");
		return;
	}

	printk(KERN_INFO "RAMDISK: %08llx - %08llx\n", ramdisk_image,
			ramdisk_end);


	if (ramdisk_end <= end_of_lowmem) {
		/* All in lowmem, easy case */
		/*
		 * don't need to reserve again, already reserved early
		 * in i386_start_kernel
		 */
		initrd_start = ramdisk_image + PAGE_OFFSET;
		initrd_end = initrd_start + ramdisk_size;
		return;
	}

	relocate_initrd();

	memblock_x86_free_range(ramdisk_image, ramdisk_end);
}
#else
static void __init reserve_initrd(void)
{
}
#endif /* CONFIG_BLK_DEV_INITRD */

/* 여분의 setup_data를 파싱한다. */
static void __init parse_setup_data(void)
{
	/* struct setup_data { */
	/* 	__u64 next; */
	/* 	__u32 type; */
	/* 	__u32 len; */
	/* 	__u8 data[0]; */
	/* }; */
	struct setup_data *data;
	u64 pa_data;

	/* BOOT PROTOCOL 2.09 부터 지원 */
	/* Protocol 2.09:	(Kernel 2.6.26) Added a field of 64-bit physical */
	/* pointer to single linked list of struct	setup_data. */
	if (boot_params.hdr.version < 0x0209)
		return;
	/* 헤더의 setup_data심볼 */
	/* setup_data:		.quad 0			# 64-bit physical pointer to */
	/* 						# single linked list of */
	/* 						# struct setup_data */
	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		u32 data_len, map_len;

		map_len = max(PAGE_SIZE - (pa_data & ~PAGE_MASK),
			      (u64)sizeof(struct setup_data)); /*  페이지 나머지 or  setup_data 구조체 크기중 최대값 */
		data = early_memremap(pa_data, map_len);
		data_len = data->len + sizeof(struct setup_data);
		if (data_len > map_len) {
			early_iounmap(data, map_len);
			data = early_memremap(pa_data, data_len);
			map_len = data_len;
		}

		switch (data->type) {
		case SETUP_E820_EXT:
			parse_e820_ext(data);
			break;
		case SETUP_DTB:
			add_dtb(pa_data);
			break;
		default:
			break;
		}
		pa_data = data->next;
		early_iounmap(data, map_len);
	}
}

static void __init e820_reserve_setup_data(void)
{
	struct setup_data *data;
	u64 pa_data;
	int found = 0;

	if (boot_params.hdr.version < 0x0209)
		return;
	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		e820_update_range(pa_data, sizeof(*data)+data->len,
			 E820_RAM, E820_RESERVED_KERN);
		found = 1;
		pa_data = data->next;
		early_iounmap(data, sizeof(*data));
	}
	if (!found)
		return;

	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
	memcpy(&e820_saved, &e820, sizeof(struct e820map));
	printk(KERN_INFO "extended physical RAM map:\n");
	e820_print_map("reserve setup_data");
}
/* 확장 데이터들을 reserve 블럭에 등록한다. (sorted) */
static void __init memblock_x86_reserve_range_setup_data(void)
{
	struct setup_data *data;
	u64 pa_data;
	char buf[32];

	if (boot_params.hdr.version < 0x0209) /* setup_data가 추가된 버전 kernel 2.6.26 2008 8월 13일 */
		return;
	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		sprintf(buf, "setup data %x", data->type);
		memblock_x86_reserve_range(pa_data, pa_data+sizeof(*data)+data->len, buf);
		pa_data = data->next;
		early_iounmap(data, sizeof(*data));
	}
}

/*
 * --------- Crashkernel reservation ------------------------------
 */

#ifdef CONFIG_KEXEC

static inline unsigned long long get_total_mem(void)
{
	unsigned long long total;

	total = max_pfn - min_low_pfn;

	return total << PAGE_SHIFT;
}

/*
 * Keep the crash kernel below this limit.  On 32 bits earlier kernels
 * would limit the kernel to the low 512 MiB due to mapping restrictions.
 * On 64 bits, kexec-tools currently limits us to 896 MiB; increase this
 * limit once kexec-tools are fixed.
 */
#ifdef CONFIG_X86_32
# define CRASH_KERNEL_ADDR_MAX	(512 << 20)
#else
# define CRASH_KERNEL_ADDR_MAX	(896 << 20)
#endif

static void __init reserve_crashkernel(void)
{
	unsigned long long total_mem;
	unsigned long long crash_size, crash_base;
	int ret;

	total_mem = get_total_mem();

	ret = parse_crashkernel(boot_command_line, total_mem,
			&crash_size, &crash_base);
	if (ret != 0 || crash_size <= 0)
		return;

	/* 0 means: find the address automatically */
	if (crash_base <= 0) {
		const unsigned long long alignment = 16<<20;	/* 16M */

		/*
		 *  kexec want bzImage is below CRASH_KERNEL_ADDR_MAX
		 */
		crash_base = memblock_find_in_range(alignment,
			       CRASH_KERNEL_ADDR_MAX, crash_size, alignment);

		if (crash_base == MEMBLOCK_ERROR) {
			pr_info("crashkernel reservation failed - No suitable area found.\n");
			return;
		}
	} else {
		unsigned long long start;

		start = memblock_find_in_range(crash_base,
				 crash_base + crash_size, crash_size, 1<<20);
		if (start != crash_base) {
			pr_info("crashkernel reservation failed - memory is in use.\n");
			return;
		}
	}
	memblock_x86_reserve_range(crash_base, crash_base + crash_size, "CRASH KERNEL");

	printk(KERN_INFO "Reserving %ldMB of memory at %ldMB "
			"for crashkernel (System RAM: %ldMB)\n",
			(unsigned long)(crash_size >> 20),
			(unsigned long)(crash_base >> 20),
			(unsigned long)(total_mem >> 20));

	crashk_res.start = crash_base;
	crashk_res.end   = crash_base + crash_size - 1;
	insert_resource(&iomem_resource, &crashk_res);
}
#else
static void __init reserve_crashkernel(void)
{
}
#endif

static struct resource standard_io_resources[] = {
	{ .name = "dma1", .start = 0x00, .end = 0x1f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic1", .start = 0x20, .end = 0x21,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer0", .start = 0x40, .end = 0x43,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer1", .start = 0x50, .end = 0x53,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x60, .end = 0x60,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x64, .end = 0x64,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma page reg", .start = 0x80, .end = 0x8f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic2", .start = 0xa0, .end = 0xa1,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma2", .start = 0xc0, .end = 0xdf,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "fpu", .start = 0xf0, .end = 0xff,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO }
};

void __init reserve_standard_io_resources(void)
{
	int i;

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < ARRAY_SIZE(standard_io_resources); i++)
		request_resource(&ioport_resource, &standard_io_resources[i]);

}

static __init void reserve_ibft_region(void)
{
	unsigned long addr, size = 0;
	addr = find_ibft_region(&size);

	if (size)
		memblock_x86_reserve_range(addr, addr + size, "* ibft");
}
/* low memory는 KB단위 */
static unsigned reserve_low = CONFIG_X86_RESERVE_LOW << 10;

static void __init trim_bios_range(void)
{
	/*
	 * A special case is the first 4Kb of memory;
	 * This is a BIOS owned area, not kernel ram, but generally
	 * not listed as such in the E820 table.
	 *
	 * This typically reserves additional memory (64KiB by default)
	 * since some BIOSes are known to corrupt low memory.  See the
	 * Kconfig help text for X86_RESERVE_LOW.
	 */
	/* Interrupt Vector Table, Bios Data area, 부트로더등의 영역을 low memory로 예약해놓는다. */
	/* 0부터 low로 예약된 영역(보통64KB)를 예약되었다고 표시 */
	e820_update_range(0, ALIGN(reserve_low, PAGE_SIZE),
			  E820_RAM, E820_RESERVED);

	/*
	 * special case: Some BIOSen report the PC BIOS
	 * area (640->1Mb) as ram even though it is not.
	 * take them out.
	 */
	/* 기본메모리(640KB) 제외한 384KB를 제거 */
	e820_remove_range(BIOS_BEGIN, BIOS_END - BIOS_BEGIN, E820_RAM, 1);
	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
}

static int __init parse_reservelow(char *p)
{
	unsigned long long size;

	if (!p)
		return -EINVAL;

	size = memparse(p, &p);

	if (size < 4096)
		size = 4096;

	if (size > 640*1024)
		size = 640*1024;

	reserve_low = size;

	return 0;
}

early_param("reservelow", parse_reservelow);

/*
 * Determine if we were loaded by an EFI loader.  If so, then we have also been
 * passed the efi memmap, systab, etc., so we should use these data structures
 * for initialization.  Note, the efi init code path is determined by the
 * global efi_enabled. This allows the same kernel image to be used on existing
 * systems (with a traditional BIOS) as well as on EFI systems.
 * EFI : http://ko.wikipedia.org/wiki/%ED%99%95%EC%9E%A5_%ED%8E%8C%EC%9B%A8%EC%96%B4_%EC%9D%B8%ED%84%B0%ED%8E%98%EC%9D%B4%EC%8A%A4
 */
/*
 * setup_arch - architecture-specific boot-time initializations
 *
 * Note: On x86_64, fixmaps are ready for use even before this is called.
 */

void __init setup_arch(char **cmdline_p)
{
#ifdef CONFIG_X86_32
	memcpy(&boot_cpu_data, &new_cpu_data, sizeof(new_cpu_data));
	visws_early_detect();		/* SGI workstation 초기화 */

	/*
	 * copy kernel address range established so far and switch
	 * to the proper swapper page table
	 */
	clone_pgd_range(swapper_pg_dir     + KERNEL_PGD_BOUNDARY,
			initial_page_table + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);

	load_cr3(swapper_pg_dir);	/* cr3를 swapper_pg_dir로 세팅(물리주소로 변환)
					 * 커널의 페이징 위치는 swapper_pg_dir이다. */
	__flush_tlb_all();			/* 페이징 변경후엔 tlb를 초기화 */
#else
	printk(KERN_INFO "Command line: %s\n", boot_command_line);
#endif

	/* 
	 * If we have OLPC OFW, we might end up relocating the fixmap due to
	 * reserve_top(), so do this before touching the ioremap area.
	 */

	// http://blog.naver.com/PostView.nhn?blogId=idthek&logNo=90120709677&redirect=Dlog&widgetTypeCall=true
	// http://gnudevel.tistory.com/31
	olpc_ofw_detect();			/* 어린이의, 어린이를 위한 OLPC open firmware */

	early_trap_init();			/* breakpoint(3), debug(1), page fault(14) 인터럽트 게이트를 등록하고 lidt 명령어로 idtr에 등록한다. */
	early_cpu_init();
	early_ioremap_init(); // pmd를 fixmap으로 설정해준다.

	setup_olpc_ofw_pgd();  // not kernel , so pass i'am cool

	ROOT_DEV = old_decode_dev(boot_params.hdr.root_dev); /* 옛 장치의 major/minor 번호를 8/8에서 12/20으로 확장 dev_t형 (__kernel_dev_t) */
	screen_info = boot_params.screen_info; // screen 초기화변수 넣기. 
	edid_info = boot_params.edid_info;	   /* 모니터에 대한 자세한 정보 - 제조사 이름, 제품 유형, edid 버전 등등 */
#ifdef CONFIG_X86_32
	apm_info.bios = boot_params.apm_bios_info;
	ist_info = boot_params.ist_info;
	if (boot_params.sys_desc_table.length != 0) {
		set_mca_bus(boot_params.sys_desc_table.table[3] & 0x2);
		machine_id = boot_params.sys_desc_table.table[0];
		machine_submodel_id = boot_params.sys_desc_table.table[1];
		BIOS_revision = boot_params.sys_desc_table.table[2];
	}
#endif
	saved_video_mode = boot_params.hdr.vid_mode;
	bootloader_type = boot_params.hdr.type_of_loader; /* type_of_loader에 타입과 버전 정보가 있다 */
	if ((bootloader_type >> 4) == 0xe) {			  /* type이 0xe면 extended 정보가 활성화 */
		bootloader_type &= 0xf;
		bootloader_type |= (boot_params.hdr.ext_loader_type+0x10) << 4;
	}
	bootloader_version  = bootloader_type & 0xf;
	bootloader_version |= boot_params.hdr.ext_loader_ver << 4;

/* 
 * #define RAMDISK_IMAGE_START_MASK	0x07FF
 * #define RAMDISK_PROMPT_FLAG		0x8000
 * #define RAMDISK_LOAD_FLAG		0x4000
 */
#ifdef CONFIG_BLK_DEV_RAM
/* 커널은 initrd 를 보통의 램디스크로 변환하고  initrd 에 의하여 사용된 메모리를 풀어놓는다. 
 * ram_size 옵션은 현재 사용하지 않는다. 옛날에 bootsect.S에서 사용되었다.
 */
	rd_image_start = boot_params.hdr.ram_size & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((boot_params.hdr.ram_size & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((boot_params.hdr.ram_size & RAMDISK_LOAD_FLAG) != 0);
#endif
/* 
 * How differ Bios between efi ? 
 * The Basic Input/Output System (BIOS) served as the OS-firmware interface for the original PC-XT and PC-AT computers.
 * This interface has been expanded over the years as the "PC clone" market has grown, but was never fully modernized as the market grew.
 * UEFI defines a similar OS-firmware interface, known as "boot services" and "runtime services", but is not specific to any processor architecture.
 * BIOS is specific to the Intel x86 processor architecture, as it relies on the 16-bit "real mode" interface supported by x86 processors.
 * UEFI is an interface. It can be implemented on top of a traditional BIOS (in which case it supplants the traditional "INT" entry points into BIOS) or on top of non-BIOS implementations.
 */

/* EFI를 지원하면 OS 비트 확인후 관련 세팅 */
#ifdef CONFIG_EFI
	if (!strncmp((char *)&boot_params.efi_info.efi_loader_signature,
#ifdef CONFIG_X86_32
		     "EL32",
#else
		     "EL64",
#endif
		     4)) {
		efi_enabled = 1; /* efi가 켜있으면 1 */
		efi_memblock_x86_reserve_range();
		
	}
#endif

	x86_init.oem.arch_setup();	/* 없다 */

	iomem_resource.end = (1ULL << boot_cpu_data.x86_phys_bits) - 1; /* 물리 메모리 상한선 early_cpu_init에서 호출. */
	setup_memory_map();
	parse_setup_data();
	/* update the e820_saved too */
	e820_reserve_setup_data();

	copy_edd();

	/* root_flags가 1이면 read only == 옵션 ro와 같다. */
	if (!boot_params.hdr.root_flags)
		root_mountflags &= ~MS_RDONLY;
	/* 코드, 데이터 영역 설정 */
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = _brk_end;		/* heap은 bss 위쪽에 있다. */

	/* 물리주소 영역들도 세팅 */
	code_resource.start = virt_to_phys(_text);
	code_resource.end = virt_to_phys(_etext)-1;
	data_resource.start = virt_to_phys(_etext);
	data_resource.end = virt_to_phys(_edata)-1;
	bss_resource.start = virt_to_phys(&__bss_start);
	bss_resource.end = virt_to_phys(&__bss_stop)-1;

#ifdef CONFIG_CMDLINE_BOOL
#ifdef CONFIG_CMDLINE_OVERRIDE
	strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
#else
	if (builtin_cmdline[0]) {
		/* append boot loader cmdline to builtin */
		strlcat(builtin_cmdline, " ", COMMAND_LINE_SIZE);
		strlcat(builtin_cmdline, boot_command_line, COMMAND_LINE_SIZE);
		strlcpy(boot_command_line, builtin_cmdline, COMMAND_LINE_SIZE);
	}
#endif
#endif
	/* 커맨드라인 복사 */
	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	/*
	 * x86_configure_nx() is called before parse_early_param() to detect
	 * whether hardware doesn't support NX (so that the early EHCI debug
	 * console setup can safely call set_fixmap()). It may then be called
	 * again from within noexec_setup() during parsing early parameters
	 * to honor the respective command line option.
	 */
	/* NX비트 관련 체크를 하고 NX비트가 사용가능하면 pte_mask에 세팅한다. */
	x86_configure_nx();
	/* early 파라미터들을 호출한다. */
	parse_early_param();

	x86_report_nx();

	/* after early param, so could get panic from serial */
	memblock_x86_reserve_range_setup_data();

	/* Advanced Configuration and Power Interface */
	/* 보통은 acpi를 지원하기 때문에 이쪽은 실행되지 않는다. */
	if (acpi_mps_check()) {
#ifdef CONFIG_X86_LOCAL_APIC
		disable_apic = 1; /* apic 를 사용하지 않는다. */
#endif
		/* cpu 수집 정보에서 APIC를 꺼주고 표시한다. */
		setup_clear_cpu_cap(X86_FEATURE_APIC);
	}
	/* command line에 early_dump 옵션이 켜있으면 장치 출력 */
#ifdef CONFIG_PCI
	if (pci_early_dump_regs)
		early_dump_pci_devices();
#endif

	finish_e820_parsing();

	if (efi_enabled)    /* CONFIG_EFI가 켜있으면 export되어 1이다. */
		efi_init();	/* EFI runtime 서비스를 사용가능하게 한다. */

	dmi_scan_machine();

	/*
	 * VMware detection requires dmi to be available, so this
	 * needs to be done after dmi_scan_machine, for the BP.
	 */
	init_hypervisor_platform(); /* 하이퍼바이저 체크 및 초기화 */

	x86_init.resources.probe_roms(); /* 롬 영역을 스캔하고 자원을 등록한다. */

	/* after parse_early_param, so could debug it */
	/* iomem 리소스 아래에 code 리소스를 등록  */
	insert_resource(&iomem_resource, &code_resource);
	insert_resource(&iomem_resource, &data_resource);
	insert_resource(&iomem_resource, &bss_resource);

	trim_bios_range();	/* 하위 1M 메모리의 앞뒤를 짤라준디. */
#ifdef CONFIG_X86_32
	if (ppro_with_ram_bug()) {
		e820_update_range(0x70000000ULL, 0x40000ULL, E820_RAM,
				  E820_RESERVED);
		sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
		printk(KERN_INFO "fixed physical RAM map:\n");
		e820_print_map("bad_ppro");
	}
#else
	/* PCI포트를 통해 AGP가 있는지 검색하고 관련 작업들을 한다. */
	early_gart_iommu_check();
#endif

	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	/* 최대 페이지 넘버 값 */
	max_pfn = e820_end_of_ram_pfn();

	/* update e820 for memory not covered by WB MTRRs */
	mtrr_bp_init();		/* 캐시를 위해 mtrr을 초기화한다. */
	/* Intel 아키텍쳐에서 WB가 아닌 부분을 e820에서 예약됨으로 업데이트 한다.
	 * 이렇게 trim되고 e820이 갱신된 부분이 있으면 최대 페이지값을 갱신한다.
	 */
	if (mtrr_trim_uncached_memory(max_pfn))
		max_pfn = e820_end_of_ram_pfn();

#ifdef CONFIG_X86_32
	/* max_low_pfn get updated here */
	/* low와 high를 찾는다. */
	find_low_pfn_range();
#else
	/* 최대 물리 페이지 수를 세팅 */
	num_physpages = max_pfn;

	check_x2apic();		/* intel x2apic 를 지원하는지 체크 */

	/* How many end-of-memory variables you have, grandma! */
	/* need this before calling reserve_initrd */
	/* 메모리크기가 4G보다 크면 e820, 즉 low는 최대 4G이다. */
	if (max_pfn > (1UL<<(32 - PAGE_SHIFT)))
		max_low_pfn = e820_end_of_low_ram_pfn(); /* max_low_pfn은 max가 4G */
	else
		max_low_pfn = max_pfn; /* 4G 이하면 그냥 PFN 최대값 */
	/* 가용 메모리 주소 (가상주소) 끝을 high_memory로 둔다. */
	high_memory = (void *)__va(max_pfn * PAGE_SIZE - 1) + 1;
#endif

	/*
	 * Find and reserve possible boot-time SMP configuration:
	 */
	find_smp_config();	/* 기본메모리에서 smp를 찾아서 예약한다. */

	reserve_ibft_region();	/* iscsi boot firmware table을 검색하고 예약 */

	/*
	 * Need to conclude brk, before memblock_x86_fill()
	 *  it could use memblock_find_in_range, could overlap with
	 *  brk area.
	 */
	reserve_brk();

	cleanup_highmap();	/* 커널영역 제외 초기화 */

	memblock.current_limit = get_max_mapped(); /* memblock의 한계를 세팅 (물리메모리?) */
	/* e820의 정보(RAM, KERN)를 memblock의 사용가능(memory)한 쪽에 더하고 체크한뒤
	 * debug 옵션이 켜있으면 memblock 정보를 출력한다.
	 */
	memblock_x86_fill();

	/*
	 * The EFI specification says that boot service code won't be called
	 * after ExitBootServices(). This is, in fact, a lie.
	 */
	if (efi_enabled)
		efi_reserve_boot_services();

	/* preallocate 4k for mptable mpc */
	early_reserve_e820_mpc_new();

#ifdef CONFIG_X86_CHECK_BIOS_CORRUPTION
	/* 바이오스의 커럽션 체크하는걸 셋업한다. */
	setup_bios_corruption_check();
#endif

	printk(KERN_DEBUG "initial memory mapped : 0 - %08lx\n",
			max_pfn_mapped<<PAGE_SHIFT);

	setup_trampolines();	/* trampoline코드를 1M이하로 복사 */

	init_gbpages();		/* 1GB 페이징이 가능한지 체크 */

	/* max_pfn_mapped is updated here */
	/* 4G까지의 메모리 매핑 초기화 */
	max_low_pfn_mapped = init_memory_mapping(0, max_low_pfn<<PAGE_SHIFT);
	max_pfn_mapped = max_low_pfn_mapped;

#ifdef CONFIG_X86_64
	/* 64비트 이상이고 메모리가 4G이상이면 해당 영역을 초기화 */
	if (max_pfn > max_low_pfn) {
		max_pfn_mapped = init_memory_mapping(1UL<<32,
						     max_pfn<<PAGE_SHIFT);
		/* can we preseve max_low_pfn ?*/
		max_low_pfn = max_pfn; /* 64비트는 결국 max_low와 max가 동일하다 */
	}
#endif
	memblock.current_limit = get_max_mapped();

	/*
	 * NOTE: On x86-32, only from this point on, fixmaps are ready for use.
	 */

#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	if (init_ohci1394_dma_early)
		init_ohci1394_dma_on_all_controllers();
#endif
	/* Allocate bigger log buffer */
	setup_log_buf(1);

	reserve_initrd();

	reserve_crashkernel();

	vsmp_init();

	io_delay_init();

	/*
	 * Parse the ACPI tables for possible boot-time SMP configuration.
	 */
	acpi_boot_table_init();

	early_acpi_boot_init();

	initmem_init();
	memblock_find_dma_reserve();

#ifdef CONFIG_KVM_CLOCK
	kvmclock_init();
#endif

	x86_init.paging.pagetable_setup_start(swapper_pg_dir);
	paging_init();
	x86_init.paging.pagetable_setup_done(swapper_pg_dir);

	if (boot_cpu_data.cpuid_level >= 0) {
		/* A CPU has %cr4 if and only if it has CPUID */
		mmu_cr4_features = read_cr4();
	}

#ifdef CONFIG_X86_32
	/* sync back kernel address range */
	clone_pgd_range(initial_page_table + KERNEL_PGD_BOUNDARY,
			swapper_pg_dir     + KERNEL_PGD_BOUNDARY,
			KERNEL_PGD_PTRS);
#endif

	tboot_probe();

#ifdef CONFIG_X86_64
	map_vsyscall();
#endif

	generic_apic_probe();

	early_quirks();

	/*
	 * Read APIC and some other early information from ACPI tables.
	 */
	acpi_boot_init();
	sfi_init();
	x86_dtb_init();

	/*
	 * get boot-time SMP configuration:
	 */
	if (smp_found_config)
		get_smp_config();

	prefill_possible_map();

	init_cpu_to_node();

	init_apic_mappings();
	ioapic_and_gsi_init();

	kvm_guest_init();

	e820_reserve_resources();
	e820_mark_nosave_regions(max_low_pfn);

	x86_init.resources.reserve_resources();

	e820_setup_gap();

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	if (!efi_enabled || (efi_mem_type(0xa0000) != EFI_CONVENTIONAL_MEMORY))
		conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
	x86_init.oem.banner();

	x86_init.timers.wallclock_init();

	x86_platform.wallclock_init();

	mcheck_init();

	arch_init_ideal_nops();
}

#ifdef CONFIG_X86_32

static struct resource video_ram_resource = {
	.name	= "Video RAM area",
	.start	= 0xa0000,
	.end	= 0xbffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

void __init i386_reserve_resources(void)
{
	request_resource(&iomem_resource, &video_ram_resource);
	reserve_standard_io_resources();
}

#endif /* CONFIG_X86_32 */
