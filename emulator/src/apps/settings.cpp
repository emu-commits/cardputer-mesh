// Settings — the Meshtastic config editor, data-driven from cfg::Settings
// (mirrors Plai's settings screen). Three levels, all single lists/overlays per
// §5.1: group list -> item list -> a typed editor overlay (toggle / option
// picker / number stepper / text input). When the mesh backend forbids config
// writes (a live connected node), the screens are read-only.
#include "apps/apps.h"
#include <cstdlib>
#include "core/mesh.h"
#include "core/settings.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class SettingsApp : public App {
    enum Edit { NONE, PICK, NUMBER, TEXT };
public:
    void on_pause(AppContext& ctx) override { if (ctx.settings && ctx.state) ctx.settings->save(*ctx.state); }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (!ctx.settings) return false;
        if (edit_ != NONE) return edit_key(ctx, k);
        if (gi_ < 0) return groups_key(ctx, k);
        return items_key(ctx, k);
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        if (!ctx.settings) { ui::header(c, "Settings", ui::Red, "no config"); return; }
        ro_ = ctx.mesh && !ctx.mesh->config_writable();
        if (gi_ < 0) render_groups(ctx, c); else render_items(ctx, c);
        if (edit_ != NONE) render_editor(c);
    }

private:
    cfg::Group& grp(AppContext& ctx) { return ctx.settings->groups()[gi_]; }

    void rebuild_vis(AppContext& ctx) {
        vis_.clear();
        auto& g = grp(ctx);
        for (int i = 0; i < (int)g.items.size(); ++i) if (g.items[i].visible(g)) vis_.push_back(i);
    }

    // ---- group level ----
    bool groups_key(AppContext& ctx, const KeyEvent& k) {
        int n = (int)ctx.settings->groups().size();
        if (ls_.move(k, n, rows_)) return true;
        if (k.key == Key::Enter && n) { gi_ = ls_.sel; ls2_ = {}; rebuild_vis(ctx); }
        return true;
    }
    void render_groups(AppContext& ctx, TextCanvas& c) {
        int top = ui::header(c, "Settings", ui::BrightGreen, ro_ ? "read-only" : "");
        auto& gs = ctx.settings->groups();
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)gs.size(), [&](int i) { return gs[i].name; },
                 ui::White, ui::BrightGreen);
        ui::footer(c, " enter:open  esc:back ");
    }

    // ---- item level ----
    bool items_key(AppContext& ctx, const KeyEvent& k) {
        rebuild_vis(ctx);
        if (k.key == Key::Esc) { gi_ = -1; return true; } // back to groups (also handled globally, but be explicit)
        if (ls2_.move(k, (int)vis_.size(), rows_)) return true;
        if (k.key == Key::Enter && !vis_.empty()) begin_edit(ctx);
        return true;
    }
    void render_items(AppContext& ctx, TextCanvas& c) {
        rebuild_vis(ctx);
        auto& g = grp(ctx);
        int top = ui::header(c, g.name, ui::BrightGreen, ro_ ? "read-only" : g.ns);
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls2_, (int)vis_.size(), [&](int i) {
            const cfg::Item& it = g.items[vis_[i]];
            std::string v = (it.type == cfg::BOOL) ? (it.value == "1" ? "On" : "Off") : it.value;
            int pad = c.width() - 4 - (int)it.label.size() - (int)v.size();
            if (pad < 1) pad = 1;
            return it.label + std::string(pad, ' ') + v;
        }, ui::White, ui::BrightGreen);
        // hint line for the selected item
        if (!vis_.empty()) {
            const cfg::Item& it = g.items[vis_[ls2_.sel]];
            ui::footer(c, ro_ ? " read-only (live node) — esc:groups "
                              : std::string(" ") + (it.hint.empty() ? "enter:edit  esc:groups" : it.hint));
        } else ui::footer(c, " esc:groups ");
    }

    // ---- editor ----
    void begin_edit(AppContext& ctx) {
        cfg::Item& it = grp(ctx).items[vis_[ls2_.sel]];
        if (ro_) { return; } // read-only: ignore
        if (it.type == cfg::BOOL) { it.value = (it.value == "1") ? "0" : "1"; save(ctx); return; }
        edit_item_ = &it; // stable: the group's item vector isn't resized during edit
        if (it.type == cfg::ENUM) {
            edit_ = PICK;
            opt_.sel = 0;
            for (int i = 0; i < (int)it.options.size(); ++i) if (it.options[i] == it.value) opt_.sel = i;
            opt_.top = 0;
        } else { edit_ = (it.type == cfg::NUMBER) ? NUMBER : TEXT; buf_ = it.value; }
    }

    bool edit_key(AppContext& ctx, const KeyEvent& k) {
        cfg::Item* it = edit_item_;
        if (!it) { edit_ = NONE; return true; }
        if (k.key == Key::Esc) { edit_ = NONE; return true; }
        if (edit_ == PICK) {
            if (opt_.move(k, (int)it->options.size(), 8)) return true;
            if (k.key == Key::Enter) { it->value = it->options[opt_.sel]; edit_ = NONE; save(ctx); }
            return true;
        }
        // NUMBER / TEXT share a text buffer
        if (k.key == Key::Enter) {
            if (edit_ == NUMBER) {
                long v = std::strtol(buf_.c_str(), nullptr, 10);
                if (v < it->min) v = it->min;
                if (v > it->max) v = it->max;
                it->value = std::to_string(v);
            } else it->value = buf_;
            edit_ = NONE; save(ctx); return true;
        }
        if (k.key == Key::Backspace) { if (!buf_.empty()) buf_.pop_back(); return true; }
        if (edit_ == NUMBER) {
            if (k.key == Key::Up || k.key == Key::Down) {
                long v = std::strtol(buf_.c_str(), nullptr, 10) + (k.key == Key::Up ? 1 : -1);
                if (v < it->min) v = it->min;
                if (v > it->max) v = it->max;
                buf_ = std::to_string(v); return true;
            }
            if (k.is_char() && ((k.ch >= '0' && k.ch <= '9') || (k.ch == '-' && buf_.empty()))) buf_ += (char)k.ch;
            return true;
        }
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) buf_ += (char)k.ch;
        return true;
    }
    void render_editor(TextCanvas& c) {
        int ir, ic, iw, ih;
        cfg::Item* it = edit_item_; // set in begin_edit
        std::string title = it ? it->label : "Edit";
        if (edit_ == PICK && it) {
            int h = (int)it->options.size(); if (h > 8) h = 8;
            ui::modal_box(c, h + 3, 30, title, ui::BrightCyan, ir, ic, iw, ih, "enter:set  esc:cancel");
            ui::list(c, ir, ih, opt_, (int)it->options.size(), [&](int i) { return it->options[i]; },
                     ui::White, ui::BrightCyan);
        } else {
            const char* foot = (edit_ == NUMBER) ? "digits / up-dn  enter:ok  esc:cancel"
                                                 : "type  enter:ok  esc:cancel";
            ui::modal_box(c, 5, 40, title, ui::BrightCyan, ir, ic, iw, ih, foot);
            ui::input_line(c, ir + 1, ic, "> ", buf_, ui::BrightWhite, iw);
        }
    }
    void save(AppContext& ctx) { if (ctx.settings && ctx.state) ctx.settings->save(*ctx.state); }

    int gi_ = -1;                 // -1 = group list, else index into groups
    std::vector<int> vis_;        // visible item indices in current group
    ui::ListState ls_, ls2_, opt_;
    int rows_ = 1;
    Edit edit_ = NONE;
    std::string buf_;
    cfg::Item* edit_item_ = nullptr;
    bool ro_ = false;
};

std::unique_ptr<App> make_settings() { return std::make_unique<SettingsApp>(); }

} // namespace apps
