/*
 * mywmctl IPC (DESIGN.md 8). A request is one line holding a JSON array of
 * strings, e.g. ["get","clients"]; the reply is one line of JSON. Replies come
 * from compositor state only, and a client that can't keep up is dropped, so
 * nothing here ever waits on a client (DESIGN.md 4.1).
 */
#include <ctype.h>

#define IPC_MAXOUT (1 << 20) /* bytes queued for a client before it is dropped */
#define JBOOL(x) ((x) ? "true" : "false")

typedef struct {
	char *s;
	size_t len, cap;
} Buf;

typedef struct {
	struct wl_list link;
	int fd, dead, subscribed;
	struct wl_event_source *source;
	char in[4096];
	size_t inlen;
	Buf out;
} IpcClient;

enum { ActNone, ActTag, ActDir, ActDelta, ActCmd, ActScratch }; /* argument types */

typedef struct {
	const char *name;
	void (*func)(const Arg *);
	int type;
	Arg arg; /* for ActNone */
} Action;

/* Keybind actions by name, DESIGN.md 10 */
static const Action actions[] = {
	{ "view",                 view,                  ActTag,     {0} },
	{ "send_to_tag",          tag,                   ActTag,     {0} },
	{ "view_prev",            viewadj,               ActNone,    {.i = -1} },
	{ "view_next",            viewadj,               ActNone,    {.i = +1} },
	{ "send_to_prev_tag",     tagadj,                ActNone,    {.i = -1} },
	{ "send_to_next_tag",     tagadj,                ActNone,    {.i = +1} },
	{ "focus",                focusdir,              ActDir,     {0} },
	{ "swap",                 swapdir,               ActDir,     {0} },
	{ "stack_into",           stackinto,             ActDir,     {0} },
	{ "cycle_width",          cyclewidth,            ActNone,    {0} },
	{ "togglefullscreen",     togglefullscreen,      ActNone,    {0} },
	{ "togglemaximizescreen", togglemaximizescreen,  ActNone,    {0} },
	{ "togglefloating",       togglefloating,        ActNone,    {0} },
	{ "resize",               resizewin,             ActDelta,   {0} },
	{ "cycle_layout",         cyclelayout,           ActNone,    {0} },
	{ "close",                killclient,            ActNone,    {0} },
	{ "focus_output",         focusmon,              ActDir,     {0} },
	{ "send_to_output",       tagmon,                ActDir,     {0} },
	{ "spawn",                spawn,                 ActCmd,     {0} },
	{ "toggle_scratchpad",    togglescratch,         ActScratch, {0} },
	{ "quit",                 quit,                  ActNone,    {0} },
};

static int animations = 1; /* no-op until phase 5 */
static int ipcfd = -1;
static char ipcpath[sizeof(((struct sockaddr_un *)0)->sun_path)];
static struct wl_event_source *ipcsource, *ipcreaper;
static struct wl_list ipcclients;
static char *lastfocus, *lasttags, *lastoutputs; /* last events sent */

static void
bputn(Buf *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		b->cap = MAX(b->cap * 2, b->len + n + 1);
		if (!(b->s = realloc(b->s, b->cap)))
			die("realloc:");
	}
	memcpy(b->s + b->len, s, n);
	b->len += n;
	b->s[b->len] = '\0';
}

static void
bputs(Buf *b, const char *s)
{
	bputn(b, s, strlen(s));
}

static void
bprintf(Buf *b, const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n > 0)
		bputn(b, tmp, MIN((size_t)n, sizeof(tmp) - 1));
}

static void
bstr(Buf *b, const char *s)
{
	/* s as a JSON string, NULL as null */
	if (!s) {
		bputs(b, "null");
		return;
	}
	bputs(b, "\"");
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			bprintf(b, "\\%c", *s);
		else if ((unsigned char)*s < 0x20)
			bprintf(b, "\\u%04x", *s);
		else
			bputn(b, s, 1);
	}
	bputs(b, "\"");
}

