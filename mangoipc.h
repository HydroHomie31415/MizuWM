/*
 * mango-compatible IPC, so shells with a mango backend (Dux Shell) get
 * workspaces, the window list and layouts. Framed like mango's socket: a
 * request is one line of text; "watch all-monitors" and "watch all-clients"
 * stream a full JSON snapshot on every change, anything else gets one reply
 * and the connection is closed. Exported as MANGO_INSTANCE_SIGNATURE.
 */

enum { WatchNone, WatchMonitors, WatchClients };

static int mangofd = -1;
static char mangopath[sizeof(((struct sockaddr_un *)0)->sun_path)];
static struct wl_event_source *mangosource, *mangoidle;
static char *lastmonitors, *lastclients;

static const char *
layoutname(const Layout *l)
{
	return l->arrange == scroller ? "scroller" : l->arrange == tile ? "tile" : l->symbol;
}

static void
mangomonitors(Buf *b)
{
	Monitor *m;
	Client *c;
	uint32_t act;
	const char *sep = "";
	int i, n, urg;

	bputs(b, "{\"monitors\":[");
	wl_list_for_each(m, &mons, link) {
		act = m->tagset[m->seltags];
		bputs(b, sep);
		bputs(b, "{\"name\":");
		bstr(b, m->wlr_output->name);
		bprintf(b, ",\"active\":%s,\"is_vrr\":%s,\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
				"\"scale\":%.2f,\"layout_index\":%d,\"layout_symbol\":",
				JBOOL(m == selmon),
				JBOOL(m->wlr_output->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED),
				m->m.x, m->m.y, m->m.width, m->m.height, m->wlr_output->scale,
				(int)(LAYOUT(m) - layouts));
		bstr(b, m->ltsymbol);
		bprintf(b, ",\"tag_num\":%d,\"tags\":[", TAGCOUNT);
		for (i = 0; i < TAGCOUNT; i++) {
			n = urg = 0;
			wl_list_for_each(c, &clients, link) {
				if (c->mon != m || !(c->tags & 1u << i))
					continue;
				n++;
				urg |= c->isurgent;
			}
			bprintf(b, "%s{\"index\":%d,\"is_active\":%s,\"is_urgent\":%s,\"layout\":",
					i ? "," : "", i + 1, JBOOL(act & 1u << i), JBOOL(urg));
			bstr(b, m->lt[i]->symbol);
			bprintf(b, ",\"client_count\":%d}", n);
		}
		bputs(b, "],\"active_tags\":[");
		for (i = n = 0; i < TAGCOUNT; i++)
			if (act & 1u << i)
				bprintf(b, "%s%d", n++ ? "," : "", i + 1);
		bputs(b, "],\"active_client\":");
		if ((c = focustop(m))) {
			bprintf(b, "{\"id\":%u,\"title\":", c->id);
			bstr(b, client_get_title(c));
			bputs(b, ",\"appid\":");
			bstr(b, client_get_appid(c));
			bputs(b, "}");
		} else {
			bputs(b, "null");
		}
		bputs(b, ",\"keymode\":\"default\",\"keyboardlayout\":\"\"}");
		sep = ",";
	}
	bputs(b, "]}\n");
}

static void
mangoclients(Buf *b)
{
	Client *c;
	const char *sep = "";
	int i, n;
	pid_t pid;

	bputs(b, "{\"clients\":[");
	wl_list_for_each(c, &clients, link) {
		pid = 0;
		wl_client_get_credentials(c->surface.xdg->client->client, &pid, NULL, NULL);
		bprintf(b, "%s{\"id\":%u,\"pid\":%d,\"title\":", sep, c->id, (int)pid);
		bstr(b, client_get_title(c));
		bputs(b, ",\"appid\":");
		bstr(b, client_get_appid(c));
		bputs(b, ",\"monitor\":");
		bstr(b, c->mon ? c->mon->wlr_output->name : NULL);
		bputs(b, ",\"tags\":[");
		for (i = n = 0; i < TAGCOUNT; i++)
			if (c->tags & 1u << i)
				bprintf(b, "%s%d", n++ ? "," : "", i + 1);
		bprintf(b, "],\"is_xwayland\":false,\"is_visible\":%s,\"is_focused\":%s,"
				"\"is_fullscreen\":%s,\"is_floating\":%s,\"is_maximized\":%s,",
				JBOOL(VISIBLEON(c, c->mon)), JBOOL(selmon && c == focustop(selmon)),
				JBOOL(c->isfullscreen), JBOOL(c->isfloating), JBOOL(c->ismaximized));
		bprintf(b, "\"is_overlay\":%s,\"is_minimized\":false,\"is_urgent\":%s,"
				"\"is_scratchpad\":%s,\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}",
				JBOOL(c->isoverlay), JBOOL(c->isurgent), JBOOL(c->scratchpad),
				c->geom.x, c->geom.y, c->geom.width, c->geom.height);
		sep = ",";
	}
	bputs(b, "]}\n");
}

