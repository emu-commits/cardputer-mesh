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
char cycle_prio(char p) { return p == 'A' ? 'B' : p == 'B' ? 'C' : p == 'C' ? '-' : 'A'; }
std::string two(int n) { char b[12]; std::snprintf(b, sizeof b, "%02d", n); return b; }
const char* MON[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
} // namespace

class Calcurse : public App {
    struct Todo { char prio; bool done; std::string text; std::string topic; };
    struct Appt { int y, m, d, hh, mm; std::string text; bool fired = false; };
    enum Mode { TODO = 0, CAL = 1 };
    enum Overlay { NONE, ADD_TODO, ADD_APPT, GRID, ADD_TOPIC };

public:
    void on_create(AppContext& ctx) override {
        std::time_t t = std::time(nullptr); std::tm tm{}; localtime_r(&t, &tm);
        sy_ = tm.tm_year + 1900; sm_ = tm.tm_mon + 1; sd_ = tm.tm_mday;
        if (ctx.state) {
            mode_ = ctx.state->get_int("calcurse.mode", TODO) == CAL ? CAL : TODO;
            load_topics(ctx.state->get("calcurse.topics", ""));
            load_todos(ctx.state->get("calcurse.todos", ""));
            load_appts(ctx.state->get("calcurse.appts", ""));
        }
        if (topics_.empty()) topics_.push_back("general");
        sort_all();
    }
    void on_pause(AppContext& ctx) override {
        if (!ctx.state) return;
        ctx.state->set_int("calcurse.mode", mode_);
        ctx.state->set("calcurse.topics", dump_topics());
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
        else if (overlay_ == ADD_TODO || overlay_ == ADD_APPT || overlay_ == ADD_TOPIC) render_prompt(c);
    }

