#
# Makefile for local APIC drivers and for the IO-APIC code
#

obj-$(CONFIG_X86_LOCAL_APIC)	+= apic.o apic_noop.o ipi.o
obj-y				+= hw_nmi.o

obj-$(CONFIG_X86_IO_APIC)	+= io_apic.o
obj-$(CONFIG_SMP)		+= ipi.o
# 64비트에서 apic는 default로 apic_flat이다.
# 32비트에서 apic는 probe_32에서 apic_default를 default로 세팅한다.
# config에 따라 apic 코드들을 추가한다.
# apic 구조체가 apic_driver 매크로로 .apicdriver 섹션에 추가되며
# probe 함수가 callback될때 전역 apic에 대입되기도 한다.
ifeq ($(CONFIG_X86_64),y)
# APIC probe will depend on the listing order here
obj-$(CONFIG_X86_UV)		+= x2apic_uv_x.o
obj-$(CONFIG_X86_X2APIC)	+= x2apic_phys.o
obj-$(CONFIG_X86_X2APIC)	+= x2apic_cluster.o
obj-y				+= apic_flat_64.o
endif

# APIC probe will depend on the listing order here
obj-$(CONFIG_X86_NUMAQ)		+= numaq_32.o
obj-$(CONFIG_X86_SUMMIT)	+= summit_32.o
obj-$(CONFIG_X86_BIGSMP)	+= bigsmp_32.o
obj-$(CONFIG_X86_ES7000)	+= es7000_32.o

# For 32bit, probe_32 need to be listed last
obj-$(CONFIG_X86_LOCAL_APIC)	+= probe_$(BITS).o
