// CyberHack headless harness — the Phase 1 keystone. Property tests, determinism
// tests, the balance sweep, and the content linter, all driving the pure engine
// with no UI. Dependency-free (a tiny CHECK macro, no gtest). Lives in sim/,
// outside the emulator's src/ GLOB, so it never collides with host/main.cpp and
// never compiles into firmware.
//
//   make sim && ./cyberhack_sim            # run all tests
//   ./cyberhack_sim balance 20000 cautious # balance sweep
//   ./cyberhack_sim run 1 42 reckless      # one verbose dive
//   ./cyberhack_sim chronicle 1 42 cautious
//   ./cyberhack_sim lint
#include "core/cyberhack_world.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

using namespace cyber;

// ---- tiny test harness -----------------------------------------------------
static int g_fail = 0, g_checks = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_fail; \
    std::printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)

static uint8_t parse_pol(const char* s) {
    if (!s) return P_CAUTIOUS;
    if (!std::strcmp(s, "reckless")) return P_RECKLESS;
    if (!std::strcmp(s, "cautious")) return P_CAUTIOUS;
    if (!std::strcmp(s, "opportunist")) return P_OPPORTUNIST;
    if (!std::strcmp(s, "loyalist")) return P_LOYALIST;
    return P_CAUTIOUS;
}
static const char* pol_name(uint8_t p) {
    const char* n[] = {"reckless", "cautious", "opportunist", "loyalist"};
    return p < P_COUNT ? n[p] : "?";
}

// BFS reachability over a fresh world (no burned edges)
static bool all_reachable(const World& w) {
    std::vector<uint8_t> seen(w.node_count, 0);
    std::vector<uint8_t> q; q.push_back(w.entry); seen[w.entry] = 1; size_t h = 0;
    while (h < q.size()) {
        const Node& n = w.nodes[q[h++]];
        for (int k = 0; k < n.deg; ++k) {
            uint8_t nb = n.nbr[k];
            if (nb != NONE8 && !seen[nb]) { seen[nb] = 1; q.push_back(nb); }
        }
    }
    for (int i = 0; i < w.node_count; ++i) if (!seen[i]) return false;
    return true;
}

// ---- Phase 1: determinism --------------------------------------------------
static void test_rng() {
    std::printf("[rng] determinism + range bounds\n");
    Rng a, b; a.seed(12345); b.seed(12345);
    for (int i = 0; i < 1000; ++i) CHECK(a.next() == b.next(), "same seed must give same stream");
    Rng c; c.seed(999);
    for (int i = 0; i < 10000; ++i) { uint32_t r = c.range(7); CHECK(r < 7, "range(7) in [0,7)"); }
    Rng d; d.seed(1); Rng e; e.seed(2);
    int diff = 0; for (int i = 0; i < 64; ++i) if (d.next() != e.next()) ++diff;
    CHECK(diff > 50, "different seeds diverge");
}

// ---- Phase 2: world generation invariants ----------------------------------
static void test_world_invariants() {
    std::printf("[world] invariants over 10000 seeds\n");
    int worst_deg = 0;
    for (uint32_t s = 1; s <= 10000; ++s) {
        Sim sim; sim.start(1, s, P_CAUTIOUS, nullptr);
        const World& w = sim.world();
        CHECK(w.node_count >= 40 && w.node_count <= MAX_NODES, "node count in [40,MAX]");
        CHECK(w.edge_count <= MAX_EDGES, "edge count bounded");
        CHECK(all_reachable(w), "graph fully connected from entry");
        CHECK(w.objective.target < w.node_count, "objective node valid");
        for (int i = 0; i < w.node_count; ++i) {
            CHECK(w.nodes[i].deg <= 5, "degree <= 5");
            if (w.nodes[i].deg > worst_deg) worst_deg = w.nodes[i].deg;
            for (int k = 0; k < w.nodes[i].deg; ++k) CHECK(w.nodes[i].nbr[k] != i, "no self-loop");
            CHECK(w.nodes[i].security <= 9, "security <= 9");
        }
    }
    // determinism: same seeds → identical topology
    Sim x, y; x.start(7, 42, P_CAUTIOUS, nullptr); y.start(7, 42, P_CAUTIOUS, nullptr);
    CHECK(x.world().node_count == y.world().node_count, "world gen deterministic (nodes)");
    CHECK(x.world().edge_count == y.world().edge_count, "world gen deterministic (edges)");
    CHECK(x.world().objective.target == y.world().objective.target, "objective deterministic");
    std::printf("  (max observed degree %d)\n", worst_deg);
}

