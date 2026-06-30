// Midnight City headless harness — the Phase 1 keystone. Determinism tests,
// worldgen invariants, and serialize/deserialize round-trip tests, all driving
// the pure substrate engine with no UI. Dependency-free (a tiny CHECK macro).
// Lives in sim/, outside the emulator's src/ GLOB, so its main() never collides
// with host/main.cpp and it never compiles into firmware.
//
//   make midnight_sim && ./midnight_sim      # run all tests
//   ./midnight_sim world 42                  # print a world summary for seed 42
#include "core/midnight_world.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

using namespace mid;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_fail; \
    std::printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

// ---- determinism ------------------------------------------------------------
static void test_rng() {
    std::printf("[rng] determinism + range bounds\n");
    Rng a, b; a.seed(12345); b.seed(12345);
    for (int i = 0; i < 1000; ++i) CHECK(a.next() == b.next(), "same seed -> same stream");
    Rng c; c.seed(999);
    for (int i = 0; i < 10000; ++i) { uint32_t r = c.range(7); CHECK(r < 7, "range(7) in [0,7)"); }
    Rng d; d.seed(1); Rng e; e.seed(2);
    int diff = 0; for (int i = 0; i < 64; ++i) if (d.next() != e.next()) ++diff;
    CHECK(diff > 50, "different seeds diverge");
}

static void test_determinism() {
    std::printf("[gen] same seed -> identical world; different seeds differ\n");
    for (uint32_t s = 1; s <= 500; ++s) {
        World a, b;
        gen_world(a, s);
        gen_world(b, s);
        std::string sa, sb;
        serialize(a, sa); serialize(b, sb);
        CHECK(sa == sb, "gen_world is deterministic for a fixed seed");
    }
    World x, y;
    gen_world(x, 1234);
    gen_world(y, 1235);
    std::string sx, sy;
    serialize(x, sx); serialize(y, sy);
    CHECK(sx != sy, "different seeds yield different worlds");
}

// ---- worldgen invariants ----------------------------------------------------
static void test_worldgen_invariants() {
    std::printf("[gen] structural invariants over 5000 seeds\n");
    for (uint32_t s = 1; s <= 5000; ++s) {
        World w; gen_world(w, s);

        CHECK(w.district_count >= 24 && w.district_count <= MAX_DISTRICTS, "district_count in range");
        CHECK(w.agent_count >= F_COUNT + 1 && w.agent_count <= MAX_AGENTS, "agent_count in range");
        CHECK(world_connected(w), "district graph is fully connected");

        // graph well-formed: degree bounds, valid + symmetric + no self-loop edges
        for (int i = 0; i < w.district_count; ++i) {
            const District& d = w.districts[i];
            CHECK(d.deg >= 1 && d.deg <= MAXDEG, "district degree in [1,MAXDEG]");
            CHECK(d.type < DT_COUNT, "district type valid");
            CHECK(d.owner == F_COUNT || d.owner < F_COUNT, "owner valid faction or neutral");
            for (int k = 0; k < d.deg; ++k) {
                uint8_t nb = d.adj[k];
                CHECK(nb != NONE8 && nb < w.district_count, "adjacency index valid");
                CHECK(nb != (uint8_t)i, "no self-loop");
                CHECK(districts_adjacent(w, nb, (uint8_t)i), "adjacency is symmetric");
            }
        }

        // protagonist invariants
        const Agent& p = w.agents[0];
        CHECK(p.status & AF_PLAYER, "agent 0 is the player");
        CHECK(p.status & AF_ALIVE, "player starts alive");
        CHECK(p.job == J_NONE, "player starts unemployed");
        CHECK(p.faction == F_COUNT, "player starts unaffiliated");
        CHECK(!(p.status & AF_HAS_DECK), "player starts with no deck");
        CHECK(p.loc < w.district_count, "player location valid");
        CHECK(w.home < w.district_count && p.loc == w.home, "player at home district");
        CHECK(p.money <= 40, "player starts with basically nothing");

        // every agent: valid location, valid job, alive flag set on the active set
        for (int i = 0; i < w.agent_count; ++i) {
            const Agent& a = w.agents[i];
            CHECK(a.loc < w.district_count, "agent location valid");
            CHECK(a.job < J_COUNT, "agent job valid");
            CHECK(a.faction == F_COUNT || a.faction < F_COUNT, "agent faction valid");
            CHECK(a.status & AF_ALIVE, "active agent is alive");
        }

        // faction leaders are valid, alive, affiliated to that faction
        for (int f = 0; f < F_COUNT; ++f) {
            uint8_t li = w.factions[f].leader;
            CHECK(li != NONE8 && li < w.agent_count, "faction leader index valid");
            CHECK(w.agents[li].faction == f, "leader belongs to its faction");
            CHECK(w.factions[f].specialty < C_COUNT, "faction specialty valid");
            CHECK(w.factions[f].grudge[f] == 0, "no grudge against self");
        }

        // company starts as a solo nobody
        CHECK(w.company.tier == CT_SOLO, "company starts SOLO");
        CHECK(w.company.treasury == 0, "company starts broke");
        CHECK(w.company.emp_count == 0, "company starts with no employees");

        // influence/supply bytes within range (uint8 is implicitly <=255; check >0 somewhere)
        int any_owned = 0;
        for (int i = 0; i < w.district_count; ++i) if (w.districts[i].owner != F_COUNT) ++any_owned;
        CHECK(any_owned > 0, "at least one district is faction-owned");
    }
}

