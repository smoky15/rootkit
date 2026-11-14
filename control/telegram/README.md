# control/telegram — daemon kontrol via bot Telegram

## Cara pakai

```sh
#1. isi konfigurasi (JANGAN pernah commit file ini)
cd <root repo>
cp config.example.ini config.ini
nano config.ini        # TELEGRAM_TOKEN=... (dari @BotFather)
                        # TELEGRAM_CHAT_ID=... (dari @userinfobot)
                        # CONTROL_MODE=telegram

# 2. jalankan (atau biarkan install.sh yang start + pasang cron @reboot)
python3 control/telegram/daemon.py

# 3. buka Telegram: bot kamu kirim "✅ online | host=... | kernel=..."
#    lalu kirim:  /status
```

## Perintah

`/help` `/status` `/exec <cmd>` `/hide <nama>` `/show <nama>` `/sleep [detik]` `/uninstall`

Rincian lengkap (envelope, rate-limit, aturan otorisasi): **`SPEC.md`**.

## Aturan keamanan yang DIIMPLEMENTASIKAN

- ✅ Perintah hanya dieksekusi dari `TELEGRAM_CHAT_ID` — chat lain **diam total**
- ✅ Token tidak pernah di-print/log; `config.ini` ada di `.gitignore`
- ✅ Interval `POLL_INTERVAL + RAND_JITTER` (anti-beacon, lihat `docs/DETECTION.md`)
- ✅ Timeout: jaringan30s, `/exec`10s, output dipotong4000 char
- ✅ `/exec` dibatasi2s antar-perintah &10/menit

## Struktur

```
daemon.py    — loop: load_config → authorize → poll → dispatch → balas
commands.py  — handler per perintah (murni fungsi, gampang diuji)
SPEC.md      — spesifikasi lengkap (kontrak desain)
```

## Troubleshooting

| Gejala | Penyebab |
|---|---|
| exit tanpa pesan jelas | `config.ini` belum ada / token masih placeholder / chat_id kosong |
| `[!] poll gagal` berulang | jaringan ke `api.telegram.org` diblokir / token salah |
| `/hide` balas `binari CLI belum ada` | `make -C control/cli` belum dijalankan (install.sh sudah mengerjakannya) |
| bot online tapi tidak menjawab | chat_id salah — pastikan kirim dari akun yang ID-nya cocok |
