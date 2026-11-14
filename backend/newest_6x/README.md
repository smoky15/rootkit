# backend/newest_6x — kernel 6.9+ (kode sudah ada)

Status: 🚧 **kode `rkit6x.c` tersedia, BELUM diuji** — jangan pakai sebelum
lolos `docs/TESTING.md` di VM dengan kernel6.9+.

## Isi

| File | Fungsi |
|---|---|
| `rkit6x.c` | modul file-tunggal: resolver kprobe + kretprobe getdents64 + kprobe kill (LPE) + `/proc/rkit` |
| `Makefile` | `make` → `rkit6x.ko` |

Teknik **identik** dengan `modern_5x_6x` (dipisah agar guard versi & uji
per-era independen). Fitur v1: hide file by nama, hide PID, `kill -s 63` LPE,
kontrol runtime `/proc/rkit`.

## Kenapa perlu backend terpisah?

- Guard `>=6.9 && <7.0` — rilis6.9+ punya perubahan API yang belum kita
  verifikasi (riwayat: hampir tiap era butuh patch baru, lihat `docs/PORTING.md`)
- Status harus diuji nyata, bukan diasumsikan — baris `KERNEL_COMPAT.md`
  hanya boleh naik 🧪/✅ setelah ada hasil uji

## Roadmap

- [ ] Uji di VM kernel 6.9/6.10/6.11 → isi `KERNEL_COMPAT.md`
- [ ] Evaluasi pendekatan eBPF bila ftrace mulai bergeser
- [ ] Inspirasi referensi: `Trevohack/Venom`

## Kontribusi

PR diterima asal:1) hasil uji nyata,2) tabel status diperbarui,3) disclaimer
tidak dihapus.
