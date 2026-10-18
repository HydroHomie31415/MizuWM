# Upstream

mywm is a hard fork of [dwl](https://codeberg.org/dwl/dwl). The diff is not kept
rebasable; the `upstream` remote exists only to cherry-pick wlroots API fixes.

    git remote add upstream https://codeberg.org/dwl/dwl.git

## Base

| | |
|---|---|
| Branch | `main` |
| Commit | `e2038450f2b7746d5c2600b0b04bc7717a29c986` ("fix popup positioning", 2026-09-29) |
| wlroots | 0.20 (`wlroots-0.20` pkg-config, 0.20.2 tested) |

dwl `main` targets the latest wlroots release (0.20); `wlroots-next` already
targets 0.21, so it is the wrong base. The base commit built clean with no
warnings against wlroots 0.20.2, so no API fixes were needed at fork time.

## Fork-time changes

- Binary, man page and session file renamed `dwl` -> `mywm`. The source file
  stays `dwl.c` so upstream cherry-picks apply without rename detection.
