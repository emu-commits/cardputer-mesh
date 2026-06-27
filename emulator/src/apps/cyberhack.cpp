// CyberHack — the app shell over the cyber:: engine (docs/CYBERHACK.md, Phase 8).
// Hybrid view: a net-graph "console" you watch your daemon crawl, dropping into a
// decision card for each multi-round ICE siege, then a Gibson-voice chronicle at
// the end. The engine is pure/headless and proven by sim/cyberhack_sim; this file
// is only rendering + input + the HumanPolicy (you, answering the decision cards).
#include "apps/apps.h"
#include <cstdio>
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
    static constexpr uint32_t STEP_MS = 600;   // "watch it crawl" cadence
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
            case CRAWL: render_crawl(c); break;
            case CARD:  render_crawl(c); render_card(c); break;
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
        view_ = CRAWL; last_step_ = ctx.now_ms;
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
    char node_glyph(const cy::World& w, uint8_t i) const {
        const cy::Node& n = w.nodes[i];
        if (i == sim_.state().pos) return '@';
        if (sim_.state().hunt_active && i == sim_.state().hunter_pos) return 'H';
        if ((n.flags & cy::NF_OBJECTIVE) && !sim_.state().objective_done) return '*';
        if (n.guard_named != cy::NONE8 && !(n.flags & cy::NF_GUARD_DONE)) return '#';
        if (!(n.flags & cy::NF_VISITED)) return '?';
        switch (n.type) { case cy::N_VAULT: return 'V'; case cy::N_GANG: return 'G';
            case cy::N_ABANDONED: return 'x'; case cy::N_SHRINE: return 'A'; default: return '.'; }
    }
    void render_crawl(TextCanvas& c) {
        const cy::World& w = sim_.world();
        const cy::RunState& r = sim_.state();
        char hd[24]; std::snprintf(hd, sizeof hd, "heat %d  T%d", (int)r.heat, (int)r.tier);
        int top = ui::header(c, "CyberHack :: NIGHT-NET", ui::BrightGreen, hd);
        // map box (left), positioned by node x,y
        int map_h = 0;
        for (int i = 0; i < w.node_count; ++i) {
            int rr = top + w.nodes[i].y;
            int cc = 1 + w.nodes[i].x * 2;
            if (rr > ui::body_bottom(c) - 6 || cc >= c.width() - 14) continue;
            char g = node_glyph(w, i);
            uint8_t fg = g == '@' ? ui::BrightWhite : g == '*' ? ui::BrightYellow : g == 'H' ? ui::BrightRed
                       : g == '#' ? ui::BrightMagenta : g == '?' ? ui::Gray : ui::BrightCyan;
            c.put(rr, cc, (char32_t)g, fg, ui::Black, g == '@' ? ui::ATTR_BOLD : ui::ATTR_NONE);
            if (rr - top > map_h) map_h = rr - top;
        }
        // side legend
        int lc = c.width() - 13;
        c.text(top + 0, lc, "@ you", ui::BrightWhite, ui::Black);
        c.text(top + 1, lc, "* target", ui::BrightYellow, ui::Black);
        c.text(top + 2, lc, "# named", ui::BrightMagenta, ui::Black);
        c.text(top + 3, lc, "H hunter", ui::BrightRed, ui::Black);
        // log pane (recent lines) below the map
        int logtop = top + map_h + 2;
        c.hline(logtop - 1, 1, c.width() - 2, U'-', ui::Gray, ui::Black);
        // auto-combat siege line — always visible, never a separate screen
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
    std::string chron_;
    int over_scroll_ = 0;
    bool copied_ = false;
};

std::unique_ptr<App> make_cyberhack() { return std::make_unique<CyberHack>(); }

} // namespace apps
