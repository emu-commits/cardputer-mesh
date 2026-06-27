// CyberHack — the app shell over the cyber:: engine (docs/CYBERHACK.md, Phase 8).
// Hybrid view: a net-graph "console" you watch your daemon crawl, dropping into a
// decision card for each multi-round ICE siege, then a Gibson-voice chronicle at
// the end. The engine is pure/headless and proven by sim/cyberhack_sim; this file
// is only rendering + input + the HumanPolicy (you, answering the decision cards).
#include "apps/apps.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include "core/clipboard.h"
#include "core/cyberhack_world.h"
#include "core/persist.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;
namespace cy = cyber;

namespace apps {

class CyberHack : public App {
    enum View { START, CRAWL, CARD, OVER };
    static constexpr uint32_t STEP_MS = 1500;  // "watch it crawl" cadence — slow enough to read stats + log
public:
    void on_create(AppContext& ctx) override {
        load_legends(ctx);
        view_ = START; sel_ = 0;
    }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        switch (view_) {
            case START: return key_start(ctx, k);
            case CRAWL: return key_crawl(ctx, k);
            case CARD:  return key_card(ctx, k);
            case OVER:  return key_over(ctx, k);
        }
        return false;
    }

    void tick(AppContext& ctx) override {
        if (view_ != CRAWL) return;
        if (ctx.keep_awake) ctx.keep_awake();              // hold the CYD awake through the no-keypress crawl
        if (ctx.now_ms - last_step_ < STEP_MS) return;
        last_step_ = ctx.now_ms;
        cy::AdvanceResult ar = sim_.advance();
        if (ar == cy::AR_DECISION) { view_ = CARD; sel_ = 0; }
        else if (ar == cy::AR_ENDED) end_run(ctx);
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        switch (view_) {
            case START: render_start(c); break;
            case CRAWL: render_crawl(ctx, c); break;
            case CARD:  render_crawl(ctx, c); render_card(c); break;
            case OVER:  render_over(c); break;
        }
    }