// ---- serialize round-trip ---------------------------------------------------
static void test_roundtrip() {
    std::printf("[save] serialize -> deserialize -> serialize is byte-identical\n");
    for (uint32_t s = 1; s <= 2000; ++s) {
        World a; gen_world(a, s);
        std::string s1; serialize(a, s1);
        World b;
        CHECK(deserialize(s1, b), "deserialize accepts a valid blob");
        std::string s2; serialize(b, s2);
        CHECK(s1 == s2, "round-trip is byte-identical");
        // spot-check a few fields survived
        CHECK(a.world_seed == b.world_seed, "world_seed survives");
        CHECK(a.district_count == b.district_count, "district_count survives");
        CHECK(a.agent_count == b.agent_count, "agent_count survives");
        CHECK(std::memcmp(a.rng.s, b.rng.s, sizeof(a.rng.s)) == 0, "rng state survives");
        CHECK(a.agents[0].money == b.agents[0].money, "player money survives");
    }
    // blob size is stable across seeds (fixed-width format) and "single-digit KB"
    World w; gen_world(w, 7);
    std::string blob; serialize(w, blob);
    std::printf("       save blob = %zu bytes\n", blob.size());
    CHECK(blob.size() < 10000, "save blob is single-digit KB");

    // corruption / truncation must be rejected, not crash
    World junk;
    CHECK(!deserialize(std::string("xx"), junk), "bad magic rejected");
    std::string truncated = blob.substr(0, 50);
    World t;
    bool ok = deserialize(truncated, t);
    CHECK(!ok, "truncated blob rejected (reader underrun)");
}

// ---- Phase 2: tick determinism ---------------------------------------------
static void test_tick_determinism() {
    std::printf("[tick] same seed + same ticks -> identical world\n");
    for (uint32_t s = 1; s <= 300; ++s) {
        World a; gen_world(a, s);
        World b; gen_world(b, s);
        for (int t = 0; t < 500; ++t) { tick_world(a); tick_world(b); }
        std::string sa, sb; serialize(a, sa); serialize(b, sb);
        CHECK(sa == sb, "tick_world is deterministic");
    }
    // ticking then saving/restoring resumes the same future
    World x; gen_world(x, 77);
    for (int t = 0; t < 200; ++t) tick_world(x);
    std::string mid; serialize(x, mid);
    World y; CHECK(deserialize(mid, y), "restore mid-run");
    for (int t = 0; t < 200; ++t) { tick_world(x); tick_world(y); }
    std::string sx, sy; serialize(x, sx); serialize(y, sy);
    CHECK(sx == sy, "save/restore mid-run resumes identical future");
}

// ---- Phase 2: economy soak (the gate) --------------------------------------
// Run many worlds for 10k ticks; assert no extinction, no runaway inflation,
// no deadlock. Tracks aggregate stats; prints a digest.
static void test_economy_soak() {
    const int SEEDS = 80, TICKS = 10000;
    std::printf("[soak] %d worlds x %d ticks: extinction / inflation / deadlock\n", SEEDS, TICKS);

    int worst_survival_pct = 100;
    int global_max_price = 0;
    double sum_late_avg_price = 0; int late_price_samples = 0;
    long total_buys = 0, total_works = 0;
    int frozen_worlds = 0;

    for (uint32_t s = 1; s <= (uint32_t)SEEDS; ++s) {
        World w; gen_world(w, s);
        int start_alive = alive_count(w);

        long buys = 0, works = 0, moves = 0;
        long last_quarter_activity = 0;

        for (int t = 0; t < TICKS; ++t) {
            tick_world(w);

            // sample prices + activity
            for (int i = 0; i < w.agent_count; ++i) {
                uint8_t act = w.agents[i].activity;
                if (act == ACT_BUY) ++buys;
                else if (act == ACT_WORK) ++works;
                else if (act == ACT_MOVE) ++moves;
                if (t >= TICKS * 3 / 4 && (act == ACT_BUY || act == ACT_WORK || act == ACT_MOVE))
                    ++last_quarter_activity;
            }
            if (t == TICKS - 1 || (t % 1000) == 0) {
                int mx = 0; long psum = 0; int pn = 0;
                for (int d = 0; d < w.district_count; ++d) {
                    for (int c = 0; c < C_COUNT; ++c) {
                        int p = price_of(w, (uint8_t)d, (uint8_t)c);
                        if (p > mx) mx = p;
                        psum += p; ++pn;
                    }
                }
                if (mx > global_max_price) global_max_price = mx;
                if (t >= TICKS * 3 / 4 && pn) { sum_late_avg_price += (double)psum / pn; ++late_price_samples; }
            }
        }

        int end_alive = alive_count(w);
        int survival = start_alive ? end_alive * 100 / start_alive : 100;
        if (survival < worst_survival_pct) worst_survival_pct = survival;
        total_buys += buys; total_works += works;

        // --- per-world gate assertions --------------------------------------
        // "No extinction" = the city stays populated. The world is lethal by design
        // (combat/threats/droughts/riots), but population inflow (#26) backfills the
        // dead, so the living population holds at a steady state rather than decaying.
        CHECK(end_alive >= start_alive * 3 / 5, "no extinction (inflow holds the population)");
        CHECK(buys > 0, "economy moves: agents buy");
        CHECK(works > 0, "economy moves: agents work");
        CHECK(last_quarter_activity > 0, "no deadlock: still active in the final quarter");
        if (last_quarter_activity == 0) ++frozen_worlds;
        (void)moves;
    }

    // --- global gate assertions --------------------------------------------
    double late_avg = late_price_samples ? sum_late_avg_price / late_price_samples : 0;
    CHECK(global_max_price <= g_mtune.price_max, "no runaway inflation: price is hard-bounded");
    CHECK(late_avg < g_mtune.price_max * 0.85, "prices not pinned at the cap (economy clears)");
    CHECK(frozen_worlds == 0, "no world deadlocked");

    std::printf("       worst survival=%d%%  max price=%d (cap %d)  late avg price=%.1f\n",
                worst_survival_pct, global_max_price, g_mtune.price_max, late_avg);
    std::printf("       total buys=%ld  works=%ld\n", total_buys, total_works);
}