static int
tagnum(uint32_t tags)
{
	/* 1-based, 0 for none */
	return tags ? __builtin_ctz(tags) + 1 : 0;
}

static void
bclient(Buf *b, Client *c)
{
	if (!c) {
		bputs(b, "null");
		return;
	}
	bprintf(b, "{\"id\":%u,\"app_id\":", c->id);
	bstr(b, client_get_appid(c));
	bputs(b, ",\"title\":");
	bstr(b, client_get_title(c));
	bputs(b, ",\"output\":");
	bstr(b, c->mon ? c->mon->wlr_output->name : NULL);
	bprintf(b, ",\"tag\":%d,\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
			"\"floating\":%s,\"fullscreen\":%s,\"maximized\":%s,\"urgent\":%s,"
			"\"visible\":%s,\"focused\":%s,\"overlay\":%s,\"game\":%s,\"scratchpad\":",
			tagnum(c->tags), c->geom.x, c->geom.y, c->geom.width, c->geom.height,
			JBOOL(c->isfloating), JBOOL(c->isfullscreen), JBOOL(c->ismaximized),
			JBOOL(c->isurgent), JBOOL(VISIBLEON(c, c->mon)),
			JBOOL(selmon && c == focustop(selmon)), JBOOL(c->isoverlay),
			JBOOL(c->isgame));
	bstr(b, c->scratchpad ? c->scratchpad->name : NULL);
	bputs(b, "}");
}

static void
boutput(Buf *b, Monitor *m)
{
	struct wlr_output *o = m->wlr_output;

	bputs(b, "{\"name\":");
	bstr(b, o->name);
	bprintf(b, ",\"enabled\":%s,\"mode\":{\"width\":%d,\"height\":%d,\"refresh\":%.3f},"
			"\"x\":%d,\"y\":%d,\"scale\":%.2f,\"vrr\":%s,\"focused\":%s}",
			JBOOL(o->enabled), o->width, o->height, o->refresh / 1000.0,
			m->m.x, m->m.y, o->scale,
			JBOOL(o->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED),
			JBOOL(m == selmon));
}

static int
parsereq(char *s, char **argv, int max)
{
	/* A JSON array of strings, unescaped in place; argc or -1 */
	int argc = 0, i;
	unsigned int u;
	char *w;

	while (isspace((unsigned char)*s))
		s++;
	if (*s++ != '[')
		return -1;
	while (isspace((unsigned char)*s))
		s++;
	if (*s == ']')
		return 0;
	for (;;) {
		if (*s++ != '"' || argc == max)
			return -1;
		argv[argc++] = w = s;
		for (; *s != '"'; s++) {
			if (!*s)
				return -1;
			if (*s != '\\') {
				*w++ = *s;
				continue;
			}
			switch (*++s) {
			case 'b': *w++ = '\b'; break;
			case 'f': *w++ = '\f'; break;
			case 'n': *w++ = '\n'; break;
			case 'r': *w++ = '\r'; break;
			case 't': *w++ = '\t'; break;
			case '"': case '\\': case '/': *w++ = *s; break;
			case 'u':
				for (i = 1; i <= 4; i++)
					if (!isxdigit((unsigned char)s[i]))
						return -1;
				sscanf(s + 1, "%4x", &u);
				s += 4;
				if (u < 0x80) {
					*w++ = (char)u;
				} else if (u < 0x800) {
					*w++ = (char)(0xc0 | u >> 6);
					*w++ = (char)(0x80 | (u & 0x3f));
				} else {
					*w++ = (char)(0xe0 | u >> 12);
					*w++ = (char)(0x80 | (u >> 6 & 0x3f));
					*w++ = (char)(0x80 | (u & 0x3f));
				}
				break;
			default:
				return -1;
			}
		}
		s++;
		*w = '\0'; /* at or before the closing quote */
		while (isspace((unsigned char)*s))
			s++;
		if (*s == ']')
			return argc;
		if (*s++ != ',')
			return -1;
		while (isspace((unsigned char)*s))
			s++;
	}
}

