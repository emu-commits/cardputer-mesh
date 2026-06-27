// CyberHack WASM glue — a THIN shell around the real cyber::Sim so the web build
// runs the exact same engine as the Cardputer (single source of truth). No game
// logic lives here: this only drives Sim and serializes its state to JSON for the
// JS/HTML renderer, reusing the engine's own legends_serialize/deserialize for
// save data. Built by web/build.sh into cyberhack.js + cyberhack.wasm.
#include "core/cyberhack_world.h"
#include <emscripten.h>
#include <string>

using namespace cyber;

static Sim       g_sim;
static Legends   g_leg;
static std::string g_buf;   // backing store for returned C strings

static void jesc(std::string& o, const std::string& s) {
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += ' ';
        else if ((unsigned char)c < 0x20) o += ' ';
        else o += c;
    }
}

static std::string state_json() {
    const RunState& r = g_sim.state();
    const World& w = g_sim.world();
    std::string o; o.reserve(4096);
    char b[256];
    std::snprintf(b, sizeof b,
        "{\"r\":{\"int\":%d,\"intmax\":%d,\"buf\":%d,\"bufmax\":%d,\"heat\":%d,\"cor\":%d,"
        "\"tier\":%d,\"sh\":%d,\"pos\":%d,\"out\":%d,\"dc\":%d,\"fight\":%d,\"ihp\":%d,\"ihpmax\":%d,"
        "\"objdone\":%d,\"hunt\":%d,\"hpos\":%d,\"score\":%d,\"depth\":%d,\"objs\":%d},",
        r.integrity, r.integrity_max, r.buffer, r.buffer_max, r.heat, r.corruption,
        r.tier, r.shield, r.pos, r.outcome, r.death_cause, r.in_fight ? 1 : 0, r.ice_hp, r.ice_hp_max,
        r.objective_done ? 1 : 0, r.hunt_active ? 1 : 0, r.hunt_active ? r.hunter_pos : -1, g_sim.score(),
        r.depth, r.objectives_done);
    o += b;
    int nexthop = g_sim.next_hop_to_objective();
    std::snprintf(b, sizeof b, "\"obj\":%d,\"entry\":%d,\"nc\":%d,\"nexthop\":%d,", w.objective.target, w.entry, w.node_count, nexthop);
    o += b;
    // nodes: [x,y,type,visited,named-guard-pending,name_pre,sec,loot,guardice,guarddone]
    o += "\"nodes\":[";
    for (int i = 0; i < w.node_count; ++i) {
        const Node& n = w.nodes[i];
        int named = (n.guard_named != NONE8 && !(n.flags & NF_GUARD_DONE)) ? 1 : 0;
        int loot  = (n.shards > 0 && !(n.flags & NF_LOOTED)) ? 1 : 0;
        int guard = (!(n.flags & NF_GUARD_DONE) && n.guard_ice < I_COUNT) ? (int)n.guard_ice : -1;
        std::snprintf(b, sizeof b, "%s[%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]", i ? "," : "",
                      n.x, n.y, n.type, (n.flags & NF_VISITED) ? 1 : 0, named, n.name_pre,
                      (int)n.security, loot, guard, (n.flags & NF_GUARD_DONE) ? 1 : 0);
        o += b;
    }
    o += "],";
    // adjacency: usable neighbors of each node (so the web can draw exits + scout ahead)
    o += "\"adj\":[";
    for (int i = 0; i < w.node_count; ++i) {
        const Node& n = w.nodes[i];
        o += i ? ",[" : "[";
        bool first = true;
        for (int k = 0; k < n.deg; ++k) {
            uint8_t nb = n.nbr[k];
            if (nb == NONE8) continue;
            std::snprintf(b, sizeof b, "%s%d", first ? "" : ",", (int)nb); o += b; first = false;
        }
        o += "]";
    }
    o += "],";
    // decision (if any)
    if (g_sim.needs_decision()) {
        const Decision& d = g_sim.decision();
        std::snprintf(b, sizeof b, "\"dec\":{\"kind\":%d,\"prompt\":\"", d.kind); o += b;
        jesc(o, d.prompt); o += "\",\"opts\":[";
        for (size_t i = 0; i < d.options.size(); ++i) { o += i ? ",\"" : "\""; jesc(o, d.options[i]); o += "\""; }
        o += "]},";
    } else o += "\"dec\":null,";
    // recent log
    o += "\"log\":[";
    const auto& log = g_sim.log();
    int from = (int)log.size() - 14; if (from < 0) from = 0;
    for (int i = from; i < (int)log.size(); ++i) { o += i > from ? ",\"" : "\""; jesc(o, log[i]); o += "\""; }
    o += "]}";
    return o;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE void ch_start(unsigned cnet, unsigned seed, int pers, const char* leg) {
    g_leg = Legends{};
    bool have = false;
    if (leg && *leg) have = legends_deserialize(std::string(leg), g_leg);
    if (pers < 0 || pers >= P_COUNT) pers = P_CAUTIOUS;
    g_sim.start((uint32_t)cnet, (uint32_t)seed, (uint8_t)pers, have ? &g_leg : nullptr);
}
EMSCRIPTEN_KEEPALIVE int  ch_advance() { return (int)g_sim.advance(); }
EMSCRIPTEN_KEEPALIVE void ch_choose(int i) { g_sim.choose(i); }
EMSCRIPTEN_KEEPALIVE void ch_jack_out() { g_sim.jack_out(); }   // player pulls the plug
EMSCRIPTEN_KEEPALIVE int  ch_running() { return g_sim.running() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE const char* ch_state() { g_buf = state_json(); return g_buf.c_str(); }
EMSCRIPTEN_KEEPALIVE const char* ch_chronicle() { g_buf = g_sim.chronicle(); return g_buf.c_str(); }
// fold this run into the legends blob and return the engine's own CHV1 serialization
EMSCRIPTEN_KEEPALIVE const char* ch_legends() {
    g_sim.update_legends(g_leg);
    std::string s; legends_serialize(g_leg, s); g_buf = s; return g_buf.c_str();
}

} // extern "C"