// ---- Phase 3: auto-career runner -------------------------------------------
struct CareerResult {
    bool     alive;
    uint32_t ticks;       // ticks survived / run
    uint8_t  company_tier;
    uint32_t treasury;
    uint8_t  top_skill;   // best skill tier across professions
    uint8_t  target_skill;// skill tier in the directive's target profession
    uint8_t  job;
};
static CareerResult run_career(uint32_t seed, uint8_t ambition, uint8_t target,
                               uint8_t risk, uint8_t thrift, uint32_t max_ticks) {
    World w; gen_world(w, seed);
    w.directive.ambition = ambition;
    w.directive.target = target;
    w.directive.risk = risk;
    w.directive.thrift = thrift;
    uint32_t t = 0;
    for (; t < max_ticks; ++t) {
        tick_world(w);
        if (!(w.agents[0].status & AF_ALIVE)) break;
        if (w.company.tier == CT_MEGACORP) { ++t; break; } // goal reached
    }
    CareerResult r;
    r.alive = (w.agents[0].status & AF_ALIVE) != 0;
    r.ticks = t;
    r.company_tier = w.company.tier;
    r.treasury = w.company.treasury;
    r.top_skill = top_skill_tier(w.agents[0]);
    uint8_t tj = (target >= J_CONSTRUCTION && target < J_COUNT) ? target : (uint8_t)J_DECKER;
    r.target_skill = skill_tier(w.agents[0].skill[tj]);
    r.job = w.agents[0].job;
    return r;
}

static void career_curve(uint32_t seed, uint8_t ambition) {
    const uint32_t MAX = 200000;
    World w; gen_world(w, seed);
    w.directive.ambition = ambition;
    std::printf("career seed=%u ambition=%s\n", seed, ambition_name(ambition));
    std::printf("   day   tick   $personal  coTier   coTreasury  emp  assets  topSkill  job\n");
    auto snap = [&](uint32_t t) {
        const Agent& p = w.agents[0];
        std::printf("  %5u  %6u  %9u  %-7s  %10u  %3d  %5d   %-7s  %s\n",
                    t / 24, t, p.money, company_tier_name(w.company.tier), w.company.treasury,
                    w.company.emp_count, w.company.asset_count,
                    skill_tier_name(top_skill_tier(p)), job_name(p.job));
    };
    uint32_t marks[] = {0, 240, 1200, 4800, 12000, 48000, 120000, 200000};
    size_t mi = 0;
    for (uint32_t t = 0; t <= MAX; ++t) {
        if (mi < sizeof(marks)/sizeof(marks[0]) && t == marks[mi]) { snap(t); ++mi; }
        if (!(w.agents[0].status & AF_ALIVE)) { std::printf("  >> DIED at tick %u (day %u)\n", t, t/24); break; }
        if (w.company.tier == CT_MEGACORP) { snap(t); std::printf("  >> MEGACORP at tick %u (day %u)\n", t, t/24); break; }
        if (t < MAX) tick_world(w);
    }
}

// ---- Phase 3: career determinism + the gate --------------------------------
static void test_career_determinism() {
    std::printf("[career] same seed + directive -> identical career\n");
    for (uint32_t s = 1; s <= 200; ++s) {
        CareerResult a = run_career(s, AMB_WEALTH, J_DECKER, 128, 128, 8000);
        CareerResult b = run_career(s, AMB_WEALTH, J_DECKER, 128, 128, 8000);
        CHECK(a.alive == b.alive && a.ticks == b.ticks &&
              a.company_tier == b.company_tier && a.treasury == b.treasury,
              "auto-career is deterministic for a fixed seed + directive");
    }
}

// Gate part 1: a WEALTH auto-career sometimes reaches MEGACORP, sometimes
// stalls/dies — no dominant line — and the risk dial steers survival.
static void test_career_outcomes() {
    std::printf("[career] WEALTH outcomes spread (no dominant line); risk steers survival\n");
    const int SEEDS = 80; const uint32_t H = 40000;
    const uint8_t risks[3] = { 30, 128, 225 };
    int mega = 0, dead = 0, alive_nonmega = 0, total = 0;
    int dead_cautious = 0, dead_reckless = 0;
    for (int ri = 0; ri < 3; ++ri) {
        for (uint32_t s = 1; s <= (uint32_t)SEEDS; ++s) {
            CareerResult r = run_career(s, AMB_WEALTH, J_DECKER, risks[ri], 128, H);
            ++total;
            if (!r.alive) { ++dead; if (ri == 0) ++dead_cautious; if (ri == 2) ++dead_reckless; }
            else if (r.company_tier == CT_MEGACORP) ++mega;
            else ++alive_nonmega;
        }
    }
    int megapct = mega * 100 / total, deadpct = dead * 100 / total;
    std::printf("       mega=%d%% dead=%d%% alive-non-mega=%d%%  (cautious dead=%d, reckless dead=%d)\n",
                megapct, deadpct, alive_nonmega * 100 / total, dead_cautious, dead_reckless);
    CHECK(megapct >= 10, "some careers reach MEGACORP");
    CHECK((dead + alive_nonmega) * 100 / total >= 10, "some careers stall or die");
    CHECK(deadpct >= 5, "death has teeth");
    CHECK(megapct < 90 && deadpct < 90, "no single outcome dominates");
    CHECK(dead_reckless > dead_cautious, "risk steers survival: reckless dies more than cautious");
}

