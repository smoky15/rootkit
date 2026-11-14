# KERNEL_COMPAT — matriks versi kernel vs backend vs status uji

> Status selalu jujur. Ubah hanya setelah melewati `TESTING.md`.
> Legenda: ✅ teruji | 🧪 teruji sebagian (VM) | 🚧 belum | ❌ gagal

## Era1 — legacy (patch memori kernel)

| Kernel | Backend | Status | Catatan |
|---|---|---|---|
| 2.6.x – 3.x | `legacy_4x` (`L4X_OLD_ARGS`) | 🚧 | kode ada (konvensi argumen lama) — uji di VM lama |
| 4.0 – 4.16 | `legacy_4x` (`L4X_OLD_ARGS`) | 🚧 | signature argumen individual — target Ubuntu16.04 (4.4) |
| 4.17 – 4.19 | `legacy_4x` (pt_regs) | 🚧 | target RHEL8 (4.18); Ubuntu18.04 (4.15) masih konvensi lama |
| catatan | | ❌ | **jangan diuji di >=5.x**: `STRICT_KERNEL_RWX` + risiko panic (pembelajaran dari uji Reptile di 5.15) |

## Era2 — modern (ftrace / kprobes)

| Kernel | Backend | Status | Catatan |
|---|---|---|---|
| 5.0 – 5.4 | `modern_5x_6x` | 🚧 | belum diuji |
| 5.5 – 5.15 | `modern_5x_6x` | 🚧 | landasan v1; sumber teruji: bds_lkm_ftrace (jalan di 5.15.0-198) |
| 5.16 – 5.19 | `modern_5x_6x` | 🚧 | belum diuji |
| 6.0 – 6.8 | `modern_5x_6x` | 🚧 | inspirasi: bds_lkm (klaim s/d6.8) |
| PREREQ umum era ini | | | gcc + make + headers cocok; symbol resolver via kprobe (setelah5.7 `kallsyms` tak diekspor) |

## Era3 — terbaru (6.9+)

| Kernel | Backend | Status | Catatan |
|---|---|---|---|
| 6.9 – sekarang | `newest_6x` (`rkit6x.c`) | 🚧 | kode ada, belum diuji — teknik identik modern; eBPF tetap opsi |

## Kenapa versi di sini penting (ringkas)

- **4.x -> 5.x**: hardening memori kernel (tulis memori read-only dilarang)
- **5.7**: `kallsyms_lookup_name` tidak lagi diekspor ke modul
- **5.8**: `mmap_sem` -> `mmap_lock`
- **5.x-era**: `module_mutex` tidak lagi tersedia untuk modul
- **API audit**: `TIF_SYSCALL_AUDIT` tidak ada di x86 kernel ini
- Setiap distro punya **patch-backport** sendiri — versi vanilla != versi distro
