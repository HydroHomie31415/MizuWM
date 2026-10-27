# mywm — Design Document

A gaming-first, scrolling-tiling Wayland compositor for one machine: mine.
Working name `mywm`; rename freely (binary, socket, and IPC tool names follow it).

Status: **design / pre-code**. Last updated 2026-10-18.

---

## 1. Why this exists

On 2026-10-18 a Proton game (Benny's Backrooms, GE-Proton 11-5, Unity/DXVK) froze
permanently when switching focus under mango. Diagnosis:

- All 126 game threads asleep, ~0% CPU, Unity log stopped at the moment of the switch.
- Main thread parked in a Wine wait (`ntsync_schedule`), render threads idle behind it.
- Killing `xalia.exe` did not help. The same game/Proton did not freeze on niri or
  Hyprland, and does not freeze inside gamescope.

Conclusion: the compositor's built-in X window manager (wlroots XWM + mango's focus
logic) hands focus to Wine in a way Wine's X11 driver deadlocks on. niri avoids
this entirely by running X apps through **xwayland-satellite**.

The project's core rule follows directly: **the compositor contains no X11 code.**

## 2. Goals

1. **Games never freeze, stutter, or lose input because of the compositor.**
2. Scrolling-column layout (niri-style) as the primary layout, matching my current
   mango setup (section 6).
3. Full gaming feature set: tearing, on-demand VRR, direct scanout, explicit sync,
   pointer lock, DRM lease.
4. Works with my existing desktop: Noctalia shell, grim/slurp, wl-copy,
   gpu-screen-recorder, the `game-session` and scratchpad scripts.
5. Small and understandable: a dwl-sized codebase I can read end to end.

## 3. Non-goals

- Built-in XWayland / any XWM code. (X apps go through xwayland-satellite.)
- Blur, shadows, rounded corners, fancy effects. My mango config disables all of them.
- Being a general-purpose compositor for other people. No distro packaging,
  no config compatibility with other compositors.
- Touch/tablet polish beyond what wlroots gives for free.
- Animations in the first milestones (see section 9, phase 5).

## 4. Architecture

| Decision | Choice | Reason |
|---|---|---|
| Base | Hard fork of **dwl** (codeberg.org/dwl/dwl) | ~3k lines of C, already wlroots-native, mango started the same way |
| Library | **wlroots 0.20** (`wlroots0.20` from Arch extra, 0.20.2 installed) | Ships nearly every protocol needed (section 7) |
| Language | C (C11), dwl style | Match the base; no rewrite |
| Rendering | `wlr_scene` scene graph | Gives direct scanout and damage tracking for free |
| X11 apps | **xwayland-satellite** (0.8.1 installed), spawned and supervised by the compositor | Section 1 |
| Config | `config.h` (compile-time, dwl style) + runtime toggles over IPC | Simple; runtime needs (animations, monitors) are covered by IPC |
| IPC | Unix socket + `mywmctl` CLI, JSON output | Replaces `mmsg` in my scripts (section 8) |

Fork policy: treat dwl as a **hard fork**. The scroller layout changes the core
data model, so do not try to keep the diff rebasable on upstream. Keep an
`upstream` git remote to cherry-pick wlroots-API fixes.

Before forking, verify which dwl commit builds against wlroots 0.20 and record it in
`UPSTREAM.md`.

### 4.1 The non-blocking rule

The event loop must **never wait on a client**. No synchronous round trips to
clients during focus changes, no waiting for a commit before switching focus, no
waiting on xwayland-satellite. If a client is slow or dead, the compositor moves on.
Any code path that touches focus gets reviewed against this rule.

### 4.2 xwayland-satellite supervision

- At startup, pick a free display number, spawn `xwayland-satellite :N`, and export
  `DISPLAY=:N` to the environment of everything we spawn.
- If it exits, log it and restart it with backoff (1s, 2s, 4s, max 30s).
- If the binary is missing, run without X and log one warning. This allows a
  pure-Wayland session for testing.
- Satellite windows are ordinary xdg-toplevels to us. Window rules match them on
  app_id/title like anything else.

## 5. Hardware and environment (target machine)

- GPU: AMD Radeon RX 9070 XT (Navi 48), `amdgpu`, Mesa.
- Monitors, side by side:
  - `DP-1`: 2560x1440 @ 143.973 Hz, position 0,0, scale 1
  - `DP-3`: 2560x1440 @ 239.992 Hz, position 2560,0, scale 1, **primary / gaming monitor**
