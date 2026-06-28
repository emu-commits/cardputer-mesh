#include "core/cyberhack_world.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cyber {

Tunables g_tune;

// ---- content tables (flash) ------------------------------------------------
static const char* FACTION_NAME[F_COUNT] = {
    "Kurogane Holdings", "Chrome Vultures", "Null Sigil", "Greywall Security", "the Switchboard"
};
static const char* FACTION_SHORT[F_COUNT] = {
    "Kurogane", "the Vultures", "Null Sigil", "Greywall", "the Switchboard"
};
static const char* NODE_TYPE_NAME[N_TYPECOUNT] = {
    "corp vault", "gang server", "public grid", "abandoned node", "cold cache"
};
static const char* ICE_NAME[I_COUNT] = {
    "Black ICE", "Trace Daemon", "Data Warden", "Entropy Swarm", "Watchdog", "Sysop"
};
static const char* MODULE_NAME[M_COUNT] = { "Spike", "Mask", "Fork", "Patch", "Ghost" };
static const char* NAMED_POOL[] = {
    "GREYHOUND", "KRAKEN", "MOTHER", "LICH", "ARGUS", "BASILISK", "REVENANT",
    "MONOLITH", "WIDOW", "TALLOW", "CINDER", "HALCYON"
};
static const int NAMED_POOL_N = (int)(sizeof(NAMED_POOL) / sizeof(NAMED_POOL[0]));
static const char* NODE_PREFIX[] = {
    "GRID", "NODE", "VAULT", "RELAY", "GHOST", "CACHE", "SPIRE", "SUMP", "HOLLOW", "EXCHANGE"
};
static const int NODE_PREFIX_N = (int)(sizeof(NODE_PREFIX) / sizeof(NODE_PREFIX[0]));
static const char* PERSONA_NAME[PR_COUNT] = { "semi-sentient AI", "corporate sysop", "gang daemon", "rogue construct", "memory construct" };

// Weaknesses map only to the three damage routines (Spike/Mask/Fork) so a
// counter actually breaks the ICE faster. Ghost (disengage) and Patch (heal) are
// universal tactics, never a "counter". Black ICE and the Sysop punish Spike —
// raw aggression against the heavy ICE gets you flatlined.
static const uint8_t ICE_WEAK[I_COUNT]   = { M_FORK, M_MASK, M_SPIKE, M_FORK, M_SPIKE, M_MASK };
static const uint8_t ICE_PUNISH[I_COUNT] = { M_SPIKE, M_COUNT, M_COUNT, M_COUNT, M_COUNT, M_SPIKE };

const char* faction_name(uint8_t f) { return f < F_COUNT ? FACTION_NAME[f] : "freelancers"; }
const char* faction_short(uint8_t f) { return f < F_COUNT ? FACTION_SHORT[f] : "nobody"; }
const char* node_type_name(uint8_t t) { return t < N_TYPECOUNT ? NODE_TYPE_NAME[t] : "node"; }
const char* ice_name(uint8_t i) { return i < I_COUNT ? ICE_NAME[i] : "ICE"; }
const char* module_name(uint8_t m) { return m < M_COUNT ? MODULE_NAME[m] : "?"; }
const char* named_name(uint8_t id) { return id < NAMED_POOL_N ? NAMED_POOL[id] : "SOMETHING"; }
const char* persona_name(uint8_t p) { return p < PR_COUNT ? PERSONA_NAME[p] : "process"; }
uint8_t ice_weakness(uint8_t i) { return i < I_COUNT ? ICE_WEAK[i] : M_SPIKE; }
uint8_t ice_punish(uint8_t i) { return i < I_COUNT ? ICE_PUNISH[i] : M_COUNT; }

std::string node_label(const World& w, uint8_t n) {
    if (n >= w.node_count) return "?";
    char b[16];
    std::snprintf(b, sizeof b, "%s-%d", NODE_PREFIX[w.nodes[n].name_pre % NODE_PREFIX_N], (int)n + 1);
    return b;
}

// ---- RNG -------------------------------------------------------------------
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

// ---- balance maths ---------------------------------------------------------
int player_power(const RunState& r) {
    int mods = 0;
    for (int i = 0; i < M_COUNT; ++i) mods += r.mod_level[i];
    int p = g_tune.base_power + r.tier * g_tune.k_tier + mods * g_tune.k_module +
            r.shield * g_tune.k_shield - (r.corruption / (g_tune.p_corruption ? g_tune.p_corruption : 1));
    return p < 1 ? 1 : p;
}
int threat_level(const World& w, const RunState& r, const Node& n) {
    int t = n.security * g_tune.sec_threat + r.tier * g_tune.k_tier_threat + r.heat / (g_tune.k_heat ? g_tune.k_heat : 1);
    if (n.faction < F_COUNT) t += w.factions[n.faction].grudge * g_tune.k_grudge;
    if (r.hunt_active) t += g_tune.hunter_threat;
    return t < 1 ? 1 : t;
}

// ---- world generation ------------------------------------------------------
void Sim::gen_world(const Legends* prior) {
    World& w = world_;
    Rng cnet; cnet.seed(w.citynet_seed);

    w.node_count = (uint8_t)cnet.between(40, MAX_NODES);
    int cols = 1; while (cols * cols < w.node_count) ++cols;
    for (int i = 0; i < w.node_count; ++i) {
        Node& nd = w.nodes[i];
        nd = Node{};
        nd.x = (uint8_t)(i % cols);
        nd.y = (uint8_t)(i / cols);
        nd.name_pre = (uint8_t)cnet.range(NODE_PREFIX_N);
        // depth = distance-ish from entry (row); deeper = more secure
        int depth = nd.y;
        nd.faction = (uint8_t)cnet.range(F_COUNT);
        // type weighted by depth
        if (depth == 0) nd.type = N_PUBLIC;
        else {
            int roll = cnet.range(100);
            nd.type = roll < 30 ? N_VAULT : roll < 50 ? N_GANG : roll < 70 ? N_PUBLIC
                     : roll < 88 ? N_ABANDONED : N_SHRINE;
        }
        int base_sec = 1 + depth + run_.depth + (nd.type == N_VAULT ? 2 : nd.type == N_GANG ? 1 : 0);
        nd.security = (uint8_t)std::min(9, base_sec + (int)cnet.range(2));
        if (nd.type == N_PUBLIC || nd.type == N_SHRINE) nd.faction = (depth == 0) ? (uint8_t)F_SWITCHBOARD : nd.faction;
    }
    w.entry = 0;
    build_topology();
    place_content();
    apply_legends(prior);
}

void Sim::build_topology() {
    World& w = world_;
    w.edge_count = 0;
    Rng cnet; cnet.seed(w.citynet_seed ^ 0xEDuLL);
    auto add_edge = [&](int a, int b) {
        if (a == b) return;
        if (w.edge_count >= MAX_EDGES) return;
        Node& na = w.nodes[a]; Node& nb = w.nodes[b];
        if (na.deg >= 5 || nb.deg >= 5) return;
        for (int e = 0; e < w.edge_count; ++e)
            if ((w.edges[e].a == a && w.edges[e].b == b) || (w.edges[e].a == b && w.edges[e].b == a)) return;
        Edge& ed = w.edges[w.edge_count++];
        ed.a = (uint8_t)a; ed.b = (uint8_t)b;
        ed.risk = (uint8_t)std::max<int>(1, (na.security + nb.security) / 2);
        na.nbr[na.deg++] = (uint8_t)b;
        nb.nbr[nb.deg++] = (uint8_t)a;
    };
    // spanning tree → guaranteed connectivity (each node links to an earlier one)
    for (int i = 1; i < w.node_count; ++i) add_edge(i, (int)cnet.range(i));
    // a few extra edges for loops/branches
    int extra = w.node_count / 2;
    for (int k = 0; k < extra; ++k) add_edge((int)cnet.range(w.node_count), (int)cnet.range(w.node_count));
}

void Sim::place_content() {
    World& w = world_;
    Rng r; r.seed(w.run_seed);
    // objective: the most secure node, far from entry
    uint8_t best = w.entry; int bestscore = -1;
    for (int i = 0; i < w.node_count; ++i) {
        int sc = w.nodes[i].security * 4 + w.nodes[i].y * 3;
        if (sc > bestscore) { bestscore = sc; best = (uint8_t)i; }
    }
    w.objective.target = best;
    w.objective.kind = (uint8_t)r.range(3);
    w.objective.reward = (uint16_t)(w.nodes[best].security * r.between(20, 40));
    w.nodes[best].flags |= NF_OBJECTIVE;

    // guards + loot
    for (int i = 0; i < w.node_count; ++i) {
        Node& n = w.nodes[i];
        switch (n.type) {
            case N_VAULT:     n.guard_ice = n.security >= 6 ? I_BLACK : I_WARDEN; break;
            case N_GANG:      n.guard_ice = r.chance(50) ? I_SWARM : I_WATCHDOG; break;
            case N_ABANDONED: n.guard_ice = I_SWARM; break;
            case N_PUBLIC:    n.guard_ice = n.security >= 2 ? I_WATCHDOG : I_COUNT; break;
            case N_SHRINE:    n.guard_ice = I_COUNT; break;   // fabricator, no guard
            default:          n.guard_ice = I_WATCHDOG; break;
        }
        if (n.guard_ice == I_COUNT) n.flags |= NF_GUARD_DONE;  // unguarded
        n.guard_count = 1;
        if (n.type == N_VAULT || n.type == N_GANG || n.type == N_SHRINE)
            n.shards = (int16_t)(n.security * r.between(5, 15));
    }
    // the objective node is the climax boss
    if (w.nodes[best].guard_ice == I_COUNT) { w.nodes[best].guard_ice = I_BLACK; w.nodes[best].flags &= ~NF_GUARD_DONE; }
    w.nodes[best].guard_count = 1;

    // --- the gauntlet: escalating named ICE placed ALONG the route to the
    // objective, so the run is a forced descent. Each kill earns the Tier the
    // next fight demands; off-path hubs get the leftovers for world flavor.
    w.named_count = 0;
    auto unique_name = [&](uint32_t salt) {
        uint8_t id = (uint8_t)((w.run_seed ^ (salt * 2654435761u)) % NODE_PREFIX_N % NAMED_POOL_N);
        id = (uint8_t)((w.run_seed ^ salt) % NAMED_POOL_N);
        int guard = 0; bool dup;
        do { dup = false; for (int j = 0; j < w.named_count; ++j) if (w.named[j].name_id == id) dup = true;
             if (dup) id = (uint8_t)((id + 1) % NAMED_POOL_N); } while (dup && ++guard < NAMED_POOL_N);
        return id;
    };
    // named ICE get real archetypes (never the trivial Watchdog) so the
    // module×signature rock-paper-scissors actually matters at every fight.
    auto assign_named = [&](uint8_t node, int tier, uint32_t salt, uint8_t arch) {
        if (w.named_count >= MAX_NAMED) return;
        Node& n = w.nodes[node];
        n.guard_ice = arch;
        n.flags &= ~NF_GUARD_DONE;
        NamedIce& m = w.named[w.named_count];
        m.name_id = unique_name(salt);
        m.archetype = arch;
        m.tier = (uint8_t)std::min(60, std::max(1, tier));   // no tier ceiling deep in the dive
        m.faction = n.faction;
        m.status = NS_ALIVE;
        // persona: who's behind the ICE. Archetype drives it (so the gauntlet's
        // ARCH_CYCLE yields an even spread of minds), with faction/turf overriding
        // for flavor — gangs run daemons, Null Sigil breeds rogue constructs.
        switch (arch) {
            case I_SYSOP:                  m.persona = PR_SYSOP; break;
            case I_TRACE: case I_WATCHDOG: m.persona = PR_DAEMON; break;
            case I_SWARM:                  m.persona = PR_CONSTRUCT; break;
            default:                       m.persona = PR_AI; break;   // Black ICE / Warden
        }
        if (arch != I_SYSOP) {
            if (n.faction == F_VULTURES || n.type == N_GANG) m.persona = PR_DAEMON;
            else if (n.faction == F_NULLSIGIL || n.type == N_ABANDONED) m.persona = PR_CONSTRUCT;
        }
        // disposition: how talkable it is. AIs are curious, sysops are walls.
        switch (m.persona) {
            case PR_AI:        m.disposition = (uint8_t)r.between(55, 85); break;
            case PR_CONSTRUCT: m.disposition = (uint8_t)r.between(35, 75); break;
            case PR_DAEMON:    m.disposition = (uint8_t)r.between(40, 70); break;
            default:           m.disposition = (uint8_t)r.between(10, 40); break;  // sysop
        }
        n.guard_named = w.named_count;
        w.named_count++;
    };
    static const uint8_t ARCH_CYCLE[4] = { I_TRACE, I_WARDEN, I_SWARM, I_BLACK };

    // BFS path entry -> objective
    int prev[MAX_NODES]; for (int i = 0; i < w.node_count; ++i) prev[i] = -2;
    { uint8_t q[MAX_NODES]; int qh = 0, qt = 0; q[qt++] = w.entry; prev[w.entry] = -1;
      while (qh < qt) { uint8_t cur = q[qh++]; const Node& n = w.nodes[cur];
        for (int k = 0; k < n.deg; ++k) { uint8_t nb = n.nbr[k]; if (nb != NONE8 && prev[nb] == -2) { prev[nb] = cur; q[qt++] = nb; } } } }
    std::vector<uint8_t> path;
    for (int c = best; c >= 0; c = prev[c]) { path.push_back((uint8_t)c); if (prev[c] == -1) break; }
    std::reverse(path.begin(), path.end());

    // place up to 4 escalating named on intermediate path nodes (skip entry+objective)
    int placed = 0, depth = 0;
    for (size_t i = 1; i + 1 < path.size() && placed < 4; ++i) {
        uint8_t nd = path[i];
        ++depth;
        if (w.nodes[nd].security < 2 && (i % 2)) continue;       // let some breathing room
        assign_named(nd, 1 + depth + (int)i, 0x100u + (uint32_t)i, ARCH_CYCLE[placed % 4]);
        ++placed;
    }

    // the objective is always the boss — the climax that gates the haul
    Node& obj = w.nodes[best];
    uint8_t boss_arch = obj.security >= 8 ? I_SYSOP : I_BLACK;
    if (obj.guard_named == NONE8) assign_named(best, obj.security + 1, 0xB055u, boss_arch);
    else { w.named[obj.guard_named].tier = (uint8_t)std::min(60, obj.security + 1); w.named[obj.guard_named].archetype = boss_arch; obj.guard_ice = boss_arch; }

    // a couple of off-path named hubs for the legends to remember
    std::vector<uint8_t> cand;
    for (int i = 0; i < w.node_count; ++i)
        if (w.nodes[i].guard_named == NONE8 && w.nodes[i].security >= 5) cand.push_back((uint8_t)i);
    std::sort(cand.begin(), cand.end(), [&](uint8_t a, uint8_t b) { return w.nodes[a].security > w.nodes[b].security; });
    int off = 0;
    for (uint8_t ni : cand) { if (w.named_count >= MAX_NAMED) break; assign_named(ni, w.nodes[ni].security, 0x500u + ni, ARCH_CYCLE[off++ % 4]); }

    // --- pick the few NEGOTIABLE personalities. Everything else — the gauntlet ICE
    // and the boss — is a straight fight, so you can't appease your way past every
    // wall. Sysops and the objective boss never negotiate. Cap keeps parley special. ---
    uint8_t boss_named = w.nodes[best].guard_named;
    std::vector<uint8_t> neg_cand;
    for (int i = 0; i < w.named_count; ++i)
        if (i != boss_named && w.named[i].persona != PR_SYSOP) neg_cand.push_back((uint8_t)i);
    // a fresh name not used by another entity here nor already spoken-with this run
    auto fresh_name = [&](uint8_t cur) -> uint8_t {
        auto taken = [&](uint8_t id) {
            for (int j = 0; j < w.named_count; ++j) if (w.named[j].name_id == id) return true;
            return std::find(spoken_names_.begin(), spoken_names_.end(), id) != spoken_names_.end();
        };
        if (std::find(spoken_names_.begin(), spoken_names_.end(), cur) == spoken_names_.end()) return cur;
        for (int t = 1; t <= NAMED_POOL_N; ++t) { uint8_t c = (uint8_t)((cur + t) % NAMED_POOL_N); if (!taken(c)) return c; }
        return cur;
    };
    int neg_cap = 2, made = 0;
    while (made < neg_cap && !neg_cand.empty()) {
        int pick = (int)r.range((uint32_t)neg_cand.size());
        uint8_t ni = neg_cand[pick];
        neg_cand.erase(neg_cand.begin() + pick);
        NamedIce& m = w.named[ni];
        m.name_id = fresh_name(m.name_id);          // don't reuse a name you've parleyed this run
        m.negotiable = 1;
        if (r.chance(40)) m.persona = PR_MEMORY;    // some negotiables are dead minds (memory dives)
        spoken_names_.push_back(m.name_id);
        ++made;
    }
}

