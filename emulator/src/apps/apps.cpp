#include "apps/apps.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
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
        int top = ui::header(c, "Cardputer Deck", ui::BrightGreen, hhmm(ctx.now_ms));
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
        // wordless cyberpunk motif: antennas + neon data-tower + grid floor
        static const char* art[] = {
            "     .    .",
            "  )  |  : |  (",
            " (( -+- : +- ))",
            "    _|__:__|_",
            "   /========\\",
            "   | ## ## # |",
            "   | ## ## # |",
            "   |_##_##_#_|",
            "    | |  | |",
            "  =:+=+==+=+:=",
            "   /_/_/\\_\\_\\",
        };
        int rows = (int)(sizeof(art) / sizeof(art[0]));
        for (int i = 0; i < rows; ++i) {
            int r = top + i;
            if (r > c.height() - 3) break;
            uint8_t col = (i < 4) ? ui::BrightCyan : (i < 9) ? ui::BrightMagenta : ui::BrightBlue;
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
        if (help_) { help_ = false; return true; } // any key closes help
        if (win_open_) return win_key(ctx, k);
        if (k.key == Key::Tab) { win_open_ = true; return true; }
        if (k.key == Key::Enter) {
            if (!compose_.empty()) {
                if (compose_[0] == '/') handle_command(ctx, compose_);     // irssi-style command
                else if (dm_) ctx.mesh->send_text(target_dm_, 0, compose_);
                else ctx.mesh->send_text(mesh::BROADCAST, (uint8_t)target_ch_, compose_);
                compose_.clear();
            }
            return true;
        }
        if (k.key == Key::Backspace) { if (!compose_.empty()) compose_.pop_back(); return true; }
        if (k.key == Key::Up) { if (scroll_ < scroll_max_) scroll_++; return true; } // scroll into history
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
            {"Commands help", [this](AppContext&) { help_ = true; }},
            {"Paste into message", [this](AppContext& c) { if (c.clip) compose_ += c.clip->get(); }},
            {"Clear message", [this](AppContext&) { compose_.clear(); }},
        };
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        process_new(ctx);
        unread_[win_key()] = 0; // the window you're looking at is read
        std::string title = dm_ ? ("DM " + name_for(ctx, target_dm_)) : ("#" + chan_name(ctx, target_ch_));
        int other = total_unread();
        std::string right = other > 0 ? ("tab:windows (" + std::to_string(other) + ")") : "tab:windows";
        int top = ui::header(c, title, ui::BrightCyan, right);

        int bottom = c.height() - 3;
        int rows = bottom - top + 1;
        mesh::LogQuery q = cur_query(ctx);
        scroll_max_ = ctx.log->match_count(q) - rows; if (scroll_max_ < 0) scroll_max_ = 0;
        if (scroll_ > scroll_max_) scroll_ = scroll_max_;
        auto msgs = ctx.log->window(q, rows, scroll_); // only this window is materialized
        int row = top;
        for (auto& m : msgs) {
            if (row > bottom) break;
            if (!m.outgoing && ctx.mesh->is_ignored(m.from_id)) continue; // hide ignored senders (#2)
            std::string who = m.outgoing ? ctx.mesh->our_short() : m.from_name;
            std::string st = " ";
            if (m.outgoing) st = (m.ack == mesh::ACK_PENDING) ? "·" : (m.ack == mesh::ACK_OK) ? "✓"
                                : (m.ack == mesh::ACK_FAIL) ? "x" : " ";
            std::string line = "[" + hhmm(m.ts_ms) + "]" + st + who + ": " + m.text;
            uint8_t fg = m.outgoing ? ui::BrightGreen : ui::White;
            c.text(row, 1, ui::fit(line, c.width() - 2), fg, ui::Black);
            ++row;
        }
        ui::input_line(c, c.height() - 2, 1, "> ", compose_);
        ui::footer(c, " enter:send  tab:win  /help  esc:back ");
        if (win_open_) render_windows(ctx, c);
        if (help_) render_help(c);
    }

