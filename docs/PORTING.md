# PORTING.md — cheatsheet patch kernel untuk rootkit portabel

> Setiap kali memindahkan kode backend ke kernel lain,50% masalah
> datang dari daftar ini. (Pengalaman nyata sesi pengembangan repo ini —
> patch Reptile→5.15 dan install bds di5.15.0-198.)

##1. Simpul wajib sebelum menulis kode

```sh
uname -r                          # versi target PERSIS
ls /lib/modules/$(uname -r)/build # headers ada? (wajib — kalau tidak: STOP)
gcc --version && make --version   # toolchain ada?
modinfo <modul>.ko | grep vermagic # setelah build: wajib cocok dengan uname -r
```

##2. Peta perubahan API (penyebab umum error kompilasi)

| Sejak | Perubahan | Gejala saat build | Solusi |
|---|---|---|---|
|4.11+ | `STRICT_KERNEL_RWX` jadi standar | tulis `sys_call_table` = oops saat load | pakai backend kprobe (modern) |
|5.7 | `kallsyms_lookup_name` / `kallsyms_on_each_symbol` tak diekspor | `modpost: ... undefined!` | resolver via kprobe (lihat `resolver.c`) |
|5.8 | `mmap_sem` → `mmap_lock` | `no member named 'mmap_sem'` | rename di shim |
|5.x | `module_mutex` tak tersedia bagi modul | `undeclared` | jangan sentuh daftar modul; self-hide = studi khusus (bds) |
|Ubuntu5.15 | `TIF_SYSCALL_AUDIT` tak ada di x86 | `undeclared` | bungkus `#ifdef` |
| — | deklarasi setelah pernyataan | `-Werror=declaration-after-statement` | semua deklarasi di atas blok (aturan C90 kernel) |
| — | warning objtool `frame pointer` | warning | BUKAN error untuk hook kprobe — abaikan |

##3. Aturan yang TIDAK boleh dilanggar

1. **Kompilasi sukses ≠ aman.** Load pertama SELALU dari console VM
   (bukan SSH) dengan `dmesg -w` di layar lain → lihat `TESTING.md`.
2. **Unload harus bersih** — semua patch tabel/hook dikembalikan di
   `module_exit`. Kalau tidak bisa unload (simbol hilang dari daftar),
   jalan keluar satu-satunya = reboot.
3. **Satu backend per rentang** — jangan memaksa teknik era4.x di5.x
   (guard ada di `kver.h` masing-masing backend; jangan dihapus).

##4. Template catatan patch (isi setiap kali porting)

```
target kernel : <uname -r> + <distro>
backend       : modern_5x_6x / legacy_4x
error #1      : <pesan compiler> → patch: <ubah apa>
error #2      : ...
load test     : OK / OOPS / PANIC (setelah N detik)
unload test   : OK / gagal
catatan       : ...
```
