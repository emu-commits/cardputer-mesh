// Midnight City — the app shell over the mid:: engine (docs/MIDNIGHT_CITY.md,
// Phase 8b). DF-classic EMBARK VIEW: you watch the @ and the co-located NPCs
// walk the current district while the world crawls; menus are modal overlays.
// The engine is pure/headless and proven by sim/midnight_sim; this file is only
// rendering + input. The avatar SELF-DRIVES (agency model, §1), so the menu is
// standing-orders + jack-in + travel + info, not micro-management.
//
// Screen layout (Batch A): status row · focus row · legend row · separator ·
// the (1/3-shorter) embark map · a wrapping LOG panel · footer.
#include "apps/apps.h"
#include <cstdio>
#include <functional>
#include <vector>
#include <string>
#include "core/midnight_world.h"
#include "core/persist.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;
namespace mc = mid;

namespace apps {

class Midnight : public App {
    enum View { EMBARK, MENU, ORDERS, FOCUSMENU, COMPANY, SECTOR, CRAFT, INVENTORY, TRAVEL };
    static constexpr uint32_t STEP_MS = 600;   // "watch it crawl" cadence
    static constexpr uint32_t ANIM_MS = 110;   // @ walks smoothly between crawls
    static constexpr size_t   LOG_CAP = 24;     // recent log entries kept

public:
    void on_create(AppContext& ctx) override {
        if (!load(ctx)) {
            uint32_t seed = (ctx.now_ms | 1u) * 2654435761u;
            mc::gen_world(world_, seed);
        }
        enter_district(world_.agents[0].loc, /*announce=*/false);
        view_ = EMBARK; paused_ = false; last_step_ = ctx.now_ms;
        push_log("Midnight City. You have nothing. The rain doesn't care.");
    }
    void on_pause(AppContext& ctx) override { save(ctx); }
    void on_destroy(AppContext& ctx) override { save(ctx); }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (view_ == MENU)      return key_menu(ctx, k);
        if (view_ == ORDERS)    return key_orders(ctx, k);
        if (view_ == FOCUSMENU) return key_focus(ctx, k);
        if (view_ == COMPANY)   return key_company(ctx, k);
        if (view_ == SECTOR)    return key_sector(ctx, k);
        if (view_ == CRAFT)     return key_craft(ctx, k);
        if (view_ == INVENTORY) { if (k.key == Key::Esc || k.key == Key::Enter) { view_ = MENU; mls_ = ui::ListState{}; } return true; }
        if (view_ == TRAVEL)    return key_travel(ctx, k);
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
        if (!(world_.agents[0].status & mc::AF_ALIVE)) { paused_ = true; push_log(chronicle_death()); return; }
        // the @ walks smoothly (faster than the world crawl) toward its focus
        if (ctx.now_ms - last_anim_ >= ANIM_MS) { last_anim_ = ctx.now_ms; step_avatar(); }
        if (ctx.now_ms - last_step_ >= step_ms_) { last_step_ = ctx.now_ms; crawl(); }
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        (void)ctx;
        int h = c.height(), w = c.width();
        draw_status(c, 0);
        draw_focus(c, 1);
        draw_legend(c, 2);
        c.hline(3, 0, w);
        // body split: a 1/3-shorter map on top, a wrapping log panel beneath it.
        int log_rows = (h - 5) / 3; if (log_rows < 4) log_rows = 4;
        int map_top  = 4;
        int log_top  = h - 1 - log_rows;          // log occupies [log_top, h-1)
        int map_rows = log_top - map_top;
        if (map_rows < 3) { map_rows = 3; log_top = map_top + map_rows; }
        draw_map(c, map_top, map_rows);
        draw_log(c, log_top, log_rows, w);
        ui::footer(c, paused_ ? " [PAUSED] m:menu space:resume <>:speed esc:home"
                              : " m:menu  space:pause  <>:speed  esc:home ");
        if (view_ != EMBARK) draw_menu(c);
    }

private:
    mc::World     world_;
    mc::LocalMap  local_;
    uint8_t  cur_district_ = 0;
    uint8_t  pending_district_ = mc::NONE8; // sim moved us; @ must walk to edge first (#3)
    int      px_ = 0, py_ = 0;            // @ tile on local_
    int      tx_ = -1, ty_ = -1;          // current walk target (edge when crossing)
    uint32_t last_step_ = 0, step_ms_ = STEP_MS;
    uint32_t last_anim_ = 0;
    bool     paused_ = false;
    View     view_ = EMBARK;
    std::vector<std::string> log_;        // scrolling log ring (newest at back)
    ui::ListState mls_;

    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static int iabs(int v) { return v < 0 ? -v : v; }

