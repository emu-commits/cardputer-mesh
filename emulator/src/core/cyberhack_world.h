// CyberHack — the engine. A pure, deterministic, zero-dependency simulation of a
// cyberspace dive: a network graph, a decker's deck, ICE, Heat/Corruption, and a
// run that ends in extraction or death. Depends only on the C++ stdlib so it can
// be (a) GLOB'd into the firmware, (b) compiled into the emulator app, and (c)
// driven headless by sim/cyberhack_sim.cpp for property/balance tests.
//
// Architectural keystone (docs/CYBERHACK.md): all randomness flows through one
// Rng; every choice flows through Sim::advance()/choose(). The UI is just one
// caller of that loop; the AI policy is another. Correctness never depends on a
// screen. Rendering and the cosmetic "node interior" live entirely in the app.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cyber {

// ---- fixed budgets (all freed on app-close; nothing in steady-state heap) ---
static constexpr int MAX_NODES   = 60;
static constexpr int MAX_EDGES   = 140;
static constexpr int MAX_NAMED   = 8;
static constexpr int MAX_BURNED  = 16;
static constexpr int MAX_MARKED  = 16;
static constexpr int EVENT_RING  = 64;
static constexpr int MAX_STEPS   = 4000;  // high safety bound; runs end by jack-out or death
static constexpr uint8_t NONE8   = 0xFF;

// ---- enums -----------------------------------------------------------------
enum Faction : uint8_t { F_KUROGANE, F_VULTURES, F_NULLSIGIL, F_GREYWALL, F_SWITCHBOARD, F_COUNT };
enum NodeType : uint8_t { N_VAULT, N_GANG, N_PUBLIC, N_ABANDONED, N_SHRINE, N_TYPECOUNT };
enum Attitude : uint8_t { A_FRIENDLY, A_NEUTRAL, A_HOSTILE, A_HUNTING };
enum Module   : uint8_t { M_SPIKE, M_MASK, M_FORK, M_PATCH, M_GHOST, M_COUNT };
enum Ice      : uint8_t { I_BLACK, I_TRACE, I_WARDEN, I_SWARM, I_WATCHDOG, I_SYSOP, I_COUNT };
enum Personality : uint8_t { P_RECKLESS, P_CAUTIOUS, P_OPPORTUNIST, P_LOYALIST, P_COUNT };
// the kinds of mind behind a named entity — drives dialogue + which tactic bites.
// PR_MEMORY is special: a dead mind looped in the ICE; you don't fight or bargain
// with it, you dive its memory (a different encounter, same "talk to something" loop).
enum Persona  : uint8_t { PR_AI, PR_SYSOP, PR_DAEMON, PR_CONSTRUCT, PR_MEMORY, PR_COUNT };
enum Outcome  : uint8_t { O_RUNNING, O_EXTRACTED, O_DIED };
enum Death    : uint8_t { D_NONE, D_ICE, D_CORRUPTION, D_HUNTED, D_TRACE, D_TIMEOUT };

// event tags drive the chronicle
enum Tag : uint8_t {
    T_NONE, T_JACKIN, T_FIRST_BLOOD, T_BIG_SCORE, T_CLOSE_CALL, T_REVENGE,
    T_BETRAYAL, T_SACRIFICE, T_HUMILIATION, T_LOCKDOWN, T_BURNED, T_HUNT,
    T_EXTRACT, T_DEATH, T_PARLEY, T_ALLY, T_EXTORT, T_BRIBE, T_MEMORY, T_TAGCOUNT
};

// decision-spike kinds (new kinds appended to keep the wire numbers stable)
enum DecisionKind : uint8_t { DK_ENCOUNTER, DK_BRANCH, DK_FACTION, DK_SURVIVAL, DK_EXTRACT, DK_DIVE, DK_PARLEY, DK_MEMORY };

// NS_ALLIED: an entity you talked down / befriended — it opens gates on later runs
enum NamedStatus : uint8_t { NS_ALIVE, NS_CRIPPLED, NS_DEAD, NS_ALLIED };