void Sim::apply_legends(const Legends* prior) {
    World& w = world_;
    for (int f = 0; f < F_COUNT; ++f) {
        int g = prior ? prior->grudge[f] : 0;
        w.factions[f].grudge = (int8_t)g;
        w.factions[f].attitude = g >= 4 ? A_HUNTING : g >= 2 ? A_HOSTILE : g <= -2 ? A_FRIENDLY : A_NEUTRAL;
    }
    if (!prior) return;
    // dead/crippled named carry their status; revived crippled come back tougher
    for (int i = 0; i < w.named_count; ++i) {
        for (int j = 0; j < prior->named_count; ++j) {
            if (prior->named[j].name_id == w.named[i].name_id) {
                if (prior->named[j].status == NS_DEAD) {
                    // dead ICE: this node loses its named guard (replaced by stock ICE)
                    for (int n = 0; n < w.node_count; ++n)
                        if (w.nodes[n].guard_named == i) w.nodes[n].guard_named = NONE8;
                } else if (prior->named[j].status == NS_CRIPPLED) {
                    w.named[i].tier = (uint8_t)std::min(60, w.named[i].tier + 2);  // back, angrier
                    w.named[i].grudge = prior->named[j].grudge;
                } else if (prior->named[j].status == NS_ALLIED) {
                    // a friend from a past run — holds the gate open, no fight
                    w.named[i].status = NS_ALLIED;
                    w.named[i].grudge = prior->named[j].grudge;
                    w.named[i].disposition = 90;
                }
            }
        }
    }
    // burned routes stay burned
    for (int b = 0; b < prior->burned_count; ++b)
        for (int e = 0; e < w.edge_count; ++e) {
            Edge& ed = w.edges[e];
            if ((ed.a == prior->burned[b].a && ed.b == prior->burned[b].b) ||
                (ed.a == prior->burned[b].b && ed.b == prior->burned[b].a))
                ed.flags |= EF_BURNED;
        }
    // marked nodes start hotter
    for (int m = 0; m < prior->marked_count; ++m) {
        uint8_t id = prior->marked[m];
        if (id < w.node_count) { w.nodes[id].flags |= NF_MARKED; w.nodes[id].security = (uint8_t)std::min(9, w.nodes[id].security + 1); }
    }
}

// ---- pathfinding -----------------------------------------------------------
bool Sim::edge_usable(const Edge& e) const { return !(e.flags & (EF_BURNED | EF_LOCKED)); }

int Sim::next_hop_toward(uint8_t target, bool ignore_locks) const {
    const World& w = world_;
    if (run_.pos == target) return -1;
    int prev[MAX_NODES]; for (int i = 0; i < w.node_count; ++i) prev[i] = -2;
    uint8_t q[MAX_NODES]; int qh = 0, qt = 0;
    q[qt++] = run_.pos; prev[run_.pos] = -1;
    while (qh < qt) {
        uint8_t cur = q[qh++];
        const Node& n = w.nodes[cur];
        for (int k = 0; k < n.deg; ++k) {
            uint8_t nb = n.nbr[k];
            if (nb == NONE8 || prev[nb] != -2) continue;
            // is the edge usable? (ignore_locks lets us force a path through a
            // temporary lockdown — locks are this-run; burns stay burned)
            bool ok = false;
            for (int e = 0; e < w.edge_count; ++e) {
                const Edge& ed = w.edges[e];
                if (((ed.a == cur && ed.b == nb) || (ed.a == nb && ed.b == cur)) &&
                    (edge_usable(ed) || (ignore_locks && !(ed.flags & EF_BURNED)))) { ok = true; break; }
            }
            if (!ok) continue;
            prev[nb] = cur;
            if (nb == target) {
                // walk back to the first hop from pos
                uint8_t step = nb;
                while (prev[step] != (int)run_.pos) { if (prev[step] < 0) return -1; step = (uint8_t)prev[step]; }
                return step;
            }
            q[qt++] = nb;
        }
    }
    return -1;
}

// ---- event/log helpers -----------------------------------------------------
void Sim::logline(const std::string& s) {
    log_.push_back(s);
    if (log_.size() > 80) log_.erase(log_.begin(), log_.begin() + (log_.size() - 80));
}
void Sim::push_event(uint8_t tag, uint8_t node, uint8_t ice, uint8_t named, int8_t heat_d) {
    Event& e = events_[ev_head_];
    e.step = run_.step; e.node = node; e.ice = ice; e.named = named; e.tag = tag; e.heat_d = heat_d;
    e.faction = node < world_.node_count ? world_.nodes[node].faction : F_COUNT;
    ev_head_ = (uint8_t)((ev_head_ + 1) % EVENT_RING);
    if (ev_count_ < EVENT_RING) ev_count_++;
}

void Sim::add_heat(int d) {
    int h = (int)run_.heat + d;
    run_.heat = (uint8_t)std::max(0, std::min(255, h));
}
void Sim::add_corruption(int d) {
    int c = (int)run_.corruption + d;
    run_.corruption = (uint8_t)std::max(0, std::min(100, c));
    if (run_.corruption >= 100 && run_.outcome == O_RUNNING) {
        run_.outcome = O_DIED; run_.death_cause = D_CORRUPTION;
    }
}
void Sim::hurt(int d, uint8_t cause) {
    if (d < 0) d = 0;
    run_.integrity = (int16_t)(run_.integrity - d);
    if (run_.integrity < 0) run_.integrity = 0;            // always clamp (death may already be set)
    if (run_.integrity == 0 && run_.outcome == O_RUNNING) {
        run_.outcome = O_DIED; run_.death_cause = cause;
    }
}
void Sim::heal(int d) { run_.integrity = (int16_t)std::min((int)run_.integrity_max, run_.integrity + d); }

void Sim::grant_loot(Node& n) {
    if ((n.flags & NF_LOOTED) || n.shards <= 0) return;
    run_.shards = (uint16_t)(run_.shards + n.shards);
    logline(std::string("+") + std::to_string((int)n.shards) + " data shards (total " + std::to_string((int)run_.shards) + ").");
    n.flags |= NF_LOOTED;
}

// The avatar walked onto the cache in the room view: bank this node's loot now,
// but only once its ICE is down (you don't pocket the data past a live guard).
void Sim::collect_current_node() {
    Node& here = world_.nodes[run_.pos];
    if ((here.flags & NF_GUARD_DONE) || here.guard_ice >= I_COUNT) grant_loot(here);
}

// A level-up grows the whole deck, not just its bite: every Tier raises max
// integrity and buffer (and, every other tier, shielding), and tops them up so
// the gain is felt immediately. Power growth (Tier/modules) is handled by callers.
void Sim::apply_tier_growth() {
    // Growth is real but deliberately SUB-linear vs. the dive's escalation: every
    // named kill nudges the deck up a little, but the pool can't outrun how mean the
    // deep layers get — so eventually you have to read the danger and jack out.
    run_.integrity_max = (int16_t)std::min(255, run_.integrity_max + 3);
    heal(3);
    run_.buffer_max = (int16_t)std::min(120, run_.buffer_max + 2);
    run_.buffer = (int16_t)std::min((int)run_.buffer_max, run_.buffer + 2);
    if (run_.tier % 3 == 0 && run_.shield < 9) run_.shield++;
    logline(std::string("** GROWTH ** max integrity ") + std::to_string((int)run_.integrity_max) +
            ", max buffer " + std::to_string((int)run_.buffer_max) + ", shield " + std::to_string((int)run_.shield) + ".");
}

// ---- lifecycle -------------------------------------------------------------
void Sim::start(uint32_t citynet_seed, uint32_t run_seed, uint8_t personality, const Legends* prior) {
    world_ = World{};
    world_.citynet_seed = citynet_seed;
    world_.run_seed = run_seed;
    run_ = RunState{};
    run_.personality = personality;
    run_.shards = 40;   // a little credit on the deck — bribe/appease are live from node 1
    rng_.seed(((uint64_t)citynet_seed << 21) ^ run_seed ^ 0xC0FFEEuLL);
    log_.clear();
    ev_head_ = ev_count_ = 0;
    pending_ = false;
    committed_branch_ = NONE8;
    survival_warned_ = false;
    branched_.clear();
    spoken_names_.clear();
    gen_world(prior);
    run_.pos = world_.entry;
    run_.hunter_pos = world_.entry;
    world_.nodes[run_.pos].flags |= NF_VISITED;
    push_event(T_JACKIN, run_.pos, I_COUNT, NONE8, 0);
    logline(std::string("jacked into ") + node_label(world_, run_.pos) + ".");
}

