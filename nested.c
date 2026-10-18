/*
 * Nested sessions only: ask the parent compositor to pass its own shortcuts
 * through while our window has focus, so Super binds reach mywm.
 * Client-side protocol code, kept apart from the server headers in dwl.c.
 */
#include <string.h>
#include <wayland-client.h>
#include <wlr/backend/wayland.h>
#include <wlr/types/wlr_output.h>
#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"

void nestedinhibit(struct wlr_output *output);

static struct zwp_keyboard_shortcuts_inhibit_manager_v1 *manager;
static struct wl_seat *seat;

static void
global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (!strcmp(interface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name))
		manager = wl_registry_bind(registry, name,
				&zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1);
	else if (!strcmp(interface, wl_seat_interface.name) && !seat)
		seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
}

static void
globalremove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = global,
	.global_remove = globalremove,
};

void
nestedinhibit(struct wlr_output *output)
{
	struct wl_display *remote;
	struct wl_event_queue *queue;
	struct wl_registry *registry;

	if (!wlr_output_is_wl(output))
		return;
	remote = wlr_wl_backend_get_remote_display(output->backend);
	if (!manager) {
		/* A private queue, so this never dispatches the backend's events */
		queue = wl_display_create_queue(remote);
		registry = wl_display_get_registry(remote);
		wl_proxy_set_queue((struct wl_proxy *)registry, queue);
		wl_registry_add_listener(registry, &registry_listener, NULL);
		wl_display_roundtrip_queue(remote, queue);
	}
	if (!manager || !seat)
		return;
	zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(manager,
			wlr_wl_output_get_surface(output), seat);
	wl_display_flush(remote);
}