    std::vector<Command> commands(AppContext& ctx) override {
        std::vector<Command> v;
        if (view_ == OVER) v.push_back({"Copy chronicle", [this](AppContext& c){ if (c.clip) c.clip->set(chron_); }});
        if (view_ != START) v.push_back({"Abandon run (new)", [this](AppContext&){ view_ = START; sel_ = 0; }});
        return v;
    }

private:
    // ---- START: pick a personality, then jack in ---------------------------
    static const char* pers_name(int p) {
        static const char* n[] = {"Reckless", "Cautious", "Opportunist", "Loyalist"}; return n[p];
    }
    static const char* pers_desc(int p) {
        static const char* d[] = {
            "Push through with raw Spike. Big scores, big heat, short life.",
            "Spend the right tool. Survive, extract, settle for less.",
            "Detour for loot, fight smart. Rich runs, hotter trails.",
            "Careful, and never sells out a friendly faction.",
        };
        return d[p];
    }
    bool key_start(AppContext& ctx, const KeyEvent& k) {
        if (k.key == Key::Up)   { sel_ = (sel_ + cy::P_COUNT - 1) % cy::P_COUNT; return true; }
        if (k.key == Key::Down) { sel_ = (sel_ + 1) % cy::P_COUNT; return true; }
        if (k.key == Key::Enter || k.key == Key::Right) { start_run(ctx, (uint8_t)sel_); return true; }
        return false;   // Esc bubbles to launcher
    }
    void render_start(TextCanvas& c) {
        int top = ui::header(c, "CyberHack", ui::BrightGreen, "jack in");
        c.text(top, 2, "Choose your decker profile:", ui::Gray, ui::Black);
        for (int p = 0; p < cy::P_COUNT; ++p) {
            bool s = (p == sel_);
            c.text(top + 2 + p, 2, ui::fit(std::string(s ? "> " : "  ") + pers_name(p), 16),
                   s ? ui::BrightWhite : ui::White, ui::Black, s ? ui::ATTR_INVERSE : ui::ATTR_NONE);
        }
        // selected description, wrapped under the list
        int dr = top + 2 + cy::P_COUNT + 1;
        for (auto& ln : ui::wrap_text(pers_desc(sel_), c.width() - 4))
            { if (dr > ui::body_bottom(c)) break; c.text(dr++, 2, ln, ui::BrightCyan, ui::Black); }
        // carry-over: the net remembers across runs even though the deck resets.
        if (legends_.run_count) {
            int mr = ui::body_bottom(c) - 3; if (mr < dr + 1) mr = dr + 1;
            char b[64]; std::snprintf(b, sizeof b, "net memory: %d runs, best %u",
                                      legends_.run_count, (unsigned)legends_.best_score);
            c.text(mr, 2, ui::fit(b, c.width() - 3), ui::Yellow, ui::Black);
            std::string fs;
            for (int f = 0; f < cy::F_COUNT; ++f) if (legends_.grudge[f]) {
                char t[28]; std::snprintf(t, sizeof t, "%s%+d  ", cy::faction_short((uint8_t)f), (int)legends_.grudge[f]);
                fs += t;
            }
            if (!fs.empty()) c.text(mr + 1, 2, ui::fit(std::string("standing: ") + fs, c.width() - 3), ui::Gray, ui::Black);
            std::string allies, fallen;
            for (int i = 0; i < legends_.named_count; ++i) {
                const char* nm = cy::named_name(legends_.named[i].name_id);
                if (legends_.named[i].status == cy::NS_ALLIED) { if (!allies.empty()) allies += ", "; allies += nm; }
                else { if (!fallen.empty()) fallen += ", "; fallen += nm; }
            }
            if (!allies.empty()) c.text(mr + 2, 2, ui::fit(std::string("allies: ") + allies, c.width() - 3), ui::BrightCyan, ui::Black);
            if (!fallen.empty()) c.text(mr + 3, 2, ui::fit(std::string("burned: ") + fallen, c.width() - 3), ui::BrightRed, ui::Black);
        }
        ui::footer(c, " up/dn profile   enter: jack in   esc: back ");
    }

    void start_run(AppContext& ctx, uint8_t pers) {
        uint32_t cnet = legends_.citynet_seed ? legends_.citynet_seed : ((ctx.now_ms | 1u));
        legends_.citynet_seed = cnet;
        uint32_t rseed = ((uint32_t)ctx.now_ms * 2654435761u) ^ (legends_.run_count * 40503u) ^ 0x51A5u;
        if (!rseed) rseed = 1;
        sim_.start(cnet, rseed, pers, &legends_);
        view_ = CRAWL; last_step_ = ctx.now_ms; room_sig_ = 0xFFFFFFFFu;
    }

