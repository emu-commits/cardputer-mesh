// Search — an fzf-inspired global finder over PERSONAL content. As you type it
// fuzzy-filters a small in-RAM corpus harvested from the other apps' stores:
// contacts, todos, appointments, channels, recent messages, journal lines, and
// file paths. Selecting a hit hands off to the owning app via a nav_arg intent
// (e.g. "chat|dm:123", "editor|open:/journal/x.txt", "contacts|contact:123").
//
// The offline Wiki (267K articles) is deliberately NOT fuzzy-indexed — that
// can't fit in 512 KB RAM. Instead a synthetic top row hands the raw query to
// the Wiki app, which runs its own FTS5 search. (See docs/WIKI.md.)
//
// RAM: the corpus is bounded (MAX_ITEMS, truncated labels) and rebuilt on
// resume / freed on pause, so it only lives while Search is the foreground app.
#include "apps/apps.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>
#include "core/fs.h"
#include "core/mesh.h"
#include "core/persist.h"
#include "core/ui_kit.h"
#include "core/wiki.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Search : public App {
    struct Item {
        char kind;          // C T A H M J F (contact/todo/appt/channel/msg/journal/file)
        std::string label;  // display + search text (already bounded)
        std::string target; // app id to switch to ("" = no-op)
        std::string arg;    // nav_arg intent for the target
    };
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) input_ = ctx.state->get("search.query", ""); // restore last query (Esc-back to results)
        build(ctx); refilter(ctx);
    }
    void on_pause(AppContext& ctx) override {
        if (ctx.state) ctx.state->set("search.query", input_);
        corpus_.clear(); corpus_.shrink_to_fit(); shown_.clear();
    }
    // The corpus is harvested on entry, but inbound mesh messages can arrive while
    // Search is foreground. Re-harvest only when the message log grows (cheap
    // count() check) so live messages show up without re-walking the filesystem.
    void tick(AppContext& ctx) override {
        if (ctx.log && ctx.log->count() != last_log_count_) { build(ctx); refilter(ctx, /*reset_sel=*/false); }
    }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (k.key == Key::Up || k.key == Key::Down || k.key == Key::PageUp || k.key == Key::PageDown) {
            ls_.move(k, total_rows(), rows_); return true;
        }
        if (k.key == Key::Enter) { activate(ctx); return true; }
        if (k.key == Key::Backspace) {
            if (!input_.empty()) { input_.pop_back(); refilter(ctx); }
            return true;
        }
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) { input_ += (char)k.ch; refilter(ctx); return true; }
        return false; // Esc bubbles to launcher
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        char r[20]; std::snprintf(r, sizeof r, "%d/%d", total_rows(), (int)corpus_.size());
        int top = ui::header(c, "Search", ui::BrightMagenta, r);
        ui::input_line(c, top, 0, "find: ", input_, ui::BrightWhite);
        c.hline(top + 1, 1, c.width() - 2, U'-', ui::Gray, ui::Black);
        rows_ = ui::body_bottom(c) - (top + 2) + 1;
        int n = total_rows();
        if (n == 0)
            c.text(top + 3, 2, input_.empty() ? "type to search your stuff" : "no matches",
                   ui::Gray, ui::Black, ui::ATTR_DIM);
        ui::list(c, top + 2, rows_, ls_, n, [&](int i) { return row_label(ctx, i); },
                 ui::White, ui::BrightMagenta);
        ui::footer(c, " type:filter  up/dn:pick  enter:open  esc:back ");
    }

