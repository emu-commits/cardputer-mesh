// Midnight City — engine substrate implementation (Phase 1).
// See midnight_world.h for the architectural contract.
#include "core/midnight_world.h"
#include <cstdio>   // snprintf for the narrator (§7)

namespace mid {

// ---- RNG (xorshift128, splitmix64 seed) — identical scheme to CyberHack ------
void Rng::seed(uint64_t v) {
    for (int i = 0; i < 4; ++i) {
        v += 0x9E3779B97F4A7C15ULL;
        uint64_t z = v;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31);
        s[i] = (uint32_t)z;
    }
    if ((s[0] | s[1] | s[2] | s[3]) == 0) s[0] = 1;
}
uint32_t Rng::next() {
    uint32_t t = s[3];
    const uint32_t sx = s[0];
    s[3] = s[2]; s[2] = s[1]; s[1] = sx;
    t ^= t << 11; t ^= t >> 8;
    s[0] = t ^ sx ^ (sx >> 19);
    return s[0];
}
uint32_t Rng::range(uint32_t n) { return n ? next() % n : 0; }
int Rng::between(int lo, int hi) { return hi <= lo ? lo : lo + (int)range((uint32_t)(hi - lo + 1)); }
bool Rng::chance(int pct) { return (int)range(100) < pct; }

// ---- flash string tables ----------------------------------------------------
static const char* const kAgentNames[] = {
    "Case", "Molly", "Armitage", "Riviera", "Wintermute", "Dixie", "Maelcum",
    "Linda", "Finn", "Ratz", "Julius", "Hideo", "Angie", "Bobby", "Jammer",
    "Pris", "Roy", "Leon", "Zhang", "Kuang", "Slick", "Cherry", "Gentry",
    "Mona", "Kumiko", "Sally", "Petal", "Swain", "Yanaka", "Tick", "Pyramid",
    "Lupus", "Continuity", "Aleph", "Marly", "Alain", "Wig", "Jones", "Brigitte",
    "Virek", "Pocket", "Rhea", "Sendai", "Colin", "Idoru", "Rez", "Laney", "Blackwell"
};
static const char* const kCompanyNames[] = {
    "Sense/Net", "Tessier-Ashpool", "Hosaka", "Maas Biolabs", "Ono-Sendai",
    "Zion Cluster", "Freeside", "Chiba Holdings", "Sprawl Dynamics", "Nighttown",
    "Cheap Hotel", "Gentleman Loser", "Straylight", "Villa Diamond", "BAMA Coop"
};
static const char* const kFactionNames[F_COUNT] = {
    "Street Crew", "Megacorp", "Syndicate", "Nomad Caravan", "Mutant Gang", "Techno-Cult"
};
static const char* const kDistrictTypeNames[DT_COUNT] = {
    "Slum Block", "Arcology", "Neon Market", "Industrial", "Undercity",
    "Data Center", "Toxic Zone", "Logistics Hub", "Rooftop Farm", "Metro Ruins"
};
static const char* const kJobNames[J_COUNT] = {
    "Unemployed", "Construction Tech", "Machinist", "Electronics Tech",
    "Biotech Engineer", "Ripperdoc", "Gunsmith", "Armor Tech", "Chemtech",
    "Decker", "Drone Tech", "Infrastructure Eng"
};
static const char* const kCommodityNames[C_COUNT] = {
    "food", "water", "scrap", "electronics", "biotech", "chems", "data", "weapons"
};

const char* agent_name(uint8_t id) {
    int n = (int)(sizeof(kAgentNames) / sizeof(kAgentNames[0]));
    return kAgentNames[id % n];
}
int agent_name_count() { return (int)(sizeof(kAgentNames) / sizeof(kAgentNames[0])); }
const char* company_name(uint8_t id) {
    int n = (int)(sizeof(kCompanyNames) / sizeof(kCompanyNames[0]));
    return kCompanyNames[id % n];
}
int company_name_count() { return (int)(sizeof(kCompanyNames) / sizeof(kCompanyNames[0])); }
const char* faction_name(uint8_t f) { return f < F_COUNT ? kFactionNames[f] : "Unaffiliated"; }
const char* district_type_name(uint8_t t) { return t < DT_COUNT ? kDistrictTypeNames[t] : "?"; }
const char* job_name(uint8_t j) { return j < J_COUNT ? kJobNames[j] : "?"; }
const char* commodity_name(uint8_t c) { return c < C_COUNT ? kCommodityNames[c] : "?"; }
const char* agent_kind_name(uint8_t k) {
    static const char* n[AK_COUNT] = { "human", "synth", "mutant" };
    return k < AK_COUNT ? n[k] : "?";
}
int human_count(const World& w) {
    int n = 0;
    for (int i = 0; i < w.agent_count; ++i)
        if ((w.agents[i].status & AF_ALIVE) && w.agents[i].kind == AK_HUMAN) ++n;
    return n;
}
int synth_count(const World& w) {
    int n = 0;
    for (int i = 0; i < w.agent_count; ++i)
        if ((w.agents[i].status & AF_ALIVE) && w.agents[i].kind == AK_SYNTH) ++n;
    return n;
}

// ---- graph queries ----------------------------------------------------------
bool districts_adjacent(const World& w, uint8_t a, uint8_t b) {
    if (a >= w.district_count) return false;
    for (int k = 0; k < w.districts[a].deg; ++k)
        if (w.districts[a].adj[k] == b) return true;
    return false;
}
bool world_connected(const World& w) {
    if (w.district_count == 0) return true;
    uint8_t seen[MAX_DISTRICTS] = {0};
    uint8_t q[MAX_DISTRICTS];
    int head = 0, tail = 0;
    q[tail++] = 0; seen[0] = 1;
    while (head < tail) {
        const District& d = w.districts[q[head++]];
        for (int k = 0; k < d.deg; ++k) {
            uint8_t nb = d.adj[k];
            if (nb != NONE8 && nb < w.district_count && !seen[nb]) { seen[nb] = 1; q[tail++] = nb; }
        }
    }
    for (int i = 0; i < w.district_count; ++i) if (!seen[i]) return false;
    return true;
}
int district_distance(const World& w, uint8_t from, uint8_t to) {
    if (from >= w.district_count || to >= w.district_count) return -1;
    uint8_t dist[MAX_DISTRICTS];
    for (int i = 0; i < MAX_DISTRICTS; ++i) dist[i] = NONE8;
    uint8_t q[MAX_DISTRICTS];
    int head = 0, tail = 0;
    q[tail++] = from; dist[from] = 0;
    while (head < tail) {
        uint8_t cur = q[head++];
        if (cur == to) return dist[cur];
        const District& d = w.districts[cur];
        for (int k = 0; k < d.deg; ++k) {
            uint8_t nb = d.adj[k];
            if (nb != NONE8 && nb < w.district_count && dist[nb] == NONE8) {
                dist[nb] = (uint8_t)(dist[cur] + 1); q[tail++] = nb;
            }
        }
    }
    return dist[to] == NONE8 ? -1 : (int)dist[to];
}

// ---- worldgen helpers -------------------------------------------------------
static void add_edge(World& w, uint8_t a, uint8_t b) {
    if (a == b) return;
    if (districts_adjacent(w, a, b)) return;
    District& da = w.districts[a];
    District& db = w.districts[b];
    if (da.deg >= MAXDEG || db.deg >= MAXDEG) return;
    da.adj[da.deg++] = b;
    db.adj[db.deg++] = a;
}

// services typical of each district type (bitmask of Service)
static uint16_t base_services(uint8_t type, Rng& rng) {
    uint16_t s = 0;
    switch (type) {
        case DT_SLUM:         s = SV_JOB_BOARD | SV_BAR | SV_FENCE; break;
        case DT_ARCOLOGY:     s = SV_MARKET | SV_CLINIC | SV_JOB_BOARD; break;
        case DT_MARKET:       s = SV_MARKET | SV_FENCE | SV_BAR | SV_JOB_BOARD; break;
        case DT_INDUSTRIAL:   s = SV_FORGE | SV_FABLAB | SV_JOB_BOARD; break;
        case DT_UNDERCITY:    s = SV_CHOPSHOP | SV_FENCE | SV_CLINIC; break;
        case DT_DATACENTER:   s = SV_DATAVAULT | SV_JOB_BOARD; break;
        case DT_TOXIC:        s = SV_CHEMLAB; break;
        case DT_LOGISTICS:    s = SV_MARKET | SV_DRONEBAY | SV_JOB_BOARD; break;
        case DT_ROOFTOP_FARM: s = SV_MARKET | SV_BAR; break;
        case DT_METRO:        s = SV_FENCE | SV_CHOPSHOP; break;
        default: break;
    }
    // denser services (#G): more interaction sites per block -> more jobs, trade,
    // fixers, crafting spots -> more economic pressure and emergent events. Free
    // (services is a bitmask; POIs are regenerated, not serialized).
    if (rng.chance(28)) s |= SV_CLINIC;
    if (rng.chance(22)) s |= SV_ARMORSHOP;
    if (rng.chance(32)) s |= SV_JOB_BOARD;   // more places to get hired
    if (rng.chance(26)) s |= SV_MARKET;      // more trade -> more shortage pressure
    if (rng.chance(22)) s |= SV_BAR;         // more fixers / gigs
    if (rng.chance(18)) s |= SV_FENCE;
    if (rng.chance(16)) s |= SV_FABLAB;      // more crafting sites (feeds Batch H)
    return s;
}

// distance from a seat district, by BFS, into out[] (NONE8 = unreachable)
static void bfs_dist(const World& w, uint8_t seat, uint8_t* out) {
    for (int i = 0; i < MAX_DISTRICTS; ++i) out[i] = NONE8;
    uint8_t q[MAX_DISTRICTS]; int head = 0, tail = 0;
    q[tail++] = seat; out[seat] = 0;
    while (head < tail) {
        uint8_t cur = q[head++];
        const District& d = w.districts[cur];
        for (int k = 0; k < d.deg; ++k) {
            uint8_t nb = d.adj[k];
            if (nb != NONE8 && nb < w.district_count && out[nb] == NONE8) {
                out[nb] = (uint8_t)(out[cur] + 1); q[tail++] = nb;
            }
        }
    }
}

void gen_world(World& w, uint32_t seed) {
    w = World{};                       // reset to defaults
    w.world_seed = seed;
    w.tick = 0;
    w.rng.seed(0x9E3779B9u ^ seed);

    Rng& r = w.rng;

    // --- districts ----------------------------------------------------------
    int n = r.between(24, MAX_DISTRICTS);
    w.district_count = (uint8_t)n;

    // weighted type pick (slums + markets common; datacenter/arcology rarer)
    static const uint8_t typeWeight[DT_COUNT] = {
        /*SLUM*/5, /*ARCOLOGY*/2, /*MARKET*/3, /*INDUSTRIAL*/3, /*UNDERCITY*/3,
        /*DATACENTER*/2, /*TOXIC*/2, /*LOGISTICS*/2, /*ROOFTOP_FARM*/2, /*METRO*/2
    };
    int weightTotal = 0;
    for (int t = 0; t < DT_COUNT; ++t) weightTotal += typeWeight[t];

    for (int i = 0; i < n; ++i) {
        District& d = w.districts[i];
        int roll = r.range((uint32_t)weightTotal), acc = 0, type = 0;
        for (int t = 0; t < DT_COUNT; ++t) { acc += typeWeight[t]; if (roll < acc) { type = t; break; } }
        d.type = (uint8_t)type;
        d.seed = r.next();
        d.population = (uint8_t)r.between(4, 40);
        d.prosperity = (uint8_t)r.between(5, 90);
        d.danger     = (uint8_t)r.between(0, 60);
        d.danger_base = d.danger;            // intrinsic danger; spikes decay back to it
        d.services   = base_services(d.type, r);
        for (int c = 0; c < C_COUNT; ++c) d.supply[c] = (uint8_t)r.between(10, 80);
        for (int h = 0; h < HZ_COUNT; ++h) d.hazard[h] = (uint8_t)(r.chance(25) ? r.between(1, 50) : 0);
        d.owner = F_COUNT;             // resolved after influence below
    }
    // guarantee key district types exist so the early game has somewhere to start/sell/hack
    w.districts[0].type = DT_SLUM;     w.districts[0].services = base_services(DT_SLUM, r);
    if (n > 1) { w.districts[1].type = DT_MARKET;     w.districts[1].services = base_services(DT_MARKET, r); }
    if (n > 2) { w.districts[2].type = DT_DATACENTER; w.districts[2].services = base_services(DT_DATACENTER, r); }

    // --- graph: ring guarantees connectivity, then random chords -----------
    for (int i = 0; i < n; ++i) add_edge(w, (uint8_t)i, (uint8_t)((i + 1) % n));
    int chords = r.between(n / 3, n);
    for (int e = 0; e < chords; ++e) add_edge(w, (uint8_t)r.range((uint32_t)n), (uint8_t)r.range((uint32_t)n));

    // --- factions + territorial influence ----------------------------------
    // each faction seats at a distinct district; influence falls off with hops.
    uint8_t seat[F_COUNT];
    for (int f = 0; f < F_COUNT; ++f) {
        uint8_t s;
        bool clash;
        int guard = 0;
        do {
            s = (uint8_t)r.range((uint32_t)n);
            clash = false;
            for (int g = 0; g < f; ++g) if (seat[g] == s) clash = true;
        } while (clash && ++guard < 64);
        seat[f] = s;
        FactionState& fs = w.factions[f];
        fs.alignment  = (uint8_t)r.between(0, 3);
        fs.tech_level = (uint8_t)r.between(0, 3);
        fs.specialty  = (uint8_t)r.range(C_COUNT);
        fs.treasury   = (uint32_t)r.between(1000, 50000);
        for (int g = 0; g <= F_COUNT; ++g) fs.grudge[g] = (int8_t)r.between(-2, 4);
        fs.grudge[f] = 0;              // no grudge against self
    }
    for (int f = 0; f < F_COUNT; ++f) {
        uint8_t dist[MAX_DISTRICTS];
        bfs_dist(w, seat[f], dist);
        for (int i = 0; i < n; ++i) {
            int dd = dist[i] == NONE8 ? 99 : dist[i];
            int inf = 90 - dd * 30;
            if (inf < 0) inf = 0;
            if (inf > 0) inf += r.between(-8, 8);
            if (inf < 0) inf = 0;
            if (inf > 100) inf = 100;
            w.districts[i].influence[f] = (uint8_t)inf;
        }
    }
    // owner = dominant faction if it clearly leads, else neutral
    for (int i = 0; i < n; ++i) {
        District& d = w.districts[i];
        int best = -1, bestf = F_COUNT, second = -1;
        for (int f = 0; f < F_COUNT; ++f) {
            int v = d.influence[f];
            if (v > best) { second = best; best = v; bestf = f; }
            else if (v > second) second = v;
        }
        d.owner = (best >= 40 && best - second >= 15) ? (uint8_t)bestf : (uint8_t)F_COUNT;
    }

    // --- agents -------------------------------------------------------------
    int m = r.between(40, MAX_AGENTS);   // denser population (#G) -> more emergent pressure
    if (m < F_COUNT + 1) m = F_COUNT + 1;
    w.agent_count = (uint8_t)m;

    // protagonist: agent 0 — a nobody, no faction, no job, tiny stake.
    {
        Agent& p = w.agents[0];
        p.name_id = (uint8_t)r.range((uint32_t)agent_name_count());
        // start in a slum/undercity if one exists, else district 0
        uint8_t start = 0;
        for (int i = 0; i < n; ++i) if (w.districts[i].type == DT_SLUM || w.districts[i].type == DT_UNDERCITY) { start = (uint8_t)i; break; }
        w.home = start;
        p.loc = start;
        p.faction = F_COUNT;
        p.job = J_NONE;
        for (int t = 0; t < TR_COUNT; ++t) p.trait[t] = (uint8_t)r.between(40, 200);
        for (int nd = 0; nd < ND_COUNT; ++nd) p.need[nd] = (uint8_t)r.between(20, 50);
        p.money = (uint32_t)r.between(10, 40);
        p.status = AF_PLAYER | AF_ALIVE;
        p.mood = (uint8_t)r.between(40, 70);
        p.kind = AK_HUMAN;                          // the protagonist is human
    }
    w.synth_tide = (uint8_t)r.between(0, 20);       // tides start low; conditions decide their fate
    w.mutant_tide = (uint8_t)r.between(0, 20);

    for (int i = 1; i < m; ++i) {
        Agent& a = w.agents[i];
        a.name_id = (uint8_t)r.range((uint32_t)agent_name_count());
        a.loc = (uint8_t)r.range((uint32_t)n);
        // mostly human at worldgen; a few synths/mutants by district flavor
        uint8_t dt = w.districts[a.loc].type;
        a.kind = ((dt == DT_TOXIC || dt == DT_UNDERCITY) && r.chance(35)) ? (uint8_t)AK_MUTANT
                 : r.chance(12) ? (uint8_t)AK_SYNTH : (uint8_t)AK_HUMAN;
        a.faction = r.chance(50) ? (uint8_t)r.range(F_COUNT) : (uint8_t)F_COUNT;
        for (int t = 0; t < TR_COUNT; ++t) a.trait[t] = (uint8_t)r.between(0, 255);
        for (int nd = 0; nd < ND_COUNT; ++nd) a.need[nd] = (uint8_t)r.between(10, 60);
        a.status = AF_ALIVE;
        if (r.chance(70)) {            // most NPCs hold a novice trade
            a.job = (uint8_t)r.between(J_CONSTRUCTION, J_INFRA);
            a.skill[a.job] = (uint8_t)r.between(5, 60);
            a.status |= AF_EMPLOYED;
        }
        if (r.chance(15) || a.job == J_DECKER) a.status |= AF_HAS_DECK;
        a.money = (uint32_t)r.between(0, 600);
        a.mood = (uint8_t)r.between(30, 80);
        for (int it = 0; it < IT_COUNT; ++it) a.inv[it] = (uint8_t)(r.chance(30) ? r.between(1, 5) : 0);
    }

    // faction leaders: reserve agents 1..F_COUNT (guaranteed to exist since m>=F_COUNT+1)
    for (int f = 0; f < F_COUNT; ++f) {
        uint8_t li = (uint8_t)(1 + f);
        w.agents[li].faction = (uint8_t)f;
        w.agents[li].loc = seat[f];
        w.factions[f].leader = li;
    }

    // --- player company (just the protagonist, tier SOLO) -------------------
    w.company = Company{};
    w.company.name_id = (uint8_t)r.range((uint32_t)company_name_count());
    w.company.tier = CT_SOLO;
    w.company.treasury = 0;
    w.company.reputation = 0;

    // --- protagonist focus: starts unemployed, hunting for work (#10) -------
    w.focus = FC_FIND_WORK;
    w.interrupt_focus = NONE8;
    w.apt_district = NONE8;
    w.work_district = NONE8;
    w.craft_target = 0xFF;
    w.contract = Contract{};
    w.agents[0].status &= (uint8_t)~AF_EMPLOYED;
    w.agents[0].job = J_NONE;

    // --- events: empty ring -------------------------------------------------
    w.event_count = 0;
    w.event_head = 0;
}

