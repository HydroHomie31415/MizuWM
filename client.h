/*
 * Attempt to consolidate unavoidable suck into one file, away from dwl.c.  This
 * file is not meant to be pretty.  We use a .h file with static inline
 * functions instead of a separate .c module, or function pointers like sway, so
 * that they will simply compile out if the chosen #defines leave them unused.
 */

static inline void
client_get_size_hints(Client *c, struct wlr_box *max, struct wlr_box *min)
{
	struct wlr_xdg_toplevel_state *state;
	state = &c->surface.xdg->toplevel->current;
	max->width = state->max_width;
	max->height = state->max_height;
	min->width = state->min_width;
	min->height = state->min_height;
}

static inline struct wlr_surface *
client_surface(Client *c)
{
	return c->surface.xdg->surface;
}

static inline int
toplevel_from_wlr_surface(struct wlr_surface *s, Client **pc, LayerSurface **pl)
{
	struct wlr_xdg_surface *xdg_surface, *tmp_xdg_surface;
	struct wlr_surface *root_surface;
	struct wlr_layer_surface_v1 *layer_surface;
	Client *c = NULL;
	LayerSurface *l = NULL;
	int type = -1;

	if (!s)
		return -1;
	root_surface = wlr_surface_get_root_surface(s);
	if ((layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(root_surface))) {
		l = layer_surface->data;
		type = LayerShell;
		goto end;
	}

	xdg_surface = wlr_xdg_surface_try_from_wlr_surface(root_surface);
	while (xdg_surface) {
		tmp_xdg_surface = NULL;
		switch (xdg_surface->role) {
		case WLR_XDG_SURFACE_ROLE_POPUP:
			if (!xdg_surface->popup || !xdg_surface->popup->parent)
				return -1;

			tmp_xdg_surface = wlr_xdg_surface_try_from_wlr_surface(xdg_surface->popup->parent);

			if (!tmp_xdg_surface)
				return toplevel_from_wlr_surface(xdg_surface->popup->parent, pc, pl);

			xdg_surface = tmp_xdg_surface;
			break;
		case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
			c = xdg_surface->data;
			type = c->type;
			goto end;
		case WLR_XDG_SURFACE_ROLE_NONE:
			return -1;
		}
	}

end:
	if (pl)
		*pl = l;
	if (pc)
		*pc = c;
	return type;
}

/* The others */
static inline void
client_activate_surface(struct wlr_surface *s, int activated)
{
	struct wlr_xdg_toplevel *toplevel;
	if ((toplevel = wlr_xdg_toplevel_try_from_wlr_surface(s)))
		wlr_xdg_toplevel_set_activated(toplevel, activated);
}

static inline uint32_t
client_set_bounds(Client *c, int32_t width, int32_t height)
{
	if (wl_resource_get_version(c->surface.xdg->toplevel->resource) >=
			XDG_TOPLEVEL_CONFIGURE_BOUNDS_SINCE_VERSION && width >= 0 && height >= 0
			&& (c->bounds.width != width || c->bounds.height != height)) {
		c->bounds.width = width;
		c->bounds.height = height;
		return wlr_xdg_toplevel_set_bounds(c->surface.xdg->toplevel, width, height);
	}
	return 0;
}

static inline const char *
client_get_appid(Client *c)
{
	return c->surface.xdg->toplevel->app_id ? c->surface.xdg->toplevel->app_id : "";
}

static inline void
client_get_clip(Client *c, struct wlr_box *clip)
{
	*clip = (struct wlr_box){
		.x = c->surface.xdg->geometry.x,
		.y = c->surface.xdg->geometry.y,
		.width = c->geom.width - c->bw,
		.height = c->geom.height - c->bw,
	};
}

static inline void
client_get_geometry(Client *c, struct wlr_box *geom)
{
	*geom = c->surface.xdg->geometry;
	geom->x = geom->y = 0;
}

static inline Client *
client_get_parent(Client *c)
{
	Client *p = NULL;
	if (c->surface.xdg->toplevel->parent)
		toplevel_from_wlr_surface(c->surface.xdg->toplevel->parent->base->surface, &p, NULL);
	return p;
}

static inline int
client_has_children(Client *c)
{
	/* surface.xdg->link is never empty because it always contains at least the
	 * surface itself. */
	return wl_list_length(&c->surface.xdg->link) > 1;
}

