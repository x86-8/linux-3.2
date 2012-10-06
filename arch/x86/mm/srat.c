/*
 * ACPI 3.0 based NUMA setup
 * Copyright 2004 Andi Kleen, SuSE Labs.
 *
 * Reads the ACPI SRAT table to figure out what memory belongs to which CPUs.
 *
 * Called from acpi_numa_init while reading the SRAT and SLIT tables.
 * Assumes all memory regions belonging to a single proximity domain
 * are in one chunk. Holes between them will be included in the node.
 */

#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/mmzone.h>
#include <linux/bitmap.h>
#include <linux/module.h>
#include <linux/topology.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <asm/proto.h>
#include <asm/numa.h>
#include <asm/e820.h>
#include <asm/apic.h>
#include <asm/uv/uv.h>

int acpi_numa __initdata;
/* pxm 정보와 node를 매핑(초기화) */
static __init int setup_node(int pxm)
{
	return acpi_map_pxm_to_node(pxm);
}

static __init void bad_srat(void)
{
	printk(KERN_ERR "SRAT: SRAT not used.\n");
	acpi_numa = -1;
}

static __init inline int srat_disabled(void)
{
	return acpi_numa < 0;
}

/* Callback for SLIT parsing */
void __init acpi_numa_slit_init(struct acpi_table_slit *slit)
{
	int i, j;

	/* NODE 마다 distance 를 얻어와서 저장 */
	for (i = 0; i < slit->locality_count; i++)
		for (j = 0; j < slit->locality_count; j++)
			numa_set_distance(pxm_to_node(i), pxm_to_node(j),
				slit->entry[slit->locality_count * i + j]);
}

/* Callback for Proximity Domain -> x2APIC mapping */
void __init
acpi_numa_x2apic_affinity_init(struct acpi_srat_x2apic_cpu_affinity *pa)
{
	int pxm, node;
	int apic_id;

	if (srat_disabled())
		return;
	if (pa->header.length < sizeof(struct acpi_srat_x2apic_cpu_affinity)) {
		bad_srat();
		return;
	}
	if ((pa->flags & ACPI_SRAT_CPU_ENABLED) == 0)
		return;
	pxm = pa->proximity_domain;
	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains %x\n", pxm);
		bad_srat();
		return;
	}

	apic_id = pa->apic_id;
	if (apic_id >= MAX_LOCAL_APIC) {
		printk(KERN_INFO "SRAT: PXM %u -> APIC 0x%04x -> Node %u skipped apicid that is too big\n", pxm, apic_id, node);
		return;
	}
	set_apicid_to_node(apic_id, node);
	/* 파싱이 완료된(APIC로부터 찾아낸) node 설정 */
	node_set(node, numa_nodes_parsed);
	/* numa 완료 상태 설정 (-1일 경우 실패) */
	acpi_numa = 1;
	printk(KERN_INFO "SRAT: PXM %u -> APIC 0x%04x -> Node %u\n",
	       pxm, apic_id, node);
}

/* Callback for Proximity Domain -> LAPIC mapping */
void __init
acpi_numa_processor_affinity_init(struct acpi_srat_cpu_affinity *pa)
{
	int pxm, node;
	int apic_id;

	if (srat_disabled())
		return;
	if (pa->header.length != sizeof(struct acpi_srat_cpu_affinity)) {
		bad_srat();
		return;
	}
	if ((pa->flags & ACPI_SRAT_CPU_ENABLED) == 0)
		return;
	pxm = pa->proximity_domain_lo;
	/* ACPI로 부터 얻어온 pxm으로부터 node를 얻어온다 */
	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains %x\n", pxm);
		bad_srat();
		return;
	}

	/* ACPI로 부터 얻어온 apic id 설정 */
	if (get_uv_system_type() >= UV_X2APIC)
		apic_id = (pa->apic_id << 8) | pa->local_sapic_eid;
	else
		apic_id = pa->apic_id;

	if (apic_id >= MAX_LOCAL_APIC) {
		printk(KERN_INFO "SRAT: PXM %u -> APIC 0x%02x -> Node %u skipped apicid that is too big\n", pxm, apic_id, node);
		return;
	}

	/* node에 apic id 설정 */
	set_apicid_to_node(apic_id, node);
	node_set(node, numa_nodes_parsed);
	acpi_numa = 1;
	printk(KERN_INFO "SRAT: PXM %u -> APIC 0x%02x -> Node %u\n",
	       pxm, apic_id, node);
}

#ifdef CONFIG_MEMORY_HOTPLUG
static inline int save_add_info(void) {return 1;}
#else
static inline int save_add_info(void) {return 0;}
#endif

/* Callback for parsing of the Proximity Domain <-> Memory Area mappings */
void __init
acpi_numa_memory_affinity_init(struct acpi_srat_mem_affinity *ma)
{
	u64 start, end;
	int node, pxm;

	if (srat_disabled())
		return;
	if (ma->header.length != sizeof(struct acpi_srat_mem_affinity)) {
		bad_srat();
		return;
	}
	/* Memory Affinity (SRAT) 정보를 사용하지 않음 */
	if ((ma->flags & ACPI_SRAT_MEM_ENABLED) == 0)
		return;

	/* Memory Hot Pluggable 이사용되지만, CONFIG 에 설정되어 있지 않은 경우 */
	if ((ma->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE) && !save_add_info())
		return;

	/* ACPI(SRAT)로 얻어온 Memory Affinity 정보를 설정 */
	start = ma->base_address;
	end = start + ma->length;
	pxm = ma->proximity_domain;
	/* pxm 으로 node id를 가져옮 */
	node = setup_node(pxm);
	if (node < 0) {
		printk(KERN_ERR "SRAT: Too many proximity domains.\n");
		bad_srat();
		return;
	}

	/* node에 memory block을 추가 */
	if (numa_add_memblk(node, start, end) < 0) {
		bad_srat();
		return;
	}

	printk(KERN_INFO "SRAT: Node %u PXM %u %Lx-%Lx\n", node, pxm,
	       start, end);
}

void __init acpi_numa_arch_fixup(void) {}

int __init x86_acpi_numa_init(void)
{
	int ret;

	ret = acpi_numa_init();
	if (ret < 0)
		return ret;
	return srat_disabled() ? -EINVAL : 0;
}
