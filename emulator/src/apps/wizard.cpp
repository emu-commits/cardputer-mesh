// Config wizard — guided first-run provisioning, the on-device alternative to a
// phone app (§ scope: "configured entirely on-device"). Walks the essential
// cfg items in sequence: region -> preset -> freq slot -> hop limit -> long name
// -> short name -> review. Edits the cfg::Settings items in place; on finish it
// saves and marks the device provisioned. Reachable any time; auto-launches on a
// brand-new install (see main.cpp).
#include "apps/apps.h"
#include <cstdlib>
#include "core/settings.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

namespace {
struct Step { const char* ns; const char* key; };
const Step STEPS[] = {
    {"lora", "region"}, {"lora", "modem_preset"}, {"lora", "freq_slot"},
    {"lora", "hop_limit"}, {"node", "long_name"}, {"node", "short_name"},
};
const int NSTEP = (int)(sizeof(STEPS) / sizeof(STEPS[0]));
} // namespace

class Wizard : public App {
public:
    void on_create(AppContext& ctx) override { step_ = 0; if (ctx.settings) init_step(ctx); }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (!ctx.settings) return false;
        if (step_ >= NSTEP) { // review
            if (k.key == Key::Enter) finish(ctx);
            else if (k.key == Key::Left) { step_ = NSTEP - 1; init_step(ctx); }
            return true;
        }
        cfg::Item* it = item(ctx);
        if (k.key == Key::Left) { if (step_ > 0) { step_--; init_step(ctx); } return true; }
        if (k.key == Key::Enter) { commit(it); step_++; if (step_ < NSTEP) init_step(ctx); return true; }
        if (it->type == cfg::ENUM) { opt_.move(k, (int)it->options.size(), 8); return true; }
        // NUMBER / TEXT buffer
        if (k.key == Key::Backspace) { if (!buf_.empty()) buf_.pop_back(); return true; }
        if (it->type == cfg::NUMBER) {
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

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        if (!ctx.settings) { ui::header(c, "Setup", ui::Red, "no config"); return; }
        if (step_ >= NSTEP) { render_review(ctx, c); return; }
        cfg::Item* it = item(ctx);
        char r[24]; std::snprintf(r, sizeof r, "step %d/%d", step_ + 1, NSTEP);
        int top = ui::header(c, "Mesh setup  " + it->label, ui::BrightCyan, r);
        c.text(top, 1, ui::fit(it->hint, c.width() - 2), ui::Gray, ui::Black, ui::ATTR_DIM);
        if (it->type == cfg::ENUM) {
            ui::list(c, top + 2, ui::body_bottom(c) - (top + 2) + 1, opt_, (int)it->options.size(),
                     [&](int i) { return it->options[i]; }, ui::White, ui::BrightCyan);
        } else {
            ui::input_line(c, top + 2, 1, "> ", buf_);
        }
        ui::footer(c, " enter:next  left:back  esc:cancel ");
    }

private:
    cfg::Item* item(AppContext& ctx) {
        for (auto& g : ctx.settings->groups()) if (g.ns == STEPS[step_].ns) return g.find(STEPS[step_].key);
        return nullptr;
    }
    void init_step(AppContext& ctx) {
        cfg::Item* it = item(ctx);
        if (!it) return;
        if (it->type == cfg::ENUM) {
            opt_.sel = 0; opt_.top = 0;
            for (int i = 0; i < (int)it->options.size(); ++i) if (it->options[i] == it->value) opt_.sel = i;
        } else buf_ = it->value;
    }
    void commit(cfg::Item* it) {
        if (!it) return;
        if (it->type == cfg::ENUM) { if (!it->options.empty()) it->value = it->options[opt_.sel]; }
        else if (it->type == cfg::NUMBER) {
            long v = std::strtol(buf_.c_str(), nullptr, 10);
            if (v < it->min) v = it->min;
            if (v > it->max) v = it->max;
            it->value = std::to_string(v);
        } else it->value = buf_;
    }
    void render_review(AppContext& ctx, TextCanvas& c) {
        int top = ui::header(c, "Mesh setup  review", ui::BrightGreen, "done?");
        int r = top;
        for (int i = 0; i < NSTEP; ++i) {
            cfg::Item* it = nullptr;
            for (auto& g : ctx.settings->groups()) if (g.ns == STEPS[i].ns) it = g.find(STEPS[i].key);
            if (it) c.text(r++, 1, ui::fit(it->label + ": " + it->value, c.width() - 2), ui::White, ui::Black);
        }
        ui::footer(c, " enter:save & finish  left:back  esc:cancel ");
    }
    void finish(AppContext& ctx) {
        if (ctx.settings && ctx.state) ctx.settings->save(*ctx.state);
        if (ctx.state) { ctx.state->set("cfg.provisioned", "1"); ctx.state->flush(); }
        ctx.apps->request_switch("launcher");
    }

    int step_ = 0;
    ui::ListState opt_;
    std::string buf_;
};

std::unique_ptr<App> make_wizard() { return std::make_unique<Wizard>(); }

} // namespace apps