static int
parseint(const char *s, int *v)
{
	char *end;
	long l = strtol(s, &end, 10);
	*v = (int)l;
	return *s && !*end && l >= INT_MIN && l <= INT_MAX;
}

static int
parsedir(const char *s)
{
	return !strcmp(s, "left") ? WLR_DIRECTION_LEFT : !strcmp(s, "right") ? WLR_DIRECTION_RIGHT
			: !strcmp(s, "up") ? WLR_DIRECTION_UP : !strcmp(s, "down") ? WLR_DIRECTION_DOWN : -1;
}

static void
ipcreap(void *data)
{
	IpcClient *ic, *tmp;

	ipcreaper = NULL;
	wl_list_for_each_safe(ic, tmp, &ipcclients, link) {
		if (!ic->dead)
			continue;
		wl_list_remove(&ic->link);
		wl_event_source_remove(ic->source);
		close(ic->fd);
		free(ic->out.s);
		free(ic);
	}
}

static void
ipcdrop(IpcClient *ic)
{
	/* Freed from an idle callback, as callers may still hold ic */
	ic->dead = 1;
	if (!ipcreaper)
		ipcreaper = wl_event_loop_add_idle(event_loop, ipcreap, NULL);
}

static void
ipcsend(IpcClient *ic, const char *s, size_t len)
{
	ssize_t n = 0;

	if (ic->dead)
		return;
	if (!ic->out.len && (n = send(ic->fd, s, len, MSG_NOSIGNAL)) < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK) {
			ipcdrop(ic);
			return;
		}
		n = 0;
	}
	if ((size_t)n == len)
		return;
	if (ic->out.len + len - n > IPC_MAXOUT) {
		ipcdrop(ic);
		return;
	}
	bputn(&ic->out, s + n, len - n);
	wl_event_source_fd_update(ic->source, WL_EVENT_READABLE | WL_EVENT_WRITABLE);
}

static void
ipcflush(IpcClient *ic)
{
	ssize_t n = send(ic->fd, ic->out.s, ic->out.len, MSG_NOSIGNAL);

	if (n < 0) {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			ipcdrop(ic);
		return;
	}
	memmove(ic->out.s, ic->out.s + n, ic->out.len - n);
	ic->out.len -= n;
	if (!ic->out.len)
		wl_event_source_fd_update(ic->source, WL_EVENT_READABLE);
}

static void
bevents(Buf *b, Buf *focuskey, Buf *tags, Buf *outputs)
{
	/* The focus, tags and output events for the current state */
	Client *c = selmon ? focustop(selmon) : NULL;
	Monitor *m;
	uint32_t occ, urg;
	const char *sep = "";
	int i;

	bputs(focuskey, "");
	if (c)
		bprintf(focuskey, "%u %s", c->id, client_get_title(c));
	bputs(b, "{\"event\":\"focus\",\"client\":");
	bclient(b, c);
	bputs(b, "}\n");

	bputs(tags, "{\"event\":\"tags\",\"outputs\":[");
	bputs(outputs, "{\"event\":\"output\",\"outputs\":[");
	wl_list_for_each(m, &mons, link) {
		occ = urg = 0;
		wl_list_for_each(c, &clients, link) {
			if (c->mon != m)
				continue;
			occ |= c->tags;
			urg |= c->isurgent ? c->tags : 0;
		}
		bputs(tags, sep);
		bputs(tags, "{\"name\":");
		bstr(tags, m->wlr_output->name);
		bprintf(tags, ",\"tag\":%d,\"occupied\":[", tagnum(m->tagset[m->seltags]));
		for (i = 0; i < TAGCOUNT; i++)
			if (occ & 1u << i)
				bprintf(tags, "%s%d", (occ & ((1u << i) - 1)) ? "," : "", i + 1);
		bputs(tags, "],\"urgent\":[");
		for (i = 0; i < TAGCOUNT; i++)
			if (urg & 1u << i)
				bprintf(tags, "%s%d", (urg & ((1u << i) - 1)) ? "," : "", i + 1);
		bputs(tags, "],\"layout\":");
		bstr(tags, m->ltsymbol);
		bputs(tags, "}");
		bputs(outputs, sep);
		boutput(outputs, m);
		sep = ",";
	}
	bputs(tags, "]}\n");
	bputs(outputs, "]}\n");
}

