# Cardputer ADV Mesh Communicator — Firmware Architecture

> Single-firmware, offline, mesh-first pocket terminal. Built **on Plai**
> (github.com/d4rkmen/plai, GPL-3.0) — we reuse its proven mesh engine, HAL, and
> Mooncake app framework, and replace its presentation layer with a text-UI driven
> over a second screen (2.8" CYD as a dumb ANSI terminal). The built-in 1.14"
> screen becomes a status strip + iOS-style notification center. No WiFi, no BLE.

Status: design spec **v0.2** (2026-06-23). The contract the PC dev emulator is
built against.

**v0.2 changelog** (after reading Plai source):
- Mooncake app lifecycle confirmed; `onRunningBG()`/`setAllowBgRunning()` adopted
  for background work + notification center.
- **Correction:** the mesh is pumped cooperatively from a single main loop, *not*
  a standalone FreeRTOS task. Reworked §7/§8 accordingly.
- Mesh facade pinned to real API (`sendText`, `setMessageCallback`, `hal->mesh()`).
- HAL display reality: `_canvas` (main) vs `_canvas_system_bar` (top bar) already
  split; dual-screen + alerts ride existing structure.
- New §6.5 **Notification Center** (built-in screen) added.

---

## 1. Goals & non-goals

**Goals**
- One cohesive firmware on the Cardputer ADV running a suite of text-based apps.
- Real Meshtastic-wire-compatible mesh (v2.7+), fully offline.
- 2.8" CYD as the primary work surface; built-in 1.14" screen as an ambient
  status + notification surface.
- "Everything is a list, everything else is an overlay" — one active app at a time.
- All bulk data on SD; RAM holds only the visible window.

**Non-goals (this phase)**
- Image viewer, archive extract — **deferred**.
- WiFi / BLE / MQTT — **out** (LoRa is unaffected; it's SPI + RadioLib).
- shell/ssh, Telegram, direct "claude buddy" — out (need internet). A future
  "claude buddy" would ride over the mesh via an internet gateway node.

---

## 2. Hardware ceiling

| Resource | Value | Implication |
|---|---|---|
| SoC | ESP32-S3FN8 (Stamp-S3A) | dual-core LX7 @240MHz |
| SRAM | **512 KB, NO PSRAM** | the binding constraint |
| Flash | 8 MB | comfortable; single app partition + LittleFS |
| Screens | CYD ILI9341 320×240 (primary) + built-in ST7789 1.14" (status/notify) | |
| Radio | M5Stack SX1262 cap over rear-EXT SPI | RadioLib |
| Link to CYD | Port-A as UART: G1(TX)→CYD GPIO35(RX), ~921600 baud | 3 wires, 3.3V, no shifter, no solder |
| Input | built-in 56-key keyboard | modifiers: ctrl/alt/fn/opt/shift |
| Storage | microSD | all bulk data + indices |

No WiFi/BLE → floor ≈150–250 KB, leaving ~250–350 KB for the active app. The
dumb-terminal model means the brain holds **no main-screen framebuffer** — only a
logical text grid (~6–12 KB); pixels live on the CYD.

---

## 3. The Plai seam — reuse vs. rebuild

Plai: ESP-IDF v5.5 / CMake / C(+C++) / single cooperative main loop. We split it
horizontally.

```
        ┌─────────────────────────────────────────────┐
  OURS  │  apps/ (text UIs) · TextCanvas · ANSI backend │   ← rebuild
        │  command palette · clipboard · NotifyCenter   │
        ├─────────────────────────────────────────────┤
  PLAI  │  Mooncake (components/mooncake) app framework  │   ← reuse
        │  main/mesh/ (mesh_service/router/node_db/data) │   ← reuse as-is
        │  main/settings/ (NVS)                          │
        │  main/hal/ radio(SX1262)·sdcard·keyboard·bat·  │
        │   led·speaker   (+ existing _canvas_system_bar)│
        └─────────────────────────────────────────────┘
          ▲ change hal/display: main content → CYD ANSI;
            built-in ST7789 → status strip + notify center
```

**Reuse unchanged (the hard 60%):**
- `main/mesh/` — `mesh_service.*` (API: `sendText`, `setMessageCallback`,
  `getNode`, `getState`…), `packet_router.*` (priority TX/RX, duty cycle, CSMA/CA),
  `node_db.*` (SD-backed, 1000 nodes), `mesh_data.*` (file-backed history).
  Pumped via `MeshService::update()` from the main loop. AES-CTR + X25519,
  hop-limit flooding, Nanopb protobufs.
- `components/mooncake/` — app framework (see §5).
- `main/hal/` radio (SX1262), sdcard, keyboard, battery, led, speaker; the
  `_canvas` / `_canvas_system_bar` sprite split; `playNotificationSound()`.
- `main/settings/` — NVS cache + export/import. `hal->mesh()`/`hal->nodedb()`
  are globally reachable; apps fetch `HAL`/`SETTINGS` from Mooncake's SimpleKV.

**Rebuild (the upper half):**
- Presentation: Plai draws pixels (LovyanGFX) to ST7789. We add a `TextCanvas`
  + **ANSI-over-UART backend** for the CYD, and repurpose the built-in screen.
- App layer: Plai's `app_nodes/app_channels/app_monitor/app_stats/app_settings`
  become **logic references**; we re-author views as text apps + add new apps.

**HAL change (least-invasive):** add a `HAL_USE_CYD` text-output surface (UART +
ANSI backend) as the main render target; keep `_canvas_system_bar` + the (now
freed) built-in panel for the status strip + notification center. If the CYD is
absent, the built-in screen falls back to a minimal local UI.

---

## 4. Presentation layer — CYD as a dumb ANSI terminal

CYD runs its own VT100/ANSI firmware (FabGL-class); it only *receives* bytes.

**TextCanvas** (the API every app renders into):
- Logical cell grid. CYD is **320×240 (4:3 landscape)**. With a 6×12 font that is
  **53 cols × 20 rows** (chosen: 12px is legible on a 2.8" panel, and cols/rows≈8/3
  also reads landscape in a host terminal, where cells are ~2× taller than wide).
  A denser 6×8 font would give 53×30 but renders cramped on hardware and looks
  portrait in the emulator. Orientation is set device-side via the CYD's display
  rotation (240×320 ST7789 scanned as 320×240); the brain only streams ANSI cells.
- Cell = `{ codepoint, fg, bg, attr(bold/inverse/underline) }`.
- `canvas.put/line/list/overlay(...)`. Apps never emit ANSI directly.
- Cost: grid ≈ 6 KB; double-buffer for diffing ≈ 12 KB.

**ANSI backend:** diffs current vs. previous grid → minimal ANSI (cursor + SGR +
text) so UART stays light at 921600. Flushed from the main loop (optionally a
tiny dedicated writer task). **Dev mode:** same backend writes to a PTY → a real
terminal window is a 1:1 CYD stand-in (§11).

---

## 5. App model — our UX on Mooncake (verified)

`APP_PACKER_BASE` = factory (`getAppName`, `newApp`, `deleteApp`); `APP_BASE` =
lifecycle FSM: `onCreate → onResume → onRunning ⇄ onRunningBG → onPause →
onDestroy`, with `startApp()/closeApp()/destroyApp()` and `setAllowBgRunning()`.
Registered via `mooncake.installApp(new XPacker)`; userdata (framework + SimpleKV
DB holding `HAL`/`SETTINGS`) is injected.

Mapping:
- **An "Emacs mode/buffer" == a Mooncake app.** Mode switch = arena swaps the
  foreground app (`onRunning`).
- **Arena:** exactly **one foreground app** alive on the CYD at a time. On switch,
  the outgoing app persists state (§6) and is closed/destroyed to free its working
  set. Resident-but-backgrounded apps (notification center) keep running via
  `onRunningBG()` with `setAllowBgRunning(true)`.
- **App contract (extends APP_BASE):** allocate working set in `onCreate`/restore;
  read `hal->keyboard()->keysState()` in `onRunning`; render into `TextCanvas`;
  persist + free in `onPause`/`onDestroy`. Declared `ram_budget()` asserted in debug.
- **Command palette** — global overlay (ctrl-chord) merging active-app + global
  commands; transient.
- **Clipboard** — one bounded (8 KB) resident text buffer; copy/paste across apps.
- **Launcher** — root list of modes (Plai `apps/launcher/` reworked to text).

### 5.1 Rendering contract (hard rule)
Every primary view is **one scrolling list**. Everything else (info, month-grid
picker, wizard step, palette, confirm) is a **modal overlay**. No split panes,
columns, sidebars, or preview panes.

---

## 6. State persistence — return-to-last-position

Reuses Plai's existing backends (both already persist across reboot/firmware
update): **NVS** (`SETTINGS::Settings` — namespaced get/set + `saveAll`) for small
scalars; **SD files** (as Plai does for node_db / message history) for larger state.

**Model — `session.dat` + per-app resume tokens:**
- `session.dat` records the active app id (+ breadcrumb).
- Each app owns a small **resume token**, e.g. chat `{conv_id, scroll}`, editor
  `{file_path, cursor, scroll}`, file browser `{cwd, sel_index}`, calcurse
  `{mode, sel_date}`. Scalars → NVS (fast, works without SD); larger context → SD.
- **Boot:** read `session` → recreate that Mooncake app → `onCreate()` restores
  from its token (pages its window from SD). Tokens are version-tagged + validated;
  fall back to launcher if stale (e.g. referenced file gone).

**Checkpoint discipline (the one real constraint):** no guaranteed flush on an
abrupt battery cut, and NVS is flash-wear-sensitive — so **never** save per
keystroke. Save at safe points: `onPause`/`onDestroy` (every mode switch — frequent
and natural), explicit actions, and a debounced periodic flush (~30–60 s) for the
foreground app. Restore = last *checkpoint*, which is what "last position" needs.
A clean UI power-down triggers a final flush; only a hard cut falls back.

**Editor exception:** to recover unsaved *content* (not just cursor), mirror the
dirty buffer to an SD scratch/swap file periodically (vim-swap style). Negligible
RAM cost; the breadcrumb is tiny and each app restores its window from SD.

## 6.5 Notification Center (built-in 1.14" screen)

The built-in screen is an **ambient surface**: a persistent status strip plus an
iOS-style notification center fed by background events. Independent of the CYD.

**Implementation — a resident background app `NotificationCenter`:**
- Installed at boot, `setAllowBgRunning(true)`, created+started but kept
  **backgrounded** (foreground = whatever app owns the CYD). Its `onRunningBG()`
  runs every main-loop tick.
- At boot it registers `hal->mesh()->setMessageCallback(...)`. The callback (fires
  in main-loop context — safe) **classifies each RX packet**:
  - destination == our `node_id` → **DM**;
  - text on a channel containing our short/long name → **name callout / mention**;
  - (extensible: ack/routing, new-node greetings).
  It pushes a `Notification{type, from, channel, preview, ts}` into a small RAM
  ring (≈16) and appends to `logs/notifications`, then fires
  `hal->playNotificationSound(channelSound)` + LED blink.
- `onRunningBG()` also polls non-mesh sources: **calendar/reminder due**, **timer/
  stopwatch expiry** (queried from the calcurse/timer modules' SD state + RTC).

**Rendering (built-in screen):**
- **Status strip** (reuse `_canvas_system_bar`): clock, battery, mesh state
  (nodes / last-rx), unread badge.
- **Banner:** on a new event, briefly show a one-line banner (auto-dismiss ~Ns).
- **Center overlay:** a dedicated chord opens a scrollable list of recent
  notifications on the built-in screen; selecting one **deep-links** the CYD's
  foreground to the relevant app (e.g. open mesh chat to that DM / channel,
  jump to the calendar entry).

Event sources (this phase): Meshtastic DMs, mesh chat name callouts/mentions,
calendar/reminder events, timer/stopwatch events.

---

## 7. Mesh integration (cooperative, single loop)

No mesh modification. Apps use a thin `MeshFacade` over `hal->mesh()`:
- `send_text(dest|channel, text)` → `MeshService::sendText`.
- `subscribe(on_rx)` — the firmware registers **one** `setMessageCallback` at boot
  that fans out to subscribers (chat app + NotificationCenter).
- `nodes()/node(id)/status()` → `node_db` / `MeshService::getNode/getState`.
- History/NodeDB are **streamed from SD** (Plai already stores them) — apps page
  the visible window, never the whole log. Chat history is behind a
  `mesh::MessageLog` interface (`window()`/`scan_from()`/`match_count()`); the
  dev backing `RamMessageLog` keeps a bounded ring (cap 200 + 30-day trim, stable
  absolute cursors across front-trim), and the device backing pages from an SD
  file so 500+ messages never sit resident in 512 KB SRAM.
- The mesh is advanced by `MeshService::update()` inside the one main loop; RX
  callbacks therefore run in UI context (no cross-task marshaling needed).

Mesh-facing apps: **chat** (irssi-style single scrolling pane), **node list**,
**mesh status** (vertical list), **contacts/abook** (favorites + aliases),
**config wizard** (step overlays → NVS).

---

## 8. Background work (one cooperative loop)

Plai's model is a single `while(1)` doing `hal.updateMesh()` + `mooncake.update()`.
We keep that:
1. `hal.updateMesh()` — mesh RX/TX/routing/duty-cycle (callbacks fire here).
2. `mooncake.update()` — foreground app `onRunning` + backgrounded apps
   `onRunningBG` (NotificationCenter; later, FZF indexer).
3. ANSI flush — drain the render diff to Port-A (loop or tiny task).

"Background jobs" = `onRunningBG` work, not extra FreeRTOS tasks. Keeps RAM for
the arena and matches Plai's scheduling. Radio/keyboard interrupts only wake the
loop from display-sleep (`setNotifyTask`).

---

## 9. SD-card data layout (extends Plai's)

```
/sdcard/
  ▸ node_db ▸ <conv_id>/… ▸ favorites.dat ▸ ignorelist.dat
  ▸ templates.txt ▸ neighbors/            # ▸ = reused from Plai
  session.dat            # active mode + breadcrumb
  clipboard.dat          # persisted clipboard
  notes/                 # editor docs
  journal/               # jrnl entries (date-keyed)
  calendar/              # calcurse events + todos (GTD)
  wiki/                  # offline text wiki dump (read-only)
  fzf/                   # on-disk search index — STRETCH
  logs/                  # rotating logs + notifications
```

---

## 10. Apps & per-app RAM contracts (arena ≤120 KB/app)

| App | Primary view | Data (SD) | RAM | Notes |
|---|---|---|---|---|
| Launcher | mode list | — | <8 KB | root |
| Mesh chat | scrolling pane | conv history | ~24 KB | irssi-style; DM + channel |
| Node list | list | node_db | ~16 KB | enter→DM, info overlay, traceroute |
| Mesh status | vertical list | live | <8 KB | shares data w/ status strip |
| Contacts | list | favorites/aliases | ~12 KB | abook-style |
| Calc | input + history | — | <10 KB | expr/qalc + unit tables (flash) |
| Calcurse | list (cal\|todo) | calendar/ | ~16 KB | month-grid = modal selector; GTD |
| File browser | file list | filesystem | ~16 KB | breadcrumb; i=info, v=view(text); enter=open |
| Editor / jrnl | text buffer | notes/, journal/ | ~64–96 KB | gap buffer; <64 KB load else page from SD |
| Search (fzf-style) | query + results | other apps' stores | ~tens of KB | bounded in-RAM corpus, freed on pause; wiki via FTS5 handoff |
| **NotificationCenter** | banner + center overlay | logs/ | ~12 KB | **resident bg app** (built-in screen) |

Resident (outside arena): mesh working set, status strip + notify ring,
TextCanvas (~12 KB), clipboard (8 KB), UART ring.

### 10.1 Search (fzf-style global finder) — *built*
**As-built, this replaces the originally-planned on-disk inverted index.** On the
512 KB no-PSRAM part a background indexer over the wiki (267K articles) is
infeasible, so Search is split in two:

- **Personal content** is harvested *synchronously* on app resume into a small,
  hard-bounded in-RAM corpus (`MAX_ITEMS`, truncated labels) from the other apps'
  existing stores — contacts, todos, appointments, channels, recent messages
  (`MessageLog` window), journal lines, and file paths. Typing fuzzy-filters it
  (subsequence scoring: consecutive-run, word-boundary, and prefix bonuses). The
  corpus is freed on `on_pause`, so it only lives while Search is foreground.
  No on-disk index, no background indexer.
- **The wiki is NOT fuzzy-indexed.** A synthetic top result hands the raw query
  to the Wiki app (`nav_arg "wiki:<q>"`), which runs its own SQLite **FTS5**
  search. File *contents* are likewise not indexed (paths/filenames only;
  journal is indexed per-line).

Selecting any hit hands off to the owning app via the same `request_switch` +
`nav_arg` intent the other apps already use (`contact:`, `dm:`, `open:`).

---

## 11. PC dev emulator mapping
- **Brain native build:** app layer + TextCanvas compiled for host; ANSI backend →
  **PTY** → terminal window = the CYD, 1:1.
- **Mesh:** `MeshFacade` has two backends — dev: **Muzi R1 Neo** on `/dev/ttyACM*`
  via Meshtastic serial API (real RF peer); device: Plai `mesh_service` → SX1262.
- **HAL stubs:** sdcard→local dir, keyboard→termios stdin, bat/led→noop. Built-in
  screen / notification center → a second small terminal pane or log.
- **Wokwi** (ESP32-S3 + ILI9341) for periodic timing/pin/memory truth checks.

---

## 12. Build, partitions, licensing
- ESP-IDF v5.5 / CMake (Plai's toolchain); `protobufs/` submodule + Nanopb.
- Single large app partition + LittleFS; **updates via USB/SD** (no OTA).
- **License: GPL-3.0** (derivative of Plai). Resale OK; complete source must be
  provided to recipients; no closed-sourcing / non-commercial restriction.

---

## 13. Open items

**Resolved (v0.2, against Plai source):** Mooncake app base/registration;
`onRunningBG` background hook; mesh threading (cooperative main loop, *not* a
task); mesh API (`sendText`/`setMessageCallback`/`hal->mesh()`); HAL display split
(`_canvas` vs `_canvas_system_bar`) + `playNotificationSound`; keyboard `KeysState`
+ modifiers.

**Still open — hardware (need physical unit):** Port-A 5V boots the CYD; G1/G2 not
internally claimed; CYD GPIO35 broken out; rear antenna vs. clamshell hinge
clearance.

**Still open — design TBD:** exact `TextCanvas` API; chord map for command palette
+ Emacs bindings; CYD-side firmware choice (FabGL vs alternative) + handshake/flow
control at 921600.

---

## 14. Phasing
1. **Foundation:** fork Plai; TextCanvas + ANSI backend; launcher + mesh chat +
   node list rendering text to a PTY (PC) then the CYD; NotificationCenter resident
   bg app on the built-in screen (DMs + mentions first).
2. **Core apps:** calc, calcurse (+reminder/timer events into NotifyCenter), file
   browser, editor/jrnl, contacts, command palette, clipboard, state persistence.
3. **Search + reference:** offline Wiki (SQLite/FTS5) + fzf-style global Search
   over personal content with a wiki handoff (in-RAM corpus, not an on-disk index).
```
