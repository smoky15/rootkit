# HOOK_TECHNIQUES — kenapa teknik rootkit berbeda tiap era kernel

> Satu pertanyaan besar di balik seluruh repo ini:
> **kenapa tidak bisa1 binary rootkit untuk semua kernel?**
> Jawabannya = perang keras antara peretas dan pemelihara kernel selama15 tahun.

## Era A — kernel4.x ke bawah: "tukang kayu"

```
sys_call_table:  [ ... ][ open ][ read ][ getdents64 ][ kill ][ ... ]
                                   ↑
              tulis langsung di sini (CR0.WP diturunkan sebentar)
```

- Papan fungsi syscall ada di memori, dan **masih boleh ditulis** kernel
- `kallsyms_lookup_name` **diekspor** → cari alamat tabel = mudah
- Kode kita: `backend/legacy_4x/kernel/syscall_hook.c`

**Kenapa mati di5.x:** memori kernel di-set **read-only**
(`STRICT_KERNEL_RWX`) — menulis tabel = oops/panic. Trik CR0 pun tidak
lagi bisa diandalkan di banyak konfigurasi.

## Era B — kernel5.0–6.8: "pengintai dari luar"

Karena tidak boleh MENGGANTI fungsi, tekniknya berubah jadi
**MEMANTAU** dari samping:

```
kprobe/kretprobe  →  dipasang di titik fungsi ASLI
                     (getdents64 selesai → kita sunting hasilnya;
                      kill masuk → kita ubah argumennya sebelum jalan)
```

Tapi muncul masalah baru bertahap:

| Kernel | Masalah | Solusi di repo ini |
|---|---|---|
|5.7 | `kallsyms_lookup_name` **tak lagi diekspor** → modul tak bisa cari alamat | daftarkan kprobe sesaat pada nama symbol → dapat alamatnya (`resolver.c`) — **sudah teruji di5.15** |
|5.8 | `mmap_sem` → `mmap_lock` | shim versi |
|5.15 (Ubuntu) | `TIF_SYSCALL_AUDIT` tak ada, `module_mutex` tak tersedia | `#ifdef` guard; self-hide ditunda (pelajari teknik bds) |
| — | objtool warning "frame pointer" saat build | wajar untuk hook berbasis kprobe, BUKAN error |

- Kode kita: `backend/modern_5x_6x/` — **inti proyek ini**

## Era C — kernel6.9+: "era baru (roadmap)"

- API ftrace/kprobes terus bergerak antar minor
- Ekosistem makin condong ke **eBPF** untuk instrumentasi
- Lihat `backend/newest_6x/README.md` — status 🚧, kontribusi dengan bukti uji

## Pelajaran operasional yang dicatat dari pengalaman nyata

1. **Kompilasi sukses ≠ aman.** Sebuah rootkit era lama bisa di-patch sampai
   kompilasi lolos di5.15 tapi **panic saat di-load**. Uji HANYA di VM +
   console, ikuti `TESTING.md`.
2. **Satu bug filter bisa membocorkan semuanya.** Salah hitung offset
   `d_reclen` → buffer readdir korup → `ls` error, operator curiga.
3. **Deteksi ada di ketidakkonsistenan.** `ls` vs akses path, `ps` vs
   `systemctl`, `ss` vs `netstat` — semua inkonsistensi = bendera merah.
   Justru itu bagian yang didokumentasikan di `DETECTION.md`.

## Ringkasan visual

```
2.6 ──── 4.x ────────── 5.x ──────────── 6.8 ──── 6.9+
 tukang     tulis tabel    pantau (kprobe/    ← backend ini   roadmap
 kayu       + CR0.WP       ftrace) + resolver                 (eBPF?)
                ↑                ↑
          legacy_4x         modern_5x_6x     ← dua backend di repo ini
```