static void
ipcbroadcast(Buf *b)
{
	IpcClient *ic;
	wl_list_for_each(ic, &ipcclients, link)
		if (ic->subscribed)
			ipcsend(ic, b->s, b->len);
}

static void
ipcchanged(char **last, const char *key, Buf *b)
{
	/* Send event b to subscribers when key differs from last time */
	if (*last && !strcmp(*last, key))
		return;
	free(*last);
	if (!(*last = strdup(key)))
		die("strdup:");
	ipcbroadcast(b);
}

static void
ipcstatus(void)
{
	/* Called on every state change, see printstatus() */
	IpcClient *ic;
	Buf focus = {0}, focuskey = {0}, tags = {0}, outputs = {0};
	int n = 0;

	wl_list_for_each(ic, &ipcclients, link)
		n += ic->subscribed && !ic->dead;
	if (!n)
		return;
	bevents(&focus, &focuskey, &tags, &outputs);
	ipcchanged(&lastfocus, focuskey.s, &focus);
	ipcchanged(&lasttags, tags.s, &tags);
	ipcchanged(&lastoutputs, outputs.s, &outputs);
	free(focus.s);
	free(focuskey.s);
	free(tags.s);
	free(outputs.s);
}

static void
ipcclientevent(const char *event, Client *c)
{
	Buf b = {0};

	bprintf(&b, "{\"event\":\"%s\",\"client\":", event);
	bclient(&b, c);
	bputs(&b, "}\n");
	ipcbroadcast(&b);
	free(b.s);
}

static const char *
ipcoutput(const char *name, const char *state)
{
	/* Turn an output on with its monitor rule, or off */
	Monitor *m, *t = NULL;
	struct wlr_output_state s;
	const MonitorRule *r;
	int on = !strcmp(state, "on"), n = 0;

	if (!on && strcmp(state, "off"))
		return "expected on or off";
	wl_list_for_each(m, &mons, link) {
		n += m->wlr_output->enabled;
		if (!strcmp(m->wlr_output->name, name))
			t = m;
	}
	if (!t)
		return "no such output";
	if (on == t->wlr_output->enabled)
		return NULL;
	if (!on && n == 1)
		return "refusing to disable the last enabled output";

	r = monrule(t->wlr_output);
	wlr_output_state_init(&s);
	if (on)
		monrulestate(t->wlr_output, r, &s);
	else
		wlr_output_state_set_enabled(&s, 0);
	n = wlr_output_commit_state(t->wlr_output, &s);
	wlr_output_state_finish(&s);
	if (!n)
		return "output commit failed";
	t->asleep = 0;
	if (on && (r->x != -1 || r->y != -1))
		wlr_output_layout_add(output_layout, t->wlr_output, r->x, r->y);
	updatemons(NULL, NULL);
	return NULL;
}

static void
ipcreload(void)
{
	/* Runtime toggles back to their defaults, outputs back to their rules */
	Monitor *m;
	const MonitorRule *r;
	struct wlr_output_state s;

	animations = 1;
	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output->enabled)
			continue;
		r = monrule(m->wlr_output);
		wlr_output_state_init(&s);
		monrulestate(m->wlr_output, r, &s);
		wlr_output_commit_state(m->wlr_output, &s);
		wlr_output_state_finish(&s);
		if (r->x != -1 || r->y != -1)
			wlr_output_layout_add(output_layout, m->wlr_output, r->x, r->y);
	}
	updatemons(NULL, NULL);
}

