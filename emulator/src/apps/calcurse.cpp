// Calcurse — calendar + todo in one app (calcurse/taskwarrior lessons).
// Two views toggled by Tab: TODO (GTD: priority A/B/C + done) and CAL (date-sorted
// appointments). Per §5.1 each view is a single list; the month picker and the
// add prompts are modal overlays. Due appointments raise NotificationCenter
// Reminder events (§6.5) so they surface on the built-in screen.
#include "apps/apps.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>
#include "core/notification_center.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

namespace {
int prio_rank(char p) { return p == 'A' ? 0 : p == 'B' ? 1 : p == 'C' ? 2 : 3; }
char cycle_prio(char p) { return p == 'A' ? 'B' : p == 'B' ? 'C' : p == 'C' ? '-' : 'A'; }
std::string two(int n) { char b[12]; std::snprintf(b, sizeof b, "%02d", n); return b; }
const char* MON[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
} // namespace

class Calcurse : public App {
    struct Todo { char prio; bool done; std::string text; };
    struct Appt { int y, m, d, hh, mm; std::string text; bool fired = false; };
    enum Mode { TODO = 0, CAL = 1 };
    enum Overlay { NONE, ADD_TODO, ADD_APPT, GRID };

public:
    void on_create(AppContext& ctx) override {
        std::time_t t = std::time(nullptr); std::tm tm{}; localtime_r(&t, &tm);
        sy_ = tm.tm_year + 1900; sm_ = tm.tm_mon + 1; sd_ = tm.tm_mday;
        if (ctx.state) {
            mode_ = ctx.state->get_int("calcurse.mode", TODO) == CAL ? CAL : TODO;
            load_todos(ctx.state->get("calcurse.todos", ""));
            load_appts(ctx.state->get("calcurse.appts", ""));
        }
        sort_all();
    }
    void on_pause(AppContext& ctx) override {
        if (!ctx.state) return;
        ctx.state->set_int("calcurse.mode", mode_);
        ctx.state->set("calcurse.todos", dump_todos());
        ctx.state->set("calcurse.appts", dump_appts());
    }

    void tick(AppContext& ctx) override {
        std::time_t now = std::time(nullptr);
        for (auto& a : appts_) {
            if (a.fired) continue;
            std::time_t due = epoch(a);
            if (now >= due && now < due + 120) {
                a.fired = true;
                if (ctx.notify)
                    ctx.notify->add_event(nc::NotifType::Reminder, "calendar",
                                          two(a.hh) + ":" + two(a.mm) + " " + a.text, ctx.now_ms);
            }
        }
    }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (overlay_ != NONE) return overlay_key(ctx, k);
        if (k.key == Key::Tab) { mode_ = (mode_ == TODO) ? CAL : TODO; ls_.sel = 0; ls_.top = 0; return true; }
        return (mode_ == TODO) ? todo_key(k) : cal_key(k);
    }

