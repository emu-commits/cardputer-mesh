# Midnight City — pickup & test-feedback guide

A cold-start handoff for testing **Midnight City** (the DF-style emergent cyberpunk
sim). Read this + `docs/MIDNIGHT_CITY.md` (the design spec) to resume after context
loss. Status as of this writing: **Phases 1–8 complete** plus **two playtest-feedback
rounds — overhaul A–D and round-2 polish E–H** (plan:
`.claude/plans/witty-doodling-adleman.md`). The on-glass soak (open it on the CYD, run
it a while, watch heap) is the open step; balance is intentionally first-pass.

Round-2 polish (batches E–H, after the first round's chronicle review):
- **Death is choice-gated** (E): an incident never one-shots — it robs + wounds; death
  only strikes the *already injured AND broke* on a repeat hit. A cautious avatar gets
  patched up (survives); a *reckless* one (high risk dial) pushes on through wounds and
  lingers in dangerous blocks → that's how risk-takers die. At default risk you now
  survive 1500+ days.
- **Narrator is generative-only** (F): the renderer dropped the baked "named arcs" and
  the "X gave way to X" spam; it surfaces one salience-ranked per-event beat per crawl.
- **Denser world** (G): `MAX_AGENTS` 40→56 + denser services → far more
  shortages/riots/turf/combat/refugees (save format v4).
- **Crafting tree** (H): the **Crafting** menu shows your trade/skill + inventory +
  recipes; pick one and the @ gathers parts and makes it. Skilled work crafts-and-sells
  for income. Craft a **cyberdeck** (Expert decker + parts) to unlock "Jack in". Crafted
  armor/implants cut mugging lethality; chems blunt stress (addiction risk).

What the first overhaul (A–D) changed (vs the very first on-glass build):
- **Layout**: shorter map + a wrapping multi-line **log** panel; a **focus row** and
  a **legend row** under the status bar; content-sized modals.
- **Economy is legible**: every change to your cash is a logged transaction
  (`+$12 wage`, `-$8 supplies`, `-$50 rent`…); off-screen NPC chatter is filtered out.
- **Focus-driven life**: you start **unemployed**, hunt for work (a steady job OR a
  one-time **contract/gig**), then a daily **work↔home commute** is the default loop.
  Events interrupt it (you flee danger, then resume). **No district warping** — the
  `@` walks to the map edge before crossing. The company never self-founds anymore.
- **Company management**: found with a chosen **business model (sector)**; a Company
  screen shows P&L (treasury, crew/target, gross/payroll/upkeep/net); **grow/shrink**
  headcount directives.

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

Menu (the avatar self-drives micro-steps; you set focus + milestones):
- **Standing orders** — sub-menu: pursue Survive / Wealth / Mastery / Territory
- **Set focus** — sub-menu: Look for work · Rent an apartment · Found a company ·
  Run the company · Resume daily work · Abandon the contract
- **Company** — P&L screen + actions: Grow (hire) · Shrink (lay off) · Change
  business model · (or **Found a company…** → pick a sector, if not yet founded)
- **Crafting** — trade/skill + inventory + recipes; pick one to set a craft directive
  (the @ gathers parts and makes it). Craft a **cyberdeck** to unlock jacking in.
- **Jack in** — run the net (needs a cyberdeck — craft one; starts without)
- **Travel** — set an adjacent district as the destination; the `@` walks to the
  edge and crosses (no warp)

Screen rows (top→bottom): **status bar** (`name $money [CompanyTier] Dday hh:00
[HURT] H#S#M#`; H/S/M = living humans/synths/mutants) · **focus row**
(`Focus: … - …`, or `fleeing danger (was: …)`, or a contract line) · **legend row**
· the **embark map** · the wrapping **log** (transactions + Gibson-voice narration)
· footer. Glyphs: `@` = you · `p`/`s`/`m` = human/synth/mutant NPCs · POIs (`J`ob
board, `$`market, `C`linic, `X`chop shop, `F`ab lab, `B`ar, `D`ata vault, `f`ence,
`R`drone bay, `L`chem lab, `A`rmor shop, `G`forge) · `#` wall · `.` floor · `~` hazard.

---

## 5. What to look for (test-feedback checklist)

Watch a crawl for a few in-game days and note:

**Overhaul (batches A–D) — verify the punch-list fixes:**
- [ ] Map is shorter; the **log wraps** cleanly with no mid-word breaks.
- [ ] The **focus row** reads true (find work → work → commuting/resting; "fleeing
      danger" when a block goes hot; a contract line when on a gig).
- [ ] **Money matches the log**: every up/down on the status bar has a transaction
      line (`+$ wage`, `-$ supplies`, `-$ rent`, `+$ contract`, `-$ invested`).
- [ ] **No NPC spam**: you only see beats here / about you / city-scale.
- [ ] **No warping**: travel + sim-driven moves walk the `@` to the edge first.
- [ ] You start with **no job**; "Look for work" lands a job or a contract.
- [ ] Menus: nested Orders / Set focus / Company; modal text fits the box.
- [ ] **Company**: Found a company (pick a sector) → the Company screen shows P&L;
      Grow/Shrink changes the crew target; the company doesn't appear on its own.

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

- **Balance is still first-pass** (knobs in `MidTunables`, `midnight_world.h`).
  Death is now choice-gated (cautious survives; reckless dies) — feedback on whether
  reckless death feels frequent *enough* (or too much) is wanted. Skilled crafting
  income (`production_value`) vs the company ladder may want tuning. Riots remain the
  rarest emergent beat (fire in aggregate, sparse per-seed).
- **Sector has no heat/risk downside** yet (#14 is choice + revenue only); illicit
  sectors are currently pure upside. Heat/reputation is a planned balance pass.
- **No collision→interaction modals yet** — co-located NPCs walk the map but bumping
  into one doesn't open a decision. Planned next.
- **Some beats reuse generic event narration** (e.g. hired/lease via EV_RECRUIT), so a
  few *log lines* read generically; the focus row + transaction line carry the precise
  meaning. (Crafting/deck now have bespoke EV_CRAFTED lines.)
- **The named arcs are TEST-ONLY now**: the player log is pure generative per-event
  narration (one salient beat/crawl). `ArcTracker`/`narrate_arc` live only in the
  engine + harness to prove the chains fire (`./midnight_sim chronicle <seed> <days>`
  prints them for eyeballing).
- **Heap watch (on-glass):** Batch G's `MAX_AGENTS` 40→56 grows World + the
  RAM-resident NVS save (~+1.5 KB). Confirm idle/app-open/after-a-save heap is healthy
  before pushing `MAX_AGENTS` higher.

---

## 7. If you need to resume the build work

The phase plan + gates are in `docs/MIDNIGHT_CITY.md` §12. Each phase shipped with
its harness gate; re-run `./midnight_sim` to confirm all green (it prints per-phase
results + a final `N checks, 0 failures`). The project memory file has a detailed
per-phase changelog (commits, tunables, decisions) for deep context.

**Immediate next steps after a successful flash:** confirm on-glass render + an
overnight-ish soak (heap-min over uptime, no reboot). Then start the gameplay-depth
+ balance iteration listed in §6.