    // ---- CRAWL: the net-graph console you watch ----------------------------
    bool key_crawl(AppContext& ctx, const KeyEvent& k) {
        if (k.is_char() && (k.ch == ' ')) { last_step_ = 0; return true; }  // nudge a step
        if (k.is_char() && (k.ch == 'j' || k.ch == 'J')) {                  // pull the plug, bank the haul
            sim_.jack_out();
            if (!sim_.running()) end_run(ctx);
            return true;
        }
        return false;   // Esc bubbles
    }
    static char ice_glyph(uint8_t ice) {
        static const char g[] = { 'B', 'T', 'W', 'S', 'w', 'Y' };
        return ice < cy::I_COUNT ? g[ice] : '#';
    }
    // Overlay corruption noise on the room view: replace a fraction of the drawn
    // glyphs (skip empty void) with glitch characters in a sick magenta/red. The
    // fraction tracks corruption%; the pattern is seeded by a ~110ms time bucket so
    // it flickers without re-diffing every 33ms frame.
    static void apply_glitch(TextCanvas& c, int r0, int r1, uint32_t now, int corr) {
        if (corr <= 0) return;
        static const char G[] = "#%&*?/\\=+~^@$01";
        int density = corr / 4; if (density > 30) density = 30;   // cap so it stays readable
        uint32_t bucket = now / 110;
        for (int r = r0; r <= r1 && r < c.height(); ++r)
            for (int x = 0; x < c.width(); ++x) {
                uint32_t h = bucket * 2654435761u ^ (uint32_t)(r * 73856093) ^ (uint32_t)(x * 19349663);
                h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                if ((int)(h % 100) >= density) continue;
                if (c.at(r, x).cp == U' ') continue;             // keep the dark void dark
                char g = G[h % (sizeof(G) - 1)];
                c.put(r, x, (char32_t)g, (h & 1) ? ui::BrightMagenta : ui::BrightRed, ui::Black);
            }
    }
    static const char* ice_short(uint8_t ice) {
        static const char* s[] = { "BLACK", "TRACE", "WARDEN", "SWARM", "WATCH", "SYSOP" };
        return ice < cy::I_COUNT ? s[ice] : "ICE";
    }
    // exits under the room: each usable neighbour labelled + colour-coded so you
    // can see DANGER ahead and route around it (the door toward the objective is bold).
    void draw_exits(TextCanvas& c, int row) {
        const cy::World& w = sim_.world();
        const cy::RunState& r = sim_.state();
        const cy::Node& here = w.nodes[r.pos];
        int nexthop = sim_.next_hop_to_objective();
        int col = 1;
        c.text(row, col, "exits:", ui::Gray, ui::Black); col += 7;
        int shown = 0;
        for (int k = 0; k < here.deg && shown < 3; ++k) {
            uint8_t nb = here.nbr[k];
            if (nb == cy::NONE8) continue;
            const cy::Node& nn = w.nodes[nb];
            const char* tag; uint8_t fg;
            if (nn.guard_named != cy::NONE8 && !(nn.flags & cy::NF_GUARD_DONE)) { tag = "danger"; fg = ui::BrightRed; }
            else if (nn.type == cy::N_SHRINE) { tag = "recov"; fg = ui::BrightCyan; }
            else if (nn.shards > 0 && !(nn.flags & cy::NF_LOOTED)) { tag = "loot"; fg = ui::BrightYellow; }
            else { tag = "quiet"; fg = ui::White; }
            char b[28];
            std::snprintf(b, sizeof b, "%s%s s%d %s", nb == nexthop ? ">" : " ",
                          cy::node_label(w, nb).c_str(), (int)nn.security, tag);
            int len = (int)std::strlen(b);
            if (col + len > c.width() - 1) break;
            c.text(row, col, b, fg, ui::Black, nb == nexthop ? ui::ATTR_BOLD : ui::ATTR_NONE);
            col += len + 1; ++shown;
        }
    }
    // ---- a NetHack-style lit room (open . floor, walls, a down-stair) themed as a
    // cyberspace construct; @ auto-tours it 4-directionally: in via the access port,
    // over to the data ($), up to the ICE to break it, then down the stair. ---------
    bool blocked_cell(int x, int y) const {
        return room_wall_[y][x] || (ice_x_ >= 0 && x == ice_x_ && y == ice_y_);
    }
    bool bfs_seg(int sx, int sy, int gx, int gy, uint8_t* ox, uint8_t* oy, int& olen) {
        const int W = room_W_, H = room_H_, N = W * H;
        static int16_t prev[64 * 16], q[64 * 16], tmp[64 * 16];
        for (int i = 0; i < N; ++i) prev[i] = -2;
        int qh = 0, qt = 0, s = sy * W + sx, goal = gy * W + gx;
        prev[s] = -1; q[qt++] = (int16_t)s;
        const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
        while (qh < qt) {
            int cur = q[qh++], cx = cur % W, cyy = cur / W;
            if (cur == goal) break;
            for (int d = 0; d < 4; ++d) {
                int nx = cx + dx[d], ny = cyy + dy[d];
                if (nx < 0 || nx >= W || ny < 0 || ny >= H) continue;
                int nk = ny * W + nx;
                if (prev[nk] != -2) continue;
                if (blocked_cell(nx, ny) && nk != goal) continue;
                prev[nk] = (int16_t)cur; q[qt++] = (int16_t)nk;
            }
        }
        if (prev[goal] == -2) return false;
        int n = 0;
        for (int cc = goal; cc != -1; cc = prev[cc]) tmp[n++] = (int16_t)cc;
        olen = n;
        for (int i = 0; i < n; ++i) { int cc = tmp[n - 1 - i]; ox[i] = (uint8_t)(cc % W); oy[i] = (uint8_t)(cc / W); }
        return true;
    }
    void append_seg(int sx, int sy, int gx, int gy) {
        static uint8_t sgx[64 * 16], sgy[64 * 16]; int sl = 0;
        if (!bfs_seg(sx, sy, gx, gy, sgx, sgy, sl)) return;
        int start = (path_len_ > 0) ? 1 : 0;                 // skip the duplicated joint cell
        for (int i = start; i < sl && path_len_ < (int)sizeof(path_x_); ++i) {
            path_x_[path_len_] = sgx[i]; path_y_[path_len_] = sgy[i]; ++path_len_;
        }
    }
    bool ice_adj(int& ax, int& ay) const {
        const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
        for (int d = 0; d < 4; ++d) { int nx = ice_x_ + dx[d], ny = ice_y_ + dy[d];
            if (nx >= 0 && nx < room_W_ && ny >= 0 && ny < room_H_ && !blocked_cell(nx, ny)) { ax = nx; ay = ny; return true; } }
        return false;
    }
    void build_room(const cy::World& w, const cy::RunState& r, int AW, int AH, int sx, int sy) {
        const cy::Node& here = w.nodes[r.pos];
        uint32_t h = w.run_seed ^ ((uint32_t)r.pos * 73856093u) ^ (((uint32_t)r.depth + 1) * 19349663u)
                     ^ (((uint32_t)here.security + 1) * 83492791u);
        auto rnd = [&](int n) { h = h * 1664525u + 1013904223u; return (int)((h >> 16) % (uint32_t)(n < 1 ? 1 : n)); };
        int rh = AH - 2; if (rh < 5) rh = 5; if (rh > 14) rh = 14;
        int rw = AW - 12; if (rw < 14) rw = 14; if (rw > 46) rw = 46;
        room_W_ = rw; room_H_ = rh; room_ox_ = (AW - rw) / 2; room_oy_ = 1;
        for (int y = 0; y < rh; ++y) for (int x = 0; x < rw; ++x)
            room_wall_[y][x] = (x == 0 || x == rw - 1 || y == 0 || y == rh - 1);   // border walls, open floor
        door_w_ = 1 + rnd(rh - 2);
        door_e_ = 1 + rnd(rh - 2);
        room_wall_[door_w_][0] = false;                       // access ports (doorways)
        room_wall_[door_e_][rw - 1] = false;
        stairs_x_ = rw - 2; stairs_y_ = door_e_;              // the down-stair (jack deeper)
        bool guarded = !(here.flags & cy::NF_GUARD_DONE) && here.guard_ice < cy::I_COUNT;
        ice_x_ = ice_y_ = loot_x_ = loot_y_ = shrine_x_ = shrine_y_ = -1;
        if (guarded) { ice_x_ = rw / 2 + rnd((rw - 2) / 3); if (ice_x_ > rw - 2) ice_x_ = rw - 2; ice_y_ = 1 + rnd(rh - 2); }
        if (here.shards > 0 && !(here.flags & cy::NF_LOOTED)) { loot_x_ = 1 + rnd((rw - 2) / 2); loot_y_ = 1 + rnd(rh - 2); }
        if (here.type == cy::N_SHRINE) { shrine_x_ = rw / 2; shrine_y_ = rh / 2; }
        for (int i = 0; i < 3; ++i) { mote_x_[i] = 1 + rnd(rw - 2); mote_y_[i] = 1 + rnd(rh - 2); }
        // tour path: enter just inside the west port -> data -> ICE (or -> stair)
        int ex = (sx >= 1 && sx < rw - 1) ? sx : 1, ey = (sy >= 1 && sy < rh - 1) ? sy : door_w_;
        path_len_ = 0; int cx = ex, cy2 = ey;
        if (loot_x_ >= 0) { append_seg(cx, cy2, loot_x_, loot_y_); if (path_len_) { cx = path_x_[path_len_ - 1]; cy2 = path_y_[path_len_ - 1]; } }
        if (guarded) { int ax, ay; if (ice_adj(ax, ay)) append_seg(cx, cy2, ax, ay); }
        else append_seg(cx, cy2, stairs_x_, stairs_y_);
        if (path_len_ == 0) { path_x_[0] = (uint8_t)ex; path_y_[0] = (uint8_t)ey; path_len_ = 1; }
    }

