/*
 *	header.S
 *
 *	Copyright (C) 1991, 1992 Linus Torvalds
 *
 *	Based on bootsect.S and setup.S
 *	modified by more people than can be counted
 *
 *	Rewritten as a common file by H. Peter Anvin (Apr 2007)
 *
 * BIG FAT NOTE: We're in real mode using 64k segments.  Therefore segment
 * addresses must be multiplied by 16 to obtain their respective linear
 * addresses. To avoid confusion, linear addresses are written using leading
 * hex while segment addresses are written as segment:offset.
 *
 */
/*
 * 커널의 시작부분이다. header는 BOOTSECT와 SETUP부분 첫부분이다.
 * header는 Boot protocol로 정보를 교환한다.
 * 앞쪽의 한섹터 (bootsect)는 끝의 헤더 데이터만 보면 된다. 코드는 두번째 섹터(setup)부터 실행한다.
 * 부트로더는 header를 통해 정보를 넘긴다. Documentation/x86/boot.txt 참조
 */
	
#include <asm/segment.h>
#include <generated/utsrelease.h>
#include <asm/boot.h>
#include <asm/e820.h>
#include <asm/page_types.h>
#include <asm/setup.h>
#include "boot.h"
#include "voffset.h"
#include "zoffset.h"
/* 0x7c0은 부팅할때의 세그먼트, 0x1000은 1M이하로 커널이 로드됐을때의 영역 */
BOOTSEG		= 0x07C0		/* original address of boot-sector */
SYSSEG		= 0x1000		/* historical load address >> 4 */

#ifndef SVGA_MODE
#define SVGA_MODE ASK_VGA
#endif

#ifndef RAMDISK
#define RAMDISK 0
#endif

#ifndef ROOT_RDONLY
#define ROOT_RDONLY 1
#endif

	.code16
	.section ".bstext", "ax"    /* bstext 섹션에 넣는다. allocatable, executable */

	.global bootsect_start
bootsect_start:

	# Normalize the start address
	ljmp	$BOOTSEG, $start2

start2:
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es
	movw	%ax, %ss
	xorw	%sp, %sp
	sti
	cld

	movw	$bugger_off_msg, %si
		/* 커널의 맨 앞쪽 bootsect는 이제 쓰이지 않는다. 만약 이쪽으로 점프해왔다면 메세지 출력후 컴퓨터를 재시작한다. */
msg_loop:
	lodsb
	andb	%al, %al
	jz	bs_die
	movb	$0xe, %ah
	movw	$7, %bx
	int	$0x10
	jmp	msg_loop

bs_die:
	# Allow the user to press a key, then reboot
	xorw	%ax, %ax
	int	$0x16  # 키입력 대기
	int	$0x19  # reboot

	# int 0x19 should never return.  In case it does anyway,
	# invoke the BIOS reset code...
	# 0xffff0은 부팅시 cpu가 bios를 호출하는 주소 & reboot 코드
	# int 19h가 실패하면 이 방법으로 리붓한다.
	ljmp	$0xf000,$0xfff0

	.section ".bsdata", "a"
bugger_off_msg:
	.ascii	"Direct booting from floppy is no longer supported.\r\n"
	.ascii	"Please use a boot loader program instead.\r\n"
	.ascii	"\n"
	.ascii	"Remove disk and press any key to reboot . . .\r\n"
	.byte	0


	# Kernel attributes; used by setup.  This is part 1 of the
	# header, from the old boot sector.
	# bootsect의 끝부분부터 boot protocol의 정보가 담긴다.
	.section ".header", "a"
	.globl	hdr
hdr:
setup_sects:	.byte 0			/* Filled in by build.c */
root_flags:	.word ROOT_RDONLY
syssize:	.long 0			/* Filled in by build.c */
ram_size:	.word 0			/* Obsolete */
vid_mode:	.word SVGA_MODE
root_dev:	.word 0			/* Filled in by build.c */
boot_flag:	.word 0xAA55

	# offset 512, entry point
	# 여기부터 시작이다.(setup). 점프 코드 뒤에 header 정보가 이어진다.
	.globl	_start
