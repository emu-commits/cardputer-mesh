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
        if (legends_.run_count)
            { char b[48]; std::snprintf(b, sizeof b, "net memory: %d runs, best %u", legends_.run_count, (unsigned)legends_.best_score);
              c.text(ui::body_bottom(c), 2, b, ui::Yellow, ui::Black); }
        ui::footer(c, " up/dn profile   enter: jack in   esc: back ");
    }

    void start_run(AppContext& ctx, uint8_t pers) {
        uint32_t cnet = legends_.citynet_seed ? legends_.citynet_seed : ((ctx.now_ms | 1u));
        legends_.citynet_seed = cnet;
        uint32_t rseed = ((uint32_t)ctx.now_ms * 2654435761u) ^ (legends_.run_count * 40503u) ^ 0x51A5u;
        if (!rseed) rseed = 1;
        sim_.start(cnet, rseed, pers, &legends_);
        view_ = CRAWL; last_step_ = ctx.now_ms; last_pos_ = 0xFF;
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
    // The CYD console: a NetHack-style CURRENT-ROOM view. You watch @ walk across
    // the room toward the exit, the room flashes when you cross into the next, and
    // the exits are labelled so the navigation graphic is never obscured.
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
        // crossed into a new room → brief wipe flash on the walls + restart the walk
        if (r.pos != last_pos_) { last_pos_ = r.pos; wipe_t0_ = ctx.now_ms; }
        bool wiping = (ctx.now_ms - wipe_t0_) < 160;
        // the room box (a wide rectangle filling the CYD width)
        int boxr = top + 1, boxH = 8, W = c.width();
        c.draw_box(boxr, 0, boxH, W, wiping ? ui::BrightWhite : ui::Gray, ui::Black);
        int mid = boxr + boxH / 2;
        int nexthop = sim_.next_hop_to_objective();
        if (nexthop >= 0) c.put(mid, W - 1, U'>', ui::BrightCyan, ui::Black);   // door toward objective
        // room contents
        bool guarded = !(here.flags & cy::NF_GUARD_DONE) && here.guard_ice < cy::I_COUNT;
        bool named   = here.guard_named != cy::NONE8 && !(here.flags & cy::NF_GUARD_DONE);
        int icex = W - 7;
        if (here.shards > 0 && !(here.flags & cy::NF_LOOTED)) c.put(mid + 1, 4, U'$', ui::BrightYellow, ui::Black);
        if (here.type == cy::N_SHRINE) c.put(mid - 1, W / 2, U'&', ui::BrightCyan, ui::Black);
        if (guarded) c.put(mid - 1, icex, (char32_t)ice_glyph(here.guard_ice),
                           named ? ui::BrightMagenta : ui::Gray, ui::Black, named ? ui::ATTR_BOLD : ui::ATTR_NONE);
        // @ walks toward the door between ticks; in a fight it stands off the ICE
        float f = (float)((int32_t)ctx.now_ms - (int32_t)last_step_) / 1100.0f;
        if (f < 0) f = 0;
        if (f > 1) f = 1;
        int px = r.in_fight ? (guarded ? icex - 2 : W - 5) : 2 + (int)(f * (W - 6));
        if (px < 2) px = 2;
        if (px > W - 2) px = W - 2;
        if (!r.in_fight) for (int x = 2; x < px; x += 2) c.put(mid, x, U'.', ui::Gray, ui::Black);
        c.put(mid, px, U'@', ui::BrightWhite, ui::Black, ui::ATTR_BOLD);
        // exits + legend
        int exr = boxr + boxH;
        draw_exits(c, exr);
        c.text(exr + 1, 1, ui::fit("@ you  $ loot  &shrine  letters=ICE  >exit", c.width() - 2), ui::Gray, ui::Black);
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
        char b[80];
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
                          : d.kind == cy::DK_SURVIVAL ? " SURVIVAL " : " ROUTE ";
        uint8_t accent = d.kind == cy::DK_SURVIVAL ? ui::BrightRed : d.kind == cy::DK_DIVE ? ui::BrightGreen : ui::BrightYellow;
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
        ui::footer(c, " up/dn pick   enter/1-4 choose   esc:leave ");
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
    uint8_t last_pos_ = 0xFF;     // room-view: detect crossing into a new room (wipe + walk restart)
    uint32_t wipe_t0_ = 0;        // when the last room transition started (for the flash)
    std::string chron_;
    int over_scroll_ = 0;
    bool copied_ = false;
};

std::unique_ptr<App> make_cyberhack() { return std::make_unique<CyberHack>(); }

} // namespace apps