// ---- Phase 3: every run terminates in a valid state ------------------------
static void test_termination() {
    std::printf("[run] 10000 runs x 4 policies terminate cleanly\n");
    for (uint8_t p = 0; p < P_COUNT; ++p) {
        for (uint32_t s = 1; s <= 10000; ++s) {
            Sim sim; run_headless(sim, 1, s, p, nullptr);
            const RunState& r = sim.state();
            CHECK(r.outcome != O_RUNNING, "run terminated");
            CHECK(r.step <= MAX_STEPS, "step bound respected");
            CHECK(r.integrity >= 0 && r.integrity <= r.integrity_max, "integrity in range");
            CHECK(r.corruption <= 100, "corruption <= 100");
        }
    }
}

// ---- Phase 5: legends round-trip + cross-run escalation ---------------------
static bool legends_equal(const Legends& a, const Legends& b) {
    if (a.citynet_seed != b.citynet_seed || a.run_count != b.run_count || a.best_score != b.best_score) return false;
    for (int f = 0; f < F_COUNT; ++f) if (a.grudge[f] != b.grudge[f]) return false;
    if (a.named_count != b.named_count || a.burned_count != b.burned_count || a.marked_count != b.marked_count) return false;
    for (int i = 0; i < a.named_count; ++i) if (a.named[i].name_id != b.named[i].name_id || a.named[i].status != b.named[i].status) return false;
    return true;
}
static void test_legends() {
    std::printf("[legends] serialize round-trip + escalation\n");
    Legends L{}; L.citynet_seed = 1; L.run_count = 3; L.best_score = 1234;
    L.grudge[F_KUROGANE] = 5; L.grudge[F_VULTURES] = -2;
    L.named_count = 2; L.named[0] = {3, I_BLACK, NS_DEAD, 4}; L.named[1] = {5, I_WARDEN, NS_CRIPPLED, 2};
    L.burned_count = 1; L.burned[0] = {4, 9};
    L.marked_count = 2; L.marked[0] = 11; L.marked[1] = 22;
    std::string s; legends_serialize(L, s);
    Legends R{}; CHECK(legends_deserialize(s, R), "deserialize ok");
    CHECK(legends_equal(L, R), "legends round-trip byte-stable");

    // escalation: feed grudges back, confirm the world boots more hostile
    Legends grudged{}; grudged.citynet_seed = 1; for (int f = 0; f < F_COUNT; ++f) grudged.grudge[f] = 6;
    Sim a, b; a.start(1, 100, P_CAUTIOUS, nullptr); b.start(1, 100, P_CAUTIOUS, &grudged);
    int hostile_a = 0, hostile_b = 0;
    for (int f = 0; f < F_COUNT; ++f) { if (a.world().factions[f].attitude >= A_HOSTILE) ++hostile_a; if (b.world().factions[f].attitude >= A_HOSTILE) ++hostile_b; }
    CHECK(hostile_b > hostile_a, "grudges raise faction hostility next run");
}

// ---- Phase 6: chronicle determinism + no unfilled slots --------------------
static void test_chronicle() {
    std::printf("[chronicle] determinism + slot fill\n");
    for (uint32_t s = 1; s <= 200; ++s) {
        Sim a; run_headless(a, 1, s, P_RECKLESS, nullptr);
        Sim b; run_headless(b, 1, s, P_RECKLESS, nullptr);
        std::string ca = a.chronicle(), cb = b.chronicle();
        CHECK(ca == cb, "same seed -> same chronicle");
        CHECK(ca.find('{') == std::string::npos, "no unfilled {slots}");
        CHECK(ca.size() > 40 && ca.size() < 1200, "chronicle length sane");
    }
}