// Gate part 2: a set ambition demonstrably steers the auto-career — WEALTH and
// MASTERY produce measurably different traces from the same seeds.
static void test_directive_steering() {
    std::printf("[career] directives steer: WEALTH vs MASTERY produce different traces\n");
    const int SEEDS = 80; const uint32_t H = 40000; const uint8_t risk = 30;
    int w_mega = 0, m_mega = 0, w_master_tgt = 0, m_master_tgt = 0;
    long w_tier = 0, m_tier = 0;
    for (uint32_t s = 1; s <= (uint32_t)SEEDS; ++s) {
        CareerResult W = run_career(s, AMB_WEALTH, J_DECKER, risk, 128, H);
        CareerResult M = run_career(s, AMB_MASTERY, J_DECKER, risk, 128, H);
        if (W.company_tier == CT_MEGACORP) ++w_mega;
        if (M.company_tier == CT_MEGACORP) ++m_mega;
        w_tier += W.company_tier; m_tier += M.company_tier;
        if (W.target_skill == ST_MASTER) ++w_master_tgt;
        if (M.target_skill == ST_MASTER) ++m_master_tgt;
    }
    std::printf("       WEALTH: mega=%d avgTier=%.2f tgtMastered=%d | MASTERY: mega=%d avgTier=%.2f tgtMastered=%d\n",
                w_mega, (double)w_tier / SEEDS, w_master_tgt,
                m_mega, (double)m_tier / SEEDS, m_master_tgt);
    CHECK(w_mega > m_mega, "WEALTH reaches megacorp more than MASTERY");
    CHECK(w_tier > m_tier, "WEALTH builds a bigger company than MASTERY");
    CHECK(m_master_tgt > w_master_tgt, "MASTERY masters the chosen profession far more than WEALTH");
}

static bool is_mega_kind(uint8_t k) {
    return k == TK_CONSTRUCT_MECH || k == TK_SEWER_LEVIATHAN ||
           k == TK_ROGUE_AI_BODY || k == TK_MUTANT_COLONY;
}

// ---- Phase 5: emergent-arc detector ----------------------------------------
// Watches the event stream and recognizes multi-event chain-reactions on a
// district within a window — proving the §3.1 arcs emerge from the base rules.
// (This logic is what the Phase 7 in-engine narrator will reuse.)
enum Arc { ARC_NONE, ARC_DEBT_CASCADE, ARC_WATER_RIOT, ARC_MEGA_RAMPAGE, ARC_FEUD, ARC_CYBER_FALLOUT };
struct ArcDetector {
    int shortage_t[MAX_DISTRICTS], gang_t[MAX_DISTRICTS], riot_t[MAX_DISTRICTS];
    int mega_t[MAX_DISTRICTS], death_t[MAX_DISTRICTS], heist_t[MAX_DISTRICTS];
    int heat_until, W;
    explicit ArcDetector(int w) : heat_until(-(1 << 28)), W(w) {
        for (int i = 0; i < MAX_DISTRICTS; ++i)
            shortage_t[i] = gang_t[i] = riot_t[i] = mega_t[i] = death_t[i] = heist_t[i] = -(1 << 28);
    }
    int ingest(uint8_t kind, uint8_t d, uint8_t data, int t) {
        if (kind == EV_HEATWAVE) { heat_until = t + W; return ARC_NONE; }
        if (d == NONE8 || d >= MAX_DISTRICTS) return ARC_NONE;
        auto recent = [&](int s) { return t - s <= W; };
        // --- stage arming ---
        if (kind == EV_SHORTAGE) shortage_t[d] = t;
        if (kind == EV_THREAT_SPAWN && is_mega_kind(data)) mega_t[d] = t;
        if ((kind == EV_EXTORT || kind == EV_RAID || kind == EV_TURF_FLIP) && recent(shortage_t[d])) gang_t[d] = t;
        if (kind == EV_RIOT && (recent(shortage_t[d]) || t < heat_until)) riot_t[d] = t;
        if (kind == EV_DEATH && data == 1) death_t[d] = t;   // a combat death
        if (kind == EV_HEIST) heist_t[d] = t;                // a big cyberspace score
        // --- completions (check in priority order; reset to avoid recount) ---
        // cyberspace pivotal: a heist's corp fallout erupts into meatspace violence
        if ((kind == EV_RAID || kind == EV_TURF_FLIP || kind == EV_DEATH) && recent(heist_t[d])) {
            heist_t[d] = -(1 << 28); return ARC_CYBER_FALLOUT;
        }
        if ((kind == EV_LOCKDOWN || kind == EV_COMBAT) && recent(gang_t[d])) {
            gang_t[d] = shortage_t[d] = -(1 << 28); return ARC_DEBT_CASCADE;
        }
        if ((kind == EV_TURF_FLIP || kind == EV_REFUGEE || kind == EV_RAID) && recent(riot_t[d])) {
            riot_t[d] = -(1 << 28); return ARC_WATER_RIOT;
        }
        if (kind == EV_THREAT_DEFEAT && is_mega_kind(data) && recent(mega_t[d])) {
            mega_t[d] = -(1 << 28); return ARC_MEGA_RAMPAGE;
        }
        if (kind == EV_COMBAT && t - death_t[d] <= W && death_t[d] < t) {
            death_t[d] = -(1 << 28); return ARC_FEUD;
        }
        return ARC_NONE;
    }
};

