# DISCLAIMER

This project is provided **solely for legitimate security research,
education, defensive testing, and analysis in controlled environments**.

## Aturan penggunaan

1. Pakai **hanya** di sistem milikmu sendiri atau dengan **izin tertulis**.
2. Jangan dipakai untuk: akses tidak sah, penyembunan aktivitas berbahaya,
   atau apapun yang melanggar hukum.
3. **Jangan pernah** dijalankan di server produksi — uji hanya di VM/lab.
4. Kernel yang tidak tercantum di `KERNEL_COMPAT.md` = **belum diuji**,
   jalankan dengan asumsi bisa **panic/crash**.

## Peringatan teknis

- Modul kernel yang salah bisa: panic, corrupt data, kehilangan akses.
- Selalu sediakan jalan keluar sebelum uji:
  - snapshot/backup VM,
  - akses console (bukan hanya shell jarak jauh),
  - tahu cara unload: `rmmod <nama>` + rencana reboot.
- Rootkit yang terdeteksi = langsung **remediasi** oleh pemilik sistem.

## Tanggung jawab

Penulis/kontributor tidak bertanggung jawab atas penyalahgunaan,
kerusakan, atau konsekuensi hukum dari penggunaan software ini.