// ---- Phase 7: content linter -----------------------------------------------
static int lint() {
    std::printf("[lint] content tables\n");
    int bad = 0;
    for (int i = 0; i < I_COUNT; ++i) {
        if (ice_weakness(i) >= M_COUNT) { std::printf("  ICE %s has no weakness\n", ice_name(i)); ++bad; }
        if (std::strlen(ice_name(i)) == 0) { std::printf("  ICE %d unnamed\n", i); ++bad; }
    }
    for (int m = 0; m < M_COUNT; ++m) if (std::strlen(module_name(m)) == 0) { std::printf("  module %d unnamed\n", m); ++bad; }
    for (int f = 0; f < F_COUNT; ++f) if (std::strlen(faction_name(f)) == 0) { std::printf("  faction %d unnamed\n", f); ++bad; }
    // every decision a run can raise must offer >=2 options with labels
    int dec_seen = 0;
    for (uint32_t s = 1; s <= 500 && dec_seen < 50; ++s) {
        Sim sim; sim.start(1, s, P_OPPORTUNIST, nullptr);
        Rng pol; pol.seed(s);
        int guard = 0;
        while (sim.running() && guard++ < MAX_STEPS * 4) {
            AdvanceResult ar = sim.advance();
            if (ar == AR_ENDED) break;
            if (ar == AR_DECISION) {
                const Decision& d = sim.decision(); ++dec_seen;
                if (d.options.size() < 2) { std::printf("  decision kind %d has <2 options\n", d.kind); ++bad; }
                if (d.options.size() != d.opt_module.size()) { std::printf("  option/module size mismatch\n"); ++bad; }
                for (auto& o : d.options) if (o.empty()) { std::printf("  empty option label\n"); ++bad; }
                sim.choose(ai_decide(P_OPPORTUNIST, sim.state(), d, pol));
            }
        }
    }
    std::printf("  checked %d decisions; %d problems\n", dec_seen, bad);
    return bad;
}

// ---- Phase 4: balance sweep ------------------------------------------------
static void balance(uint32_t seeds, uint8_t pol) {
    std::printf("=== balance: %u runs, policy=%s, citynet=1 ===\n", seeds, pol_name(pol));
    int extracted = 0, died = 0;
    int death_cause[D_TIMEOUT + 1] = {0};
    long sum_steps = 0, sum_tier = 0, sum_shards = 0, sum_heat = 0, sum_corr = 0, sum_score = 0, sum_named = 0;
    long obj_done = 0, sum_depth = 0, sum_objs = 0, max_depth = 0;
    // power-vs-threat gap, sampled at every decision point across all runs
    long gap_sum = 0; long gap_n = 0; int gap_min = 1 << 30, gap_max = -(1 << 30);
    for (uint32_t s = 1; s <= seeds; ++s) {
        Sim sim; sim.start(1, s, pol, nullptr);
        Rng pr; pr.seed(((uint64_t)s << 13) ^ pol ^ 0xA11CEuLL);
        int guard = 0;
        while (sim.running() && guard++ < MAX_STEPS * 4) {
            AdvanceResult ar = sim.advance();
            if (ar == AR_ENDED) break;
            if (ar == AR_DECISION) {
                const RunState& r = sim.state();
                int gap = player_power(r) - threat_level(sim.world(), r, sim.world().nodes[r.pos]);
                gap_sum += gap; ++gap_n; if (gap < gap_min) gap_min = gap; if (gap > gap_max) gap_max = gap;
                sim.choose(ai_decide(pol, r, sim.decision(), pr));
            }
        }
        const RunState& r = sim.state();
        if (r.outcome == O_EXTRACTED) ++extracted; else { ++died; death_cause[r.death_cause]++; }
        if (r.objectives_done > 0) ++obj_done;
        sum_depth += r.depth; sum_objs += r.objectives_done; if (r.depth > max_depth) max_depth = r.depth;
        sum_steps += r.step; sum_tier += r.tier; sum_shards += r.shards;
        sum_heat += r.heat; sum_corr += r.corruption; sum_score += sim.score(); sum_named += r.named_killed;
    }
    double n = seeds ? seeds : 1;
    std::printf("extract  %5.1f%%   died %5.1f%%   cracked>=1 obj %5.1f%%\n",
                100.0 * extracted / n, 100.0 * died / n, 100.0 * obj_done / n);
    std::printf("descent: avg layer %.2f (max %ld)   avg objectives %.2f\n",
                sum_depth / n + 1.0, max_depth + 1, sum_objs / n);
    const char* dn[] = {"none", "ice", "corruption", "hunted", "trace", "timeout"};
    std::printf("deaths:");
    for (int i = 0; i <= D_TIMEOUT; ++i) if (death_cause[i]) std::printf("  %s=%.1f%%", dn[i], 100.0 * death_cause[i] / n);
    std::printf("\n");
    std::printf("avg  steps %.1f  tier %.2f  shards %.0f  heat %.1f  corr %.1f  named %.2f  score %.0f\n",
                sum_steps / n, sum_tier / n, sum_shards / n, sum_heat / n, sum_corr / n, sum_named / n, sum_score / n);
    std::printf("power-vs-threat gap: avg %.1f  min %d  max %d  (n=%ld decisions)\n",
                gap_n ? (double)gap_sum / gap_n : 0.0, gap_min == (1 << 30) ? 0 : gap_min, gap_max, gap_n);
}