// ---- RNG (xorshift128) — the one source of determinism ---------------------
struct Rng {
    uint32_t s[4];
    void seed(uint64_t v);
    uint32_t next();
    uint32_t range(uint32_t n);       // [0,n)
    int      between(int lo, int hi);  // [lo,hi]
    bool     chance(int pct);          // pct in [0,100]
};

// ---- world structures ------------------------------------------------------
enum NodeFlags : uint8_t {
    NF_VISITED   = 1, NF_BURNED_GUARD = 2, NF_MARKED = 4,
    NF_BACKDOOR  = 8, NF_LOOTED = 16, NF_GUARD_DONE = 32, NF_OBJECTIVE = 64,
    NF_PARLEYED  = 128   // a parley was already offered at this node (once per node)
};

struct Node {
    uint8_t faction   = F_COUNT;  // owner (F_COUNT == unowned)
    uint8_t type      = N_PUBLIC;
    uint8_t security  = 1;        // 0..9
    uint8_t flags     = 0;
    uint8_t x = 0, y = 0;         // layout for the graph view
    uint8_t name_pre  = 0;        // index into NODE_PREFIX
    uint8_t guard_ice = I_WATCHDOG;
    uint8_t guard_named = NONE8;  // index into World.named, else NONE8
    uint8_t guard_count = 1;      // ICE to clear here (a node is a few battles, then move on)
    uint8_t deg = 0;
    uint8_t nbr[5] = {NONE8, NONE8, NONE8, NONE8, NONE8};
    int16_t shards  = 0;          // loot payload
};

struct Edge { uint8_t a = NONE8, b = NONE8; uint8_t risk = 1; uint8_t flags = 0; };
enum EdgeFlags : uint8_t { EF_BURNED = 1, EF_LOCKED = 2 };

struct NamedIce {
    uint8_t name_id  = 0;     // index into NAMED_POOL
    uint8_t archetype = I_BLACK;
    uint8_t tier     = 1;
    uint8_t status   = NS_ALIVE;
    uint8_t faction  = F_COUNT;
    int8_t  grudge   = 0;
    uint8_t persona  = PR_AI;     // kind of mind — drives dialogue + negotiation
    uint8_t disposition = 50;     // 0..100 openness to being talked down
};

struct FactionState { uint8_t attitude = A_NEUTRAL; int8_t grudge = 0; };

struct Objective {
    uint8_t kind = 0;     // 0 extract,1 trace,2 burn
    uint8_t target = 0;   // node index
    uint16_t reward = 0;
};

struct World {
    uint32_t citynet_seed = 0;
    uint32_t run_seed = 0;
    uint8_t  node_count = 0;
    Node     nodes[MAX_NODES];
    uint8_t  edge_count = 0;
    Edge     edges[MAX_EDGES];
    FactionState factions[F_COUNT];
    uint8_t  entry = 0;
    Objective objective;
    uint8_t  named_count = 0;
    NamedIce named[MAX_NAMED];
};

// ---- the deck + the live run ----------------------------------------------
struct RunState {
    int16_t integrity = 100, integrity_max = 100;
    int16_t buffer = 30, buffer_max = 30;
    uint8_t shield = 2;
    uint8_t tier = 1;
    uint8_t heat = 0;
    uint8_t corruption = 0;
    uint8_t pos = 0;
    uint8_t personality = P_CAUTIOUS;
    uint8_t mod_level[M_COUNT] = {1, 1, 1, 1, 1};
    uint16_t shards = 0;
    uint8_t outcome = O_RUNNING;
    uint8_t death_cause = D_NONE;
    uint16_t step = 0;
    uint8_t depth = 0;           // how many layers deep this dive has gone
    uint8_t objectives_done = 0; // objectives cracked this run (across layers)
    uint8_t node_clears = 0;     // ICE cleared at the current node
    bool objective_done = false; // current layer's objective cracked
    bool exfil = false;          // (legacy, unused with the descent model)
    bool has_ghostkey = false;
    // hunter
    bool    hunt_active = false;
    uint8_t hunter_named = NONE8;
    uint8_t hunter_pos = 0;
    uint8_t hunter_faction = F_COUNT;
    uint16_t next_hunt_step = 0;   // cooldown: no new hunt before this step
    // active multi-round fight (named ICE only — the hard-fought sieges)
    bool    in_fight = false;
    int16_t ice_hp = 0, ice_hp_max = 0;
    uint8_t fight_ice = I_COUNT;
    uint8_t fight_named = NONE8;
    uint8_t fight_node = 0;
    uint8_t fight_round = 0;
    bool    fork_active = false;       // a spawned helper chipping the ICE each round
    bool    is_hunter_fight = false;
    // bookkeeping
    uint8_t nodes_burned = 0;
    uint8_t ice_killed = 0;
    uint8_t named_killed = 0;
};

