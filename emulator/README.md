# Cardputer Mesh — Phase 1 Emulator

A PC-native dev emulator for the Cardputer ADV mesh communicator firmware
(built on [Plai](https://github.com/d4rkmen/plai)). Phase 1 stands up the
**foundation** described in `../docs/ARCHITECTURE.md`:

- **`TextCanvas` + ANSI backend** — the exact seam the real CYD uses. The brain
  renders into a logical cell grid; a diff renderer emits minimal ANSI to a
  `Terminal` sink. On the host that sink is your terminal; on device it's the
  Port-A UART to the CYD.
- **Mooncake-shaped app framework + arena** — one foreground app alive at a time;
  Esc → launcher; `Ctrl-P` command palette overlay.
- **Apps** — Launcher, Mesh chat, Node list (text re-authoring of Plai's mesh apps).
- **`MeshFacade` + `StubMesh`** — generates live traffic (DMs, @mentions, channel
  chatter) and auto-replies, so everything animates with no hardware. This is the
  swappable seam: next backend = Muzi R1 Neo over `/dev/ttyACM*` (real RF); on
  device = Plai's `MeshService`.
- **NotificationCenter** — resident background service rendering the built-in
  1.14" screen (status strip + iOS-style notifications). Classifies mesh RX
  (DM / @mention) and accepts calendar/timer/reminder events.

## Layout

```
top  pane  = CYD  (53x30)  primary work surface
=====================  built-in screen  =====================
bottom pane = built-in 1.14" screen (53x8): status + notifications
```

## Build & run

```sh
make          # builds ./emu  (g++, C++17, no external deps)
make run      # or: ./emu
```

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

## What's portable vs host-only

- `src/core/` — portable C++17 (no platform deps). This is shared with the
  eventual firmware: `TextCanvas`, `AnsiRenderer`, the app framework, the
  `MeshFacade` interface, the notification center.
- `src/host/` — POSIX glue (termios raw input, stdout/PTY sinks, monotonic clock).
  Replaced on device by the Plai HAL + UART sink.

## Next (Phase 1 follow-ups)

- R1-Neo `MeshFacade` backend (Meshtastic serial API over `/dev/ttyACM*`).
- Route the CYD frame to `PtyTerminal` (the faithful "CYD on a serial line" sink).
- State persistence (`session.dat` + resume tokens) per `ARCHITECTURE.md` §6.
