#include "apps/apps.h"
#include <cstdio>
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
class Launcher : public App {
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) ls_.sel = ctx.state->get_int("launcher.sel", 0);
        ls_.clamp((int)ctx.apps->list().size(), 1);
    }
    void on_pause(AppContext& ctx) override {
        if (ctx.state) ctx.state->set_int("launcher.sel", ls_.sel);
    }
    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        auto items = ctx.apps->list();
        if (ls_.move(k, (int)items.size(), rows_)) return true;
        if (k.key == Key::Enter && ls_.sel < (int)items.size())
            ctx.apps->request_switch(items[ls_.sel].first);
        return true;
    }
    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        int top = ui::header(c, "Cardputer Mesh", ui::BrightGreen, hhmm(ctx.now_ms));
        auto items = ctx.apps->list();
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)items.size(),
                 [&](int i) { return items[i].second; }, ui::White, ui::BrightGreen);
        ui::footer(c, " [up/dn] move  [enter] open  [ctrl-p] palette  [ctrl-q] quit ");
    }
private:
    ui::ListState ls_;
    int rows_ = 1;
};

// ---- Chat ------------------------------------------------------------------
class Chat : public App {
public:
    void on_create(AppContext& ctx) override {
        if (ctx.notify) ctx.notify->mark_read();
        if (ctx.state) scroll_ = ctx.state->get_int("chat.scroll", 0);
    }
    void on_pause(AppContext& ctx) override {
        if (ctx.state) ctx.state->set_int("chat.scroll", scroll_);
    }
    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (k.key == Key::Enter) {
            if (!compose_.empty()) { ctx.mesh->send_text(mesh::BROADCAST, 0, compose_); compose_.clear(); }
            return true;
        }
        if (k.key == Key::Backspace) { if (!compose_.empty()) compose_.pop_back(); return true; }
        if (k.key == Key::Up) { scroll_++; return true; }
        if (k.key == Key::Down) { if (scroll_ > 0) scroll_--; return true; }
        if (k.ctrl && (k.ch == 'u' || k.ch == 'U')) { if (ctx.clip) compose_ += ctx.clip->get(); return true; }
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) { compose_ += (char)k.ch; return true; }
        return false;
    }

    std::vector<Command> commands(AppContext&) override {
        return {
            {"Paste into message", [this](AppContext& c) { if (c.clip) compose_ += c.clip->get(); }},
            {"Clear message", [this](AppContext&) { compose_.clear(); }},
        };
    }
    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        int top = ui::header(c, "Mesh chat  #primary", ui::BrightCyan);

        std::vector<const mesh::Message*> rel;
        for (auto& m : ctx.store->all()) {
            bool dm = (m.dest == ctx.mesh->our_id()) || (m.outgoing);
            bool ch = (m.dest == mesh::BROADCAST && m.channel == 0);
            if (dm || ch) rel.push_back(&m);
        }

        int bottom = c.height() - 3; // leave compose + footer
        int rows = bottom - top + 1;
        int total = (int)rel.size();
        int start = total - rows - scroll_;
        if (start < 0) start = 0;
        int row = top;
        for (int i = start; i < total && row <= bottom; ++i, ++row) {
            const mesh::Message& m = *rel[i];
            std::string who = m.outgoing ? ctx.mesh->our_short() : m.from_name;
            std::string tag = (m.dest == ctx.mesh->our_id() && !m.outgoing) ? "*" : " ";
            std::string line = "[" + hhmm(m.ts_ms) + "]" + tag + who + ": " + m.text;
            uint8_t fg = m.outgoing ? ui::BrightGreen : (tag == "*" ? ui::BrightYellow : ui::White);
            c.text(row, 1, ui::fit(line, c.width() - 2), fg, ui::Black);
        }
        ui::input_line(c, c.height() - 2, 1, "> ", compose_);
        ui::footer(c, " type + [enter] send  [up/dn] scroll  [esc] back ");
    }
private:
    std::string compose_;
    int scroll_ = 0;
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
    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        auto ns = ctx.mesh->nodes();
        if (ls_.move(k, (int)ns.size(), rows_)) return true;
        if (k.key == Key::Enter) { ctx.apps->request_switch("chat"); return true; }
        return false;
    }
    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        auto ns = ctx.mesh->nodes();
        char cnt[16]; std::snprintf(cnt, sizeof cnt, "%d nodes", (int)ns.size());
        int top = ui::header(c, "Nodes", ui::BrightMagenta, cnt);
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)ns.size(), [&](int i) {
            char buf[80];
            std::snprintf(buf, sizeof buf, "%-5s %-14s snr%+3d",
                          ns[i].short_name.c_str(), ns[i].long_name.c_str(), ns[i].snr);
            return std::string(buf);
        }, ui::White, ui::BrightMagenta);
        ui::footer(c, " [up/dn] move  [enter] open chat  [esc] back ");
    }
private:
    ui::ListState ls_;
    int rows_ = 1;
};

std::unique_ptr<App> make_launcher() { return std::make_unique<Launcher>(); }
std::unique_ptr<App> make_chat() { return std::make_unique<Chat>(); }
std::unique_ptr<App> make_node_list() { return std::make_unique<NodeList>(); }

} // namespace apps