struct Event {
    uint16_t step = 0;
    uint8_t  node = 0;
    uint8_t  faction = F_COUNT;
    uint8_t  named = NONE8;     // index into World.named
    uint8_t  ice = I_COUNT;
    uint8_t  tag = T_NONE;
    int8_t   heat_d = 0;
};

// ---- decision presented to the policy / UI --------------------------------
struct Decision {
    uint8_t kind = DK_ENCOUNTER;
    uint8_t node = 0;
    uint8_t ice = I_COUNT;
    uint8_t named = NONE8;
    uint8_t persona = PR_COUNT;   // parley only: who's behind the ICE
    uint8_t disposition = 0;      // parley only: 0..100 openness
    std::string prompt;
    std::vector<std::string> options;  // 2..4 labels
    std::vector<uint8_t>     opt_module;// module each option uses, or M_COUNT
};

// ---- persistent legends (the world remembers; the deck does not) -----------
struct Legends {
    uint32_t citynet_seed = 0;
    uint16_t run_count = 0;
    uint32_t best_score = 0;
    int8_t   grudge[F_COUNT] = {0, 0, 0, 0, 0};
    uint8_t  named_count = 0;
    struct { uint8_t name_id, archetype, status; int8_t grudge; } named[MAX_NAMED];
    uint8_t  burned_count = 0;
    struct { uint8_t a, b; } burned[MAX_BURNED];
    uint8_t  marked_count = 0;
    uint8_t  marked[MAX_MARKED];
};

bool legends_serialize(const Legends&, std::string& out);
bool legends_deserialize(const std::string& in, Legends&);

// ---- balance tunables (overridable by the harness for sweeps) --------------
struct Tunables {
    int base_power   = 8;
    int k_tier       = 4;   // power per Tier
    int k_module     = 1;   // power per module level
    int k_shield     = 2;   // power per shield
    int p_corruption = 4;   // power lost per (corruption/p)
    int sec_threat   = 5;   // threat per node security
    int k_tier_threat = 2;  // threat scales with YOUR tier (the world keeps pace)
    int k_heat       = 4;   // threat += heat / k_heat
    int k_grudge     = 2;   // threat per faction grudge
    int hunter_threat = 10;
    int heat_t1 = 25, heat_t2 = 48, heat_t3 = 80;
    int low_int_pct = 30;   // survival spike below this % integrity
};
extern Tunables g_tune;

int player_power(const RunState&);
int threat_level(const World&, const RunState&, const Node&);

// ---- the simulation driver -------------------------------------------------
enum AdvanceResult : uint8_t { AR_STEPPED, AR_DECISION, AR_ENDED };

class Sim {
public:
    void start(uint32_t citynet_seed, uint32_t run_seed, uint8_t personality,
               const Legends* prior = nullptr);

    AdvanceResult advance();                 // one sim-step, or surface a decision
    // Auto-play ICE fights via the personality AI (default on): combat resolves
    // one round per advance() so it animates, and only strategic spikes (branch/
    // extract/survival) surface as AR_DECISION. Off = the caller drives every round.
    void set_auto_combat(bool b) { auto_combat_ = b; }
    // Player pulls the plug: bank the haul and end the run (only out of combat).
    // The descent otherwise continues deeper until the player jacks out or dies.
    void jack_out();
    // UI hook: the avatar physically reached the data cache in the room view —
    // bank the current node's loot now (idempotent; only if its guard is clear).
    void collect_current_node();
    bool needs_decision() const { return pending_; }
    const Decision& decision() const { return dec_; }
    void choose(int option_index);           // resolve the pending decision
    bool running() const { return run_.outcome == O_RUNNING; }

