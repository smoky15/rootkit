# Unified Rootkit — satu repo, kernel 2.6 → 6.x

> **EDUKASI / RESEARCH ONLY.** Rootkit ini dibuat untuk belajar
> mekanisme hiding, hooking per-era kernel, dan cara MENGDETEKSINYA.
> Pakai hanya di VM/lab milik sendiri atau dengan izin tertulis.
> Lihat [DISCLAIMER](docs/DISCLAIMER.md).

## Kenapa repo ini?

Setiap rootkit yang ada menutup hanya **satu celah versi kernel**:

| Era | Teknik | Kenapa beda |
|---|---|---|
| 2.6 – 4.x | patch `sys_call_table` | memori kernel masih bisa ditulis |
| 5.0 – 6.8 | **ftrace / kprobes** | `STRICT_KERNEL_RWX`, `kallsyms` tak diekspor (5.7+), API banyak berubah |
| 6.9+ | ftrace / eBPF | API terbaru |

Repo ini menggabungkannya jadi **satu proyek dengan deteksi otomatis**:
`install.sh` membaca versi kernel lalu memilih backend yang sesuai,
build, vermagic-check, load, dan memasang auto-load saat boot.

## Status (jujur)

| Kernel | Backend | Status |
|---|---|---|
| 2.6 – 3.x | `backend/legacy_4x` (`L4X_OLD_ARGS`) | ✅ teruji di **3.10 el7** · 🚧 sisa rentang |
| 4.0 – 4.19 | `backend/legacy_4x` | 🚧 kode ada, belum diuji |
| 5.0 – 6.8 | `backend/modern_5x_6x` | 🧪 fitur penuh lulus di 5.15.0-198 (portal) · tinggal uji reboot · sisa rentang 🚧 |
| 6.9+ | `backend/newest_6x` | 🧪 build+load OK (6.14.0-37) · fitur menyusul |
| 7.x | `backend/newest_6x` | 🚧 **EKSPERIMENTAL** — jalur install dibuka, menunggu uji (risiko panic) |

> Status sengaja dibuat jujur. Tidak ada klaim "works on everything"
> sebelum benar-benar diuji. Setiap kode wajib lolos `docs/TESTING.md`
> sebelum statusnya dinaikkan.

## Fitur

| Fitur | legacy (2.6–4.19) | modern (5.0–6.8) | newest (6.9+) |
|---|:-:|:-:|:-:|
| Sembunyikan file/folder **by nama persis** | ✅ param `hide=` | ✅ `/proc/rkit` + CLI | ✅ |
| Sembunyikan PID | ✅ param `hidepid=` | ✅ | ✅ |
| Sembunyikan **koneksi** (port di `/proc/net/*`) | ✅ param `hideport=` | ✅ `rkit hide port` | ✅ |
| LPE: `kill -s 63 1` | ✅ | ✅ | ✅ |
| **Self-hide modul** | ✅ `kill -s 64` / `kill -s 62` | ✅ `rkit hide/unhide mod` | ✅ |
| **Backdoor port-knock** (opt-in `backdoor_ip`) | ✅ | ✅ | ✅ |
| Auto-load saat boot + restore state | ✅ | ✅ | ✅ |
| Kontrol runtime | sinyal (gaya Diamorphine) | `/proc/rkit` | `/proc/rkit` |
| Daemon Telegram (opsional) | — | ✅ (lihat `control/telegram/SPEC.md`) | ✅ |

> Keterbatasan jujur v1: `/sys/module/` masih terlihat walau modul
> disembunyikan dari `lsmod`; `ss` (netlink) bisa tetap melihat koneksi
> yang disembunyikan dari `netstat`. Detail: `docs/DETECTION.md`.

## Quick start

```sh
# 1. ambil SELURUH repo (install.sh butuh struktur folder lengkap —
#    TIDAK bisa "curl | sh" satu file saja)
git clone <url-repo-anda> unified-rootkit
cd unified-rootkit

# 2. jalankan: deteksi kernel -> build -> vermagic -> load -> (opsional) daemon
sudo sh install.sh

# 3. baca hasilnya: backend terpilih + apakah build/load lolos
```

Prasyarat: **root**, `gcc`, `make`, `linux-headers-$(uname -r)`,
dan **VM/lab** — bukan server produksi.

## Struktur

```
├── install.sh            # deteksi kernel -> build -> vermagic -> load -> autoload+daemon
├── config.example.ini    # template konfigurasi (token diisi sendiri)
├── tools/
│   ├── rkit-load.sh      # loader boot: insmod + restore state hide/backdoor
│   └── rkit-rehide.sh    # auto re-hide PID tiap boot (pola -> pgrep -> hide pid)
├── backend/              # modul kernel per-era (Lengkap)
│   ├── legacy_4x/        #   2.6 -4.19  (sys_call_table + CR0.WP)
│   ├── modern_5x_6x/     #   5.0 -6.8   (kretprobe/kprobe)  <-- inti
│   └── newest_6x/        #   6.9 +      (kretprobe, file tunggal)
├── control/cli/          # binari rkit (hide/unhide file|pid|port|mod, status)
├── control/telegram/     # daemon bot (opsional, token placeholder)
└── docs/
    ├── KERNEL_COMPAT.md  # matriks uji per versi kernel
    ├── HOOK_TECHNIQUES.md# kenapa teknik tiap era berbeda
    ├── DETECTION.md      # cara rootkit ini DIDETEKSI
    ├── TESTING.md        # checklist uji wajib di VM
    ├── QUICKTEST.md      # cheat-sheet uji cepat via CLI (perintah + hasil harapan)
    ├── PORTING.md        # cheatsheet patch antar versi
    └── DISCLAIMER.md     # batasan penggunaan
```

## Kontrol

- **v1 — CLI lokal**: mode `cli` di `config.example.ini`
- **opsional — Telegram**: isi `TELEGRAM_TOKEN` di config milikmu sendiri
  (token TIDAK pernah dicantumkan di repo ini)

## Deteksi

Membangun rootkit tanpa paham deteksi = belajar setengah jalan.
Wajib baca: **[docs/DETECTION.md](docs/DETECTION.md)**.

## Kredit

Proyek ini terinspirasi dari (silakan baca juga lisensinya masing-masing):
- [f0rb1dd3n/Reptile](https://github.com/f0rb1dd3n/Reptile) — era 4.x, loader kmatryoshka
- [flashnuke/mod-rootkit](https://github.com/flashnuke/mod-rootkit) — GPLv3, struktur modern
- [lukasbalazik/1337kit](https://github.com/lukasbalazik/1337kit) — BSD-3, konsep builder
- [bluedragonsecurity/bds_lkm_ftrace](https://github.com/bluedragonsecurity/bds_lkm_ftrace) — backend ftrace 5.x
- [Zhang1933/linux-rootkit](https://github.com/Zhang1933/linux-rootkit) — MIT, kontrol ICMP