// ---- the step machine ------------------------------------------------------
// If a combat round is pending and auto-combat is on, resolve ONE round via the
// personality AI (no rng_ entropy consumed for the choice) and report a step so
// the UI animates it; otherwise surface the decision to the caller.
AdvanceResult Sim::decide_or_auto() {
    if (auto_combat_ && pending_ && dec_.kind == DK_ENCOUNTER) {
        choose(ai_decide(run_.personality, run_, dec_, rng_));
        return AR_STEPPED;
    }
    return AR_DECISION;
}

AdvanceResult Sim::advance() {
    if (run_.outcome != O_RUNNING) return AR_ENDED;
    if (pending_) return decide_or_auto();
    if (run_.step >= MAX_STEPS) {
        if (run_.objective_done) { run_.outcome = O_EXTRACTED; }
        else { run_.outcome = O_DIED; run_.death_cause = D_TIMEOUT; }
        return AR_ENDED;
    }

    Node& here = world_.nodes[run_.pos];
    here.flags |= NF_VISITED;

    // 1) hunter caught up?
    if (run_.hunt_active) {
        advance_hunter();
        if (run_.hunter_pos == run_.pos) {
            uint8_t named = run_.hunter_named;
            uint8_t ice = named != NONE8 ? world_.named[named].archetype : I_BLACK;
            push_event(T_HUNT, run_.pos, ice, named, 0);
            begin_fight(run_.pos, ice, named, true);
            return decide_or_auto();
        }
    }

    // 2) guard at this node: named ICE → a real multi-round fight; trivial ICE
    // the deck sweeps on its own (a small toll in buffer + integrity).
    if (!(here.flags & NF_GUARD_DONE) && here.guard_ice < I_COUNT) {
        // a lone watchdog the deck sweeps; anything else is a break-in of one or
        // more ICE fought in sequence (node_clears tracks progress here).
        bool trivial = (here.guard_count <= 1 && here.guard_named == NONE8 && here.guard_ice == I_WATCHDOG);
        if (trivial) { auto_trivial(here.guard_ice); here.flags |= NF_GUARD_DONE; return AR_STEPPED; }
        // a named entity is a mind, not just a wall: an ally waves you in, a
        // stranger gets one chance to be talked down before the deck commits.
        if (here.guard_named != NONE8) {
            NamedIce& m = world_.named[here.guard_named];
            if (m.status == NS_ALLIED) {
                here.flags |= NF_GUARD_DONE;
                grant_loot(here);
                push_event(T_ALLY, run_.pos, m.archetype, here.guard_named, 0);
                logline(std::string(named_name(m.name_id)) + " holds the gate open for you — an old debt repaid.");
                return AR_STEPPED;
            }
            // only the few recurring personalities can be talked to; gauntlet ICE and
            // the boss are non-negotiable — you fight your way through them.
            if (m.negotiable && !(here.flags & NF_PARLEYED)) {
                if (m.persona == PR_MEMORY) open_memory(); else open_parley();
                return AR_DECISION;
            }
        }
        bool boss = (run_.node_clears >= here.guard_count - 1) && here.guard_named != NONE8;
        begin_fight(run_.pos, here.guard_ice, boss ? here.guard_named : NONE8, false);
        return decide_or_auto();
    }

    // 3) objective reached
    if (run_.pos == world_.objective.target && !run_.objective_done) {
        open_extract();
        return AR_DECISION;
    }

    // 4) survival spike (once per dip below the line)
    int int_pct = run_.integrity_max ? (run_.integrity * 100 / run_.integrity_max) : 0;
    if (!survival_warned_ && (int_pct < g_tune.low_int_pct || run_.heat >= g_tune.heat_t3)) {
        survival_warned_ = true;
        open_survival();
        return AR_DECISION;
    }
    if (int_pct >= g_tune.low_int_pct + 15 && run_.heat < g_tune.heat_t3) survival_warned_ = false;

    // 5) branch at a junction (once per node)
    bool already = std::find(branched_.begin(), branched_.end(), run_.pos) != branched_.end();
    if (!run_.exfil && !already && here.deg >= 2 && rng_.chance(55)) {
        branched_.push_back(run_.pos);
        open_branch();
        if (pending_) return AR_DECISION;   // only if there were ≥2 real options
    }

    // 6) move
    accrue_and_move();
    check_thresholds();
    return AR_STEPPED;
}

void Sim::advance_hunter() {
    // hunter walks one hop toward the player
    const World& w = world_;
    uint8_t from = run_.hunter_pos, target = run_.pos;
    if (from == target) return;
    int prev[MAX_NODES]; for (int i = 0; i < w.node_count; ++i) prev[i] = -2;
    uint8_t q[MAX_NODES]; int qh = 0, qt = 0; q[qt++] = from; prev[from] = -1;
    while (qh < qt) {
        uint8_t cur = q[qh++];
        const Node& n = w.nodes[cur];
        for (int k = 0; k < n.deg; ++k) {
            uint8_t nb = n.nbr[k];
            if (nb == NONE8 || prev[nb] != -2) continue;
            prev[nb] = cur;
            if (nb == target) { uint8_t step = nb; while (prev[step] != (int)from) step = (uint8_t)prev[step]; run_.hunter_pos = step; return; }
            q[qt++] = nb;
        }
    }
}

void Sim::accrue_and_move() {
    Node& here = world_.nodes[run_.pos];
    bool clinic_fresh = (here.type == N_SHRINE) && !(here.flags & NF_LOOTED);
    grant_loot(here);                                          // sets NF_LOOTED — read clinic_fresh first

    // --- recovery happens every tick of the crawl. A black clinic is a full
    // rebuild bay (the payoff for routing toward recovery); quiet dark grids
    // let the deck trickle back; surveilled corp turf notices you and heats up. ---
    if (clinic_fresh) {
        int hp = run_.integrity_max / 3;
        heal(hp); add_heat(-12);
        if (run_.corruption > 0) add_corruption(-8);
        run_.buffer = run_.buffer_max;
        here.flags |= NF_LOOTED;                                // a cold cache gives once
        logline(std::string("cold cache at ") + node_label(world_, run_.pos) +
                " — restored your deck from a clean backup. integrity " + std::to_string((int)run_.integrity) +
                "/" + std::to_string((int)run_.integrity_max) + ", heat -12, corruption -8, buffer full.");
    } else {
        if (here.security <= 2) {                               // the deep dark cools and mends
            add_heat(-1);
            if (run_.integrity < run_.integrity_max) heal(2);  // a slow knit between fights
        } else if (here.faction < F_COUNT && here.security >= 4) add_heat(2);
        if (run_.buffer < run_.buffer_max)                     // buffer always trickles a little
            run_.buffer = (int16_t)std::min((int)run_.buffer_max, run_.buffer + 2);
    }
    run_.step++;

    // descend toward this layer's objective (no exfil — you jack out by choice).
    int hop = committed_branch_ != NONE8 ? committed_branch_ : next_hop_toward(world_.objective.target);
    committed_branch_ = NONE8;
    // Boxed in: lockdowns/burns sealed every open route. There is no hard end here —
    // you tear through a sealed route, and it costs you (the deeper you are, the more
    // it bites). Only if even that is impossible (a true island) do you have to bail.
    if (hop < 0) {
        hop = next_hop_toward(world_.objective.target, /*ignore_locks=*/true);
        if (hop < 0) { jack_out(); return; }   // genuinely isolated — pull out with the haul
        int toll = 6 + run_.depth * 2;
        if (run_.buffer >= 4) run_.buffer = (int16_t)(run_.buffer - 4); else add_corruption(4);
        add_heat(6); add_corruption(3);
        // mark the forced edge clear so we don't re-bash it every step
        for (int e = 0; e < world_.edge_count; ++e) {
            Edge& ed = world_.edges[e];
            if (((ed.a == run_.pos && ed.b == (uint8_t)hop) || (ed.a == (uint8_t)hop && ed.b == run_.pos)) && (ed.flags & EF_LOCKED))
                { ed.flags &= ~EF_LOCKED; break; }
        }
        logline(std::string("routes sealed — tore through the lockdown to ") + node_label(world_, (uint8_t)hop) +
                ". it cost you (heat +6, corruption +3).");
        hurt(toll, D_TRACE);                   // can flatline you if you're already on the brink
        if (run_.outcome != O_RUNNING) return;
    }
    run_.pos = (uint8_t)hop;
    run_.node_clears = 0;                 // fresh node = fresh break-in
    world_.nodes[run_.pos].flags |= NF_VISITED;

    // --- heat draws patrols. The hotter your trail, the more roaming daemons cut
    // across your route: small auto-swept scuffles that cost a little and, more
    // importantly, keep interrupting the descent. Running loud should FEEL loud. ---
    int patrol_chance = ((int)run_.heat - 20) * 3 / 4;       // 0 below heat 20, ramps up
    if (patrol_chance > 70) patrol_chance = 70;
    Node& dest = world_.nodes[run_.pos];
    bool dest_open = (dest.flags & NF_GUARD_DONE) || dest.guard_ice >= I_COUNT;
    if (patrol_chance > 0 && dest_open && rng_.chance(patrol_chance)) {
        uint8_t pice = rng_.chance(55) ? I_TRACE : I_WATCHDOG;   // common ICE, nothing named
        int cost = 2;
        if (run_.buffer >= cost) run_.buffer = (int16_t)(run_.buffer - cost); else add_corruption(2);
        hurt(std::max(1, (int)dest.security / 2), D_ICE);
        add_heat(1);                                          // the scuffle adds a little more noise
        if (pice == I_TRACE) add_heat(2);
        run_.ice_killed++;
        logline(std::string("a roaming ") + ice_name(pice) + " patrol cut your route at " +
                node_label(world_, run_.pos) + " — swept it, but it cost you.");
    }
}

void Sim::check_thresholds() {
    if (!run_.hunt_active && run_.heat >= g_tune.heat_t2 && run_.step >= run_.next_hunt_step) {
        // spawn a hunter from the most-aggrieved faction
        int worst = 0; for (int f = 1; f < F_COUNT; ++f) if (world_.factions[f].grudge > world_.factions[worst].grudge) worst = f;
        run_.hunt_active = true;
        run_.hunter_faction = (uint8_t)worst;
        run_.hunter_pos = world_.entry;
        // pick a fully-intact named of that faction as the hunter, else generic
        run_.hunter_named = NONE8;
        for (int i = 0; i < world_.named_count; ++i)
            if (world_.named[i].faction == worst && world_.named[i].status == NS_ALIVE
                && world_.named[i].persona != PR_MEMORY) { run_.hunter_named = (uint8_t)i; break; }  // a dead mind doesn't hunt
        push_event(T_HUNT, run_.pos, I_BLACK, run_.hunter_named, 0);
        logline(run_.hunter_named != NONE8
                ? std::string(faction_short(worst)) + " loosed " + named_name(world_.named[run_.hunter_named].name_id) + " on your trail."
                : std::string(faction_short(worst)) + " sent a hunter onto the grid.");
    }
    if (run_.heat >= g_tune.heat_t3) {
        // lockdown: burn a random usable edge
        for (int tries = 0; tries < 8; ++tries) {
            int e = (int)rng_.range(world_.edge_count);
            if (edge_usable(world_.edges[e])) { world_.edges[e].flags |= EF_LOCKED; push_event(T_LOCKDOWN, run_.pos, I_COUNT, NONE8, 0); break; }
        }
    }
}

void Sim::auto_trivial(uint8_t ice) {
    // a watchdog/minor daemon handled by the deck without bothering the operator
    int cost = 2;
    if (run_.buffer >= cost) run_.buffer = (int16_t)(run_.buffer - cost); else add_corruption(2);
    if (ice == I_SWARM) add_corruption(3);
    // even a trivial sweep costs a little integrity — a long crawl bleeds you
    hurt(std::max(1, (int)world_.nodes[run_.pos].security - 1), D_ICE);
    add_heat(2);
    run_.ice_killed++;
    logline(std::string("deck swept a ") + ice_name(ice) + " at " + node_label(world_, run_.pos) + ".");
}

