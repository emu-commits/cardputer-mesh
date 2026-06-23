#include "apps/apps.h"
#include <cstdio>
#include <ctime>
#include "core/mesh.h"
#include "core/notification_center.h"

using namespace app;
using ui::Key;
using ui::KeyEvent;
using ui::TextCanvas;

namespace apps {

// ---- helpers ---------------------------------------------------------------
static void footer(TextCanvas& c, const std::string& hint) {
    std::string line(c.width(), ' ');
    for (int i = 0; i < (int)hint.size() && i < c.width(); ++i) line[i] = hint[i];
    c.text(c.height() - 1, 0, line, ui::Black, ui::Cyan, ui::ATTR_INVERSE);
}

static std::string hhmm(uint32_t ts_ms) {
    std::time_t t = std::time(nullptr); // wall clock; ts_ms is monotonic so we just show now
    std::tm tm{}; localtime_r(&t, &tm);
    char b[8]; std::snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
    (void)ts_ms; return b;
}

// ---- Launcher --------------------------------------------------------------
class Launcher : public App {
public:
    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        auto items = ctx.apps->list();
        if (k.key == Key::Up && sel_ > 0) sel_--;
        else if (k.key == Key::Down && sel_ + 1 < (int)items.size()) sel_++;
        else if (k.key == Key::Enter && sel_ < (int)items.size())
            ctx.apps->request_switch(items[sel_].first);
        return true;
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        c.text(0, 1, "Cardputer Mesh  -  Home", ui::BrightGreen, ui::Black, ui::ATTR_BOLD);
        c.hline(1, 1, c.width() - 2, U'-', ui::Gray, ui::Black);
        auto items = ctx.apps->list();
        for (int i = 0; i < (int)items.size(); ++i) {
            bool sel = (i == sel_);
            std::string line = (sel ? " > " : "   ") + items[i].second;
            while ((int)line.size() < c.width() - 2) line += ' ';
            c.text(3 + i, 1, line, sel ? ui::BrightWhite : ui::White, ui::Black,
                   sel ? ui::ATTR_INVERSE : ui::ATTR_NONE);
        }
        footer(c, " [up/dn] move  [enter] open  [ctrl-p] palette  [ctrl-q] quit ");
    }
private:
    int sel_ = 0;
};

// ---- Chat ------------------------------------------------------------------
class Chat : public App {
public:
    void on_create(AppContext& ctx) override { if (ctx.notify) ctx.notify->mark_read(); }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (k.key == Key::Enter) {
            if (!compose_.empty()) {
                ctx.mesh->send_text(mesh::BROADCAST, 0, compose_);
                compose_.clear();
            }
            return true;
        }
        if (k.key == Key::Backspace) { if (!compose_.empty()) compose_.pop_back(); return true; }
        if (k.key == Key::Up) { scroll_++; return true; }
        if (k.key == Key::Down) { if (scroll_ > 0) scroll_--; return true; }
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) { compose_ += (char)k.ch; return true; }
        return false;
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        c.text(0, 1, "Mesh chat  -  #primary", ui::BrightCyan, ui::Black, ui::ATTR_BOLD);
        c.hline(1, 1, c.width() - 2, U'-', ui::Gray, ui::Black);

        // Relevant messages: channel 0 broadcast, DMs to/from us.
        std::vector<const mesh::Message*> rel;
        for (auto& m : ctx.store->all()) {
            bool dm = (m.dest == ctx.mesh->our_id()) || (m.outgoing);
            bool ch = (m.dest == mesh::BROADCAST && m.channel == 0);
            if (dm || ch) rel.push_back(&m);
        }

        int top = 2, bottom = c.height() - 3; // leave compose + footer
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
            if ((int)line.size() > c.width() - 2) line.resize(c.width() - 2);
            uint8_t fg = m.outgoing ? ui::BrightGreen : (tag == "*" ? ui::BrightYellow : ui::White);
            c.text(row, 1, line, fg, ui::Black);
        }

        std::string prompt = "> " + compose_;
        c.text(c.height() - 2, 1, prompt, ui::BrightWhite, ui::Black);
        c.put(c.height() - 2, 1 + (int)prompt.size(), U'_', ui::BrightWhite, ui::Black, ui::ATTR_BOLD);
        footer(c, " type + [enter] send  [up/dn] scroll  [esc] back ");
    }
private:
    std::string compose_;
    int scroll_ = 0;
};

// ---- Node list -------------------------------------------------------------
class NodeList : public App {
public:
    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        auto ns = ctx.mesh->nodes();
        if (k.key == Key::Up && sel_ > 0) { sel_--; return true; }
        if (k.key == Key::Down && sel_ + 1 < (int)ns.size()) { sel_++; return true; }
        if (k.key == Key::Enter) { ctx.apps->request_switch("chat"); return true; }
        return false;
    }
    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        c.text(0, 1, "Nodes", ui::BrightMagenta, ui::Black, ui::ATTR_BOLD);
        c.hline(1, 1, c.width() - 2, U'-', ui::Gray, ui::Black);
        auto ns = ctx.mesh->nodes();
        for (int i = 0; i < (int)ns.size(); ++i) {
            bool sel = (i == sel_);
            char buf[80];
            std::snprintf(buf, sizeof buf, " %-5s %-14s snr%+3d",
                          ns[i].short_name.c_str(), ns[i].long_name.c_str(), ns[i].snr);
            std::string line(buf);
            while ((int)line.size() < c.width() - 2) line += ' ';
            c.text(3 + i, 1, line, sel ? ui::BrightWhite : ui::White, ui::Black,
                   sel ? ui::ATTR_INVERSE : ui::ATTR_NONE);
        }
        footer(c, " [up/dn] move  [enter] open chat  [esc] back ");
    }
private:
    int sel_ = 0;
};

std::unique_ptr<App> make_launcher() { return std::make_unique<Launcher>(); }
std::unique_ptr<App> make_chat() { return std::make_unique<Chat>(); }
std::unique_ptr<App> make_node_list() { return std::make_unique<NodeList>(); }

} // namespace apps