// ---- Phase 2: economy + needs + basic behavior ------------------------------
MidTunables g_mtune;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static void push_event(World& w, uint8_t kind, uint8_t node, uint8_t agent, uint8_t data); // fwd

// record a cash move on the PROTAGONIST's ledger (#4). No-op for any other agent,
// so the call sites can fire unconditionally. amount carries the sign of the move.
static void record_txn(World& w, const Agent& a, int32_t amount, uint8_t reason, uint8_t data = 0) {
    if (&a != &w.agents[0] || amount == 0) return;
    Txn& t = w.txns[w.txn_head];
    t.amount = amount; t.reason = reason; t.data = data; t.tick = (uint16_t)w.tick;
    w.txn_head = (uint8_t)((w.txn_head + 1) % TXNMAX);
    if (w.txn_count < TXNMAX) w.txn_count++;
}

const char* txn_reason_name(uint8_t r) {
    static const char* n[TXN_REASON_COUNT] = {
        "wage", "supplies", "rent", "invested", "craft sale", "contract",
        "heist", "loot", "robbed", "company payout", "clinic care", "sale"
    };
    return r < TXN_REASON_COUNT ? n[r] : "?";
}
const char* item_name(uint8_t it) {
    static const char* n[IT_COUNT] = {
        "materials", "components", "a weapon", "armor", "an implant", "chems", "data", "food"
    };
    return it < IT_COUNT ? n[it] : "goods";
}
// what a profession's labor produces (the unit a skilled worker crafts to sell)
static uint8_t job_output(uint8_t job) {
    switch (job) {
        case J_GUNSMITH:    return IT_WEAPONS;
        case J_ARMOR:       return IT_ARMOR;
        case J_RIPPERDOC: case J_BIOTECH: return IT_IMPLANTS;
        case J_CHEMTECH:    return IT_CHEMS;
        case J_ELECTRONICS: case J_INFRA: return IT_COMPONENTS;
        case J_DECKER:      return IT_DATA;
        default:            return IT_MATERIALS;   // construction/machinist/drone -> raw goods
    }
}

int price_of(const World& w, uint8_t district, uint8_t commodity) {
    if (district >= w.district_count || commodity >= C_COUNT) return g_mtune.price_max;
    const MidTunables& T = g_mtune;
    int supply = w.districts[district].supply[commodity];
    // scarcity pricing: low supply -> high price; clamped so it can never run away
    int p = T.base_price[commodity] * T.demand_ref / (supply + 1);
    return clampi(p, T.price_min, T.price_max);
}

int wage_of(const World& w, uint8_t district, const Agent& a) {
    if (district >= w.district_count) return g_mtune.wage_base;
    const District& d = w.districts[district];
    int skill = a.job < J_COUNT ? a.skill[a.job] : 0;
    // base + skill bonus + local prosperity; modest spread, always positive
    return g_mtune.wage_base + skill / 8 + d.prosperity / 16;
}

int alive_count(const World& w) {
    int n = 0;
    for (int i = 0; i < w.agent_count; ++i) if (w.agents[i].status & AF_ALIVE) ++n;
    return n;
}

// BFS first hop from `from` toward the nearest district offering `service`.
// Returns the adjacent district to step to, or `from` if none / already there.
static uint8_t hop_to_service(const World& w, uint8_t from, uint16_t service) {
    if (from >= w.district_count) return from;
    if (w.districts[from].services & service) return from;
    uint8_t parent[MAX_DISTRICTS];
    for (int i = 0; i < MAX_DISTRICTS; ++i) parent[i] = NONE8;
    uint8_t q[MAX_DISTRICTS]; int head = 0, tail = 0;
    q[tail++] = from; parent[from] = from;
    uint8_t found = NONE8;
    while (head < tail && found == NONE8) {
        uint8_t cur = q[head++];
        const District& d = w.districts[cur];
        for (int k = 0; k < d.deg; ++k) {
            uint8_t nb = d.adj[k];
            if (nb == NONE8 || nb >= w.district_count || parent[nb] != NONE8) continue;
            parent[nb] = cur; q[tail++] = nb;
            if (w.districts[nb].services & service) { found = nb; break; }
        }
    }
    if (found == NONE8) return from;
    // walk back to the first hop out of `from`
    uint8_t cur = found;
    while (parent[cur] != from) cur = parent[cur];
    return cur;
}

// the commodity that satisfies a vital need
static uint8_t need_commodity(uint8_t need) {
    return need == ND_THIRST ? C_WATER : C_FOOD;
}

static void bump_need(uint8_t& n, int delta) {
    int v = (int)n + delta;
    n = (uint8_t)clampi(v, 0, 255);
}

// ---- Phase 3: skill tiers, careers, crafting, company -----------------------
uint8_t skill_tier(uint8_t xp) {
    const MidTunables& T = g_mtune;
    if (xp < T.skill_tier_xp[0]) return ST_NOVICE;
    if (xp < T.skill_tier_xp[1]) return ST_SKILLED;
    if (xp < T.skill_tier_xp[2]) return ST_EXPERT;
    return ST_MASTER;
}
const char* skill_tier_name(uint8_t t) {
    static const char* n[ST_COUNT] = { "Novice", "Skilled", "Expert", "Master" };
    return t < ST_COUNT ? n[t] : "?";
}
const char* ambition_name(uint8_t a) {
    static const char* n[AMB_COUNT] = { "Survive", "Wealth", "Mastery", "Territory" };
    return a < AMB_COUNT ? n[a] : "?";
}
const char* company_tier_name(uint8_t t) {
    static const char* n[CT_COUNT] = { "Solo", "Crew", "Outfit", "Corp", "Megacorp" };
    return t < CT_COUNT ? n[t] : "?";
}
uint8_t top_skill_tier(const Agent& a) {
    uint8_t best = ST_NOVICE;
    for (int j = J_CONSTRUCTION; j < J_COUNT; ++j) {
        uint8_t t = skill_tier(a.skill[j]);
        if (t > best) best = t;
    }
    return best;
}
// which facility a profession needs to craft for profit
static uint16_t job_facility(uint8_t job) {
    switch (job) {
        case J_CONSTRUCTION: return SV_FORGE;
        case J_MACHINIST:    return SV_FORGE;
        case J_ELECTRONICS:  return SV_FABLAB;
        case J_BIOTECH:      return SV_CLINIC;
        case J_RIPPERDOC:    return SV_CLINIC;
        case J_GUNSMITH:     return SV_ARMORSHOP;
        case J_ARMOR:        return SV_ARMORSHOP;
        case J_CHEMTECH:     return SV_CHEMLAB;
        case J_DECKER:       return SV_DATAVAULT;
        case J_DRONE:        return SV_DRONEBAY;
        case J_INFRA:        return SV_FABLAB;
        default:             return 0;
    }
}
int production_value(const World& w, const Agent& a) {
    if (a.job < J_CONSTRUCTION || a.job >= J_COUNT) return 0;
    int tier = skill_tier(a.skill[a.job]);
    int v = g_mtune.craft_base * (tier + 1);
    v += v * w.districts[a.loc].prosperity / 200;   // up to +50% in a rich district
    return v;
}
// ---- crafting recipe tree (gather -> craft -> use/sell) ---------------------
// Each discipline (profession + min tier) at a facility turns gathered inputs into an
// output item, or a cyberdeck (out_item 0xFF -> AF_HAS_DECK). The cyberdeck is the
// gated late goal that unlocks running the net.
static const Recipe kRecipes[CR_COUNT] = {
    /* CR_COMPONENTS */ { J_ELECTRONICS, ST_NOVICE,  SV_FABLAB,    2, 0, 0, IT_COMPONENTS },
    /* CR_WEAPON     */ { J_GUNSMITH,    ST_SKILLED, SV_ARMORSHOP, 1, 1, 0, IT_WEAPONS },
    /* CR_ARMOR      */ { J_ARMOR,       ST_SKILLED, SV_ARMORSHOP, 2, 1, 0, IT_ARMOR },
    /* CR_IMPLANT    */ { J_RIPPERDOC,   ST_SKILLED, SV_CLINIC,    0, 2, 1, IT_IMPLANTS },
    /* CR_CHEMS      */ { J_CHEMTECH,    ST_NOVICE,  SV_CHEMLAB,   1, 0, 0, IT_CHEMS },
    /* CR_DECK       */ { J_DECKER,      ST_EXPERT,  SV_FABLAB,    1, 3, 2, 0xFF },
};
const Recipe& recipe_of(uint8_t cr) { return kRecipes[cr < CR_COUNT ? cr : 0]; }
const char* craft_name(uint8_t cr) {
    static const char* n[CR_COUNT] = { "components", "a weapon", "body armor", "an implant", "chems", "a cyberdeck" };
    return cr < CR_COUNT ? n[cr] : "?";
}
// raw input item -> the commodity you buy to gather it
static uint8_t item_commodity(uint8_t item) {
    switch (item) {
        case IT_MATERIALS:  return C_SCRAP;
        case IT_COMPONENTS: return C_ELECTRONICS;
        case IT_DATA:       return C_DATA;
        default:            return C_COUNT;
    }
}

// learning by doing — diminishing per tier so mastery is a long grind (deterministic).
static void gain_skill(World& w, Agent& a, int boost = 0) {
    if (a.job < J_CONSTRUCTION || a.job >= J_COUNT || a.skill[a.job] >= 255) return;
    uint8_t tier = skill_tier(a.skill[a.job]);
    int chance = (tier == ST_NOVICE) ? 45 : (tier == ST_SKILLED) ? 22 : (tier == ST_EXPERT) ? 10 : 4;
    chance += boost;
    if (w.rng.chance(chance)) a.skill[a.job]++;
}

// --- forward decl: the basic NPC subsistence ladder (also the survival floor) -
static uint8_t npc_action(World& w, Agent& a);
static bool    flee_to_safer(World& w, Agent& a);   // defined with the combat layer

// ============================================================================
// Focus state machine (#9/#10): the protagonist self-drives toward a concrete
// focus (find work -> commute -> save -> found/run a company), can be pulled off
// it by danger (the interrupt-and-return arc engine), and earns ONLY when present
// at work — deviating costs a wage laborer income, while a staffed company keeps
// paying passively (company_step runs daily regardless of where the avatar is).
// ============================================================================

// work hours of the daily commute: 08:00..18:00 (10 earning hours)
static bool work_hours(const World& w) { int h = (int)(w.tick % 24); return h >= 8 && h < 18; }

// one hop along the shortest district route toward `to` (greedy on BFS distance)
static uint8_t next_hop_toward(const World& w, uint8_t from, uint8_t to) {
    if (from == to || to >= w.district_count) return from;
    int best = district_distance(w, from, to);
    if (best < 0) return from;
    uint8_t nx = from;
    const District& d = w.districts[from];
    for (int i = 0; i < d.deg; ++i) {
        uint8_t nb = d.adj[i];
        if (nb == NONE8 || nb >= w.district_count) continue;
        int dd = district_distance(w, nb, to);
        if (dd >= 0 && dd < best) { best = dd; nx = nb; }
    }
    return nx;
}

// pick a profession to take here — prefer one whose facility this block offers
static uint8_t pick_job_for(World& w, uint8_t loc) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint8_t j = (uint8_t)w.rng.between(J_CONSTRUCTION, J_INFRA);
        if (w.districts[loc].services & job_facility(j)) return j;
    }
    return (uint8_t)w.rng.between(J_CONSTRUCTION, J_INFRA);
}

// post a one-time gig at a fixer: travel to a far district, do a thing, get paid.
static void offer_contract(World& w, Agent& a) {
    Contract& c = w.contract;
    c.active = 1;
    c.kind   = (uint8_t)w.rng.range(CK_COUNT);
    uint8_t t = a.loc;
    for (int i = 0; i < 5 && t == a.loc; ++i) t = (uint8_t)w.rng.range(w.district_count);
    c.target = t;
    c.progress = 0;
    c.need_ticks = (uint8_t)w.rng.between(2, 5);
    int dist = district_distance(w, a.loc, t); if (dist < 1) dist = 1;
    c.reward   = (uint16_t)(80 + dist * 40 + w.rng.between(0, 120));
    c.deadline = (uint16_t)(w.tick + (uint32_t)w.rng.between(48, 120));   // 2-5 days
    w.focus = FC_CONTRACT;
    push_event(w, EV_BOUNTY, t, 0, c.kind);   // a gig pointing at district t
}

