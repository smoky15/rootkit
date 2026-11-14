#ifndef L4X_KVER_H
#define L4X_KVER_H
/*
 * kver.h — guard backend legacy (kernel2.6.x –4.19, x86_64)
 *
 * Teknik: tulis langsung sys_call_table + trik CR0.WP (khas era lama).
 *
 * DUA konvensi signature syscall x86_64 (ini penyebab bug laten #2):
 *   <4.17  : sys_call_table memanggil fungsi dengan ARGUMEN INDIVIDUAL
 *            contoh: sys_getdents64(fd, dirp, count)
 *            -> dipakai kernel2.6 /3.x /4.0-4.16 (Ubuntu16.04 =4.4!)
 *   >=4.17 : wrapper __x64_sys_* menerima const struct pt_regs *
 *            -> dipakai4.17-4.19 (Ubuntu18.04 =4.15 masih INDIVIDUAL,
 *               RHEL8 =4.18 sudah pt_regs)
 */
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) || LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)
#error "backend legacy: kernel 2.6.x - 4.19 saja (di >=5.0 pakai modern_5x_6x)"
#endif

#if !defined(CONFIG_X86_64)
#error "backend legacy hanya untuk x86_64 (trik CR0.WP x86-specific)"
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,17,0)
#define L4X_OLD_ARGS 1     /* argumen individual */
#else
#define L4X_OLD_ARGS 0     /* pt_regs */
#endif

#endif /* L4X_KVER_H */