static const char *
ipcdispatch(int argc, char **argv)
{
	const Action *a;
	const char *sh[] = { "/bin/sh", "-c", NULL, NULL };
	Arg arg;
	Buf cmd = {0};
	int d[2], i;

	for (a = actions; a < END(actions) && strcmp(a->name, argv[0]); a++);
	if (a == END(actions))
		return "unknown action";
	arg = a->arg;
	switch (a->type) {
	case ActNone:
		if (argc != 1)
			return "this action takes no arguments";
		break;
	case ActTag:
		if (argc != 2 || !parseint(argv[1], &i) || i < 1 || i > TAGCOUNT)
			return "expected a tag number";
		arg.ui = 1u << (i - 1);
		break;
	case ActDir:
		if (argc != 2 || (arg.i = parsedir(argv[1])) < 0)
			return "expected left, right, up or down";
		break;
	case ActDelta:
		if (argc != 3 || !parseint(argv[1], &d[0]) || !parseint(argv[2], &d[1]))
			return "expected two integers";
		arg.v = d;
		break;
	case ActCmd:
		if (argc < 2)
			return "expected a command";
		for (i = 1; i < argc; i++) {
			bputs(&cmd, argv[i]);
			bputs(&cmd, i + 1 < argc ? " " : "");
		}
		sh[2] = cmd.s;
		arg.v = sh;
		break;
	case ActScratch:
		for (i = 0; argc == 2 && i < (int)LENGTH(scratchpads)
				&& strcmp(scratchpads[i].name, argv[1]); i++);
		if (argc != 2 || i == (int)LENGTH(scratchpads))
			return "expected a scratchpad name";
		arg.v = &scratchpads[i];
		break;
	}
	a->func(&arg);
	free(cmd.s);
	return NULL;
}

static const char *
ipccommand(IpcClient *ic, Buf *b, int argc, char **argv)
{
	Client *c;
	Monitor *m;
	Buf key = {0}, tags = {0}, outputs = {0};
	const char *sep = "";

	if (!strcmp(argv[0], "get") && argc == 2) {
		if (!strcmp(argv[1], "outputs")) {
			bputs(b, "[");
			wl_list_for_each(m, &mons, link) {
				bputs(b, sep);
				boutput(b, m);
				sep = ",";
			}
			bputs(b, "]");
		} else if (!strcmp(argv[1], "clients")) {
			bputs(b, "[");
			wl_list_for_each(c, &clients, link) {
				bputs(b, sep);
				bclient(b, c);
				sep = ",";
			}
			bputs(b, "]");
		} else if (!strcmp(argv[1], "focused")) {
			bclient(b, selmon ? focustop(selmon) : NULL);
		} else if (!strcmp(argv[1], "cursor")) {
			m = xytomon(cursor->x, cursor->y);
			bprintf(b, "{\"x\":%d,\"y\":%d,\"output\":",
					(int)round(cursor->x), (int)round(cursor->y));
			bstr(b, m ? m->wlr_output->name : NULL);
			bputs(b, "}");
		} else {
			return "expected get outputs, clients, focused or cursor";
		}
	} else if (!strcmp(argv[0], "output") && argc == 3) {
		return ipcoutput(argv[1], argv[2]);
	} else if (!strcmp(argv[0], "set") && argc == 3 && !strcmp(argv[1], "animations")) {
		if (strcmp(argv[2], "0") && strcmp(argv[2], "1"))
			return "expected 0 or 1";
		animations = argv[2][0] == '1';
	} else if (!strcmp(argv[0], "dispatch") && argc >= 2) {
		return ipcdispatch(argc - 1, argv + 1);
	} else if (!strcmp(argv[0], "reload") && argc == 1) {
		ipcreload();
	} else if (!strcmp(argv[0], "subscribe") && argc == 1) {
		/* The current state first, then changes */
		ic->subscribed = 1;
		bputs(b, "{\"ok\":true}\n");
		bevents(b, &key, &tags, &outputs);
		bputs(b, tags.s);
		bputn(b, outputs.s, outputs.len - 1); /* ipcrequest() adds the newline */
		free(key.s);
		free(tags.s);
		free(outputs.s);
	} else {
		return "unknown command";
	}
	return NULL;
}

