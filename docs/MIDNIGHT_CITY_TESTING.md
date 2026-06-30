# Midnight City — pickup & test-feedback guide

A cold-start handoff for testing **Midnight City** (the DF-style emergent cyberpunk
sim). Read this + `docs/MIDNIGHT_CITY.md` (the design spec) to resume after context
loss. Status as of this writing: **Phases 1–8 complete** (full engine + playable
embark renderer in the emulator); **Phase 9 = device flash + on-glass soak** is the
open step; balance is intentionally rough ("tune more later").

---

## 1. What it is (one paragraph)

You play **one** protagonist — a nobody in a cyberpunk sprawl — clawing toward an
unstoppable megacorp. The world is an abstract graph of ~32 districts with ~40
algorithmic agents, ~67 micro-systems colliding into emergent story arcs. You watch
it crawl (DF embark view: an `@` walking a tile map); the avatar **self-drives** and
only interrupts you for big decisions. Cyberspace = a headless **CyberHack** run.
The city stays alive via population inflow, and can drift human / synth / mutant over
a long game. No win screen; it ends only on the protagonist's death.

---

## 2. Where everything lives

| Thing | Path |
|---|---|
| Design spec (ground truth) | `docs/MIDNIGHT_CITY.md` |
| This guide | `docs/MIDNIGHT_CITY_TESTING.md` |
| Engine (pure, shared emu+device) | `emulator/src/core/midnight_world.{h,cpp}` |
| Headless test/balance harness | `emulator/sim/midnight_sim.cpp` |
| Renderer app (the playable shell) | `emulator/src/apps/midnight.cpp` |
| Registered as | `"midnight"` / "Midnight City" in `emulator/src/host/main.cpp` + `firmware/main/main.cpp` |
| Memory (cross-session notes) | `~/.claude/.../memory/project_cardputer_midnight.md` |

Architecture in one line: a **pure deterministic engine** (`mid::`) drives both the
headless harness and the renderer; the app only *calls* the engine and draws — it
never duplicates game logic (so engine/UI parity is structural).

---

## 3. Build & run

### Headless engine tests + balance (fast, no hardware)
```bash
cd emulator
make midnight_sim && ./midnight_sim          # full suite (~2.6M checks, ~20s)
./midnight_sim world  <seed>                 # dump a generated city
./midnight_sim econ   <seed>                 # economy health curve
./midnight_sim career <seed> [wealth|mastery|territory]   # one auto-career trace
./midnight_sim scan   <ambition> [horizon]   # career outcome distribution
./midnight_sim map    <seed> <district>      # print an ASCII embark map
```

### Emulator (play it on the host)
```bash
cd emulator
make && ./emu                                # launcher; open "Midnight City"
# or pin it as the boot app for a deterministic smoke test:
printf 'session.active\tmidnight\n' > emu_state.dat && ./emu
```

### Firmware (device)
```bash
cd firmware
. ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash        # Cardputer = /dev/ttyACM0 (Espressif 303a)
# CYD terminal (the screen) is a separate ESP — already flashed; see project memory.
```
The CYD shows the 53×20 embark view over UART; the Cardputer is the brain.

---

## 4. Controls (in the app)

- **space** — pause / resume the crawl
- **`<` / `>`** — slower / faster crawl
- **m** — open the Orders menu (modal)
- **esc** — (in a menu) back; (in embark) leave to the launcher

Menu (the avatar self-drives, so this is *orders*, not micro-management):
- **Orders: pursue <ambition>** — Enter cycles Survive → Wealth → Mastery → Territory
- **Jack in** — run the net (only if the avatar has a cyberdeck; the protagonist
  starts without one — see Known Gaps)
- **Travel** — move to an adjacent district (manual override; the sim may move you too)

Screen legend: `@` = you · `p`/`s`/`m` = human/synth/mutant NPCs · capital letters
= POIs (`J`ob board, `$`market, `C`linic, `X`chop shop, `F`ab lab, `B`ar, `D`ata
vault, `f`ence, `R`drone bay, `L`chem lab, `A`rmor shop, `G`forge) · `#` wall ·
`.` floor · `~` hazard. The bottom cyan line is the **Gibson-voice ticker** (the
narrator). Status bar top: `name $money [CompanyTier] Dday hh:00 [HURT] H#S#M#`
(the H/S/M counts = living humans / synths / mutants).

---

## 5. What to look for (test-feedback checklist)

Watch a crawl for a few in-game days and note:

**Does it feel alive?**
- [ ] The ticker tells a coherent, varied story (not repetitive); arcs read well.
- [ ] NPCs/the `@` move around the map believably.
- [ ] Events feel causally linked over time (a shortage → trouble, a heist →
      heat, a megathreat → panic → someone puts it down).

**Pacing & readability (CYD 6×12 font, 53×20):**
- [ ] Crawl speed is comfortable (default ~600ms/tick; `<>` to taste).
- [ ] Status bar fits and is legible; nothing cut off.
- [ ] Ticker lines fit on one row and read cleanly.
- [ ] Camera-follow keeps the `@` on screen; map is readable.

**The arc / progression:**
- [ ] Money/skills/company tier visibly progress over a long sit.
- [ ] Setting an Ambition visibly changes what the avatar does.
- [ ] Death is meaningful but not constant; you can survive by playing safe.
- [ ] Over a *long* game the city composition can shift (H/S/M counts drift).

**Stability (the Phase 9 gate):**
- [ ] Boots and renders on the real CYD.
- [ ] Runs for a long time without reboot (watch for resets — see project memory's
      reboot notes; capture serial if it reboots).
- [ ] Heap stays stable over uptime (System/About app shows free RAM).

Please jot specifics: seed/screenshot, what the ticker said, what felt off
(too fast/slow, too deadly/easy, confusing glyphs, illegible text, boring stretches).

---

## 6. Known gaps / rough edges (expected, not bugs)

- **Balance is rough on purpose.** Career death rate, water-riot frequency, and
  the inflow turbulence are first-pass numbers (`MidTunables` in
  `midnight_world.h`). Feedback on "too deadly / too easy / too chaotic" is exactly
  what's wanted.
- **Protagonist starts with no cyberdeck**, so "Jack in" is mostly NPC-facing for
  now; deck acquisition is future content. (NPC deckers do jack in the background.)
- **Menu is shallow** (orders / jack / travel). Info screens (self / people here /
  company) and collision→interaction decision modals + a live-play flatline spike
  are planned next.
- **Ticker shows whatever the latest beat is** — no selection/rate-limiting yet, so
  busy days can flicker. Beat prioritization is a known follow-up.
- **No births/immigration tuning pass** beyond keeping population stable; the
  human/synth/mutant drift is a possibility space, mutant-led cities are rarer
  (need toxic-heavy worldgen).

---

## 7. If you need to resume the build work

The phase plan + gates are in `docs/MIDNIGHT_CITY.md` §12. Each phase shipped with
its harness gate; re-run `./midnight_sim` to confirm all green (it prints per-phase
results + a final `N checks, 0 failures`). The project memory file has a detailed
per-phase changelog (commits, tunables, decisions) for deep context.

**Immediate next steps after a successful flash:** confirm on-glass render + an
overnight-ish soak (heap-min over uptime, no reboot). Then start the gameplay-depth
+ balance iteration listed in §6.