    // The CYD console: a NetHack-style CURRENT-ROOM view. You watch @ auto-navigate
    // the room (around walls / the ICE) toward the exit; the room flashes when you
    // cross into the next, and the exits are labelled so the graphic is never hidden.
    void render_crawl(AppContext& ctx, TextCanvas& c) {
        const cy::World& w = sim_.world();
        const cy::RunState& r = sim_.state();
        const cy::Node& here = w.nodes[r.pos];
        char hd[24]; std::snprintf(hd, sizeof hd, "heat %d  T%d", (int)r.heat, (int)r.tier);
        int top = ui::header(c, "CyberHack :: NIGHT-NET", ui::BrightGreen, hd);
        // ribbon: layer / room type / security  +  objective marker
        char rb[40]; std::snprintf(rb, sizeof rb, "L%d  %s  sec%d",
                                   (int)r.depth + 1, cy::node_type_name(here.type), (int)here.security);
        c.text(top, 1, rb, ui::BrightCyan, ui::Black);
        { std::string ob = std::string("obj>") + cy::node_label(w, w.objective.target);
          c.text(top, c.width() - 1 - (int)ob.size(), ob, ui::BrightYellow, ui::Black); }
        // (re)generate the room layout + path when we cross into a new node, or when
        // the guard clears / a fight starts — @ then re-routes from where it stands.
        const int play_top = top + 1, AH = 10;               // play area: rows play_top..play_top+9
        bool guarded = !(here.flags & cy::NF_GUARD_DONE) && here.guard_ice < cy::I_COUNT;
        bool named   = here.guard_named != cy::NONE8 && !(here.flags & cy::NF_GUARD_DONE);
        uint32_t sig = ((uint32_t)r.pos << 2) | (guarded ? 2u : 0u) | (r.in_fight ? 1u : 0u);
        if (sig != room_sig_) {
            int sx = -1, sy = -1;
            if ((room_sig_ >> 2) == (uint32_t)r.pos) { sx = at_x_; sy = at_y_; }  // same node: continue
            else wipe_t0_ = ctx.now_ms;                                            // new node: flash
            room_sig_ = sig; last_room_t0_ = ctx.now_ms;
            build_room(w, r, c.width(), AH, sx, sy);
        }
        bool wiping = (ctx.now_ms - wipe_t0_) < 160;
        const int rtop = play_top + room_oy_, rleft = room_ox_, rw = room_W_, rh = room_H_;
        uint8_t wallc = wiping ? ui::BrightWhite : ui::Cyan;
        int nexthop = sim_.next_hop_to_objective();
        // corridors out into the dark, west (in) and east (deeper)
        for (int x = 0; x < rleft; ++x)             c.put(rtop + door_w_, x, U'#', ui::Gray, ui::Black);
        for (int x = rleft + rw; x < c.width(); ++x) c.put(rtop + door_e_, x, U'#', ui::Gray, ui::Black);
        // room: border walls + open lit floor (the matrix grid)
        for (int y = 0; y < rh; ++y) for (int x = 0; x < rw; ++x) {
            if (room_wall_[y][x]) c.put(rtop + y, rleft + x, (y == 0 || y == rh - 1) ? U'-' : U'|', wallc, ui::Black);
            else                  c.put(rtop + y, rleft + x, U'.', ui::Green, ui::Black);
        }
        c.put(rtop + door_w_, rleft + 0,      U'+', ui::BrightCyan, ui::Black);   // access ports
        c.put(rtop + door_e_, rleft + rw - 1, U'+', ui::BrightCyan, ui::Black);
        for (int i = 0; i < 3; ++i) if (!room_wall_[mote_y_[i]][mote_x_[i]])
            c.put(rtop + mote_y_[i], rleft + mote_x_[i], U':', ui::Green, ui::Black);
        if (nexthop >= 0) c.put(rtop + stairs_y_, rleft + stairs_x_, U'>', ui::BrightCyan, ui::Black, ui::ATTR_BOLD);
        if (loot_x_ >= 0 && !(here.flags & cy::NF_LOOTED))
            c.put(rtop + loot_y_, rleft + loot_x_, U'$', ui::BrightYellow, ui::Black, ui::ATTR_BOLD);
        if (shrine_x_ >= 0) c.put(rtop + shrine_y_, rleft + shrine_x_, U'&', ui::BrightCyan, ui::Black);
        if (ice_x_ >= 0)    c.put(rtop + ice_y_,    rleft + ice_x_, (char32_t)ice_glyph(here.guard_ice),
                                  named ? ui::BrightMagenta : ui::BrightRed, ui::Black, ui::ATTR_BOLD);
        // @ steps cell-by-cell along the tour (around walls / up to the ICE)
        if (path_len_ > 0) {
            int idx;
            if (r.in_fight) idx = path_len_ - 1;
            else { float f = (float)((int32_t)ctx.now_ms - (int32_t)last_room_t0_) / 1300.0f;
                   if (f < 0) f = 0;
                   if (f > 1) f = 1;
                   idx = (int)(f * (path_len_ - 1)); }
            if (idx < 0) idx = 0;
            if (idx > path_len_ - 1) idx = path_len_ - 1;
            at_x_ = path_x_[idx]; at_y_ = path_y_[idx];
            c.put(rtop + at_y_, rleft + at_x_, U'@', ui::BrightWhite, ui::Black, ui::ATTR_BOLD);
            if (loot_x_ >= 0 && at_x_ == loot_x_ && at_y_ == loot_y_)
                sim_.collect_current_node();                     // walked onto the cache: bank it
        }
        // corruption glitch: the deck's own rot eats into the picture. Density scales
        // with corruption%; the pattern shifts a few times a second (seeded by a time
        // bucket so it's stable between buckets = no needless diff churn to the CYD).
        apply_glitch(c, play_top, play_top + AH - 1, ctx.now_ms, (int)r.corruption);
        // exits + legend
        int exr = play_top + AH;
        draw_exits(c, exr);
        c.text(exr + 1, 1, ui::fit("@you .grid >jack-deeper + port $data &cache  ICE:B/T/W/S/Y", c.width() - 2), ui::Gray, ui::Black);
        // siege bar (auto-combat) then the log, down to the status line
        int logtop = exr + 2;
        if (r.in_fight) {
            int barw = 16; int filled = r.ice_hp_max > 0 ? r.ice_hp * barw / r.ice_hp_max : 0;
            std::string bar = "breaking ICE [";
            for (int i = 0; i < barw; ++i) bar += (i < filled ? '#' : '-');
            char hp[24]; std::snprintf(hp, sizeof hp, "] %d/%d", (int)r.ice_hp, (int)r.ice_hp_max);
            c.text(logtop, 1, ui::fit(bar + hp, c.width() - 2), ui::BrightRed, ui::Black);
            logtop += 1;
        }
        const auto& log = sim_.log();
        int rows = (c.height() - 3) - logtop + 1;
        int start = (int)log.size() - rows; if (start < 0) start = 0;
        for (int i = start, rr = logtop; i < (int)log.size() && rr <= c.height() - 3; ++i, ++rr)
            c.text(rr, 1, ui::fit(log[i], c.width() - 2), i == (int)log.size() - 1 ? ui::White : ui::Gray, ui::Black);
        render_status(c);
        ui::footer(c, " watch the dive   j: jack out   space: step   esc: leave ");
    }
    void render_status(TextCanvas& c) {
        const cy::RunState& r = sim_.state();
        char b[112];
        std::snprintf(b, sizeof b, " Intg %d/%d Buf %d/%d Heat %d Cor %d%% T%d L%d obj%d $%d ",
                      (int)r.integrity, (int)r.integrity_max, (int)r.buffer, (int)r.buffer_max,
                      (int)r.heat, (int)r.corruption, (int)r.tier, (int)r.depth + 1,
                      (int)r.objectives_done, (int)r.shards);
        c.text(c.height() - 2, 0, ui::fit(b, c.width()), ui::Black, ui::BrightGreen);
    }