// FC_FIND_WORK: get to a hiring spot, then land a steady job OR a one-time gig.
static uint8_t do_find_work(World& w, Agent& a) {
    District& here = w.districts[a.loc];
    const uint16_t hiring = SV_JOB_BOARD | SV_BAR | SV_FENCE;
    if (!(here.services & hiring)) {
        uint8_t nx = hop_to_service(w, a.loc, SV_JOB_BOARD);
        if (nx != a.loc) { a.loc = nx; return ACT_MOVE; }
        return ACT_REST;   // nothing reachable; wait for the world to shift
    }
    // MASTERY: the player wants a specific trade — take it, skip the gig roll.
    if (w.directive.ambition == AMB_MASTERY) {
        uint8_t tgt = (w.directive.target >= J_CONSTRUCTION && w.directive.target < J_COUNT)
                          ? w.directive.target : (uint8_t)J_DECKER;
        a.job = tgt; a.status |= AF_EMPLOYED; w.work_district = a.loc; w.focus = FC_WORK;
        push_event(w, EV_RECRUIT, a.loc, 0, a.faction);
        return ACT_SEEKJOB;
    }
    // otherwise: sometimes a fixer offers a gig instead of steady work
    if (!w.contract.active && w.district_count > 1 && w.rng.chance(35)) {
        offer_contract(w, a);
        return ACT_SEEKJOB;
    }
    a.job = pick_job_for(w, a.loc); a.status |= AF_EMPLOYED; w.work_district = a.loc; w.focus = FC_WORK;
    push_event(w, EV_RECRUIT, a.loc, 0, a.faction);
    return ACT_SEEKJOB;
}

// FC_WORK / FC_SAVE: the default daily commute (#10). On-hours -> be at work and
// earn (presence-gated); off-hours -> head home and rest.
static uint8_t do_work(World& w, Agent& a) {
    const MidTunables& T = g_mtune;
    if (!(a.status & AF_EMPLOYED) || w.work_district >= w.district_count) { w.focus = FC_FIND_WORK; return do_find_work(w, a); }
    if (work_hours(w)) {
        if (a.loc != w.work_district) { uint8_t nx = next_hop_toward(w, a.loc, w.work_district); if (nx != a.loc) { a.loc = nx; return ACT_MOVE; } return ACT_MOVE; }
        // skilled evolution of the job (chosen integration): once Skilled in your
        // profession with its facility here, "work" CRAFTS a sellable item and sells
        // it (income that outpaces wages); otherwise it's entry-level wage labor.
        bool skilled_here = a.job >= J_CONSTRUCTION && a.job < J_COUNT &&
                            skill_tier(a.skill[a.job]) >= ST_SKILLED &&
                            (w.districts[a.loc].services & job_facility(a.job)) &&
                            production_value(w, a) > 0;
        if (skilled_here) {
            // craft a unit of your trade's output, then sell it -- inventory genuinely
            // moves (made -> sold), and the log names the product (#3).
            uint8_t out = job_output(a.job);
            if (a.inv[out] < 250) a.inv[out]++;
            int v = production_value(w, a);
            if (a.inv[out] > 0) { a.inv[out]--; a.money += (uint32_t)v; record_txn(w, a, v, TXN_SALE, out); }
        } else {
            int wg = wage_of(w, a.loc, a); a.money += (uint32_t)wg; record_txn(w, a, wg, TXN_WAGE, a.job);
        }
        bump_need(a.need[ND_FATIGUE], T.fatigue_work);
        bool master = (w.directive.ambition == AMB_MASTERY && a.job == w.directive.target);
        gain_skill(w, a, master ? T.train_rate * 18 : 0);
        return ACT_WORK;
    }
    // off-hours: head home. With no apartment you doss at work (home == work, so no
    // commute at all); once you've rented, home is an ADJACENT zone -> a one-hop
    // daily commute (#5/#8), not a multi-zone scramble.
    uint8_t home = (w.apt_district < w.district_count) ? w.apt_district : w.work_district;
    if (a.loc != home) { uint8_t nx = next_hop_toward(w, a.loc, home); if (nx != a.loc) { a.loc = nx; return ACT_MOVE; } }
    return ACT_REST;
}

// acquire one unit of a raw input by buying the matching commodity at a market
// (logged); if the market here is dry/unaffordable, earn or go find one.
static uint8_t gather_item(World& w, Agent& a, uint8_t item) {
    uint8_t com = item_commodity(item);
    District& here = w.districts[a.loc];
    if (com < C_COUNT && (here.services & SV_MARKET) && here.supply[com] > 0) {
        int price = price_of(w, a.loc, com);
        if ((int)a.money >= price) {
            a.money -= (uint32_t)price; record_txn(w, a, -price, TXN_BUY, com);   // names what was bought
            int s = here.supply[com] - 1; here.supply[com] = (uint8_t)(s < 0 ? 0 : s);
            if (a.inv[item] < 250) a.inv[item]++;
            return ACT_BUY;
        }
        return do_work(w, a);                            // can't afford the stock -> earn first
    }
    uint8_t nx = hop_to_service(w, a.loc, SV_MARKET);
    if (nx != a.loc) { a.loc = nx; return ACT_MOVE; }
    return do_work(w, a);                                // no stocked market reachable -> earn meanwhile
}

// FC_CRAFT (player directive): make w.craft_target. Adopt the discipline, train to
// the required tier, gather the inputs, then craft at the facility and KEEP the
// output (gear/deck) for use. Clears the target when done.
static uint8_t do_craft(World& w, Agent& a) {
    const MidTunables& T = g_mtune;
    if (w.craft_target >= CR_COUNT) { w.focus = (a.status & AF_EMPLOYED) ? FC_WORK : FC_FIND_WORK; return ACT_REST; }
    const Recipe& r = recipe_of(w.craft_target);
    District& here = w.districts[a.loc];
    if (a.job != r.job) { a.job = r.job; a.status |= AF_EMPLOYED; }        // adopt the trade
    if (skill_tier(a.skill[r.job]) < r.min_tier) {                        // train up first
        if (here.services & r.facility) { gain_skill(w, a, T.train_rate * 18); bump_need(a.need[ND_FATIGUE], T.fatigue_work); return ACT_WORK; }
        uint8_t nx = hop_to_service(w, a.loc, r.facility);
        if (nx != a.loc) { a.loc = nx; return ACT_MOVE; }
        return ACT_REST;
    }
    if (a.inv[IT_MATERIALS]  < r.in_mat)  return gather_item(w, a, IT_MATERIALS);
    if (a.inv[IT_COMPONENTS] < r.in_comp) return gather_item(w, a, IT_COMPONENTS);
    if (a.inv[IT_DATA]       < r.in_data) return gather_item(w, a, IT_DATA);
    if (!(here.services & r.facility)) {                                  // get to the workshop
        uint8_t nx = hop_to_service(w, a.loc, r.facility);
        if (nx != a.loc) { a.loc = nx; return ACT_MOVE; }
        return ACT_REST;
    }
    a.inv[IT_MATERIALS]  -= r.in_mat;                                     // consume inputs
    a.inv[IT_COMPONENTS] -= r.in_comp;
    a.inv[IT_DATA]       -= r.in_data;
    if (r.out_item == 0xFF) a.status |= AF_HAS_DECK;                      // cyberdeck -> jack-in unlocked
    else if (a.inv[r.out_item] < 250) a.inv[r.out_item]++;
    gain_skill(w, a);
    push_event(w, EV_CRAFTED, a.loc, 0, w.craft_target);
    w.craft_target = 0xFF;                                                // made it -> back to the daily loop
    w.focus = (a.status & AF_EMPLOYED) ? FC_WORK : FC_FIND_WORK;
    return ACT_WORK;
}

// FC_RENT_APT (player milestone): rent a flat in a DIFFERENT zone, adjacent to work
// (#8 — one-hop daily commute + room for encounters), 3 periods up front.
static uint8_t do_rent_apt(World& w, Agent& a) {
    const MidTunables& T = g_mtune;
    // choose the home zone: a neighbour of the work district (not work itself)
    uint8_t home_zone = a.loc;
    if (w.work_district < w.district_count) {
        const District& wd = w.districts[w.work_district];
        for (int i = 0; i < wd.deg; ++i) {
            uint8_t nb = wd.adj[i];
            if (nb != NONE8 && nb < w.district_count && nb != w.work_district) { home_zone = nb; break; }
        }
    }
    if (a.loc != home_zone) { uint8_t nx = next_hop_toward(w, a.loc, home_zone); if (nx != a.loc) { a.loc = nx; return ACT_MOVE; } }
    int deposit = (T.rent_base + w.districts[a.loc].prosperity / 8) * 3;
    if ((int)a.money >= deposit) {
        w.apt_district = a.loc;
        a.money -= (uint32_t)deposit; record_txn(w, a, -deposit, TXN_RENT);
        push_event(w, EV_RECRUIT, a.loc, 0, F_COUNT);   // "signed a lease" beat
        w.focus = (a.status & AF_EMPLOYED) ? FC_WORK : FC_FIND_WORK;
        return ACT_REST;
    }
    return (a.status & AF_EMPLOYED) ? do_work(w, a) : do_find_work(w, a);   // earn the deposit first
}

// FC_CONTRACT: travel to the gig's district, work it, collect the lump sum.
static uint8_t do_contract(World& w, Agent& a) {
    Contract& c = w.contract;
    if (!c.active) { w.focus = (a.status & AF_EMPLOYED) ? FC_WORK : FC_FIND_WORK; return ACT_REST; }
    if ((uint16_t)w.tick > c.deadline) {                 // blew the deadline
        c.active = 0; push_event(w, EV_RUMOR, a.loc, 0, c.kind);
        w.focus = (a.status & AF_EMPLOYED) ? FC_WORK : FC_FIND_WORK; return ACT_REST;
    }
    if (a.loc != c.target) { uint8_t nx = next_hop_toward(w, a.loc, c.target); if (nx != a.loc) a.loc = nx; return ACT_MOVE; }
    if (++c.progress >= c.need_ticks) {                  // delivered
        a.money += c.reward; record_txn(w, a, (int32_t)c.reward, TXN_CONTRACT);
        District& d = w.districts[c.target];
        d.danger = (uint8_t)clampi(d.danger + 4, 0, 255);   // gigs leave heat -> feeds arcs
        push_event(w, EV_BOUNTY, c.target, 0, c.kind);
        c.active = 0;
        w.focus = (a.status & AF_EMPLOYED) ? FC_WORK : FC_FIND_WORK;
    }
    return ACT_WORK;
}

// FC_FOUND_CO (player milestone): seed the company from personal cash.
static uint8_t do_found_co(World& w, Agent& a) {
    const MidTunables& T = g_mtune;
    if ((int)a.money >= T.found_threshold) {
        uint32_t reserve = (uint32_t)T.personal_reserve;
        uint32_t seed = a.money > reserve ? a.money - reserve : 0;
        a.money -= seed; record_txn(w, a, -(int32_t)seed, TXN_INVEST);   // one-time founding stake (logged, #3)
        Company& co = w.company;
        co.treasury += seed;
        if (co.tier < CT_SOLO) co.tier = CT_SOLO;
        push_event(w, EV_RECRUIT, a.loc, 0, F_CREW);
        w.focus = FC_RUN_CO;
        return ACT_WORK;
    }
    return (a.status & AF_EMPLOYED) ? do_work(w, a) : do_find_work(w, a);    // raise the stake first
}

// FC_RUN_CO: keep earning personally; sweep the day's surplus into the company ONCE
// a day (18:00) as a single, NAMED, logged move -- so personal cash builds visibly
// through the day instead of silently bleeding every tick (#3/#9). The company also
// compounds on its own (company_step), present or not.
static uint8_t do_run_co(World& w, Agent& a) {
    const MidTunables& T = g_mtune;
    if ((int)(w.tick % 24) == 18 && (int)a.money > T.personal_reserve + 50) {
        uint32_t invest = a.money - (uint32_t)T.personal_reserve;
        a.money -= invest; record_txn(w, a, -(int32_t)invest, TXN_INVEST);
        uint64_t t = (uint64_t)w.company.treasury + invest;
        w.company.treasury = (uint32_t)(t > 4000000000ULL ? 4000000000ULL : t);
        return ACT_WORK;
    }
    return (a.status & AF_EMPLOYED) ? do_work(w, a) : (uint8_t)ACT_REST;
}

// the protagonist's self-driving policy (§1 agency model) — a focus dispatcher
// with a survival floor and a danger interrupt.
static uint8_t career_action(World& w, Agent& a) {
    const MidTunables& T = g_mtune;
    District& here = w.districts[a.loc];

    // survival floor: a pressing vital this tick -> behave like an NPC (eat/drink)
    if (a.need[ND_HUNGER] > T.buy_thresh || a.need[ND_THIRST] > T.buy_thresh)
        return npc_action(w, a);

    // INJURY OFF-RAMP (so a wound isn't a death spiral, and death stays a choice):
    // a cautious avatar gets patched up before doing anything else (clinic if it can
    // pay, else lie low and heal slowly). A RECKLESS one (risk >= care_risk_max) skips
    // this and pushes on through the wound — staying exposed, which is how the
    // risk-takers die. Death is thus a consequence of the player's risk dial.
    if ((a.status & AF_INJURED) && (int)w.directive.risk < T.care_risk_max) {
        if (here.services & SV_CLINIC) {
            if ((int)a.money >= T.care_cost) {
                a.money -= (uint32_t)T.care_cost; record_txn(w, a, -T.care_cost, TXN_CARE);
                a.status &= (uint8_t)~AF_INJURED;            // patched up; resume focus below
            } else return ACT_REST;                          // can't pay -> rest and heal slowly
        } else if ((int)a.money >= T.care_cost) {
            uint8_t nx = hop_to_service(w, a.loc, SV_CLINIC);
            if (nx != a.loc) { a.loc = nx; return ACT_MOVE; }
            return ACT_REST;
        } else {
            return ACT_REST;                                 // broke + hurt -> lie low, heal slowly
        }
    }

    // INTERRUPT-AND-RETURN (the arc engine): a dangerous block or a threat here
    // pulls the avatar off its focus to flee; the prior focus is stashed and
    // restored once it clears.
    bool threat_here = false;
    for (int i = 0; i < w.threat_count; ++i)
        if (w.threats[i].active && w.threats[i].district == a.loc) { threat_here = true; break; }
    // how much danger the avatar tolerates before bailing scales with the risk dial:
    // cautious flees early (stays safe), reckless lingers in the bad blocks (more
    // muggings -> the risk-takers' deaths). A co-located threat is always fled.
    int flee_at = clampi(T.refugee_danger + ((int)w.directive.risk - 128) / 2, 25, 140);
    if (here.danger >= flee_at || threat_here) {
        if (w.interrupt_focus == NONE8) w.interrupt_focus = w.focus;
        if (flee_to_safer(w, a)) return ACT_MOVE;
        return ACT_REST;
    }
    if (w.interrupt_focus != NONE8) { w.focus = w.interrupt_focus; w.interrupt_focus = NONE8; }

    switch (w.focus) {
        case FC_FIND_WORK: return do_find_work(w, a);
        case FC_RENT_APT:  return do_rent_apt(w, a);
        case FC_CONTRACT:  return do_contract(w, a);
        case FC_FOUND_CO:  return do_found_co(w, a);
        case FC_RUN_CO:    return do_run_co(w, a);
        case FC_CRAFT:     return do_craft(w, a);
        case FC_SURVIVE:   return npc_action(w, a);
        case FC_WORK:
        case FC_SAVE:
        default:           return do_work(w, a);
    }
}

