#ifndef _LINUX_PFN_H_
#define _LINUX_PFN_H_

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif
/* 올림 정렬 */
#define PFN_ALIGN(x)	(((unsigned long)(x) + (PAGE_SIZE - 1)) & PAGE_MASK)
/* 페이지 넘버(올림) */
#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
/* 페이지 넘버(내림) */
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)	((phys_addr_t)(x) << PAGE_SHIFT)

#endif