    static const char* act_name(uint8_t a) {
        switch (a) {
            case mc::ACT_WORK:    return "working";
            case mc::ACT_BUY:     return "buying supplies";
            case mc::ACT_MOVE:    return "on the move";
            case mc::ACT_REST:    return "resting";
            case mc::ACT_SEEKJOB: return "seeking work";
            default:              return "idle";
        }
    }

    void push_log(const std::string& s) {
        if (s.empty()) return;
        if (!log_.empty() && log_.back() == s) return;    // de-dup consecutive lines
        log_.push_back(s);
        while (log_.size() > LOG_CAP) log_.erase(log_.begin());
    }

    void enter_district(uint8_t d, bool announce) {
        cur_district_ = d;
        mc::gen_localmap(local_, world_, d);
        px_ = local_.entry_x; py_ = local_.entry_y; tx_ = ty_ = -1;
        if (announce) push_log(std::string("You move into the ") + mc::district_type_name(world_.districts[d].type) + ".");
    }

    // how notable an event is, so a busy tick surfaces the beat that matters most
    static int salience(uint8_t kind) {
        switch (kind) {
            case mc::EV_DEATH: case mc::EV_FLATLINE: return 9;
            case mc::EV_THREAT_SPAWN: case mc::EV_THREAT_DEFEAT: return 8;
            case mc::EV_RIOT: case mc::EV_COMBAT: return 7;
            case mc::EV_TURF_FLIP: case mc::EV_RAID: case mc::EV_HEIST: return 6;
            case mc::EV_LOCKDOWN: case mc::EV_COLLAPSE: case mc::EV_EXTORT: return 5;
            case mc::EV_SHORTAGE: case mc::EV_REFUGEE: case mc::EV_HEATWAVE: return 4;
            case mc::EV_BOUNTY: case mc::EV_POP_SHIFT: case mc::EV_NETALLY: return 3;
            case mc::EV_RECRUIT: case mc::EV_JACKIN: return 2;
            default:           return 1;   // market day / rumor / newcomer — ambient
        }
    }

    std::string fmt_txn(const mc::Txn& t) {
        char b[64];
        long v = t.amount < 0 ? -(long)t.amount : (long)t.amount;
        const char* sign = t.amount < 0 ? "-" : "+";
        if (t.reason == mc::TXN_RENT && world_.apt_district < world_.district_count)
            std::snprintf(b, sizeof b, "%s$%ld rent (%s)", sign, v,
                          mc::district_type_name(world_.districts[world_.apt_district].type));
        else if (t.reason == mc::TXN_BUY)
            std::snprintf(b, sizeof b, "-$%ld bought %s", v, mc::commodity_name(t.data));
        else if (t.reason == mc::TXN_SALE)
            std::snprintf(b, sizeof b, "+$%ld sold %s", v, mc::item_name(t.data));
        else
            std::snprintf(b, sizeof b, "%s$%ld %s", sign, v, mc::txn_reason_name(t.reason));
        return std::string(b);
    }

