# Midnight City — design spec (v1, draft)

A Dwarf-Fortress-style emergent simulation re-skinned into a Neuromancer /
Chiba-City cyberpunk setting, living as a normal app inside the Cardputer Deck
firmware. You play **one** protagonist — a nobody who starts with nothing and
claws toward an unstoppable megacorp. The world is a small graph of districts
full of algorithmically-driven NPCs. The payoff is **emergence**: ~65 tiny
interacting micro-systems that continuously manufacture scarcity, conflict, and
opportunity, colliding into unpredictable chain-reaction story arcs.

> **Implementation note — two layers.** DF's identity is two things: a deep
> *simulation* and the *embark view* you watch it through. We keep both, but
> split them so each fits an ESP32-S3 with no PSRAM:
>
> 1. **Simulation layer — abstract district graph.** A graph of ~32 district
>    nodes (not a global tile world), ~40 agents, abstract recipe tables. This is
>    where all ~65 micro-systems live. No digging, z-levels, fluids, or hundreds
>    of agents — the things that blow up RAM/CPU. Cheap: rules are code (~1.8 MB
>    flash free); the live state substrate is a few KB.
> 2. **View layer — realized local embark map.** The *one* district the
>    protagonist currently occupies is realized as a DF-classic tile map you
>    watch the `@` (and co-located NPCs) walk around, exactly like DF's embark/
>    fortress view. Only one district is resident at a time (~2–4 KB), generated
>    deterministically from its seed; travel swaps it. CyberHack already proves
>    this split — abstract net + a rendered interior where `@` walks. See
>    [§6 UI](#6-ui--rendering-cyd-53x20--embark-view) and [Budget](#budget).
>
> So the world *simulates* as a graph but *presents* as a place you move through.

> **The cyberspace win.** The design's headline feature — "when an entity jacks
> in, none of it happens in cyberspace; run [CyberHack](CYBERHACK.md) headless
> with AI decision-making driven by the entity's situation, and feed the results
> back" — is the *cheapest* thing in this doc, because the CyberHack engine
> already does exactly that. See [CyberHack bridge](#cyberhack-bridge).

## Locked / proposed decisions

| Question | Decision |
|---|---|
| Protagonist count | **One** named character (DF "adventurer-scale," not a colony) |
| Goal | Start a company → grow it to a **megacorp**; amass billions of shards |
| Cross-load persistence | **Same world, autosaved to SD**; resume where you left off (menu option to reset) |
| World model | **Two layers**: abstract district *graph* (the sim) + a realized local *tile map* for the district you're in (the view). One district resident at a time |
| View | **DF-classic embark view** — the current district as an ASCII tile map; the `@` and co-located NPCs walk around; camera follows the `@` |
| Input | **Fully menu-driven**, **no map cursor** — pick a destination/POI/action from a menu (modal overlay) and watch the `@` pathfind there |
| Pacing | **Watch it crawl** — world advances in ticks; player sets the protagonist's activity and steers at decision spikes |
| Cyberspace | **Headless CyberHack run**, personality chosen from the entity's state; results feed back |
| Chronicle | Gibson-voice **arc narrator**, no LLM (reuse CyberHack's template-grammar approach) |

---

## 1. Core loop

```
        ┌─────────────────────────────────────────────────────────┐
        │  world ticks (hours) — ~65 micro-systems update the      │
        │  shared substrate; pressure/scarcity/opportunity build   │
        └─────────────┬───────────────────────────────────────────┘
                      │  a system raises an event that needs you
                      ▼
        ┌─────────────────────────┐      you set the protagonist's
        │  DECISION SPIKE (modal)  │ ───► activity / order, or react
        └─────────────┬───────────┘      to a situation
                      │
                      ▼
        protagonist + every NPC act on their rules → outcomes mutate
        the substrate → feeds the next tick's systems → arcs emerge
```

Time advances in **ticks ≈ 1 in-game hour**, animated at "watch it crawl" speed
(default ~400–600 ms/tick, with pause / normal / fast). The protagonist holds a
**current activity** (work a shift, craft, travel, scavenge, negotiate, jack in,
rest…). The sim runs forward, pausing when an activity completes or a system
surfaces a **decision** (`AR_DECISION`). There is no win screen and no auto-end
— the game ends only on the protagonist's death; "winning" is reaching megacorp
tier, which keeps playing.

> **Agency model — the avatar self-drives; you are interrupted only when it can't
> decide alone.** The protagonist (agent 0) runs on the *same* `DecisionPolicy`
> seam as every NPC, with a personality/policy. Anything it can legitimately
> resolve on its own, it just *does* — and you watch it happen (log + ticker),
> not a prompt. The human is escalated to **only** when a choice crosses an
> **escalation threshold** the avatar shouldn't take unilaterally:
> - **irreversible or life-risking** (confront an armed grudge-holder, take a
>   dangerous contract, jack a hostile target with low buffer);
> - **large resource commitment** (a big bribe, buying a facility, hiring);
> - **relationship- or identity-defining** (betray an ally, switch factions);
> - **genuinely ambiguous** (no clear best option under the avatar's policy).
>
> Everything below the threshold — accept a fair trade, take free training,
> dodge a trivial bump, buy needed food at the going rate, auto-run a routine
> low-risk hack — the avatar handles, and it appears as a ticker line, never a
> modal. When escalated, the sim **interrupts** (pauses the crawl, pops the
> modal). This is exactly CyberHack's pattern (auto-combat resolves rounds; only
> strategic spikes surface) generalized to the whole game. The escalation
> threshold is tunable, and could later expose a per-category "always ask me /
> let me decide" setting.

---

## 2. State substrate (the thing that costs RAM)

Everything below is fixed-size arrays — no per-tick allocation, IDs + flash
string tables instead of `std::string` for names. The ~65 systems all read and
write *these same fields*; that shared substrate is what makes chains possible.

### 2.1 Districts (nodes) — `N ≈ 32`
```c
struct District {            // ~24 B
  uint8_t  type;             // SLUM, ARCOLOGY, MARKET, INDUSTRIAL, UNDERCITY,
                             // DATACENTER, TOXIC, LOGISTICS, ROOFTOP_FARM, METRO
  uint8_t  owner;            // faction id, or NONE (contested/neutral)
  uint8_t  influence[F];     // per-faction influence field (the "1 byte/tile" → 1 byte/node)
  uint8_t  supply[C];        // per-commodity local supply (C≈8 commodities)
  uint8_t  hazard[H];        // toxin, blackout, alarm/heat, disease, collapse (H≈5)
  uint8_t  population;       // agents resident
  uint8_t  prosperity;       // local wealth (drives demand + rent)
  uint8_t  danger;           // aggregate threat (drives flee/avoid)
  uint16_t services;         // bitmask: which workshops/vendors/job-boards are here
};
District districts[N];
uint8_t  adj[N][MAXDEG];     // graph adjacency (sparse), like CyberHack edges
```

### 2.2 Agents — `M ≈ 40` (agent 0 = the protagonist)
```c
struct Agent {               // ~52 B
  uint8_t  name_id;          // index into a flash name-pool (no std::string)
  uint8_t  loc;              // district id
  uint8_t  faction;          // affiliation, or NONE
  uint8_t  personality;      // trait vector packed: greed/caution/loyalty/aggression
  uint8_t  need[NEED];       // hunger,thirst,fatigue,social,safety,cyberware,addiction,stress (NEED≈8)
  uint8_t  job;              // current profession id (0 = none/unemployed)
  uint8_t  skill[JOBS];      // XP/tier per profession — sparse in practice (see §4)
  uint8_t  mood;             // derived morale/cyberpsychosis gauge
  uint8_t  status;           // bitflags: injured, addicted, in_debt, employed, fleeing…
  uint32_t money;            // shards
  uint8_t  inv[ITEMCLASS];   // abstract counts per item class (ITEMCLASS≈8)
  uint8_t  activity;         // what they're doing this tick
  Relation rel[RELMAX];      // sparse top-K relationships {other_id, valence}  (RELMAX≈4)
  Scar     scar[SCARMAX];    // memory scars {node, kind, valence}              (SCARMAX≈3)
};
Agent agents[M];
```

### 2.3 Factions — `F ≈ 6`
Street Crew · Megacorp Enclave · Syndicate · Nomad Caravan · Mutant Gang ·
Techno-Cult. (Rogue-AI Collective optional.)
```c
struct Faction {             // ~16 B
  uint8_t alignment, tech_level, specialty;
  int8_t  grudge[F];         // standing toward each other faction (and the player)
  uint8_t leader;            // agent id
  uint32_t treasury;
};
```

### 2.4 Player company
```c
struct Company {
  uint8_t  name_id;
  uint8_t  tier;             // SOLO → CREW → OUTFIT → CORP → MEGACORP
  uint32_t treasury;
  uint8_t  employees[EMPMAX]; // agent ids hired (EMPMAX≈8 early, grows by tier)
  uint16_t assets;            // bitmask: facilities owned (chop shop, fab lab, clinic…) per district
  uint8_t  reputation;
};
```

### 2.5 Events / micro-quests — bounded ring
```c
struct Event { uint8_t kind, node, agent, data; uint16_t tick; };
Event events[EVMAX];         // EVMAX≈32, oldest-evicted ring
```

### 2.6 Per-target CyberHack legends
A small `cyber::Legends` blob per persistent data-target the protagonist/NPCs
hack repeatedly (so the matrix "remembers" across jack-ins). Stored sparsely.

### 2.7 Resident local tile map (the view layer — only ONE at a time)
The district the protagonist currently occupies is *realized* as a tile grid for
the embark view. Every other district stays purely abstract (§2.1). Travel frees
this and realizes the destination.
```c
#define LW 64   // local map width  (tiles)   — tune; camera scrolls within it
#define LH 40   // local map height (tiles)
struct LocalMap {                 // ~2.6 KB resident, regenerated on entry
  uint8_t tile[LH][LW];           // terrain/feature glyph index (1 B/tile)
  uint8_t poi_count;
  struct { uint8_t x, y, kind, target; } poi[POIMAX]; // workshops/vendors/boards/your facilities
  // live actors are the agents whose .loc == this district; their tile (x,y)
  // is held in a small parallel array, not in the District struct:
  uint8_t actor_xy[LOCAL_ACTORS][2];   // LOCAL_ACTORS ≈ 16 drawn at once
};
LocalMap local;                   // single instance
```
The tile layout is **deterministic from the district's seed + type** (a slum
generates differently from an arcology), so it's stable across visits without
being stored — it regenerates identically on re-entry. POIs are placed as labeled
tiles; the District's abstract `services` bitmask (§2.1) decides which exist. The
`@`'s position lives here; movement is cosmetic animation of the abstract "go to
the chop shop" order (within-district pathfinding, §6).

> **Layering principle (what "tile-aware" means precisely).** The **abstract
> simulation layer** — all ~65 micro-systems (§3) and every agent's
> *decision-making* — never reads a tile coordinate. It works in district IDs and
> POI IDs: the order is "be at POI = chop_shop," never "walk to (x,y)." Only the
> **view layer's single resident `LocalMap`** is tile-aware, and it is *derived
> from* abstract state, not authoritative over it: the generator places the chop
> shop at a tile *because* `services` says the district has one, and pathfinding
> animates the `@` there. The arrival the sim cares about is just *activity-done
> after N ticks*, independent of the literal path. **Delete the renderer and the
> game still simulates identically** — which is exactly why the headless harness
> can run thousands of years with no tile maps at all. Tile coordinates exist only
> locally, ephemerally, and only for the one district a human is watching.

**Total live substrate ≈ 7–9 KB** (≈4–6 KB abstract sim + ≈2.6 KB resident tile
map). Tables (commodities, recipes, jobs, names, district templates, tile
palettes, narrator grammar) live in **flash**. Against ~247 KB free heap this is
a rounding error — confirming system *count* is not the constraint.

---

## 3. The micro-system catalog (~65 systems)

Each system is a small update function over the shared substrate. Most are 5–20
lines. They are grouped only for readability — at runtime they form one
interacting web. The right-hand columns are the **interaction matrix**: what each
system reads and writes. Chains emerge wherever one system *writes* a field
another system *reads*.

### A. Need / pressure (per agent) — the universal pressure engine
| # | System | Reads | Writes |
|---|---|---|---|
| 1 | Hunger decay | tick | need.hunger↑ |
| 2 | Thirst decay | tick, hazard.weather | need.thirst↑ |
| 3 | Fatigue / sleep | activity | need.fatigue↑ |
| 4 | Social decay | rel | need.social↑ |
| 5 | Safety / stress | district.danger, scar | need.safety↓, need.stress↑ |
| 6 | Cyberware stability | inv(implants), time | need.cyberware↑ (maintenance debt) |
| 7 | Addiction / tolerance | inv(chems), use | need.addiction↑ |
| 8 | Burnout | activity(overwork) | need.stress↑, skill XP gain↓ |
| 9 | Cyberpsychosis | implant load vs humanity | mood↓ (→ strange-mood analogue) |
| 10 | Injury recovery | status.injured, clinic access | status, mood |
| 11 | Mood aggregation | all needs | mood (feeds behavior systems) |

### B. Economy (per node / global)
| # | System | Reads | Writes |
|---|---|---|---|
| 12 | Commodity regeneration | district.type, services | supply[]↑ (slow) |
| 13 | Demand | population, prosperity, needs | (derived) demand[] |
| 14 | Price formation | supply, demand | price[] (commodity, node) |
| 15 | Wage market | employer demand, labor pool | wage rate |
| 16 | Black-market premium | legality, scarcity, faction | price[] (illicit goods) |
| 17 | Rent / cost-of-living | prosperity, owner faction | agent.money drain |
| 18 | Debt & interest | status.in_debt | money↓, status |
| 19 | Hiring demand | company tiers, prosperity | posts job events |
| 20 | Company payroll/upkeep | Company.employees, assets | treasury↓ |

### C. Territory / faction
| # | System | Reads | Writes |
|---|---|---|---|
| 21 | Influence spread/contract | influence[], treasury | influence[] (node→neighbor) |
| 22 | Patrol generation | influence, grudge | spawns patrol agent/event |
| 23 | Raid generation | border tension (Δinfluence) | hazard.alarm↑, injuries |
| 24 | Turf-war ignition | overlapping influence + grudge | district.owner flip, danger↑ |
| 25 | Grudge drift | recent events | faction.grudge[] |
| 26 | Refugee flow | district.danger, eviction | agent.loc migration |
| 27 | Extortion / rackets | gang influence | vendor money↓, prices↑ |

### D. Hazard propagation
| # | System | Reads | Writes |
|---|---|---|---|
| 28 | Contamination spread | hazard.toxin, adj | neighbor hazard.toxin↑ |
| 29 | Blackout propagation | infrastructure, grid load | hazard.blackout, services↓ |
| 30 | Alarm / heat spread | crime events | hazard.alarm, draws security |
| 31 | Disease spread | population crowding | status, mood↓ |
| 32 | Structural collapse | derelict + hazard | district destruction event |
| 33 | Environmental drift | clock (heatwave/rain/season) | amplifies thirst, flooding |
| 34 | Drone activity | faction tech, alarm | roaming threat agents |

### E. Decay / scarcity cycle
| # | System | Reads | Writes |
|---|---|---|---|
| 35 | Resource depletion | harvest activity | supply[]↓ |
| 36 | Equipment decay | inv, use | inv quality↓ |
| 37 | Implant denaturing | cheap-implant flag, heat | seizure events |
| 38 | Perishable spoilage | inv(food), time | inv↓ |
| 39 | Drug-batch instability | chem production quality | bad-batch hazard events |

### F. Agent behavior (rule collision — where stories happen)
| # | System | Reads | Writes |
|---|---|---|---|
| 40 | Memory scars | injuries, deaths, finds | scar[] (bias future choices) |
| 41 | Revenge arc | grudge, scar | activity → seek target |
| 42 | Theft when desperate | needs unmet + low money | steal event, grudge↑ |
| 43 | Panic / flee | need.safety low | agent.loc change |
| 44 | Migration on scarcity | chronic unmet needs | agent.loc change |
| 45 | Job-seeking | unemployed + low money | takes posted job |
| 46 | Opportunity-seeking | skill + bounty posted | pursues bounty |
| 47 | Random faults | small chance | dropped item, mistake, accident |

### G. Opportunity pulses (timers)
| # | System | Reads | Writes |
|---|---|---|---|
| 48 | Market day / festival | clock | demand spike, social relief |
| 49 | Corp bounty posting | faction grudge, data caches | bounty events |
| 50 | Recruiter offers | displaced/skilled agents | faction recruitment |
| 51 | Patrol sweep | clock, heat | temporary crackdown |
| 52 | Rumor spawning | events (some false) | info propagation across agents |
| 53 | Predator / drone hunt | drone activity | threat pulse |

### H. Progression (player + NPC)
| # | System | Reads | Writes |
|---|---|---|---|
| 54 | Skill XP by doing | activity, success | skill[job]↑ |
| 55 | Mentorship | nearby high-skill agent | skill XP gain↑ |
| 56 | Tier unlocks | skill thresholds | unlocks recipes/safer/faster (§4) |
| 57 | Reputation | completed contracts | Company.reputation, faction standing |
| 58 | Company growth | treasury, assets, rep | Company.tier transitions |

### J. Combat & threats (meatspace — see §5.1)
| # | System | Reads | Writes |
|---|---|---|---|
| 61 | Combat resolution | atk/def (skill, inv weapons/armor/implants, mood, injury) | injury/death, loot transfer, danger/heat, scar, grudge |
| 62 | Threat spawn | district danger, faction tech, alarm | seeds drones / megathreats into `threats[]` |
| 63 | Faction warfare | `#23` raid / `#24` turf-war triggers | resolves combat between faction war-parties |
| 64 | Bounty & retaliation | grudge (`#25`), revenge arc (`#41`) | spawns a hunter who seeks + fights the target |
| 65 | Megathreat behavior | `threats[]`, behavior | roam/occupy/attack; driven off → loot + legend + `#26` refugees |

### I. Surfacing
| # | System | Reads | Writes |
|---|---|---|---|
| 59 | Micro-quest generator | needs + environment | emits goals for player & NPCs |
| 60 | Arc narrator | event stream | Gibson-voice chronicle lines (§7) |

That's **65** — many are one-liners. We can ship a first cut at ~40 and grow the
rest, but the architecture (shared substrate + scheduler) treats them uniformly.

### 3.1 Worked chains (these must *emerge*, not be scripted)
The doc's own examples, annotated with the systems that fire each step — these
are the harness's acceptance targets in [Phase 5](#phasing):

**Black-clinic debt cascade:** `#16 black-market premium`↑ → `#6 cyberware`
maintenance debt unmet → `#35 depletion` of clinic biogel → `#40 scar` (avoid
old clinic) → `#21 influence` gang expands into safe clinic → `#30 alarm` →
clinic lockdown → `#48 pulse` illicit-biogel market opens → **decision spike**:
buy illegal / enter gang turf / self-patch / hack the lockdown (→ CyberHack) /
steal stash.

**Megacorp water-ration riot:** `#21 influence` corp throttles a slum's water →
`#2 thirst`↑ → `#33 drift` heatwave → crowding `#31 disease` → `#27 extortion`
gang "protects" water → `#23 raid`/`#30 alarm` → `#52 rumor` of a hidden cistern
→ `#43/#44 flee/migrate` or riot → district `#24 turf flip`.

---

## 4. Job & career progression tree (the spine)

11 professions (from the design doc), each a **4-tier curriculum** Novice →
Skilled → Expert → Master, gated by `skill[job]` XP (`#54–56`). Tiers unlock
recipes in the crafting tree, reduce failure/injury, and speed production. This
is the explicit career path the protagonist works toward.  Entities can have multiple jobs and each progression path is tracked.

| Job | Novice | Skilled | Expert | Master |
|---|---|---|---|---|
| Construction Tech | mix concrete, basic walls | reinforced structures, ductwork | multi-level architecture | arcology retrofits |
| Machinist | cutting, basic metalwork | servos, pistons, gears | precision barrels | rail components |
| Electronics Tech | soldering, repair | logic boards, sensors | regulators, wireless | cyberdeck internals |
| Biotech Engineer | tissue handling | biografts, organ prep | neural pads | vat organs, combat implants |
| Ripperdoc | trauma care | implant install | neural surgery | experimental augmentation |
| Gunsmith | cleaning, repair | pistol assembly | monoblades, custom barrels | smartgun/rail integration |
| Armor Tech | kevlar stitching | armor panels | tactical armor | reactive/stealth fabric |
| Chemtech | solvent mixing | stims, painkillers | medkits, repair foam | combat drug cocktails |
| Decker | basic encryption | data shards, drives | intrusion scripts | deck optimization, black-market data |
| Drone Tech | battery swaps | actuators, frames | drone weapons | autonomous systems |
| Infrastructure Eng | wiring, pipes | power nodes, pumps | grids, ventilation | automated security grids |

**Crafting tree** = the TIER 0→3 recipe tables from the design doc, encoded as
flash data (`{inputs[], output, job, min_tier}`). Crafting is abstract: consume
input item-class counts, produce output, gated by the maker's skill tier.
**Cross-discipline synergy** (e.g. cyberdeck needs Electronics + Decker; reactive
armor needs Armor + Electronics) is enforced by recipes requiring multiple
trained roles — early on that means *hiring* NPCs with those skills, which is the
engine of company growth.

**Company-tier ladder** (`#58`), the macro-goal:

| Tier | Unlock condition (tunable) | What changes |
|---|---|---|
| SOLO | start | just the protagonist; hourly-wage survival |
| CREW | treasury + 1 owned facility | hire ≤3; take small contracts |
| OUTFIT | rep + multi-facility | hire ≤8; faction deals; bounties |
| CORP | treasury millions + territory influence | own districts; influence field of your own |
| MEGACORP | billions + dominant influence | endgame sandbox; you *are* a faction |

The career path is therefore: learn a novice trade → earn wage + XP → craft/sell
→ buy a facility → hire skilled NPCs → unlock cross-discipline advanced goods →
take faction contracts → acquire territory influence → become a faction.

### 4.1 Standing orders — the directives menu (stub; built in Phase 3)

The agency model (§1) lets the avatar self-drive, but it needs to know *toward
what*. **Standing orders** are the player's incremental goals that bias the
avatar's autonomous choices between interrupts — they're the steering wheel for a
character who mostly drives itself. Set and adjusted any time via a **Directives
menu** (pause → modal), persisted in the save.

Two horizons:
- **Ambition (one active):** the long arc — e.g. *"reach Decker mastery"*, *"hit
  CORP tier"*, *"amass $1 M"*, *"control the Neon Market"*. Drives which jobs,
  facilities, and contracts the avatar gravitates to.
- **Priorities (a few toggles/weights):** near-term policy — *prioritize cash vs.
  XP*, *risk tolerance (cautious↔reckless)*, *avoid gang turf*, *keep cyberware
  maintained*, *save toward <next purchase>*, *never jack in below N buffer*.
  These shape activity selection, spend-vs-save, and the escalation threshold.

Mechanically these are just inputs to the avatar's `DecisionPolicy` (the same seam
NPCs use), so "set a goal" = nudge weights the policy already reads — no special-
case AI. When an ambition **completes**, the engine surfaces a decision spike
inviting the player to set the next one (a natural incremental-goal loop, and a
satisfying beat). A goal that becomes **impossible** (target faction wiped, trade
obsoleted) likewise surfaces for re-targeting.

> Phase-3 scope: the directive data model + how each toggle maps into the policy's
> activity/job/spend weighting; the menu UI; goal-complete / goal-impossible
> surfacing. The headless harness drives directives programmatically to verify an
> auto-career *pursues* a set ambition (and that different ambitions yield
> different career traces).

---

## 5. Combat — two arenas

Violence is not optional in this world: gangers, muggers, faction raids, security
drones, and "rogue AIs in robot bodies" all fight. Combat happens in **two
arenas**, each with its own resolver but one shared consequence layer:

- **Meatspace (§5.1)** — street fights, raids, drones, megathreats. Resolved by
  *this* engine, abstract and stat-based.
- **Cyberspace (§5.2)** — jacking a hostile data target. Deferred to a headless
  **CyberHack** run; no new combat code.

Both obey the same two rules already established: combat is **auto-resolved and
watched** (watch-it-crawl), and the **agency model (§1)** decides when it
interrupts you — a fight the avatar can clearly handle is resolved on its own as a
ticker line; a life-risking one stops the crawl for your call. And both feed the
*same* consequence layer: injury/death, grudges (`#25`), revenge arcs (`#41`),
memory scars (`#40`), district danger/heat (`#30`), loot/economy, refugees
(`#26`), and reputation (`#57`). Combat is woven into the emergence web, not a
side minigame.

### 5.1 In-world (meatspace) combat

**Abstract & stat-based — mirrors CyberHack's ethos, no tiles.** Each combatant's
power is *derived* from existing substrate (no heavy new state), like CyberHack's
`player_power`/`threat_level`:
```
atk = base + combat_skill + weapon_grade(inv[IT_WEAPONS]) + aggression/k
            + implant_bonus(inv[IT_IMPLANTS]) + mood_factor
def = base + armor_grade(inv[IT_ARMOR]) + combat_skill/2
            + implant_bonus - injury_penalty(status & AF_INJURED)
```
`combat_skill` is derived (aggression trait + weapon inventory + a generic
proficiency that grows by surviving fights), so we don't bloat `skill[J_COUNT]`.
**The crafting tree pays off here:** weapons/armor/implants from §4 directly raise
atk/def, giving a Gunsmith/Armor-Tech/Ripperdoc career real combat weight — and
implants trade power for cyberpsychosis (`#9`), a risk/reward dial.

**Resolution** is a short auto-exchange (a single opposed roll for cheap NPC-vs-
NPC; a few animated rounds when the avatar is involved and it's worth watching).
Outcomes: decisive win → loser killed or routed (flees), winner loots (inv
transfer); close → both `AF_INJURED`, one flees; loss → injury or death. Every
result writes the shared consequence layer above.

**Avatar combat & the flatline spike.** Per §1: a fight the avatar's policy is
confident in resolves autonomously (ticker line); a life-risking one **interrupts**
with a combat decision spike — *Fight / Flee / Use item (medkit·stim·weapon) /
Negotiate / Call allies*. And the **do-or-die "flatline" beat is reused verbatim
from CyberHack**: when a blow would drop the avatar, pause *once* for a survival
choice (burn a medkit, desperate swing, flee at a cost) — same pattern as
`DK_FLATLINE`.

**Threats array (megabeasts included).** A small fixed array carries non-agent
combatants so we don't inflate the agent budget:
```c
#define MAX_THREATS 8
struct Threat {                  // ~6 B; drones + megathreats
  uint8_t kind;                  // FERAL_DRONE, SEC_DRONE, KILLDRONE_SWARM,
                                 // CONSTRUCT_MECH, SEWER_LEVIATHAN, ROGUE_AI_BODY,
                                 // MUTANT_COLONY, BLACKOPS_TEAM
  uint8_t power;                 // 1..10 (DF megabeast scale)
  uint8_t district;
  uint8_t behavior;              // AGGRESSIVE / TERRITORIAL / DORMANT
  uint8_t hp;
};
Threat threats[MAX_THREATS];
```
**Megathreats are the DF megabeasts.** A rogue construction mech or a kill-drone
swarm occupies a district, mauls agents and factions, spikes danger/heat, drives
**refugee flows (`#26`)**, and *can only be driven off by combat* — player
heroics, hired muscle, or a faction war-party. Surviving one is a reputation and
**legends** beat (reuse CyberHack's legends ethos: a slain megabeast becomes a
street-legend chronicle line), and it drops rare tech/implants/data → an
**opportunity pulse**. This is the watch-it-crawl spectacle the design wants: you
can watch a leviathan tear through the undercity and decide whether to run, profit,
or be a hero.

### 5.2 Cyberspace combat — the CyberHack bridge

CyberHack's engine already runs headless with personality AI and returns
shards/death/relationship deltas. We call it directly — no new combat code.

**When any agent with a deck jacks into a data target:**
```c
// choose the decker personality from the agent's live situation
uint8_t persona =
    (agent.status & IN_DEBT || needs_desperate(agent)) ? P_RECKLESS :
    (agent.money > comfortable_threshold)              ? P_CAUTIOUS :
    (agent.personality & TRAIT_GREED)                  ? P_OPPORTUNIST :
                                                         P_LOYALIST;

cyber::Sim run;
run.start(target_citynet_seed,        // persistent per data-target (matrix remembers)
          world_rng.next(),           // run seed
          persona,
          &legends_for_target);       // prior legends for this target
run.set_auto_combat(true);
cyber::Rng pr; pr.seed(world_rng.next());
while (run.running()) {
    if (run.advance() == AR_DECISION)
        run.choose(cyber::ai_decide(persona, run.state(), run.decision(), pr));
}
run.update_legends(legends_for_target);   // fold results back into the world
const cyber::RunState& r = run.state();
```

**Feed-back mapping:**
| CyberHack result | Midnight City effect |
|---|---|
| `r.shards` (extracted) | → agent.money / company treasury |
| `r.outcome == O_DIED` (flatline) | agent injured or killed; `#9 cyberpsychosis` hit; scar |
| `update_legends` grudge[] | → faction `#25 grudge drift` toward the target's owner |
| named allies (NS_ALLIED) | → persistent contacts / future contract access |
| corp data extracted | → spawns `#49 bounty` heat or `#16 black-market data` to sell |

A headless run is just sim-steps (no rendering, no sleeps) → completes in a few
ms; NPC jack-ins are invisible background events. **The protagonist's own
jack-ins may optionally be played live** — same app, flip CyberHack to interactive
and hand control over, then return the result to Midnight City. (Decision left to
playtest: always-live vs. offer-auto for the player.)

---

## 6. UI / rendering (CYD 53×20, embark view)

The **embark map is the persistent backdrop**; menus open as **modal overlays on
top of it** (exactly DF's model — you watch the map, menus pop over it). This
still honors the firmware UI law (one active app; one list shown at a time, as an
overlay) and the locked "no map cursor, navigate via menu" rule.

```
 row 0       STATUS: name · ♥intg · needs glyphs · $money · [CORP] · Day 14 03:00
 row 1       ────────────────────────────────────────────────────────
 rows 2–17   EMBARK VIEW — DF-classic ASCII tile map of the CURRENT district.
             53×16 camera window onto the 64×40 LocalMap (§2.7), scrolls to
             keep the @ centered. Walls/floor/ducts/hazard glyphs; POIs as
             letters; '@' = you; other glyphs = co-located NPCs walking their
             own activities. Hazard tiles tinted (toxin/alarm/blackout).
 row 18      EVENT TICKER — newest arc/chronicle line (Gibson voice)
 row 19      footer: [m]enu  [space]pause  [>]speed  ...
```

**Movement is menu-driven, watched not steered.** Pressing `m` opens the action
menu as a modal overlay (Travel / Jobs / Market / People here / Craft / Company /
Self / Jack in). Choosing a destination POI sets the `@`'s order; the engine
**pathfinds within the LocalMap** (small BFS/A* on the ≤64×40 grid) and the `@`
animates tile-by-tile to it at crawl speed — you see it walk, DF-style, then the
action's modal opens on arrival. Travel to another district fades the LocalMap,
does the abstract graph hop, and realizes the destination's tile map.

**NPCs walk the same map as the `@`.** Every agent whose `.loc` is the current
district (≤16 drawn) gets a tile position in `actor_xy` and moves each tick toward
a tile implied by its abstract `activity` — an NPC "working" drifts to a workshop
tile, "shopping" to the market, "fleeing" to an exit, "seeking you" toward the
`@`. Their *real* behavior is still decided by the abstract systems (§3.F); the
local walk just visualizes it. Agents in other districts have **no tile cost at
all** — they exist only abstractly until you enter their district (or they enter
yours), at which point they're assigned an entry tile.

**Collision triggers interaction — routed through the agency model (§1).** When
an NPC's tile becomes adjacent to (or shares) the `@`'s tile, it raises an
`Event`; *which* interaction it is comes from **abstract state**, not the collision
(the collision is only the trigger). The avatar then applies the §1 rule — resolve
it autonomously if it can, otherwise **interrupt**:
- hostile faction / active grudge / revenge target (`#25`,`#41`) → above the
  threshold (life-risking) → **interrupts**: confront / flee / pay off;
- desperate theft-flagged agent (`#42`) demanding money from a richer `@` →
  **interrupts**: comply / fight / run;
- a contact/ally/recruiter (`#50`, CyberHack `NS_ALLIED`) offering a contract or
  faction switch → identity/resource-defining → **interrupts**;
- a fair trade or free mentor training (`#55`) the avatar's policy clearly
  accepts → handled autonomously → **ticker line, no interrupt**;
- a passing acquaintance / trivial bump → flavor line, no interrupt.

So bumping into someone in the alley *means* something — but whether it stops you
depends on whether your avatar could have made that call itself. The systems
already track the relationships that turn a hallway encounter into a story beat;
proximity checks run only over the ≤16 co-located actors, so the cost is trivial.

**Deterministic generation.** `LocalMap` is built from `district.seed + type` so
re-entering a district reproduces the same layout without storing it; the
abstract `services` bitmask decides which POIs are placed.

During auto-advance / `@`-walk animation, pump `keep_awake` each step (CyberHack's
documented gotcha) so the CYD backlight doesn't blank mid-crawl.

---

## 7. Chronicle / arc narrator

Reuse CyberHack's pattern: a template-grammar + weighted-lexicon narrator (no
LLM, deterministic from the world RNG) watching the `#60` event stream. It
detects *arcs* (a chain of related events on a node/agent within a window) and
emits Gibson-voice lines into the ticker and a per-day log on SD. Hard rule
(inherited): original pastiche only, never embedded source text.

**Generative, not enumerated — this is the whole point.** Because ~65 systems
share one substrate, the set of *possible* event chains is combinatorial: there
are effectively **n arcs**, not a fixed menu. The §3.1 examples (debt cascade,
water riot) are *illustrative samples* of that space, never a closed list. So the
narrator must be **generative**, in two layers:

1. **General arc engine (the long tail).** Detect *any* causally-linked run of
   events on the same node/agent within a window — "this led to this led to
   this," whatever its shape — and narrate it from the *event types and actors
   involved*. A never-before-seen chain still becomes a story beat. This is the
   default and it has no enumerated list.
2. **Recognized motifs (richer phrasing).** A small, growing set of named
   patterns (the §3.1 archetypes + others) sit *on top* and, when matched, swap
   in bespoke, higher-flavor phrasing. They're recognizers for color, not the
   source of arcs.

> **Note on the Phase 5 harness.** The Phase 5 `ArcDetector` recognizes 4 named
> archetypes *only because the gate had to verify those specific examples fire* —
> it is a counter, not the generator. The world already emits n arcs today; the
> detector just names four. Phase 7 ports detection into the engine and inverts
> the emphasis: a general "linked-events ⇒ narrate" engine first, named motifs as
> an optional flavor layer.

---

## 8. Save format

Versioned blob to SD (`/midnight/world.sav`), tiny resume key in NVS. Sections:
`header{version, world_seed, tick, company}` · `districts[]` · `agents[]` ·
`factions[]` · `events[]` · `legends[]` (per hacked target) · `rng_state`. All
fixed-width — **single-digit KB**. Autosave on each in-game day boundary and on
app pause (matches the existing `persist::Store` + `fs` checkpoint pattern). A
menu option wipes and regenerates the world.

---

## 9. Engine architecture & files

Mirrors CyberHack exactly so it shares the proven pipeline:

- `emulator/src/core/midnight_world.{h,cpp}` — pure deterministic engine, GLOB'd
  into both the emulator and firmware (one source of truth). Uses `cyber::Rng`.
- **`DecisionPolicy` seam** — player decisions surface as `AR_DECISION` (like
  CyberHack); NPC AI is internal rule systems. An `AiPolicy` can also drive the
  *protagonist* headless so the balance harness can simulate entire careers.
- `emulator/sim/midnight_sim.cpp` + a `make` target (NOT compiled into firmware)
  — the headless emergence + balance harness.
- `emulator/src/apps/midnight.cpp` — the renderer app (embark view + camera +
  modal menus); registered "midnight" in `host/main.cpp` and
  `firmware/main/main.cpp`. The `LocalMap` generator + within-district pathfinding
  live in the portable core (deterministic, harness-testable), not the renderer.
- Reuse: `keep_awake`, `persist::Store`, `fs`, the Gibson chronicle grammar.
- Keep it warning-clean (firmware is `-Werror`: misleading-indentation, format).

---

## 10. Budget

- **Flash:** app .bin is ~1.19 MB today with ~1.8 MB of the 3 MB partition free.
  A second large engine + tables fits comfortably.
- **RAM (the real constraint):** ~247 KB free heap, ~46 K floor under a Wiki
  search. Midnight City's live substrate is ~7–9 KB (§2: ~4–6 KB abstract sim +
  ~2.6 KB the single resident tile map; only ONE district is ever realized).
  Narrator + render buffers dominate and stay small. **System count is free**
  (rules are flash); **the embark view costs one tile map, not 32.**
  Non-negotiables, same as the firmware's standing RAM rules: fixed-size arrays,
  no per-tick allocation, IDs + flash string pools instead of `std::string`,
  stream the day-log/chronicle to SD.
- **CPU:** ~65 systems × (≤40 agents + ≤32 nodes + ≤8 threats) per full sweep ≈ a
  few thousand ops; at watch-it-crawl tick rate on a 240 MHz S3 it's trivial. The
  scheduler
  (§11) still staggers systems to keep any single tick cheap.

---

## 11. Scheduler

Not every system every tick — a tiered, round-robin scheduler bounds per-tick
cost and gives systems their natural cadence:

- **Per tick (hour):** protagonist activity, needs decay, hazard propagation,
  event surfacing, decision check.
- **Every K ticks:** economy (prices/wages), influence fields, agent behavior
  decisions — agents processed in **round-robin slices** so no tick touches all 40.
- **Per day:** faction drift, opportunity pulses, world drift, company-tier
  checks, autosave, chronicle flush.

---

## 12. Test-gated phase plan

Same discipline as CyberHack — balance the engine headless over thousands of
seeded worlds *before* any rendering. Each phase is gated by the harness.

| Phase | Deliverable | Gate |
|---|---|---|
| 0 | This spec | — |
| 1 | Substrate + worldgen (districts/agents/factions) | serialize/deserialize round-trips; deterministic per seed |
| 2 | Economy + needs + basic agent behavior | 10 k-tick soak: no runaway inflation, no extinction, no deadlock |
| 3 | Job/skill/career tree + crafting + company ladder + **standing-orders directives** (§4.1) | headless auto-career sometimes reaches MEGACORP, sometimes stalls/dies (no dominant line); a set ambition demonstrably steers the auto-career, different ambitions → different traces |
| 4 | Territory + hazards + decay + pulses + **combat (§5.1: resolver, threats/megathreats, flatline spike)** | full interaction matrix wired; stable under soak; raids/turf-wars resolve via combat; a megathreat can appear, terrorize, and be driven off; life-risking avatar fights interrupt while routine ones auto-resolve; combat deaths feed grudges/scars/refugees |
| 5 | **Emergence harness** | the named chain classes (§3.1) actually *fire* from base rules; tune frequency |
| 6 | CyberHack bridge + feedback | headless jack-ins resolve & feed back; optional live player handoff |
| 7 | Arc narrator (Gibson voice) — **generative** (general linked-events engine + named motifs as a flavor layer, §7) | arbitrary arcs detected & narrated deterministically; named motifs get bespoke phrasing |
| 8a | `LocalMap` gen + within-district pathfinding (portable core) | deterministic per district seed; `@` reaches any POI; one resident map |
| 8b | Renderer app — embark view, camera-follows-`@`, modal menus | engine/UI parity (scripted decisions → identical state headless vs UI) |
| 9 | Device flash + soak | heap stable over uptime on the no-PSRAM part |

---

## 13. Honest risks

1. **Tuning 65 interacting systems** is the dominant effort and where the game
   lives or dies — runaway loops, deadlocks, or boring equilibria. The headless
   emergence harness (Phase 5) is the mitigation and the bulk of the work.
2. **Legibility** — the player must *perceive* the arc, not just watch numbers
   drift. The narrator + good decision-spike surfacing carry this.
3. **Save versioning** for an indefinitely-persistent world.
4. **Heap over long uptime** — the firmware's known no-PSRAM fragmentation watch;
   the fixed-array discipline is what keeps this game from being the thing that
   tips it over.

None of these is a hardware wall. This is a big, tuning-heavy build in exactly
the shape we've already shipped twice (CyberHack + the app suite).
```
