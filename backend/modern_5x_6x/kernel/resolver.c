/*
 * resolver.c — cari ALAMAT symbol kernel yang tidak diekspor.
 *
 * Masalah era5.7+: kallsyms_lookup_name / kallsyms_on_each_symbol
 * tidak lagi EXPORT_SYMBOL -> modul yang memanggilnya langsung
 * GAGAL di tahap modpost ("undefined!").
 *
 * Solusi (pola ini SUDAH TERBUKTI pada sesi porting kernel5.15):
 *   1. register_kprobe sementara dengan .symbol_name = "kallsyms_on_each_symbol"
 *      -> kprobe menerjemahkan nama -> alamat diisi di kp.addr
 *      (kprobe boleh mengakses symbol yang TIDAK diekspor)
 *   2. simpan alamatnya, lepas kprobe lagi
 *   3. sekarang kita punya "pintu" iterasi symbol — dipakai rk_lookup()
 */
#include <linux/kprobes.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include "rkit.h"

/* tipe callback iterasi: (data, nama_symbol, modul, alamat) */
typedef int (*rk_cb_t)(void *, const char *, struct module *, unsigned long);
/* tipe fungsi iterasinya sendiri — INGAT:2 ARGUMEN (callback, data) */
typedef int (*rk_iter_t)(rk_cb_t, void *);

static rk_iter_t g_iter;

struct rk_lookup_arg {
	const char    *name;
	unsigned long  addr;
};

static int rk_lookup_cb(void *data, const char *name,
			struct module *mod, unsigned long addr)
{
	struct rk_lookup_arg *a = data;

	(void)mod;
	if (strcmp(a->name, name) == 0) {
		a->addr = addr;
		return 1;
	}
	return 0;
}

int rk_resolver_init(void)
{
	struct kprobe kp = { .symbol_name = "kallsyms_on_each_symbol" };
	int ret;

	ret = register_kprobe(&kp);
	if (ret < 0) {
		pr_err("rkit: resolver: register_kprobe gagal (%d)\n", ret);
		return ret;
	}
	g_iter = (rk_iter_t)kp.addr;
	unregister_kprobe(&kp);

	if (!g_iter) {
		pr_err("rkit: resolver: alamat kallsyms_on_each_symbol kosong\n");
		return -ENOENT;
	}
	pr_info("rkit: resolver siap (kallsyms_on_each_symbol via kprobe)\n");
	return 0;
}

unsigned long rk_lookup(const char *sym)
{
	struct rk_lookup_arg a = { .name = sym, .addr = 0 };

	if (!g_iter)
		return 0;
	g_iter(rk_lookup_cb, &a);
	return a.addr;
}

void rk_resolver_exit(void)
{
	g_iter = NULL;
}