    // one "watch it crawl" step: advance the world, narrate, animate the @
    void crawl() {
        uint32_t before = world_.tick;
        mc::tick_world(world_);
        uint16_t tk = (uint16_t)(before);                 // events/txns this tick carry tick==before

        // transactions this tick -> the log: every cash move is accounted for (#4)
        if (world_.txn_count) {
            int n = world_.txn_count;
            int start = (world_.txn_head + mc::TXNMAX - n) % mc::TXNMAX;
            for (int i = 0; i < n; ++i) {
                const mc::Txn& t = world_.txns[(start + i) % mc::TXNMAX];
                if (t.tick == tk) push_log(fmt_txn(t));
            }
        }

        // event narration — surface ONE salient beat we can SEE this tick
        // (co-located / about us / world-scale, #5). Generative per-event voice; the
        // story emerges from a dense stream of these, not baked "named arcs". The
        // consecutive-duplicate de-dup in push_log keeps it from repeating.
        const mc::Event* best = nullptr; int best_sal = -1;
        for (int i = 0; i < mc::EVMAX; ++i) {
            const mc::Event& e = world_.events[i];
            if (e.kind == mc::EV_NONE || e.tick != tk) continue;
            bool here = (e.node == cur_district_) || (e.agent == 0) || (e.node == mc::NONE8);
            if (!here) continue;
            int s = salience(e.kind);
            if (s > best_sal) { best_sal = s; best = &e; }
        }
        if (best) push_log(mc::narrate_event(world_, *best));

        // the sim is authoritative about WHICH district the avatar is in, but the
        // view never warps: flag a pending crossing so the @ walks to the edge (#3).
        if (world_.agents[0].loc != cur_district_ && pending_district_ == mc::NONE8)
            pending_district_ = world_.agents[0].loc;
    }

    // walk the @ one tile toward where the current focus needs it (#9), or — when a
    // district crossing is pending — back to the entry "gate" and then cross (#1: a
    // visible walk, never an instant jump). Pure view animation; never touches the sim.
    void step_avatar() {
        if (pending_district_ != mc::NONE8) {
            int gx = local_.entry_x, gy = local_.entry_y;     // leave via the gate (always reachable)
            for (int step = 0; step < 3; ++step) {            // brisk but visible
                if (px_ == gx && py_ == gy) break;
                int nx = px_, ny = py_;
                if (mc::localmap_step_toward(local_, px_, py_, gx, gy, &nx, &ny)) { px_ = nx; py_ = ny; }
                else break;
            }
            if (px_ == gx && py_ == gy) {                     // reached the gate -> cross
                uint8_t d = pending_district_; pending_district_ = mc::NONE8;
                enter_district(d, /*announce=*/true);
            }
            return;
        }
        // path toward the POI the avatar's current activity calls for (work / market
        // / job board); 0xFF = head "home" (the entry tile) and idle there.
        uint8_t want = mc::avatar_target_poi(world_);
        int gx = local_.entry_x, gy = local_.entry_y;
        if (want != 0xFF) {
            int bd = 1 << 30, found = 0;
            for (int i = 0; i < local_.poi_count; ++i) if (local_.poi[i].service_bit == want) {
                int d = iabs(local_.poi[i].x - px_) + iabs(local_.poi[i].y - py_);
                if (d < bd) { bd = d; gx = local_.poi[i].x; gy = local_.poi[i].y; found = 1; }
            }
            (void)found;
        }
        if (px_ == gx && py_ == gy) return;               // arrived; stay put (at work/home)
        int nx = px_, ny = py_;
        if (mc::localmap_step_toward(local_, px_, py_, gx, gy, &nx, &ny)) { px_ = nx; py_ = ny; }
    }

    // ---- rendering ---------------------------------------------------------
    void draw_status(TextCanvas& c, int row) {
        const mc::Agent& p = world_.agents[0];
        const mc::Company& co = world_.company;
        int hum = mc::human_count(world_), syn = mc::synth_count(world_), mut = 0;
        for (int i = 0; i < world_.agent_count; ++i) if ((world_.agents[i].status & mc::AF_ALIVE) && world_.agents[i].kind == mc::AK_MUTANT) ++mut;
        char b[96];
        std::snprintf(b, sizeof b, "%s $%u [%s] D%u %02u:00%s H%dS%dM%d",
                      mc::agent_name(p.name_id), (unsigned)p.money, mc::company_tier_name(co.tier),
                      (unsigned)(world_.tick / 24), (unsigned)(world_.tick % 24),
                      (p.status & mc::AF_INJURED) ? " HURT" : "", hum, syn, mut);
        c.text(row, 0, ui::fit(b, c.width()), ui::Black, ui::BrightGreen);
    }