// ---- multi-round ICE fights (the hard-fought sieges) -----------------------
// A named ICE has its own integrity; you wear it down over several rounds while
// it claws at you. Each round you commit a module that costs scarce Buffer; when
// the Buffer runs dry you overclock — corruption and a harder hit. The choices
// that matter: race it with Spike (Black ICE/Sysop punish that), spend your
// counter, Patch mid-fight (a wasted round of break), or Ghost out and walk away
// from the score. This is where runs are won and lost.
static int buffer_cost(uint8_t mod) {
    switch (mod) { case M_SPIKE: return 2; case M_MASK: return 4; case M_FORK: return 5;
                   case M_PATCH: return 5; case M_GHOST: return 4; default: return 3; }
}
void Sim::begin_fight(uint8_t node, uint8_t ice, uint8_t named, bool hunter) {
    run_.in_fight = true;
    run_.fight_node = node;
    run_.fight_ice = ice;
    run_.fight_named = named;
    run_.fight_round = 0;
    run_.fork_active = false;
    run_.is_hunter_fight = hunter;
    flatline_used_ = false;
    int tier = named != NONE8 ? world_.named[named].tier : (int)world_.nodes[node].security;
    // ICE grows tougher the deeper you push — there is no floor on how mean a layer
    // can get. Depth makes it tankier (longer sieges = more claw-back).
    run_.ice_hp_max = (int16_t)std::min(800, 12 + tier * 3 + run_.depth * 6);
    run_.ice_hp = run_.ice_hp_max;
    std::string who = named != NONE8 ? std::string(named_name(world_.named[named].name_id)) + " [" + ice_name(ice) + "]"
                                     : std::string(ice_name(ice));
    // Boss ICE gets a loud announcement tick so you know it's a real wall.
    bool boss = (named != NONE8) && (tier >= 6 || (world_.nodes[node].flags & NF_OBJECTIVE));
    if (boss)
        logline(std::string("!! BOSS ICE !! ") + named_name(world_.named[named].name_id) + " [" +
                ice_name(ice) + "] tier " + std::to_string(tier) + " — a wall. brace the deck.");
    logline(hunter ? who + " has run you down at " + node_label(world_, node) + "."
                   : who + " stands in the way at " + node_label(world_, node) + ".");
    // intel: spell out the counter so the fight is legible, not a black box
    logline(std::string("  intel: weak to ") + module_name(ice_weakness(ice)) +
            (ice_punish(ice) != M_COUNT ? std::string("; punishes ") + module_name(ice_punish(ice)) : std::string("")) + ".");
    open_fight_round();
}
void Sim::open_fight_round() {
    pending_ = true;
    dec_ = Decision{};
    dec_.kind = DK_ENCOUNTER;
    dec_.node = run_.fight_node; dec_.ice = run_.fight_ice; dec_.named = run_.fight_named;
    std::string who = run_.fight_named != NONE8 ? named_name(world_.named[run_.fight_named].name_id) : ice_name(run_.fight_ice);
    char b[96];
    std::snprintf(b, sizeof b, "%s [%s]  ICE %d/%d   buffer %d", who.c_str(), ice_name(run_.fight_ice),
                  (int)run_.ice_hp, (int)run_.ice_hp_max, (int)run_.buffer);
    dec_.prompt = b;
    dec_.options = { "Spike (raw break)", "Mask (blunt its hit)", "Fork (helper)", "Patch (heal)", "Ghost (disengage)" };
    dec_.opt_module = { M_SPIKE, M_MASK, M_FORK, M_PATCH, M_GHOST };
}
void Sim::resolve_round(int option) {
    uint8_t mod = dec_.opt_module[option];
    uint8_t ice = run_.fight_ice, named = run_.fight_named;
    run_.fight_round++;

    // pay the Buffer toll; running dry forces an overclock (corruption + a worse hit)
    int cost = buffer_cost(mod);
    bool overclock = run_.buffer < cost;
    if (!overclock) run_.buffer = (int16_t)(run_.buffer - cost);
    else add_corruption(5);                               // running the buffer dry rots the deck

    // corruption can misfire any module mid-fight
    bool misfire = (run_.corruption > 60 && rng_.chance(run_.corruption / 2));

    // --- Ghost: attempt to break contact and slip the fight ---
    if (mod == M_GHOST) {
        add_corruption(2);
        int chance = 55 + run_.mod_level[M_GHOST] * 6 - (named != NONE8 ? world_.named[named].tier * 3 : 0);
        if (overclock) chance -= 20;
        if (misfire) chance -= 30;
        if (rng_.chance(chance)) { logline("ghosted out — broke contact, left the score behind."); finish_fight(false, true); return; }
        logline("the phase-out failed — still locked in.");
    }

    // --- your break against the ICE this round ---
    int brk = 0;
    if (mod == M_SPIKE) brk = 9 + run_.mod_level[M_SPIKE];
    else if (mod == M_FORK) { brk = 5 + run_.mod_level[M_FORK]; run_.fork_active = true; }
    else if (mod == M_MASK) brk = 3 + run_.mod_level[M_MASK];
    // PATCH/GHOST deal no break this round
    if (mod == ice_weakness(ice)) brk += 12;             // the right tool bites deep — fast, efficient kills
    if (overclock) brk = brk * 3 / 5;
    if (misfire) { brk = brk / 3; logline("module misfired — corruption bleeding through."); }
    if (run_.fork_active && mod != M_FORK) brk += 4 + run_.mod_level[M_FORK]; // helper keeps chipping
    run_.ice_hp = (int16_t)(run_.ice_hp - brk);

    // --- module side-effects ---
    if (mod == M_SPIKE) { add_heat(3); add_corruption(1); }   // noisy AND dirty: raw break corrodes
    if (mod == M_MASK)  add_heat(-6);
    if (mod == M_PATCH) {
        int amt = !overclock ? 16 + run_.mod_level[M_PATCH] * 3 : 6;
        heal(amt); add_corruption(-3);
        logline("patched mid-break — integrity " + std::to_string(run_.integrity) + ".");
    }

    // --- the ICE claws back ---
    if (run_.ice_hp > 0) {
        int tier = named != NONE8 ? world_.named[named].tier : (int)world_.nodes[run_.fight_node].security;
        // deeper layers hit harder, on top of the node's own security/tier — the
        // dive escalates until you can't take the hits anymore.
        int atk = world_.nodes[run_.fight_node].security / 2 + tier / 2 + run_.depth + rng_.between(-1, 3);
        if (mod == M_MASK) atk = atk / 3;                 // mask blunts the incoming hit
        if (mod == ice_punish(ice)) atk = atk * 2 + tier; // punished: raw aggression vs heavy ICE gets you mauled
        atk -= run_.shield;
        if (ice == I_SWARM) add_corruption(3);
        if (ice == I_TRACE) add_heat(4);
        uint8_t cause = (run_.is_hunter_fight) ? D_HUNTED : D_ICE;
        int dmg = std::max(1, atk);
        // do-or-die: a hit that would flatline you pauses the siege for one panic
        // decision (once per fight). The damage is held, not yet applied.
        if (dmg >= run_.integrity && !flatline_used_) {
            flatline_used_ = true;
            flatline_dmg_ = (int16_t)dmg;
            flatline_cause_ = cause;
            open_flatline();
            return;
        }
        hurt(dmg, cause);
    }

    // --- per-round readout: which module your deck (its directive = personality)
    // committed, whether it was the counter, what it cost — so the auto-fight is
    // legible and you can see the personality acting round to round. ---
    if (mod == M_SPIKE || mod == M_MASK || mod == M_FORK) {
        static const char* PN[] = { "Reckless", "Cautious", "Opportunist", "Loyalist" };
        char lb[120];
        std::snprintf(lb, sizeof lb, "[%s] %s%s%s — %d break, ICE %d/%d",
                      PN[run_.personality % 4], module_name(mod),
                      (mod == ice_weakness(ice) ? " COUNTER" : ""),
                      (overclock ? " (overclock!)" : (mod == M_SPIKE ? " heat+3" : mod == M_MASK ? " heat-6" : "")),
                      brk, (int)(run_.ice_hp < 0 ? 0 : run_.ice_hp), (int)run_.ice_hp_max);
        logline(lb);
    }

    // --- resolve fight end ---
    if (run_.outcome != O_RUNNING) { run_.in_fight = false; return; }   // died mid-fight
    if (run_.ice_hp <= 0) { finish_fight(true, false); return; }
    if (run_.fight_round >= 8) { finish_fight(false, true); return; }   // stalemate → it breaks off
    open_fight_round();                                                  // another round
}
void Sim::finish_fight(bool won, bool escaped) {
    run_.in_fight = false;
    pending_ = false;
    uint8_t ice = run_.fight_ice, named = run_.fight_named;
    Node& node = world_.nodes[run_.fight_node];

    if (escaped && !won) {
        run_.fork_active = false;
        node.flags |= NF_GUARD_DONE;     // slipped past — the whole node is bypassed (no loot)
        return;
    }
    // won this ICE
    run_.fork_active = false;
    run_.ice_killed++;
    run_.node_clears++;
    if (named != NONE8) {
        run_.named_killed++;
        int old_power = player_power(run_);
        run_.tier = (uint8_t)std::min(99, run_.tier + 1);   // no cap on the dive — deck keeps climbing
        apply_tier_growth();
        bool boss = world_.named[named].tier >= 7;
        world_.named[named].status = boss ? NS_CRIPPLED : NS_DEAD;
        if (node.faction < F_COUNT) world_.factions[node.faction].grudge += 2;
        push_event(boss ? T_HUMILIATION : T_FIRST_BLOOD, run_.fight_node, ice, named, (int8_t)run_.heat);
        logline(std::string(boss ? "crippled " : "burned ") + named_name(world_.named[named].name_id) + ", left it as static.");
        logline(std::string("** TIER UP -> T") + std::to_string((int)run_.tier) + " ** power " +
                std::to_string(old_power) + " -> " + std::to_string(player_power(run_)) + ".");
        if (run_.hunt_active && named == run_.hunter_named) { run_.hunt_active = false; run_.hunter_named = NONE8; run_.next_hunt_step = (uint16_t)(run_.step + 12); }
    } else {
        push_event(T_FIRST_BLOOD, run_.fight_node, ice, NONE8, 0);
    }
    // node cleared once all its ICE are down → loot + move on
    if (run_.node_clears >= node.guard_count) {
        node.flags |= NF_GUARD_DONE;
        if (node.shards > 0 && !(node.flags & NF_LOOTED)) {
            grant_loot(node); add_heat(5);
            if (node.faction < F_COUNT) world_.factions[node.faction].grudge += 1;
            push_event(T_BIG_SCORE, run_.fight_node, ice, named, (int8_t)run_.heat);
            logline(std::string("cracked ") + node_label(world_, run_.fight_node) + " — payload's yours.");
        }
    }
    if (run_.integrity * 100 / run_.integrity_max < 25)
        push_event(T_CLOSE_CALL, run_.fight_node, ice, named, 0);
}