- VRR off by default on both; enabled only while a matching game is fullscreen.
- Session env to set before spawning children:
  - `MESA_SHADER_CACHE_MAX_SIZE=10G`
  - `MESA_DISK_CACHE_SINGLE_FILE=true`
  - `XDG_CURRENT_DESKTOP=mywm`, `XDG_SESSION_TYPE=wayland`
- Autostart: `noctalia`, `gnome-keyring-daemon --start --components=secrets`.
  Also `vesktop` and `steam` (opening on DP-1) and `helium` (on DP-3); later
  windows of these apps open on the focused monitor.

## 6. Layout and window management

Behaviour is defined by my current mango config (`~/.config/mango/config.conf`).
That file is the source of truth when this section is ambiguous.

### 6.1 Tags

- 9 tags **per monitor**. Switching tags is per monitor.
- A window lives on exactly one tag (no dwl-style multi-tag bitmasks in the UI;
  the internal representation may stay a bitmask).

### 6.2 Scroller layout (default on every tag)

- Windows are arranged in **columns** on an infinite horizontal strip; the
  viewport scrolls to keep the focused column visible.
- New column width: **0.5** of the monitor width.
- Width presets cycle with Super+F: **1.0, 0.5**. A column at any other width jumps
  to 1.0 first.
- A **single window** on a tag fills the whole monitor, ignoring its proportion.
- No centering: do not center the focused column, do not prefer centering.
- Columns can hold several windows stacked vertically (`scroller_stack left/right`
  moves the focused window into the neighbouring column).