    // ---- CARD: a decision (incl. each round of an ICE siege) ---------------
    bool key_card(AppContext& ctx, const KeyEvent& k) {
        const cy::Decision& d = sim_.decision();
        int n = (int)d.options.size();
        if (n <= 0) { view_ = CRAWL; return true; }
        if (k.key == Key::Up)   { sel_ = (sel_ + n - 1) % n; return true; }
        if (k.key == Key::Down) { sel_ = (sel_ + 1) % n; return true; }
        if (k.is_char() && k.ch >= '1' && k.ch < '1' + n) { sel_ = (int)(k.ch - '1'); choose_and_resume(ctx); return true; }
        if (k.key == Key::Enter) { choose_and_resume(ctx); return true; }
        return true;   // capture everything while a card is up
    }
    void choose_and_resume(AppContext& ctx) {
        sim_.choose(sel_); sel_ = 0;
        if (!sim_.running()) { end_run(ctx); return; }
        if (sim_.needs_decision()) { view_ = CARD; }        // next siege round / next decision
        else { view_ = CRAWL; last_step_ = ctx.now_ms; }
    }
    // A decision (route / extraction / survival — combat auto-resolves and never
    // reaches here). Docked as a panel ABOVE the status line so the map + status
    // stay visible the whole time; never a full-screen modal.
    void render_card(TextCanvas& c) {
        const cy::Decision& d = sim_.decision();
        int n = (int)d.options.size();
        const char* title = d.kind == cy::DK_DIVE ? " DESCENT " : d.kind == cy::DK_EXTRACT ? " EXTRACTION "
                          : d.kind == cy::DK_SURVIVAL ? " SURVIVAL " : d.kind == cy::DK_PARLEY ? " PARLEY "
                          : d.kind == cy::DK_MEMORY ? " MEMORY DIVE " : " ROUTE ";
        uint8_t accent = d.kind == cy::DK_SURVIVAL ? ui::BrightRed : d.kind == cy::DK_DIVE ? ui::BrightGreen
                       : d.kind == cy::DK_PARLEY ? ui::BrightCyan : d.kind == cy::DK_MEMORY ? ui::BrightMagenta : ui::BrightYellow;
        int bottom = c.height() - 3;                       // leave status (h-2) + footer (h-1)
        auto pl = ui::wrap_text(d.prompt, c.width() - 2);
        int pln = (int)pl.size(); if (pln > 2) pln = 2;
        int h = 1 + pln + n;                               // title + prompt + options
        int topr = bottom - h + 1; if (topr < 3) topr = 3;
        c.fill_rect(topr - 1, 0, h + 1, c.width(), U' ', ui::White, ui::Black);
        c.hline(topr - 1, 0, c.width(), U'=', accent, ui::Black);
        c.text(topr - 1, 2, title, accent, ui::Black, ui::ATTR_BOLD);
        int row = topr;
        for (int i = 0; i < pln; ++i) c.text(row++, 1, ui::fit(pl[i], c.width() - 2), ui::White, ui::Black);
        for (int i = 0; i < n && row <= bottom; ++i, ++row) {
            bool s = (i == sel_);
            char line[48]; std::snprintf(line, sizeof line, "%d %s", i + 1, d.options[i].c_str());
            c.text(row, 1, ui::fit(line, c.width() - 2), s ? ui::BrightWhite : ui::White, ui::Black, s ? ui::ATTR_INVERSE : ui::ATTR_NONE);
        }
        render_status(c);                                  // keep status visible under the panel
        ui::footer(c, " up/dn pick   enter/1-5 choose   esc:leave ");
    }