static void
mangowatchers(int watch, char **last, Buf *b)
{
	IpcClient *ic;

	if (*last && !strcmp(*last, b->s))
		return;
	free(*last);
	if (!(*last = strdup(b->s)))
		die("strdup:");
	wl_list_for_each(ic, &ipcclients, link)
		if (ic->mango && ic->watch == watch)
			ipcsend(ic, b->s, b->len);
}

static void
mangostatus(void)
{
	IpcClient *ic;
	Buf mon = {0}, cl = {0};
	int wm = 0, wc = 0;

	wl_list_for_each(ic, &ipcclients, link) {
		wm |= ic->mango && !ic->dead && ic->watch == WatchMonitors;
		wc |= ic->mango && !ic->dead && ic->watch == WatchClients;
	}
	if (wm) {
		mangomonitors(&mon);
		mangowatchers(WatchMonitors, &lastmonitors, &mon);
	}
	if (wc) {
		mangoclients(&cl);
		mangowatchers(WatchClients, &lastclients, &cl);
	}
	free(mon.s);
	free(cl.s);
}

static void
mangoidlecb(void *data)
{
	mangoidle = NULL;
	mangostatus();
}

static void
mangoschedule(void)
{
	/* After the current change is done, e.g. a closing client is unlinked */
	if (!mangoidle && mangofd >= 0)
		mangoidle = wl_event_loop_add_idle(event_loop, mangoidlecb, NULL);
}

static Monitor *
mangomon(const char *name)
{
	Monitor *m;
	wl_list_for_each(m, &mons, link)
		if (!strcmp(m->wlr_output->name, name))
			return m;
	return NULL;
}

static const char *
mangodispatch(char *s)
{
	/* "func,arg,..." in mango's names, then mywm's own action names */
	static const char *const renames[][2] = {
		{ "toggleview", "view" }, { "tag", "send_to_tag" }, { "killclient", "close" },
		{ "toggleoverview", "overview" }, { "viewtoleft", "view_prev" },
		{ "viewtoright", "view_next" },
	};
	char *argv[16], *rest = strchr(s, ','), *next;
	struct wlr_output_state st;
	Monitor *m;
	size_t i;
	int argc = 0, on, d;

	if (rest)
		*rest++ = '\0';
	if (!strcmp(s, "reload_config")) {
		ipcreload();
		return NULL;
	}
	if (!strcmp(s, "switch_keyboard_layout"))
		return NULL;
	if (!strcmp(s, "focusmon") && rest) {
		if ((d = parsedir(rest)) >= 0) {
			focusmon(&(Arg){.i = d});
		} else if ((m = mangomon(rest)) && m->wlr_output->enabled) {
			selmon = m;
			focusclient(focustop(selmon), NULL, 1);
			printstatus();
		} else {
			return "no such monitor";
		}
		return NULL;
	}
	if ((on = !strcmp(s, "wakeup_monitor")) || !strcmp(s, "sleep_monitor")) {
		if (!rest || !(m = mangomon(rest)))
			return "no such monitor";
		m->gamma_lut_changed = 1;
		wlr_output_state_init(&st);
		wlr_output_state_set_enabled(&st, on);
		wlr_output_commit_state(m->wlr_output, &st);
		wlr_output_state_finish(&st);
		m->asleep = !on;
		updatemons(NULL, NULL);
		return NULL;
	}
	if (!strcmp(s, "focusstack") && rest) {
		argv[argc++] = "focus";
		argv[argc++] = !strcmp(rest, "prev") ? "left" : "right";
		return ipcdispatch(argc, argv);
	}
	if (!strcmp(s, "setlayout") && rest) {
		for (i = 0; i < LENGTH(layouts); i++)
			if (!strcmp(rest, layoutname(&layouts[i])) || !strcmp(rest, layouts[i].symbol))
				break;
		if (i == LENGTH(layouts))
			return "no such layout";
		setlayout(&(Arg){.v = &layouts[i]});
		return NULL;
	}
	for (i = 0; i < LENGTH(renames); i++)
		if (!strcmp(s, renames[i][0]))
			s = (char *)renames[i][1];
	argv[argc++] = s;
	if (!strcmp(s, "spawn") || !strcmp(s, "spawn_shell")) {
		argv[0] = "spawn";
		if (rest)
			argv[argc++] = rest; /* the command may contain commas */
	} else {
		for (; rest && argc < (int)LENGTH(argv); rest = next) {
			if ((next = strchr(rest, ',')))
				*next++ = '\0';
			argv[argc++] = rest;
		}
	}
	/* mango's view and toggleview take extra flags after the tag */
	if (!strcmp(s, "view") && argc > 2)
		argc = 2;
	return ipcdispatch(argc, argv);
}

