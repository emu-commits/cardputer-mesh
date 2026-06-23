#include "apps/apps.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include "core/clipboard.h"
#include "core/mesh.h"
#include "core/notification_center.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key;
using ui::KeyEvent;
using ui::TextCanvas;

namespace apps {

static std::string hhmm(uint32_t ts_ms) {
    std::time_t t = std::time(nullptr); // wall clock; ts_ms is monotonic so we just show now
    std::tm tm{}; localtime_r(&t, &tm);
    char b[8]; std::snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
    (void)ts_ms; return b;
}

// ---- Launcher --------------------------------------------------------------
// Left: the app list. Right: a small cyberpunk skyline. Bottom: a two-row hint
// bar. Type a letter to jump to the next app starting with it (repeat cycles).
class Launcher : public App {
    static constexpr int LIST_W = 33; // left column width; art lives to the right
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) ls_.sel = ctx.state->get_int("launcher.sel", 0);
    }
    void on_pause(AppContext& ctx) override {
        if (ctx.state) ctx.state->set_int("launcher.sel", ls_.sel);
    }
    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        auto items = ctx.apps->list();
        int n = (int)items.size();
        if (ls_.move(k, n, rows_)) return true;
        if (k.key == Key::Enter && ls_.sel < n) { ctx.apps->request_switch(items[ls_.sel].first); return true; }
        // type-to-jump: next item whose title starts with the typed letter (cycles)
        if (k.is_char() && !k.ctrl && k.ch > 0x20 && k.ch < 0x7f && n > 0) {
            char want = (char)std::tolower((int)k.ch);
            for (int step = 1; step <= n; ++step) {
                int i = (ls_.sel + step) % n;
                if (!items[i].second.empty() && std::tolower((int)items[i].second[0]) == want) { ls_.sel = i; break; }
            }
            return true;
        }
        return false; // let Esc etc. bubble (no-op at launcher)
    }
    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        int top = ui::header(c, "Cardputer Mesh", ui::BrightGreen, hhmm(ctx.now_ms));
        auto items = ctx.apps->list();
        int n = (int)items.size();
        rows_ = (c.height() - 2) - top; // leave two footer rows
        ls_.clamp(n, rows_);
        // left: app list (manual draw so it stays in the left column)
        for (int r = 0; r < rows_; ++r) {
            int i = ls_.top + r;
            if (i >= n) break;
            bool sel = (i == ls_.sel);
            std::string line = (sel ? "> " : "  ") + items[i].second;
            c.text(top + r, 0, ui::fit(line, LIST_W - 1), sel ? ui::BrightWhite : ui::White,
                   ui::Black, sel ? ui::ATTR_INVERSE : ui::ATTR_NONE);
        }
        draw_art(c, top);
        ui::footer2(c, " up/dn move (wrap)  a-z jump  enter open ",
                       " ctrl-p palette   ctrl-q quit ");
    }
private:
    void draw_art(TextCanvas& c, int top) {
        // ~16-wide non-offensive cyberpunk skyline w/ a signal antenna
        static const char* art[] = {
            "       .",
            "      /|\\  (o)",
            "     / | \\  )) ",
            "   __|_|_|__ ))",
            "  | _|___|_ |",
            "  |#|# # #|#|",
            "  |#|# # #|#|",
            "  |#|#[]#  |#|",
            " _|#|# # #|#|_",
            "=================",
            "  N E O M E S H",
            "  > link up _",
        };
        int rows = (int)(sizeof(art) / sizeof(art[0]));
        for (int i = 0; i < rows; ++i) {
            int r = top + i;
            if (r > c.height() - 3) break;
            uint8_t col = (i < 9) ? ui::BrightCyan : ui::BrightMagenta;
            c.text(r, LIST_W + 1, art[i], col, ui::Black, ui::ATTR_BOLD);
        }
    }
    ui::ListState ls_;
    int rows_ = 1;
};