    std::vector<Command> commands(AppContext&) override {
        return {
            {"New todo", [this](AppContext&) { mode_ = TODO; overlay_ = ADD_TODO; ibuf_.clear(); }},
            {"New todo topic", [this](AppContext&) { mode_ = TODO; overlay_ = ADD_TOPIC; ibuf_.clear(); }},
            {"New appointment", [this](AppContext&) { mode_ = CAL; overlay_ = ADD_APPT; ibuf_.clear(); }},
            {"Pick date", [this](AppContext&) { mode_ = CAL; gy_ = sy_; gm_ = sm_; gd_ = sd_; overlay_ = GRID; }},
        };
    }

private:
    // ---- TODO view (per-topic) ----
    std::string cur_topic() { return topics_.empty() ? "general" : topics_[cur_topic_ % topics_.size()]; }
    void rebuild_vis() {
        vis_.clear();
        std::string t = cur_topic();
        for (int i = 0; i < (int)todos_.size(); ++i) if (todos_[i].topic == t) vis_.push_back(i);
    }
    void next_topic(int dir) {
        if (topics_.empty()) return;
        cur_topic_ = (cur_topic_ + (dir > 0 ? 1 : (int)topics_.size() - 1)) % (int)topics_.size();
        ls_.sel = 0; ls_.top = 0;
    }
    bool todo_key(const KeyEvent& k) {
        rebuild_vis();
        if (k.key == Key::Left) { next_topic(-1); return true; }   // switch topic
        if (k.key == Key::Right) { next_topic(1); return true; }
        if (ls_.move(k, (int)vis_.size(), rows_)) return true;
        if (k.is_char()) {
            if (k.ch == 'a') { overlay_ = ADD_TODO; ibuf_.clear(); return true; }
            if (k.ch == 'n') { overlay_ = ADD_TOPIC; ibuf_.clear(); return true; }
            if (k.ch == '>') { next_topic(1); return true; }
            if (k.ch == '<') { next_topic(-1); return true; }
            if (k.ch == 'X') { delete_topic(); return true; }
            if (vis_.empty()) return true;
            int idx = vis_[ls_.sel];
            if (k.ch == ' ') { todos_[idx].done = !todos_[idx].done; return true; }
            if (k.ch == 'p') { todos_[idx].prio = cycle_prio(todos_[idx].prio); return true; }
            if (k.ch == 'd') { todos_.erase(todos_.begin() + idx); rebuild_vis(); ls_.clamp((int)vis_.size(), rows_); return true; }
            if (k.ch == '[') { reorder(-1); return true; } // move up within topic
            if (k.ch == ']') { reorder(1); return true; }  // move down within topic
        }
        return false;
    }
    void reorder(int dir) {
        rebuild_vis();
        int a = ls_.sel, b = a + dir;
        if (a < 0 || a >= (int)vis_.size() || b < 0 || b >= (int)vis_.size()) return;
        std::swap(todos_[vis_[a]], todos_[vis_[b]]); // both in current topic
        ls_.sel = b;
    }
    void delete_topic() {
        if (topics_.size() <= 1) return; // keep at least one
        std::string t = cur_topic();
        todos_.erase(std::remove_if(todos_.begin(), todos_.end(),
                                    [&](const Todo& td) { return td.topic == t; }), todos_.end());
        topics_.erase(topics_.begin() + (cur_topic_ % topics_.size()));
        if (cur_topic_ >= (int)topics_.size()) cur_topic_ = (int)topics_.size() - 1;
        ls_.sel = 0;
    }
    void render_todo(TextCanvas& c) {
        rebuild_vis();
        char r[28]; std::snprintf(r, sizeof r, "%s  %d/%d", cur_topic().c_str(), cur_topic_ + 1, (int)topics_.size());
        int top = ui::header(c, "Todo", ui::BrightCyan, r);
        rows_ = ui::body_bottom(c) - top + 1;
        if (vis_.empty())
            c.text(top + 1, 2, "(no todos in this topic — a:add  n:new topic)", ui::Gray, ui::Black, ui::ATTR_DIM);
        ui::list(c, top, rows_, ls_, (int)vis_.size(), [&](int i) {
            const Todo& t = todos_[vis_[i]];
            std::string box = t.done ? "[x] " : "[ ] ";
            std::string pr = t.prio == '-' ? "  " : std::string("(") + t.prio + ")";
            return box + pr + " " + t.text;
        }, ui::White, ui::BrightCyan);
        ui::footer2(c, " a:add  spc:done  p:prio  [ ]:reorder  d:del ",
                       " <-/-> topic  n:new  X:del topic  tab:cal ");
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
        if (overlay_ == ADD_TODO) { todos_.push_back({'-', false, s, cur_topic()}); ls_.sel = 0; }
        else if (overlay_ == ADD_TOPIC) {
            topics_.push_back(s); cur_topic_ = (int)topics_.size() - 1; ls_.sel = 0;
        }
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
        const char* title = (overlay_ == ADD_TODO) ? "New todo" : (overlay_ == ADD_TOPIC) ? "New topic" : "New appointment";
        const char* hint = (overlay_ == ADD_APPT) ? "HH:MM text   enter:ok  esc:cancel"
                                                   : "type text  enter:ok  esc:cancel";
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
    // Todos keep MANUAL order (so reorder works); only appointments auto-sort.
    void sort_all() {
        std::stable_sort(appts_.begin(), appts_.end(), [](const Appt& a, const Appt& b) {
            if (a.y != b.y) return a.y < b.y;
            if (a.m != b.m) return a.m < b.m;
            if (a.d != b.d) return a.d < b.d;
            if (a.hh != b.hh) return a.hh < b.hh;
            return a.mm < b.mm;
        });
    }
    std::string dump_topics() const {
        std::string s; for (size_t i = 0; i < topics_.size(); ++i) { if (i) s += '\n'; s += topics_[i]; }
        return s;
    }
    void load_topics(const std::string& s) {
        topics_.clear(); size_t i = 0;
        while (i < s.size()) {
            size_t nl = s.find('\n', i);
            std::string t = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            if (!t.empty()) topics_.push_back(t);
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
    }
    // todo line: "<prio><done>\t<topic>\t<text>"
    std::string dump_todos() const {
        std::string s;
        for (auto& t : todos_) { s += t.prio; s += (t.done ? '1' : '0'); s += '\t'; s += t.topic; s += '\t'; s += t.text; s += '\n'; }
        return s;
    }
    void load_todos(const std::string& s) {
        todos_.clear(); size_t i = 0;
        while (i < s.size()) {
            size_t nl = s.find('\n', i); std::string ln = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            if (ln.size() >= 3 && ln[2] == '\t') {
                size_t t2 = ln.find('\t', 3);
                if (t2 != std::string::npos)
                    todos_.push_back({ln[0], ln[1] == '1', ln.substr(t2 + 1), ln.substr(3, t2 - 3)});
            }
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
    std::vector<std::string> topics_{"general"};
    int cur_topic_ = 0;
    std::vector<int> vis_;
    ui::ListState ls_;
    int rows_ = 1;
    std::string ibuf_;
    int sy_ = 2026, sm_ = 1, sd_ = 1; // selected date
    int gy_ = 2026, gm_ = 1, gd_ = 1; // month-grid cursor
};

std::unique_ptr<App> make_calcurse() { return std::make_unique<Calcurse>(); }

} // namespace apps