// ---- flatline: a killing blow, held on the brink, surfaced as a do-or-die -----
// Combat is otherwise hands-off, but a lethal hit is the run's climax — the one
// moment we hand the wheel back. Three ways out: jack the cord (live, end the run
// poorer), burn your best module (live, keep fighting, lasting scar), or ride it
// (free gamble: scrape through at a thread, or flatline for real).
void Sim::open_flatline() {
    pending_ = true;
    dec_ = Decision{};
    dec_.kind = DK_FLATLINE; dec_.node = run_.fight_node; dec_.ice = run_.fight_ice; dec_.named = run_.fight_named;
    std::string who = run_.fight_named != NONE8 ? named_name(world_.named[run_.fight_named].name_id) : ice_name(run_.fight_ice);
    char b[112];
    std::snprintf(b, sizeof b, "FLATLINE. %s's hit (%d) will kill you. The deck screams white.", who.c_str(), (int)flatline_dmg_);
    dec_.prompt = b;
    int hi = 0; for (int i = 1; i < M_COUNT; ++i) if (run_.mod_level[i] > run_.mod_level[hi]) hi = i;
    bool can_burn = run_.mod_level[hi] >= 2;
    dec_.options.clear(); dec_.opt_module.clear();
    dec_.options.push_back("Jack the cord - live, lose 40% of the haul"); dec_.opt_module.push_back(M_COUNT);
    if (can_burn) {
        char mb[56]; std::snprintf(mb, sizeof mb, "Burn %s - live at 1 intg, halve it for the run", module_name(hi));
        dec_.options.push_back(mb); dec_.opt_module.push_back((uint8_t)hi);
    }
    dec_.options.push_back("Ride it - 45%: scrape to 1 intg, else flatline"); dec_.opt_module.push_back(M_COUNT);
    logline(dec_.prompt);
}
void Sim::resolve_flatline(int option) {
    uint8_t mod = (option >= 0 && option < (int)dec_.opt_module.size()) ? dec_.opt_module[option] : M_COUNT;
    uint8_t ice = run_.fight_ice, named = run_.fight_named;
    auto resume = [&]() {                                   // pick the siege back up
        if (run_.ice_hp <= 0) finish_fight(true, false);
        else if (run_.fight_round >= 8) finish_fight(false, true);
        else open_fight_round();
    };

    if (mod != M_COUNT) {                                   // BURN A MODULE
        int old = run_.mod_level[mod];
        run_.mod_level[mod] = (uint8_t)std::max(1, old / 2);
        run_.integrity = 1;
        add_corruption(2);
        push_event(T_FLATLINE, run_.fight_node, ice, named, 0);
        logline(std::string("** SACRIFICE ** burned ") + module_name(mod) + " (L" + std::to_string(old) +
                " -> L" + std::to_string((int)run_.mod_level[mod]) + ") to live. integrity 1.");
        resume();
        return;
    }
    if (option == 0) {                                      // JACK THE CORD
        run_.in_fight = false;
        int lost = run_.shards * 2 / 5;
        run_.shards = (uint16_t)(run_.shards - lost);
        push_event(T_SACRIFICE, run_.fight_node, ice, named, 0);
        logline(std::string("** JACKED THE CORD ** ripped the trodes — alive, but ") +
                std::to_string(lost) + " shards scrambled in the pull.");
        jack_out();                                        // banks what's left, ends the run
        return;
    }
    // RIDE IT
    if (rng_.chance(45)) {
        run_.integrity = 1;
        push_event(T_FLATLINE, run_.fight_node, ice, named, 0);
        logline("** RODE THE FLATLINE ** the deck held by a thread. integrity 1.");
        resume();
    } else {
        logline("** FLATLINE ** the deck went to white noise. you didn't come back.");
        hurt(flatline_dmg_, flatline_cause_);              // applies death
        run_.in_fight = false;
    }
}
void Sim::open_branch() {
    const Node& here = world_.nodes[run_.pos];
    std::vector<uint8_t> opts;
    for (int k = 0; k < here.deg; ++k) {
        uint8_t nb = here.nbr[k];
        if (nb == NONE8 || (world_.nodes[nb].flags & NF_VISITED)) continue;
        bool usable = false;
        for (int e = 0; e < world_.edge_count; ++e)
            if (((world_.edges[e].a == run_.pos && world_.edges[e].b == nb) || (world_.edges[e].a == nb && world_.edges[e].b == run_.pos)) && edge_usable(world_.edges[e])) usable = true;
        if (usable) opts.push_back(nb);
    }
    if (opts.size() < 2) return;  // not really a branch
    std::sort(opts.begin(), opts.end(), [&](uint8_t a, uint8_t b) { return world_.nodes[a].security < world_.nodes[b].security; });
    if (opts.size() > 4) opts.resize(4);
    pending_ = true;
    dec_ = Decision{};
    dec_.kind = DK_BRANCH; dec_.node = run_.pos;
    dec_.prompt = std::string("Fork in the route at ") + node_label(world_, run_.pos) + ".";
    branch_opts_ = opts;
    for (uint8_t nb : opts) {
        const Node& nn = world_.nodes[nb];
        // spell out what's down each route so the player can steer AROUND a wall
        // toward loot or recovery — the fork is a real routing decision, not a coin flip.
        std::string tag;
        if (nn.guard_named != NONE8 && !(nn.flags & NF_GUARD_DONE))
            tag = std::string("DANGER ") + named_name(world_.named[nn.guard_named].name_id);
        else if (nn.type == N_SHRINE) tag = "RECOVERY cache";
        else if (nn.shards > 0 && !(nn.flags & NF_LOOTED)) tag = "loot";
        else tag = "quiet";
        char b[64];
        std::snprintf(b, sizeof b, "%s  sec%d  %s", node_label(world_, nb).c_str(), (int)nn.security, tag.c_str());
        dec_.options.push_back(b);
        dec_.opt_module.push_back(M_COUNT);
    }
    logline(dec_.prompt);
}
// ---- parley: a named entity blocks the way — talk, pay, threaten, or break it -
// The fixed 5-option layout (Parley/Bribe/Threaten/Appease/Fight) keeps the AI
// policy + indices stable; unaffordable choices fall through to a fight.
void Sim::open_parley() {
    const Node& here = world_.nodes[run_.pos];
    NamedIce& m = world_.named[here.guard_named];
    parley_named_ = here.guard_named;
    parley_node_  = run_.pos;
    parley_bribe_   = m.tier * 8 + here.security * 2;     // pay to bypass
    parley_tribute_ = m.tier * 3 + 4;                     // smaller, to de-escalate
    pending_ = true;
    dec_ = Decision{};
    dec_.kind = DK_PARLEY; dec_.node = run_.pos; dec_.named = here.guard_named; dec_.ice = m.archetype;
    dec_.persona = m.persona; dec_.disposition = m.disposition;
    // a persona-flavored opening line, deterministic by who's behind the ICE
    const char* line;
    switch (m.persona) {
        case PR_SYSOP:     line = "locks the node. 'Unauthorized. You will be traced.'"; break;
        case PR_DAEMON:    line = "bares its teeth. 'Toll's due, runner. Pay or bleed.'"; break;
        case PR_CONSTRUCT: line = "flickers, wearing a dead voice. 'Do I know you? Should I?'"; break;
        default:           line = "unfolds across the gate. 'Another ghost in my corridors. State your intent.'"; break;
    }
    dec_.prompt = std::string(named_name(m.name_id)) + " [" + ice_name(m.archetype) + "], a " +
                  persona_name(m.persona) + " (tier " + std::to_string((int)m.tier) + "), " + line;
    char bribe[56], appe[56], threat[56], fight[56];
    std::snprintf(bribe, sizeof bribe, "Bribe -%d sh - buy a clean pass", parley_bribe_);
    std::snprintf(appe,  sizeof appe,  "Appease -%d sh - cool grudge (may fail)", parley_tribute_);
    std::snprintf(threat,sizeof threat,"Threaten +10 heat - extort, or enrage it");
    std::snprintf(fight, sizeof fight, "Fight - break it (counter: %s)", module_name(ice_weakness(m.archetype)));
    dec_.options   = { "Parley - talk past free (may anger it)", bribe, threat, appe, fight };
    dec_.opt_module = { M_COUNT, M_COUNT, M_COUNT, M_COUNT, M_COUNT };
    logline(dec_.prompt);
}
void Sim::resolve_parley(int option) {
    Node& node = world_.nodes[parley_node_];
    NamedIce& m = world_.named[parley_named_];
    node.flags |= NF_PARLEYED;
    uint8_t f = m.faction;
    int fg = (f < F_COUNT) ? world_.factions[f].grudge : 0;
    const char* nm = named_name(m.name_id);

    auto faction_grudge = [&](int d) {
        if (f < F_COUNT) world_.factions[f].grudge = (int8_t)std::max(-9, std::min(9, world_.factions[f].grudge + d));
    };
    auto bypass = [&](bool loot) {                       // peaceful pass; advance() moves on next tick
        node.flags |= NF_GUARD_DONE;
        if (loot) grant_loot(node);
    };
    // Fall through to combat WITHOUT starting it here: NF_PARLEYED is already set,
    // so the next advance() reaches begin_fight on the normal auto-combat path
    // (which animates round by round). Starting the fight inline would surface an
    // encounter card and break the hands-off combat rule.
    auto to_fight = [&]() { /* NF_PARLEYED set above; advance() takes the fight */ };

    switch (option) {
        case 0: {  // PARLEY — talk it down (free, risky)
            int chance = m.disposition + ((m.persona == PR_AI || m.persona == PR_CONSTRUCT) ? 15 : 0)
                         - m.tier * 4 - fg * 5;
            chance = std::max(5, std::min(95, chance));
            if (rng_.chance(chance)) {
                m.grudge = (int8_t)std::max(-9, m.grudge - 1); faction_grudge(-1);
                push_event(T_PARLEY, parley_node_, m.archetype, parley_named_, 0);
                logline(std::string("[PARLEY] ") + nm + " stands down — the corridor opens.");
                if (m.disposition >= 65 && (m.persona == PR_AI || m.persona == PR_CONSTRUCT)) {
                    m.status = NS_ALLIED;
                    push_event(T_ALLY, parley_node_, m.archetype, parley_named_, 0);
                    logline(std::string(nm) + " marks you a friend — it'll remember next time.");
                }
                bypass(true);
            } else {
                add_heat(2);
                logline(std::string("[PARLEY] ") + nm + " won't hear it — talk's over.");
                to_fight();
            }
            break;
        }
        case 1: {  // BRIBE — pay to bypass (sysops refuse)
            if (m.persona == PR_SYSOP) {
                add_heat(4); faction_grudge(1); m.grudge = (int8_t)std::min(9, m.grudge + 1);
                logline(std::string("[BRIBE] ") + nm + " doesn't take bribes — and flags the attempt.");
                to_fight();
            } else if (run_.shards < parley_bribe_) {
                logline(std::string("[BRIBE] not enough shards to interest ") + nm + ".");
                to_fight();
            } else {
                run_.shards = (uint16_t)(run_.shards - parley_bribe_);
                faction_grudge(-1);
                push_event(T_BRIBE, parley_node_, m.archetype, parley_named_, 0);
                logline(std::string("[BRIBE] paid ") + std::to_string(parley_bribe_) + " shards — " + nm + " waves you through.");
                bypass(false);
            }
            break;
        }
        case 2: {  // THREATEN — costs heat, intimidate the weak, anger the rest
            add_heat(10);
            int chance = 55 + (run_.tier - m.tier) * 8 - (m.persona == PR_SYSOP ? 25 : 0) - fg * 3;
            chance = std::max(5, std::min(90, chance));
            if (rng_.chance(chance)) {
                int take = m.tier * 4 + 3;
                run_.shards = (uint16_t)(run_.shards + take);
                m.grudge = (int8_t)std::min(9, m.grudge + 2); faction_grudge(1);
                push_event(T_EXTORT, parley_node_, m.archetype, parley_named_, 0);
                logline(std::string("[THREATEN] ") + nm + " folds — extorted " + std::to_string(take) + " shards, free passage.");
                bypass(false);
            } else {
                m.grudge = (int8_t)std::min(9, m.grudge + 2); faction_grudge(1);
                logline(std::string("[THREATEN] ") + nm + " calls your bluff.");
                to_fight();
            }
            break;
        }
        case 3: {  // APPEASE — a small tribute to de-escalate. Spurned by those who
                   // want hard coin (daemons) or simply don't like you (low
                   // disposition / high grudge): a gesture, not a transaction.
            if (run_.shards < parley_tribute_) {
                logline(std::string("[APPEASE] nothing to offer ") + nm + ".");
                to_fight();
                break;
            }
            int chance = m.disposition - m.grudge * 6;
            chance += (m.persona == PR_AI) ? 10 : (m.persona == PR_CONSTRUCT) ? 5
                    : (m.persona == PR_DAEMON) ? -40 : -25;   // daemons want coin; sysops don't deal
            chance = std::max(5, std::min(90, chance));
            if (!rng_.chance(chance)) {                        // tribute spurned — you keep your shards
                m.grudge = (int8_t)std::min(9, m.grudge + 1);
                logline(std::string("[APPEASE] ") + nm +
                        (m.persona == PR_DAEMON ? " spurns the tribute — it wanted hard coin, not trinkets."
                                                : " spurns the tribute — the gesture means nothing to it."));
                to_fight();
                break;
            }
            run_.shards = (uint16_t)(run_.shards - parley_tribute_);
            add_heat(-4);
            m.grudge = (int8_t)std::max(-9, m.grudge - 3); faction_grudge(-2);
            m.disposition = (uint8_t)std::min(100, (int)m.disposition + 15);
            logline(std::string("[APPEASE] tribute paid — ") + nm + " softens; faction grudge eased.");
            if (m.grudge <= -3) {
                m.status = NS_ALLIED;
                push_event(T_ALLY, parley_node_, m.archetype, parley_named_, 0);
                logline(std::string(nm) + " counts you a friend now.");
            } else {
                push_event(T_PARLEY, parley_node_, m.archetype, parley_named_, 0);
            }
            bypass(false);
            break;
        }
        default:   // FIGHT
            to_fight();
            break;
    }
}
// ---- memory dive: a dead mind looped in the ICE (a special NPC encounter) -----
// Same "talk to something" loop as parley, different verbs: dive it for lore + a
// small permanent buff, extract it for shards at the cost of corruption, stabilize
// it to clean your own corruption, or release it — a gamble on a future ally/enemy.
namespace {
const char* MEM_FRAGMENT[] = {
    "a childhood that wasn't yours: rain on a window, a language you almost speak.",
    "the last login of someone who never logged out — a name, a debt, a way in.",
    "a decker's muscle-memory, the shape of a hundred breaks you've never run.",
    "a corporate black-budget schematic, half-corrupted, still warm.",
    "a face you'll see again, and the exact weight of how it betrayed you.",
    "the cadence of a dead operator's keystrokes, and you find your hands faster.",
};
}
void Sim::open_memory() {
    const Node& here = world_.nodes[run_.pos];
    NamedIce& m = world_.named[here.guard_named];
    parley_named_ = here.guard_named;
    parley_node_  = run_.pos;
    parley_bribe_ = m.tier * 6 + 8;     // shards an extraction yields
    pending_ = true;
    dec_ = Decision{};
    dec_.kind = DK_MEMORY; dec_.node = run_.pos; dec_.named = here.guard_named; dec_.ice = m.archetype;
    dec_.persona = m.persona; dec_.disposition = m.disposition;
    dec_.prompt = std::string(named_name(m.name_id)) + ", a memory construct looped in the ICE at " +
                  node_label(world_, run_.pos) + " — a dead mind, still dreaming. What do you do with it?";
    char ex[56];
    std::snprintf(ex, sizeof ex, "Extract +%d sh, corr +10 - rip it", parley_bribe_);
    dec_.options    = { "Dive - lore + a permanent edge, corr +2", ex, "Stabilize - corr -14, eases grudge", "Release - gamble: future ally or foe" };
    dec_.opt_module = { M_COUNT, M_COUNT, M_COUNT, M_COUNT };
    logline(dec_.prompt);
}
void Sim::resolve_memory(int option) {
    Node& node = world_.nodes[parley_node_];
    NamedIce& m = world_.named[parley_named_];
    node.flags |= NF_PARLEYED;
    node.flags |= NF_GUARD_DONE;        // interacted with — the way is clear either way
    const char* nm = named_name(m.name_id);
    uint8_t f = m.faction;
    auto faction_grudge = [&](int d) {
        if (f < F_COUNT) world_.factions[f].grudge = (int8_t)std::max(-9, std::min(9, world_.factions[f].grudge + d));
    };

    switch (option) {
        case 0: {  // DIVE — lore + a small permanent edge (learn from the dead mind)
            const char* frag = MEM_FRAGMENT[rng_.range((uint32_t)(sizeof(MEM_FRAGMENT)/sizeof(MEM_FRAGMENT[0])))];
            logline(std::string("[DIVE] you fall into ") + nm + "'s memory: " + frag);
            // teach the lowest-level module (else widen the buffer if all maxed)
            int lo = 0; for (int i = 1; i < M_COUNT; ++i) if (run_.mod_level[i] < run_.mod_level[lo]) lo = i;
            if (run_.mod_level[lo] < 9) {
                run_.mod_level[lo] = (uint8_t)(run_.mod_level[lo] + 1);
                logline(std::string("  the dead operator's reflexes are yours now — ") + module_name(lo) +
                        " -> L" + std::to_string((int)run_.mod_level[lo]) + ".");
            } else {
                run_.buffer_max = (int16_t)std::min(120, run_.buffer_max + 4);
                logline("  the memory widens your buffer.");
            }
            add_corruption(2);          // diving a dead mind leaves a little residue
            push_event(T_MEMORY, parley_node_, m.archetype, parley_named_, 0);
            break;
        }
        case 1: {  // EXTRACT — rip it for data: shards now, corruption later
            run_.shards = (uint16_t)(run_.shards + parley_bribe_);
            add_corruption(10);
            m.grudge = (int8_t)std::min(9, m.grudge + 2); faction_grudge(1);
            logline(std::string("[EXTRACT] you strip ") + nm + " for parts — +" + std::to_string(parley_bribe_) +
                    " shards, corruption +10. something in you curdles.");
            push_event(T_BIG_SCORE, parley_node_, m.archetype, parley_named_, (int8_t)run_.heat);
            break;
        }
        case 2: {  // STABILIZE — help the construct hold together; it steadies your deck
            int before = run_.corruption;
            add_corruption(-14);
            m.disposition = (uint8_t)std::min(100, (int)m.disposition + 10);
            m.grudge = (int8_t)std::max(-9, m.grudge - 1); faction_grudge(-1);
            logline(std::string("[STABILIZE] you shore up ") + nm + "'s loops — corruption " +
                    std::to_string(before) + " -> " + std::to_string((int)run_.corruption) + ".");
            push_event(T_MEMORY, parley_node_, m.archetype, parley_named_, 0);
            break;
        }
        default: {  // RELEASE — cut it loose: a future ally, or a future enemy
            int chance = m.disposition - m.grudge * 6;
            chance = std::max(10, std::min(90, chance));
            if (rng_.chance(chance)) {
                m.status = NS_ALLIED; faction_grudge(-1);
                push_event(T_ALLY, parley_node_, m.archetype, parley_named_, 0);
                logline(std::string("[RELEASE] you free ") + nm + " into the grid — it remembers kindness. an ally, somewhere ahead.");
            } else {
                m.grudge = (int8_t)std::min(9, m.grudge + 3); faction_grudge(2);
                push_event(T_REVENGE, parley_node_, m.archetype, parley_named_, 0);
                logline(std::string("[RELEASE] you free ") + nm + " — and it comes back wrong. you've made an enemy.");
            }
            break;
        }
    }
}
void Sim::open_survival() {
    pending_ = true;
    dec_ = Decision{};
    dec_.kind = DK_SURVIVAL; dec_.node = run_.pos;
    int pct = run_.integrity_max ? run_.integrity * 100 / run_.integrity_max : 0;
    char b[80]; std::snprintf(b, sizeof b, "Integrity %d%%, heat %d, corr %d%%. The deck is screaming.",
                              pct, (int)run_.heat, (int)run_.corruption);
    dec_.prompt = b;
    // Every option here is a real trade — spell out the cost so the call is yours.
    int heal_amt = run_.buffer >= 5 ? 12 + run_.mod_level[M_PATCH] * 3 : 5;
    char pa[56], gh[56], pu[56], jo[56];
    std::snprintf(pa, sizeof pa, "Patch +%d intg, -5 buffer", heal_amt);
    std::snprintf(gh, sizeof gh, "Ghost - heat -20, but corr +4");
    std::snprintf(pu, sizeof pu, "Push on as-is - spend nothing, stay exposed");
    std::snprintf(jo, sizeof jo, "Jack out - bank $%d, end the run", (int)run_.shards);
    dec_.options = { pa, gh, pu, jo };
    dec_.opt_module = { M_PATCH, M_GHOST, M_COUNT, M_COUNT };
    logline(dec_.prompt);
}
void Sim::open_extract() {
    pending_ = true;
    dec_ = Decision{};
    dec_.kind = DK_EXTRACT; dec_.node = run_.pos;
    dec_.prompt = std::string("The core of ") + node_label(world_, run_.pos) + " is open. " +
                  faction_short(world_.nodes[run_.pos].faction) + "'s payload is right there.";
    // spell out the trade so the choice is legible: haul vs heat/corruption, and it
    // also levels the module you use. (Spike=smash&grab before jacking out; Mask=
    // stay quiet to keep diving; Fork=balanced.)
    int rw = world_.objective.reward;
    char so[64], mo[64], fo[64];
    std::snprintf(so, sizeof so, "Spike the core - %d haul, heat+25 corr+8 (LOUD)", rw * 3 / 2);
    std::snprintf(mo, sizeof mo, "Mask a quiet pull - %d haul, heat+4 (clean)", rw * 3 / 4);
    std::snprintf(fo, sizeof fo, "Fork the warden - %d haul, heat+12 corr+3", rw);
    dec_.options = { so, mo, fo };
    dec_.opt_module = { M_SPIKE, M_MASK, M_FORK };
    logline(dec_.prompt);
}