    // Row 1: the avatar's concrete current focus + destination (#9).
    void draw_focus(TextCanvas& c, int row) {
        const mc::World& w = world_;
        char b[96];
        if (w.interrupt_focus != mc::NONE8) {
            std::snprintf(b, sizeof b, "Focus: fleeing danger  (was: %s)", mc::focus_name(w.focus));
        } else if (w.focus == mc::FC_CONTRACT && w.contract.active) {
            std::snprintf(b, sizeof b, "Focus: %s -> %s (%s)", mc::contract_kind_name(w.contract.kind),
                          mc::district_type_name(w.districts[w.contract.target].type),
                          w.agents[0].loc == w.contract.target ? "working" : "en route");
        } else {
            std::snprintf(b, sizeof b, "Focus: %s - %s", mc::focus_name(w.focus), act_name(w.agents[0].activity));
        }
        c.text(row, 0, ui::fit(b, c.width()), ui::BrightYellow, ui::Black);
    }

    // Row 2: one-line glyph legend (#6).
    void draw_legend(TextCanvas& c, int row) {
        c.text(row, 0, ui::fit("@you  p s m=folk  J$CXFBDfRLAG=places  ~hazard  #wall",
                               c.width()), ui::Gray, ui::Black);
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

    // The wrapping log panel (#2): wrap each entry on spaces/hyphens (never
    // mid-word) via ui::wrap_text, then show the tail so the newest is at bottom.
    void draw_log(TextCanvas& c, int top, int rows, int w) {
        std::vector<std::string> lines;
        for (const std::string& s : log_) {
            std::vector<std::string> ws = ui::wrap_text(s, w);
            for (std::string& l : ws) lines.push_back(l);
        }
        int start = (int)lines.size() - rows; if (start < 0) start = 0;
        for (int r = 0; r < rows; ++r) {
            int li = start + r;
            std::string s = (li >= 0 && li < (int)lines.size()) ? lines[li] : std::string();
            c.text(top + r, 0, ui::fit(s, w), ui::BrightCyan, ui::Black);
        }
    }

    // ---- menus -------------------------------------------------------------
    void open_menu() { view_ = MENU; mls_ = ui::ListState{}; }

    int main_menu_n() const { return 8; } // Orders/Focus/Company/Crafting/Inventory/Jack/Travel/Close
    std::string main_menu_item(int i) {
        switch (i) {
            case 0: return "Standing orders";
            case 1: return "Set focus";
            case 2: return "Company";
            case 3: return "Crafting";
            case 4: return "Inventory";
            case 5: return (world_.agents[0].status & mc::AF_HAS_DECK) ? "Jack in (run the net)" : "Jack in (need a deck)";
            case 6: return "Travel to an adjacent district";
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
                case 0: view_ = ORDERS; mls_ = ui::ListState{}; return true;
                case 1: view_ = FOCUSMENU; mls_ = ui::ListState{}; return true;
                case 2: view_ = COMPANY; mls_ = ui::ListState{}; return true;
                case 3: view_ = CRAFT; mls_ = ui::ListState{}; return true;
                case 4: view_ = INVENTORY; mls_ = ui::ListState{}; return true;
                case 5: { if (world_.agents[0].status & mc::AF_HAS_DECK) { mc::JackResult r = mc::jack_in(world_, 0);
                              push_log(r.ran ? (std::string("You jacked in: ") + mc::outcome_name(r.outcome) + (r.flatlined ? " - flatline." : "."))
                                             : "No data target in reach.");
                          } else push_log("You have no cyberdeck to jack with."); view_ = EMBARK; break; }
                case 6: view_ = TRAVEL; mls_ = ui::ListState{}; return true;
                default: view_ = EMBARK; break;
            }
            return true;
        }
        return true;   // modal swallows keys
    }

    // ---- crafting (#H2): visibility + the craft directive --------------------
    bool key_craft(AppContext& ctx, const KeyEvent& k) {
        (void)ctx;
        int n = mc::CR_COUNT;
        if (mls_.move(k, n, n)) return true;
        if (k.key == Key::Esc) { view_ = MENU; mls_ = ui::ListState{}; return true; }
        if (k.key == Key::Enter) {
            uint8_t cr = (uint8_t)clampi(mls_.sel, 0, n - 1);
            world_.craft_target = cr;
            world_.focus = mc::FC_CRAFT;
            push_log(std::string("New focus: craft ") + mc::craft_name(cr) + " (gathering parts).");
            view_ = EMBARK;
            return true;
        }
        return true;
    }

