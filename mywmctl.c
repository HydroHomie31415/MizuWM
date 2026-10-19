/*
 * mywmctl: send one request to mywm and print the JSON reply (DESIGN.md 8).
 * With subscribe, print events until mywm goes away.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const char usage[] =
	"usage: mywmctl <command> [args...]\n"
	"  get outputs|clients|focused|cursor\n"
	"  output <name> on|off\n"
	"  set animations 0|1\n"
	"  dispatch <action> [args...]\n"
	"  reload\n"
	"  subscribe\n";

static void
put(char **buf, size_t *len, size_t *cap, const char *s, size_t n)
{
	if (*len + n + 1 > *cap) {
		*cap = (*len + n + 1) * 2;
		if (!(*buf = realloc(*buf, *cap))) {
			perror("mywmctl: realloc");
			exit(2);
		}
	}
	memcpy(*buf + *len, s, n);
	*len += n;
}

int
main(int argc, char *argv[])
{
	struct sockaddr_un addr = {.sun_family = AF_UNIX};
	const char *path = getenv("MYWM_SOCKET"), *dir, *dpy, *s;
	char *req = NULL, *line = NULL, esc[8];
	size_t len = 0, cap = 0, linecap = 0;
	ssize_t n;
	FILE *f;
	int fd, i, sub, err = 0;

	if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
		fputs(usage, argc < 2 ? stderr : stdout);
		return argc < 2 ? 2 : 0;
	}
	if (path && *path) {
		n = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	} else if ((dir = getenv("XDG_RUNTIME_DIR")) && (dpy = getenv("WAYLAND_DISPLAY"))) {
		n = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/mywm.%s.sock", dir, dpy);
	} else {
		fputs("mywmctl: MYWM_SOCKET is not set\n", stderr);
		return 2;
	}
	if (n >= (ssize_t)sizeof(addr.sun_path)) {
		fputs("mywmctl: socket path too long\n", stderr);
		return 2;
	}

	/* argv as a JSON array of strings */
	put(&req, &len, &cap, "[", 1);
	for (i = 1; i < argc; i++) {
		put(&req, &len, &cap, i > 1 ? ",\"" : "\"", i > 1 ? 2 : 1);
		for (s = argv[i]; *s; s++) {
			if (*s == '"' || *s == '\\')
				n = snprintf(esc, sizeof(esc), "\\%c", *s);
			else if ((unsigned char)*s < 0x20)
				n = snprintf(esc, sizeof(esc), "\\u%04x", *s);
			else
				n = snprintf(esc, sizeof(esc), "%c", *s);
			put(&req, &len, &cap, esc, n);
		}
		put(&req, &len, &cap, "\"", 1);
	}
	put(&req, &len, &cap, "]\n", 2);

	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0
			|| connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "mywmctl: connect %s: ", addr.sun_path);
		perror(NULL);
		return 2;
	}
	for (s = req; len; s += n, len -= n) {
		if ((n = write(fd, s, len)) < 0) {
			perror("mywmctl: write");
			return 2;
		}
	}

	sub = !strcmp(argv[1], "subscribe");
	if (!(f = fdopen(fd, "r"))) {
		perror("mywmctl: fdopen");
		return 2;
	}
	while ((n = getline(&line, &linecap, f)) > 0) {
		err = !strncmp(line, "{\"error\":", 9);
		fputs(line, err ? stderr : stdout);
		fflush(stdout);
		if (!sub || err)
			return err;
	}
	if (!sub) {
		fputs("mywmctl: no reply\n", stderr);
		return 2;
	}
	return 0;
}