// ---- decisions: resolve ----------------------------------------------------
void Sim::choose(int option_index) {
    if (!pending_) return;
    if (option_index < 0) option_index = 0;
    if (option_index >= (int)dec_.options.size()) option_index = (int)dec_.options.size() - 1;
    uint8_t kind = dec_.kind;
    pending_ = false;
    switch (kind) {
        case DK_ENCOUNTER: resolve_round(option_index); break;
        case DK_BRANCH:    resolve_branch(option_index); break;
        case DK_PARLEY:    resolve_parley(option_index); break;
        case DK_MEMORY:    resolve_memory(option_index); break;
        case DK_FLATLINE:  resolve_flatline(option_index); break;
        case DK_SURVIVAL:  resolve_survival(option_index); break;
        case DK_EXTRACT:   resolve_extract(option_index); break;
        case DK_DIVE:      resolve_dive(option_index); break;
        case DK_FACTION:   break;
    }
}

void Sim::resolve_branch(int option) {
    if (option < (int)branch_opts_.size()) {
        committed_branch_ = branch_opts_[option];
        logline(std::string("routing via ") + node_label(world_, committed_branch_) + ".");
    }
    accrue_and_move();
    check_thresholds();
}

void Sim::resolve_survival(int option) {
    uint8_t mod = dec_.opt_module[option];
    if (mod == M_PATCH) {
        // a stopgap, not a reset — and useless if the buffer's already spent
        int amt = run_.buffer >= 5 ? 12 + run_.mod_level[M_PATCH] * 3 : 5;
        heal(amt); add_corruption(-4);
        if (run_.buffer >= 5) run_.buffer = (int16_t)(run_.buffer - 5); else add_corruption(4);
        logline("patched the deck — integrity " + std::to_string(run_.integrity) + ".");
    } else if (mod == M_GHOST) {
        add_heat(-20); add_corruption(4);
        logline("ghosted — heat -20, corruption +4.");
    } else if (option == (int)dec_.options.size() - 1) {
        jack_out();                                  // pull out NOW: bank the haul, end the run
    } else {
        logline("held the line — spent nothing, still exposed.");   // push on as-is: gamble the resources
    }
    survival_warned_ = true;
}

void Sim::resolve_extract(int option) {
    uint8_t mod = dec_.opt_module[option];
    Node& node = world_.nodes[run_.pos];
    run_.objective_done = true;
    run_.objectives_done++;
    run_.has_ghostkey = true;
    int haul = world_.objective.reward;
    if (mod == M_SPIKE) { haul = haul * 3 / 2; add_heat(25); add_corruption(8); }   // loud + dirty
    else if (mod == M_MASK) { haul = haul * 3 / 4; add_heat(4); }                    // clean quiet pull
    else if (mod == M_FORK) { haul = haul; add_heat(12); add_corruption(3); }
    run_.shards = (uint16_t)(run_.shards + haul);
    grant_loot(node);
    int old_power = player_power(run_);
    run_.tier = (uint8_t)std::min(99, run_.tier + 1);   // no cap on the dive — deck keeps climbing
    run_.mod_level[mod] = (uint8_t)std::min(9, run_.mod_level[mod] + 1);
    apply_tier_growth();
    if (node.faction < F_COUNT) world_.factions[node.faction].grudge += 3;
    push_event(T_BIG_SCORE, run_.pos, I_COUNT, NONE8, (int8_t)run_.heat);
    push_event(T_REVENGE, run_.pos, I_COUNT, NONE8, 0);
    logline(std::string("burned the core of ") + node_label(world_, run_.pos) + " — " + std::to_string(haul) +
            " shards" + (mod == M_SPIKE ? " (LOUD: heat +25, corr +8)" : mod == M_FORK ? " (heat +12, corr +3)" : " (quiet: heat +4)") + ".");
    logline(std::string("** DECK UPGRADE ** tier T") + std::to_string((int)run_.tier) + ", " + module_name(mod) +
            " -> L" + std::to_string((int)run_.mod_level[mod]) + ", buffer max " + std::to_string((int)run_.buffer_max) +
            ". power " + std::to_string(old_power) + " -> " + std::to_string(player_power(run_)) + ".");
    open_dive();   // the choice: jack out with the haul, or dive deeper for more
}

// After an objective: bank and leave, or push into a harder layer.
void Sim::open_dive() {
    pending_ = true;
    dec_ = Decision{};
    dec_.kind = DK_DIVE; dec_.node = run_.pos;
    char b[112];
    std::snprintf(b, sizeof b, "Objective down (%d cracked). $%d rides on you. Jack out, or dive deeper?",
                  (int)run_.objectives_done, (int)run_.shards);
    dec_.prompt = b;
    char j[56], d[56];
    std::snprintf(j, sizeof j, "Jack out - bank $%d, walk away safe", (int)run_.shards);
    std::snprintf(d, sizeof d, "Dive deeper (L%d) - more $, ICE gets meaner", (int)run_.depth + 2);
    dec_.options = { j, d };
    dec_.opt_module = { M_COUNT, M_COUNT };
    logline(dec_.prompt);
}
void Sim::resolve_dive(int option) {
    if (option == 0) jack_out();
    else dive_deeper();
}
void Sim::dive_deeper() {
    pending_ = false;
    // carry faction grudges into the deeper layer so the net stays hostile
    Legends prior{};
    for (int f = 0; f < F_COUNT; ++f) prior.grudge[f] = world_.factions[f].grudge;
    uint32_t base = world_.citynet_seed, rs = world_.run_seed;
    run_.depth++;
    uint32_t newcnet = base ^ (uint32_t)(run_.depth * 0x9E3779B9u);
    uint32_t newseed = (rs * 2654435761u) ^ (uint32_t)(run_.depth * 40503u) ^ 0xD1Eu;
    world_ = World{};
    world_.citynet_seed = newcnet; world_.run_seed = newseed;
    rng_.seed(((uint64_t)newcnet << 21) ^ newseed ^ 0xC0FFEEuLL);
    gen_world(&prior);                                  // escalates via run_.depth
    // reset the layer position/state; the DECK (tier, modules, shards, int, corr) carries
    run_.pos = world_.entry; run_.hunter_pos = world_.entry;
    run_.objective_done = false; run_.node_clears = 0; run_.in_fight = false;
    run_.hunt_active = false; run_.hunter_named = NONE8; run_.exfil = false;
    // punching through buys a breather: you enter the new layer near-fit and
    // recharged — but Corruption never washes out, and each push adds more. Deep
    // dives end when the deck is too corrupted to fire straight, not at a wall.
    int floor80 = run_.integrity_max * 4 / 5;
    if (run_.integrity < floor80) run_.integrity = (int16_t)floor80;
    run_.buffer = run_.buffer_max;
    if (run_.heat > 12) add_heat(-12);                  // shook some heat changing systems
    add_corruption(4);                                  // ...but the deck degrades each push
    survival_warned_ = false; committed_branch_ = NONE8; branched_.clear();
    world_.nodes[run_.pos].flags |= NF_VISITED;
    push_event(T_JACKIN, run_.pos, I_COUNT, NONE8, 0);
    logline(std::string("punched through — layer ") + std::to_string((int)run_.depth + 1) + ", deeper and meaner.");
}