static void test_emergence() {
    std::printf("[arcs] the named chain-reactions fire from the base rules (§3.1)\n");
    const int SEEDS = 60; const uint32_t TICKS = 30000; const int W = 720; // 30-day window
    long arc[6] = {0};
    long raw[EV_COUNT] = {0};
    for (uint32_t s = 1; s <= (uint32_t)SEEDS; ++s) {
        World w; gen_world(w, s);
        ArcDetector det(W);
        for (uint32_t t = 0; t < TICKS; ++t) {
            tick_world(w);
            uint16_t tk = (uint16_t)(w.tick - 1);
            for (int i = 0; i < EVMAX; ++i) {
                const Event& e = w.events[i];
                if (e.kind == EV_NONE || e.tick != tk) continue;
                raw[e.kind]++;
                int a = det.ingest(e.kind, e.node, e.data, (int)w.tick - 1);
                if (a != ARC_NONE) arc[a]++;
            }
        }
    }
    std::printf("       raw: shortage=%ld heatwave=%ld lockdown=%ld riot=%ld turf=%ld refugee=%ld death=%ld\n",
                raw[EV_SHORTAGE], raw[EV_HEATWAVE], raw[EV_LOCKDOWN], raw[EV_RIOT],
                raw[EV_TURF_FLIP], raw[EV_REFUGEE], raw[EV_DEATH]);
    std::printf("       arcs: debt-cascade=%ld  water-riot=%ld  mega-rampage=%ld  feud=%ld  cyber-fallout=%ld  (over %d worlds)\n",
                arc[ARC_DEBT_CASCADE], arc[ARC_WATER_RIOT], arc[ARC_MEGA_RAMPAGE], arc[ARC_FEUD],
                arc[ARC_CYBER_FALLOUT], SEEDS);
    CHECK(arc[ARC_DEBT_CASCADE] > 0, "black-clinic debt-cascade arcs emerge (§3.1)");
    CHECK(arc[ARC_WATER_RIOT] > 0, "water-ration riot arcs emerge (§3.1)");
    CHECK(arc[ARC_MEGA_RAMPAGE] > 0, "megathreat-rampage arcs emerge");
    CHECK(arc[ARC_FEUD] > 0, "revenge-feud arcs emerge");
    CHECK(arc[ARC_CYBER_FALLOUT] > 0, "cyberspace heists spill into meatspace arcs (pivotal)");
}

// ---- Phase 4: combat mechanics + escalation classifier ---------------------
static void test_combat_mechanics() {
    std::printf("[combat] gear/aggression drive atk/def; escalation classifier\n");
    World w; gen_world(w, 1);
    Agent armed = w.agents[1];
    armed.inv[IT_WEAPONS] = 5; armed.inv[IT_ARMOR] = 5; armed.inv[IT_IMPLANTS] = 2;
    armed.trait[TR_AGGRESSION] = 220; armed.status &= (uint8_t)~AF_INJURED; armed.mood = 200;
    Agent meek = w.agents[2];
    for (int i = 0; i < IT_COUNT; ++i) meek.inv[i] = 0;
    meek.trait[TR_AGGRESSION] = 20; meek.status &= (uint8_t)~AF_INJURED; meek.mood = 80;
    CHECK(combat_atk(w, armed) > combat_atk(w, meek) * 2, "weapons + aggression raise attack");
    CHECK(combat_def(w, armed) > combat_def(w, meek), "armor raises defense");
    // agency model: a trivial foe is auto-resolved; a deadly one interrupts (§1)
    CHECK(!avatar_fight_escalates(w, 1), "a trivial foe does not interrupt the avatar");
    CHECK(avatar_fight_escalates(w, 999), "a deadly foe interrupts the avatar");
}

// ---- Phase 6: CyberHack bridge (the gate) ----------------------------------
static void test_cyberhack_bridge() {
    std::printf("[cyber] headless jack-ins resolve, feed back, and the matrix remembers\n");
    // determinism: same seed + same agent -> identical jack outcome
    {
        World a, b; gen_world(a, 5); gen_world(b, 5);
        a.agents[3].status |= AF_HAS_DECK; b.agents[3].status |= AF_HAS_DECK;
        JackResult ra = jack_in(a, 3), rb = jack_in(b, 3);
        CHECK(ra.ran && rb.ran, "a deck-equipped agent can jack a data target");
        CHECK(ra.shards == rb.shards && ra.outcome == rb.outcome && ra.killed == rb.killed,
              "jack-ins are deterministic for a fixed seed");
    }
    // resolution + feedback + memory over many runs
    World w; gen_world(w, 9);
    CHECK(net_target_count(w) > 0, "world has a data target to hack");
    int ai = 4; int ran = 0, extracted = 0, flatlined = 0, allies = 0, heists = 0;
    int hanging = 0, money_fed = 0;
    for (int j = 0; j < 600; ++j) {
        Agent& p = w.agents[ai];
        p.status |= AF_ALIVE | AF_HAS_DECK;        // keep the test subject in play
        p.money = 0; p.status &= (uint8_t)~AF_INJURED;
        JackResult r = jack_in(w, ai);
        if (!r.ran) continue;
        ++ran;
        if (r.outcome != cyber::O_EXTRACTED && r.outcome != cyber::O_DIED) ++hanging;
        if (r.outcome == cyber::O_EXTRACTED) ++extracted;
        if (r.flatlined) { ++flatlined; if (!r.killed) CHECK(w.agents[ai].status & AF_INJURED, "a flatline injures in meatspace"); }
        if (r.net_ally) ++allies;
        if (r.shards >= (uint32_t)g_mtune.heist_score) ++heists;
        if (w.agents[ai].money == r.shards) ++money_fed;
    }
    int max_runcount = 0;
    for (int i = 0; i < MAX_NET; ++i) if (w.net_legends[i].run_count > max_runcount) max_runcount = w.net_legends[i].run_count;
    std::printf("       ran=%d extracted=%d flatlined=%d heists=%d net-allies=%d  matrix run_count(max)=%d\n",
                ran, extracted, flatlined, heists, allies, max_runcount);
    CHECK(ran > 0, "jacks run");
    CHECK(hanging == 0, "every jack-in resolves (extract or flatline, never left running)");
    CHECK(extracted > 0, "some jack-ins extract shards");
    CHECK(money_fed == ran, "extracted shards feed back to the agent's money");
    CHECK(max_runcount > 1, "the matrix remembers a target across jack-ins (legends persist)");
}

