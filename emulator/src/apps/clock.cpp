// Clock — local time + a world clock for travelers (pairs with the region
// presets). No alarms: an alarm can't fire while the device is powered off, so
// it would be misleading. Timer/stopwatch live in the Timer app.
#include "apps/apps.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Clock : public App {
    struct Zone { std::string label; int off_min; }; // fixed UTC offset (minutes)
    enum Overlay { NONE, ADD };
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) load(ctx.state->get("clock.zones", ""));
        if (zones_.empty()) zones_ = {{"London", 0}, {"New York", -300}, {"Tokyo", 540}};
    }
    void on_pause(AppContext& ctx) override { if (ctx.state) ctx.state->set("clock.zones", dump()); }

    bool on_key(AppContext&, const KeyEvent& k) override {
        if (overlay_ == ADD) {
            if (k.key == Key::Esc) { overlay_ = NONE; return true; }
            if (k.key == Key::Enter) { commit(); overlay_ = NONE; return true; }
            if (k.key == Key::Backspace) { if (!ibuf_.empty()) ibuf_.pop_back(); return true; }
            if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) ibuf_ += (char)k.ch;
            return true;
        }
        if (ls_.move(k, (int)zones_.size(), rows_)) return true;
        if (k.is_char() && k.ch == 'a') { overlay_ = ADD; ibuf_.clear(); return true; }
        if (k.is_char() && k.ch == 'd' && !zones_.empty()) {
            zones_.erase(zones_.begin() + ls_.sel); ls_.clamp((int)zones_.size(), rows_); return true;
        }
        return false;
    }

    void render(AppContext&, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        std::time_t now = std::time(nullptr);
        std::tm lt{}; localtime_r(&now, &lt);
        char date[32]; std::snprintf(date, sizeof date, "%04d-%02d-%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
        int top = ui::header(c, "Clock", ui::BrightGreen, date);
        char big[16]; std::snprintf(big, sizeof big, "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
        c.text(top, 3, std::string("  ") + big + "  local", ui::BrightWhite, ui::Black, ui::ATTR_BOLD);
        c.hline(top + 1, 1, c.width() - 2, U'-', ui::Gray, ui::Black);

        rows_ = ui::body_bottom(c) - (top + 2) + 1;
        ui::list(c, top + 2, rows_, ls_, (int)zones_.size(), [&](int i) {
            std::time_t z = now + zones_[i].off_min * 60;
            std::tm g{}; gmtime_r(&z, &g);
            char hm[8]; std::snprintf(hm, sizeof hm, "%02d:%02d", g.tm_hour, g.tm_min);
            int oh = zones_[i].off_min / 60, om = std::abs(zones_[i].off_min % 60);
            char off[16]; std::snprintf(off, sizeof off, "%+03d:%02d", oh, om);
            return std::string(hm) + "  " + ui::fit(zones_[i].label, 16) + " UTC" + off;
        }, ui::White, ui::BrightGreen);
        ui::footer(c, " a:add zone   d:delete   up/dn   esc:back ");
        if (overlay_ == ADD) {
            int ir, ic, iw, ih;
            ui::modal_box(c, 6, 44, "Add zone", ui::BrightGreen, ir, ic, iw, ih, "format:  +HH:MM Label");
            c.text(ir, ic, "e.g.  -08:00 Los Angeles", ui::Gray, ui::Black, ui::ATTR_DIM);
            ui::input_line(c, ir + 2, ic, "> ", ibuf_, ui::BrightWhite, iw);
        }
    }

private:
    void commit() {
        size_t a = ibuf_.find_first_not_of(' '); if (a == std::string::npos) return;
        std::string s = ibuf_.substr(a);
        // parse leading "+HH:MM" or "+HH" then label
        int sign = 1; size_t i = 0;
        if (s[0] == '+' || s[0] == '-') { sign = s[0] == '-' ? -1 : 1; i = 1; }
        int hh = 0, mm = 0, consumed = 0;
        if (std::sscanf(s.c_str() + i, "%d:%d%n", &hh, &mm, &consumed) >= 1) {
            size_t rest = i + consumed;
            std::string label = (rest < s.size()) ? s.substr(rest) : "";
            size_t b = label.find_first_not_of(' '); label = (b == std::string::npos) ? "zone" : label.substr(b);
            zones_.push_back({label, sign * (hh * 60 + mm)});
        }
    }
    std::string dump() const {
        std::string s; for (auto& z : zones_) s += z.label + "\t" + std::to_string(z.off_min) + "\n";
        return s;
    }
    void load(const std::string& s) {
        zones_.clear(); size_t i = 0;
        while (i < s.size()) {
            size_t nl = s.find('\n', i);
            std::string ln = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            size_t t = ln.find('\t');
            if (t != std::string::npos) zones_.push_back({ln.substr(0, t), std::atoi(ln.substr(t + 1).c_str())});
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
    }

    std::vector<Zone> zones_;
    ui::ListState ls_;
    int rows_ = 1;
    Overlay overlay_ = NONE;
    std::string ibuf_;
};

std::unique_ptr<App> make_clock() { return std::make_unique<Clock>(); }

} // namespace apps