    // ---- OVER: the chronicle -----------------------------------------------
    void end_run(AppContext& ctx) {
        sim_.update_legends(legends_);
        save_legends(ctx);
        chron_ = sim_.chronicle();
        view_ = OVER; over_scroll_ = 0;
    }
    bool key_over(AppContext& ctx, const KeyEvent& k) {
        if (k.key == Key::Up)   { if (over_scroll_ > 0) over_scroll_--; return true; }
        if (k.key == Key::Down) { over_scroll_++; return true; }
        if (k.is_char() && (k.ch == 'c' || k.ch == 'C')) { if (ctx.clip) ctx.clip->set(chron_); copied_ = true; return true; }
        if (k.key == Key::Enter) { view_ = START; sel_ = 0; copied_ = false; return true; }  // new run
        return false;   // Esc bubbles to launcher
    }
    void render_over(TextCanvas& c) {
        const cy::RunState& r = sim_.state();
        bool won = r.outcome == cy::O_EXTRACTED;
        int top = ui::header(c, won ? "JACKED OUT" : "FLATLINED", won ? ui::BrightGreen : ui::BrightRed,
                             copied_ ? "copied!" : "");
        auto lines = ui::wrap_text(chron_, c.width() - 2);
        int rows = ui::body_bottom(c) - top + 1;
        if (over_scroll_ > (int)lines.size() - 1) over_scroll_ = (int)lines.size() > 0 ? (int)lines.size() - 1 : 0;
        for (int i = 0; i < rows; ++i) {
            int li = over_scroll_ + i; if (li >= (int)lines.size()) break;
            bool stat = !lines[li].empty() && lines[li][0] == '[';
            c.text(top + i, 1, lines[li], stat ? ui::BrightYellow : ui::White, ui::Black);
        }
        ui::footer(c, " up/dn scroll   c: copy   enter: new run   esc: leave ");
    }

