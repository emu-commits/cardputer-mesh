# Cardputer Mesh — Phase 1 Emulator

A PC-native dev emulator for the Cardputer ADV mesh communicator firmware
(built on [Plai](https://github.com/d4rkmen/plai)). Phase 1 stands up the
**foundation** described in `../docs/ARCHITECTURE.md`:

- **`TextCanvas` + ANSI backend** — the exact seam the real CYD uses. The brain
  renders into a logical cell grid; a diff renderer emits minimal ANSI to a
  `Terminal` sink. On the host that sink is your terminal; on device it's the
  Port-A UART to the CYD.
- **Mooncake-shaped app framework + arena** — one foreground app alive at a time;
  Esc → launcher; `Ctrl-P` command palette overlay that merges the active app's
  **context commands** (e.g. editor Save/Cut/Copy/Paste, calc Copy-result/Clear)
  with app-switches and global actions, type-to-filter.
- **Apps** — Launcher, Mesh chat, Node list (text re-authoring of Plai's mesh
  apps), **Calc** (expression + unit converter with a live preview & history tape),
  **Calendar/Todo** (calcurse-style: Tab toggles todo↔calendar, month-grid picker,
  GTD priorities; due appointments raise notifications), **Editor** (nano-style
  notes; ^S save, ^K cut line, ^U paste via the shared clipboard), **Timer**
  (countdown presets that fire a notification on expiry — even after you switch
  away), **Files** (mc-style browser over the `fs` seam; enter opens, `v` views
  text with hard-wrapping, `i` info), **Contacts** (abook-style favorites +
  aliases over the node DB).
- **`ui_kit`** — the shared widget vocabulary (header/footer chrome, one
  `ListState`+`list()` scrolling-list model, `modal_box` overlays, `input_line`).
  Every app renders through it, enforcing §5.1: one scrolling list per view,
  everything else a modal overlay.
- **`fs::FileSystem` + `clip::Clipboard`** — the storage and copy/paste seams.
  Host backings: `UnixFs` (a sandboxed `emu_sd/` tree, auto-seeded) and an
  in-RAM clipboard; on device = SD/LittleFS and the resident 8 KB buffer.
- **`MeshFacade` + `StubMesh`** — generates live traffic (DMs, @mentions, channel
  chatter) and auto-replies, so everything animates with no hardware. This is the
  swappable seam: next backend = Muzi R1 Neo over `/dev/ttyACM*` (real RF); on
  device = Plai's `MeshService`.
- **NotificationCenter** — resident background service rendering the built-in
  1.14" screen (status strip + iOS-style notifications). Classifies mesh RX
  (DM / @mention) and accepts calendar/timer/reminder events.
- **State persistence** (`persist::Store`) — return-to-last-position: the active
  app + per-app resume tokens are checkpointed on every mode switch and on exit,
  so a relaunch resumes where you left off. Host backing = a flat `emu_state.dat`;
  on device = NVS + SD.

## Layout

```
top  pane  = CYD  (53x20)  primary work surface  (320x240 landscape)
=====================  built-in screen  =====================
bottom pane = built-in 1.14" screen (53x8): status + notifications
```

## Build & run

```sh
make          # builds ./emu  (g++, C++17, no external deps)
make run      # or: ./emu
```

### Two-screen modes

- **Combined (default):** CYD pane (53x20) on top, built-in screen (53x8) below a
  divider, in one terminal window.
- **`--pty` (faithful):** the **CYD renders to its own pseudo-terminal** — the
  "CYD on a serial line" topology. The built-in screen + status stay in the
  launching terminal, which prints the CYD's PTY path. Attach a second terminal:

  ```sh
  ./emu --pty
  # built-in panel shows e.g.  CYD -> /dev/pts/7
  # in another terminal:
  screen /dev/pts/7      # or: cat /dev/pts/7
  ```

  The PTY is put in raw mode (our ANSI stream has no newlines) and a full frame is
  re-sent ~1/s so a terminal attached late (or a CYD that resets) re-syncs.

Use a terminal at least **53x40**. Keys:

| Key | Action |
|---|---|
| up / down | move selection / scroll |
| enter | open app / send chat message |
| esc | back to launcher |
| Ctrl-P | command palette |
| Ctrl-Q | quit |

Watch the bottom pane: a DM banner appears within seconds, a `standup` reminder
around 12 s, and the `unread:` badge increments as events arrive. Open **Mesh
chat** to send a line and get an auto-reply.

## Real mesh — Muzi R1 Neo over USB

The `MeshFacade` seam swaps between the stub and a **real Meshtastic node**. The
host backend (`BridgeMesh`) spawns `src/host/mesh_bridge.py` (canonical
`meshtastic` lib) which talks to the node over serial and relays a tiny
tab-separated protocol over stdio. On device this role is Plai's `MeshService`.

One-time setup (Python venv with the meshtastic lib):

```sh
python3 -m venv .venv
. .venv/bin/activate
pip install meshtastic
```

Run against a connected node:

```sh
./emu --real                 # defaults to /dev/ttyACM0
./emu --real --port /dev/ttyACM1
```

The node DB loads a few seconds after connect (the status bar's `nodes:` count
jumps once the bridge finishes its initial config download). Library chatter and
diagnostics go to `mesh_bridge.log`, never to the protocol stdout.

> Mesh config (region/preset/channel) is left to the node itself — set it per
> your network's guide (e.g. nyme.sh). The emulator does not change radio config.

## What's portable vs host-only

- `src/core/` — portable C++17 (no platform deps). This is shared with the
  eventual firmware: `TextCanvas`, `AnsiRenderer`, the app framework, the
  `MeshFacade` interface, the notification center.
- `src/host/` — POSIX glue (termios raw input, stdout/PTY sinks, monotonic clock).
  Replaced on device by the Plai HAL + UART sink.

## Next

- FZF on-disk index + search (stretch) — the only remaining planned subsystem.

Done: R1-Neo real-mesh backend, `--pty` CYD sink, state persistence, shared
`ui_kit`, the `fs`/clipboard seams, the app suite (Calc, Calendar/Todo, Editor,
Timer, Files, Contacts) with calendar/timer events into the notification center,
and the command palette with per-app context commands + clipboard ops (`^U`
paste / `^K` cut wired through chat, calc, and the editor).