    // ---- company management (#12/#13/#14) ----------------------------------
    bool company_founded() const { const mc::Company& co = world_.company; return !(co.treasury == 0 && co.emp_count == 0); }
    int  company_emp_cap() const { int t = world_.company.tier < mc::CT_COUNT ? world_.company.tier : mc::CT_COUNT - 1; return mc::g_mtune.emp_cap[t]; }
    int  company_n() const { return company_founded() ? 4 : 2; }
    std::string company_item(int i) {
        if (!company_founded()) return i == 0 ? "Found a company..." : "Close";
        switch (i) {
            case 0: return "Grow: hire more";
            case 1: return "Shrink: lay off";
            case 2: return "Change business model";
            default: return "Close";
        }
    }
    bool key_company(AppContext& ctx, const KeyEvent& k) {
        (void)ctx;
        int n = company_n();
        if (mls_.move(k, n, n)) return true;
        if (k.key == Key::Esc) { view_ = MENU; mls_ = ui::ListState{}; return true; }
        if (k.key == Key::Enter) {
            mc::Company& co = world_.company;
            if (!company_founded()) {
                if (mls_.sel == 0) { view_ = SECTOR; mls_ = ui::ListState{}; return true; }
                view_ = EMBARK; return true;
            }
            int cap = company_emp_cap();
            switch (mls_.sel) {
                case 0: { int t = (co.target_emp ? co.target_emp : co.emp_count) + 1; if (t > cap) t = cap; if (t < 1) t = 1; co.target_emp = (uint8_t)t;
                          char b[48]; std::snprintf(b, sizeof b, "Orders: grow the crew toward %d.", t); push_log(b); view_ = EMBARK; break; }
                case 1: { int t = (co.target_emp ? co.target_emp : co.emp_count); if (t > 1) t--; co.target_emp = (uint8_t)t;
                          char b[48]; std::snprintf(b, sizeof b, "Orders: trim the crew toward %d.", t); push_log(b); view_ = EMBARK; break; }
                case 2: view_ = SECTOR; mls_ = ui::ListState{}; return true;
                default: view_ = EMBARK; break;
            }
            return true;
        }
        return true;
    }

    // sector picker (#14): the named business models (skip SEC_NONE)
    int sector_n() const { return mc::SEC_COUNT - 1; }
    bool key_sector(AppContext& ctx, const KeyEvent& k) {
        (void)ctx;
        int n = sector_n();
        if (mls_.move(k, n, n)) return true;
        if (k.key == Key::Esc) { view_ = COMPANY; mls_ = ui::ListState{}; return true; }
        if (k.key == Key::Enter) {
            uint8_t sec = (uint8_t)(clampi(mls_.sel, 0, n - 1) + 1);   // +1 skips SEC_NONE
            world_.company.sector = sec;
            if (!company_founded()) {
                world_.focus = mc::FC_FOUND_CO;
                push_log(std::string("Founding a ") + mc::sector_name(sec) + " outfit (saving the stake).");
            } else {
                push_log(std::string("Business model is now ") + mc::sector_name(sec) + ".");
            }
            view_ = EMBARK;
            return true;
        }
        return true;
    }

    // the player's focus commands (#9/#10): milestones + redirects the avatar pursues
    static constexpr int FOCUS_N = 6;
    std::string focus_item(int i) {
        switch (i) {
            case 0: return "Look for work";
            case 1: return "Rent an apartment";
            case 2: return "Found a company";
            case 3: return "Run the company";
            case 4: return "Resume daily work";
            default: return world_.contract.active ? "Abandon the contract" : "(no contract to abandon)";
        }
    }
    bool key_focus(AppContext& ctx, const KeyEvent& k) {
        (void)ctx;
        if (mls_.move(k, FOCUS_N, FOCUS_N)) return true;
        if (k.key == Key::Esc)  { view_ = MENU; mls_ = ui::ListState{}; return true; }
        if (k.key == Key::Enter) {
            mc::World& w = world_;
            bool emp = (w.agents[0].status & mc::AF_EMPLOYED) != 0;
            switch (mls_.sel) {
                case 0: w.focus = mc::FC_FIND_WORK; push_log("New focus: out looking for work."); break;
                case 1: w.focus = mc::FC_RENT_APT;  push_log("New focus: find a place to live."); break;
                case 2: w.focus = mc::FC_FOUND_CO;  push_log("New focus: scrape together a company."); break;
                case 3: w.focus = mc::FC_RUN_CO;    push_log("New focus: build the company up."); break;
                case 4: w.focus = emp ? mc::FC_WORK : mc::FC_FIND_WORK; push_log("Back to the daily grind."); break;
                default:
                    if (w.contract.active) { w.contract.active = 0; w.focus = emp ? mc::FC_WORK : mc::FC_FIND_WORK; push_log("You walk away from the contract."); }
                    else push_log("No contract to abandon.");
                    break;
            }
            view_ = EMBARK;
            return true;
        }
        return true;
    }