    void render(AppContext&, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        if (mode_ == TODO) render_todo(c); else render_cal(c);
        if (overlay_ == GRID) render_grid(c);
        else if (overlay_ == ADD_TODO || overlay_ == ADD_APPT) render_prompt(c);
    }

private:
    // ---- TODO view ----
    bool todo_key(const KeyEvent& k) {
        if (ls_.move(k, (int)todos_.size(), rows_)) return true;
        if (k.is_char()) {
            if (k.ch == 'a') { overlay_ = ADD_TODO; ibuf_.clear(); return true; }
            if (todos_.empty()) return true;
            if (k.ch == ' ') { todos_[ls_.sel].done = !todos_[ls_.sel].done; sort_all(); return true; }
            if (k.ch == 'p') { todos_[ls_.sel].prio = cycle_prio(todos_[ls_.sel].prio); sort_all(); return true; }
            if (k.ch == 'd') { todos_.erase(todos_.begin() + ls_.sel); ls_.clamp((int)todos_.size(), rows_); return true; }
        }
        return false;
    }
    void render_todo(TextCanvas& c) {
        char r[16]; std::snprintf(r, sizeof r, "%d items", (int)todos_.size());
        int top = ui::header(c, "Todo", ui::BrightCyan, r);
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)todos_.size(), [&](int i) {
            const Todo& t = todos_[i];
            std::string box = t.done ? "[x] " : "[ ] ";
            std::string pr = t.prio == '-' ? "  " : std::string("(") + t.prio + ")";
            return box + pr + " " + t.text;
        }, ui::White, ui::BrightCyan);
        ui::footer(c, " a:add  spc:done  p:prio  d:del  tab:cal  esc:back ");
    }

    // ---- CAL view ----
    bool cal_key(const KeyEvent& k) {
        if (ls_.move(k, (int)appts_.size(), rows_)) return true;
        if (k.is_char()) {
            if (k.ch == 'a') { overlay_ = ADD_APPT; ibuf_.clear(); return true; }
            if (k.ch == 'g') { gy_ = sy_; gm_ = sm_; gd_ = sd_; overlay_ = GRID; return true; }
            if (k.ch == 'd' && !appts_.empty()) { appts_.erase(appts_.begin() + ls_.sel); ls_.clamp((int)appts_.size(), rows_); return true; }
        }
        return false;
    }
    void render_cal(TextCanvas& c) {
        char r[16]; std::snprintf(r, sizeof r, "%04d-%02d-%02d", sy_, sm_, sd_);
        int top = ui::header(c, "Calendar", ui::BrightMagenta, r);
        rows_ = ui::body_bottom(c) - top + 1;
        if (appts_.empty())
            c.text(top + 1, 2, "(no appointments — a:add  g:pick date)", ui::Gray, ui::Black, ui::ATTR_DIM);
        ui::list(c, top, rows_, ls_, (int)appts_.size(), [&](int i) {
            const Appt& a = appts_[i];
            return two(a.m) + "/" + two(a.d) + " " + two(a.hh) + ":" + two(a.mm) + "  " + a.text;
        }, ui::White, ui::BrightMagenta);
        ui::footer(c, " a:add  g:pick date  d:del  tab:todo  esc:back ");
    }

    // ---- overlays ----
    bool overlay_key(AppContext&, const KeyEvent& k) {
        if (overlay_ == GRID) {
            switch (k.key) {
                case Key::Left:  step_day(-1); return true;
                case Key::Right: step_day(1); return true;
                case Key::Up:    step_day(-7); return true;
                case Key::Down:  step_day(7); return true;
                case Key::Enter: sy_ = gy_; sm_ = gm_; sd_ = gd_; overlay_ = NONE; return true;
                case Key::Esc:   overlay_ = NONE; return true;
                default: return true;
            }
        }
        // text prompt (ADD_TODO / ADD_APPT)
        if (k.key == Key::Esc) { overlay_ = NONE; return true; }
        if (k.key == Key::Enter) { commit_prompt(); overlay_ = NONE; return true; }
        if (k.key == Key::Backspace) { if (!ibuf_.empty()) ibuf_.pop_back(); return true; }
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) ibuf_ += (char)k.ch;
        return true;
    }
    void commit_prompt() {
        std::string s = ibuf_;
        size_t a = s.find_first_not_of(' '); if (a == std::string::npos) return;
        s = s.substr(a);
        if (overlay_ == ADD_TODO) { todos_.push_back({'-', false, s}); sort_all(); ls_.sel = 0; }
        else { // ADD_APPT: optional leading "HH:MM "
            int hh = 9, mm = 0; std::string text = s;
            int rd = 0;
            if (std::sscanf(s.c_str(), "%d:%d%n", &hh, &mm, &rd) >= 2 && rd > 0) {
                text = s.substr(rd);
                size_t b = text.find_first_not_of(' '); text = (b == std::string::npos) ? "" : text.substr(b);
            } else { hh = 9; mm = 0; }
            if (hh < 0 || hh > 23) hh = 9;
            if (mm < 0 || mm > 59) mm = 0;
            appts_.push_back({sy_, sm_, sd_, hh, mm, text.empty() ? "(appt)" : text, false});
            sort_all(); ls_.sel = 0;
        }
    }
    void render_prompt(TextCanvas& c) {
        int ir, ic, iw, ih;
        const char* title = (overlay_ == ADD_TODO) ? "New todo" : "New appointment";
        const char* hint = (overlay_ == ADD_TODO) ? "type text  enter:ok  esc:cancel"
                                                   : "HH:MM text   enter:ok  esc:cancel";
        ui::modal_box(c, 5, 40, title, ui::BrightYellow, ir, ic, iw, ih, hint);
        ui::input_line(c, ir + 1, ic, "> ", ibuf_, ui::BrightWhite, iw);
    }
    void render_grid(TextCanvas& c) {
        int ir, ic, iw, ih;
        std::string title = std::string(MON[gm_ - 1]) + " " + std::to_string(gy_);
        ui::modal_box(c, 11, 24, title, ui::BrightMagenta, ir, ic, iw, ih,
                      "arrows move  enter:pick");
        c.text(ir, ic, "Su Mo Tu We Th Fr Sa", ui::Gray, ui::Black);
        int fdow = weekday(gy_, gm_, 1);     // 0=Sun
        int dim = days_in_month(gy_, gm_);
        int row = ir + 1, col0 = ic;
        for (int day = 1; day <= dim; ++day) {
            int cell = fdow + day - 1;
            int rr = row + cell / 7, cc = col0 + (cell % 7) * 3;
            bool sel = (day == gd_);
            c.text(rr, cc, two(day), sel ? ui::Black : ui::White, sel ? ui::BrightMagenta : ui::Black,
                   sel ? ui::ATTR_INVERSE : ui::ATTR_NONE);
        }
    }

    // ---- date math ----
    static int days_in_month(int y, int m) {
        static const int d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
        return d[m - 1];
    }
    static int weekday(int y, int m, int d) { // 0=Sun (Sakamoto)
        static int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
        if (m < 3) y -= 1;
        return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
    }
    void step_day(int delta) {
        std::tm tm{}; tm.tm_year = gy_ - 1900; tm.tm_mon = gm_ - 1; tm.tm_mday = gd_ + delta;
        tm.tm_hour = 12; std::mktime(&tm);
        gy_ = tm.tm_year + 1900; gm_ = tm.tm_mon + 1; gd_ = tm.tm_mday;
    }
    static std::time_t epoch(const Appt& a) {
        std::tm tm{}; tm.tm_year = a.y - 1900; tm.tm_mon = a.m - 1; tm.tm_mday = a.d;
        tm.tm_hour = a.hh; tm.tm_min = a.mm; tm.tm_isdst = -1;
        return std::mktime(&tm);
    }

    // ---- sort + (de)serialize ----
    void sort_all() {
        std::stable_sort(todos_.begin(), todos_.end(), [](const Todo& a, const Todo& b) {
            if (a.done != b.done) return !a.done;
            return prio_rank(a.prio) < prio_rank(b.prio);
        });
        std::stable_sort(appts_.begin(), appts_.end(), [](const Appt& a, const Appt& b) {
            if (a.y != b.y) return a.y < b.y;
            if (a.m != b.m) return a.m < b.m;
            if (a.d != b.d) return a.d < b.d;
            if (a.hh != b.hh) return a.hh < b.hh;
            return a.mm < b.mm;
        });
    }
    std::string dump_todos() const {
        std::string s;
        for (auto& t : todos_) { s += t.prio; s += (t.done ? '1' : '0'); s += '\t'; s += t.text; s += '\n'; }
        return s;
    }
    void load_todos(const std::string& s) {
        todos_.clear(); size_t i = 0;
        while (i < s.size()) {
            size_t nl = s.find('\n', i); std::string ln = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            if (ln.size() >= 3 && ln[2] == '\t') todos_.push_back({ln[0], ln[1] == '1', ln.substr(3)});
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
    }
    std::string dump_appts() const {
        std::string s; char b[32];
        for (auto& a : appts_) { std::snprintf(b, sizeof b, "%d %d %d %d %d\t", a.y, a.m, a.d, a.hh, a.mm); s += b; s += a.text; s += '\n'; }
        return s;
    }
    void load_appts(const std::string& s) {
        appts_.clear(); size_t i = 0;
        while (i < s.size()) {
            size_t nl = s.find('\n', i); std::string ln = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            size_t tab = ln.find('\t');
            if (tab != std::string::npos) {
                Appt a{}; if (std::sscanf(ln.c_str(), "%d %d %d %d %d", &a.y, &a.m, &a.d, &a.hh, &a.mm) == 5) {
                    a.text = ln.substr(tab + 1); a.fired = false; appts_.push_back(a);
                }
            }
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
    }

    Mode mode_ = TODO;
    Overlay overlay_ = NONE;
    std::vector<Todo> todos_;
    std::vector<Appt> appts_;
    ui::ListState ls_;
    int rows_ = 1;
    std::string ibuf_;
    int sy_ = 2026, sm_ = 1, sd_ = 1; // selected date
    int gy_ = 2026, gm_ = 1, gd_ = 1; // month-grid cursor
};

std::unique_ptr<App> make_calcurse() { return std::make_unique<Calcurse>(); }

} // namespace apps
