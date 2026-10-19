# mywm

A gaming-first, scrolling-tiling Wayland compositor for one machine. Hard fork of
[dwl](https://codeberg.org/dwl/dwl) on wlroots 0.20.

- Spec and roadmap: [DESIGN.md](DESIGN.md)
- Fork base and upstream policy: [UPSTREAM.md](UPSTREAM.md)

The compositor contains no X11 code. X clients run through
[xwayland-satellite](https://github.com/Supreeeme/xwayland-satellite), which mywm
spawns at startup and restarts if it exits. Without it, mywm runs Wayland-only.

## Building

Dependencies: wlroots 0.20, wayland, wayland-protocols, libinput, xkbcommon.
Optional at runtime: xwayland-satellite.

    make            # ./mywm
    make debug      # ./mywm-debug, ASAN + UBSAN

Configuration is compile-time, in `config.h` (tracked; edit it directly).

## Running

Never test on the live session. Nested inside the current compositor:

    WLR_BACKENDS=wayland ./mywm -s alacritty

Headless (no GPU needed, screenshot with `grim`):

    WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=2 ./mywm -s <cmd>

## Session

    make && sudo make install

installs `mywm` and `mywmctl` under `/usr/local`, the session file to
`/usr/share/wayland-sessions/mywm.desktop` (pick "mywm" in the greeter, or run
`mywm` from a TTY) and `mywm-portals.conf`. On real hardware mywm sets
`XDG_CURRENT_DESKTOP=mywm`, imports the session environment into D-Bus
activation and starts Noctalia and gnome-keyring (`autostart` in `config.h`).
`mywm -d` logs to `$XDG_RUNTIME_DIR/mywm.log`.

Screen capture goes through xdg-desktop-portal-wlr (`mywm-portals.conf`), the
backend the mango setup already used; mywm provides the wlr-screencopy,
ext-image-copy-capture and ext-foreign-toplevel protocols it needs for outputs
and windows. A `~/.config/xdg-desktop-portal/mywm-portals.conf` overrides it.

## mywmctl

`mywmctl` talks to the socket in `$MYWM_SOCKET`
(`$XDG_RUNTIME_DIR/mywm.$WAYLAND_DISPLAY.sock`). Each request is one line
holding a JSON array of strings; each reply is one line of JSON, and errors are
`{"error":"..."}` with exit status 1.

    mywmctl get outputs|clients|focused|cursor
    mywmctl output DP-1 off             # on restores the monitor rule's mode and position
    mywmctl set animations 0            # no-op until phase 5
    mywmctl dispatch view 3             # any action in ipc.h, e.g. toggle_scratchpad term
    mywmctl reload                      # outputs back to their rules, toggles to defaults
    mywmctl subscribe                   # current state, then focus/tags/output/open/close events

## License

GPL-3.0, inherited from dwl; see `LICENSE` and the upstream `LICENSE.*` files.