    // Standing-orders sub-menu (#7): a real selection, not a cycle.
    bool key_orders(AppContext& ctx, const KeyEvent& k) {
        (void)ctx;
        int n = mc::AMB_COUNT;
        if (mls_.move(k, n, n)) return true;
        if (k.key == Key::Esc)  { view_ = MENU; mls_ = ui::ListState{}; return true; }
        if (k.key == Key::Enter) {
            world_.directive.ambition = (uint8_t)clampi(mls_.sel, 0, n - 1);
            push_log(std::string("New standing order: pursue ") + mc::ambition_name(world_.directive.ambition) + ".");
            view_ = EMBARK;
            return true;
        }
        return true;
    }

    bool key_travel(AppContext& ctx, const KeyEvent& k) {
        (void)ctx;
        int n = world_.districts[cur_district_].deg;
        if (mls_.move(k, n, n)) return true;
        if (k.key == Key::Esc)  { view_ = MENU; mls_ = ui::ListState{}; return true; }
        if (k.key == Key::Enter && n > 0) {
            uint8_t dest = world_.districts[cur_district_].adj[clampi(mls_.sel, 0, n - 1)];
            if (dest != mc::NONE8 && dest < world_.district_count) {
                world_.agents[0].loc = dest;              // sim authoritative
                pending_district_ = dest; tx_ = ty_ = -1; // @ walks to the edge, then crosses (#3)
                push_log(std::string("Heading for the ") + mc::district_type_name(world_.districts[dest].type) + ".");
            }
            view_ = EMBARK;
            return true;
        }
        return true;
    }

    // a content-sized list modal so text never overflows the box (#8)
    void list_modal(TextCanvas& c, const char* title, const char* foot, int n,
                    const std::function<std::string(int)>& item) {
        size_t maxw = std::string(title).size();
        if (std::string(foot).size() > maxw) maxw = std::string(foot).size();
        for (int i = 0; i < n; ++i) { size_t l = item(i).size(); if (l > maxw) maxw = l; }
        int cols = clampi((int)maxw + 6, 18, c.width() - 2);
        int rows = clampi(n + 4, 6, c.height() - 2);
        int ir, ic, iw, ih;
        ui::modal_box(c, rows, cols, title, ui::BrightCyan, ir, ic, iw, ih, foot);
        ui::list(c, ir, ic, ih, iw, mls_, n, item);
    }

    void draw_menu(TextCanvas& c) {
        if (view_ == TRAVEL) {
            const mc::District& d = world_.districts[cur_district_];
            list_modal(c, "Travel", "enter:go  esc:back", d.deg, [&](int i) {
                uint8_t nb = d.adj[i];
                return std::string(nb < world_.district_count ? mc::district_type_name(world_.districts[nb].type) : "?");
            });
            return;
        }
        if (view_ == ORDERS) {
            list_modal(c, "Standing orders", "enter:set  esc:back", mc::AMB_COUNT, [&](int i) {
                return std::string("Pursue ") + mc::ambition_name((uint8_t)i);
            });
            return;
        }
        if (view_ == FOCUSMENU) {
            list_modal(c, "Set focus", "enter:set  esc:back", FOCUS_N, [&](int i) { return focus_item(i); });
            return;
        }
        if (view_ == COMPANY) { draw_company(c); return; }
        if (view_ == CRAFT) { draw_craft(c); return; }
        if (view_ == INVENTORY) { draw_inventory(c); return; }
        if (view_ == SECTOR) {
            list_modal(c, "Business model", "enter:pick  esc:back", sector_n(),
                       [&](int i) { return std::string(mc::sector_name((uint8_t)(i + 1))); });
            return;
        }
        list_modal(c, "Menu", "enter:do  esc:back", main_menu_n(), [&](int i) { return main_menu_item(i); });
    }