void Sim::jack_out() {
    if (run_.outcome != O_RUNNING || run_.in_fight) return;   // can't pull out mid-siege
    pending_ = false;
    run_.outcome = O_EXTRACTED;
    push_event(T_EXTRACT, run_.pos, I_COUNT, NONE8, 0);
    logline(std::string("jacked out — banked ") + std::to_string((int)run_.shards) + " shards.");
}

// ---- scoring: a haul only counts if you JACK OUT; death loses the unbanked data
int Sim::score() const {
    int s = run_.tier * 100 + run_.named_killed * 200 + run_.objectives_done * 300 - run_.corruption * 3;
    if (run_.outcome == O_EXTRACTED) s += run_.shards;     // banked
    return s < 0 ? 0 : s;
}

// ---- legends ---------------------------------------------------------------
void Sim::update_legends(Legends& L) const {
    L.citynet_seed = world_.citynet_seed;
    L.run_count++;
    if ((uint32_t)score() > L.best_score) L.best_score = (uint32_t)score();
    for (int f = 0; f < F_COUNT; ++f) {
        int g = world_.factions[f].grudge;
        if (g > 9) g = 9;
        if (g < -9) g = -9;
        L.grudge[f] = (int8_t)g;
    }
    // named status
    for (int i = 0; i < world_.named_count; ++i) {
        if (world_.named[i].status == NS_ALIVE) continue;
        int slot = -1;
        for (int j = 0; j < L.named_count; ++j) if (L.named[j].name_id == world_.named[i].name_id) { slot = j; break; }
        if (slot < 0 && L.named_count < MAX_NAMED) slot = L.named_count++;
        if (slot >= 0) {
            L.named[slot].name_id = world_.named[i].name_id;
            L.named[slot].archetype = world_.named[i].archetype;
            L.named[slot].status = world_.named[i].status;
            L.named[slot].grudge = (int8_t)std::min(9, world_.named[i].grudge + 1);
        }
    }
    // burned/locked routes persist
    for (int e = 0; e < world_.edge_count && L.burned_count < MAX_BURNED; ++e)
        if (world_.edges[e].flags & (EF_BURNED | EF_LOCKED)) {
            bool have = false;
            for (int b = 0; b < L.burned_count; ++b)
                if ((L.burned[b].a == world_.edges[e].a && L.burned[b].b == world_.edges[e].b)) have = true;
            if (!have) { L.burned[L.burned_count].a = world_.edges[e].a; L.burned[L.burned_count].b = world_.edges[e].b; L.burned_count++; }
        }
    // mark nodes we looted hard
    for (int i = 0; i < world_.node_count && L.marked_count < MAX_MARKED; ++i)
        if ((world_.nodes[i].flags & NF_LOOTED) && world_.nodes[i].security >= 5) {
            bool have = false; for (int m = 0; m < L.marked_count; ++m) if (L.marked[m] == i) have = true;
            if (!have) L.marked[L.marked_count++] = (uint8_t)i;
        }
}

bool legends_serialize(const Legends& L, std::string& out) {
    char b[64];
    out = "CHV1\n";
    std::snprintf(b, sizeof b, "citynet,%u\n", (unsigned)L.citynet_seed); out += b;
    std::snprintf(b, sizeof b, "runs,%u\n", (unsigned)L.run_count); out += b;
    std::snprintf(b, sizeof b, "best,%u\n", (unsigned)L.best_score); out += b;
    out += "grudge";
    for (int f = 0; f < F_COUNT; ++f) { std::snprintf(b, sizeof b, ",%d", L.grudge[f]); out += b; }
    out += "\n";
    std::snprintf(b, sizeof b, "named,%u\n", L.named_count); out += b;
    for (int i = 0; i < L.named_count; ++i) {
        std::snprintf(b, sizeof b, "%u,%u,%u,%d\n", L.named[i].name_id, L.named[i].archetype, L.named[i].status, L.named[i].grudge);
        out += b;
    }
    std::snprintf(b, sizeof b, "burned,%u\n", L.burned_count); out += b;
    for (int i = 0; i < L.burned_count; ++i) { std::snprintf(b, sizeof b, "%u,%u\n", L.burned[i].a, L.burned[i].b); out += b; }
    std::snprintf(b, sizeof b, "marked,%u\n", L.marked_count); out += b;
    for (int i = 0; i < L.marked_count; ++i) { std::snprintf(b, sizeof b, "%u\n", L.marked[i]); out += b; }
    return true;
}

static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out; size_t i = 0;
    while (i < s.size()) { size_t nl = s.find('\n', i); out.push_back(s.substr(i, nl == std::string::npos ? std::string::npos : nl - i)); if (nl == std::string::npos) break; i = nl + 1; }
    return out;
}
static std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> out; size_t i = 0;
    while (i <= s.size()) { size_t cm = s.find(',', i); std::string t = s.substr(i, cm == std::string::npos ? std::string::npos : cm - i); out.push_back(t); if (cm == std::string::npos) break; i = cm + 1; }
    return out;
}

bool legends_deserialize(const std::string& in, Legends& L) {
    L = Legends{};
    auto lines = split_lines(in);
    if (lines.empty() || lines[0].rfind("CHV1", 0) != 0) return false;
    size_t i = 1;
    while (i < lines.size()) {
        auto f = split_commas(lines[i]);
        if (f.empty() || f[0].empty()) { ++i; continue; }
        if (f[0] == "citynet" && f.size() >= 2) L.citynet_seed = (uint32_t)std::strtoul(f[1].c_str(), nullptr, 10);
        else if (f[0] == "runs" && f.size() >= 2) L.run_count = (uint16_t)std::strtoul(f[1].c_str(), nullptr, 10);
        else if (f[0] == "best" && f.size() >= 2) L.best_score = (uint32_t)std::strtoul(f[1].c_str(), nullptr, 10);
        else if (f[0] == "grudge") { for (int g = 0; g < F_COUNT && g + 1 < (int)f.size(); ++g) L.grudge[g] = (int8_t)std::atoi(f[g + 1].c_str()); }
        else if (f[0] == "named" && f.size() >= 2) {
            int n = std::atoi(f[1].c_str());
            for (int k = 0; k < n && L.named_count < MAX_NAMED && i + 1 < lines.size(); ++k) {
                auto g = split_commas(lines[++i]);
                if (g.size() >= 4) { auto& nm = L.named[L.named_count++]; nm.name_id = (uint8_t)std::atoi(g[0].c_str()); nm.archetype = (uint8_t)std::atoi(g[1].c_str()); nm.status = (uint8_t)std::atoi(g[2].c_str()); nm.grudge = (int8_t)std::atoi(g[3].c_str()); }
            }
        } else if (f[0] == "burned" && f.size() >= 2) {
            int n = std::atoi(f[1].c_str());
            for (int k = 0; k < n && L.burned_count < MAX_BURNED && i + 1 < lines.size(); ++k) {
                auto g = split_commas(lines[++i]);
                if (g.size() >= 2) { L.burned[L.burned_count].a = (uint8_t)std::atoi(g[0].c_str()); L.burned[L.burned_count].b = (uint8_t)std::atoi(g[1].c_str()); L.burned_count++; }
            }
        } else if (f[0] == "marked" && f.size() >= 2) {
            int n = std::atoi(f[1].c_str());
            for (int k = 0; k < n && L.marked_count < MAX_MARKED && i + 1 < lines.size(); ++k) {
                auto g = split_commas(lines[++i]);
                if (!g.empty() && !g[0].empty()) L.marked[L.marked_count++] = (uint8_t)std::atoi(g[0].c_str());
            }
        }
        ++i;
    }
    return true;
}

// ---- chronicle (Gibson-voice generator, no LLM) ----------------------------
// Template-grammar + weighted lexicons + a synesthetic-simile slot, seeded from
// the run seed so a given seed always tells the same story. ORIGINAL pastiche —
// never embeds actual Neuromancer text; the "dead-channel sky" is a riff, homage,
// not a quote (docs/CYBERHACK.md).
namespace {
template <class A> const char* pick(Rng& r, const A& arr) { return arr[r.range((uint32_t)(sizeof(arr) / sizeof(arr[0])))]; }

const char* OPENERS[] = {
    "You came up into the grid under a sky the color of a screen with no signal.",
    "Dawn over the sprawl, gray as old data.",
    "The net opened cold and blue, like neon under rain.",
    "You jacked in with the city humming somewhere behind your eyes.",
    "Midnight on the grid, and the ICE was already awake.",
    "The matrix unfolded around you, all dead light and distance.",
};
const char* SIMILES[] = {
    "like neon bleeding into wet concrete",
    "like static finding the shape of a face",
    "like cheap chrome under a thumbnail",
    "like a tooth coming loose",
    "like dermal grafts going septic",
    "the color of a dead channel",
    "like surf against a breakwater of glass",
    "soft as snow on a dead monitor",
};
const char* B_FIRST_BLOOD[] = {
    "{enemy} found you first; you burned it down anyway, {simile}.",
    "You peeled {enemy} off the gate, {simile}.",
};
const char* B_HUMILIATION[] = {
    "You crippled {enemy} and left it twitching, {simile}.",
    "{enemy} you broke open and walked straight through, {simile}.",
};
const char* B_BIG_SCORE[] = {
    "{node} cracked open easy, {faction}'s payload spilling out {simile}.",
    "You took {node} for everything it had, {simile}.",
};
const char* B_CLOSE_CALL[] = {
    "Somewhere in there the deck nearly flatlined, {simile}.",
    "Integrity bottomed out and held, barely, {simile}.",
};
const char* B_HUNT[] = {
    "Then {faction} remembered, and sent something up through the dead grids after you.",
    "{faction} put a hunter on the wire behind you.",
};
const char* B_REVENGE[] = {
    "You burned {faction}'s core, and the whole net felt the weight of it.",
};
const char* B_LOCKDOWN[] = {
    "The grid went into lockdown, routes folding shut {simile}.",
};
const char* B_SACRIFICE[] = {
    "You left the prize on the table and ran for the door.",
};
const char* B_PARLEY[] = {
    "You traded words with {enemy} in {node} until the gate sighed open, {simile}.",
    "{enemy} listened, which was more than the ICE usually did, and let you slide past.",
};
const char* B_ALLY[] = {
    "{enemy} owed you now — a voice that would answer next time you came up cold into the dark.",
    "You'd turned {enemy}, bought yourself a ghost in somebody else's machine, {simile}.",
};
const char* B_EXTORT[] = {
    "You leaned on {enemy} until it bled credit, {simile}.",
    "{enemy} bought its own life back from you in {node}, and hated you for it.",
};
const char* B_BRIBE[] = {
    "You paid {enemy} off in {node}; down here even the watchdogs took coin.",
};
const char* B_MEMORY[] = {
    "You went down into {enemy}'s memory and came back carrying something that wasn't yours, {simile}.",
    "A dead mind called {enemy} was still dreaming in {node}; you walked through its dreams and out the other side.",
};
const char* B_FLATLINE[] = {
    "You flatlined under {enemy} in {node} and clawed back out of the white, {simile}.",
    "The deck died for a second against {enemy}; you came back leaving a piece of it behind.",
};

// pick a simile NOT already used in this chronicle, so the same image never lands
// twice in one summary (resets only if every simile has been spent).
const char* next_simile(Rng& cr, std::vector<uint8_t>& used) {
    const int N = (int)(sizeof(SIMILES) / sizeof(SIMILES[0]));
    if ((int)used.size() >= N) used.clear();
    int idx = (int)cr.range(N), guard = 0;
    while (std::find(used.begin(), used.end(), (uint8_t)idx) != used.end() && ++guard < 2 * N)
        idx = (int)cr.range(N);
    used.push_back((uint8_t)idx);
    return SIMILES[idx];
}

std::string subst(const std::string& tmpl, Rng& cr, std::vector<uint8_t>& used_sim,
                  const std::string& node, const std::string& faction, const std::string& enemy) {
    std::string s = tmpl;
    auto rep = [&](const char* key, const std::string& val) {
        size_t p; while ((p = s.find(key)) != std::string::npos) s.replace(p, std::strlen(key), val);
    };
    rep("{node}", node); rep("{faction}", faction); rep("{enemy}", enemy);
    size_t p; while ((p = s.find("{simile}")) != std::string::npos) s.replace(p, 8, next_simile(cr, used_sim));
    return s;
}
} // namespace