private:
    // ---- corpus build (harvest the other apps' stores) --------------------
    static std::string lower(std::string s) {
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }
    static std::string trunc(const std::string& s, size_t n = 72) {
        return s.size() > n ? s.substr(0, n - 1) + "…" : s;
    }
    void add(char kind, const std::string& label, const std::string& target, const std::string& arg) {
        if ((int)corpus_.size() >= MAX_ITEMS) return;
        if (label.empty()) return;
        corpus_.push_back({kind, trunc(label), target, arg});
    }
    static std::vector<std::string> split(const std::string& s, char d) {
        std::vector<std::string> out; size_t i = 0;
        for (;;) { size_t t = s.find(d, i); out.push_back(s.substr(i, t == std::string::npos ? std::string::npos : t - i)); if (t == std::string::npos) break; i = t + 1; }
        return out;
    }

    void build(AppContext& ctx) {
        corpus_.clear();
        harvest_contacts(ctx);
        harvest_todos(ctx);
        harvest_appts(ctx);
        harvest_channels(ctx);
        harvest_messages(ctx);
        harvest_journal(ctx);
        harvest_files(ctx);
        last_log_count_ = ctx.log ? ctx.log->count() : 0;
    }

    void harvest_contacts(AppContext& ctx) {
        if (!ctx.state) return;
        for (auto& ln : split(ctx.state->get("contacts.records", ""), '\n')) {
            auto f = split(ln, '\t');
            if (f.size() < 4) continue;
            std::string name = f[1], loc = f.size() >= 7 ? f[5] : "", notes = f.size() >= 7 ? f[6] : (f.size() >= 5 ? f[4] : "");
            std::string label = name;
            if (!loc.empty()) label += " @" + loc;
            if (!notes.empty()) label += " - " + notes;
            // pass id + name so Contacts can open the EXISTING record for edit (#13),
            // even when the contact has no LoRa id (id 0 -> match by name).
            add('C', label, "contacts", "contact:" + f[0] + "\t" + name);
        }
    }
    void harvest_todos(AppContext& ctx) {
        if (!ctx.state) return;
        for (auto& ln : split(ctx.state->get("calcurse.todos", ""), '\n')) {
            auto f = split(ln, '\t');
            if (f.size() < 3) continue;
            bool done = f[0].size() > 1 && f[0][1] == '1';
            add('T', std::string(done ? "[x] " : "[ ] ") + f[2] + "  (" + f[1] + ")", "calcurse", "");
        }
    }
    void harvest_appts(AppContext& ctx) {
        if (!ctx.state) return;
        for (auto& ln : split(ctx.state->get("calcurse.appts", ""), '\n')) {
            auto f = split(ln, '\t');
            if (f.size() < 2) continue;
            int y = 0, m = 0, d = 0, hh = 0, mm = 0;
            std::sscanf(f[0].c_str(), "%d %d %d %d %d", &y, &m, &d, &hh, &mm);
            char date[24]; std::snprintf(date, sizeof date, "%04d-%02d-%02d %02d:%02d", y, m, d, hh, mm);
            add('A', std::string(date) + "  " + f[1], "calcurse", "");
        }
    }
    void harvest_channels(AppContext& ctx) {
        if (!ctx.state) return;
        for (auto& ln : split(ctx.state->get("channels.data", ""), '\n')) {
            if (ln.empty()) continue; // skip the trailing-newline blank line
            auto f = split(ln, '\t');
            std::string nm = f[0].empty() ? "(primary)" : f[0];
            add('H', "#" + nm, "channels", "");
        }
    }
    void harvest_messages(AppContext& ctx) {
        if (!ctx.log) return;
        size_t total = ctx.log->count();
        size_t from = total > MSG_SCAN ? total - MSG_SCAN : 0;
        uint32_t me = ctx.mesh ? ctx.mesh->our_id() : 0;
        std::vector<mesh::Message> recent;
        ctx.log->scan_from(from, [&](const mesh::Message& m) { recent.push_back(m); });
        // newest first so the freshest messages win ties on an empty query
        for (auto it = recent.rbegin(); it != recent.rend(); ++it) {
            const auto& m = *it;
            std::string who = m.outgoing ? "me" : m.from_name;
            bool dm = (m.dest == me && !m.outgoing) || (m.outgoing && m.dest != mesh::BROADCAST);
            std::string tgt = dm ? ("dm:" + std::to_string(m.outgoing ? m.dest : m.from_id)) : std::string();
            add('M', who + ": " + m.text, "chat", tgt);
        }
    }
    void harvest_journal(AppContext& ctx) {
        if (!ctx.fs) return;
        std::vector<fs::Entry> es;
        if (!ctx.fs->list("/journal", es)) return;
        int budget = JRNL_LINES;
        for (auto& e : es) {
            if (e.is_dir || e.name.size() <= 4 || e.name.substr(e.name.size() - 4) != ".txt") continue;
            std::string path = "/journal/" + e.name, raw;
            ctx.fs->read_text(path, raw, 16 * 1024);
            std::string topic = e.name.substr(0, e.name.size() - 4);
            for (auto& line : split(raw, '\n')) {
                if (line.empty()) continue;
                if (budget-- <= 0) return;
                add('J', topic + ": " + line, "editor", "open:" + path);
            }
        }
    }
    void harvest_files(AppContext& ctx) {
        if (!ctx.fs) return;
        walk(ctx, "/", 0);
    }
    void walk(AppContext& ctx, const std::string& dir, int depth) {
        if (depth > 3 || (int)corpus_.size() >= MAX_ITEMS) return;
        std::vector<fs::Entry> es;
        if (!ctx.fs->list(dir, es)) return;
        for (auto& e : es) {
            std::string path = (dir == "/" ? "" : dir) + "/" + e.name;
            if (e.is_dir) {
                if (e.name == "journal") continue; // journal indexed by line above
                walk(ctx, path, depth + 1);
            } else {
                if (e.name == "wiki.db") continue; // the encyclopedia is its own app
                add('F', path, "editor", "open:" + path);
            }
        }
    }

    // ---- fuzzy filter (fzf-style subsequence scoring) ---------------------
    // Case-insensitive subsequence match; rewards consecutive runs and matches
    // at word boundaries, penalizes leading/scattered gaps. Returns false if not
    // all needle chars are found in order.
    static bool fuzzy(const std::string& needle_lc, const std::string& hay, int& score) {
        if (needle_lc.empty()) { score = 0; return true; }
        score = 0; int ni = 0, run = 0; int gap_before = 0;
        char prev = ' ';
        for (size_t i = 0; i < hay.size() && ni < (int)needle_lc.size(); ++i) {
            char h = (char)std::tolower((unsigned char)hay[i]);
            if (h == needle_lc[ni]) {
                int s = 1;
                if (run > 0) s += 4 * run;                 // consecutive run bonus
                bool boundary = (prev == ' ' || prev == '/' || prev == '_' || prev == '-' || prev == ':' || prev == '.');
                if (boundary) s += 8;                      // word-start bonus
                if (ni == 0 && i == 0) s += 6;             // exact prefix bonus
                score += s; ++ni; ++run;
            } else {
                run = 0;
                if (ni == 0) ++gap_before;
            }
            prev = hay[i];
        }
        if (ni < (int)needle_lc.size()) return false;
        score -= gap_before / 4; // mild penalty for a late first match
        return true;
    }

    void refilter(AppContext& ctx, bool reset_sel = true) {
        shown_.clear();
        std::string q = lower(input_);
        std::vector<std::pair<int, int>> scored; // (score, index)
        for (int i = 0; i < (int)corpus_.size(); ++i) {
            int s;
            if (fuzzy(q, corpus_[i].label, s)) scored.push_back({s, i});
        }
        if (!q.empty())
            std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
        int cap = (int)scored.size() < MAX_SHOWN ? (int)scored.size() : MAX_SHOWN;
        for (int i = 0; i < cap; ++i) shown_.push_back(scored[i].second);
        wiki_row_ = !input_.empty() && ctx.wiki && ctx.wiki->ok();
        if (reset_sel) { ls_.sel = 0; ls_.top = 0; }
        else ls_.clamp(total_rows(), rows_); // keep selection on a live re-harvest
    }

    // displayed rows = optional wiki-handoff row + filtered corpus
    int total_rows() const { return (wiki_row_ ? 1 : 0) + (int)shown_.size(); }
    std::string row_label(AppContext&, int i) {
        if (wiki_row_) {
            if (i == 0) return "[?] Search wiki for \"" + input_ + "\"";
            i -= 1;
        }
        const Item& it = corpus_[shown_[i]];
        char tag[5] = {'[', it.kind, ']', ' ', 0};
        return std::string(tag) + it.label;
    }

    void activate(AppContext& ctx) {
        int sel = ls_.sel;
        if (wiki_row_) {
            if (sel == 0) { ctx.apps->set_back_target("search"); ctx.nav_arg = "wiki:" + input_; ctx.apps->request_switch("wiki"); return; }
            sel -= 1;
        }
        if (sel < 0 || sel >= (int)shown_.size()) return;
        const Item& it = corpus_[shown_[sel]];
        if (it.target.empty()) return;
        ctx.apps->set_back_target("search"); // Esc in the target returns to these results (#14)
        ctx.nav_arg = it.arg;
        ctx.apps->request_switch(it.target);
    }

    static constexpr int MAX_ITEMS = 800;  // hard RAM bound on the corpus
    static constexpr int MAX_SHOWN = 150;  // displayed-results cap
    static constexpr size_t MSG_SCAN = 200; // recent messages to index
    static constexpr int JRNL_LINES = 400; // journal lines indexed across topics

    std::vector<Item> corpus_;
    std::vector<int> shown_;
    bool wiki_row_ = false;
    std::string input_;
    ui::ListState ls_;
    int rows_ = 1;
    size_t last_log_count_ = 0;
};

std::unique_ptr<App> make_search() { return std::make_unique<Search>(); }

} // namespace apps
