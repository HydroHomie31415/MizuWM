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

## License

GPL-3.0, inherited from dwl; see `LICENSE` and the upstream `LICENSE.*` files.