static void run_verbose(uint32_t cnet, uint32_t rseed, uint8_t pol) {
    Sim sim; run_headless(sim, cnet, rseed, pol, nullptr);
    std::printf("=== run citynet=%u seed=%u policy=%s ===\n", cnet, rseed, pol_name(pol));
    for (auto& l : sim.log()) std::printf("  %s\n", l.c_str());
    const RunState& r = sim.state();
    std::printf("-- outcome=%s steps=%d tier=%d shards=%d heat=%d corr=%d score=%d\n",
                r.outcome == O_EXTRACTED ? "EXTRACTED" : "DIED", r.step, r.tier, r.shards, r.heat, r.corruption, sim.score());
    std::printf("\n%s\n", sim.chronicle().c_str());
}

int main(int argc, char** argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "balance")) {
        balance(argc >= 3 ? (uint32_t)std::strtoul(argv[2], nullptr, 10) : 20000, parse_pol(argc >= 4 ? argv[3] : "cautious"));
        return 0;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "run")) {
        run_verbose(argc >= 3 ? (uint32_t)std::strtoul(argv[2], nullptr, 10) : 1,
                    argc >= 4 ? (uint32_t)std::strtoul(argv[3], nullptr, 10) : 42, parse_pol(argc >= 5 ? argv[4] : "cautious"));
        return 0;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "chronicle")) {
        Sim sim; run_headless(sim, argc >= 3 ? (uint32_t)std::strtoul(argv[2], nullptr, 10) : 1,
                              argc >= 4 ? (uint32_t)std::strtoul(argv[3], nullptr, 10) : 42, parse_pol(argc >= 5 ? argv[4] : "cautious"), nullptr);
        std::printf("%s\n", sim.chronicle().c_str());
        return 0;
    }
    if (argc >= 2 && !std::strcmp(argv[1], "lint")) return lint() == 0 ? 0 : 1;

    // default: run the test suite
    std::printf("CyberHack engine tests\n----------------------\n");
    test_rng();
    test_world_invariants();
    test_termination();
    test_legends();
    test_chronicle();
    int bad = lint();
    std::printf("----------------------\n%d checks, %d failures, %d lint problems\n", g_checks, g_fail, bad);
    return (g_fail || bad) ? 1 : 0;
}
