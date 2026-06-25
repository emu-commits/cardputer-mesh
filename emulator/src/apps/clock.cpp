// Clock — local time + a world clock for travelers (pairs with the region
// presets). No alarms: an alarm can't fire while the device is powered off, so
// it would be misleading. Timer/stopwatch live in the Timer app.
//
// The list is a fixed table of world timezones (#4); 's' sets the clock, 'z'
// tags the highlighted zone as the system timezone (#3).
#include "apps/apps.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include "core/ui_kit.h"
#include "core/wallclock.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Clock : public App {
    struct Zone { const char* label; int off_min; }; // fixed UTC offset (minutes)
    enum Overlay { NONE, SET_TIME };
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) home_ = ctx.state->get("clock.home", "");
        // Land the cursor on the tagged system zone, if any.
        for (int i = 0; i < (int)zones().size(); ++i)
            if (home_ == zones()[i].label) { ls_.sel = i; break; }
    }
    void on_pause(AppContext& ctx) override {
        if (ctx.state) ctx.state->set("clock.home", home_);
    }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (overlay_ != NONE) {
            if (k.key == Key::Esc) { overlay_ = NONE; return true; }
            if (k.key == Key::Enter) { commit_time(ctx); overlay_ = NONE; return true; }
            if (k.key == Key::Backspace) { if (!ibuf_.empty()) ibuf_.pop_back(); return true; }
            if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) ibuf_ += (char)k.ch;
            return true;
        }
        if (ls_.move(k, (int)zones().size(), rows_)) return true;
        if (k.is_char() && k.ch == 's') { overlay_ = SET_TIME; ibuf_.clear(); return true; } // set the clock (#6)
        if (k.is_char() && k.ch == 'z') { home_ = zones()[ls_.sel].label; return true; }     // tag system tz (#3)
        return false;
    }

    void render(AppContext&, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        std::time_t now = wallclock::now();               // honor a manually-set clock (#6)
        std::tm lt{}; localtime_r(&now, &lt);
        char date[32]; std::snprintf(date, sizeof date, "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
        int top = ui::header(c, "Clock", ui::BrightGreen, date);
        char big[16]; std::snprintf(big, sizeof big, "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
        std::string tag = home_.empty() ? "local" : ("local (" + home_ + ")");
        c.text(top, 3, std::string("  ") + big + "  " + tag, ui::BrightWhite, ui::Black, ui::ATTR_BOLD);
        c.hline(top + 1, 1, c.width() - 2, U'-', ui::Gray, ui::Black);

        rows_ = ui::body_bottom(c) - (top + 2) + 1;
        ui::list(c, top + 2, rows_, ls_, (int)zones().size(), [&](int i) {
            std::time_t z = now + zones()[i].off_min * 60;
            std::tm g{}; gmtime_r(&z, &g);
            char hm[8]; std::snprintf(hm, sizeof hm, "%02d:%02d", g.tm_hour, g.tm_min);
            int oh = zones()[i].off_min / 60, om = std::abs(zones()[i].off_min % 60);
            char off[16]; std::snprintf(off, sizeof off, "%+03d:%02d", oh, om);
            bool sys = (home_ == zones()[i].label);
            return std::string(sys ? "* " : "  ") + std::string(hm) + "  " +
                   ui::fit(zones()[i].label, 16) + " UTC" + off;
        }, ui::White, ui::BrightGreen);
        ui::footer(c, " s:set date/time  z:set as system tz  *=system  esc ");
        if (overlay_ == SET_TIME) {
            int ir, ic, iw, ih;
            ui::modal_box(c, 6, 44, "Set date & time", ui::BrightGreen, ir, ic, iw, ih,
                          "YYYY-MM-DD HH:MM  (or HH:MM)  enter:set");
            char ex[40]; std::snprintf(ex, sizeof ex, "e.g.  %04d-%02d-%02d %02d:%02d",
                                       lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
            c.text(ir, ic, ex, ui::Gray, ui::Black, ui::ATTR_DIM);
            ui::input_line(c, ir + 2, ic, "> ", ibuf_, ui::BrightWhite, iw);
        }
    }

private:
    // Fixed world-timezone table (standard offsets, DST-agnostic, west to east).
    static const std::vector<Zone>& zones() {
        static const std::vector<Zone> z = {
            {"Honolulu", -600}, {"Anchorage", -540}, {"Los Angeles", -480}, {"Denver", -420},
            {"Chicago", -360}, {"New York", -300}, {"Halifax", -240}, {"Sao Paulo", -180},
            {"Azores", -60}, {"London", 0}, {"Paris", 60}, {"Cairo", 120}, {"Moscow", 180},
            {"Dubai", 240}, {"Karachi", 300}, {"Delhi", 330}, {"Dhaka", 360}, {"Bangkok", 420},
            {"Singapore", 480}, {"Tokyo", 540}, {"Sydney", 600}, {"Noumea", 660}, {"Auckland", 720},
        };
        return z;
    }
    // Set the wall clock. Accepts a full "YYYY-MM-DD HH:MM[:SS]" or, for quick
    // tweaks, just "HH:MM[:SS]" (keeps today's date). Stored as an offset from
    // std::time via wallclock and persisted so it survives an app switch.
    void commit_time(AppContext& ctx) {
        int Y = 0, M = 0, D = 0, hh = 0, mm = 0, ss = 0;
        bool have_date = std::sscanf(ibuf_.c_str(), "%d-%d-%d %d:%d:%d", &Y, &M, &D, &hh, &mm, &ss) >= 5;
        std::tm lt{}; std::time_t cur = wallclock::now(); localtime_r(&cur, &lt);
        if (!have_date) {
            if (std::sscanf(ibuf_.c_str(), "%d:%d:%d", &hh, &mm, &ss) < 2) return;
            Y = lt.tm_year + 1900; M = lt.tm_mon + 1; D = lt.tm_mday;
        }
        if (Y < 1970 || M < 1 || M > 12 || D < 1 || D > 31) return;
        if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) return;
        std::tm t{}; t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D;
        t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss; t.tm_isdst = -1;
        std::time_t epoch = std::mktime(&t);
        if (epoch == (std::time_t)-1) return;
        wallclock::set_epoch(epoch);
        if (ctx.state) { ctx.state->set_int("clock.offset_s", (int)wallclock::offset()); ctx.state->flush(); }
    }

    ui::ListState ls_;
    int rows_ = 1;
    Overlay overlay_ = NONE;
    std::string ibuf_;
    std::string home_;     // tagged system timezone label (#3)
};

std::unique_ptr<App> make_clock() { return std::make_unique<Clock>(); }

} // namespace apps
