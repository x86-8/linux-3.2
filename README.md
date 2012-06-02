Linux kernel 스터디 iamroot x86-8 초급
========================================
* 1차 교재 = [리눅스 커널의 내부구조](http://www.yes24.com/24/goods/3080849)
* 2차 교재 = [만들면서 배우는 OS 커널의 구조와 원리](http://www.yes24.com/24/goods/1469757)
* 부트로더 = [LILO 23.2](http://code.google.com/p/linx86study8-lilo232)
* 커널분석 = [Linux 3.2](https://github.com/x86-8/linux-3.2.git)

Study history (날짜:인원:내용)
==============================
01. 2011/03/26:38:스터디 OT
02. 2011/04/02:34:<리눅스 커널 내부구조> 1-3장
03. 2011/04/09:25:<리눅스 커널 내부구조> 4장
04. 2011/04/16:21:<리눅스 커널 내부구조> 5-6장
05. 2011/04/23:05:<리눅스 커널 내부구조> 7-9장
06. 2011/04/30:18:<리눅스 커널 내부구조> 가상화, <만들면서 배우는 OS 커널의 구조와 원리> P.1-78
07. 2011/05/07:21:<만들면서 배우는 OS 캐널의 구조와 원리> P.79-117
08. 2011/05/14:19:<만들면서 배우는 OS 커널의 구조와 원리> P.117~225
09. 2011/05/21:19:<만들면서 배우는 OS 커널의 구조와 원리> P.226-255
10. 2011/05/28:15:<만들면서 배우는 OS 커널의 구조와 원리> P.256-325
11. 2011/06/04:16:*first.S* - use_installed
12. 2011/06/11:12:*first.S* - use_boot - pread - disk_read - disk_convert
13. 2011/06/18:12:*first.s* - sload
14. 2011/06/25:11:*second.s* - comcom, 회식
15. 2011/07/02:11:*second.s* - kt_read
16. 2011/07/09:12:*second.s* - build_vol_tab - is_prev_mapper
17. 2011/07/16:13:*second.s* - build_vol_tab
18. 2011/07/23:10:*lilo.c* - geo_init
19. 2011/07/30:09:*lilo.c*
20. 2011/08/06:10:*second.s* - vir_loop
21. 2011/08/13:09:*second.s* - load1 - loadit - load
22. 2011/08/20:07:*second.s* - start_setup3
23. 2011/09/03:10:*arch/x86/boot/header.S*, *arch/x86/boot/main.c* - copy_boot_params
24. 2011/09/17:10:*arch/x86/boot/main.c* - console_init - parse_earlyprintk
25. 2011/09/24:10:*arch/x86/boot/main.c* - validate_cpu - check_cpu
26. 2011/10/01:08:*arch/x86/boot/main.c* - init_heap
27. 2011/10/08:09:*arch/x86/boot/main.c* - query_edd
28. 2011/10/15:06:*arch/x86/boot/main.c* - set_video - mode_menu
29. 2011/10/22:06:*arch/x86/boot/main.c* - set_video - vga_recalc_vertical
30. 2011/10/29:09:*arch/x86/boot/main.c* - go_to_protected_mode - reset_coprocessor, 회식
31. 2011/11/05:10:*arch/x86/boot/main.c*, *pm.c*, *pmjump.S*, *head_64.S* - verify_cpu
32. 2011/11/12:04:*Makefile*, *arch/x86/boot/compressed/head_64.S* - MSR_EFER
33. 2011/11/19:09:*arch/x86/boot/compressed/head_64.S* - decompress_kernel - parse_elf
34. 2011/11/26:05:*arch/x86/boot/compressed/head_64.S*, *arch/x86/kernel/head_64.S* - startup_64
35. 2011/12/03:06:*arch/x86/kernel/head_64.S* - startup_64
36. 2011/12/10:06:*arch/x86/kernel/head_64.S* - ident_complete
37. 2011/12/17:07:*Makefile*, *vmlinux.lds.S*, *arch/x86/kernel/head_64.S* - secondary_startup_64
38. 2011/12/24:03:*arch/x86/kernel/head_64.S*, *arch/x86/kernel/head64.c*, *init/main.c* - start_kernel, 회식
39. 2012/01/07:05:*init/main.c* - boot_init_stack_canary - get_random_bytes
40. 2012/01/14:05:*init/main.c* - cgroup_init_early
41. 2012/01/28:07:*init/main.c* - init_tick
42. 2012/02/04:08:*init/main.c* - init_tick
43. 2012/02/11:07:*init/main.c* - setup_arch
44. 2012/02/18:06:*init/main.c* - setup_arch - early_cpu_init
45. 2012/02/25:04:*init/main.c* - setup_arch - early_ioremap_init
46. 2012/03/03:05:*init/main.c* - setup_arch - setup_olpc_ofw_pgd
47. 2012/03/10:06:*init/main.c* - setup_arch - setup_memory_map
48. 2012/03/17:06:*init/main.c* - setup_arch - parse_setup_data
49. 2012/03/24:04:*init/main.c* - setup_arch - x86_configure_nx
50. 2012/03/31:05:*init/main.c* - setup_arch - memblock_x86_reserve_range_setup_data
51. 2012/04/07:05:*init/main.c* - setup_arch - memblock_x86_reserve_range_setup_data, 2012 OT
52. 2021/04/14:04:*init/main.c* - setup_arch - acpi_mps_check
53. 2012/04/21:05:*init/main.c* - setup_arch - dmi_scan_machine
54. 2012/04/28:05:*init/main.c* - setup_arch - x86_init.resources.probe_rom
55. 2012/05/05:06:*init/main.c* - setup_arch - x86_init.resources.probe_rom - request_resource
56. 2012/05/12:06:*init/main.c* - setup_arch - insert_resource, 저장소 변경
57. 2012/05/19:07:*init/main.c* - setup_arch - early_gart_iommu_check
58. 2012/05/26:06:*init/main.c* - setup_arch - mtrr_bp_init
59. 2012/06/02:07:*init/main.c* - setup_arch - mtrr_bp_init