// ---- Phase 4: the systems fire & feed back (the gate) -----------------------
static void test_phase4_systems() {
    std::printf("[world4] territory/threats/combat fire and feed the consequence layer\n");
    const int SEEDS = 40; const uint32_t TICKS = 20000;
    long ev[EV_COUNT] = {0};
    int mega_spawn = 0, mega_defeat = 0;
    int combat_scars = 0, grudge_peak = 0;
    auto is_mega = [](uint8_t k){ return k == TK_CONSTRUCT_MECH || k == TK_SEWER_LEVIATHAN ||
                                          k == TK_ROGUE_AI_BODY || k == TK_MUTANT_COLONY; };
    for (uint32_t s = 1; s <= (uint32_t)SEEDS; ++s) {
        World w; gen_world(w, s);
        for (uint32_t t = 0; t < TICKS; ++t) {
            tick_world(w);
            uint16_t tk = (uint16_t)(w.tick - 1);
            for (int i = 0; i < EVMAX; ++i) {
                const Event& e = w.events[i];
                if (e.kind != EV_NONE && e.tick == tk) {
                    ev[e.kind]++;
                    if (e.kind == EV_THREAT_SPAWN && is_mega(e.data)) ++mega_spawn;
                    if (e.kind == EV_THREAT_DEFEAT && is_mega(e.data)) ++mega_defeat;
                }
            }
        }
        // feedback: combat leaves scars and grudges
        for (int i = 0; i < w.agent_count; ++i)
            for (int k = 0; k < SCARMAX; ++k)
                if (w.agents[i].scar[k].kind == 1) { ++combat_scars; break; }
        for (int f = 0; f < F_COUNT; ++f)
            for (int g = 0; g <= F_COUNT; ++g)
                if (w.factions[f].grudge[g] > grudge_peak) grudge_peak = w.factions[f].grudge[g];
    }
    std::printf("       combat=%ld turf=%ld raid=%ld spawn=%ld defeat=%ld (mega %d/%d) refugee=%ld death=%ld extort=%ld\n",
                ev[EV_COMBAT], ev[EV_TURF_FLIP], ev[EV_RAID], ev[EV_THREAT_SPAWN],
                ev[EV_THREAT_DEFEAT], mega_spawn, mega_defeat, ev[EV_REFUGEE], ev[EV_DEATH], ev[EV_EXTORT]);
    std::printf("       combat-scarred agents=%d  peak faction grudge=%d\n", combat_scars, grudge_peak);
    CHECK(ev[EV_COMBAT] > 0, "combat resolves (raids/fights)");
    CHECK(ev[EV_TURF_FLIP] > 0, "turf wars flip district ownership");
    CHECK(ev[EV_THREAT_SPAWN] > 0, "threats spawn");
    CHECK(ev[EV_THREAT_DEFEAT] > 0, "threats get driven off");
    CHECK(mega_spawn > 0, "megathreats (DF megabeasts) appear");
    CHECK(mega_defeat > 0, "a megathreat can be driven off");
    CHECK(ev[EV_REFUGEE] > 0, "violence displaces people (refugees)");
    CHECK(ev[EV_DEATH] > 0, "combat/threats are lethal");
    CHECK(combat_scars > 0, "combat leaves memory scars");
    CHECK(grudge_peak > 10, "combat/turf builds faction grudges");
}

// ---- population inflow + automation tide (endless world) -------------------
static void test_population_inflow() {
    std::printf("[pop] inflow keeps the city alive; the automation tide can replace it\n");
    // 1) inflow holds the population over a long run (no slow depopulation)
    {
        World w; gen_world(w, 11);
        int start = alive_count(w);
        long newcomers = 0;
        for (uint32_t t = 0; t < 40000; ++t) {
            tick_world(w);
            uint16_t tk = (uint16_t)(w.tick - 1);
            for (int i = 0; i < EVMAX; ++i) if (w.events[i].kind == EV_NEWCOMER && w.events[i].tick == tk) ++newcomers;
        }
        int end = alive_count(w);
        std::printf("       run A: alive %d -> %d over 1666 days, %ld newcomers admitted\n", start, end, newcomers);
        CHECK(newcomers > 0, "newcomers arrive to backfill the dead");
        CHECK(end >= start * 3 / 5, "inflow keeps the population from collapsing");
    }
    // 2) over a long game the automation tide rises and synths can take over
    //    (the protagonist's road to being the last human)
    {
        World w; gen_world(w, 7);
        int synth0 = synth_count(w), human0 = human_count(w);
        bool synth_majority_ever = false;
        for (uint32_t t = 0; t < 200000; ++t) {  // ~8300 days
            tick_world(w);
            if (synth_count(w) > human_count(w)) synth_majority_ever = true;
        }
        std::printf("       run B: tide %d, humans %d->%d, synths %d->%d, synth-majority reached=%s\n",
                    w.synth_tide, human0, human_count(w), synth0, synth_count(w),
                    synth_majority_ever ? "yes" : "no");
        CHECK(w.synth_tide > 60, "the automation tide rises over a long game");
        CHECK(synth_count(w) > synth0, "the synthetic population grows as humans are replaced");
        CHECK(alive_count(w) > 0, "the city stays alive even as it changes hands");
    }
}