// ---- Chat (irssi-style windows: channels + DMs) ----------------------------
class Chat : public App {
public:
    void on_create(AppContext& ctx) override {
        if (ctx.notify) ctx.notify->mark_read();
        if (ctx.state) {
            dm_ = ctx.state->get_int("chat.dm", 0) != 0;
            target_ch_ = ctx.state->get_int("chat.ch", 0);
            target_dm_ = (uint32_t)std::strtoul(ctx.state->get("chat.dmid", "0").c_str(), nullptr, 10);
            load_dms(ctx.state->get("chat.opendms", ""));
        }
        consume_intent(ctx);
    }
    void on_resume(AppContext& ctx) override { consume_intent(ctx); }
    void on_pause(AppContext& ctx) override {
        if (!ctx.state) return;
        ctx.state->set_int("chat.dm", dm_ ? 1 : 0);
        ctx.state->set_int("chat.ch", target_ch_);
        ctx.state->set("chat.dmid", std::to_string(target_dm_));
        ctx.state->set("chat.opendms", dump_dms());
    }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (win_open_) return win_key(ctx, k);
        if (k.is_char() && k.ch == '\t') { win_open_ = true; return true; }
        if (k.key == Key::Enter) {
            if (!compose_.empty()) {
                if (dm_) ctx.mesh->send_text(target_dm_, 0, compose_);
                else ctx.mesh->send_text(mesh::BROADCAST, (uint8_t)target_ch_, compose_);
                compose_.clear();
            }
            return true;
        }
        if (k.key == Key::Backspace) { if (!compose_.empty()) compose_.pop_back(); return true; }
        if (k.key == Key::Up) { scroll_++; return true; }
        if (k.key == Key::Down) { if (scroll_ > 0) scroll_--; return true; }
        if (k.ctrl && (k.ch == 'u' || k.ch == 'U')) { if (ctx.clip) compose_ += ctx.clip->get(); return true; }
        if (k.ctrl && (k.ch == 'w' || k.ch == 'W')) { win_open_ = true; return true; }
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) { compose_ += (char)k.ch; return true; }
        return false;
    }

    std::vector<Command> commands(AppContext&) override {
        return {
            {"Switch window", [this](AppContext&) { win_open_ = true; }},
            {"Close this DM", [this](AppContext&) { if (dm_) close_dm(target_dm_); }},
            {"Paste into message", [this](AppContext& c) { if (c.clip) compose_ += c.clip->get(); }},
            {"Clear message", [this](AppContext&) { compose_.clear(); }},
        };
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        auto_open_dms(ctx);
        std::string title = dm_ ? ("DM " + name_for(ctx, target_dm_)) : ("#" + chan_name(ctx, target_ch_));
        int top = ui::header(c, title, ui::BrightCyan, "tab:windows");

        std::vector<const mesh::Message*> rel;
        for (auto& m : ctx.store->all()) if (in_window(ctx, m)) rel.push_back(&m);

        int bottom = c.height() - 3;
        int rows = bottom - top + 1;
        int total = (int)rel.size();
        int start = total - rows - scroll_;
        if (start < 0) start = 0;
        int row = top;
        for (int i = start; i < total && row <= bottom; ++i, ++row) {
            const mesh::Message& m = *rel[i];
            std::string who = m.outgoing ? ctx.mesh->our_short() : m.from_name;
            std::string line = "[" + hhmm(m.ts_ms) + "] " + who + ": " + m.text;
            uint8_t fg = m.outgoing ? ui::BrightGreen : ui::White;
            c.text(row, 1, ui::fit(line, c.width() - 2), fg, ui::Black);
        }
        ui::input_line(c, c.height() - 2, 1, "> ", compose_);
        ui::footer(c, " type+enter send  tab:windows  up/dn scroll  esc:back ");
        if (win_open_) render_windows(ctx, c);
    }