const char* focus_name(uint8_t f) {
    static const char* n[FC_COUNT] = {
        "Survive", "Find work", "Rent an apartment", "Work", "Run a contract",
        "Save up", "Found a company", "Run the company", "Craft"
    };
    return f < FC_COUNT ? n[f] : "?";
}
const char* contract_kind_name(uint8_t k) {
    static const char* n[CK_COUNT] = { "courier run", "fetch job", "sabotage job", "clear-out job", "guard detail" };
    return k < CK_COUNT ? n[k] : "job";
}
const char* sector_name(uint8_t s) {
    static const char* n[SEC_COUNT] = {
        "Unfounded", "Fabrication", "Cyberware", "Chemtech", "Data-brokering",
        "Drone Ops", "Protection", "Smuggling"
    };
    return s < SEC_COUNT ? n[s] : "?";
}
// lowest set-bit index of a Service mask (1<<n -> n), or 0xFF if none
static uint8_t svc_bit_index(uint16_t mask) {
    for (uint8_t i = 0; i < 16; ++i) if (mask & (1u << i)) return i;
    return 0xFF;
}
uint8_t avatar_target_poi(const World& w) {
    const Agent& a = w.agents[0];
    switch (a.activity) {
        case ACT_WORK: {
            uint16_t f = (a.job >= J_CONSTRUCTION && a.job < J_COUNT) ? job_facility(a.job) : 0;
            uint8_t bi = svc_bit_index(f);
            return bi != 0xFF ? bi : (uint8_t)0;   // fall back to the job board (bit 0)
        }
        case ACT_BUY:     return 1;   // SV_MARKET
        case ACT_SEEKJOB: return 0;   // SV_JOB_BOARD
        default:          return 0xFF;  // home / wander
    }
}

// per-day company P&L (#12) — sector modulates gross; SEC_NONE keeps base math.
CompanyFinance company_finance(const World& w) {
    const MidTunables& T = g_mtune;
    const Company& co = w.company;
    CompanyFinance f;
    if (co.emp_count == 0 && co.treasury == 0) return f;   // unfounded -> all zero
    int tier = co.tier < CT_COUNT ? co.tier : CT_COUNT - 1;
    uint64_t labor = (uint64_t)co.emp_count * T.per_emp * T.tier_mult[tier];
    labor += labor * (uint64_t)(co.asset_count * T.asset_bonus_pct) / 100;
    uint64_t capital = (uint64_t)co.treasury * (uint64_t)T.tier_return[tier] / 10000;
    int sp = (co.sector < SEC_COUNT) ? T.sector_rev_pct[co.sector] : 100;
    uint64_t gross = (labor + capital) * (uint64_t)sp / 100;
    if (gross > 4000000000ULL) gross = 4000000000ULL;
    f.gross   = (uint32_t)gross;
    f.payroll = (uint32_t)((uint64_t)co.emp_count * T.emp_payroll);
    f.upkeep  = (uint32_t)((uint64_t)co.asset_count * T.asset_upkeep);
    f.net     = (int32_t)((int64_t)f.gross - (int64_t)f.payroll - (int64_t)f.upkeep);
    return f;
}

void company_step(World& w) {
    const MidTunables& T = g_mtune;
    Company& co = w.company;
    if (co.emp_count == 0 && co.treasury == 0) return; // not founded

    int tier = co.tier < CT_COUNT ? co.tier : CT_COUNT - 1;

    CompanyFinance f = company_finance(w);
    uint64_t cost = (uint64_t)f.payroll + (uint64_t)f.upkeep;
    uint64_t t = (uint64_t)co.treasury + (uint64_t)f.gross;
    t = (cost > t) ? 0 : t - cost;
    if (t > 4000000000ULL) t = 4000000000ULL;
    co.treasury = (uint32_t)t;

    // headcount directive (#13): target_emp == 0 -> auto-fill capacity; otherwise
    // hire/fire toward the player's target (bounded by this tier's cap).
    int desired_emp = (co.target_emp > 0 && co.target_emp <= T.emp_cap[tier]) ? co.target_emp : T.emp_cap[tier];
    if (co.emp_count > desired_emp) co.emp_count--;   // a layoff (one per day)

    // reinvest a slice (keep ~80% compounding): grow toward the desired headcount
    bool favor_assets = (w.directive.ambition == AMB_TERRITORY);
    int asset_cap = tier * 4;   // assets only pay off from CREW up
    for (int guard = 0; guard < 6; ++guard) {
        int hire_cost  = T.hire_cost0 * (1 + co.emp_count);
        int asset_cost = T.asset_cost0 * (1 + co.asset_count);
        bool can_hire  = co.emp_count < desired_emp && (int)co.treasury >= hire_cost * 5;
        bool can_asset = co.asset_count < asset_cap && (int)co.treasury >= asset_cost * 5;
        if (favor_assets && can_asset) { co.treasury -= (uint32_t)asset_cost; co.asset_count++; }
        else if (can_hire) { co.treasury -= (uint32_t)hire_cost; co.emp_count++; }
        else if (can_asset) { co.treasury -= (uint32_t)asset_cost; co.asset_count++; }
        else break;
    }

    // tier transitions (up only)
    while (co.tier + 1 < CT_COUNT && co.treasury >= T.tier_thresh[co.tier + 1]) co.tier++;
}

// the basic NPC subsistence ladder — also the avatar's survival floor
static uint8_t npc_action(World& w, Agent& a) {
    const MidTunables& T = g_mtune;
    District& here = w.districts[a.loc];

    uint8_t vital = a.need[ND_THIRST] >= a.need[ND_HUNGER] ? ND_THIRST : ND_HUNGER;
    uint8_t vcom  = need_commodity(vital);
    uint8_t action = ACT_REST;

    if (a.need[vital] > T.buy_thresh) {
        int price = price_of(w, a.loc, vcom);
        bool market_here = (here.services & SV_MARKET) != 0;
        bool stocked     = here.supply[vcom] > 0;
        if (market_here && stocked && (int)a.money >= price) {
            // CONSUME: pressure down, money + supply down, a little local prosperity up
            a.money -= (uint32_t)price;
            record_txn(w, a, -price, TXN_BUY, vcom);   // names the vital (food/water)
            bump_need(a.need[vital], -T.consume_relief);
            int s = here.supply[vcom] - T.consume_supply;
            here.supply[vcom] = (uint8_t)(s < 0 ? 0 : s);
            if (here.prosperity < 100) here.prosperity++;
            action = ACT_BUY;
        } else if ((int)a.money < price) {
            // can't afford -> need income
            if (a.status & AF_EMPLOYED) { int wg = wage_of(w, a.loc, a); a.money += (uint32_t)wg; record_txn(w, a, wg, TXN_WAGE, a.job); bump_need(a.need[ND_FATIGUE], T.fatigue_work); action = ACT_WORK; }
            else if (here.services & SV_JOB_BOARD) { a.job = (uint8_t)w.rng.between(J_CONSTRUCTION, J_INFRA); a.status |= AF_EMPLOYED; action = ACT_SEEKJOB; }
            else { uint8_t nx = hop_to_service(w, a.loc, SV_JOB_BOARD); if (nx != a.loc) { a.loc = nx; action = ACT_MOVE; } else { action = ACT_REST; } }
        } else {
            // have money but no stocked market here -> go find one
            uint8_t nx = hop_to_service(w, a.loc, SV_MARKET);
            if (nx != a.loc) { a.loc = nx; action = ACT_MOVE; }
            else { action = ACT_REST; } // market here but out of stock; wait for regen
        }
    } else if ((int)a.money < T.reserve) {
        // build a buffer for the next purchases
        if (a.status & AF_EMPLOYED) { int wg = wage_of(w, a.loc, a); a.money += (uint32_t)wg; record_txn(w, a, wg, TXN_WAGE, a.job); bump_need(a.need[ND_FATIGUE], T.fatigue_work); gain_skill(w, a); action = ACT_WORK; }
        else if (here.services & SV_JOB_BOARD) { a.job = (uint8_t)w.rng.between(J_CONSTRUCTION, J_INFRA); a.status |= AF_EMPLOYED; action = ACT_SEEKJOB; }
        else { uint8_t nx = hop_to_service(w, a.loc, SV_JOB_BOARD); if (nx != a.loc) { a.loc = nx; action = ACT_MOVE; } else { action = ACT_REST; } }
    } else {
        action = ACT_REST;
    }
    return action;
}

// per-tick: needs decay (common) -> policy (player=career / NPC=ladder) ->
// recovery / starvation / mood (common). Deterministic via w.rng.
static void agent_step(World& w, int idx) {
    const MidTunables& T = g_mtune;
    Agent& a = w.agents[idx];
    if (!(a.status & AF_ALIVE)) return;

    District& here = w.districts[a.loc];

    // --- needs decay (synths sip power, not food/water) ---------------------
    int hr = T.hunger_rate, tr = T.thirst_rate + (w.weather > 0 ? 2 : 0); // heatwave (#33)
    if (a.kind == AK_SYNTH) { hr = hr / 3; tr = tr / 3; }
    bump_need(a.need[ND_HUNGER], hr);
    bump_need(a.need[ND_THIRST], tr);
    bump_need(a.need[ND_SOCIAL], T.social_rate);
    bump_need(a.need[ND_SAFETY], here.danger > 40 ? 2 : -2);
    bump_need(a.need[ND_STRESS], here.danger / 16 - T.stress_relief);

    // self-medicate: owned/crafted chems blunt stress, at the cost of dependency
    if (a.need[ND_STRESS] > 180 && a.inv[IT_CHEMS] > 0) {
        a.inv[IT_CHEMS]--; bump_need(a.need[ND_STRESS], -120); bump_need(a.need[ND_ADDICTION], 15);
    }

    uint8_t action = (a.status & AF_PLAYER) ? career_action(w, a) : npc_action(w, a);

    if (action == ACT_REST) { bump_need(a.need[ND_FATIGUE], -T.fatigue_rest); bump_need(a.need[ND_SOCIAL], -4); }

    // --- starvation: a maxed vital hurts; sustained -> injury -> death -------
    if (a.need[ND_HUNGER] >= T.starve || a.need[ND_THIRST] >= T.starve) {
        bump_need(a.need[ND_STRESS], 8);
        if (a.status & AF_INJURED) { if (w.rng.chance(8)) { a.status &= (uint8_t)~AF_ALIVE; push_event(w, EV_DEATH, a.loc, (uint8_t)idx, /*starvation*/0); } }
        else if (w.rng.chance(20)) a.status |= AF_INJURED;
    } else if (a.status & AF_INJURED) {
        if (w.rng.chance(10)) a.status &= (uint8_t)~AF_INJURED; // recover when fed
    }

    // --- mood = inverse of worst pressures ---------------------------------
    int worst = a.need[ND_HUNGER];
    for (int nd = 0; nd < ND_COUNT; ++nd) if (a.need[nd] > worst) worst = a.need[nd];
    a.mood = (uint8_t)clampi(255 - worst, 0, 255);

    a.activity = action;
}

// ============================================================================
// Phase 4: territory, hazards, decay, pulses, combat & threats
// ============================================================================

const char* threat_name(uint8_t k) {
    static const char* n[TK_COUNT] = {
        "feral drone", "security drone", "kill-drone swarm", "construction mech",
        "sewer leviathan", "rogue AI", "mutant colony", "black-ops team"
    };
    return k < TK_COUNT ? n[k] : "?";
}
const char* event_name(uint8_t k) {
    static const char* n[EV_COUNT] = {
        "-", "combat", "death", "turf flip", "raid", "threat spawn", "threat defeat",
        "refugee", "extortion", "bounty", "recruit", "market day", "rumor", "collapse",
        "shortage", "heatwave", "lockdown", "riot",
        "jack-in", "heist", "flatline", "net-ally", "newcomer", "population shift"
    };
    return k < EV_COUNT ? n[k] : "?";
}

static void push_event(World& w, uint8_t kind, uint8_t node, uint8_t agent, uint8_t data) {
    Event& e = w.events[w.event_head];
    e.kind = kind; e.node = node; e.agent = agent; e.data = data; e.tick = (uint16_t)w.tick;
    w.event_head = (uint8_t)((w.event_head + 1) % EVMAX);
    if (w.event_count < EVMAX) w.event_count++;
}

static void add_scar(Agent& a, uint8_t node, uint8_t kind, int8_t val) {
    for (int k = SCARMAX - 1; k > 0; --k) a.scar[k] = a.scar[k - 1];
    a.scar[0].node = node; a.scar[0].kind = kind; a.scar[0].valence = val;
}

// move an agent to the adjacent district with the lowest danger (flee)
static bool flee_to_safer(World& w, Agent& a) {
    const District& d = w.districts[a.loc];
    int best = -1, bestdanger = w.districts[a.loc].danger;
    for (int k = 0; k < d.deg; ++k) {
        uint8_t nb = d.adj[k];
        if (nb == NONE8 || nb >= w.district_count) continue;
        if (w.districts[nb].danger < bestdanger) { bestdanger = w.districts[nb].danger; best = nb; }
    }
    if (best < 0) return false;
    a.loc = (uint8_t)best; a.status |= AF_FLEEING;
    return true;
}

// ---- combat (§5.1) — abstract, stat-based ----------------------------------
int combat_atk(const World& w, const Agent& a) {
    (void)w;
    int v = 10 + a.trait[TR_AGGRESSION] / 16;                 // 10..25 temperament
    v += a.inv[IT_WEAPONS] * g_mtune.weapon_grade;            // gear is the proficiency
    v += a.inv[IT_IMPLANTS] * g_mtune.implant_grade;
    v += a.mood / 64;
    if (a.status & AF_INJURED) v = v * 2 / 3;
    return v;
}
int combat_def(const World& w, const Agent& a) {
    (void)w;
    int v = 8 + a.trait[TR_AGGRESSION] / 32;
    v += a.inv[IT_ARMOR] * g_mtune.armor_grade;
    v += a.inv[IT_IMPLANTS] * g_mtune.implant_grade / 2;
    if (a.status & AF_INJURED) v = v * 2 / 3;
    return v;
}

bool avatar_fight_escalates(const World& w, int foe_power) {
    const Agent& p = w.agents[0];
    int mine = combat_atk(w, p);
    if (p.status & AF_INJURED) return foe_power * 3 >= mine * 2; // hurt -> wary sooner
    return foe_power * 4 >= mine * 5;                            // foe >= ~1.25x our power
}

// resolve a fight between two agents; applies loot, grudge, scar, danger, events
static void resolve_combat(World& w, int ai, int di) {
    Agent& A = w.agents[ai]; Agent& D = w.agents[di];
    if (!(A.status & AF_ALIVE) || !(D.status & AF_ALIVE)) return;
    int sa = combat_atk(w, A) + (int)w.rng.range(8) - combat_def(w, D);
    int sd = combat_atk(w, D) + (int)w.rng.range(8) - combat_def(w, A);
    int wi = (sa >= sd) ? ai : di;
    int li = (sa >= sd) ? di : ai;
    int margin = sa >= sd ? sa - sd : sd - sa;
    Agent& W = w.agents[wi]; Agent& L = w.agents[li];

    uint32_t loot = L.money / 2; L.money -= loot; W.money += loot;
    record_txn(w, W, (int32_t)loot, TXN_LOOT);
    record_txn(w, L, -(int32_t)loot, TXN_ROBBED);
    if (L.faction < F_COUNT) {
        int gi = (W.faction < F_COUNT) ? (int)W.faction : (int)F_COUNT;
        int8_t& g = w.factions[L.faction].grudge[gi]; if (g < 120) g = (int8_t)(g + 2);
    }
    add_scar(L, L.loc, /*kind=combat*/1, -20);
    District& d = w.districts[L.loc];
    d.danger = (uint8_t)clampi(d.danger + 3, 0, 255);
    d.hazard[HZ_ALARM] = (uint8_t)clampi(d.hazard[HZ_ALARM] + 5, 0, 255);
    push_event(w, EV_COMBAT, L.loc, (uint8_t)li, 0);

    int dc = clampi(g_mtune.combat_death_pct * (margin + 4) / 20, 2, 90);
    bool vulnerable = (L.status & AF_INJURED) || margin > 18;
    if (vulnerable && w.rng.chance(dc)) {
        L.status &= (uint8_t)~AF_ALIVE;
        if (W.inv[IT_WEAPONS] < 250) W.inv[IT_WEAPONS]++;   // strip the body for gear
        push_event(w, EV_DEATH, L.loc, (uint8_t)li, /*by combat*/1);
    } else {
        L.status |= AF_INJURED;
    }
}