// ---- Phase 7: generative narrator (the gate) -------------------------------
static bool ascii_ok(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return false;
    return true;
}
static void test_narrator() {
    std::printf("[narr] every event narrates (Gibson voice, deterministic, ASCII); arcs too\n");
    World w; gen_world(w, 3);
    // 1) every event kind produces a non-empty, ASCII, deterministic line
    for (uint8_t k = EV_NONE + 1; k < EV_COUNT; ++k) {
        Event e; e.kind = k; e.node = 1; e.agent = 2; e.data = 1; e.tick = 100;
        std::string a = narrate_event(w, e), b = narrate_event(w, e);
        CHECK(ascii_ok(a), "event line is non-empty + ASCII (fits the CYD font)");
        CHECK(a == b, "narration is deterministic");
    }
    // 2) every arc kind narrates distinctly
    for (uint8_t a = MA_NONE + 1; a < MA_COUNT; ++a) {
        std::string s = narrate_arc(w, a, 1, EV_SHORTAGE, EV_RIOT);
        CHECK(ascii_ok(s), "arc line is non-empty + ASCII");
    }
    // 3) a real run yields a deterministic chronicle, including recognized arcs
    auto chronicle_hash = [](uint32_t seed) {
        World ww; gen_world(ww, seed);
        ArcTracker tr; tr.reset(720);
        uint32_t hh = 2166136261u; long lines = 0, arcs = 0;
        for (uint32_t t = 0; t < 20000; ++t) {
            tick_world(ww);
            uint16_t tk = (uint16_t)(ww.tick - 1);
            for (int i = 0; i < EVMAX; ++i) {
                const Event& e = ww.events[i];
                if (e.kind == EV_NONE || e.tick != tk) continue;
                std::string line = narrate_event(ww, e); ++lines;
                for (char c : line) hh = (hh ^ (unsigned char)c) * 16777619u;
                uint8_t arc = tr.ingest(e.kind, e.node, e.data, (int)ww.tick - 1);
                if (arc != MA_NONE) {
                    std::string al = narrate_arc(ww, arc, e.node, tr.last_kind[e.node], e.kind); ++arcs;
                    for (char c : al) hh = (hh ^ (unsigned char)c) * 16777619u;
                }
            }
        }
        return std::make_pair(hh, std::make_pair(lines, arcs));
    };
    auto r1 = chronicle_hash(8);
    auto r2 = chronicle_hash(8);
    CHECK(r1.first == r2.first, "the whole chronicle is reproducible from the seed");
    CHECK(r1.second.first > 100, "a run produces a stream of narrated beats");
    CHECK(r1.second.second > 0, "recognized arcs are narrated within the stream");
    std::printf("       seed 8: %ld narrated beats, %ld arc beats (deterministic hash %08x)\n",
                r1.second.first, r1.second.second, r1.first);
    // show a few sample lines for eyeballing
    std::printf("       e.g.  %s\n", narrate_event(w, [&]{ Event e; e.kind=EV_HEIST; e.node=2; e.agent=3; e.tick=1; return e; }()).c_str());
    std::printf("             %s\n", narrate_arc(w, MA_WATER_RIOT, 0, EV_SHORTAGE, EV_RIOT).c_str());
}

// ---- Phase 3: distribution scan (calibration aid) --------------------------
static void scan_outcomes(uint8_t ambition, uint32_t horizon) {
    const int SEEDS = 120;
    const uint8_t risks[3] = { 30, 128, 225 };
    const char* rn[3] = { "cautious", "balanced", "reckless" };
    std::printf("scan ambition=%s horizon=%u ticks (~%u days)\n",
                ambition_name(ambition), horizon, horizon / 24);
    std::printf("  risk      MEGA  CORP  OUTF  CREW  SOLO  DEAD   reachedMaster\n");
    for (int ri = 0; ri < 3; ++ri) {
        int mega = 0, corp = 0, outf = 0, crew = 0, solo = 0, dead = 0, master = 0;
        for (uint32_t s = 1; s <= (uint32_t)SEEDS; ++s) {
            CareerResult r = run_career(s, ambition, J_DECKER, risks[ri], 128, horizon);
            if (!r.alive) ++dead;
            else switch (r.company_tier) {
                case CT_MEGACORP: ++mega; break;
                case CT_CORP: ++corp; break;
                case CT_OUTFIT: ++outf; break;
                case CT_CREW: ++crew; break;
                default: ++solo; break;
            }
            if (r.top_skill == ST_MASTER) ++master;
        }
        std::printf("  %-8s  %4d  %4d  %4d  %4d  %4d  %4d   %4d/%d\n",
                    rn[ri], mega, corp, outf, crew, solo, dead, master, SEEDS);
    }
}