- Gaps 0, borders 0, radius 0.
- When the pointer reaches a screen edge with a partially visible column, focus
  follows it (mango's `edge_scroller_pointer_focus`).

### 6.3 Secondary layout

- `tile` (dwl master/stack). `Super+'` cycles scroller <-> tile on the current tag.

### 6.4 Floating, fullscreen, maximize

- Toggle floating, toggle fullscreen, toggle "maximize to screen".
- Super+left-drag moves, Super+right-drag resizes (floating and tiled).
- Fullscreen windows are drawn above everything except overlay windows and the
  lock screen.

### 6.5 Focus

- Sloppy focus (focus follows mouse), warp cursor to newly focused window.
- `xdg-activation` requests get focus (`focus_on_activate=1`).
- Never steal focus from a fullscreen game because a background window asks for it.
  Mark the requester urgent instead.

### 6.6 Named scratchpads and overlays

- A **named scratchpad** is a floating window bound to an app_id (+ optional title
  regex), toggled shown/hidden by a keybind, centered, 1792x1152.
- **Overlay** windows draw above fullscreen windows (so notes/terminal can sit on
  top of a game without un-fullscreening it).
- Built-in scratchpads (replace the `mango-scratchpad` script workarounds):
  | Key | app_id | Title match | Launch |
  |---|---|---|---|
  | Super+N | `md.obsidian.Obsidian` | – | `obsidian` |
  | Super+\ | `scratch-term` | – | `alacritty --class scratch-term` |
  | Super+C | `com.microsoft.VSCode` | `VS Code Scratchpad$` | `code --user-data-dir ~/.local/share/code-scratchpad --new-window` |
  | Super+S | `spotify` | – | `spotify --enable-features=UseOzonePlatform --ozone-platform=wayland` |
- The VS Code instance sets its title **after** mapping. Rules must be re-evaluated on
  title change for windows not yet claimed by a scratchpad.

### 6.7 Window rules

Matched on app_id and/or title (regex), evaluated at map time and on title/app_id
change until the window has been placed.

| Match | Effect |
|---|---|
| app_id `org.gnome.Calculator`, `gnome-calculator`, `galculator`, `blueman-manager`, `org.gnome.Nautilus`, `xdg-desktop-portal.*`, `zoom` | floating |
| app_id `firefox` or `zen`, title `Picture-in-Picture` | floating |
| app_id `dev.noctalia.Noctalia` | floating, 1080x920 |
| app_id `steam`, title `notificationtoasts_[0-9]+_desktop` | floating, don't focus, offset 100,100 |
| app_id `steam` | ignore fullscreen at map: satellite fullscreens X windows that map at an output's size, and Steam reopens at the size it was tiled to |
| app_id `steam`, `vesktop` | on `DP-1`, only in the first 90 s after login (autostart) |
| app_id `helium` | on `DP-3`, only in the first 90 s after login (autostart) |
| app_id `steam_app_[0-9]+`, `valheim.x86_64`, `cs2`, `gamescope` | **game** (6.8) |
| title `Minecraft\*? [0-9.]+( - .*)?` (mango has `Minecraft\*? .*`, which also catches browser tabs) | **game** + fullscreen |
| game app_ids above **with a non-empty title** | fullscreen; untitled ones ignore fullscreen at map |

The "non-empty title" rule matters: Proton opens an untitled splash window first,
and fullscreening it breaks mouse capture.

### 6.8 Game windows

A window marked **game**:
- opens on `DP-3`;
- VRR is enabled on its output only while it is fullscreen and focused;
- tearing allowed if it asks for it via `wp_tearing_control` (async page flips),
  only while fullscreen (mango's `allow_tearing=2`);
- pointer confined to it while focused;
- no animations, borders, or effects;
- idle inhibited while fullscreen (controllers don't reset the idle timer).

## 7. Protocols

Everything here is provided by wlroots 0.20 unless marked **(ours)**.

### 7.1 Core and gaming

| Protocol | Why |
|---|---|
| xdg-shell, xdg-decoration (force server-side = none) | Windows |
| linux-dmabuf, **linux-drm-syncobj-v1** | Explicit sync; required for smooth Vulkan/Proton on modern Mesa |
| **tearing-control-v1** | Async flips for games |
| presentation-time | Frame pacing for games and video |
| content-type-v1 | Games can tag themselves |
| **drm-lease-v1** | VR headsets |
| pointer-constraints-v1, relative-pointer-v1 | Mouse look in games |
| keyboard-shortcuts-inhibit-v1 | Let games/VMs grab keys (with an escape bind) |
| idle-inhibit-v1, idle-notify (ext-idle-notify-v1) | Screen stays on; Noctalia idle |
| color-management-v1, color-representation-v1 | HDR, later (phase 6) |
| fractional-scale-v1, viewporter, single-pixel-buffer | Standard client needs |
| cursor-shape-v1 | Noctalia, modern toolkits |
| xdg-activation-v1 | Focus requests (6.5) |
| xdg-output-v1, output-management-v1, output-power-management-v1 | Output tools, `dpms-off` |
| text-input-v3, input-method-v2, virtual-keyboard, virtual-pointer | Input tools |
| primary-selection, data-device | Clipboard |
| security-context-v1 | Flatpak |

### 7.2 Needed by Noctalia (checked against `/usr/bin/noctalia` 5.0.0-git)

| Protocol | Noctalia feature |
|---|---|
| wlr-layer-shell-v1 | Bar, panels, launcher |
| **ext-workspace-v1** | Workspace widget (preferred) |
| **dwl-ipc-unstable-v2 (ours)** | Workspace widget fallback; dwl has an existing patch. Implement only if ext-workspace isn't enough |
| ext-foreign-toplevel-list-v1, wlr-foreign-toplevel-management-v1 | Taskbar / window list |
| ext-session-lock-v1 | Lock screen |
| ext-data-control-v1, wlr-data-control-v1 | Clipboard panel |
| wlr-gamma-control-v1 | Night light |
| wlr-screencopy-v1, ext-image-copy-capture-v1 | Screenshots (also grim), recording |

The workspace model exposed through ext-workspace: one workspace group per output,
9 workspaces per group, matching tags.

Screen recording via `gpu-screen-recorder` goes through xdg-desktop-portal. Use
`xdg-desktop-portal-wlr` (or `-gnome`'s screencast backend if wlr misbehaves) and
document the choice in `README.md`.

## 8. IPC: `mywmctl`

Unix socket at `$XDG_RUNTIME_DIR/mywm.$WAYLAND_DISPLAY.sock`. Line-delimited JSON
requests, JSON responses. `mywmctl` is a thin CLI over it. The socket path is also
exported as `MYWM_SOCKET`.

Requests must be answered from compositor state without waiting on clients (4.1).

Minimum command set (derived from what my scripts call on `mmsg` today):

| Command | Used by | Returns / does |
|---|---|---|
| `get outputs` | `game-session` | name, enabled, mode, position, vrr state, focused |
| `output <name> on\|off` | `game-session --single-monitor` | Enable/disable an output, restoring mode and position from config |
| `set animations <0\|1>` | `game-session` | Runtime toggle; off finishes running animations at once |
| `get clients` | scratchpads | app_id, title, tag, output, geometry, floating, fullscreen, visible, scratchpad name |
| `get focused` | `mango-screenshot window` | Focused client geometry |
| `get cursor` | `mango-screenshot output` | Cursor position and output name |
| `dispatch <action> [args]` | everything | Any keybind action by name (section 10) |
| `reload` | – | Re-read runtime-tunable values; full config changes need rebuild + restart |
| `subscribe` | Noctalia/bars later | Event stream: focus, tag, output, client open/close |

After phase 3, port `game-session`, `mango-screenshot`, and the scratchpad script to
`mywmctl` (detect via `XDG_CURRENT_DESKTOP=mywm`, like the existing mango/Hyprland/niri
branches in `game-session`).

## 9. Roadmap

Each phase ends with an acceptance check. Do not start a phase before the previous
one passes.

### Phase 0: Bootstrap
- Clone dwl into this repo with `upstream` remote; record the base commit in `UPSTREAM.md`.
- Build against wlroots 0.20 with `-Wall -Wextra`, plus an ASAN/UBSAN debug target.
- Rename binary to `mywm`.
- **Accept:** builds clean; starts nested inside mango (`WLR_BACKENDS=wayland`) and
  headless (`WLR_BACKENDS=headless WLR_RENDERER=pixman`); a terminal opens in it.

### Phase 1: No X, satellite supervision
- Build with XWayland disabled; remove all XWM code paths.
- Implement 4.2.
- **Accept:** `xeyes`/`xterm` run through satellite nested; killing satellite restarts
  it; missing binary logs a warning and the session continues.

### Phase 2: Scroller layout and keybinds
- Implement 6.1 to 6.5 and 6.7 (rules without the game parts).
- Port every keybind from section 10.
- **Accept:** daily-driveable nested; layout matches my mango setup side by side.

### Phase 3: IPC, shell integration, real hardware
- Implement section 8 and `mywmctl`.
- ext-workspace, layer-shell, session-lock, foreign-toplevel, data-control, gamma,
  screencopy; Noctalia works fully (bar, workspaces, lock, clipboard, screenshots).
- Named scratchpads and overlays (6.6).
- Session entry: `/usr/share/wayland-sessions/mywm.desktop` (or start from TTY).
- **Accept:** a full day on real hardware with Noctalia; ported scripts work.

### Phase 4: Gaming
- Game windows (6.8): on-demand VRR, tearing, pointer confinement, idle inhibit.
- Verify direct scanout of fullscreen games (`WLR_DEBUG`/scene logs show it).
- linux-drm-syncobj, drm-lease, content-type.
- Steps, one commit each:
  - [x] 4.1 Game rules (6.7): `isgame` and `isfullscreen` rule fields, the game
    rows in `config.h`, games open on DP-3, fullscreen once titled (also when the
    title arrives after mapping). *Check:* headless, a `steam_app_1` window with no
    title opens on the game output, not fullscreen; setting a title fullscreens it;
    a `Minecraft 1.21` title is game + fullscreen; `get clients` shows `game`.
  - [x] 4.2 Pointer confined to a focused game. *Check:* headless virtual pointer
    can't leave a focused game window; it can once focus moves.
  - [x] 4.3 Idle inhibited while a game is fullscreen. *Check:* an idle-notify
    client gets no idle event while a fullscreen game is mapped, and does after.
  - [ ] 4.4 tearing-control-v1: async page flips for a fullscreen game that asks.
    *Check:* headless `get outputs` reports tearing on/off as the hint and
    fullscreen state change; real flips checked on hardware.
  - [ ] 4.5 On-demand VRR on the game's output while fullscreen and focused.
    *Check:* `get outputs` vrr follows focus/fullscreen headless (where
    supported); real VRR on DP-3 by hand.
  - [ ] 4.6 Direct scanout: log when a fullscreen game is scanned out
    (`-d`). *Check:* on hardware the log shows scanout for a fullscreen game.
  - [ ] 4.7 drm-lease-v1 on the DRM backend. *Check:* the global is advertised on
    hardware (`wayland-info`), absent nested/headless without crashing.
  - [ ] 4.8 content-type-v1: treat a `game` content type like a game rule.
    *Check:* headless client setting content type game gets `game` in `get clients`.
  - [ ] 4.9 Test matrix (11.2) on real hardware.
- **Accept:** test matrix (section 11) passes, including 50 focus switches in and out
  of a fullscreen Proton game without a freeze.

### Phase 5: Polish
- Short animations (open/close zoom+fade 150 ms, move/tag 250 ms, vertical tag
  slides), always off for game windows and toggleable over IPC.
- Overview (Super+Tab) and Alt+Tab switcher.

### Phase 6: Later / maybe
- HDR on DP-3 via color-management-v1.
- Runtime config file instead of `config.h`.
- dwl-ipc-v2 if Noctalia needs more than ext-workspace gives.

## 10. Keybinds

Port **all** binds from `~/.config/mango/config.conf` (Key bindings section). Same
keys, same commands. Mango action → mywm action mapping:

| mango | mywm action | Notes |
|---|---|---|
| `view,N` / `tag,N` | `view N` / `send_to_tag N` | Per monitor |
| `viewtoleft` / `viewtoright` | `view_prev` / `view_next` | Super+I/U, PgUp/PgDn, Super+Shift+wheel |
| `tagtoleft` / `tagtoright` | `send_to_prev_tag` / `send_to_next_tag` | Follow the window |
| `focusdir` | `focus <dir>` | |
| `exchange_client` | `swap <dir>` | |
| `scroller_stack` | `stack_into <left\|right>` | |
| `switch_proportion_preset` | `cycle_width` | |
| `togglefullscreen` / `togglemaximizescreen` / `togglefloating` | same names | |
| `resizewin,+256,+0` etc. | `resize <dx> <dy>` | |
| `switch_layout` | `cycle_layout` | |
| `killclient` | `close` | |
| `focusmon` / `tagmon` | `focus_output <dir>` / `send_to_output <dir>` | |
| `toggleoverview` / `switcher` | `overview` / `switcher next\|prev` | Phase 5 |
| `quit` | `quit` | Super+Shift+E |
| `spawn,...` | `spawn ...` | Unchanged commands |
| `bindl` | locked binds | Media keys must work while the session is locked |
| `mousebind` | move/resize drags | |
| `axisbind` | wheel binds | |

Scratchpad binds use the built-in scratchpads from 6.6 instead of `mango-scratchpad`.

Also keep `Ctrl+Alt+Backspace` (xkb `terminate:ctrl_alt_bksp`) as an emergency exit.

Input settings: xkb layout `us`, model `pc105+inet`, numlock on at startup;
touchpad tap-to-click and natural scrolling.

## 11. Testing

### 11.1 Development loop (never test on the live session)
- **Nested:** run inside the current compositor: `WLR_BACKENDS=wayland ./mywm`.
- **Headless smoke test** (automatable, used by Claude Code):
  `WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=2 ./mywm -s <script>`,
  drive it with `mywmctl` and check JSON state.
- **Debug build** with ASAN/UBSAN for any crash investigation.
- **Real hardware:** from a TTY, with another TTY logged in as fallback.
  Crashes: `coredumpctl gdb mywm`.
- Logs: `-d` flag for debug logging to `$XDG_RUNTIME_DIR/mywm.log`.

### 11.2 Gaming test matrix (phase 4 and every release after)
| Test | Pass |
|---|---|
| Benny's Backrooms (Proton, X11 path): 50 focus switches to/from another tag/monitor | No freeze |
| Same with `PROTON_ENABLE_WAYLAND=1` | No freeze |
| A native Wayland/SDL game | Fullscreen, VRR on, direct scanout |
| CS2 (native) | Tearing with in-game V-Sync off; pointer lock works |
| Minecraft via Modrinth | Rule matches by title, fullscreen on DP-3 |
| Game inside gamescope | Still works (fallback path) |
| Steam notification toast during a game | Does not steal focus |
| Scratchpad terminal over a fullscreen game | Overlay shows, game keeps fullscreen |
| `game-session --single-monitor` | DP-1 off, then restored with correct mode/position |

## 12. Risks and open questions

- **Scroller complexity.** It is the biggest departure from dwl. Mango's scroller
  (also dwl-derived) is a reference for behaviour, not for code to copy blindly
  (check its licence; dwl is GPL-3.0, so a GPL fork is fine).
- **dwl vs wlroots 0.20.** If dwl main has not caught up with 0.20 API changes, fix
  them in phase 0 and note them in `UPSTREAM.md`.
- **xwayland-satellite and the primary monitor.** Mango used `primary:1` on DP-3 so X11
  games open there. Check how satellite picks the X primary output; game rules
  (6.8) move windows to DP-3 anyway, but some games pick resolution from the X
  primary before mapping.
- **Steam client** may still be X11-only, which makes satellite mandatory in practice.
- **Noctalia** may need compositor-specific IPC for some widgets beyond ext-workspace.
  Check its compositor detection once phase 3 starts.
- **Portal for screencast.** Pick and test early in phase 3; recording scripts depend on it.