static void
mangorequest(IpcClient *ic, char *line)
{
	Buf b = {0};
	const char *err = NULL;
	size_t i;

	while (*line == ' ')
		line++;
	if (!strcmp(line, "watch all-monitors") || !strcmp(line, "watch all-clients")) {
		/* A watcher stays open and starts with the current state */
		ic->watch = line[10] == 'm' ? WatchMonitors : WatchClients;
		(ic->watch == WatchMonitors ? mangomonitors : mangoclients)(&b);
		ipcsend(ic, b.s, b.len);
		free(b.s);
		return;
	}
	ic->closing = 1;
	if (!strcmp(line, "get all-monitors")) {
		mangomonitors(&b);
	} else if (!strcmp(line, "get all-clients")) {
		mangoclients(&b);
	} else if (!strcmp(line, "get layouts")) {
		bputs(&b, "{\"layouts\":[");
		for (i = 0; i < LENGTH(layouts); i++) {
			bputs(&b, i ? ",{\"symbol\":" : "{\"symbol\":");
			bstr(&b, layouts[i].symbol);
			bputs(&b, ",\"name\":");
			bstr(&b, layoutname(&layouts[i]));
			bputs(&b, "}");
		}
		bputs(&b, "]}\n");
	} else if (!strcmp(line, "get version")) {
		bputs(&b, "{\"version\":\"mywm " VERSION "\"}\n");
	} else if (!strncmp(line, "dispatch ", 9)) {
		if (pick.tree && strncmp(line + 9, "toggleoverview", 14))
			pickclose();
		err = mangodispatch(line + 9);
		bputs(&b, err ? "" : "{\"success\":true}\n");
	} else {
		err = "unknown command";
	}
	if (err) {
		bputs(&b, "{\"error\":");
		bstr(&b, err);
		bputs(&b, "}\n");
	}
	ipcsend(ic, b.s, b.len);
	free(b.s);
}

static void
mangosetup(const char *display)
{
	struct sockaddr_un addr = {.sun_family = AF_UNIX};

	if (snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/mywm-mango.%s.sock",
			getenv("XDG_RUNTIME_DIR"), display) >= (int)sizeof(addr.sun_path))
		return;
	unlink(addr.sun_path);
	if ((mangofd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)) < 0
			|| bind(mangofd, (struct sockaddr *)&addr, sizeof(addr)) < 0
			|| listen(mangofd, 16) < 0) {
		perror("mango IPC socket");
		if (mangofd >= 0)
			close(mangofd);
		mangofd = -1;
		return;
	}
	strcpy(mangopath, addr.sun_path);
	setenv("MANGO_INSTANCE_SIGNATURE", mangopath, 1);
	mangosource = wl_event_loop_add_fd(event_loop, mangofd, WL_EVENT_READABLE, ipcaccept, &mangofd);
}

static void
mangocleanup(void)
{
	if (mangofd < 0)
		return;
	if (mangoidle)
		wl_event_source_remove(mangoidle);
	free(lastmonitors);
	free(lastclients);
	lastmonitors = lastclients = NULL;
	wl_event_source_remove(mangosource);
	close(mangofd);
	unlink(mangopath);
	mangofd = -1;
}
