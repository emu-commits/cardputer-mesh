// Midnight City — the app shell over the mid:: engine (docs/MIDNIGHT_CITY.md,
// Phase 8b). DF-classic EMBARK VIEW: you watch the @ and the co-located NPCs
// walk the current district while the world crawls; menus are modal overlays.
// The engine is pure/headless and proven by sim/midnight_sim; this file is only
// rendering + input. The avatar SELF-DRIVES (agency model, §1), so the menu is
// standing-orders + jack-in + travel + info, not micro-management.
#include "apps/apps.h"
#include <cstdio>
#include <vector>
#include "core/midnight_world.h"
#include "core/persist.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;
namespace mc = mid;

namespace apps {

class Midnight : public App {
    enum View { EMBARK, MENU, TRAVEL };
    static constexpr uint32_t STEP_MS = 600;   // "watch it crawl" cadence

public:
    void on_create(AppContext& ctx) override {
        if (!load(ctx)) {
            uint32_t seed = (ctx.now_ms | 1u) * 2654435761u;
            mc::gen_world(world_, seed);
        }
        enter_district(world_.agents[0].loc, /*announce=*/false);
        arcs_.reset(720);
        view_ = EMBARK; paused_ = false; last_step_ = ctx.now_ms;
        ticker_ = "Midnight City. You have nothing. The rain doesn't care.";
    }
    void on_pause(AppContext& ctx) override { save(ctx); }
    void on_destroy(AppContext& ctx) override { save(ctx); }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (view_ == MENU)   return key_menu(ctx, k);
        if (view_ == TRAVEL) return key_travel(ctx, k);
        // EMBARK
        if (k.is_char() && (k.ch == 'm' || k.ch == 'M')) { open_menu(); return true; }
        if (k.is_char() && k.ch == ' ') { paused_ = !paused_; return true; }
        if (k.is_char() && (k.ch == '>' || k.ch == '.')) { step_ms_ = step_ms_ > 150 ? step_ms_ - 150 : 100; return true; }
        if (k.is_char() && (k.ch == '<' || k.ch == ',')) { step_ms_ += 150; return true; }
        return false;   // Esc falls through -> launcher
    }

    void tick(AppContext& ctx) override {
        if (view_ != EMBARK || paused_) return;
        if (ctx.keep_awake) ctx.keep_awake();             // hold the CYD awake through the crawl
        if (!(world_.agents[0].status & mc::AF_ALIVE)) { paused_ = true; ticker_ = chronicle_death(); return; }
        if (ctx.now_ms - last_step_ < step_ms_) return;
        last_step_ = ctx.now_ms;
        crawl();
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        (void)ctx;
        draw_status(c);
        c.hline(1, 0, c.width());
        draw_map(c, 2, c.height() - 3);                   // rows 2 .. h-3
        c.text(c.height() - 2, 0, ui::fit(ticker_, c.width()), ui::BrightCyan, ui::Black);
        ui::footer(c, paused_ ? " [PAUSED] m:menu space:resume <>:speed esc:home"
                              : " m:menu  space:pause  <>:speed  esc:home ");
        if (view_ == MENU || view_ == TRAVEL) draw_menu(c);
    }

private:
    mc::World     world_;
    mc::LocalMap  local_;
    mc::ArcTracker arcs_;
    uint8_t  cur_district_ = 0;
    int      px_ = 0, py_ = 0;            // @ tile on local_
    int      tx_ = -1, ty_ = -1;          // cosmetic walk target
    uint32_t last_step_ = 0, step_ms_ = STEP_MS;
    bool     paused_ = false;
    View     view_ = EMBARK;
    std::string ticker_;
    ui::ListState mls_;

    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    void enter_district(uint8_t d, bool announce) {
        cur_district_ = d;
        mc::gen_localmap(local_, world_, d);
        px_ = local_.entry_x; py_ = local_.entry_y; tx_ = ty_ = -1;
        if (announce) ticker_ = std::string("You move into the ") + mc::district_type_name(world_.districts[d].type) + ".";
    }

    // one "watch it crawl" step: advance the world, narrate, animate the @
    void crawl() {
        uint32_t before = world_.tick;
        mc::tick_world(world_);
        uint16_t tk = (uint16_t)(before);                 // events pushed this tick carry tick==before
        std::string line; bool got_arc = false;
        for (int i = 0; i < mc::EVMAX; ++i) {
            const mc::Event& e = world_.events[i];
            if (e.kind == mc::EV_NONE || e.tick != tk) continue;
            uint8_t arc = arcs_.ingest(e.kind, e.node, e.data, (int)before);
            if (arc != mc::MA_NONE) { line = mc::narrate_arc(world_, arc, e.node, arcs_.last_kind[e.node < mc::MAX_DISTRICTS ? e.node : 0], e.kind); got_arc = true; }
            else if (!got_arc) line = mc::narrate_event(world_, e);
        }
        if (!line.empty()) ticker_ = line;

        // the sim is authoritative about which district the avatar is in
        if (world_.agents[0].loc != cur_district_) enter_district(world_.agents[0].loc, /*announce=*/true);
        else animate_avatar();
    }