static inline const char *
client_get_title(Client *c)
{
	return c->surface.xdg->toplevel->title ? c->surface.xdg->toplevel->title : "";
}

static inline int
client_is_float_type(Client *c)
{
	struct wlr_box min = {0}, max = {0};
	client_get_size_hints(c, &max, &min);
	return c->surface.xdg->toplevel->parent || (min.width > 0 && min.height > 0 &&
			(min.width == max.width || min.height == max.height));
}

static inline int
client_is_rendered_on_mon(Client *c, Monitor *m)
{
	/* This is needed for when you don't want to check formal assignment,
	 * but rather actual displaying of the pixels.
	 * Usually VISIBLEON suffices and is also faster. */
	struct wlr_surface_output *s;
	int unused_lx, unused_ly;
	if (!wlr_scene_node_coords(&c->scene->node, &unused_lx, &unused_ly))
		return 0;
	wl_list_for_each(s, &client_surface(c)->current_outputs, link)
		if (s->output == m->wlr_output)
			return 1;
	return 0;
}

static inline int
client_is_stopped(Client *c)
{
	int pid;
	siginfo_t in = {0};

	wl_client_get_credentials(c->surface.xdg->client->client, &pid, NULL, NULL);
	if (waitid(P_PID, pid, &in, WNOHANG|WCONTINUED|WSTOPPED|WNOWAIT) < 0) {
		/* This process is not our child process, while is very unlikely that
		 * it is stopped, in order to do not skip frames, assume that it is. */
		if (errno == ECHILD)
			return 1;
	} else if (in.si_pid) {
		if (in.si_code == CLD_STOPPED || in.si_code == CLD_TRAPPED)
			return 1;
		if (in.si_code == CLD_CONTINUED)
			return 0;
	}

	return 0;
}

static inline void
client_notify_enter(struct wlr_surface *s, struct wlr_keyboard *kb)
{
	uint32_t filtered[WLR_KEYBOARD_KEYS_CAP];
	size_t size = 0;
	if (!kb) {
		wlr_seat_keyboard_notify_enter(seat, s, NULL, 0, NULL);
		return;
	}
	for (size_t i = 0; i < kb->num_keycodes; i++) {
		uint32_t key = kb->keycodes[i];
		if (!consumed[key])
			filtered[size++] = key;
	}
	wlr_seat_keyboard_notify_enter(seat, s, filtered, size, &kb->modifiers);
}

static inline void
client_send_close(Client *c)
{
	wlr_xdg_toplevel_send_close(c->surface.xdg->toplevel);
}

static inline void
client_set_border_color(Client *c, const float color[static 4])
{
	int i;
	for (i = 0; i < 4; i++)
		wlr_scene_rect_set_color(c->border[i], color);
}

static inline void
client_set_fullscreen(Client *c, int fullscreen)
{
	wlr_xdg_toplevel_set_fullscreen(c->surface.xdg->toplevel, fullscreen);
}

static inline void
client_set_maximized(Client *c, int maximized)
{
	wlr_xdg_toplevel_set_maximized(c->surface.xdg->toplevel, maximized);
}

static inline void
client_set_scale(struct wlr_surface *s, float scale)
{
	wlr_fractional_scale_v1_notify_scale(s, scale);
	wlr_surface_set_preferred_buffer_scale(s, (int32_t)ceilf(scale));
}

static inline void
client_set_size(Client *c, uint32_t width, uint32_t height)
{
	if ((int32_t)width != c->surface.xdg->toplevel->current.width
			|| (int32_t)height != c->surface.xdg->toplevel->current.height)
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, (int32_t)width, (int32_t)height);
}

static inline void
client_set_tiled(Client *c, uint32_t edges)
{
	if (wl_resource_get_version(c->surface.xdg->toplevel->resource)
			>= XDG_TOPLEVEL_STATE_TILED_RIGHT_SINCE_VERSION) {
		wlr_xdg_toplevel_set_tiled(c->surface.xdg->toplevel, edges);
	} else {
		wlr_xdg_toplevel_set_maximized(c->surface.xdg->toplevel, edges != WLR_EDGE_NONE);
	}
}

static inline void
client_set_suspended(Client *c, int suspended)
{
	wlr_xdg_toplevel_set_suspended(c->surface.xdg->toplevel, suspended);
}

static inline int
client_wants_fullscreen(Client *c)
{
	return c->surface.xdg->toplevel->requested.fullscreen;
}
