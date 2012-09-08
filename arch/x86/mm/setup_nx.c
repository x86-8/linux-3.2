#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/pgtable.h>
#include <asm/proto.h>

static int disable_nx __cpuinitdata;

/*
 * noexec = on|off
 *
 * Control non-executable mappings for processes.
 *
 * on      Enable
 * off     Disable
 */
static int __init noexec_setup(char *str)
{
	if (!str)
		return -EINVAL;
	if (!strncmp(str, "on", 2)) {
		disable_nx = 0;
	} else if (!strncmp(str, "off", 3)) {
		disable_nx = 1;
	}
	x86_configure_nx();
	return 0;
}
early_param("noexec", noexec_setup);

void __cpuinit x86_configure_nx(void)
{
  /* NX비트(No execute)를 CPU에서 지원하고 disable되어있지 않다면
   * __supported_pte_mask의(pte페이지타입) NX 비트를 켜주고 아니면 끈다.
   * NX는 64비트에서 최상위(63) 비트다.
   * _PAGE_NX는 PAE나 64비트여야 된다.
   * 
   * <인텔 메뉴얼 인용>
   * 5.6.1 No Execute (NX) Bit
   * The NX bit in the page-translation tables specifies whether instructions can be executed from the page.
   * This bit is not checked during every instruction fetch. Instead, the NX bits in the page-translation-table
   * entries are checked by the processor when the instruction TLB is loaded with a page translation. The
   * processor attempts to load the translation into the instruction TLB when an instruction fetch misses the
   * TLB. If a set NX bit is detected (indicating the page is not executable), a page-fault exception (#PF)
   * occurs.
   * The no-execute protection check applies to all privilege levels. It does not distinguish between
   * supervisor and user-level accesses.
   * The no-execute protection feature is supported only in PAE-paging mode. It is enabled by setting the
   * NXE bit in the EFER register to 1 (see “Extended Feature Enable Register (EFER)” on page 54).
   * Before setting this bit, system software must verify the processor supports the NX feature by checking
   * the CPUID extended-feature flags (see the CPUID Specification, order# 25481).
   * REQUIRED bit
   */
	/* cpu에서 NX비트를 사용가능하고 NX를 disable 하지 않았다면  */
	if (cpu_has_nx && !disable_nx)
	/* NX mask를 켜서 NX비트 사용가능하게 한다.  */
		__supported_pte_mask |= _PAGE_NX;
	else
	/* NX비트는 사용불가능 */
		__supported_pte_mask &= ~_PAGE_NX;
}

void __init x86_report_nx(void)
{
  /*  NX를 지원하지 않으면 친절하게 알려준다. */
	if (!cpu_has_nx) {
		printk(KERN_NOTICE "Notice: NX (Execute Disable) protection "
		       "missing in CPU!\n");
	} else {
	  /* 커널님의 친절한 알림 */
#if defined(CONFIG_X86_64) || defined(CONFIG_X86_PAE)
		if (disable_nx) {
			printk(KERN_INFO "NX (Execute Disable) protection: "
			       "disabled by kernel command line option\n");
		} else {
			printk(KERN_INFO "NX (Execute Disable) protection: "
			       "active\n");
		}
#else
		/* 32bit non-PAE kernel, NX cannot be used */
		printk(KERN_NOTICE "Notice: NX (Execute Disable) protection "
		       "cannot be enabled: non-PAE kernel!\n");
#endif
	}
}
