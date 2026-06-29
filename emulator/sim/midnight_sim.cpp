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

int main(int argc, char** argv) {
    if (argc >= 2 && !std::strcmp(argv[1], "world")) {
        dump_world(argc >= 3 ? (uint32_t)std::strtoul(argv[2], nullptr, 10) : 1);
        return 0;
    }
    std::printf("Midnight City — Phase 1 substrate tests\n\n");
    test_rng();
    test_determinism();
    test_worldgen_invariants();
    test_roundtrip();
    std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