    // cosmetic: drift the @ toward a target POI (or idle) — never affects the sim
    void animate_avatar() {
        if (tx_ < 0) {                                    // pick a wander target occasionally
            if (local_.poi_count && (world_.tick & 7) == 0) { const mc::LocalPOI& p = local_.poi[world_.tick % local_.poi_count]; tx_ = p.x; ty_ = p.y; }
            return;
        }
        if (px_ == tx_ && py_ == ty_) { tx_ = ty_ = -1; return; }
        int nx = px_, ny = py_;
        if (mc::localmap_step_toward(local_, px_, py_, tx_, ty_, &nx, &ny)) { px_ = nx; py_ = ny; }
        else tx_ = ty_ = -1;
    }

    // ---- rendering ---------------------------------------------------------
    void draw_status(TextCanvas& c) {
        const mc::Agent& p = world_.agents[0];
        const mc::Company& co = world_.company;
        int h = mc::human_count(world_), s = mc::synth_count(world_), m = 0;
        for (int i = 0; i < world_.agent_count; ++i) if ((world_.agents[i].status & mc::AF_ALIVE) && world_.agents[i].kind == mc::AK_MUTANT) ++m;
        char b[96];
        std::snprintf(b, sizeof b, "%s $%u [%s] D%u %02u:00%s H%dS%dM%d",
                      mc::agent_name(p.name_id), p.money, mc::company_tier_name(co.tier),
                      world_.tick / 24, world_.tick % 24, (p.status & mc::AF_INJURED) ? " HURT" : "",
                      h, s, m);
        c.text(0, 0, ui::fit(b, c.width()), ui::Black, ui::BrightGreen);
    }

    void glyph_color(uint8_t tile, uint8_t& fg) {
        switch (tile) {
            case mc::LT_WALL:  fg = ui::Gray; break;
            case mc::LT_HAZARD:fg = ui::BrightMagenta; break;
            case mc::LT_DOOR:  fg = ui::BrightYellow; break;
            default:           fg = ui::White; break;
        }
    }

    void draw_map(TextCanvas& c, int top, int rows) {
        int mw = c.width();
        int cam_x = mc::LMAP_W <= mw ? 0 : clampi(px_ - mw / 2, 0, mc::LMAP_W - mw);
        int cam_y = mc::LMAP_H <= rows ? 0 : clampi(py_ - rows / 2, 0, mc::LMAP_H - rows);
        // terrain
        for (int r = 0; r < rows; ++r) {
            int my = cam_y + r;
            for (int x = 0; x < mw; ++x) {
                int mx = cam_x + x;
                char g = ' '; uint8_t fg = ui::White;
                if (mx < mc::LMAP_W && my < mc::LMAP_H) { uint8_t t = local_.tile[my][mx]; g = mc::localmap_tile_glyph(t); glyph_color(t, fg); }
                c.put(top + r, x, (char32_t)g, fg, ui::Black);
            }
        }
        // POIs
        for (int i = 0; i < local_.poi_count; ++i) {
            const mc::LocalPOI& p = local_.poi[i];
            int sx = p.x - cam_x, sy = p.y - cam_y;
            if (sx >= 0 && sx < mw && sy >= 0 && sy < rows)
                c.put(top + sy, sx, (char32_t)mc::localmap_poi_glyph(p.service_bit), ui::BrightYellow, ui::Black);
        }
        // co-located NPCs on the actor spawn tiles (kind-colored)
        int slot = 0;
        for (int i = 1; i < world_.agent_count && slot < local_.actor_count; ++i) {
            const mc::Agent& a = world_.agents[i];
            if (!(a.status & mc::AF_ALIVE) || a.loc != cur_district_) continue;
            int ax = local_.actor_xy[slot][0], ay = local_.actor_xy[slot][1]; ++slot;
            int sx = ax - cam_x, sy = ay - cam_y;
            if (sx < 0 || sx >= mw || sy < 0 || sy >= rows) continue;
            char g = a.kind == mc::AK_SYNTH ? 's' : a.kind == mc::AK_MUTANT ? 'm' : 'p';
            uint8_t fg = a.kind == mc::AK_SYNTH ? ui::BrightCyan : a.kind == mc::AK_MUTANT ? ui::BrightGreen : ui::White;
            c.put(top + sy, sx, (char32_t)g, fg, ui::Black);
        }
        // the @
        int psx = px_ - cam_x, psy = py_ - cam_y;
        if (psx >= 0 && psx < mw && psy >= 0 && psy < rows)
            c.put(top + psy, psx, U'@', ui::BrightWhite, ui::Black);
    }