    // the company P&L readout (#12) + the action list (grow/shrink/model/found)
    void draw_company(TextCanvas& c) {
        const mc::Company& co = world_.company;
        bool founded = company_founded();
        mc::CompanyFinance f = mc::company_finance(world_);
        int nact = company_n();
        int infolines = founded ? 6 : 1;
        int rows = clampi(infolines + nact + 4, 6, c.height() - 2);
        int ir, ic, iw, ih;
        ui::modal_box(c, rows, 46, "Company", ui::BrightCyan, ir, ic, iw, ih, "enter:do  esc:back");
        int r = ir; char b[64];
        if (!founded) {
            c.text(r++, ic, ui::fit("No company yet. Save up, then found one.", iw), ui::White, ui::Black);
        } else {
            std::snprintf(b, sizeof b, "%s  [%s]", mc::sector_name(co.sector), mc::company_tier_name(co.tier));
            c.text(r++, ic, ui::fit(b, iw), ui::BrightWhite, ui::Black);
            std::snprintf(b, sizeof b, "Treasury: $%u", (unsigned)co.treasury);
            c.text(r++, ic, ui::fit(b, iw), ui::White, ui::Black);
            if (co.target_emp) std::snprintf(b, sizeof b, "Crew %u (target %u, cap %d)  Assets %u", (unsigned)co.emp_count, (unsigned)co.target_emp, company_emp_cap(), (unsigned)co.asset_count);
            else               std::snprintf(b, sizeof b, "Crew %u (auto, cap %d)  Assets %u", (unsigned)co.emp_count, company_emp_cap(), (unsigned)co.asset_count);
            c.text(r++, ic, ui::fit(b, iw), ui::White, ui::Black);
            std::snprintf(b, sizeof b, "Gross/day $%u", (unsigned)f.gross);
            c.text(r++, ic, ui::fit(b, iw), ui::White, ui::Black);
            std::snprintf(b, sizeof b, "Payroll $%u   Upkeep $%u", (unsigned)f.payroll, (unsigned)f.upkeep);
            c.text(r++, ic, ui::fit(b, iw), ui::White, ui::Black);
            std::snprintf(b, sizeof b, "Net/day %s$%ld", f.net < 0 ? "-" : "+", (long)(f.net < 0 ? -f.net : f.net));
            c.text(r++, ic, ui::fit(b, iw), f.net < 0 ? ui::BrightMagenta : ui::BrightGreen, ui::Black);
        }
        int list_rows = ih - (r - ir); if (list_rows < 1) list_rows = 1;
        ui::list(c, r, ic, list_rows, iw, mls_, nact, [&](int i) { return company_item(i); });
    }

    // plain inventory nouns (item_name() carries articles for "sold a weapon")
    static const char* inv_label(int it) {
        switch (it) {
            case mc::IT_MATERIALS:  return "scrap";
            case mc::IT_COMPONENTS: return "components";
            case mc::IT_WEAPONS:    return "weapons";
            case mc::IT_ARMOR:      return "armor";
            case mc::IT_IMPLANTS:   return "implants";
            case mc::IT_CHEMS:      return "chems";
            case mc::IT_DATA:       return "data shards";
            case mc::IT_FOOD:       return "rations";
            default:                return "goods";
        }
    }
    // read-only inventory list (#6): one line per item class with quantity.
    void draw_inventory(TextCanvas& c) {
        const mc::Agent& p = world_.agents[0];
        std::vector<std::string> lines; char b[48];
        std::snprintf(b, sizeof b, "Cash: $%u", (unsigned)p.money); lines.push_back(b);
        if (p.status & mc::AF_HAS_DECK) lines.push_back("cyberdeck x1");
        for (int it = 0; it < mc::IT_COUNT; ++it) if (p.inv[it] > 0) {
            std::snprintf(b, sizeof b, "%s x%u", inv_label(it), (unsigned)p.inv[it]);
            lines.push_back(b);
        }
        if (lines.size() == 1) lines.push_back("(nothing but the rain)");
        int n = (int)lines.size();
        int rows = clampi(n + 4, 6, c.height() - 2);
        int ir, ic, iw, ih;
        ui::modal_box(c, rows, 32, "Inventory", ui::BrightCyan, ir, ic, iw, ih, "esc:back");
        for (int i = 0; i < n && i < ih; ++i)
            c.text(ir + i, ic, ui::fit(lines[i], iw), ui::White, ui::Black);
    }