private:
    void consume_intent(AppContext& ctx) {
        if (ctx.nav_arg.rfind("dm:", 0) == 0) {
            uint32_t id = (uint32_t)std::strtoul(ctx.nav_arg.c_str() + 3, nullptr, 10);
            open_dm(id); dm_ = true; target_dm_ = id; scroll_ = 0;
            ctx.nav_arg.clear();
        }
    }
    bool in_window(AppContext& ctx, const mesh::Message& m) {
        if (dm_) return (!m.outgoing && m.dest == ctx.mesh->our_id() && m.from_id == target_dm_) ||
                        (m.outgoing && m.dest == target_dm_);
        return m.dest == mesh::BROADCAST && m.channel == (uint8_t)target_ch_;
    }
    void auto_open_dms(AppContext& ctx) {
        auto& all = ctx.store->all();
        for (size_t i = seen_; i < all.size(); ++i)
            if (!all[i].outgoing && all[i].dest == ctx.mesh->our_id()) open_dm(all[i].from_id);
        seen_ = all.size();
    }
    void open_dm(uint32_t id) { if (id && std::find(open_dms_.begin(), open_dms_.end(), id) == open_dms_.end()) open_dms_.push_back(id); }
    void close_dm(uint32_t id) {
        open_dms_.erase(std::remove(open_dms_.begin(), open_dms_.end(), id), open_dms_.end());
        if (dm_ && target_dm_ == id) { dm_ = false; target_ch_ = 0; }
    }
    std::string name_for(AppContext& ctx, uint32_t id) {
        for (auto& n : ctx.mesh->nodes()) if (n.id == id) return n.short_name.empty() ? n.long_name : n.short_name;
        char b[12]; std::snprintf(b, sizeof b, "!%08x", id); return b;
    }
    std::vector<std::string> chans(AppContext& ctx) {
        std::vector<std::string> out;
        std::string data = ctx.state ? ctx.state->get("channels.data", "") : "";
        size_t i = 0;
        while (i < data.size()) {
            size_t nl = data.find('\n', i);
            std::string ln = data.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            size_t t = ln.find('\t');
            std::string nm = (t == std::string::npos) ? ln : ln.substr(0, t);
            out.push_back(nm.empty() ? "primary" : nm);
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
        if (out.empty()) out.push_back("primary");
        return out;
    }
    std::string chan_name(AppContext& ctx, int idx) {
        auto cs = chans(ctx); return (idx >= 0 && idx < (int)cs.size()) ? cs[idx] : "primary";
    }

    struct Win { std::string label; bool dm; uint32_t id; };
    void build_wins(AppContext& ctx) {
        wins_.clear();
        auto cs = chans(ctx);
        for (int i = 0; i < (int)cs.size(); ++i) wins_.push_back({"#" + cs[i], false, (uint32_t)i});
        for (uint32_t id : open_dms_) wins_.push_back({"@" + name_for(ctx, id), true, id});
    }
    void render_windows(AppContext& ctx, TextCanvas& c) {
        build_wins(ctx);
        int ir, ic, iw, ih;
        int h = (int)wins_.size() + 3; if (h > 14) h = 14;
        ui::modal_box(c, h, 34, "Windows", ui::BrightCyan, ir, ic, iw, ih, "enter:go  d:close DM  esc");
        ui::list(c, ir, ih, win_ls_, (int)wins_.size(), [&](int i) { return wins_[i].label; },
                 ui::White, ui::BrightCyan);
    }
    bool win_key(AppContext& ctx, const KeyEvent& k) {
        build_wins(ctx);
        if (k.key == Key::Esc) { win_open_ = false; return true; }
        if (win_ls_.move(k, (int)wins_.size(), 10)) return true;
        if (wins_.empty()) { if (k.key == Key::Enter) win_open_ = false; return true; }
        const Win& w = wins_[win_ls_.sel];
        if (k.key == Key::Enter) {
            if (w.dm) { dm_ = true; target_dm_ = w.id; } else { dm_ = false; target_ch_ = (int)w.id; }
            scroll_ = 0; win_open_ = false; return true;
        }
        if (k.is_char() && (k.ch == 'd' || k.ch == 'x') && w.dm) { close_dm(w.id); return true; }
        return true;
    }

    void load_dms(const std::string& s) {
        open_dms_.clear(); size_t i = 0;
        while (i < s.size()) {
            size_t cm = s.find(',', i);
            std::string tok = s.substr(i, cm == std::string::npos ? std::string::npos : cm - i);
            if (!tok.empty()) open_dms_.push_back((uint32_t)std::strtoul(tok.c_str(), nullptr, 10));
            if (cm == std::string::npos) break;
            i = cm + 1;
        }
    }
    std::string dump_dms() const {
        std::string s; for (size_t i = 0; i < open_dms_.size(); ++i) { if (i) s += ','; s += std::to_string(open_dms_[i]); }
        return s;
    }

    std::string compose_;
    int scroll_ = 0;
    bool dm_ = false;
    int target_ch_ = 0;
    uint32_t target_dm_ = 0;
    std::vector<uint32_t> open_dms_;
    size_t seen_ = 0;
    bool win_open_ = false;
    ui::ListState win_ls_;
    std::vector<Win> wins_;
};

