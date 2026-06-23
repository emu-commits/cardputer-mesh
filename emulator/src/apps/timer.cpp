// Timer — countdown presets that raise a NotificationCenter event on expiry.
// This is the concrete §6.5/§14.2 wiring: a foreground app schedules work whose
// completion surfaces on the built-in screen even after you've switched away
// (the timer keeps running because tick() is driven by the main loop regardless
// of which app owns the CYD). UX: a preset list (§5.1) + a running banner.
#include "apps/apps.h"
#include <array>
#include "core/notification_center.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Timer : public App {
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) ls_.sel = ctx.state->get_int("timer.sel", 1);
        ls_.clamp((int)PRESETS.size(), 1);
    }
    void on_pause(AppContext& ctx) override { if (ctx.state) ctx.state->set_int("timer.sel", ls_.sel); }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (ls_.move(k, (int)PRESETS.size(), rows_)) return true;
        if (k.key == Key::Enter || (k.is_char() && k.ch == ' ')) {
            if (running_) stop();
            else start(ctx.now_ms, ls_.sel);
            return true;
        }
        if (k.is_char() && (k.ch == 's' || k.ch == 'S')) { stop(); return true; }
        return false;
    }

    void tick(AppContext& ctx) override {
        if (running_ && ctx.now_ms >= end_ms_) {
            running_ = false;
            if (ctx.notify)
                ctx.notify->add_event(nc::NotifType::Timer, "timer",
                                      PRESETS[fired_idx_].first + " elapsed", ctx.now_ms);
        }
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        std::string right = "idle";
        if (running_) {
            int rem = (int)((end_ms_ - ctx.now_ms) / 1000);
            if (rem < 0) rem = 0;
            char b[16]; std::snprintf(b, sizeof b, "%02d:%02d", rem / 60, rem % 60);
            right = std::string("running ") + b;
        }
        int top = ui::header(c, "Timer", ui::BrightBlue, right);
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)PRESETS.size(),
                 [&](int i) { return PRESETS[i].first; }, ui::White, ui::BrightBlue);
        ui::footer(c, running_ ? " [enter]/[s] stop  [up/dn] select  [esc] back "
                               : " [enter] start  [up/dn] select  [esc] back ");
    }

private:
    void start(uint32_t now, int idx) { running_ = true; fired_idx_ = idx; end_ms_ = now + PRESETS[idx].second * 1000u; }
    void stop() { running_ = false; }

    using Preset = std::pair<std::string, uint32_t>; // label, seconds
    static const std::array<Preset, 6> PRESETS;
    ui::ListState ls_;
    int rows_ = 1;
    bool running_ = false;
    uint32_t end_ms_ = 0;
    int fired_idx_ = 0;
};

const std::array<Timer::Preset, 6> Timer::PRESETS = {{
    {"5 sec  (demo)", 5}, {"1 min", 60}, {"3 min", 180},
    {"5 min", 300}, {"10 min", 600}, {"25 min  (pomodoro)", 1500},
}};

std::unique_ptr<App> make_timer() { return std::make_unique<Timer>(); }

} // namespace apps
