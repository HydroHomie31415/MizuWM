/* fifo-v1: a client may hold a content update back until the previous one
 * has been on screen for a refresh. SDL3 only prefers native Wayland over
 * XWayland when this protocol is advertised (Minecraft 26.x needs that).
 * wlroots 0.20 has no implementation, so this is ours. */
#include <wlr/util/addon.h>
#include "fifo-v1-protocol.h"

/* Forward progress if no output refreshes (hidden window, outputs off). */
#define FIFO_TIMEOUT_MS 40

typedef struct {
	int set_barrier, wait_barrier;
} FifoState;

typedef struct {
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wlr_addon addon;
	struct wlr_surface_synced synced;
	FifoState pending, current;
	int barrier;
	struct wl_array held; /* uint32_t commit seqs, oldest first */
	struct wl_event_source *timer;
	struct wl_listener client_commit;
	struct wl_list link; /* fifos */
} Fifo;

static struct wl_list fifos;

static void fifoadvance(Fifo *f);

static void
fifofree(Fifo *f, int release)
{
	struct wlr_surface *surface = f->surface;
	struct wl_array held = f->held;
	uint32_t *seq;

	wl_event_source_remove(f->timer);
	wl_list_remove(&f->client_commit.link);
	wl_list_remove(&f->link);
	wlr_surface_synced_finish(&f->synced);
	wlr_addon_finish(&f->addon);
	if (f->resource)
		wl_resource_set_user_data(f->resource, NULL);
	free(f);
	/* The surface itself is going away: wlroots frees its cached states. */
	if (release)
		wl_array_for_each(seq, &held)
			wlr_surface_unlock_cached(surface, *seq);
	wl_array_release(&held);
}

static void
fifoaddondestroy(struct wlr_addon *addon)
{
	Fifo *f = wl_container_of(addon, f, addon);
	fifofree(f, 0);
}

static const struct wlr_addon_interface fifo_addon_impl = {
	.name = "wp_fifo_v1",
	.destroy = fifoaddondestroy,
};

static void
fifosyncedmove(void *dst, void *src)
{
	/* both requests apply to a single commit only */
	*(FifoState *)dst = *(FifoState *)src;
	*(FifoState *)src = (FifoState){0};
}

static void
fifosyncedcommit(struct wlr_surface_synced *synced)
{
	Fifo *f = wl_container_of(synced, f, synced);
	if (f->current.set_barrier)
		f->barrier = 1;
}

static const struct wlr_surface_synced_impl fifo_synced_impl = {
	.state_size = sizeof(FifoState),
	.move_state = fifosyncedmove,
	.commit = fifosyncedcommit,
};

static void
fifoclientcommit(struct wl_listener *listener, void *data)
{
	Fifo *f = wl_container_of(listener, f, client_commit);
	struct wlr_subsurface *sub;
	struct wlr_surface_output *so;
	uint32_t *seq;

	if (!f->pending.wait_barrier || (!f->barrier && !f->held.size))
		return;
	if ((sub = wlr_subsurface_try_from_wlr_surface(f->surface)) && sub->synchronized)
		return;
	if (!(seq = wl_array_add(&f->held, sizeof(*seq))))
		return;
	*seq = wlr_surface_lock_pending(f->surface);
	/* An output with nothing new to draw sends no frame events. */
	wl_list_for_each(so, &f->surface->current_outputs, link)
		wlr_output_schedule_frame(so->output);
	wl_event_source_timer_update(f->timer, FIFO_TIMEOUT_MS);
}

static int
fifotimeout(void *data)
{
	fifoadvance(data);
	return 0;
}

static void
fifoadvance(Fifo *f)
{
	uint32_t seq;

	f->barrier = 0;
	if (!f->held.size)
		return;
	seq = *(uint32_t *)f->held.data;
	f->held.size -= sizeof(seq);
	memmove(f->held.data, (uint32_t *)f->held.data + 1, f->held.size);
	wl_event_source_timer_update(f->timer, f->held.size ? FIFO_TIMEOUT_MS : 0);
	/* applies the update, which may set the barrier again */
	wlr_surface_unlock_cached(f->surface, seq);
}

