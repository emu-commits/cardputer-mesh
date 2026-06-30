// Midnight City — the engine substrate (Phase 1). A pure, deterministic,
// zero-dependency model of the city: a graph of districts, a population of
// agents, factions, and the player's company — plus worldgen and a byte-exact
// save format. Depends only on the C++ stdlib so it can be (a) GLOB'd into the
// firmware, (b) compiled into the emulator app, and (c) driven headless by
// sim/midnight_sim.cpp for property/determinism tests.
//
// Architectural keystone (docs/MIDNIGHT_CITY.md): this is the ABSTRACT
// simulation layer. It works in district IDs and POI/service IDs, never tile
// coordinates — the embark tile view (Phase 8) is a separate, derived layer.
// All randomness flows through one Rng; the world is reproducible from a seed.
//
// Phase 1 scope: structs + worldgen + serialize/deserialize round-trip. The ~60
// micro-systems (Phase 2+), career/directives (Phase 3), CyberHack bridge
// (Phase 6) and the local tile map (Phase 8) are NOT here yet.
#pragma once
#include <cstdint>
#include <string>
#include "core/cyberhack_world.h"   // cyberspace = a headless CyberHack run (§5.2)

namespace mid {

// ---- fixed budgets (all freed on app-close; nothing in steady-state heap) ---
static constexpr int MAX_DISTRICTS = 32;
static constexpr int MAX_AGENTS    = 40;   // agents[0] is always the protagonist
static constexpr int MAXDEG        = 5;    // graph adjacency per district
static constexpr int RELMAX        = 4;    // sparse top-K relationships per agent
static constexpr int SCARMAX       = 3;    // memory scars per agent
static constexpr int EMPMAX        = 8;    // company employees (early cap)
static constexpr int EVMAX         = 32;   // bounded event ring
static constexpr int MAX_THREATS   = 8;    // drones + megathreats (§5.1)
static constexpr int MAX_NET       = 6;    // remembered cyberspace targets (§5.2)
static constexpr uint8_t NONE8     = 0xFF;

static constexpr uint16_t MID_MAGIC   = 0x434D; // 'M','C' little-endian
static constexpr uint8_t  MID_VERSION = 1;

// ---- enums -----------------------------------------------------------------
enum Faction : uint8_t {
    F_CREW, F_MEGACORP, F_SYNDICATE, F_NOMAD, F_MUTANT, F_CULT, F_COUNT
};
enum DistrictType : uint8_t {
    DT_SLUM, DT_ARCOLOGY, DT_MARKET, DT_INDUSTRIAL, DT_UNDERCITY, DT_DATACENTER,
    DT_TOXIC, DT_LOGISTICS, DT_ROOFTOP_FARM, DT_METRO, DT_COUNT
};
enum Commodity : uint8_t {
    C_FOOD, C_WATER, C_SCRAP, C_ELECTRONICS, C_BIOTECH, C_CHEMS, C_DATA, C_WEAPONS, C_COUNT
};
enum Hazard : uint8_t {
    HZ_TOXIN, HZ_BLACKOUT, HZ_ALARM, HZ_DISEASE, HZ_COLLAPSE, HZ_COUNT
};
enum Need : uint8_t {
    ND_HUNGER, ND_THIRST, ND_FATIGUE, ND_SOCIAL, ND_SAFETY,
    ND_CYBERWARE, ND_ADDICTION, ND_STRESS, ND_COUNT
};
// J_NONE = unemployed; the 11 professions follow (docs/MIDNIGHT_CITY.md §4).
enum Job : uint8_t {
    J_NONE, J_CONSTRUCTION, J_MACHINIST, J_ELECTRONICS, J_BIOTECH, J_RIPPERDOC,
    J_GUNSMITH, J_ARMOR, J_CHEMTECH, J_DECKER, J_DRONE, J_INFRA, J_COUNT
};
enum ItemClass : uint8_t {
    IT_MATERIALS, IT_COMPONENTS, IT_WEAPONS, IT_ARMOR, IT_IMPLANTS,
    IT_CHEMS, IT_DATA, IT_FOOD, IT_COUNT
};
enum CompanyTier : uint8_t {
    CT_SOLO, CT_CREW, CT_OUTFIT, CT_CORP, CT_MEGACORP, CT_COUNT
};
// packed personality: each trait 0..255, biases the DecisionPolicy (Phase 3)
enum Trait : uint8_t { TR_GREED, TR_CAUTION, TR_LOYALTY, TR_AGGRESSION, TR_COUNT };

// who walks the sprawl. Newcomer composition is driven by world CONDITIONS, not a
// fixed clock: automation pressure (corp/cult/datacenters/industrial/rogue-AI) draws
// synths; mutation pressure (toxic zones/undercity/contamination/mutant gangs) draws
// mutants; a quiet world stays human. So a long game may end up a synth city, a
// mutant city, or stay human — none guaranteed; none counted out. (#26 inflow side.)
enum AgentKind : uint8_t { AK_HUMAN, AK_SYNTH, AK_MUTANT, AK_COUNT };

// skill curriculum tiers (docs/MIDNIGHT_CITY.md §4): Novice->Skilled->Expert->Master
enum SkillTier : uint8_t { ST_NOVICE, ST_SKILLED, ST_EXPERT, ST_MASTER, ST_COUNT };

// standing-orders ambitions (§4.1) — the avatar's long arc
enum Ambition : uint8_t { AMB_SURVIVE, AMB_WEALTH, AMB_MASTERY, AMB_TERRITORY, AMB_COUNT };

// in-world threats (§5.1): drones + DF-megabeast-scale megathreats
enum ThreatKind : uint8_t {
    TK_FERAL_DRONE, TK_SEC_DRONE, TK_KILLDRONE_SWARM, TK_CONSTRUCT_MECH,
    TK_SEWER_LEVIATHAN, TK_ROGUE_AI_BODY, TK_MUTANT_COLONY, TK_BLACKOPS_TEAM, TK_COUNT
};
enum ThreatBehavior : uint8_t { TB_DORMANT, TB_TERRITORIAL, TB_AGGRESSIVE };

// event-ring kinds — drive surfacing (§6) + the Gibson narrator (§7)
enum EventKind : uint8_t {
    EV_NONE, EV_COMBAT, EV_DEATH, EV_TURF_FLIP, EV_RAID, EV_THREAT_SPAWN,
    EV_THREAT_DEFEAT, EV_REFUGEE, EV_EXTORT, EV_BOUNTY, EV_RECRUIT, EV_MARKET_DAY,
    EV_RUMOR, EV_COLLAPSE, EV_SHORTAGE, EV_HEATWAVE, EV_LOCKDOWN, EV_RIOT,
    EV_JACKIN, EV_HEIST, EV_FLATLINE, EV_NETALLY,
    EV_NEWCOMER, EV_POP_SHIFT, EV_COUNT   // EV_POP_SHIFT.data = the new dominant AgentKind
};

// what an agent did this tick (also drives the embark-view animation, Phase 8)
enum Activity : uint8_t {
    ACT_IDLE, ACT_WORK, ACT_BUY, ACT_MOVE, ACT_REST, ACT_SEEKJOB, ACT_COUNT
};

enum AgentFlags : uint8_t {
    AF_INJURED = 1, AF_ADDICTED = 2, AF_IN_DEBT = 4, AF_EMPLOYED = 8,
    AF_FLEEING = 16, AF_HAS_DECK = 32, AF_PLAYER = 64, AF_ALIVE = 128
};
// which workshops/vendors/boards a district offers (the abstract `services`)
enum Service : uint16_t {
    SV_JOB_BOARD = 1u << 0, SV_MARKET   = 1u << 1, SV_CLINIC    = 1u << 2,
    SV_CHOPSHOP  = 1u << 3, SV_FABLAB   = 1u << 4, SV_BAR       = 1u << 5,
    SV_DATAVAULT = 1u << 6, SV_FENCE    = 1u << 7, SV_DRONEBAY  = 1u << 8,
    SV_CHEMLAB   = 1u << 9, SV_ARMORSHOP= 1u << 10, SV_FORGE    = 1u << 11
};

// ---- RNG (xorshift128) — the one source of determinism (same as CyberHack) --
struct Rng {
    uint32_t s[4] = {0, 0, 0, 0};
    void     seed(uint64_t v);
    uint32_t next();
    uint32_t range(uint32_t n);        // [0,n)
    int      between(int lo, int hi);  // [lo,hi]
    bool     chance(int pct);          // pct in [0,100]
};

// ---- substrate structures (docs/MIDNIGHT_CITY.md §2) -----------------------
struct Relation { uint8_t other = NONE8; int8_t valence = 0; };
struct Scar     { uint8_t node = NONE8; uint8_t kind = 0; int8_t valence = 0; };

struct District {
    uint8_t  type      = DT_SLUM;
    uint8_t  owner     = F_COUNT;          // F_COUNT == neutral/contested
    uint32_t seed      = 0;                // deterministic LocalMap seed (Phase 8)
    uint8_t  influence[F_COUNT] = {0};
    uint8_t  supply[C_COUNT]    = {0};
    uint8_t  hazard[HZ_COUNT]   = {0};
    uint8_t  population = 0;
    uint8_t  prosperity = 0;
    uint8_t  danger     = 0;                // current (spikes with combat/threats)
    uint8_t  danger_base = 0;               // intrinsic level; danger mean-reverts to it
    uint16_t services   = 0;
    uint8_t  deg        = 0;
    uint8_t  adj[MAXDEG] = {NONE8, NONE8, NONE8, NONE8, NONE8};
};

struct Agent {
    uint8_t  name_id  = 0;
    uint8_t  kind     = AK_HUMAN;          // human / synth / mutant
    uint8_t  loc      = 0;
    uint8_t  faction  = F_COUNT;
    uint8_t  trait[TR_COUNT] = {0};
    uint8_t  need[ND_COUNT]  = {0};        // pressure, 0 = satisfied .. 255 = critical
    uint8_t  job      = J_NONE;
    uint8_t  skill[J_COUNT]  = {0};        // XP/tier per profession (sparse in practice)
    uint8_t  mood     = 0;
    uint8_t  status   = 0;                 // AgentFlags bitmask
    uint32_t money    = 0;
    uint8_t  inv[IT_COUNT]   = {0};
    uint8_t  activity = 0;
    Relation rel[RELMAX];
    Scar     scar[SCARMAX];
};

struct FactionState {
    uint8_t  alignment  = 0;
    uint8_t  tech_level = 0;
    uint8_t  specialty  = 0;               // a Commodity it favors
    int8_t   grudge[F_COUNT + 1] = {0};    // toward each faction; [F_COUNT] = toward player
    uint8_t  leader     = NONE8;           // agent id
    uint32_t treasury   = 0;
};

struct Company {
    uint8_t  name_id   = 0;
    uint8_t  tier      = CT_SOLO;
    uint32_t treasury  = 0;
    uint8_t  emp_count = 0;            // abstract headcount (the labor pool); see §4
    uint8_t  employees[EMPMAX] = {NONE8, NONE8, NONE8, NONE8, NONE8, NONE8, NONE8, NONE8};
    uint8_t  asset_count = 0;          // facilities owned across districts
    uint16_t assets    = 0;            // bitmask of facility kinds (flavor)
    uint8_t  reputation = 0;
};

// standing-orders directive (§4.1): the player's incremental goals that bias the
// avatar's self-driving DecisionPolicy. Defaults to "just survive".
struct Directive {
    uint8_t ambition = AMB_SURVIVE;
    uint8_t target   = J_DECKER;       // AMB_MASTERY: which profession to master
    uint8_t risk     = 128;            // 0 cautious .. 255 reckless (spend reserves vs hoard)
    uint8_t thrift   = 128;            // 0 spend-freely .. 255 hoard
};

struct Event {
    uint8_t  kind  = 0;
    uint8_t  node  = NONE8;
    uint8_t  agent = NONE8;
    uint8_t  data  = 0;
    uint16_t tick  = 0;
};

// a non-agent combatant: feral/security drones, kill-drone swarms, and the
// DF-megabeast-scale megathreats (rogue mechs, leviathans, rogue-AI bodies…)
struct Threat {
    uint8_t kind     = 0;     // ThreatKind
    uint8_t power    = 0;     // 1..10 (megabeast scale)
    uint8_t district = NONE8;
    uint8_t behavior = TB_TERRITORIAL;
    uint8_t hp       = 0;     // driven off when it hits 0
    uint8_t active   = 0;
};

struct World {
    uint32_t world_seed = 0;
    uint32_t tick       = 0;
    Rng      rng;                          // live world RNG (state evolves with the sim)
    uint8_t  home       = 0;               // protagonist's start district
    uint8_t  district_count = 0;
    uint8_t  agent_count    = 0;
    District districts[MAX_DISTRICTS];
    Agent    agents[MAX_AGENTS];           // agents[0] = protagonist
    FactionState factions[F_COUNT];
    Company  company;
    Directive directive;                   // the protagonist's standing orders (§4.1)
    uint8_t  weather = 0;                  // heatwave/drought days remaining (#33)
    uint8_t  synth_tide = 0;               // 0..255 automation pressure -> synthetic inflow
    uint8_t  mutant_tide = 0;              // 0..255 mutation pressure  -> mutant inflow
    uint8_t  threat_count = 0;             // active threats in threats[]
    Threat   threats[MAX_THREATS];
    // remembered cyberspace targets — the matrix remembers across jack-ins (§5.2)
    uint8_t  net_count = 0;
    uint8_t  net_target[MAX_NET] = { NONE8, NONE8, NONE8, NONE8, NONE8, NONE8 };
    cyber::Legends net_legends[MAX_NET];
    uint8_t  event_count = 0;              // events held in the ring
    uint8_t  event_head  = 0;              // ring write cursor
    Event    events[EVMAX];
};

// ---- worldgen + queries ----------------------------------------------------
void gen_world(World& w, uint32_t seed);
bool world_connected(const World& w);     // BFS over the district graph
bool districts_adjacent(const World& w, uint8_t a, uint8_t b);
int  district_distance(const World& w, uint8_t from, uint8_t to); // hops, -1 if unreachable

// ---- Phase 2: economy + needs + basic behavior -----------------------------
// Balance knobs (overridable by the harness for sweeps), like CyberHack's Tunables.
struct MidTunables {
    // need decay per tick
    int hunger_rate = 3, thirst_rate = 4, fatigue_work = 5, fatigue_rest = 8;
    int social_rate = 1, stress_relief = 3;
    int buy_thresh   = 120;   // consume a vital when its pressure exceeds this
    int consume_relief = 150; // pressure removed by one purchase
    int consume_supply = 2;   // district supply drawn down per purchase
    // prices: price = base * demand_ref / (supply + 1), clamped
    int base_price[C_COUNT] = { 6, 4, 5, 12, 14, 9, 16, 20 };
    int demand_ref = 31, price_min = 2, price_max = 40;
    // wages + money sinks
    int wage_base = 12, reserve = 60;
    int regen_period = 4, regen_min = 1, regen_max = 4, supply_cap = 100;
    int rent_period = 24, rent_base = 8;
    int starve = 235;         // vital pressure that starts hurting
    // --- Phase 3: careers + crafting + company ---------------------------
    int skill_tier_xp[3] = { 64, 128, 192 };  // novice<64<=skilled<128<=expert<192<=master
    int craft_base = 45;      // production value per tick at Novice; x(tier+1)
    int train_rate = 2;       // extra skill xp/tick when mastering (vs +1 working)
    int found_threshold = 500;// personal money before seeding the company
    int personal_reserve = 120; // cash the avatar keeps before investing surplus
    // company compounding (per in-game day)
    uint32_t tier_thresh[CT_COUNT] = { 0, 2000, 100000, 5000000, 1000000000 };
    int tier_mult[CT_COUNT] = { 1, 2, 5, 15, 40 };
    int emp_cap[CT_COUNT]   = { 1, 4, 12, 30, 80 };
    int per_emp = 55, emp_payroll = 18, asset_upkeep = 25;
    int hire_cost0 = 300, asset_cost0 = 2500, asset_bonus_pct = 12;
    // capital return (basis points/day per tier) — the geometric driver that lets
    // a thriving company compound from thousands toward the billion-shard megacorp.
    int tier_return[CT_COUNT] = { 0, 20, 45, 90, 150 };
    // street incidents (front-loaded danger): muggings scale with district danger
    // + the avatar's risk appetite; lethal only while broke + injured (wealth = safety).
    int incident_div = 7;          // per-day mugging chance = danger / incident_div (%)
    int incident_lethal_pct = 16;  // base lethality vs an injured victim (scaled by risk/tier)
    int safe_money = 120;          // cash above this buys care -> halves lethality
    // --- Phase 4: territory / hazards / combat / threats -----------------
    int weapon_grade = 6, armor_grade = 5, implant_grade = 4; // gear -> atk/def
    int combat_death_pct = 18;     // chance a beaten combatant dies (scaled by margin)
    int influence_decay = 2;       // per-day pull of each district's influence toward neighbors'
    int turf_lead = 15;            // influence lead needed to (re)claim ownership
    int raid_pct = 8;              // per-day chance a contested district sees a raid
    int hazard_spread = 3;         // per-day hazard diffusion to neighbors
    int hazard_decay = 2;          // per-day hazard self-decay
    int danger_decay = 2;          // per-day mean-reversion of district danger
    int refugee_danger = 70;       // district danger that drives residents to flee
    int threat_spawn_pct = 2;      // per-day chance a new threat emerges
    int threat_cap = 3;            // max concurrent active threats
    int threat_hp_mult = 5;        // spawn hp = power * this
    int megathreat_min_power = 5;  // power >= this = a megathreat (DF megabeast)
    // --- Phase 6: cyberspace bridge (§5.2) -------------------------------
    int jack_pct = 2;              // per-day chance a decker jacks a data target
    int flatline_death_pct = 16;   // chance a cyberspace flatline kills in meatspace
    int heist_score = 200;         // shards extracted that count as a "big heist"
    // --- population inflow (#26): newcomers keep the city alive endlessly -----
    int inflow_pct = 30;           // per-day chance to admit a newcomer when below target
    // the synth/mutant tides mean-revert toward condition-derived targets (rise OR
    // fall), so composition is a possibility space, not a fixed march to machines.
};
extern MidTunables g_mtune;

int  price_of(const World& w, uint8_t district, uint8_t commodity);
int  wage_of(const World& w, uint8_t district, const Agent& a);
void tick_world(World& w);                 // advance one tick (deterministic via w.rng)
int  alive_count(const World& w);

// ---- Phase 3 queries -------------------------------------------------------
uint8_t     skill_tier(uint8_t xp);
const char* skill_tier_name(uint8_t t);
const char* ambition_name(uint8_t a);
const char* company_tier_name(uint8_t t);
int         production_value(const World& w, const Agent& a); // value/tick when operating
uint8_t     top_skill_tier(const Agent& a);                   // best tier across professions
void        company_step(World& w);                          // per-day compounding (no-op if empty)
const char* agent_kind_name(uint8_t kind);
int         human_count(const World& w);
int         synth_count(const World& w);

// ---- Phase 4 queries -------------------------------------------------------
int         combat_atk(const World& w, const Agent& a);
int         combat_def(const World& w, const Agent& a);
const char* threat_name(uint8_t kind);
const char* event_name(uint8_t kind);
// would a fight against `foe_power` make the avatar interrupt the player? (agency §1)
bool        avatar_fight_escalates(const World& w, int foe_power);

// ---- Phase 6: cyberspace bridge (§5.2) -------------------------------------
struct JackResult {
    bool     ran      = false;   // did a run happen (agent had a deck + a target)
    uint8_t  target   = NONE8;   // data-target district
    uint32_t shards   = 0;       // extracted -> agent.money
    uint8_t  outcome  = 0;       // cyber::Outcome (O_EXTRACTED / O_DIED)
    uint8_t  corruption = 0;
    bool     flatlined  = false; // cyberspace death (may kill in meatspace)
    bool     killed     = false; // it killed the agent in meatspace
    bool     net_ally   = false; // forged a NS_ALLIED contact in the matrix
};
// run a headless CyberHack dive for agent `ai` and fold the results back into
// the world (shards, flatline injury/cyberpsychosis, faction grudge, events).
JackResult jack_in(World& w, int ai);
int  net_target_count(const World& w);   // # of data targets (datacenters)
const char* outcome_name(uint8_t cyber_outcome);

// ---- Phase 7: generative Gibson-voice narrator (§7) ------------------------
// Generative, not enumerated: any single event narrates (the long tail); a
// growing set of named motifs get richer bespoke phrasing on top. No LLM —
// template grammar + lexicons, deterministic from the world seed + event.
enum ArcKind : uint8_t {
    MA_NONE, MA_LINKED, MA_DEBT_CASCADE, MA_WATER_RIOT, MA_MEGA_RAMPAGE,
    MA_FEUD, MA_CYBER_FALLOUT, MA_COUNT
};
const char* arc_name(uint8_t a);
std::string narrate_event(const World& w, const Event& e);             // any event -> a line
std::string narrate_arc(const World& w, uint8_t arc, uint8_t district,
                        uint8_t prev_kind, uint8_t cur_kind);          // a recognized chain -> a line

// transient aggregator (app-owned, NOT serialized; rebuildable from the stream):
// watches events and recognizes arcs — a generic "linked pair" plus named motifs.
struct ArcTracker {
    int shortage_t[MAX_DISTRICTS], gang_t[MAX_DISTRICTS], riot_t[MAX_DISTRICTS];
    int mega_t[MAX_DISTRICTS], death_t[MAX_DISTRICTS], heist_t[MAX_DISTRICTS];
    int last_t[MAX_DISTRICTS];
    uint8_t last_kind[MAX_DISTRICTS];
    int heat_until = 0, window = 720;
    void    reset(int window_ticks);
    uint8_t ingest(uint8_t kind, uint8_t d, uint8_t data, int t);      // -> ArcKind
};

// ---- Phase 8a: realized local embark map (view layer, §2.7) ----------------
// The ONE district the protagonist occupies, realized as a DF-classic tile map
// you watch the @ walk. Deterministic from district.seed + type (regenerable, not
// stored). Tile-aware view layer ONLY — the abstract sim never reads these.
static constexpr int LMAP_W       = 64;
static constexpr int LMAP_H       = 40;
static constexpr int LMAP_POI_MAX = 12;
static constexpr int LMAP_ACTORS  = 16;
enum LocalTile : uint8_t { LT_WALL, LT_FLOOR, LT_DOOR, LT_HAZARD, LT_RUBBLE, LT_TCOUNT };
struct LocalPOI { uint8_t x = 0, y = 0, service_bit = 0; }; // service_bit = index of a Service flag
struct LocalMap {
    uint8_t  tile[LMAP_H][LMAP_W];
    uint8_t  entry_x = 0, entry_y = 0;          // the @ spawn
    uint8_t  poi_count = 0;
    LocalPOI poi[LMAP_POI_MAX];
    uint8_t  actor_count = 0;
    uint8_t  actor_xy[LMAP_ACTORS][2];          // co-located NPC spawn tiles (8b assigns agents)
};
void gen_localmap(LocalMap& m, const World& w, uint8_t district);
bool localmap_walkable(const LocalMap& m, int x, int y);
bool localmap_reachable(const LocalMap& m, int fx, int fy, int tx, int ty);
// first step from (fx,fy) toward (tx,ty) over walkable tiles; writes *nx,*ny.
// returns false (and leaves *nx,*ny) if there is no path.
bool localmap_step_toward(const LocalMap& m, int fx, int fy, int tx, int ty, int* nx, int* ny);
char localmap_tile_glyph(uint8_t tile);
char localmap_poi_glyph(uint8_t service_bit);
const char* localmap_poi_name(uint8_t service_bit);

// ---- save format (byte-exact, fixed-width) ---------------------------------
void serialize(const World& w, std::string& out);
bool deserialize(const std::string& in, World& w);

// ---- flash string tables (no std::string in the substrate) -----------------
const char* agent_name(uint8_t name_id);
int         agent_name_count();
const char* faction_name(uint8_t f);
const char* district_type_name(uint8_t t);
const char* job_name(uint8_t j);
const char* commodity_name(uint8_t c);
const char* company_name(uint8_t name_id);
int         company_name_count();

} // namespace mid