    // ---- menus -------------------------------------------------------------
    void open_menu() { view_ = MENU; mls_ = ui::ListState{}; }

    int main_menu_n() const { return 4; }                 // Orders / Jack in / Travel / Close
    std::string main_menu_item(int i) {
        const mc::Directive& d = world_.directive;
        switch (i) {
            case 0: return std::string("Orders: pursue ") + mc::ambition_name(d.ambition);
            case 1: return (world_.agents[0].status & mc::AF_HAS_DECK) ? "Jack in (run the net)" : "Jack in (need a deck)";
            case 2: return "Travel to an adjacent district";
            default: return "Close";
        }
    }

    bool key_menu(AppContext& ctx, const KeyEvent& k) {
        (void)ctx;
        int n = main_menu_n();
        if (mls_.move(k, n, n)) return true;
        if (k.key == Key::Esc)  { view_ = EMBARK; return true; }
        if (k.key == Key::Enter) {
            switch (mls_.sel) {
                case 0: { mc::Directive& d = world_.directive; d.ambition = (uint8_t)((d.ambition + 1) % mc::AMB_COUNT);
                          ticker_ = std::string("New standing order: pursue ") + mc::ambition_name(d.ambition) + "."; break; }
                case 1: { if (world_.agents[0].status & mc::AF_HAS_DECK) { mc::JackResult r = mc::jack_in(world_, 0);
                              ticker_ = r.ran ? (std::string("You jacked in: ") + mc::outcome_name(r.outcome) + (r.flatlined ? " - flatline." : "."))
                                              : "No data target in reach.";
                          } else ticker_ = "You have no cyberdeck to jack with."; view_ = EMBARK; break; }
                case 2: { view_ = TRAVEL; mls_ = ui::ListState{}; return true; }
                default: view_ = EMBARK; break;
            }
            return true;
        }
        return true;   // modal swallows keys
    }

    bool key_travel(AppContext& ctx, const KeyEvent& k) {
        (void)ctx;
        int n = world_.districts[cur_district_].deg;
        if (mls_.move(k, n, n)) return true;
        if (k.key == Key::Esc)  { view_ = MENU; mls_ = ui::ListState{}; return true; }
        if (k.key == Key::Enter && n > 0) {
            uint8_t dest = world_.districts[cur_district_].adj[clampi(mls_.sel, 0, n - 1)];
            if (dest != mc::NONE8 && dest < world_.district_count) {
                world_.agents[0].loc = dest;              // manual travel override
                enter_district(dest, /*announce=*/true);
            }
            view_ = EMBARK;
            return true;
        }
        return true;
    }

    void draw_menu(TextCanvas& c) {
        int ir, ic, iw, ih;
        if (view_ == TRAVEL) {
            const mc::District& d = world_.districts[cur_district_];
            int n = d.deg;
            ui::modal_box(c, n + 4, 34, "Travel", ui::BrightCyan, ir, ic, iw, ih, "enter:go  esc:back");
            ui::list(c, ir, ih, mls_, n, [&](int i) {
                uint8_t nb = d.adj[i];
                return std::string(nb < world_.district_count ? mc::district_type_name(world_.districts[nb].type) : "?");
            });
            return;
        }
        int n = main_menu_n();
        ui::modal_box(c, n + 4, 42, "Orders", ui::BrightCyan, ir, ic, iw, ih, "enter:do  esc:back");
        ui::list(c, ir, ih, mls_, n, [&](int i) { return main_menu_item(i); });
    }

    std::string chronicle_death() {
        return std::string(mc::agent_name(world_.agents[0].name_id)) + " is dead. The sprawl closes over the space where they stood. (esc to leave)";
    }

    // ---- persistence -------------------------------------------------------
    bool load(AppContext& ctx) {
        if (!ctx.state) return false;
        std::string blob = ctx.state->get("midnight.world", "");
        if (blob.empty()) return false;
        return mc::deserialize(blob, world_);
    }
    void save(AppContext& ctx) {
        if (!ctx.state) return;
        std::string blob; mc::serialize(world_, blob);
        ctx.state->set("midnight.world", blob);
        ctx.state->flush();
    }
};

std::unique_ptr<App> make_midnight() { return std::make_unique<Midnight>(); }

} // namespace apps
