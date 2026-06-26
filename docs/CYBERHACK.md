# CyberHack — design spec (v1)

A Neuromancer / Cyberpunk-2077 / Edgerunners-flavored cyberspace roguelike that
lives as a normal app inside the Cardputer Deck firmware. Auto-driven "decker"
runs the net; you steer at decision spikes; runs produce DF-legends-style
chronicles.

> **Implementation note.** This is a purpose-built app, **not** a NetHack fork.
> NetHack assumes a hosted OS (multi-MB binary, level/dungeon compilers, global
> state, blocking turn loop) and does not fit an ESP32-S3 with no PSRAM beside
> the resident mesh + SQLite firmware (~91–136K free heap, ~46K floor). It also
> can't be an `App` (it owns the main loop) and can't share the emulator/device
> core. Every system we actually want — node graph, auto-player, factions, Heat/
> Corruption, legends — NetHack gives nothing for, while everything it *does*
> give (roles, items, shops, spells, tile dungeon, manual turns) we delete. So
> we keep the NetHack *feel* (permadeath, emergent stories, ASCII immersion) and
> build the rest bespoke, with a fixed, knowable RAM budget.

## Locked decisions

| Question | Decision |
|---|---|
| Cross-run persistence | **World remembers, deck resets** (pure roguelike) |
| Auto-player pacing | **Watch it crawl** (~600 ms/sim-step, animated) |
| Visual model | **Hybrid: net-graph travel view + rendered node interior** |
| Mesh broadcast of chronicles | **Later hook** (keep chronicle compact so it *can* become a LoRa payload) |

## Depth model — power vs. threat

Like NetHack, depth is the **within-run climb**, not cross-run stat growth.
A deck that resets every run is the same model NetHack uses (permadeath, no
carryover) — the richness is the intra-run power curve plus a world that
ratchets challenge across runs.

**The climb (run-scoped power sources):**
- **Tier** — rep/deck level; rises by cracking high-security nodes / completing
  the objective. Raises base combat math. (≈ XP level.)
- **Module upgrades** — the 5 modules start at L1; spend **data shards** at
  fabricator/shrine nodes to level them. (≈ gear.)