// ---- threats: spawn, terrorize, be driven off (§5.1) -----------------------
static int threat_atk(const Threat& t) { return 8 + t.power * 4; }
static int threat_def(const Threat& t) { return 6 + t.power * 3; }

static void spawn_threat(World& w) {
    if (w.threat_count >= g_mtune.threat_cap) return;   // don't overrun the city
    // prefer a dangerous / contested district
    uint8_t best = (uint8_t)w.rng.range(w.district_count);
    for (int tries = 0; tries < 3; ++tries) {
        uint8_t c = (uint8_t)w.rng.range(w.district_count);
        if (w.districts[c].danger > w.districts[best].danger) best = c;
    }
    Threat& t = w.threats[w.threat_count];
    t.power = (uint8_t)w.rng.between(1, 10);
    uint8_t kind;   // kind roughly tracks power; high power = a megabeast
    if (t.power >= 8)      kind = w.rng.chance(50) ? TK_ROGUE_AI_BODY : TK_CONSTRUCT_MECH;
    else if (t.power >= 5) kind = w.rng.chance(50) ? TK_SEWER_LEVIATHAN : TK_MUTANT_COLONY;
    else if (t.power >= 3) kind = TK_KILLDRONE_SWARM;
    else                   kind = w.rng.chance(50) ? TK_FERAL_DRONE : TK_SEC_DRONE;
    t.kind = kind;
    t.district = best;
    t.behavior = (uint8_t)(t.power >= g_mtune.megathreat_min_power ? TB_AGGRESSIVE : TB_TERRITORIAL);
    t.hp = (uint8_t)clampi(t.power * g_mtune.threat_hp_mult, 1, 255);
    t.active = 1;
    w.threat_count++;
    w.districts[best].danger = (uint8_t)clampi(w.districts[best].danger + t.power * 4, 0, 255);
    push_event(w, EV_THREAT_SPAWN, best, NONE8, t.kind);
}

static void threats_step(World& w) {
    for (int ti = 0; ti < w.threat_count; ++ti) {
        Threat& t = w.threats[ti];
        if (!t.active) continue;
        uint8_t dist = t.district;

        // 1) it mauls a few locals — mostly injuries + displacement, rarely death
        int defenders_dmg = 0, scanned = 0, mauled = 0;
        for (int i = 0; i < w.agent_count && scanned < 16; ++i) {
            Agent& a = w.agents[i];
            if (!(a.status & AF_ALIVE) || a.loc != dist) continue;
            ++scanned;
            // the brave (or the avatar) fight back
            bool fights = (a.status & AF_PLAYER) || a.trait[TR_AGGRESSION] > 150 || a.faction == w.districts[dist].owner;
            if (fights) defenders_dmg += clampi(combat_atk(w, a) - threat_def(t), 0, 60);
            // only a handful get caught each day
            if (mauled >= 6) continue;
            int hit = threat_atk(t) + (int)w.rng.range(6) - combat_def(w, a);
            if (hit <= 0) continue;
            ++mauled;
            bool wasInjured = (a.status & AF_INJURED) != 0;
            a.status |= AF_INJURED;
            if (wasInjured && !(a.status & AF_PLAYER) && w.rng.chance(clampi(t.power * 2, 2, 28))) {
                a.status &= (uint8_t)~AF_ALIVE;
                push_event(w, EV_DEATH, dist, (uint8_t)i, /*by threat*/2);
            } else if (flee_to_safer(w, a)) {
                push_event(w, EV_REFUGEE, dist, (uint8_t)i, t.kind);
            }
        }

        // 3) driven off? defenders chip it AND it runs out of steam (always transient)
        int attrition = defenders_dmg + clampi(t.power / 2 + 1, 1, 12);
        t.hp = (uint8_t)clampi((int)t.hp - attrition, 0, 255);
        if (t.hp == 0) {
            t.active = 0;
            w.districts[dist].danger = (uint8_t)clampi(w.districts[dist].danger - t.power * 4, 0, 255);
            push_event(w, EV_THREAT_DEFEAT, dist, NONE8, t.kind);     // a street-legend beat
            continue;
        }
        // 4) aggressive megathreats roam to a neighbor
        if (t.behavior == TB_AGGRESSIVE && w.rng.chance(30)) {
            const District& d = w.districts[dist];
            if (d.deg > 0) {
                uint8_t nb = d.adj[w.rng.range(d.deg)];
                if (nb != NONE8 && nb < w.district_count) {
                    w.districts[dist].danger = (uint8_t)clampi(w.districts[dist].danger - t.power * 2, 0, 255);
                    t.district = nb;
                    w.districts[nb].danger = (uint8_t)clampi(w.districts[nb].danger + t.power * 4, 0, 255);
                }
            }
        }
    }
    // compact dead threats
    int n = 0;
    for (int i = 0; i < w.threat_count; ++i) if (w.threats[i].active) { if (n != i) w.threats[n] = w.threats[i]; ++n; }
    w.threat_count = (uint8_t)n;
}

// ---- territory & factions (§3.C) -------------------------------------------
static void territory_step(World& w) {
    const MidTunables& T = g_mtune;
    // influence diffusion toward neighbour average, then decay
    for (int i = 0; i < w.district_count; ++i) {
        District& d = w.districts[i];
        for (int f = 0; f < F_COUNT; ++f) {
            int sum = 0, cnt = 0;
            for (int k = 0; k < d.deg; ++k) {
                uint8_t nb = d.adj[k];
                if (nb == NONE8 || nb >= w.district_count) continue;
                sum += w.districts[nb].influence[f]; ++cnt;
            }
            int avg = cnt ? sum / cnt : d.influence[f];
            int v = d.influence[f] + (avg - d.influence[f]) * T.influence_decay / 16;
            if (d.owner == f && v < 100) v += 1;                 // owners entrench
            d.influence[f] = (uint8_t)clampi(v, 0, 100);
        }
        // owner recompute + flip
        int best = -1, bestf = F_COUNT, second = -1;
        for (int f = 0; f < F_COUNT; ++f) {
            int v = d.influence[f];
            if (v > best) { second = best; best = v; bestf = f; }
            else if (v > second) second = v;
        }
        uint8_t newowner = (best >= 40 && best - second >= T.turf_lead) ? (uint8_t)bestf : (uint8_t)F_COUNT;
        if (newowner != d.owner) {
            uint8_t old = d.owner;
            d.owner = newowner;
            d.danger = (uint8_t)clampi(d.danger + 8, 0, 255);
            if (old < F_COUNT && newowner < F_COUNT) {           // a real takeover -> grudge
                int8_t& g = w.factions[old].grudge[newowner]; if (g < 120) g = (int8_t)(g + 6);
            }
            push_event(w, EV_TURF_FLIP, (uint8_t)i, NONE8, newowner);
        }
        // extortion: a gang-owned, prosperous block is taxed
        if (d.owner < F_COUNT && d.prosperity > 20 && w.rng.chance(8)) {
            d.prosperity = (uint8_t)(d.prosperity - 1);
            w.factions[d.owner].treasury += 50;
            push_event(w, EV_EXTORT, (uint8_t)i, NONE8, d.owner);
        }
        // raids: a contested district sometimes erupts (two rival members brawl)
        if (d.owner < F_COUNT && w.rng.chance(T.raid_pct)) {
            int agg = -1, def = -1;
            for (int j = 0; j < w.agent_count; ++j) {
                Agent& a = w.agents[j];
                if (!(a.status & AF_ALIVE) || a.loc != (uint8_t)i) continue;
                if (a.faction < F_COUNT && a.faction != d.owner && agg < 0) agg = j;
                else if (a.faction == d.owner && def < 0) def = j;
            }
            if (agg >= 0 && def >= 0) { resolve_combat(w, agg, def); push_event(w, EV_RAID, (uint8_t)i, NONE8, d.owner); }
        }
    }
}

// ---- hazards (§3.D) --------------------------------------------------------
static void hazard_step(World& w) {
    const MidTunables& T = g_mtune;
    for (int i = 0; i < w.district_count; ++i) {
        District& d = w.districts[i];
        for (int h = 0; h < HZ_COUNT; ++h) {
            // diffuse a little to neighbours
            if (d.hazard[h] > T.hazard_spread) {
                for (int k = 0; k < d.deg; ++k) {
                    uint8_t nb = d.adj[k];
                    if (nb == NONE8 || nb >= w.district_count) continue;
                    w.districts[nb].hazard[h] = (uint8_t)clampi(w.districts[nb].hazard[h] + T.hazard_spread / 2, 0, 255);
                }
            }
            d.hazard[h] = (uint8_t)clampi(d.hazard[h] - T.hazard_decay, 0, 255);
        }
        // danger mean-reverts toward the district's intrinsic level (spikes from
        // combat/threats fade, but a slum stays a slum) — keeps the warzone transient
        if (d.danger > d.danger_base)
            d.danger = (uint8_t)clampi(d.danger - T.danger_decay, d.danger_base, 255);
        // disease in the crowded + sick: nudges danger; structural collapse is rare
        if (d.population > 30 && d.hazard[HZ_DISEASE] > 30 && w.rng.chance(10))
            d.danger = (uint8_t)clampi(d.danger + 2, 0, 255);
        if ((d.type == DT_METRO || d.type == DT_TOXIC || d.type == DT_UNDERCITY) && d.hazard[HZ_COLLAPSE] > 60 && w.rng.chance(3)) {
            d.danger = (uint8_t)clampi(d.danger + 10, 0, 255);
            push_event(w, EV_COLLAPSE, (uint8_t)i, NONE8, 0);
        }
        // a heavily-alarmed district can spawn a security drone (rare)
        if (d.hazard[HZ_ALARM] > 80 && w.threat_count < T.threat_cap && w.rng.chance(2)) {
            Threat& t = w.threats[w.threat_count++];
            t.kind = TK_SEC_DRONE; t.power = (uint8_t)w.rng.between(1, 3); t.district = (uint8_t)i;
            t.behavior = TB_TERRITORIAL; t.hp = (uint8_t)(t.power * T.threat_hp_mult); t.active = 1;
            push_event(w, EV_THREAT_SPAWN, (uint8_t)i, NONE8, t.kind);
        }
    }
}

// ---- decay (§3.E) ----------------------------------------------------------
static void decay_step(World& w) {
    for (int i = 0; i < w.agent_count; ++i) {
        Agent& a = w.agents[i];
        if (!(a.status & AF_ALIVE)) continue;
        // gear wears, food spoils, implants denature — small chance per class
        if (a.inv[IT_FOOD] > 0 && w.rng.chance(20)) a.inv[IT_FOOD]--;
        if (a.inv[IT_WEAPONS] > 0 && w.rng.chance(4)) a.inv[IT_WEAPONS]--;
        if (a.inv[IT_IMPLANTS] > 0 && w.rng.chance(2)) { a.inv[IT_IMPLANTS]--; bump_need(a.need[ND_CYBERWARE], 20); }
    }
}

// ---- opportunity pulses (§3.G) ---------------------------------------------
static void pulse_step(World& w) {
    // market day: a random district booms (prosperity up, residents relieved)
    if (w.rng.chance(30)) {
        uint8_t c = (uint8_t)w.rng.range(w.district_count);
        if (w.districts[c].services & SV_MARKET) {
            w.districts[c].prosperity = (uint8_t)clampi(w.districts[c].prosperity + 4, 0, 100);
            for (int i = 0; i < w.agent_count; ++i) if ((w.agents[i].status & AF_ALIVE) && w.agents[i].loc == c)
                bump_need(w.agents[i].need[ND_SOCIAL], -20);
            push_event(w, EV_MARKET_DAY, c, NONE8, 0);
        }
    }
    // corp bounty posting
    if (w.rng.chance(12)) push_event(w, EV_BOUNTY, (uint8_t)w.rng.range(w.district_count), NONE8, 0);
    // recruiter: an unaffiliated agent is pulled into a faction
    if (w.rng.chance(15)) {
        for (int tries = 0; tries < 4; ++tries) {
            int j = (int)w.rng.range(w.agent_count);
            Agent& a = w.agents[j];
            if (j != 0 && (a.status & AF_ALIVE) && a.faction == F_COUNT) {
                a.faction = (uint8_t)w.rng.range(F_COUNT);
                push_event(w, EV_RECRUIT, a.loc, (uint8_t)j, a.faction);
                break;
            }
        }
    }
    if (w.rng.chance(20)) push_event(w, EV_RUMOR, (uint8_t)w.rng.range(w.district_count), NONE8, 0);
}

// ---- unrest: shortages, lockdowns, riots (the §3.1 chain ingredients) ------
static void unrest_step(World& w) {
    for (int i = 0; i < w.district_count; ++i) {
        District& d = w.districts[i];
        // shortage: a vital commodity has run dry (biogel/water/food/chems)
        static const uint8_t vital_c[4] = { C_WATER, C_FOOD, C_BIOTECH, C_CHEMS };
        for (int vc = 0; vc < 4; ++vc)
            if (d.supply[vital_c[vc]] == 0 && w.rng.chance(50))
                push_event(w, EV_SHORTAGE, (uint8_t)i, NONE8, vital_c[vc]);
        // lockdown: a heavily-alarmed block gets sealed (clinic-lockdown beat)
        if (d.hazard[HZ_ALARM] > 85 && w.rng.chance(12)) {
            d.danger = (uint8_t)clampi(d.danger + 4, 0, 255);
            push_event(w, EV_LOCKDOWN, (uint8_t)i, NONE8, 0);
        }
        // riot: a dry, crowded, tense block boils over -> spikes danger +
        // destabilizes the owner (the water-ration riot beat). Localized, so it
        // doesn't torch the whole city.
        bool dry = (d.supply[C_WATER] == 0 || d.supply[C_FOOD] == 0);
        if (dry && d.population > 14 && d.danger > 25 && w.rng.chance(22)) {
            d.danger = (uint8_t)clampi(d.danger + 8, 0, 255);
            if (d.owner < F_COUNT)
                d.influence[d.owner] = (uint8_t)clampi(d.influence[d.owner] - 12, 0, 100);
            push_event(w, EV_RIOT, (uint8_t)i, NONE8, d.population);
        }
    }
}

// ---- Phase 6: cyberspace = a headless CyberHack run (§5.2) ------------------
static uint8_t pick_persona(const Agent& a) {
    bool desperate   = (a.status & AF_IN_DEBT) || a.need[ND_HUNGER] > 180 ||
                       a.need[ND_THIRST] > 180 || a.money < 30;
    bool comfortable = a.money > 1000;
    bool greedy      = a.trait[TR_GREED] > 160;
    if (desperate)   return cyber::P_RECKLESS;
    if (comfortable) return cyber::P_CAUTIOUS;
    if (greedy)      return cyber::P_OPPORTUNIST;
    return cyber::P_LOYALIST;
}
int net_target_count(const World& w) {
    int n = 0;
    for (int i = 0; i < w.district_count; ++i) if (w.districts[i].services & SV_DATAVAULT) ++n;
    return n;
}
const char* outcome_name(uint8_t o) {
    return o == cyber::O_EXTRACTED ? "extracted" : o == cyber::O_DIED ? "flatlined" : "running";
}
static int count_allies(const cyber::Legends& L) {
    int n = 0;
    for (int k = 0; k < L.named_count && k < cyber::MAX_NAMED; ++k)
        if (L.named[k].status == cyber::NS_ALLIED) ++n;
    return n;
}