_start:
		# Explicitly enter this as bytes, or the assembler
		# tries to generate a 3-byte jump here, which causes
		# everything else to push off to the wrong offset.
		.byte	0xeb		# short (2-byte) jump
		.byte	start_of_setup-1f # 점프의 위치는 다음 명령어 위치가 0이다. 다음 위치(start_of_setup)를 빼면 건너뛸 값이 나온다. 결국 start_of_setup으로 점프한다.
1:

	# Part 2 of the header, from the old setup.S

		.ascii	"HdrS"		# header signature
		.word	0x020a		# header version number (>= 0x0105)
					# or else old loadlin-1.5 will fail)
		.globl realmode_swtch	/* 전역으로 선언 */
realmode_swtch:	.word	0, 0		# default_switch, SETUPSEG
start_sys_seg:	.word	SYSSEG		# obsolete and meaningless, but just
					# in case something decided to "use" it
		.word	kernel_version-512 # pointing to kernel version string
					# above section of header is compatible
					# with loadlin-1.5 (header v1.5). Don't
					# change it.

type_of_loader:	.byte	0		# 0 means ancient bootloader, newer
					# bootloaders know to change this.
					# See Documentation/i386/boot.txt for
					# assigned ids

# flags, unused bits must be zero (RFU) bit within loadflags
loadflags:
LOADED_HIGH	= 1			# If set, the kernel is loaded high
CAN_USE_HEAP	= 0x80			# If set, the loader also has set
					# heap_end_ptr to tell how much
					# space behind setup.S can be used for
					# heap purposes.
					# Only the loader knows what is free
		.byte	LOADED_HIGH

setup_move_size: .word  0x8000		# size to move, when setup is not
					# loaded at 0x90000. We will move setup
					# to 0x90000 then just before jumping
					# into the kernel. However, only the
					# loader knows how much data behind
					# us also needs to be loaded.

code32_start:				# here loaders can put a different
					# start address for 32-bit code.
		.long	0x100000	# 0x100000 = default for big kernel

ramdisk_image:	.long	0		# address of loaded ramdisk image
					# Here the loader puts the 32-bit
					# address where it loaded the image.
					# This only will be read by the kernel.

ramdisk_size:	.long	0		# its size in bytes

bootsect_kludge:
		.long	0		# obsolete

heap_end_ptr:	.word	_end+STACK_SIZE-512
					# (Header version 0x0201 or later)
					# space from here (exclusive) down to
					# end of setup code can be used by setup
					# for local heap purposes.

ext_loader_ver:
		.byte	0		# Extended boot loader version
ext_loader_type:
		.byte	0		# Extended boot loader type

cmd_line_ptr:	.long	0		# (Header version 0x0202 or later)
					# If nonzero, a 32-bit pointer
					# to the kernel command line.
					# The command line should be
					# located between the start of
					# setup and the end of low
					# memory (0xa0000), or it may
					# get overwritten before it
					# gets read.  If this field is
					# used, there is no longer
					# anything magical about the
					# 0x90000 segment; the setup
					# can be located anywhere in
					# low memory 0x10000 or higher.

ramdisk_max:	.long 0x7fffffff
					# (Header version 0x0203 or later)
					# The highest safe address for
					# the contents of an initrd
					# The current kernel allows up to 4 GB,
					# but leave it at 2 GB to avoid
					# possible bootloader bugs.

kernel_alignment:  .long CONFIG_PHYSICAL_ALIGN	#physical addr alignment # 기본 16M
						#required for protected mode
						#kernel
#ifdef CONFIG_RELOCATABLE
relocatable_kernel:    .byte 1
#else
relocatable_kernel:    .byte 0
#endif
min_alignment:		.byte MIN_KERNEL_ALIGN_LG2	# minimum alignment
pad3:			.word 0

cmdline_size:   .long   COMMAND_LINE_SIZE-1     #length of the command line,
                                                #added with boot protocol
                                                #version 2.06

hardware_subarch:	.long 0			# subarchitecture, added with 2.07
						# default to 0 for normal x86 PC

hardware_subarch_data:	.quad 0