    // a recipe row: what it is + the discipline/tier it takes
    std::string craft_item(int i) {
        const mc::Recipe& rc = mc::recipe_of((uint8_t)i);
        char b[64];
        std::snprintf(b, sizeof b, "%s (%s, %s)", mc::craft_name((uint8_t)i),
                      mc::job_name(rc.job), mc::skill_tier_name(rc.min_tier));
        return std::string(b);
    }

    // crafting visibility (#H2): trade/skill + inventory + current goal + recipes
    void draw_craft(TextCanvas& c) {
        const mc::Agent& p = world_.agents[0];
        int n = mc::CR_COUNT;
        int rows = clampi(n + 6, 8, c.height() - 2);
        int ir, ic, iw, ih;
        ui::modal_box(c, rows, 50, "Crafting", ui::BrightCyan, ir, ic, iw, ih, "enter:make  esc:back");
        int r = ir; char b[80];
        uint8_t job = p.job;
        bool has_trade = (job >= mc::J_CONSTRUCTION && job < mc::J_COUNT);
        const char* jn = has_trade ? mc::job_name(job) : "no trade yet";
        uint8_t tier = has_trade ? mc::skill_tier(p.skill[job]) : 0;
        std::snprintf(b, sizeof b, "Trade: %s (%s)%s", jn, mc::skill_tier_name(tier),
                      (p.status & mc::AF_HAS_DECK) ? "  +cyberdeck" : "");
        c.text(r++, ic, ui::fit(b, iw), ui::BrightWhite, ui::Black);
        std::snprintf(b, sizeof b, "Stock: scrap%u comp%u data%u  gun%u arm%u imp%u chem%u",
                      (unsigned)p.inv[mc::IT_MATERIALS], (unsigned)p.inv[mc::IT_COMPONENTS], (unsigned)p.inv[mc::IT_DATA],
                      (unsigned)p.inv[mc::IT_WEAPONS], (unsigned)p.inv[mc::IT_ARMOR], (unsigned)p.inv[mc::IT_IMPLANTS],
                      (unsigned)p.inv[mc::IT_CHEMS]);
        c.text(r++, ic, ui::fit(b, iw), ui::White, ui::Black);
        if (world_.craft_target < mc::CR_COUNT) {
            const mc::Recipe& rc = mc::recipe_of(world_.craft_target);
            std::snprintf(b, sizeof b, "Making %s - needs scrap%u comp%u data%u", mc::craft_name(world_.craft_target),
                          (unsigned)rc.in_mat, (unsigned)rc.in_comp, (unsigned)rc.in_data);
            c.text(r++, ic, ui::fit(b, iw), ui::BrightYellow, ui::Black);
        } else {
            c.text(r++, ic, ui::fit("Pick something to build (the @ will gather + make it):", iw), ui::Gray, ui::Black);
        }
        int list_rows = ih - (r - ir); if (list_rows < 1) list_rows = 1;
        ui::list(c, r, ic, list_rows, iw, mls_, n, [&](int i) { return craft_item(i); });
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
        // NOTE: deliberately NO flush() here. On device, flush() rebuilds the entire
        // NVS key/value map into one ~13KB contiguous std::string; doing that while
        // this ~9KB World is still resident OOMs on the no-PSRAM heap (fragmentation)
        // and aborts -> reboot. The AppManager checkpoints (flush) right after the
        // app is torn down and the World is freed, when a large block is available.
    }
};

std::unique_ptr<App> make_midnight() { return std::make_unique<Midnight>(); }

} // namespace apps