JackResult jack_in(World& w, int ai) {
    JackResult res;
    if (ai < 0 || ai >= w.agent_count) return res;
    Agent& a = w.agents[ai];
    if (!(a.status & AF_ALIVE) || !(a.status & AF_HAS_DECK)) return res;

    // pick a data target (a datacenter); remote — no travel needed to jack in
    uint8_t targets[MAX_DISTRICTS]; int nt = 0;
    for (int i = 0; i < w.district_count; ++i)
        if (w.districts[i].services & SV_DATAVAULT) targets[nt++] = (uint8_t)i;
    if (nt == 0) return res;
    uint8_t target = targets[w.rng.range((uint32_t)nt)];
    res.ran = true; res.target = target;

    // the matrix remembers this target across jack-ins (persistent legends)
    int slot = target % MAX_NET;
    if (w.net_target[slot] != target) {
        w.net_legends[slot] = cyber::Legends{};
        w.net_target[slot] = target;
        if (slot + 1 > w.net_count) w.net_count = (uint8_t)(slot + 1);
    }
    cyber::Legends& leg = w.net_legends[slot];
    int allies_before = count_allies(leg);

    // run the dive headless: the personality reflects the jacker's situation,
    // and ai_decide drives every spike (NPC jacks are invisible background runs).
    uint8_t persona = pick_persona(a);
    cyber::Sim run;
    run.start(w.districts[target].seed, w.rng.next(), persona, &leg);
    run.set_auto_combat(true);
    cyber::Rng pr; pr.seed(w.rng.next());
    int guard = 0;
    while (run.running() && guard++ < 8000) {
        if (run.advance() == cyber::AR_DECISION)
            run.choose(cyber::ai_decide(persona, run.state(), run.decision(), pr));
    }
    run.update_legends(leg);
    const cyber::RunState& r = run.state();
    res.shards = r.shards; res.outcome = r.outcome; res.corruption = r.corruption;

    // ---- feed results back as EVENTS so cyberspace drives meatspace arcs ----
    a.money += r.shards;
    record_txn(w, a, (int32_t)r.shards, TXN_HEIST);
    bump_need(a.need[ND_STRESS], r.corruption / 4);
    push_event(w, EV_JACKIN, target, (uint8_t)ai, r.outcome);
    uint8_t owner = w.districts[target].owner;
    if (owner < F_COUNT) {                                   // you robbed their ice
        int gi = (a.faction < F_COUNT) ? (int)a.faction : (int)F_COUNT;
        int8_t& g = w.factions[owner].grudge[gi]; if (g < 120) g = (int8_t)(g + 3);
    }
    if (r.outcome == cyber::O_DIED) {                        // flatline
        res.flatlined = true;
        a.status |= AF_INJURED;
        bump_need(a.need[ND_CYBERWARE], 40);
        bump_need(a.need[ND_STRESS], 30);
        push_event(w, EV_FLATLINE, a.loc, (uint8_t)ai, 0);
        if (w.rng.chance(g_mtune.flatline_death_pct)) {
            a.status &= (uint8_t)~AF_ALIVE; res.killed = true;
            push_event(w, EV_DEATH, a.loc, (uint8_t)ai, /*flatline*/4);
        }
    }
    if (r.shards >= (uint32_t)g_mtune.heist_score) {         // a big score
        push_event(w, EV_HEIST, target, (uint8_t)ai, 0);
        push_event(w, EV_BOUNTY, target, NONE8, owner);      // the corp wants the runner
    }
    if (count_allies(leg) > allies_before) {                 // a contact made in the matrix
        res.net_ally = true;
        push_event(w, EV_NETALLY, a.loc, (uint8_t)ai, 0);
    }
    return res;
}

// ============================================================================
// Phase 7: generative Gibson-voice narrator (§7) — no LLM, deterministic
// ============================================================================
namespace {
uint32_t nhash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}
const char* atmos(uint32_t h) {
    static const char* a[8] = { "neon", "rain", "chrome haze", "static", "the sprawl",
                                "sodium glare", "wet asphalt", "dead air" };
    return a[h & 7];
}
const char* sky(uint32_t h) {
    static const char* a[4] = { "a sky the color of dead tv", "a bruised, rain-fat sky",
                                "the chrome dusk", "a sky gone to static" };
    return a[h & 3];
}
} // namespace

const char* arc_name(uint8_t a) {
    static const char* n[MA_COUNT] = { "-", "linked", "debt-cascade", "water-riot",
                                       "megathreat-rampage", "feud", "cyber-fallout" };
    return a < MA_COUNT ? n[a] : "?";
}

std::string narrate_event(const World& w, const Event& e) {
    uint32_t h = nhash(w.world_seed ^ (e.kind * 2654435761U) ^ (e.node * 40503U)
                       ^ (e.agent * 2246822519U) ^ (e.tick * 3266489917U));
    const char* place = (e.node < w.district_count) ? district_type_name(w.districts[e.node].type) : "the sprawl";
    const char* who   = (e.agent < w.agent_count)   ? agent_name(w.agents[e.agent].name_id)        : "a stranger";
    const char* fac   = faction_name(e.data);
    char b[192];
    switch (e.kind) {
        case EV_COMBAT:
            std::snprintf(b, sizeof b, "Blood and %s in the %s - a deal gone wrong, a debt called in.", atmos(h), place);
            break;
        case EV_DEATH: {
            const char* how = e.data == 1 ? "a blade found the gap in the chrome"
                            : e.data == 2 ? "the thing in the dark was hungrier"
                            : e.data == 4 ? "the deck went dark and took them with it"
                                          : "the street simply stopped feeding them";
            std::snprintf(b, sizeof b, "%s flatlined in the %s; %s. The %s did not blink.", who, place, how, atmos(h));
            break; }
        case EV_TURF_FLIP:
            std::snprintf(b, sizeof b, "The %s flies new colors - the %s runs it now. Same %s, fresh blood.", place, fac, atmos(h));
            break;
        case EV_RAID:
            std::snprintf(b, sizeof b, "The %s came down hard on the %s. Muzzle-flash under %s.", fac, place, sky(h));
            break;
        case EV_THREAT_SPAWN:
            std::snprintf(b, sizeof b, "Something woke in the %s: a %s, and it is not afraid.", place, threat_name(e.data));
            break;
        case EV_THREAT_DEFEAT:
            std::snprintf(b, sizeof b, "They put the %s down in the %s. A story for the bars, true or not.", threat_name(e.data), place);
            break;
        case EV_REFUGEE:
            std::snprintf(b, sizeof b, "%s ran from the %s with nothing but %s at their back.", who, place, atmos(h));
            break;
        case EV_EXTORT:
            std::snprintf(b, sizeof b, "The %s leans on the %s again - protection, they call it.", fac, place);
            break;
        case EV_BOUNTY:
            std::snprintf(b, sizeof b, "A bounty hits the %s board. Somebody upstairs wants somebody gone.", place);
            break;
        case EV_RECRUIT:
            std::snprintf(b, sizeof b, "%s took the %s's coin. Everybody belongs to someone, eventually.", who, fac);
            break;
        case EV_MARKET_DAY:
            std::snprintf(b, sizeof b, "Market day in the %s - noise, grease-smoke, a moment's mercy under %s.", place, sky(h));
            break;
        case EV_RUMOR:
            std::snprintf(b, sizeof b, "Word moves through the %s like %s: half of it lies.", place, atmos(h));
            break;
        case EV_COLLAPSE:
            std::snprintf(b, sizeof b, "The %s gave way - rebar and dust and a sound like the city exhaling.", place);
            break;
        case EV_SHORTAGE: {
            const char* c = commodity_name(e.data);
            std::snprintf(b, sizeof b, "The %s ran dry of %s. Scarcity sharpens everything.", place, c);
            break; }
        case EV_HEATWAVE:
            std::snprintf(b, sizeof b, "The heat came down like a hammer; the low blocks went thirsty under %s.", sky(h));
            break;
        case EV_LOCKDOWN:
            std::snprintf(b, sizeof b, "The %s sealed tight - drones at every door, %s in the wires.", place, atmos(h));
            break;
        case EV_RIOT:
            std::snprintf(b, sizeof b, "The %s boiled over - thirst and rage in equal measure, and no one to bill.", place);
            break;
        case EV_JACKIN:
            std::snprintf(b, sizeof b, "%s jacked the %s ice and went under - %s.", who, place, outcome_name(e.data));
            break;
        case EV_HEIST:
            std::snprintf(b, sizeof b, "%s cracked the %s vault and ran with the data. Somewhere, an alarm with teeth.", who, place);
            break;
        case EV_FLATLINE:
            std::snprintf(b, sizeof b, "%s's deck went dark mid-run - flatline, the long fall, the silver static.", who);
            break;
        case EV_NETALLY:
            std::snprintf(b, sizeof b, "%s found a friend in the matrix, of all the cold places to find one.", who);
            break;
        case EV_NEWCOMER:
            std::snprintf(b, sizeof b, "A fresh face in the %s - %s, a %s, looking for a foothold under %s.",
                          place, who, agent_kind_name(e.data), sky(h));
            break;
        case EV_POP_SHIFT:
            if (e.data == AK_SYNTH)
                std::snprintf(b, sizeof b, "The census tipped: the machines outnumber the living now. The sprawl hums where it used to breathe.");
            else
                std::snprintf(b, sizeof b, "The census tipped: the changed outnumber the clean now. The toxin won; the streets wear new faces.");
            break;
        case EV_CRAFTED:
            if (e.data == CR_DECK)
                std::snprintf(b, sizeof b, "You jacked together a deck from scavenged parts. The matrix has a door now, and it knows your name.");
            else
                std::snprintf(b, sizeof b, "You finished %s on the %s bench - made, not bought.", craft_name(e.data), place);
            break;
        default:
            std::snprintf(b, sizeof b, "The %s turns over in its sleep.", place);
            break;
    }
    return std::string(b);
}

std::string narrate_arc(const World& w, uint8_t arc, uint8_t district, uint8_t prev_kind, uint8_t cur_kind) {
    const char* place = (district < w.district_count) ? district_type_name(w.districts[district].type) : "the sprawl";
    char b[224];
    switch (arc) {
        case MA_DEBT_CASCADE:
            std::snprintf(b, sizeof b, "ARC | The %s: scarcity drew the sharks, the sharks drew the law, and the doors slammed shut. A debt that started small and ate a block.", place);
            break;
        case MA_WATER_RIOT:
            std::snprintf(b, sizeof b, "ARC | The %s went dry, then went up. They came for water and stayed for the reckoning; the old flags burned.", place);
            break;
        case MA_MEGA_RAMPAGE:
            std::snprintf(b, sizeof b, "ARC | The %s remembers the thing that came: it broke them, it scattered them, and then someone broke it. Legends are cheaper than rent.", place);
            break;
        case MA_FEUD:
            std::snprintf(b, sizeof b, "ARC | A death in the %s did not stay buried. Blood answers blood; the ledger never balances.", place);
            break;
        case MA_CYBER_FALLOUT:
            std::snprintf(b, sizeof b, "ARC | A run in the matrix put a price on the %s - and the corp collected in the street. What you steal in the deck, you bleed for in the rain.", place);
            break;
        case MA_LINKED: default:
            std::snprintf(b, sizeof b, "ARC | In the %s, the %s gave way to the %s - one thing leaning on the next, the way this city always works.",
                          place, event_name(prev_kind), event_name(cur_kind));
            break;
    }
    return std::string(b);
}

void ArcTracker::reset(int window_ticks) {
    for (int i = 0; i < MAX_DISTRICTS; ++i) {
        shortage_t[i] = gang_t[i] = riot_t[i] = mega_t[i] = death_t[i] = heist_t[i] = last_t[i] = -(1 << 28);
        last_kind[i] = EV_NONE;
    }
    heat_until = -(1 << 28);
    window = window_ticks;
}

uint8_t ArcTracker::ingest(uint8_t kind, uint8_t d, uint8_t data, int t) {
    if (kind == EV_HEATWAVE) { heat_until = t + window; return MA_NONE; }
    if (d == NONE8 || d >= MAX_DISTRICTS) return MA_NONE;
    int W = window;
    bool mega = (data == TK_CONSTRUCT_MECH || data == TK_SEWER_LEVIATHAN ||
                 data == TK_ROGUE_AI_BODY || data == TK_MUTANT_COLONY);
    // arming
    if (kind == EV_SHORTAGE) shortage_t[d] = t;
    if (kind == EV_THREAT_SPAWN && mega) mega_t[d] = t;
    if ((kind == EV_EXTORT || kind == EV_RAID || kind == EV_TURF_FLIP) && t - shortage_t[d] <= W) gang_t[d] = t;
    if (kind == EV_RIOT && (t - shortage_t[d] <= W || t < heat_until)) riot_t[d] = t;
    if (kind == EV_DEATH && data == 1) death_t[d] = t;
    if (kind == EV_HEIST) heist_t[d] = t;

    uint8_t arc = MA_NONE;
    if ((kind == EV_RAID || kind == EV_TURF_FLIP || kind == EV_DEATH) && t - heist_t[d] <= W) { heist_t[d] = -(1 << 28); arc = MA_CYBER_FALLOUT; }
    else if ((kind == EV_LOCKDOWN || kind == EV_COMBAT) && t - gang_t[d] <= W) { gang_t[d] = shortage_t[d] = -(1 << 28); arc = MA_DEBT_CASCADE; }
    else if ((kind == EV_TURF_FLIP || kind == EV_REFUGEE || kind == EV_RAID) && t - riot_t[d] <= W) { riot_t[d] = -(1 << 28); arc = MA_WATER_RIOT; }
    else if (kind == EV_THREAT_DEFEAT && mega && t - mega_t[d] <= W) { mega_t[d] = -(1 << 28); arc = MA_MEGA_RAMPAGE; }
    else if (kind == EV_COMBAT && t - death_t[d] <= W && death_t[d] < t) { death_t[d] = -(1 << 28); arc = MA_FEUD; }
    // generic long-tail: any two related events on a block in the window are a beat
    else if (last_kind[d] != EV_NONE && t - last_t[d] <= W && last_kind[d] != kind) arc = MA_LINKED;

    last_kind[d] = kind; last_t[d] = t;
    return arc;
}

