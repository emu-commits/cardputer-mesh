// Timer — two modes (Tab toggles): Countdown presets that raise a
// NotificationCenter event on expiry, and a Stopwatch (count-up start/stop/reset
// with laps). Both keep running across app switches because tick() is driven by
// the main loop regardless of which app owns the CYD.
#include "apps/apps.h"
#include <array>
#include <cstdio>
#include <vector>
#include "core/notification_center.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Timer : public App {
    enum Mode { COUNTDOWN, STOPWATCH };
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) ls_.sel = ctx.state->get_int("timer.sel", 1);
    }
    void on_pause(AppContext& ctx) override { if (ctx.state) ctx.state->set_int("timer.sel", ls_.sel); }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (k.is_char() && k.ch == '\t') { mode_ = (mode_ == COUNTDOWN) ? STOPWATCH : COUNTDOWN; return true; }
        if (mode_ == COUNTDOWN) return countdown_key(ctx, k);
        return stopwatch_key(ctx, k);
    }

    void tick(AppContext& ctx) override {
        if (running_ && ctx.now_ms >= end_ms_) {
            running_ = false;
            if (ctx.notify)
                ctx.notify->add_event(nc::NotifType::Timer, "timer", PRESETS[fired_idx_].first + " elapsed", ctx.now_ms);
        }
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        if (mode_ == COUNTDOWN) render_countdown(ctx, c); else render_stopwatch(ctx, c);
    }

private:
    static std::string mmss(int s) { char b[12]; if (s < 0) s = 0; std::snprintf(b, sizeof b, "%02d:%02d", s / 60, s % 60); return b; }

    // ---- countdown ----
    bool countdown_key(AppContext& ctx, const KeyEvent& k) {
        if (ls_.move(k, (int)PRESETS.size(), rows_)) return true;
        if (k.key == Key::Enter || (k.is_char() && k.ch == ' ')) {
            if (running_) running_ = false;
            else { running_ = true; fired_idx_ = ls_.sel; end_ms_ = ctx.now_ms + PRESETS[ls_.sel].second * 1000u; }
            return true;
        }
        if (k.is_char() && (k.ch == 's' || k.ch == 'S')) { running_ = false; return true; }
        return false;
    }
    void render_countdown(AppContext& ctx, TextCanvas& c) {
        std::string right = running_ ? ("running " + mmss((int)((end_ms_ - ctx.now_ms) / 1000))) : "idle";
        int top = ui::header(c, "Timer  (countdown)", ui::BrightBlue, right);
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)PRESETS.size(), [&](int i) { return PRESETS[i].first; }, ui::White, ui::BrightBlue);
        ui::footer(c, running_ ? " enter/s:stop  tab:stopwatch  esc:back "
                               : " enter:start  tab:stopwatch  esc:back ");
    }

    // ---- stopwatch ----
    bool stopwatch_key(AppContext& ctx, const KeyEvent& k) {
        if (k.key == Key::Enter || (k.is_char() && k.ch == ' ')) {
            if (sw_run_) { sw_accum_ += ctx.now_ms - sw_start_; sw_run_ = false; }
            else { sw_start_ = ctx.now_ms; sw_run_ = true; }
            return true;
        }
        if (k.is_char() && (k.ch == 'l')) { if (sw_run_ || sw_accum_) laps_.push_back(elapsed(ctx)); return true; }
        if (k.is_char() && (k.ch == 'r')) { sw_run_ = false; sw_accum_ = 0; laps_.clear(); return true; }
        if (ls_.move(k, (int)laps_.size(), rows_)) return true;
        return false;
    }
    uint32_t elapsed(AppContext& ctx) { return sw_accum_ + (sw_run_ ? ctx.now_ms - sw_start_ : 0); }
    void render_stopwatch(AppContext& ctx, TextCanvas& c) {
        int top = ui::header(c, "Timer  (stopwatch)", ui::BrightBlue, sw_run_ ? "running" : "stopped");
        uint32_t e = elapsed(ctx);
        char big[40]; std::snprintf(big, sizeof big, "%02u:%02u.%u", e / 60000, (e / 1000) % 60, (e % 1000) / 100);
        c.text(top + 1, 4, std::string("  ") + big, ui::BrightWhite, ui::Black, ui::ATTR_BOLD);
        rows_ = ui::body_bottom(c) - (top + 3) + 1;
        ui::list(c, top + 3, rows_, ls_, (int)laps_.size(), [&](int i) {
            uint32_t l = laps_[i];
            char b[48]; std::snprintf(b, sizeof b, "lap %d   %02u:%02u.%u", i + 1, l / 60000, (l / 1000) % 60, (l % 1000) / 100);
            return std::string(b);
        }, ui::Gray, ui::BrightBlue);
        ui::footer(c, " enter:start/stop  l:lap  r:reset  tab:countdown ");
    }

    using Preset = std::pair<std::string, uint32_t>;
    static const std::array<Preset, 6> PRESETS;
    Mode mode_ = COUNTDOWN;
    ui::ListState ls_;
    int rows_ = 1;
    bool running_ = false;
    uint32_t end_ms_ = 0;
    int fired_idx_ = 0;
    // stopwatch
    bool sw_run_ = false;
    uint32_t sw_start_ = 0, sw_accum_ = 0;
    std::vector<uint32_t> laps_;
};

const std::array<Timer::Preset, 6> Timer::PRESETS = {{
    {"5 sec  (demo)", 5}, {"1 min", 60}, {"3 min", 180},
    {"5 min", 300}, {"10 min", 600}, {"25 min  (pomodoro)", 1500},
}};

std::unique_ptr<App> make_timer() { return std::make_unique<Timer>(); }

} // namespace apps