std::string Sim::chronicle() const {
    Rng cr; cr.seed(((uint64_t)world_.run_seed << 7) ^ 0xC4807AB1EuLL);
    std::vector<uint8_t> used_sim;   // similes already spent this chronicle (no repeats)
    std::string out = pick(cr, OPENERS);
    out += "\n";

    // reconstruct events oldest→newest
    std::vector<Event> evs;
    int start = (ev_head_ - ev_count_ + EVENT_RING) % EVENT_RING;
    for (int i = 0; i < ev_count_; ++i) evs.push_back(events_[(start + i) % EVENT_RING]);

    auto ev_node    = [&](const Event& e) { return node_label(world_, e.node); };
    auto ev_faction = [&](const Event& e) { return std::string(faction_short(e.faction)); };
    auto ev_enemy   = [&](const Event& e) { return e.named != NONE8 ? std::string(named_name(world_.named[e.named].name_id)) : std::string("the ") + ice_name(e.ice); };

    std::string last_enemy = "the ICE", last_node = node_label(world_, run_.pos);
    int beats = 0;
    bool used[T_TAGCOUNT] = {false};
    for (const Event& e : evs) {
        if (beats >= 3) break;
        if (e.named != NONE8) last_enemy = std::string(named_name(world_.named[e.named].name_id));
        if (e.node < world_.node_count) last_node = node_label(world_, e.node);
        if (e.tag >= T_TAGCOUNT || used[e.tag]) continue;
        const char* const* pool = nullptr; size_t pooln = 0;
        switch (e.tag) {
            case T_FIRST_BLOOD: pool = B_FIRST_BLOOD; pooln = sizeof(B_FIRST_BLOOD)/sizeof(char*); break;
            case T_HUMILIATION: pool = B_HUMILIATION; pooln = sizeof(B_HUMILIATION)/sizeof(char*); break;
            case T_BIG_SCORE:   pool = B_BIG_SCORE;   pooln = sizeof(B_BIG_SCORE)/sizeof(char*); break;
            case T_CLOSE_CALL:  pool = B_CLOSE_CALL;  pooln = sizeof(B_CLOSE_CALL)/sizeof(char*); break;
            case T_HUNT:        pool = B_HUNT;        pooln = sizeof(B_HUNT)/sizeof(char*); break;
            case T_REVENGE:     pool = B_REVENGE;     pooln = sizeof(B_REVENGE)/sizeof(char*); break;
            case T_LOCKDOWN:    pool = B_LOCKDOWN;    pooln = sizeof(B_LOCKDOWN)/sizeof(char*); break;
            case T_SACRIFICE:   pool = B_SACRIFICE;   pooln = sizeof(B_SACRIFICE)/sizeof(char*); break;
            case T_PARLEY:      pool = B_PARLEY;      pooln = sizeof(B_PARLEY)/sizeof(char*); break;
            case T_ALLY:        pool = B_ALLY;        pooln = sizeof(B_ALLY)/sizeof(char*); break;
            case T_EXTORT:      pool = B_EXTORT;      pooln = sizeof(B_EXTORT)/sizeof(char*); break;
            case T_BRIBE:       pool = B_BRIBE;       pooln = sizeof(B_BRIBE)/sizeof(char*); break;
            case T_MEMORY:      pool = B_MEMORY;      pooln = sizeof(B_MEMORY)/sizeof(char*); break;
            case T_FLATLINE:    pool = B_FLATLINE;    pooln = sizeof(B_FLATLINE)/sizeof(char*); break;
            default: continue;
        }
        used[e.tag] = true;
        std::string t = pool[cr.range((uint32_t)pooln)];
        out += subst(t, cr, used_sim, ev_node(e), ev_faction(e), ev_enemy(e));
        out += " ";
        beats++;
    }
    if (beats == 0) out += "Nothing on the grid moved. A quiet dive, the kind nobody remembers. ";
    out += "\n";

    // closer
    char tail[160];
    if (run_.outcome == O_EXTRACTED) {
        if (run_.heat < g_tune.heat_t1)
            std::snprintf(tail, sizeof tail, "You jacked out %d shards richer with the trace still asleep behind you, and the city never felt your weight.", (int)run_.shards);
        else
            std::snprintf(tail, sizeof tail, "You came out the other side with %d shards and a bounty already on the wire.", (int)run_.shards);
    } else {
        switch (run_.death_cause) {
            case D_CORRUPTION:
                std::snprintf(tail, sizeof tail, "Corruption took the rest. You flatlined at %d%% somewhere in the meat, decklights dying one by one.", (int)run_.corruption); break;
            case D_HUNTED:
                std::snprintf(tail, sizeof tail, "%s ran you down in %s. They'll be telling that one for a while.", last_enemy.c_str(), last_node.c_str()); break;
            case D_ICE:
                std::snprintf(tail, sizeof tail, "The ICE caught you in %s. No fanfare. Just the long fall into white noise.", last_node.c_str()); break;
            default:
                std::snprintf(tail, sizeof tail, "The trace closed the last route and the grid swallowed you whole."); break;
        }
    }
    out += tail;
    out += "\n";
    char foot[96];
    std::snprintf(foot, sizeof foot, "[ %d shards | tier %d | %d ICE down | heat %d | corr %d%% | score %d ]",
                  (int)run_.shards, (int)run_.tier, (int)run_.ice_killed, (int)run_.heat, (int)run_.corruption, score());
    out += foot;
    return out;
}

// ---- AI policy + headless driver -------------------------------------------
static int find_module_option(const Decision& d, uint8_t mod) {
    for (int i = 0; i < (int)d.opt_module.size(); ++i) if (d.opt_module[i] == mod) return i;
    return -1;
}

int ai_decide(uint8_t pers, const RunState& r, const Decision& d, Rng& rng) {
    switch (d.kind) {
        case DK_ENCOUNTER: {
            // a per-round fight choice: race with Spike, spend the scarce counter,
            // Patch when hurt, or Ghost out and abandon the score.
            int weak  = find_module_option(d, ice_weakness(d.ice));
            int spike = find_module_option(d, M_SPIKE);
            int ghost = find_module_option(d, M_GHOST);
            int patch = find_module_option(d, M_PATCH);
            int lowint = r.integrity_max ? r.integrity * 100 / r.integrity_max : 0;
            bool can_counter = weak >= 0 && r.buffer >= 5;
            if (lowint < 18 && ghost >= 0) return ghost;               // about to flatline: break contact
            if (lowint < 42 && patch >= 0 && r.buffer >= 6 && r.fight_round > 0) return patch; // bind wounds mid-siege
            if (pers == P_RECKLESS) return spike >= 0 ? spike : 0;      // race it down, punish be damned
            if (pers == P_OPPORTUNIST) return can_counter ? weak : (spike >= 0 ? spike : 0);
            // cautious / loyalist: spend the right tool while the buffer lasts, else grind with Spike
            return can_counter ? weak : (spike >= 0 ? spike : 0);
        }
        case DK_BRANCH: {
            int n = (int)d.options.size();
            if (n <= 0) return 0;
            if (pers == P_RECKLESS || pers == P_OPPORTUNIST) return n - 1;  // highest risk/reward (sorted asc)
            return 0;                                                       // cautious/loyalist: safest
        }
        case DK_PARLEY: {
            // options: 0 Parley, 1 Bribe, 2 Threaten, 3 Appease, 4 Fight.
            // Negotiate the way that actually works on THIS mind: appease/parley
            // the relational ones (AI, construct), bribe the coin-hungry daemons,
            // fight the sysops who won't deal. Combat stays the fallback so
            // progression (tier from kills) still holds.
            uint8_t p = d.persona;
            bool relational = (p == PR_AI || p == PR_CONSTRUCT);
            bool talkable   = relational && d.disposition >= 45;
            bool can_pay    = r.shards >= 50;
            int lowint = r.integrity_max ? r.integrity * 100 / r.integrity_max : 0;
            if (lowint < 30) {                                  // hurt: take any door out of the fight
                if (talkable) return 0;                         // talk past it
                if (p == PR_DAEMON && can_pay) return 1;        // buy past it
                return p == PR_SYSOP ? 4 : 2;                   // sysops don't deal; else lean on it
            }
            switch (pers) {
                case P_RECKLESS:    return 4;                                   // break it
                case P_OPPORTUNIST: return p == PR_DAEMON && can_pay ? 1 : talkable ? 0 : 4;
                case P_LOYALIST:    return relational ? 3                        // appease to build allies (engine fights if broke)
                                         : p == PR_DAEMON && can_pay ? 1 : 4;
                default:            return talkable ? 0 : (p == PR_DAEMON && can_pay ? 1 : 4); // cautious
            }
        }
        case DK_FLATLINE: {
            // options: 0 jack the cord, [1 burn a module if offered], last ride it.
            int jack = 0, ride = (int)d.options.size() - 1, burn = -1;
            for (int i = 0; i < (int)d.opt_module.size(); ++i) if (d.opt_module[i] != M_COUNT) burn = i;
            switch (pers) {
                case P_RECKLESS:    return ride;                    // never stops; rides the white
                case P_OPPORTUNIST: return jack;                   // protect the bag, bail alive
                case P_CAUTIOUS:    return burn >= 0 ? burn : jack; // pay a module, keep going
                case P_LOYALIST:    return burn >= 0 ? burn : jack;
                default:            return jack;
            }
        }
        case DK_MEMORY: {
            // options: 0 Dive, 1 Extract, 2 Stabilize, 3 Release.
            // clean your corruption first if it's getting dangerous; otherwise each
            // directive treats the dead mind in character.
            if (r.corruption >= 45) return 2;                  // stabilize before it bites
            switch (pers) {
                case P_RECKLESS:    return 1;                  // rip it for shards
                case P_OPPORTUNIST: return r.corruption < 25 ? 1 : 0;
                case P_LOYALIST:    return 3;                  // free it, make a friend
                default:            return 0;                  // cautious: dive for the edge
            }
        }
        case DK_SURVIVAL: {
            int patch = find_module_option(d, M_PATCH);
            int ghost = find_module_option(d, M_GHOST);
            // reckless pushes, but isn't suicidal: shed heat if a hunter is closing
            if (pers == P_RECKLESS) return (r.hunt_active || r.heat >= 45) && ghost >= 0 ? ghost : (patch >= 0 ? patch : 0);
            if (pers == P_CAUTIOUS || pers == P_LOYALIST) return patch >= 0 ? patch : 0;
            // opportunist: patch if affordable else pull out
            return patch >= 0 ? patch : (int)d.options.size() - 1;
        }
        case DK_EXTRACT: {
            int spike = find_module_option(d, M_SPIKE);
            int mask = find_module_option(d, M_MASK);
            if (pers == P_RECKLESS || pers == P_OPPORTUNIST) return spike >= 0 ? spike : 0;
            return mask >= 0 ? mask : 0;
        }
        case DK_DIVE: {
            // option 0 = jack out (bank), 1 = dive deeper (risk it for more)
            int lowint = r.integrity_max ? r.integrity * 100 / r.integrity_max : 0;
            if (pers == P_RECKLESS)    return (lowint < 25 || r.corruption > 70) ? 0 : 1;          // push hard
            if (pers == P_OPPORTUNIST) return (lowint < 45 || r.heat >= g_tune.heat_t3 || r.depth >= 2) ? 0 : 1;
            return 0;                                                                                 // cautious/loyalist: bank the first haul, don't push luck
        }
        default: return (int)rng.range((uint32_t)std::max<size_t>(1, d.options.size()));
    }
}

void run_headless(Sim& sim, uint32_t citynet_seed, uint32_t run_seed, uint8_t personality, const Legends* prior) {
    sim.start(citynet_seed, run_seed, personality, prior);
    Rng pol; pol.seed(((uint64_t)run_seed << 13) ^ personality ^ 0xA11CEuLL);
    int guard = 0;
    while (sim.running() && guard++ < MAX_STEPS * 4) {
        AdvanceResult ar = sim.advance();
        if (ar == AR_ENDED) break;
        if (ar == AR_DECISION) sim.choose(ai_decide(personality, sim.state(), sim.decision(), pol));
    }
}

} // namespace cyber