    // ---- persistence: the legends blob (world memory) ----------------------
    void load_legends(AppContext& ctx) {
        if (!ctx.state) return;
        std::string s = ctx.state->get("cyberhack.legends", "");
        if (!s.empty()) cy::legends_deserialize(s, legends_);
    }
    void save_legends(AppContext& ctx) {
        if (!ctx.state) return;
        std::string s; cy::legends_serialize(legends_, s);
        ctx.state->set("cyberhack.legends", s); ctx.state->flush();
    }

    cy::Sim sim_;
    cy::Legends legends_;
    View view_ = START;
    int sel_ = 0;
    uint32_t last_step_ = 0;
    uint32_t wipe_t0_ = 0;        // when the last room transition started (for the flash)
    // room-view (NetHack-style room + 4-dir pathfinding) state
    bool room_wall_[16][64] = {};
    uint8_t path_x_[1024] = {}, path_y_[1024] = {};
    int path_len_ = 0, room_W_ = 0, room_H_ = 0;
    int room_ox_ = 0, room_oy_ = 1, door_w_ = 1, door_e_ = 1, stairs_x_ = 0, stairs_y_ = 0;
    int ice_x_ = -1, ice_y_ = -1, loot_x_ = -1, loot_y_ = -1, shrine_x_ = -1, shrine_y_ = -1;
    int mote_x_[3] = {-1, -1, -1}, mote_y_[3] = {0, 0, 0};
    int at_x_ = 0, at_y_ = 0;
    uint32_t room_sig_ = 0xFFFFFFFFu, last_room_t0_ = 0;
    std::string chron_;
    int over_scroll_ = 0;
    bool copied_ = false;
};

std::unique_ptr<App> make_cyberhack() { return std::make_unique<CyberHack>(); }

} // namespace apps