payload_offset:		.long ZO_input_data
payload_length:		.long ZO_z_input_len

setup_data:		.quad 0			# 64-bit physical pointer to
						# single linked list of
						# struct setup_data

pref_address:		.quad LOAD_PHYSICAL_ADDR	# preferred load addr

#define ZO_INIT_SIZE	(ZO__end - ZO_startup_32 + ZO_z_extract_offset)
#define VO_INIT_SIZE	(VO__end - VO__text)
#if ZO_INIT_SIZE > VO_INIT_SIZE
#define INIT_SIZE ZO_INIT_SIZE
#else
#define INIT_SIZE VO_INIT_SIZE
#endif
init_size:		.long INIT_SIZE		# kernel initialization size

# End of setup header #####################################################

	.section ".entrytext", "ax"
start_of_setup:
#ifdef SAFE_RESET_DISK_CONTROLLER
# Reset the disk controller.
	movw	$0x0000, %ax		# Reset disk controller
	movb	$0x80, %dl		# All disks
	int	$0x13
#endif

# Force %es = %ds
	movw	%ds, %ax
	movw	%ax, %es
	cld

# Apparently some ancient versions of LILO invoked the kernel with %ss != %ds,
# which happened to work by accident for the old code.  Recalculate the stack
# pointer if %ss is invalid.  Otherwise leave it alone, LOADLIN sets up the
# stack behind its own code, so we can't blindly put it directly past the heap.
	# 스택 계산하는 코드
	movw	%ss, %dx
	cmpw	%ax, %dx	# %ds == %ss?
	movw	%sp, %dx
	je	2f		# -> assume %sp is reasonably set # 보통은 2: 레이블로 간다.

	# Invalid %ss, make up a new stack # ss와 ds가 같지 않을때의 예외처리
	movw	$_end, %dx # 커널 setup 끝?
	testb	$CAN_USE_HEAP, loadflags
	jz	1f # heap을 사용하지 않으면 점프
	movw	heap_end_ptr, %dx # heap을 지원하면 dx에 주소를 넣는다.
1:	addw	$STACK_SIZE, %dx # dx에는 setup(혹은 kernel)의 끝이나 예약된 heap 끝부분이 들어있다. 여기에 스택크기를 더한다.
	jnc	2f
	xorw	%dx, %dx	# Prevent wraparound # 넘치면 리셋

2:	# Now %dx should point to the end of our stack space
	andw	$~3, %dx	# dword align (might as well...)
	jnz	3f
	movw	$0xfffc, %dx	# Make sure we're not zero # 주소가 세그먼트를 초과했으면 스택을 세그먼터 끝으로 지정한다.
3:	movw	%ax, %ss # ss=ds
	movzwl	%dx, %esp	# Clear upper half of %esp # 나중을 위해 esp 확장
	sti			# Now we should have a working stack

# We will have entered with %cs = %ds+0x20, normalize %cs so
# it is on par with the other segments.
	pushw	%ds
	pushw	$6f
	lretw # 바로 뒤로 점프. cs와 ds가 같아진다.
6:

# Check signature at end of setup
# setup signature 
	cmpl	$0x5a5aaa55, setup_sig # aa55 5a5a 확인
	jne	setup_bad

# Zero the bss
# BSS도 초기화한다.
	movw	$__bss_start, %di
	movw	$_end+3, %cx # 12바이트 더 쓴다?
	xorl	%eax, %eax
	subw	%di, %cx # 크기를 구하고
	shrw	$2, %cx # 4로 나눠서 count를 구한다.
	rep; stosl # BSS를 0으로 초기화

# Jump to C code (should not return)
	calll	main # main.c로 점프

# Setup corrupt somehow... # 돌아온다면 에러출력후 멈춘다.
setup_bad:
	movl	$setup_corrupt, %eax
	calll	puts
	# Fall through...

	.globl	die
	.type	die, @function
die:			# Die.
	hlt			# halt : Waiting for interrupt.
	jmp	die		# Roof.

	.size	die, .-die

	.section ".initdata", "a"
setup_corrupt:
	.byte	7
	.string	"No setup signature found...\n"