// ---- Node list -------------------------------------------------------------
class NodeList : public App {
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) ls_.sel = ctx.state->get_int("nodes.sel", 0);
        ls_.clamp((int)ctx.mesh->nodes().size(), 1);
    }
    void on_pause(AppContext& ctx) override {
        if (ctx.state) ctx.state->set_int("nodes.sel", ls_.sel);
    }
    static std::vector<mesh::Node> sorted(AppContext& ctx) {
        auto ns = ctx.mesh->nodes();
        std::stable_sort(ns.begin(), ns.end(), [](const mesh::Node& a, const mesh::Node& b) {
            if (a.is_favorite != b.is_favorite) return a.is_favorite > b.is_favorite; // favorites first
            return a.long_name < b.long_name;
        });
        return ns;
    }
    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        auto ns = sorted(ctx);
        if (ls_.move(k, (int)ns.size(), rows_)) return true;
        if (ns.empty()) return false;
        const mesh::Node& sel = ns[ls_.sel];
        uint32_t id = sel.id;
        if (k.key == Key::Enter) { ctx.nav_arg = "dm:" + std::to_string(id); ctx.apps->request_switch("chat"); return true; }
        if (k.is_char()) {
            if (k.ch == 'f') { ctx.mesh->set_favorite(id, !ctx.mesh->is_favorite(id)); return true; }
            if (k.ch == 'c') { // save as contact, prefilling lora id + names
                ctx.nav_arg = "contact:" + std::to_string(id) + "\t" + sel.long_name + "\t" + sel.short_name;
                ctx.apps->request_switch("contacts"); return true;
            }
        }
        return false;
    }
    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        auto ns = sorted(ctx);
        char cnt[16]; std::snprintf(cnt, sizeof cnt, "%d nodes", (int)ns.size());
        int top = ui::header(c, "Nodes", ui::BrightMagenta, cnt);
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)ns.size(), [&](int i) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "%s%-5s %-13s snr%+3d",
                          ns[i].is_favorite ? "*" : " ",
                          ns[i].short_name.c_str(), ns[i].long_name.c_str(), ns[i].snr);
            return std::string(buf);
        }, ui::White, ui::BrightMagenta);
        ui::footer(c, " enter:DM  f:favorite  c:save contact  esc:back ");
    }
private:
    ui::ListState ls_;
    int rows_ = 1;
};

std::unique_ptr<App> make_launcher() { return std::make_unique<Launcher>(); }
std::unique_ptr<App> make_chat() { return std::make_unique<Chat>(); }
std::unique_ptr<App> make_node_list() { return std::make_unique<NodeList>(); }

} // namespace apps
