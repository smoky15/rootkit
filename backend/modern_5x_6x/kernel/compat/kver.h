#ifndef RK_KVER_H
#define RK_KVER_H
/*
 * kver.h — guard era backend modern_5x_6x (kernel5.0 - 6.8)
 *
 * Kenapa dibatasi? Lihat docs/PORTING.md & MERGE_MAP.md:
 *   5.7  : kallsyms_lookup_name / kallsyms_on_each_symbol tak diekspor
 *           -> wajib resolver via kprobe (resolver.c)
 *   5.8  : mmap_sem -> mmap_lock
 *   5.x  : module_mutex tak tersedia bagi modul -> jangan sentuh daftar
 *           modul di v1 (self-hide = tahap lanjutan, pelajari teknik bds)
 *   4.x ke bawah : teknik sys_call_table — beda backend (legacy_4x)
 *   6.9+ : backend newest_6x (roadmap)
 */
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,0,0) || LINUX_VERSION_CODE >= KERNEL_VERSION(6,9,0)
#error "backend modern_5x_6x hanya untuk kernel >=5.0 && <6.9 — pakai backend yang sesuai"
#endif

#endif /* RK_KVER_H */