private:
    void consume_intent(AppContext& ctx) {
        if (ctx.nav_arg.rfind("dm:", 0) == 0) {
            uint32_t id = (uint32_t)std::strtoul(ctx.nav_arg.c_str() + 3, nullptr, 10);
            open_dm(id); dm_ = true; target_dm_ = id; scroll_ = 0;
            ctx.nav_arg.clear();
        }
    }
    mesh::LogQuery cur_query(AppContext& ctx) {
        mesh::LogQuery q; q.our_id = ctx.mesh->our_id();
        if (dm_) { q.dm = true; q.peer = target_dm_; } else { q.dm = false; q.channel = (uint8_t)target_ch_; }
        return q;
    }
    std::string win_key() { return dm_ ? ("d" + std::to_string(target_dm_)) : ("c" + std::to_string(target_ch_)); }
    int total_unread() { int n = 0; for (auto& kv : unread_) n += kv.second; return n; }
    // Scan messages arrived since last time: auto-open DM windows and tally
    // per-window unread for anything not in the window currently on screen.
    void process_new(AppContext& ctx) {
        std::string cur = win_key();
        seen_ = ctx.log->scan_from(seen_, [&](const mesh::Message& m) {
            if (m.outgoing) return;
            if (ctx.mesh->is_ignored(m.from_id)) return; // ignored node: no auto-open / no unread (#2)
            std::string key;
            if (m.dest == ctx.mesh->our_id()) { open_dm(m.from_id); key = "d" + std::to_string(m.from_id); }
            else if (m.dest == mesh::BROADCAST) key = "c" + std::to_string((int)m.channel);
            else return;
            if (key != cur) unread_[key]++;
        });
    }
    void handle_command(AppContext& ctx, const std::string& line) {
        std::vector<std::string> tok; size_t i = 0;
        while (i < line.size()) {
            size_t sp = line.find(' ', i);
            std::string t = line.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            if (!t.empty()) tok.push_back(t);
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        if (tok.empty()) return;
        std::string cmd = tok[0];
        for (auto& ch : cmd) ch = (char)std::tolower((int)ch);
        if (cmd == "/help" || cmd == "/?") { help_ = true; }
        else if (cmd == "/close" || cmd == "/wc") { if (dm_) close_dm(target_dm_); }
        else if (cmd == "/win" && tok.size() >= 2) {
            build_wins(ctx); int idx = std::atoi(tok[1].c_str()) - 1;
            if (idx >= 0 && idx < (int)wins_.size()) {
                const Win& w = wins_[idx];
                if (w.dm) { dm_ = true; target_dm_ = w.id; } else { dm_ = false; target_ch_ = (int)w.id; }
                scroll_ = 0;
            }
        }
        else if ((cmd == "/msg" || cmd == "/query" || cmd == "/q") && tok.size() >= 2) {
            std::string want = tok[1]; for (auto& ch : want) ch = (char)std::tolower((int)ch);
            uint32_t id = 0;
            for (auto& n : ctx.mesh->nodes()) {
                std::string s = n.short_name, l = n.long_name;
                for (auto& ch : s) ch = (char)std::tolower((int)ch);
                for (auto& ch : l) ch = (char)std::tolower((int)ch);
                if (s == want || l == want) { id = n.id; break; }
            }
            if (id) {
                open_dm(id); dm_ = true; target_dm_ = id; scroll_ = 0;
                if (tok.size() >= 3) { // remaining words = message
                    std::string msg = line.substr(line.find(tok[1]) + tok[1].size());
                    size_t a = msg.find_first_not_of(' '); if (a != std::string::npos) ctx.mesh->send_text(id, 0, msg.substr(a));
                }
            }
        }
    }
    void render_help(TextCanvas& c) {
        int ir, ic, iw, ih;
        ui::modal_box(c, 9, 44, "Chat commands", ui::BrightCyan, ir, ic, iw, ih, "any key closes");
        const char* lines[] = {
            "/win N      switch to window N",
            "/msg name [text]  open/send a DM",
            "/close      close the current DM",
            "/help       this list",
            "tab or ^W   window switcher",
        };
        for (int i = 0; i < 5; ++i) c.text(ir + i, ic, ui::fit(lines[i], iw), ui::White, ui::Black);
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
    std::string badge(const std::string& key) {
        auto it = unread_.find(key);
        return (it != unread_.end() && it->second > 0) ? ("  (" + std::to_string(it->second) + ")") : "";
    }
    void build_wins(AppContext& ctx) {
        wins_.clear();
        auto cs = chans(ctx);
        for (int i = 0; i < (int)cs.size(); ++i)
            wins_.push_back({"#" + cs[i] + badge("c" + std::to_string(i)), false, (uint32_t)i});
        for (uint32_t id : open_dms_)
            wins_.push_back({"@" + name_for(ctx, id) + badge("d" + std::to_string(id)), true, id});
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
    int scroll_ = 0, scroll_max_ = 0;
    bool dm_ = false;
    int target_ch_ = 0;
    uint32_t target_dm_ = 0;
    std::vector<uint32_t> open_dms_;
    size_t seen_ = 0;
    bool win_open_ = false;
    bool help_ = false;
    std::map<std::string, int> unread_;
    ui::ListState win_ls_;
    std::vector<Win> wins_;
};

// ---- Node list -------------------------------------------------------------
class NodeList : public App {
    enum Overlay { NONE, INFO, TRACE };
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) ls_.sel = ctx.state->get_int("nodes.sel", 0);
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
    void grab_sel(AppContext& ctx) {
        auto ns = sorted(ctx);
        if (ns.empty()) return;
        const mesh::Node& s = ns[ls_.sel];
        sel_id_ = s.id; sel_long_ = s.long_name; sel_short_ = s.short_name; sel_snr_ = s.snr; sel_heard_ = s.last_heard_ms;
    }
    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (overlay_ != NONE) {
            // info modal follows the highlighted row; traceroute stays put
            if (overlay_ == INFO && (k.key == Key::Up || k.key == Key::Down)) {
                ls_.move(k, (int)sorted(ctx).size(), rows_); grab_sel(ctx); return true;
            }
            if (k.key == Key::Esc || k.key == Key::Enter) overlay_ = NONE;
            return true;
        }
        auto ns = sorted(ctx);
        if (ls_.move(k, (int)ns.size(), rows_)) return true;
        if (ns.empty()) return false;
        const mesh::Node& sel = ns[ls_.sel];
        uint32_t id = sel.id;
        sel_id_ = id; sel_long_ = sel.long_name; sel_short_ = sel.short_name; sel_snr_ = sel.snr; sel_heard_ = sel.last_heard_ms;
        if (k.key == Key::Enter) { ctx.nav_arg = "dm:" + std::to_string(id); ctx.apps->request_switch("chat"); return true; }
        if (k.is_char()) {
            if (k.ch == 'f') { ctx.mesh->set_favorite(id, !ctx.mesh->is_favorite(id)); persist_flags(ctx); return true; }
            if (k.ch == 'x') { ctx.mesh->set_ignored(id, !ctx.mesh->is_ignored(id)); persist_flags(ctx); return true; }
            if (k.ch == 'i') { overlay_ = INFO; return true; }
            if (k.ch == 't') { ctx.mesh->request_traceroute(id); overlay_ = TRACE; return true; }
            if (k.ch == 'c') {
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
            char mark = ns[i].is_favorite ? '*' : (ctx.mesh->is_ignored(ns[i].id) ? 'x' : ' ');
            char buf[96];
            std::snprintf(buf, sizeof buf, "%c%-5s %-13s snr%+3d",
                          mark, ns[i].short_name.c_str(), ns[i].long_name.c_str(), ns[i].snr);
            return std::string(buf);
        }, ui::White, ui::BrightMagenta);
        ui::footer2(c, " enter:DM  i:info  t:traceroute  f:fav  x:ignore ",
                       " c:save contact   up/dn move   esc:back ");
        if (overlay_ == INFO) render_info(ctx, c);
        else if (overlay_ == TRACE) render_trace(ctx, c);
    }
private:
    // Persist the favorite/ignore node-id sets so they survive quit/reload (#1).
    // On a real Meshtastic node these live in the node DB; the emulator's stub
    // keeps them in RAM, so we mirror them to the state store and re-apply at
    // boot (see main.cpp). CSV of node ids.
    void persist_flags(AppContext& ctx) {
        if (!ctx.state) return;
        std::string favs, ign;
        for (auto& n : ctx.mesh->nodes()) {
            if (ctx.mesh->is_favorite(n.id)) { if (!favs.empty()) favs += ','; favs += std::to_string(n.id); }
            if (ctx.mesh->is_ignored(n.id))  { if (!ign.empty())  ign  += ','; ign  += std::to_string(n.id); }
        }
        ctx.state->set("nodes.favs", favs);
        ctx.state->set("nodes.ignored", ign);
    }
    std::string name_for(AppContext& ctx, uint32_t id) {
        if (id == ctx.mesh->our_id()) return ctx.mesh->our_short() + "(me)";
        for (auto& n : ctx.mesh->nodes()) if (n.id == id) return n.short_name.empty() ? n.long_name : n.short_name;
        char b[12]; std::snprintf(b, sizeof b, "!%08x", id); return b;
    }
    void render_info(AppContext& ctx, TextCanvas& c) {
        int ir, ic, iw, ih;
        ui::modal_box(c, 10, 42, "Node info", ui::BrightMagenta, ir, ic, iw, ih, "esc:close");
        char idb[16]; std::snprintf(idb, sizeof idb, "!%08x", sel_id_);
        c.text(ir + 0, ic, ui::fit(sel_long_ + " (" + sel_short_ + ")", iw), ui::White, ui::Black);
        c.text(ir + 1, ic, std::string("id:  ") + idb, ui::Gray, ui::Black);
        char snr[24]; std::snprintf(snr, sizeof snr, "snr: %+d dB", sel_snr_);
        c.text(ir + 2, ic, snr, ui::White, ui::Black);
        std::string heard = sel_heard_ ? (std::to_string((ctx.now_ms - sel_heard_) / 1000) + "s ago") : "(not heard yet)";
        c.text(ir + 3, ic, "heard: " + heard, ui::White, ui::Black);
        c.text(ir + 4, ic, std::string("favorite: ") + (ctx.mesh->is_favorite(sel_id_) ? "yes" : "no") +
                            "   ignored: " + (ctx.mesh->is_ignored(sel_id_) ? "yes" : "no"), ui::White, ui::Black);
        // Battery + position (populated by the real mesh backend; absent on stub).
        for (auto& n : ctx.mesh->nodes()) if (n.id == sel_id_) {
            if (n.battery >= 0) { char b[24]; std::snprintf(b, sizeof b, "batt: %d%%", n.battery); c.text(ir + 5, ic, b, ui::White, ui::Black); }
            if (n.has_pos) { char b[40]; std::snprintf(b, sizeof b, "pos: %.4f, %.4f", n.lat, n.lon); c.text(ir + 6, ic, b, ui::White, ui::Black); }
            break;
        }
    }
    void render_trace(AppContext& ctx, TextCanvas& c) {
        int ir, ic, iw, ih;
        ui::modal_box(c, 7, 46, "Traceroute", ui::BrightMagenta, ir, ic, iw, ih, "esc:close");
        c.text(ir, ic, ui::fit("to " + sel_long_, iw), ui::White, ui::Black);
        mesh::TraceRoute tr;
        if (!ctx.mesh->get_traceroute(sel_id_, tr)) { c.text(ir + 2, ic, "(no traceroute)", ui::Gray, ui::Black); return; }
        if (tr.pending) { c.text(ir + 2, ic, "requesting route...", ui::BrightYellow, ui::Black); return; }
        std::string path;
        for (size_t i = 0; i < tr.route.size(); ++i) { if (i) path += " > "; path += name_for(ctx, tr.route[i]); }
        // wrap path across up to 3 inner rows
        for (int r = 0; r < ih - 1 && !path.empty(); ++r) {
            c.text(ir + 1 + r, ic, path.substr(0, iw), ui::BrightGreen, ui::Black);
            path = path.size() > (size_t)iw ? path.substr(iw) : "";
        }
    }
    ui::ListState ls_;
    int rows_ = 1;
    Overlay overlay_ = NONE;
    uint32_t sel_id_ = 0, sel_heard_ = 0;
    int sel_snr_ = 0;
    std::string sel_long_, sel_short_;
};

std::unique_ptr<App> make_launcher() { return std::make_unique<Launcher>(); }
std::unique_ptr<App> make_chat() { return std::make_unique<Chat>(); }
std::unique_ptr<App> make_node_list() { return std::make_unique<NodeList>(); }

} // namespace apps
