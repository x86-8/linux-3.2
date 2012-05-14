/* -*- linux-c -*- ------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007-2008 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * arch/x86/boot/cpu.c
 *
 * Check for obligatory CPU features and abort if the features are not
 * present.
 */

#include "boot.h"
#include "cpustr.h"


/**
 @brief	CPU 레벨에 맞는 Vendor name을 리턴한다.
	 level 이 15 라면 level 6으로 간주한다.
 @return	Vendor Name
 */
static char *cpu_name(
		int level	///< CPU Level
		)
{
	static char buf[6];

	if (level == 64) {
		return "x86-64";
	} else {
		if (level == 15)
			level = 6;
		sprintf(buf, "i%d86", level);
		return buf;
	}
}


/**
 @brief	CPU를 검증한다.\n
	 CPU 가 지원하는 명령어 Set 에서 커널에서 필요한 명령어 Set 지원이 \n
	 안된다면 실패로 간주한다.
 @return	성공시:0, 실패시:-1
 */
int
validate_cpu(void)
{
	u32 *err_flags;
	int cpu_level, req_level;
	const unsigned char *msg_strs;

	// CPU Check...
	check_cpu(&cpu_level, &req_level, &err_flags);

	// CPU Level 이 낮으면..
	if (cpu_level < req_level) {
		printf("This kernel requires an %s CPU, ",
		       cpu_name(req_level));
		printf("but only detected an %s CPU.\n",
		       cpu_name(cpu_level));
		return -1;
	}

	// err_flags 에 설정된 에러 상태들을 출력한다.
	// CPU에서 어떤 명령어 Set 들이 미지원하는지에 대한 정보들을 출력한다.
	if (err_flags) {
		int i, j;
		puts("This kernel requires the following features "
		     "not present on the CPU:\n");

		msg_strs = (const unsigned char *)x86_cap_strs;

		for (i = 0; i < NCAPINTS; i++) {
			u32 e = err_flags[i];

			for (j = 0; j < 32; j++) {
				if (msg_strs[0] < i ||
				    (msg_strs[0] == i && msg_strs[1] < j)) {
					/* Skip to the next string */
					msg_strs += 2;
					while (*msg_strs++)
						;
				}
				if (e & 1) {
					if (msg_strs[0] == i &&
					    msg_strs[1] == j &&
					    msg_strs[2])
						printf("%s ", msg_strs+2);
					else
						printf("%d:%d ", i, j);
				}
				e >>= 1;
			}
		}
		putchar('\n');
		return -1;
	} else {
		return 0;
	}
}
