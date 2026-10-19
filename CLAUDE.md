# mywm

Gaming-first scrolling Wayland compositor, hard fork of dwl on wlroots 0.20.
**Read `DESIGN.md` before doing anything.** It is the spec; follow its phase order.

## Rules

- **No X11 code in the compositor.** X apps run through xwayland-satellite (DESIGN.md 4.2).
- **Never block the event loop on a client** (DESIGN.md 4.1). Applies to focus
  changes and IPC especially.
- One logical change per commit. A compositor bug takes down the whole desktop, so
  every change must be easy to revert.
- Never run the build on the live session. Test nested (`WLR_BACKENDS=wayland`) or
  headless (`WLR_BACKENDS=headless WLR_RENDERER=pixman`) — see DESIGN.md 11.1.
- Code style: match dwl (C11, tabs, dwl naming, comments sparse). Keep it small.
- When behaviour is unclear, `~/.config/mango/config.conf` is the reference for how
  things should feel.
- When a phase's acceptance check passes, tick it in the checklist below and commit.

## Preferences

Decided by the user where mywm deliberately differs from mango:

- **A lone window fills the whole monitor** (DESIGN.md 6.2). mango's
  `scroller_ignore_proportion_single=1` actually centres it at its column width;
  don't copy that.
- **Fullscreen acts like borderless windowed** (as on Windows): focus can move
  from a fullscreen window to other windows, and the fullscreen window stays
  fullscreen, visible and live (a game keeps running and shooting still works)
  instead of being unfullscreened or suspended. Not implemented yet: focusdir and
  sloppy focus still stop at a fullscreen window. Fits with phase 4 (6.8) and
  overlays (6.6).

## Progress

- [x] Phase 0: Bootstrap
- [x] Phase 1: No X, satellite supervision
- [x] Phase 2: Scroller layout and keybinds
- [ ] Phase 3: IPC, shell integration, real hardware
- [ ] Phase 4: Gaming
- [ ] Phase 5: Polish