    const RunState& state() const { return run_; }
    const World&    world() const { return world_; }
    const std::vector<std::string>& log() const { return log_; }
    int next_hop_to_objective() const { return next_hop_toward(world_.objective.target); } // first BFS step, -1 if none

    int score() const;
    std::string chronicle() const;           // Gibson-voice run summary
    void update_legends(Legends&) const;      // fold this run's results into the blob

private:
    World world_;
    RunState run_;
    Rng rng_;
    Decision dec_;
    bool pending_ = false;
    std::vector<std::string> log_;
    Event events_[EVENT_RING];
    uint8_t ev_head_ = 0, ev_count_ = 0;

    // internals
    void gen_world(const Legends* prior);
    void build_topology();
    void place_content();
    void apply_legends(const Legends*);
    int  next_hop_toward(uint8_t target) const;     // BFS first step, -1 if none
    bool edge_usable(const Edge&) const;
    void logline(const std::string&);
    void push_event(uint8_t tag, uint8_t node, uint8_t ice, uint8_t named, int8_t heat_d);
    AdvanceResult decide_or_auto();           // auto-resolve a combat round, or surface the decision
    void begin_fight(uint8_t node, uint8_t ice, uint8_t named, bool hunter);
    void open_fight_round();
    void resolve_round(int option);
    void finish_fight(bool won, bool escaped);
    void open_branch();
    void open_parley();          // a named entity blocks the way — negotiate or fight
    void resolve_parley(int option);
    void open_memory();          // a memory construct — dive / extract / stabilize / release
    void resolve_memory(int option);
    void open_survival();
    void open_extract();
    void open_dive();            // after an objective: jack out, or dive deeper
    void resolve_dive(int option);
    void dive_deeper();          // regenerate a harder layer, carry the deck
    void resolve_branch(int option);
    void resolve_survival(int option);
    void resolve_extract(int option);
    void auto_trivial(uint8_t ice);
    void accrue_and_move();
    void check_thresholds();
    void advance_hunter();
    void add_heat(int d);
    void add_corruption(int d);
    void hurt(int d, uint8_t cause);
    void heal(int d);
    void grant_loot(Node&);
    void apply_tier_growth();    // a level-up raises ALL vital maxima, not just power
    uint8_t committed_branch_ = NONE8;        // chosen next hop awaiting the move
    bool auto_combat_ = true;                  // deck auto-fights named ICE (watch, don't tap)
    bool survival_warned_ = false;            // suppress repeat survival spikes
    std::vector<uint8_t> branched_;           // nodes a branch was already offered at
    std::vector<uint8_t> branch_opts_;        // neighbor id per current branch option
    uint8_t parley_named_ = NONE8;            // entity in the current parley
    uint8_t parley_node_  = 0;                // node of the current parley
    int     parley_bribe_ = 0;                // bribe cost shown this parley
    int     parley_tribute_ = 0;              // appease tribute shown this parley
};

// run a whole dive headless under an AI policy; returns the finished Sim.
void run_headless(Sim&, uint32_t citynet_seed, uint32_t run_seed,
                  uint8_t personality, const Legends* prior = nullptr);
// the AI policy — shared by run_headless and the balance harness so the sampled
// power-vs-threat curve uses the exact decisions a real AI run would make.
int ai_decide(uint8_t personality, const RunState&, const Decision&, Rng&);

// content-table accessors (also used by the linter + the app)
const char* faction_name(uint8_t);
const char* faction_short(uint8_t);                   // "Kurogane" etc. (for the carry-over readout)
const char* node_type_name(uint8_t);
const char* ice_name(uint8_t);
const char* module_name(uint8_t);
const char* named_name(uint8_t name_id);
const char* persona_name(uint8_t persona);            // "AI" / "sysop" / "daemon" / "construct"
std::string node_label(const World&, uint8_t node);   // e.g. "GRID-13"
uint8_t ice_weakness(uint8_t ice);                    // module that counters it
uint8_t ice_punish(uint8_t ice);                      // module it punishes (or M_COUNT)

} // namespace cyber