static void
fifoframe(Monitor *m)
{
	/* Called after m latched a frame: barriers on surfaces shown there (or
	 * shown nowhere) clear, and each releases at most one held update. */
	Fifo *f, *tmp;
	struct wlr_surface_output *so;
	int on;

	wl_list_for_each_safe(f, tmp, &fifos, link) {
		on = wl_list_empty(&f->surface->current_outputs);
		wl_list_for_each(so, &f->surface->current_outputs, link)
			on |= so->output == m->wlr_output;
		if (on)
			fifoadvance(f);
	}
}

static Fifo *
fifofromresource(struct wl_resource *resource)
{
	Fifo *f = wl_resource_get_user_data(resource);
	if (!f)
		wl_resource_post_error(resource, WP_FIFO_V1_ERROR_SURFACE_DESTROYED,
				"the wl_surface was destroyed");
	return f;
}

static void
fifosetbarrier(struct wl_client *client, struct wl_resource *resource)
{
	Fifo *f = fifofromresource(resource);
	if (f)
		f->pending.set_barrier = 1;
}

static void
fifowaitbarrier(struct wl_client *client, struct wl_resource *resource)
{
	Fifo *f = fifofromresource(resource);
	if (f)
		f->pending.wait_barrier = 1;
}

static void
fifodestroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
fiforesourcedestroy(struct wl_resource *resource)
{
	Fifo *f = wl_resource_get_user_data(resource);
	if (f) {
		f->resource = NULL;
		fifofree(f, 1);
	}
}

static const struct wp_fifo_v1_interface fifo_impl = {
	.set_barrier = fifosetbarrier,
	.wait_barrier = fifowaitbarrier,
	.destroy = fifodestroy,
};

static void
fifomanagerdestroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
fifomanagergetfifo(struct wl_client *client, struct wl_resource *manager,
		uint32_t id, struct wl_resource *surface_resource)
{
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
	Fifo *f;

	if (wlr_addon_find(&surface->addons, NULL, &fifo_addon_impl)) {
		wl_resource_post_error(manager, WP_FIFO_MANAGER_V1_ERROR_ALREADY_EXISTS,
				"the wl_surface already has a wp_fifo_v1");
		return;
	}
	f = ecalloc(1, sizeof(*f));
	if (!wlr_surface_synced_init(&f->synced, surface, &fifo_synced_impl,
			&f->pending, &f->current)) {
		free(f);
		wl_resource_post_no_memory(manager);
		return;
	}
	if (!(f->resource = wl_resource_create(client, &wp_fifo_v1_interface,
			wl_resource_get_version(manager), id))) {
		wlr_surface_synced_finish(&f->synced);
		free(f);
		wl_resource_post_no_memory(manager);
		return;
	}
	wl_resource_set_implementation(f->resource, &fifo_impl, f, fiforesourcedestroy);
	f->surface = surface;
	wl_array_init(&f->held);
	f->timer = wl_event_loop_add_timer(wl_display_get_event_loop(dpy), fifotimeout, f);
	wlr_addon_init(&f->addon, &surface->addons, NULL, &fifo_addon_impl);
	LISTEN(&surface->events.client_commit, &f->client_commit, fifoclientcommit);
	wl_list_insert(&fifos, &f->link);
}

static const struct wp_fifo_manager_v1_interface fifo_manager_impl = {
	.destroy = fifomanagerdestroy,
	.get_fifo = fifomanagergetfifo,
};

static void
fifomanagerbind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
	struct wl_resource *resource = wl_resource_create(client,
			&wp_fifo_manager_v1_interface, version, id);
	if (!resource) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &fifo_manager_impl, NULL, NULL);
}

static void
fifosetup(void)
{
	wl_list_init(&fifos);
	wl_global_create(dpy, &wp_fifo_manager_v1_interface, 1, NULL, fifomanagerbind);
}
