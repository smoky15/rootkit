# MERGE_MAP — backend `modern_5x_6x` (kernel 5.0 – 6.8)

> **UPDATE STATUS (akhir):** seluruh fitur §1 sudah ada kodenya di ketiga
> backend — kecuali **Param XOR + .ko strip** (ditunda, bukan v1) dan
> **Builder YAML** (tetap v2). Self-hide & backdoor yang tadinya "studi/v2"
> kini **kode lengkap** (`module_hide.c`, `backdoor.c`; legacy pakai sinyal
> `kill -s64/62`). Semua status = 🚧 sampai lolos TESTING.md.

> Peta "file mana dari repo mana" untuk tahap penggabungan.
> Aturan main: **baca → pahami → tulis ulang sendiri** untuk sumber
> tanpa lisensi; copy langsung hanya untuk sumber berlisensi (lihat §4).

---

##1. Fitur → sumber inspirasi

| Fitur | Diambil dari | Status |
|---|---|---|
| Hook engine ftrace/kprobes | `mod-rootkit` (GPLv3 ✅) + pola yang sudah teruji di port-Reptile5.15 | v1 |
| Symbol resolver (`kallsyms_on_each_symbol` via kprobe) | **SUDAH TERBUKTI** — pola yang kita pakai saat port Reptile (engine.c patch) | ✅ siap pakai |
| Hide file **by nama persis** (runtime) | desain baru: getdents hook gaya bds/mod-rootkit + parameter ala Reptile | v1 |
| Hide PID **runtime** | konsep Reptile (`kill -49`) / Diamorphine (`kill -31`) — diadaptasi jadi perintah CLI `rkit hide pid` | v1 |
| Hide koneksi (seq_show tcp/udp) | `mod-rootkit` hook + bds `hook_seq` (baca, jangan copy dari bds) | v1 |
| LPE (`rkit root`) | pola bds `kill000` / Diamorphine signal | v1 |
| **Sembunyikan modul sendiri (self-unlink)** | **bds_lkm_ftrace** — INI SATUNA-SATUNANYA yang terbukti bekerja di5.15 (modul hilang dari lsmod tapi hidup). **Pelajari mekanismenya** di kernelspace-nya | v1 (studi dulu) |
| Backdoor port-knock | bds (opsional v2) — kontrol utama sudah via CLI + daemon telegram | v2 |
| Param XOR + `.ko` strip | `mod-rootkit` (GPLv3 ✅) | v1 |
| Builder YAML | `1337kit` (BSD-3 ✅) | v2 |

---

##2. Struktur target `backend/modern_5x_6x/`

```
backend/modern_5x_6x/
├── kernel/
│   ├── Makefile / Kconfig
│   ├── core.c            ← init/exit + GUARD versi: tolak load di luar5.0–6.8
│   ├── resolver.c        ← kprobe-based symbol resolver   ← MULAI DARI SINI
│   ├── compat/
│   │   └── kver.h        ← #if LINUX_VERSION_CODE: shim per-subversi
│   │                       (5.7 kallsyms,5.8 mmap_lock, TIF, module_mutex,6.x ...)
│   ├── hook_getdents.c   ← hide file/direktori (list nama runtime)
│   ├── hook_proc.c       ← hide PID (list runtime)
│   ├── hook_seq.c        ← hide koneksi tcp/udp
│   ├── hook_kill.c       ← LPE + kontrol via sinyal (atau via device/CLI)
│   ├── module_hide.c     ← self-unlink modul (studi teknik bds dulu!)
│   └── backdoor.c        ← port-knock (v2)
└── PORTING.md            ← catatan WAJIB per versi (lihat §3)
```

---

##3. PORTING.md — daftar jebakan yang SUDAH kita buktikan sesi ini

> Semua ini nyata: dialami saat port Reptile & install bds di5.15.0-198.

| Jebakan | Kernel | Solusi |
|---|---|---|
| `kallsyms_on_each_symbol` / `kallsyms_lookup_name` tidak diekspor | 5.7+ | resolver via `register_kprobe` (sudah teruji ✅) |
| `mmap_sem` tidak ada (→ `mmap_lock`) | 5.8+ | rename di shim `kver.h` |
| `TIF_SYSCALL_AUDIT` tidak didefinisikan (x86) | 5.x Ubuntu | bungkus `#ifdef` |
| `module_mutex` tidak tersedia untuk modul | 5.x | **pelajari cara bds** (terbukti jalan) — jangan asal hapus lock |
| `sys_call_table` tak bisa ditulis (STRICT_KERNEL_RWX) | 4.15+/5.x | jangan pakai teknik era4.x di sini |
| vermagic beda = modul ditolak | semua | headers HARUS `linux-headers-$(uname -r)` |
| Load sukses ≠ aman | semua | **panic saat runtime = kegagalan** (insiden Reptile) → TESTING.md wajib |
| objtool warning `frame pointer` | 5.15 | warning saja (kook-based hook), BUKAN error |

---

##4. Strategi lisensi (WAJIB dibaca sebelum copy-paste kode)

| Sumber | Lisensi | Boleh? |
|---|---|---|
| `flashnuke/mod-rootkit` | **GPLv3** | ✅ copy langsung (dengan atribusi) |
| `lukasbalazik/1337kit` | **BSD-3** | ✅ copy dengan atribusi |
| `Zhang1933/linux-rootkit` | **MIT** | ✅ bebas |
| bds_lkm_ftrace / bds_lkm | **tidak ada LICENSE** | ⚠️ **JANGAN copy verbatim** — baca, pahami, tulis ulang; sebut "inspired by" di README |
| Reptile / nurupo / Diamorphine | **tidak jelas** | ⚠️ idem — konsep saja |

**Keputusan lisensi proyek:** karena fondasi & engine dari **GPLv3 (mod-rootkit)**
→ lisensi repo gabungan: **GPLv3**. Jangan mencampur kode tanpa lisensi ke
dalam GPL tanpa rewrite (risiko takedown hak cipta).

---

##5. Urutan eksekusi (peta kerja kamu)

1. [ ] `resolver.c` + `kver.h` + Makefile — buktikan modul **load & unload bersih** di VM
2. [ ] `hook_getdents.c` — hide file by exact name → uji: `ls` vs akses path
3. [ ] `hook_kill.c` — LPE → uji dari user biasa
4. [ ] `hook_proc.c` — hide PID → uji: `ps` vs `systemctl status` (dokumentasikan inkonsistensinya)
5. [ ] `module_hide.c` — **studi bds dulu**, lalu implementasi → uji lsmod + rmmod
6. [ ] CLI `rkit` (kontrak sudah ada di `control/telegram/SPEC.md` §3)
7. [ ] Isi `KERNEL_COMPAT.md` (5.0–5.6 /5.7–5.19 /6.0–6.8) dari hasil uji nyata
8. [ ] Baru `legacy_4x` (Reptile-era) menyusul

> Setiap step = 1 PR ke repo + isi tabel status (🚧→🧪→✅) sesuai TESTING.md.
