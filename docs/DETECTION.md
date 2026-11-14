# DETECTION — cara rootkit ini (dan sejenisnya) DIDETEKSI

> Belajar membangun rootkit tanpa paham deteksi = belajar setengah jalan.
> Bagian ini justru yang paling bernilai untuk tim biru / blue team.

## 1. Saat modul dimuat

| Sinyal | Cara cek | Contoh |
|---|---|---|
| Modul tak dikenal dimuat | `auditd` rule syscall | `driver where auditd.data.syscall in ("init_module","finit_module")` |
| Kernel "tainted" | `dmesg \| grep -i taint` | `loading out-of-tree module taints kernel` + `module verification failed` |
| Modul out-of-tree | `cat /proc/modules` vs daftar paket resmi | nama tak dikenal / hilang dari daftar padahal memori berubah |

## 2. Saat berjalan (runtime)

| Sinyal | Cara cek |
|---|---|
| Proses aneh | `ps aux --sort=-rss` — "kworker" tapi VSZ/RSS **tidak 0** (kthread asli selalu 0/0) |
| Path hilang dari `ls` tapi bisa diakses | bandingkan `ls` vs akses path langsung (`stat /path/yang/tak/muncul`) |
| Directory listing tidak konsisten | `ls` vs `find` vs `ls -U` vs baca via programming |
| Port tersembunyi | `ss -tn` (netlink sock_diag) vs `netstat -tulpn` (seq_file) — kalau beda → ada hook |
| Duplicates aneh | `grep -c . /proc/net/tcp` vs `ss -tn \| wc -l` |
| Module hilang tapi `/sys/module/` masih ada | `ls -d /sys/module/*` — hook lsmod kadang tak menutup sysfs |

## 3. File & artefak

- `find / -mtime -2 -type f` (file baru di path sistem)
- `dpkg -V` / `rpm -Va` (binary ter-patch vs paket resmi)
- Unit/cron/rc.local yang tidak dikenal:
  `systemctl list-units --state=running | grep -vE 'systemd|dbus|cron|ssh'`
- `rkhunter --check`, `chkrootkit`, `aide --check` (integrity baseline)

## 4. Kontrol via bot Telegram (IOC khusus proyek ini)

| IOC | Kenapa |
|---|---|
| Koneksi periodik ke `api.telegram.org` | **beacon paling gampang** — log netflow/proxy langsung kelihatan |
| Pola `bot<digits>:AA` di memori | `strings /proc/<pid>/mem` / YARA rule |
| Interval polling beraturan | statistik waktu — koneksi persis tiap N detik = mesin |
| Token di config file | `grep -r "TELEGRAM_TOKEN" /` |

**Mitigasi opsional (yang membuat bot mode tetap "jujur"):**
- `POLL_INTERVAL` besar + `RAND_JITTER` (acak) — lihat `config.example.ini`
- Dokumentasikan pemakaian hanya untuk lab

## 5. Kalau kamu tim biru dan menemukan ini

1. Jangan langsung hapus — **kumpulkan artefak dulu** (unit, cron, modul, log)
2. Cek jejak: kapan file dibuat (`stat`), log siapa yang di-rewrite
3. `ss -tnp` untuk koneksi keluar mencurigakan
4. Reload dari snapshot bersih setelah pengumpulan selesai

##6. Sinyal dari fitur rootkit ini sendiri (v2)

### Hide koneksi (port disembunyikan dari /proc/net/*)
| Sinyal | Cara cek |
|---|---|
| **Inkonsistensi netstat vs ss** | `netstat -tulpn` (baca /proc/net → terfilter) vs `ss -tnlp` (netlink sock_diag → **jalur berbeda, biasanya MASIH terlihat**) — beda hasil = ada hook |
| Port hilang tapi socket tetap | bandingkan `ls -l /proc/<pid>/fd` vs netstat |

### Self-hide modul
| Sinyal | Cara cek |
|---|---|
| `lsmod` kosong tapi fitur jalan | `/sys/module/<nama>` **masih terlihat di v1** → pintu deteksi termudah |
| `dmesg` taint saat load | `dmesg \| grep -i taint` |
| `rmmod <nama>` → "not currently loaded" padahal fitur jalan | daftar modul sudah dicabut → curiga self-hide |

### Backdoor port-knock (TCP SYN ke port aneh)
| Sinyal | Cara cek |
|---|---|
| SYN ke port tinggi tak wajar | `tcpdump 'tcp[tcpflags] & tcp-syn != 0 and (port1337 or port4444)'` |
| Proses UMH sesaat | `ps aux \| grep '/dev/tcp'` — `bash -c 'bash -i >& /dev/tcp/...'` muncul saat knock |
| Koneksi keluar ke IP asing port tinggi | netflow/log koneksi ke `IP:4444` setelah SYN knock |
| execve monitoring | auditd: execve argumen `/dev/tcp` → alarm |