static void
ipcrequest(IpcClient *ic, char *line)
{
	char *argv[64];
	const char *err;
	int argc = parsereq(line, argv, LENGTH(argv));
	Buf b = {0};

	err = argc < 1 ? "expected a non-empty JSON array of strings"
			: ipccommand(ic, &b, argc, argv);
	if (err) {
		b.len = 0;
		bputs(&b, "{\"error\":");
		bstr(&b, err);
		bputs(&b, "}");
	} else if (!b.len) {
		bputs(&b, "{\"ok\":true}");
	}
	bputs(&b, "\n");
	ipcsend(ic, b.s, b.len);
	free(b.s);
}

static int
ipcread(int fd, uint32_t mask, void *data)
{
	IpcClient *ic = data;
	char *line, *nl;
	ssize_t n;

	if (ic->dead)
		return 0;
	if (mask & WL_EVENT_WRITABLE)
		ipcflush(ic);
	if (mask & WL_EVENT_READABLE) {
		if ((n = read(fd, ic->in + ic->inlen, sizeof(ic->in) - 1 - ic->inlen)) <= 0) {
			if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
				ipcdrop(ic);
			return 0;
		}
		ic->inlen += n;
		ic->in[ic->inlen] = '\0';
		for (line = ic->in; !ic->dead && (nl = strchr(line, '\n')); line = nl + 1) {
			*nl = '\0';
			ipcrequest(ic, line);
		}
		ic->inlen -= line - ic->in;
		memmove(ic->in, line, ic->inlen);
		if (ic->inlen == sizeof(ic->in) - 1) {
			ipcrequest(ic, ""); /* reports the error */
			ipcdrop(ic);
		}
	} else if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		ipcdrop(ic);
	}
	return 0;
}

static int
ipcaccept(int fd, uint32_t mask, void *data)
{
	IpcClient *ic;
	int cfd;

	if ((cfd = accept(fd, NULL, NULL)) < 0)
		return 0;
	if (fcntl(cfd, F_SETFD, FD_CLOEXEC) < 0 || fd_set_nonblock(cfd) < 0) {
		close(cfd);
		return 0;
	}
	ic = ecalloc(1, sizeof(*ic));
	ic->fd = cfd;
	ic->source = wl_event_loop_add_fd(event_loop, cfd, WL_EVENT_READABLE, ipcread, ic);
	wl_list_insert(&ipcclients, &ic->link);
	return 0;
}

static void
ipcsetup(const char *display)
{
	struct sockaddr_un addr = {.sun_family = AF_UNIX};

	wl_list_init(&ipcclients);
	if (snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/mywm.%s.sock",
			getenv("XDG_RUNTIME_DIR"), display) >= (int)sizeof(addr.sun_path)) {
		fprintf(stderr, "IPC socket path too long, running without IPC\n");
		return;
	}
	unlink(addr.sun_path);
	if ((ipcfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)) < 0
			|| bind(ipcfd, (struct sockaddr *)&addr, sizeof(addr)) < 0
			|| listen(ipcfd, 16) < 0) {
		perror("IPC socket");
		if (ipcfd >= 0)
			close(ipcfd);
		ipcfd = -1;
		return;
	}
	strcpy(ipcpath, addr.sun_path);
	setenv("MYWM_SOCKET", ipcpath, 1);
	ipcsource = wl_event_loop_add_fd(event_loop, ipcfd, WL_EVENT_READABLE, ipcaccept, NULL);
}

static void
ipccleanup(void)
{
	IpcClient *ic;

	if (ipcfd < 0)
		return;
	if (ipcreaper)
		wl_event_source_remove(ipcreaper);
	wl_list_for_each(ic, &ipcclients, link)
		ic->dead = 1;
	ipcreap(NULL);
	free(lastfocus);
	free(lasttags);
	free(lastoutputs);
	lastfocus = lasttags = lastoutputs = NULL;
	wl_event_source_remove(ipcsource);
	close(ipcfd);
	unlink(ipcpath);
}