// ---- a readable world dump for eyeballing -----------------------------------
static void dump_world(uint32_t seed) {
    World w; gen_world(w, seed);
    std::printf("=== Midnight City  seed=%u ===\n", seed);
    std::printf("districts=%d  agents=%d  home=%d (%s)  company=%s [%s]\n",
                w.district_count, w.agent_count, w.home,
                district_type_name(w.districts[w.home].type),
                company_name(w.company.name_id),
                w.company.tier == CT_SOLO ? "SOLO" : "?");
    std::printf("\nDistricts:\n");
    for (int i = 0; i < w.district_count; ++i) {
        const District& d = w.districts[i];
        std::printf("  %2d %-13s owner=%-13s pop=%3d prosp=%3d danger=%3d deg=%d -> ",
                    i, district_type_name(d.type), faction_name(d.owner),
                    d.population, d.prosperity, d.danger, d.deg);
        for (int k = 0; k < d.deg; ++k) std::printf("%d ", d.adj[k]);
        std::printf("\n");
    }
    std::printf("\nFactions:\n");
    for (int f = 0; f < F_COUNT; ++f) {
        const FactionState& fs = w.factions[f];
        std::printf("  %-13s leader=%s@d%d  fav=%s  treasury=%u\n",
                    faction_name(f), agent_name(w.agents[fs.leader].name_id),
                    w.agents[fs.leader].loc, commodity_name(fs.specialty), fs.treasury);
    }
    std::printf("\nProtagonist: %s @ d%d (%s)  $%u  job=%s\n",
                agent_name(w.agents[0].name_id), w.agents[0].loc,
                district_type_name(w.districts[w.agents[0].loc].type),
                w.agents[0].money, job_name(w.agents[0].job));
    std::printf("\nSample NPCs:\n");
    for (int i = 1; i < w.agent_count && i <= 8; ++i) {
        const Agent& a = w.agents[i];
        std::printf("  %-12s @ d%-2d  %-13s  job=%-18s $%-4u %s\n",
                    agent_name(a.name_id), a.loc, faction_name(a.faction),
                    job_name(a.job), a.money, (a.status & AF_HAS_DECK) ? "[deck]" : "");
    }
}

// ---- economy health curve for one world (tuning aid) -----------------------
static void econ_curve(uint32_t seed) {
    World w; gen_world(w, seed);
    int start = alive_count(w);
    std::printf("seed=%u  agents=%d districts=%d\n", seed, w.agent_count, w.district_count);
    std::printf("  tick   alive  emp%%  debt%%  avgPrice  maxPrice  totMoney\n");
    auto snap = [&](int t) {
        int al = 0, emp = 0, debt = 0; long money = 0;
        for (int i = 0; i < w.agent_count; ++i) {
            const Agent& a = w.agents[i];
            if (!(a.status & AF_ALIVE)) continue;
            ++al; money += a.money;
            if (a.status & AF_EMPLOYED) ++emp;
            if (a.status & AF_IN_DEBT) ++debt;
        }
        long psum = 0; int pn = 0, mx = 0;
        for (int d = 0; d < w.district_count; ++d) for (int c = 0; c < C_COUNT; ++c) {
            int p = price_of(w, (uint8_t)d, (uint8_t)c); psum += p; ++pn; if (p > mx) mx = p;
        }
        std::printf("  %5d  %4d   %3d   %4d   %7.1f   %7d   %8ld\n",
                    t, al, al ? emp * 100 / al : 0, al ? debt * 100 / al : 0,
                    pn ? (double)psum / pn : 0, mx, money);
    };
    int marks[] = {0, 100, 500, 1000, 2000, 5000, 10000};
    int mi = 0;
    for (int t = 0; t <= 10000; ++t) {
        if (mi < (int)(sizeof(marks)/sizeof(marks[0])) && t == marks[mi]) { snap(t); ++mi; }
        if (t < 10000) tick_world(w);
    }
    std::printf("  start_alive=%d end_alive=%d (%d%%)\n", start, alive_count(w),
                start ? alive_count(w) * 100 / start : 100);
}

int main(int argc, char** argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "world")) {
        dump_world(argc >= 3 ? (uint32_t)std::strtoul(argv[2], nullptr, 10) : 1);
        return 0;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "econ")) {
        econ_curve(argc >= 3 ? (uint32_t)std::strtoul(argv[2], nullptr, 10) : 1);
        return 0;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "career")) {
        uint32_t seed = argc >= 3 ? (uint32_t)std::strtoul(argv[2], nullptr, 10) : 1;
        uint8_t amb = AMB_WEALTH;
        if (argc >= 4) {
            if (!std::strcmp(argv[3], "mastery")) amb = AMB_MASTERY;
            else if (!std::strcmp(argv[3], "territory")) amb = AMB_TERRITORY;
            else if (!std::strcmp(argv[3], "survive")) amb = AMB_SURVIVE;
        }
        career_curve(seed, amb);
        return 0;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "scan")) {
        uint8_t amb = AMB_WEALTH;
        if (argc >= 3) {
            if (!std::strcmp(argv[2], "mastery")) amb = AMB_MASTERY;
            else if (!std::strcmp(argv[2], "territory")) amb = AMB_TERRITORY;
            else if (!std::strcmp(argv[2], "survive")) amb = AMB_SURVIVE;
        }
        uint32_t horizon = argc >= 4 ? (uint32_t)std::strtoul(argv[3], nullptr, 10) : 19200;
        scan_outcomes(amb, horizon);
        return 0;
    }
    std::printf("Midnight City — Phase 1 substrate tests\n\n");
    test_rng();
    test_determinism();
    test_worldgen_invariants();
    test_roundtrip();
    test_tick_determinism();
    test_economy_soak();
    test_career_determinism();
    test_career_outcomes();
    test_directive_steering();
    test_combat_mechanics();
    test_phase4_systems();
    test_cyberhack_bridge();
    test_population_inflow();
    test_emergence();
    test_narrator();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