// ---- population inflow (#26): newcomers keep the sprawl alive endlessly -----
// composition is a weighted choice among human/synth/mutant, driven by the global
// tides + the spawn district. As tides rise, fewer humans bother coming -> the city
// CAN become synth or mutant; if tides stay low it stays human. A possibility space.
static uint8_t pick_newcomer_kind(World& w, uint8_t loc) {
    int s = w.synth_tide, m = w.mutant_tide;
    uint8_t dt = w.districts[loc].type;
    if (dt == DT_TOXIC || dt == DT_UNDERCITY)        m += 60;   // local mutation pull
    if (dt == DT_DATACENTER || dt == DT_INDUSTRIAL)  s += 40;   // local automation pull
    int human = clampi(255 - s - m, 8, 255);                   // humans thin out as the city turns
    int total = human + s + m;
    int r = (int)w.rng.range((uint32_t)total);
    if (r < s) return AK_SYNTH;
    if (r < s + m) return AK_MUTANT;
    return AK_HUMAN;
}
// condition-derived targets for the two tides (0..255), so they can rise OR fall
static void tide_targets(const World& w, int& autom, int& mutat) {
    int dc = 0, ind = 0, tox = 0, undc = 0, inf_corp = 0, inf_cult = 0, toxin = 0, mutown = 0;
    for (int i = 0; i < w.district_count; ++i) {
        const District& d = w.districts[i];
        if (d.type == DT_DATACENTER) ++dc; else if (d.type == DT_INDUSTRIAL) ++ind;
        else if (d.type == DT_TOXIC) ++tox; else if (d.type == DT_UNDERCITY) ++undc;
        inf_corp += d.influence[F_MEGACORP]; inf_cult += d.influence[F_CULT];
        toxin += d.hazard[HZ_TOXIN];
        if (d.owner == F_MUTANT) ++mutown;
    }
    int nd = w.district_count ? w.district_count : 1;
    int rogue = 0;
    for (int t = 0; t < w.threat_count; ++t) if (w.threats[t].kind == TK_ROGUE_AI_BODY) rogue += 25;
    autom = clampi((inf_corp + inf_cult) / nd + dc * 18 + ind * 8 + rogue, 0, 255);
    mutat = clampi(toxin / nd + tox * 30 + undc * 14 + mutown * 10, 0, 255);
}
static void respawn_newcomer(World& w, int slot) {
    Agent& a = w.agents[slot];
    a = Agent{};                                   // a fresh arrival fills the dead slot
    a.name_id = (uint8_t)w.rng.range((uint32_t)agent_name_count());
    uint8_t loc = w.home;                          // arrive at a market if one exists
    for (int i = 0; i < w.district_count; ++i) if (w.districts[i].services & SV_MARKET) { loc = (uint8_t)i; break; }
    a.loc = loc;
    a.kind = pick_newcomer_kind(w, loc);
    a.faction = F_COUNT;
    for (int t = 0; t < TR_COUNT; ++t) a.trait[t] = (uint8_t)w.rng.between(0, 255);
    for (int nd = 0; nd < ND_COUNT; ++nd) a.need[nd] = (uint8_t)w.rng.between(10, 50);
    a.status = AF_ALIVE;
    if (w.rng.chance(12)) a.status |= AF_HAS_DECK;
    if (w.rng.chance(60)) { a.job = (uint8_t)w.rng.between(J_CONSTRUCTION, J_INFRA); a.skill[a.job] = (uint8_t)w.rng.between(5, 50); a.status |= AF_EMPLOYED; }
    a.money = (uint32_t)w.rng.between(10, 60);
    a.mood = (uint8_t)w.rng.between(40, 70);
    push_event(w, EV_NEWCOMER, loc, (uint8_t)slot, a.kind);
}
static void population_step(World& w) {
    // tides drift toward what the world's conditions support (rise OR fall) — no
    // built-in march to machines; an industrial/corp world leans synth, a toxic
    // one leans mutant, a quiet one stays human, and a shifting world shifts.
    int autom = 0, mutat = 0; tide_targets(w, autom, mutat);
    if (w.synth_tide < autom && w.synth_tide < 255) w.synth_tide++;
    else if (w.synth_tide > autom && w.synth_tide > 0) w.synth_tide--;
    if (w.mutant_tide < mutat && w.mutant_tide < 255) w.mutant_tide++;
    else if (w.mutant_tide > mutat && w.mutant_tide > 0) w.mutant_tide--;

    // inflow: backfill the dead toward the worldgen population (never slot 0 = player)
    int alive = 0;
    for (int i = 0; i < w.agent_count; ++i) if (w.agents[i].status & AF_ALIVE) ++alive;
    if (alive < w.agent_count && w.rng.chance(g_mtune.inflow_pct)) {
        for (int i = 1; i < w.agent_count; ++i)
            if (!(w.agents[i].status & AF_ALIVE)) { respawn_newcomer(w, i); break; }
    }

    // tipping point: when humans are no longer the majority, the city has changed
    // hands — occasional beat, data = the new dominant kind (synth or mutant).
    int humans = human_count(w), synths = synth_count(w);
    int mutants = 0;
    for (int i = 0; i < w.agent_count; ++i)
        if ((w.agents[i].status & AF_ALIVE) && w.agents[i].kind == AK_MUTANT) ++mutants;
    if (humans > 0 && (synths > humans || mutants > humans) && w.rng.chance(2))
        push_event(w, EV_POP_SHIFT, NONE8, NONE8, (uint8_t)(synths >= mutants ? AK_SYNTH : AK_MUTANT));
}

void tick_world(World& w) {
    const MidTunables& T = g_mtune;

    // supply regeneration (the scarcity/opportunity cycle)
    if (T.regen_period > 0 && (w.tick % (uint32_t)T.regen_period) == 0) {
        for (int i = 0; i < w.district_count; ++i) {
            District& d = w.districts[i];
            for (int c = 0; c < C_COUNT; ++c) {
                int s = d.supply[c] + w.rng.between(T.regen_min, T.regen_max);
                d.supply[c] = (uint8_t)(s > T.supply_cap ? T.supply_cap : s);
            }
        }
    }

    // agents act (index order = deterministic)
    for (int i = 0; i < w.agent_count; ++i) agent_step(w, i);

    // rent: the money sink that keeps wealth from running away
    if (T.rent_period > 0 && (w.tick % (uint32_t)T.rent_period) == 0 && w.tick > 0) {
        for (int i = 0; i < w.agent_count; ++i) {
            Agent& a = w.agents[i];
            if (!(a.status & AF_ALIVE)) continue;
            // the protagonist pays rent ONCE/day on their APARTMENT only (#4); with no
            // apartment they doss at work for free. NPC housing stays abstract (at loc).
            uint8_t rent_loc;
            if (a.status & AF_PLAYER) { if (w.apt_district >= w.district_count) continue; rent_loc = w.apt_district; }
            else rent_loc = a.loc;
            int rent = T.rent_base + w.districts[rent_loc].prosperity / 8;
            if ((int)a.money >= rent) { a.money -= (uint32_t)rent; record_txn(w, a, -rent, TXN_RENT); a.status &= (uint8_t)~AF_IN_DEBT; }
            else { record_txn(w, a, -(int32_t)a.money, TXN_RENT); a.money = 0; a.status |= AF_IN_DEBT; bump_need(a.need[ND_STRESS], 6); }
        }
    }

    // street incidents (once per in-game day): danger-scaled muggings. CHOICE-GATED
    // lethality — an incident NEVER kills outright: the first hit robs + wounds. Death
    // only strikes someone ALREADY injured who is also BROKE (can't buy care). Both are
    // player-addressable: get patched up (you won't be "already injured"), or keep cash
    // >= safe_money (you buy your way out of a repeat). So a careful run survives; a
    // reckless/broke one that ignores its wounds is the one that dies.
    if (T.rent_period > 0 && (w.tick % (uint32_t)T.rent_period) == 0 && w.tick > 0) {
        for (int i = 0; i < w.agent_count; ++i) {
            Agent& a = w.agents[i];
            if (!(a.status & AF_ALIVE)) continue;
            int danger = w.districts[a.loc].danger;
            int chance = danger / T.incident_div;
            if (a.status & AF_PLAYER) chance = chance * (128 + (int)w.directive.risk) / 256;
            if (chance <= 0 || !w.rng.chance(chance)) continue;
            { uint32_t lost = a.money / 2; a.money -= lost; record_txn(w, a, -(int32_t)lost, TXN_ROBBED); } // rolled / robbed
            bump_need(a.need[ND_STRESS], 10);
            if (!(a.status & AF_INJURED)) { a.status |= AF_INJURED; continue; }  // first hit: wounded, not dead
            if ((int)a.money >= T.safe_money) continue;       // hurt again, but cash buys care -> survives
            // hurt + broke + caught out again: now it can be fatal (scaled by risk; eased
            // once you're an established outfit).
            int dc = T.incident_lethal_pct;
            if (a.status & AF_PLAYER) dc = dc * (96 + (int)w.directive.risk) / 160;
            dc /= (1 + w.company.tier);
            if (a.inv[IT_ARMOR] || a.inv[IT_IMPLANTS]) dc = dc * 2 / 3;   // crafted protection saves lives
            if (dc > 0 && w.rng.chance(dc)) { a.status &= (uint8_t)~AF_ALIVE; push_event(w, EV_DEATH, a.loc, (uint8_t)i, /*mugging*/3); }
        }
    }

    // once-per-day world systems (territory / hazards / threats / decay / pulses)
    if (T.rent_period > 0 && (w.tick % (uint32_t)T.rent_period) == 0 && w.tick > 0) {
        // weather drift (#33): heatwave drought hits the POOR blocks hardest
        // (the §3.1 "megacorp throttles water in low-income districts" beat) ->
        // localized water scarcity -> unrest, while richer blocks ride it out.
        if (w.weather > 0) {
            w.weather--;
            for (int i = 0; i < w.district_count; ++i) {
                if (w.districts[i].prosperity >= 40) continue;     // rich blocks buy through it
                int s = w.districts[i].supply[C_WATER] - 15;
                w.districts[i].supply[C_WATER] = (uint8_t)(s < 0 ? 0 : s);
            }
        } else if (w.rng.chance(2)) {
            w.weather = (uint8_t)w.rng.between(2, 6);
            push_event(w, EV_HEATWAVE, NONE8, NONE8, w.weather);
        }
        territory_step(w);
        hazard_step(w);
        if (w.rng.chance(T.threat_spawn_pct)) spawn_threat(w);
        threats_step(w);
        unrest_step(w);
        decay_step(w);
        pulse_step(w);
        // background deckers jack the net (invisible runs that feed the world)
        for (int i = 0; i < w.agent_count; ++i)
            if ((w.agents[i].status & (AF_ALIVE | AF_HAS_DECK)) == (AF_ALIVE | AF_HAS_DECK) && w.rng.chance(T.jack_pct))
                jack_in(w, i);
        population_step(w);
        company_step(w);
    }

    w.tick++;
}

// ============================================================================
// Phase 8a: realized local embark map (view layer, §2.7) — deterministic,
// regenerable from district.seed + type; tile-aware VIEW only, never the sim.
// ============================================================================
bool localmap_walkable(const LocalMap& m, int x, int y) {
    return x >= 0 && x < LMAP_W && y >= 0 && y < LMAP_H && m.tile[y][x] != LT_WALL;
}
char localmap_tile_glyph(uint8_t t) {
    switch (t) { case LT_FLOOR: return '.'; case LT_DOOR: return '+';
                 case LT_HAZARD: return '~'; case LT_RUBBLE: return ','; default: return '#'; }
}
char localmap_poi_glyph(uint8_t bit) {
    switch (1u << bit) {
        case SV_JOB_BOARD: return 'J'; case SV_MARKET: return '$'; case SV_CLINIC: return 'C';
        case SV_CHOPSHOP: return 'X';  case SV_FABLAB: return 'F'; case SV_BAR: return 'B';
        case SV_DATAVAULT: return 'D'; case SV_FENCE: return 'f';  case SV_DRONEBAY: return 'R';
        case SV_CHEMLAB: return 'L';   case SV_ARMORSHOP: return 'A'; case SV_FORGE: return 'G';
        default: return '?';
    }
}
const char* localmap_poi_name(uint8_t bit) {
    switch (1u << bit) {
        case SV_JOB_BOARD: return "job board"; case SV_MARKET: return "market"; case SV_CLINIC: return "clinic";
        case SV_CHOPSHOP: return "chop shop";  case SV_FABLAB: return "fab lab"; case SV_BAR: return "bar";
        case SV_DATAVAULT: return "data vault"; case SV_FENCE: return "fence";   case SV_DRONEBAY: return "drone bay";
        case SV_CHEMLAB: return "chem lab";    case SV_ARMORSHOP: return "armor shop"; case SV_FORGE: return "forge";
        default: return "?";
    }
}

namespace {
const int kDX[4] = { 1, -1, 0, 0 }, kDY[4] = { 0, 0, 1, -1 };
// BFS distance field from (sx,sy) over walkable tiles (255 = unreachable).
bool lmap_bfs(const LocalMap& m, int sx, int sy, uint8_t dist[LMAP_H][LMAP_W]) {
    for (int y = 0; y < LMAP_H; ++y) for (int x = 0; x < LMAP_W; ++x) dist[y][x] = 255;
    if (!localmap_walkable(m, sx, sy)) return false;
    static uint16_t q[LMAP_W * LMAP_H];
    int head = 0, tail = 0;
    dist[sy][sx] = 0; q[tail++] = (uint16_t)(sy * LMAP_W + sx);
    while (head < tail) {
        int idx = q[head++], x = idx % LMAP_W, y = idx / LMAP_W;
        int nd = dist[y][x] + 1; if (nd > 254) nd = 254;
        for (int k = 0; k < 4; ++k) {
            int nx = x + kDX[k], ny = y + kDY[k];
            if (localmap_walkable(m, nx, ny) && dist[ny][nx] == 255) {
                dist[ny][nx] = (uint8_t)nd; q[tail++] = (uint16_t)(ny * LMAP_W + nx);
            }
        }
    }
    return true;
}
} // namespace

bool localmap_reachable(const LocalMap& m, int fx, int fy, int tx, int ty) {
    static uint8_t dist[LMAP_H][LMAP_W];
    if (!lmap_bfs(m, fx, fy, dist)) return false;
    if (tx < 0 || tx >= LMAP_W || ty < 0 || ty >= LMAP_H) return false;
    return dist[ty][tx] != 255;
}
bool localmap_step_toward(const LocalMap& m, int fx, int fy, int tx, int ty, int* nx, int* ny) {
    static uint8_t dist[LMAP_H][LMAP_W];
    if (!lmap_bfs(m, tx, ty, dist)) return false;          // distances TO the target
    if (fx < 0 || fx >= LMAP_W || fy < 0 || fy >= LMAP_H || dist[fy][fx] == 255) return false;
    int best = dist[fy][fx], bx = fx, by = fy;
    for (int k = 0; k < 4; ++k) {                          // descend the gradient
        int ax = fx + kDX[k], ay = fy + kDY[k];
        if (localmap_walkable(m, ax, ay) && dist[ay][ax] < best) { best = dist[ay][ax]; bx = ax; by = ay; }
    }
    *nx = bx; *ny = by;
    return true;
}

