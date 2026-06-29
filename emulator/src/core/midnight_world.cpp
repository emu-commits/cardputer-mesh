// Midnight City — engine substrate implementation (Phase 1).
// See midnight_world.h for the architectural contract.
#include "core/midnight_world.h"

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
    // a clinic/chopshop occasionally turns up elsewhere too
    if (rng.chance(20)) s |= SV_CLINIC;
    if (rng.chance(15)) s |= SV_ARMORSHOP;
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
    int m = r.between(28, MAX_AGENTS);
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
    }

    for (int i = 1; i < m; ++i) {
        Agent& a = w.agents[i];
        a.name_id = (uint8_t)r.range((uint32_t)agent_name_count());
        a.loc = (uint8_t)r.range((uint32_t)n);
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

    // --- events: empty ring -------------------------------------------------
    w.event_count = 0;
    w.event_head = 0;
}

// ---- Phase 2: economy + needs + basic behavior ------------------------------
MidTunables g_mtune;

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

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

// one agent's action this tick (deterministic; uses w.rng for ties/job pick)
static void agent_step(World& w, int idx) {
    const MidTunables& T = g_mtune;
    Agent& a = w.agents[idx];
    if (!(a.status & AF_ALIVE)) return;

    District& here = w.districts[a.loc];

    // --- needs decay --------------------------------------------------------
    bump_need(a.need[ND_HUNGER], T.hunger_rate);
    bump_need(a.need[ND_THIRST], T.thirst_rate);
    bump_need(a.need[ND_SOCIAL], T.social_rate);
    // safety pressure tracks local danger; stress eases when safe
    bump_need(a.need[ND_SAFETY], here.danger > 40 ? 2 : -2);
    bump_need(a.need[ND_STRESS], here.danger / 16 - T.stress_relief);

    // --- pick the most urgent vital ----------------------------------------
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
            bump_need(a.need[vital], -T.consume_relief);
            int s = here.supply[vcom] - T.consume_supply;
            here.supply[vcom] = (uint8_t)(s < 0 ? 0 : s);
            if (here.prosperity < 100) here.prosperity++;
            action = ACT_BUY;
        } else if ((int)a.money < price) {
            // can't afford -> need income
            if (a.status & AF_EMPLOYED) { a.money += (uint32_t)wage_of(w, a.loc, a); bump_need(a.need[ND_FATIGUE], T.fatigue_work); action = ACT_WORK; }
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
        if (a.status & AF_EMPLOYED) { a.money += (uint32_t)wage_of(w, a.loc, a); bump_need(a.need[ND_FATIGUE], T.fatigue_work); if (a.job < J_COUNT && a.skill[a.job] < 255) a.skill[a.job]++; action = ACT_WORK; }
        else if (here.services & SV_JOB_BOARD) { a.job = (uint8_t)w.rng.between(J_CONSTRUCTION, J_INFRA); a.status |= AF_EMPLOYED; action = ACT_SEEKJOB; }
        else { uint8_t nx = hop_to_service(w, a.loc, SV_JOB_BOARD); if (nx != a.loc) { a.loc = nx; action = ACT_MOVE; } else { action = ACT_REST; } }
    } else {
        action = ACT_REST;
    }

    if (action == ACT_REST) { bump_need(a.need[ND_FATIGUE], -T.fatigue_rest); bump_need(a.need[ND_SOCIAL], -4); }

    // --- starvation: a maxed vital hurts; sustained -> injury -> death -------
    if (a.need[ND_HUNGER] >= T.starve || a.need[ND_THIRST] >= T.starve) {
        bump_need(a.need[ND_STRESS], 8);
        if (a.status & AF_INJURED) { if (w.rng.chance(8)) a.status &= (uint8_t)~AF_ALIVE; }
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
            int rent = T.rent_base + w.districts[a.loc].prosperity / 8;
            if ((int)a.money >= rent) { a.money -= (uint32_t)rent; a.status &= (uint8_t)~AF_IN_DEBT; }
            else { a.money = 0; a.status |= AF_IN_DEBT; bump_need(a.need[ND_STRESS], 6); }
        }
    }

    w.tick++;
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
    wr.u8(d.population); wr.u8(d.prosperity); wr.u8(d.danger);
    wr.u16(d.services); wr.u8(d.deg);
    for (int k = 0; k < MAXDEG; ++k) wr.u8(d.adj[k]);
}
void get_district(Reader& rd, District& d) {
    d.type = rd.u8(); d.owner = rd.u8(); d.seed = rd.u32();
    for (int f = 0; f < F_COUNT; ++f) d.influence[f] = rd.u8();
    for (int c = 0; c < C_COUNT; ++c) d.supply[c] = rd.u8();
    for (int h = 0; h < HZ_COUNT; ++h) d.hazard[h] = rd.u8();
    d.population = rd.u8(); d.prosperity = rd.u8(); d.danger = rd.u8();
    d.services = rd.u16(); d.deg = rd.u8();
    for (int k = 0; k < MAXDEG; ++k) d.adj[k] = rd.u8();
}
void put_agent(Writer& wr, const Agent& a) {
    wr.u8(a.name_id); wr.u8(a.loc); wr.u8(a.faction);
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
    a.name_id = rd.u8(); a.loc = rd.u8(); a.faction = rd.u8();
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
    wr.u16(w.company.assets); wr.u8(w.company.reputation);
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
    w.company.assets = rd.u16(); w.company.reputation = rd.u8();
    w.event_count = rd.u8(); w.event_head = rd.u8();
    for (int i = 0; i < EVMAX; ++i) {
        Event& e = w.events[i];
        e.kind = rd.u8(); e.node = rd.u8(); e.agent = rd.u8(); e.data = rd.u8(); e.tick = rd.u16();
    }
    return rd.ok;
}

} // namespace mid