- **Buffer growth** — max Buffer scales with Tier → chain more routines/encounter.
- **Persistent mods** — rare run-long passives ("−1 Heat/step", "Spike no longer
  punished by Black ICE"). (≈ rings/intrinsics.)
- **One-shot scripts** — shard-bought consumables. (≈ scrolls/potions.)

**The wall (threat scaling, tuned to your power so grinding can't outrun it):**
- **Node security** — objective sits behind progressively harder nodes.
- **Heat** — player-controlled risk dial. T1 patrols → T2 *hunt* (named hunter
  spawned at **your Tier+1**) → T3 lockdown. Scaling to Tier means raw power
  never outruns Heat.
- **Corruption** — pushing greedily degrades your *own* power (misfires, Mask
  failures); a long greedy run is self-limiting from the inside.

**Knife-edge:** every big score raises power **and** Heat together. Cautious =
stay matched, extract early, modest haul. Greedy = briefly out-level the net,
then Heat overtakes you → the death that becomes the chronicle.

Balance handle (tune in the emulator):
```
PlayerPower = base + Tier·k1 + Σmodule_levels + mods − Corruption·p
ThreatLevel = node_security + Heat·k2 + faction_grudge·k3
```
Both rise through a run; only Heat's term is under player control. Keep them
within a hair of each other.

**Cross-run layer (what deck-reset buys):** each run the net boots with
accumulated grudges → hunters earlier, more marked nodes, burned routes forcing
riskier routing. Deck power resets; *challenge ratchets*; mastery + world state
is the meta-progression.

## Visual model — hybrid (supersedes the earlier "local radar" idea)

NetHack-grade ASCII: glyphs, color, box-drawing, a live message line + status
line, all native to our TextCanvas → VT100 terminal (palette, box-drawing,
per-cell diffing so a moving `@` is a tiny UART update). The auto-player drives;
you *watch* `@` traverse the scene.

- **GRAPH (travel/overview):** net as an ASCII node-link graph; daemon hops node
  to node; hunters/objective marked.
- **INTERIOR (encounter):** the entered node rendered as a small ASCII
  room/circuit; `@` walks to features (`$` payload, `&` warden, `D` ICE),
  triggering encounters → decision spikes. Generated procedurally from
  `(world_seed, node_id)` → stable per run, freed on exit.
- **DECISION** overlay (card) and **CHRONICLE** end screen on top.

Glyph legend: `@` you · `D` ICE/daemon · `$` payload/shards · `&` warden ·
`==`/`===` data channels · `#` walls. Status line: `Intg Buf Shld Heat Corr Tier`.

"Watch it crawl" now has two scales: graph hops (travel) and `@` walking the
interior (encounter).

## World model (freed on app-close; nothing in steady-state heap)

| Structure | Shape | Bytes |
|---|---|---|
| `Node[60]` | faction:u8, type:u8, security:u8, flags:u8 (burned/marked/backdoor/visited), x:u8, y:u8, name_id:u8 | ~480 |
| Topology | per-node neighbor `u8[4]` + edge attrs (risk/bandwidth/trace:u8) | ~1.0K |
| `Faction[5]` | name_id:u8, attitude (friendly/neutral/hostile/hunting), grudge:i8, flags:u8 | ~30 |
| `NamedICE[8]` | name_id, archetype, traits, tier, status, mem_ref | ~64 |
| Event ring `[64]` | tick:u16, node:u8, faction:u8, actor:u8, action:u8, outcome:u8, tag:u8 | ~0.8K |
| RunState | intg/buf:i16, shield/tier/heat/corr/pos:u8, objective, personality, xorshift128 rng | ~60 |
| Interior scratch | current node only, procedural from seed | ~0.6–1.5K |

**Working set ≈ 3–5K**, allocated in the app object, freed on app-switch.
Names + all templates live in flash, not RAM.

## Tick model — two modes off the existing 30 Hz loop

- **AUTO:** one sim-step every ~600 ms (`ctx.now_ms`-gated). Step = BFS/greedy
  hop toward objective (graph) or `@`-walk toward a feature (interior),
  auto-resolve trivial ICE per personality, accrue Heat/time, append log events.
- **DECISION:** sim frozen, card up, pick resumes AUTO.
- Redraw fires on step/decision/input only (not every frame); per-cell diffs.

**Decision-spike triggers:** named/high-tier ICE; sharp risk-vs-reward branch;
faction-sensitive action (betray/steal/help); Integrity-low / Heat-high survival
moment; objective reached (choose extraction).

### Integration gotcha — keep-alive
"Watch it crawl" runs for seconds with no keypresses, but CYD idle-sleep blanks
the backlight after 120 s of no input. **While a dive is in AUTO, the app feeds
`last_input` (or a "screen busy" flag) each step** so the terminal stays lit;
idle-sleep resumes normally on the launcher / on a long-idle decision card.

## Decision-card grammar (data, not code → cheap to author *and* lint)
```
Card { prompt (templated: {node}{enemy}{faction})
  Option[2..4] {
    label              "Push through with Spike"
    requires?          module/stat gate → greyed if unmet
    effects[]          {stat Δ, flag set, log-tag}
    risk?              {chance, bad-branch effects}
  } }
```
Cards instantiate from templates keyed by `(trigger × archetype)`. **Personality**
(RECKLESS/CAUTIOUS/OPPORTUNIST/LOYALIST) pre-highlights an option and decides
which trivial encounters auto-resolve without pausing.

## Combat — multi-round ICE sieges (NOT a single roll)

A thin coin-flip resolver tested as shallow (a perfect weakness-matcher was
invincible; balance was fragile; "optimal play" was a lookup). Combat is now an
attritional **multi-round fight** against named ICE — the Neuromancer feel of
wearing down a wall of black ICE while it tries to flatline you. Trivial ICE
(Watchdog / unnamed) the deck still auto-sweeps for a small toll.

Each named ICE has its own **integrity** (`12 + tier·3`). Per round you commit one
module, which costs scarce **Buffer**; the ICE claws back. The fight ends when the
ICE breaks, you flatline, you disengage, or it stalemates (≥8 rounds → it breaks
off). Modules in a fight:
- **Spike** — raw break (`9+lvl`), cheapest (2 Buffer). Generalist, but **Black
  ICE and the Sysop punish it**: their counterattack doubles (`atk·2 + tier`).
  Spike-spamming a boss gets you mauled.
- **Mask** — small break, blunts the incoming hit to ⅓, sheds Heat. Counters Trace/Sysop.
- **Fork** — spawns a helper that keeps chipping each subsequent round. Counters Black/Swarm.
- **Patch** — no break; heals (`16+lvl·3`), trims Corruption. A wasted round of damage.
- **Ghost** — no break; attempts to **disengage** (flee the fight, abandon the
  score). Success scales with level, falls with ICE tier / overclock / corruption.

The **counter** for each ICE is always a damage module (Spike/Mask/Fork) and gets
**+12 break** — the right tool ends the fight fast, so you take fewer
counterattacks. Ghost/Patch are universal tactics, never a "counter".

**The Buffer economy is the spine.** Buffer (`max 30`, regen +3 only on quiet
nodes) can't cover every fight optimally across a dive. Run dry → **overclock**:
the action still fires at ⅗ power and adds Corruption. By the boss you're usually
on fumes — overclocking into Corruption, risking misfires (`corruption>60`). The
real decision each round: spend the scarce counter, race with cheap Spike (and
eat the punish on heavy ICE), Patch (lose a round), or Ghost out and walk away
from the loot. Killing a named ICE → **+1 Tier** (the within-run climb).

## Story engine

Event ring tagged with: `close_call, big_score, betrayal, revenge, sacrifice,
humiliation, first_blood, lockdown_survived`. **Chronicle** = walk the ring, pick
highest-weight tagged events, fill flash sentence templates with
`{node}{faction}{enemy}{stat}`, append a stat footer (nodes burned / ICE killed /
final Heat-Corr / bounty). → copy-to-clipboard via the `clip` seam; mesh
broadcast later.

> *"You burned ZAIBATSU's vault at GRID-13, crippled the daemon GREYHOUND, and
> walked out with a ghost-key. Two dives later ZAIBATSU locked half the net and
> sent KRAKEN after you. You died in an abandoned gang node at 97% corruption
> with a bounty on your head."*

### Gibson-voice generator (no LLM)

The chronicle is rendered in a **William Gibson / Neuromancer register** by a
template-grammar + weighted-lexicon + simile engine — all flash tables, a few
hundred bytes of string-builder scratch, **seeded from the run's xorshift RNG**
so the same seed yields the same chronicle (reproducible copy-to-clipboard +
shareable). Gibson's signature moves are structural, hence mechanizable:

1. **Synesthetic simile** (the thumbprint): `X, like <concrete urban/biological/
   commercial image>`. A simile slot fires on ~half of beats — a "purple-ness"
   dial keeps it short of parody.
2. **Brand-noir nouns**: chrome, neon, the sprawl, black ice, dermal grafts, trace.
3. **Terse, fragmentary cadence**: hard cuts, occasional verbless sentences.
4. **Atmosphere opener** (keyed to run mood) + **epitaph closer** (keyed to
   outcome: `died_corruption / hunted_down / extracted_rich / extracted_broke`)
   bracketing the tagged beats.

Pieces:
- **Skeleton templates** per event tag, in Gibson cadence, slots
  `{node}{faction}{enemy}{simile}{ice-verb}{body}{color}`.
- **Lexicons**: small const arrays per slot (sky/weather, ice-verbs, body, color-
  as-media, similes), ~8–15 entries each. ~8 openers, ~6–10 templates/tag, ~8
  closers/outcome → thousands of seam-free chronicles.

**Voice:** second person ("You came up into Night-Net…") by default; switching to
third-person-with-handle is a one-line knob.

**Hard rule:** write **original** lines in Gibson's idiom; never embed actual
Neuromancer text (copyright; verbatim repeats are obvious). Riff on the famous
"dead-channel sky" as homage, don't quote it.

Sample output the grammar produces (original pastiche):

> *You came up into Night-Net under a sky the color of a screen with no signal.
> GRID-13 cracked open easy, Zaibatsu's black ice spilling out like neon into wet
> concrete, and you took the payload and the ghost-key both. GREYHOUND you burned
> — peeled it down to static — but the heat had your scent by then. They sent
> KRAKEN up through the dead grids while your Mask failed like cheap dermal grafts
> going septic. It caught you in an abandoned gang node, bounty already on the
> wire. No fanfare. Just the long fall into white noise.*

**Honest ceiling:** convincing pastiche, seam-free in normal play — not
indistinguishable from Gibson.

## Persistence (tiny SD blob, <1K) — world only

faction grudges · named-ICE status (alive/crippled/dead + grudge) · burned routes
· marked/hot nodes · run count · best score. Loaded at app start, written at run
end. The deck/RunState is **not** persisted.

## Content surface (finite & lintable)

5 factions · 5 node types · 6 ICE archetypes · ~8 named-ICE seeds · 5 modules ·
~6 card templates · ~10 tag→sentence sets. A beats-style linter verifies every
archetype has a weakness, every tag a sentence, every template its fields.

## Build plan — engine-first, test-gated

### Architectural keystone
The engine is a **pure, deterministic library** with a single seam, the
`DecisionPolicy`. Every choice (auto-resolve, decision card, personality bias)
goes through it; the UI is just one policy + a renderer. The same engine runs
10k times in a CI loop under an AI policy and once on hardware under the human.
Correctness never depends on the screen.

```
DecisionPolicy { Choice decide(const RunState&, const Decision&); }
  ├─ AiPolicy(personality)   → headless tests & balance harness
  └─ HumanPolicy(ui)         → the app
```

### Layout
- `core/cyberhack_world.{h,cpp}` — the engine (GLOB'd into both emulator & firmware).
- `host/cyberhack_sim.cpp` — headless test + balance harness (in `host/`, outside
  the `core/*`/`apps/*` GLOBs, so it never compiles into firmware). Dependency-free
  (`CHECK` macro, no gtest). Subcommands: `gen-check`, `run`, `balance`,
  `chronicle`, `lint`.
- `apps/cyberhack.cpp` — the app (Phase 8).

There is currently **no C++ test harness in the repo**; building `cyberhack_sim`
is part of Phase 1.

### Phases (each gate must pass before the next; no rendering before Phase 8)

1. **Seams & determinism.** Structs + `Rng` (xorshift128, snapshot/restore);
   `cyberhack_sim` shell + `CHECK`.
   → *Gate:* same seed → identical RNG stream; harness builds & runs.

2. **World generation.** `generate_world(seed)`: 40–60 nodes, factions/types/
   security, connected topology, objective, loot/ICE.
   → *Gate:* 10k-seed property sweep — connected, objective reachable, degree 1–4,
   no orphans, distributions in-band, deterministic. World can never gen broken.

3. **Step loop + combat + policy seam.** AUTO step (BFS toward objective, move,
   resolve, accrue Heat/time, log), decision-spike detection → `Decision`, the
   module×signature combat table, Heat/Corruption math, terminal conditions.
   → *Gate:* 10k headless runs (AI policy) all terminate (bounded, no infinite
   loop), stats clamped to valid ranges, zero crashes, log well-formed.

4. **Balance harness + tuning (strength gate).** `balance --seeds 10000 --policy …`
   aggregates extract/death rate, death-cause histogram, run-length distribution,
   Tier reached, and the **PlayerPower vs ThreatLevel gap sampled each step**.
   → *Gate:* tuned `k1/k2/k3/p` + Heat thresholds hit targets (e.g. cautious
   ~60–70% extract/smaller haul, reckless ~30–40%/bigger haul), deaths spread
   across causes, **no dominant strategy**. Final constants written back here.
   *This phase decides whether the game is deep.*

5. **Cross-run memory + persistence.** <1K world blob (grudges, named-ICE status,
   burned routes, marked nodes) applied at world-gen; (de)serialize.
   → *Gate:* scripted multi-run scenario shows expected escalation; blob
   round-trips byte-stable.

6. **Chronicle (Gibson) engine.** Event ring → tagged beats → grammar → text,
   seeded from run RNG.
   → *Gate:* same seed → same chronicle; no unfilled `{slots}`; length bounded;
   deterministic; human eyeball pass on ~20 samples.

7. **Content linter.** `lint`: every archetype has a weakness, every template's
   options resolve, every tag ≥1 sentence, every lexicon slot non-empty, combat
   table total.
   → *Gate:* lint clean; wired pre-build.

8. **App shell + render.** `apps/cyberhack.cpp`: GRAPH + INTERIOR views,
   decision-card UI, AUTO/DECISION state machine, `last_input` keep-alive,
   `HumanPolicy`.
   → *Gate (engine/UI parity):* a scripted decision sequence fed to the UI and to
   the headless engine must yield byte-identical final state.

9. **Device integration + soak.** Launcher splash + right-arrow jack-in; SD legends
   blob on device; flash-size + heap-delta check; on-glass keep-alive.
   → *Gate:* hardware soak — no heap growth across runs, backlight stays lit
   through a crawl, legends survive a power cycle.

Phases 1–7 are entirely headless (~3–5K of pure logic): the engine is proven
correct, deterministic, and *balanced over 10k runs* before any glyph is drawn.

## Tuned constants (Phase 4 gate record)

Locked after a 20k-seed sweep per policy (see `Tunables` in `cyberhack_world.h`):
`base_power 8 · k_tier 4 · k_module 1 · k_shield 2 · p_corruption 4 · sec_threat 5
· k_tier_threat 3 (world scales with your Tier) · k_heat 4 · k_grudge 2 ·
heat T1/T2/T3 = 25/48/80`. Combat: weakness +8, punish −10, Spike generalist +9,
roll variance ±10. Boss = named, tier = node.security+1, archetype Black/Sysop.
Named ICE are placed along the entry→objective path (the gauntlet) with escalating
tiers; killing one grants +1 Tier (the within-run climb).

Balance over 20000 runs/policy on citynet=1 (with multi-round combat):

| Policy | Extract | Died | Shards | Tier | Corr | Deaths |
|---|---|---|---|---|---|---|
| Reckless | 41% | 59% | 421 | 5.8 | 20 | ICE + hunted |
| Cautious | 74% | 26% | 253 | 5.8 | 10 | ICE |
| Opportunist | 88% | 12% | 468 | 5.9 | 9 | ICE |
| Loyalist | 74% | 26% | 253 | 5.8 | 10 | ICE |

~13–23 decisions/run (fights are multi-round sieges). Power-vs-threat gap: avg
−8 to −20, min −68 (bosses), max +13 — you fight from behind, clawing even with
the right tool and the Buffer to afford it. No policy at 0%/100%; Spike-spam
(reckless) is punished at heavy ICE and draws hunters. Remaining live-tune knobs:
opportunist's edge (skilled-greedy is strongest), per-personality AI nuance,
interior size vs. terminal grid.
