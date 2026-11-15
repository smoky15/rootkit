# KERNEL_COMPAT — matriks versi kernel vs backend vs status uji

> Status selalu jujur. Ubah hanya setelah melewati `TESTING.md`.
> Legenda: ✅ teruji | 🧪 teruji sebagian (VM) | 🚧 belum | ❌ gagal

## Era1 — legacy (patch memori kernel)

| Kernel | Backend | Status | Catatan |
|---|---|---|---|
| **3.10.0-1160.59.1.el7** | `legacy_4x` (`L4X_OLD_ARGS`) | ✅ | **CentOS 7.9 —2026-11-15: seluruh checklist lolos** (detail: Bukti Uji di bawah) |
| 2.6.x – 3.x (lain) | `legacy_4x` (`L4X_OLD_ARGS`) | 🚧 | kode ada (konvensi argumen lama) — uji di VM lama |
| 4.0 – 4.16 | `legacy_4x` (`L4X_OLD_ARGS`) | 🚧 | signature argumen individual — target Ubuntu16.04 (4.4) |
| 4.17 – 4.19 | `legacy_4x` (pt_regs) | 🚧 | target RHEL8 (4.18); Ubuntu18.04 (4.15) masih konvensi lama |
| catatan | | ❌ | **jangan diuji di >=5.x**: `STRICT_KERNEL_RWX` + risiko panic (pembelajaran dari uji Reptile di 5.15) |

### Bukti Uji — CentOS7 / 3.10.0-1160.59.1.el7.x86_64 (2026-11-15)

```
distro        : CentOS Linux 7.9.2009
build         : OK (gcc el7, tanpa patch tambahan)
load          : OK (vermagic cocok; tanpa OOPS sepanjang uji;
                unload = sys_call_table dipulihkan tiap rmmod)
fitur teruji  : LPE kill -s 63        (OK — uid=0 dari user nobody)
                hide file             (OK — glibc2.17 pakai getdents(78);
                                          kini getdents & getdents64 dua2nya di-hook)
                hide PID              (OK — ps & ls /proc tak melihat;
                                          akses path langsung tetap = batas desain)
                hide port             (OK di netstat; ss/netlink terlihat = batas desain)
                self-hide modul       (OK — sinyal 64/62; catatan: sinyal65 TIDAK
                                          valid di x86 (maks64), diganti62)
                backdoor port-knock   (OK e2e — knock1337 -> shell masuk listener;
                                          TANPA backdoor_ip = nonaktif total)
                state boot            (OK — /var/lib/rkit/state dipulihkan
                                          rkit-load.service)
reboot penuh   : OK (2026-11-15 — modul auto-load @4.6 dtk sejak boot dengan
                daftar hide '1 nama' pulih dari state; daemon via cron;
                repo di /opt/.cache/.rkit karena /dev/shm = tmpfs terhapus)
daemon        : online (POLL_INTERVAL=60 untuk uji; >=600 untuk pemakaian nyata)
patch         : tidak ada
status        : ✅
```

## Era2 — modern (ftrace / kprobes)

| Kernel | Backend | Status | Catatan |
|---|---|---|---|
| 5.0 – 5.4 | `modern_5x_6x` | 🚧 | belum diuji |
| 5.5 – 5.15 | `modern_5x_6x` | 🧪 | **2026-11-15: build+load OK di 5.11.0-1021-aws** (vermagic cocok, `/proc/rkit` aktif) — fitur menyusul; rujukan lama: bds_lkm_ftrace (5.15.0-198) |
| 5.16 – 5.19 | `modern_5x_6x` | 🚧 | belum diuji |
| 6.0 – 6.8 | `modern_5x_6x` | 🚧 | inspirasi: bds_lkm (klaim s/d6.8) |
| PREREQ umum era ini | | | gcc + make + headers cocok; symbol resolver via kprobe (setelah5.7 `kallsyms` tak diekspor) |

## Era3 — terbaru (6.9+)

| Kernel | Backend | Status | Catatan |
|---|---|---|---|
| 6.9 – sekarang | `newest_6x` (`rkit6x.c`) | 🧪 | **2026-11-15: build+load OK di 6.14.0-37-generic** (vermagic cocok, `/proc/rkit` aktif) — fitur menyusul; kode dapat fix ekstraksi getdents (bug identik modern); eBPF tetap opsi |
| **7.x** (contoh: `7.0.14-8-pve`) | `newest_6x` | 🚧 | **EKSPERIMENTAL** — jalur install dibuka (warning keras di install.sh); status menunggu hasil uji pertama — siapkan console sebelum load |

## Kenapa versi di sini penting (ringkas)

- **4.x -> 5.x**: hardening memori kernel (tulis memori read-only dilarang)
- **5.7**: `kallsyms_lookup_name` tidak lagi diekspor ke modul
- **5.8**: `mmap_sem` -> `mmap_lock`
- **5.x-era**: `module_mutex` tidak lagi tersedia untuk modul
- **API audit**: `TIF_SYSCALL_AUDIT` tidak ada di x86 kernel ini
- Setiap distro punya **patch-backport** sendiri — versi vanilla != versi distro