void gen_localmap(LocalMap& m, const World& w, uint8_t district) {
    m = LocalMap{};
    uint8_t  type     = (district < w.district_count) ? w.districts[district].type : (uint8_t)DT_SLUM;
    uint16_t services = (district < w.district_count) ? w.districts[district].services : 0;
    uint32_t seed     = (district < w.district_count) ? w.districts[district].seed : (uint32_t)(district + 1);
    Rng r; r.seed(seed ^ (0xB5297A4Du + (uint32_t)type * 2654435761u));

    for (int y = 0; y < LMAP_H; ++y) for (int x = 0; x < LMAP_W; ++x) m.tile[y][x] = LT_WALL;

    // room density/size by district type (slum = cramped warren, arcology = halls)
    int rooms, rmin, rmax;
    switch (type) {
        case DT_ARCOLOGY:  rooms = 5;  rmin = 7; rmax = 14; break;
        case DT_MARKET:    rooms = 4;  rmin = 8; rmax = 16; break;
        case DT_SLUM:      rooms = 11; rmin = 3; rmax = 6;  break;
        case DT_UNDERCITY: rooms = 9;  rmin = 3; rmax = 7;  break;
        case DT_INDUSTRIAL:rooms = 6;  rmin = 6; rmax = 11; break;
        case DT_DATACENTER:rooms = 6;  rmin = 5; rmax = 9;  break;
        case DT_TOXIC:     rooms = 6;  rmin = 4; rmax = 9;  break;
        default:           rooms = 7;  rmin = 4; rmax = 9;  break;
    }
    if (rooms > 14) rooms = 14;

    struct RR { int x, y, w, h; } rr[16]; int nr = 0;
    for (int i = 0; i < rooms && nr < 16; ++i) {
        int rw = r.between(rmin, rmax), rh = r.between(rmin, rmax);
        if (rw > LMAP_W - 3) rw = LMAP_W - 3;
        if (rh > LMAP_H - 3) rh = LMAP_H - 3;
        int rx = r.between(1, LMAP_W - rw - 1), ry = r.between(1, LMAP_H - rh - 1);
        for (int yy = ry; yy < ry + rh; ++yy) for (int xx = rx; xx < rx + rw; ++xx) m.tile[yy][xx] = LT_FLOOR;
        rr[nr++] = { rx, ry, rw, rh };
    }
    if (nr == 0) { rr[nr++] = { LMAP_W / 2 - 3, LMAP_H / 2 - 3, 6, 6 };
                   for (int yy = rr[0].y; yy < rr[0].y + 6; ++yy) for (int xx = rr[0].x; xx < rr[0].x + 6; ++xx) m.tile[yy][xx] = LT_FLOOR; }

    // connect consecutive rooms with L-corridors -> the whole map is one component
    for (int i = 1; i < nr; ++i) {
        int ax = rr[i - 1].x + rr[i - 1].w / 2, ay = rr[i - 1].y + rr[i - 1].h / 2;
        int bx = rr[i].x + rr[i].w / 2,         by = rr[i].y + rr[i].h / 2;
        for (int x = (ax < bx ? ax : bx); x <= (ax < bx ? bx : ax); ++x) if (m.tile[ay][x] == LT_WALL) m.tile[ay][x] = LT_FLOOR;
        for (int y = (ay < by ? ay : by); y <= (ay < by ? by : ay); ++y) if (m.tile[y][bx] == LT_WALL) m.tile[y][bx] = LT_FLOOR;
    }

    // hazard flavor (walkable): toxin/rubble sprinkled on floor by the district's state
    int toxin = (district < w.district_count) ? w.districts[district].hazard[HZ_TOXIN] : 0;
    for (int i = 0; i < (toxin / 12); ++i) {
        int x = r.between(0, LMAP_W - 1), y = r.between(0, LMAP_H - 1);
        if (m.tile[y][x] == LT_FLOOR) m.tile[y][x] = LT_HAZARD;
    }

    // POIs: one per offered service, placed on a room floor tile (reachable via corridors)
    for (uint8_t bit = 0; bit < 12 && m.poi_count < LMAP_POI_MAX; ++bit) {
        if (!(services & (1u << bit))) continue;
        const RR& room = rr[m.poi_count % nr];
        int px = room.x + 1 + (m.poi_count * 2) % (room.w > 2 ? room.w - 2 : 1);
        int py = room.y + room.h / 2;
        if (px >= LMAP_W) px = LMAP_W - 1;
        if (py >= LMAP_H) py = LMAP_H - 1;
        m.tile[py][px] = LT_DOOR;
        m.poi[m.poi_count].x = (uint8_t)px; m.poi[m.poi_count].y = (uint8_t)py; m.poi[m.poi_count].service_bit = bit;
        m.poi_count++;
    }

    // the @ spawns at the heart of the first room
    m.entry_x = (uint8_t)(rr[0].x + rr[0].w / 2);
    m.entry_y = (uint8_t)(rr[0].y + rr[0].h / 2);

    // scatter co-located NPC spawn tiles on the floor (8b binds agents to these)
    for (int i = 0; i < LMAP_ACTORS; ++i) {
        for (int tries = 0; tries < 8; ++tries) {
            int x = r.between(1, LMAP_W - 2), y = r.between(1, LMAP_H - 2);
            if (m.tile[y][x] == LT_FLOOR) { m.actor_xy[m.actor_count][0] = (uint8_t)x; m.actor_xy[m.actor_count][1] = (uint8_t)y; m.actor_count++; break; }
        }
    }
}

// ---- serialization (byte-exact, little-endian, fixed-width) -----------------
namespace {
struct Writer {
    std::string& b;
    explicit Writer(std::string& s) : b(s) {}
    void u8(uint8_t v)  { b.push_back((char)v); }
    void i8(int8_t v)   { b.push_back((char)(uint8_t)v); }
    void u16(uint16_t v){ u8((uint8_t)(v & 0xFF)); u8((uint8_t)((v >> 8) & 0xFF)); }
    void u32(uint32_t v){ u8((uint8_t)(v & 0xFF)); u8((uint8_t)((v >> 8) & 0xFF));
                          u8((uint8_t)((v >> 16) & 0xFF)); u8((uint8_t)((v >> 24) & 0xFF)); }
};
struct Reader {
    const uint8_t* p; size_t n; size_t i = 0; bool ok = true;
    Reader(const uint8_t* d, size_t len) : p(d), n(len) {}
    uint8_t  u8()  { if (i >= n) { ok = false; return 0; } return p[i++]; }
    int8_t   i8()  { return (int8_t)u8(); }
    uint16_t u16() { uint16_t a = u8(), c = u8(); return (uint16_t)(a | (c << 8)); }
    uint32_t u32() { uint32_t a = u8(), c = u8(), d = u8(), e = u8();
                     return a | (c << 8) | (d << 16) | (e << 24); }
};

void put_district(Writer& wr, const District& d) {
    wr.u8(d.type); wr.u8(d.owner); wr.u32(d.seed);
    for (int f = 0; f < F_COUNT; ++f) wr.u8(d.influence[f]);
    for (int c = 0; c < C_COUNT; ++c) wr.u8(d.supply[c]);
    for (int h = 0; h < HZ_COUNT; ++h) wr.u8(d.hazard[h]);
    wr.u8(d.population); wr.u8(d.prosperity); wr.u8(d.danger); wr.u8(d.danger_base);
    wr.u16(d.services); wr.u8(d.deg);
    for (int k = 0; k < MAXDEG; ++k) wr.u8(d.adj[k]);
}
void get_district(Reader& rd, District& d) {
    d.type = rd.u8(); d.owner = rd.u8(); d.seed = rd.u32();
    for (int f = 0; f < F_COUNT; ++f) d.influence[f] = rd.u8();
    for (int c = 0; c < C_COUNT; ++c) d.supply[c] = rd.u8();
    for (int h = 0; h < HZ_COUNT; ++h) d.hazard[h] = rd.u8();
    d.population = rd.u8(); d.prosperity = rd.u8(); d.danger = rd.u8(); d.danger_base = rd.u8();
    d.services = rd.u16(); d.deg = rd.u8();
    for (int k = 0; k < MAXDEG; ++k) d.adj[k] = rd.u8();
}
// cyber::Legends is a fixed-size POD-ish struct; dump it field-wise for a
// byte-exact mid save (independent of CyberHack's own serializer).
void put_legends(Writer& wr, const cyber::Legends& L) {
    wr.u32(L.citynet_seed); wr.u16(L.run_count); wr.u32(L.best_score);
    for (int f = 0; f < cyber::F_COUNT; ++f) wr.i8(L.grudge[f]);
    wr.u8(L.named_count);
    for (int k = 0; k < cyber::MAX_NAMED; ++k) {
        wr.u8(L.named[k].name_id); wr.u8(L.named[k].archetype);
        wr.u8(L.named[k].status); wr.i8(L.named[k].grudge);
    }
    wr.u8(L.burned_count);
    for (int k = 0; k < cyber::MAX_BURNED; ++k) { wr.u8(L.burned[k].a); wr.u8(L.burned[k].b); }
    wr.u8(L.marked_count);
    for (int k = 0; k < cyber::MAX_MARKED; ++k) wr.u8(L.marked[k]);
}
void get_legends(Reader& rd, cyber::Legends& L) {
    L.citynet_seed = rd.u32(); L.run_count = rd.u16(); L.best_score = rd.u32();
    for (int f = 0; f < cyber::F_COUNT; ++f) L.grudge[f] = rd.i8();
    L.named_count = rd.u8();
    for (int k = 0; k < cyber::MAX_NAMED; ++k) {
        L.named[k].name_id = rd.u8(); L.named[k].archetype = rd.u8();
        L.named[k].status = rd.u8(); L.named[k].grudge = rd.i8();
    }
    L.burned_count = rd.u8();
    for (int k = 0; k < cyber::MAX_BURNED; ++k) { L.burned[k].a = rd.u8(); L.burned[k].b = rd.u8(); }
    L.marked_count = rd.u8();
    for (int k = 0; k < cyber::MAX_MARKED; ++k) L.marked[k] = rd.u8();
}
void put_agent(Writer& wr, const Agent& a) {
    wr.u8(a.name_id); wr.u8(a.kind); wr.u8(a.loc); wr.u8(a.faction);
    for (int t = 0; t < TR_COUNT; ++t) wr.u8(a.trait[t]);
    for (int nd = 0; nd < ND_COUNT; ++nd) wr.u8(a.need[nd]);
    wr.u8(a.job);
    for (int j = 0; j < J_COUNT; ++j) wr.u8(a.skill[j]);
    wr.u8(a.mood); wr.u8(a.status); wr.u32(a.money);
    for (int it = 0; it < IT_COUNT; ++it) wr.u8(a.inv[it]);
    wr.u8(a.activity);
    for (int k = 0; k < RELMAX; ++k) { wr.u8(a.rel[k].other); wr.i8(a.rel[k].valence); }
    for (int k = 0; k < SCARMAX; ++k) { wr.u8(a.scar[k].node); wr.u8(a.scar[k].kind); wr.i8(a.scar[k].valence); }
}
void get_agent(Reader& rd, Agent& a) {
    a.name_id = rd.u8(); a.kind = rd.u8(); a.loc = rd.u8(); a.faction = rd.u8();
    for (int t = 0; t < TR_COUNT; ++t) a.trait[t] = rd.u8();
    for (int nd = 0; nd < ND_COUNT; ++nd) a.need[nd] = rd.u8();
    a.job = rd.u8();
    for (int j = 0; j < J_COUNT; ++j) a.skill[j] = rd.u8();
    a.mood = rd.u8(); a.status = rd.u8(); a.money = rd.u32();
    for (int it = 0; it < IT_COUNT; ++it) a.inv[it] = rd.u8();
    a.activity = rd.u8();
    for (int k = 0; k < RELMAX; ++k) { a.rel[k].other = rd.u8(); a.rel[k].valence = rd.i8(); }
    for (int k = 0; k < SCARMAX; ++k) { a.scar[k].node = rd.u8(); a.scar[k].kind = rd.u8(); a.scar[k].valence = rd.i8(); }
}
} // anonymous namespace

void serialize(const World& w, std::string& out) {
    out.clear();
    Writer wr(out);
    wr.u16(MID_MAGIC); wr.u8(MID_VERSION);
    wr.u32(w.world_seed); wr.u32(w.tick);
    for (int k = 0; k < 4; ++k) wr.u32(w.rng.s[k]);
    wr.u8(w.home); wr.u8(w.district_count); wr.u8(w.agent_count);
    // dump ALL fixed slots so round-trips are byte-identical regardless of count
    for (int i = 0; i < MAX_DISTRICTS; ++i) put_district(wr, w.districts[i]);
    for (int i = 0; i < MAX_AGENTS; ++i)    put_agent(wr, w.agents[i]);
    for (int f = 0; f < F_COUNT; ++f) {
        const FactionState& fs = w.factions[f];
        wr.u8(fs.alignment); wr.u8(fs.tech_level); wr.u8(fs.specialty);
        for (int g = 0; g <= F_COUNT; ++g) wr.i8(fs.grudge[g]);
        wr.u8(fs.leader); wr.u32(fs.treasury);
    }
    wr.u8(w.company.name_id); wr.u8(w.company.tier); wr.u32(w.company.treasury);
    wr.u8(w.company.emp_count);
    for (int k = 0; k < EMPMAX; ++k) wr.u8(w.company.employees[k]);
    wr.u8(w.company.asset_count); wr.u16(w.company.assets); wr.u8(w.company.reputation);
    wr.u8(w.company.sector); wr.u8(w.company.target_emp);
    wr.u8(w.directive.ambition); wr.u8(w.directive.target);
    wr.u8(w.directive.risk); wr.u8(w.directive.thrift);
    // protagonist focus / commute / contract (v2)
    wr.u8(w.focus); wr.u8(w.interrupt_focus); wr.u8(w.apt_district); wr.u8(w.work_district);
    wr.u8(w.craft_target);
    wr.u8(w.contract.active); wr.u8(w.contract.kind); wr.u8(w.contract.target);
    wr.u8(w.contract.progress); wr.u8(w.contract.need_ticks);
    wr.u16(w.contract.reward); wr.u16(w.contract.deadline);
    wr.u8(w.weather); wr.u8(w.synth_tide); wr.u8(w.mutant_tide);
    wr.u8(w.threat_count);
    for (int i = 0; i < MAX_THREATS; ++i) {
        const Threat& t = w.threats[i];
        wr.u8(t.kind); wr.u8(t.power); wr.u8(t.district); wr.u8(t.behavior); wr.u8(t.hp); wr.u8(t.active);
    }
    wr.u8(w.net_count);
    for (int i = 0; i < MAX_NET; ++i) { wr.u8(w.net_target[i]); put_legends(wr, w.net_legends[i]); }
    wr.u8(w.event_count); wr.u8(w.event_head);
    for (int i = 0; i < EVMAX; ++i) {
        const Event& e = w.events[i];
        wr.u8(e.kind); wr.u8(e.node); wr.u8(e.agent); wr.u8(e.data); wr.u16(e.tick);
    }
}

bool deserialize(const std::string& in, World& w) {
    Reader rd((const uint8_t*)in.data(), in.size());
    if (rd.u16() != MID_MAGIC) return false;
    if (rd.u8() != MID_VERSION) return false;
    w = World{};
    w.world_seed = rd.u32(); w.tick = rd.u32();
    for (int k = 0; k < 4; ++k) w.rng.s[k] = rd.u32();
    w.home = rd.u8(); w.district_count = rd.u8(); w.agent_count = rd.u8();
    for (int i = 0; i < MAX_DISTRICTS; ++i) get_district(rd, w.districts[i]);
    for (int i = 0; i < MAX_AGENTS; ++i)    get_agent(rd, w.agents[i]);
    for (int f = 0; f < F_COUNT; ++f) {
        FactionState& fs = w.factions[f];
        fs.alignment = rd.u8(); fs.tech_level = rd.u8(); fs.specialty = rd.u8();
        for (int g = 0; g <= F_COUNT; ++g) fs.grudge[g] = rd.i8();
        fs.leader = rd.u8(); fs.treasury = rd.u32();
    }
    w.company.name_id = rd.u8(); w.company.tier = rd.u8(); w.company.treasury = rd.u32();
    w.company.emp_count = rd.u8();
    for (int k = 0; k < EMPMAX; ++k) w.company.employees[k] = rd.u8();
    w.company.asset_count = rd.u8(); w.company.assets = rd.u16(); w.company.reputation = rd.u8();
    w.company.sector = rd.u8(); w.company.target_emp = rd.u8();
    w.directive.ambition = rd.u8(); w.directive.target = rd.u8();
    w.directive.risk = rd.u8(); w.directive.thrift = rd.u8();
    w.focus = rd.u8(); w.interrupt_focus = rd.u8(); w.apt_district = rd.u8(); w.work_district = rd.u8();
    w.craft_target = rd.u8();
    w.contract.active = rd.u8(); w.contract.kind = rd.u8(); w.contract.target = rd.u8();
    w.contract.progress = rd.u8(); w.contract.need_ticks = rd.u8();
    w.contract.reward = rd.u16(); w.contract.deadline = rd.u16();
    w.weather = rd.u8(); w.synth_tide = rd.u8(); w.mutant_tide = rd.u8();
    w.threat_count = rd.u8();
    for (int i = 0; i < MAX_THREATS; ++i) {
        Threat& t = w.threats[i];
        t.kind = rd.u8(); t.power = rd.u8(); t.district = rd.u8(); t.behavior = rd.u8(); t.hp = rd.u8(); t.active = rd.u8();
    }
    w.net_count = rd.u8();
    for (int i = 0; i < MAX_NET; ++i) { w.net_target[i] = rd.u8(); get_legends(rd, w.net_legends[i]); }
    w.event_count = rd.u8(); w.event_head = rd.u8();
    for (int i = 0; i < EVMAX; ++i) {
        Event& e = w.events[i];
        e.kind = rd.u8(); e.node = rd.u8(); e.agent = rd.u8(); e.data = rd.u8(); e.tick = rd.u16();
    }
    return rd.ok;
}

} // namespace mid
