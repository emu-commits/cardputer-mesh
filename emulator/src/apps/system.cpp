// System / About — device status at a glance: identity, radio config, uptime,
// storage, battery, message/node counts. Most fields are portable; battery and
// free-RAM are device-HAL values (the emulator shows honest placeholders).
#include "apps/apps.h"
#include <cstdio>
#include <ctime>
#include <vector>
#include "core/fs.h"
#include "core/mesh.h"
#include "core/notification_center.h"
#include "core/settings.h"
#include "core/ui_kit.h"
#include "core/wallclock.h"

#define FW_VERSION "0.3.0-dev"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class System : public App {
    enum Overlay { NONE, SET_DT };
public:
    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (overlay_ == SET_DT) {
            if (k.key == Key::Esc) { overlay_ = NONE; return true; }
            if (k.key == Key::Enter) { commit_dt(ctx); overlay_ = NONE; return true; }
            if (k.key == Key::Backspace) { if (!ibuf_.empty()) ibuf_.pop_back(); return true; }
            if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) ibuf_ += (char)k.ch;
            return true;
        }
        if (k.is_char() && (k.ch == 't' || k.ch == 'T')) { overlay_ = SET_DT; ibuf_.clear(); return true; }
        return ls_.move(k, n_, rows_);
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        int top = ui::header(c, "System", ui::BrightCyan, FW_VERSION);

        std::vector<std::pair<std::string, std::string>> rows;
        auto add = [&](const char* k, const std::string& v) { rows.push_back({k, v}); };

        add("Device", "Cardputer ADV (ESP32-S3)");
        add("Firmware", FW_VERSION);
        { std::time_t nw = wallclock::now(); std::tm lt{}; localtime_r(&nw, &lt);
          char dt[64]; std::snprintf(dt, sizeof dt, "%04d-%02d-%02d %02d:%02d",
              lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
          add("Date/time", dt); }
        if (ctx.mesh) {
            char id[16]; std::snprintf(id, sizeof id, "!%08x", ctx.mesh->our_id());
            add("Node", ctx.mesh->our_long() + " " + id);
            add("Nodes known", std::to_string(ctx.mesh->nodes().size()));
        }
        if (ctx.settings)
            add("Radio", ctx.settings->get("lora", "region") + " / " + ctx.settings->get("lora", "modem_preset"));
        if (ctx.log) add("Messages", std::to_string(ctx.log->count()));
        uint32_t up = ctx.now_ms / 1000;
        char ub[24]; std::snprintf(ub, sizeof ub, "%uh %02um %02us", up / 3600, (up / 60) % 60, up % 60);
        add("Uptime", ub);
        if (ctx.fs && ctx.fs->total_bytes()) {
            char sb[40];
            std::snprintf(sb, sizeof sb, "%llu / %llu MB free",
                          (unsigned long long)(ctx.fs->free_bytes() >> 20),
                          (unsigned long long)(ctx.fs->total_bytes() >> 20));
            add("Storage", sb);
        }
        add("Battery", ctx.notify ? ctx.notify->battery() : "USB");
        add("Free RAM", ctx.notify ? ctx.notify->ram() : "(device HAL)"); // free/min/largest-block on hardware

        n_ = (int)rows.size();
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, n_, [&](int i) {
            return ui::fit(rows[i].first, 14) + rows[i].second;
        }, ui::White, ui::BrightCyan);
        ui::footer(c, " up/dn:scroll   t:set date/time   esc:back ");

        if (overlay_ == SET_DT) {
            int ir, ic, iw, ih;
            ui::modal_box(c, 6, 44, "Set date & time", ui::BrightCyan, ir, ic, iw, ih,
                          "YYYY-MM-DD HH:MM  enter:set  esc:cancel");
            std::time_t nw = wallclock::now(); std::tm lt{}; localtime_r(&nw, &lt);
            char ex[40]; std::snprintf(ex, sizeof ex, "e.g.  %04d-%02d-%02d %02d:%02d",
                lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
            c.text(ir, ic, ex, ui::Gray, ui::Black, ui::ATTR_DIM);
            ui::input_line(c, ir + 2, ic, "> ", ibuf_, ui::BrightWhite, iw);
        }
    }

private:
    // Set the wall clock from "YYYY-MM-DD HH:MM[:SS]" (or "HH:MM[:SS]" for today).
    void commit_dt(AppContext& ctx) {
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
    int n_ = 0, rows_ = 1;
    Overlay overlay_ = NONE;
    std::string ibuf_;
};

std::unique_ptr<App> make_system() { return std::make_unique<System>(); }

} // namespace apps
