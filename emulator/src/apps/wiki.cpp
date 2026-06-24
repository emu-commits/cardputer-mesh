// Wiki — offline encyclopedia reader over the WikiSource (SQLite FTS5) seam.
// Two levels: a search screen (FTS5 over 267K articles, results list) and an
// article reader (sections, wrapped + scrollable). Also the deep-link target for
// the global search: a "wiki:<query>" or "wikiart:<id>" intent jumps straight in.
#include "apps/apps.h"
#include <cstdlib>
#include <vector>
#include "core/ui_kit.h"
#include "core/wiki.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Wiki : public App {
    enum Level { SEARCH, READ };
    enum Focus { QUERY, LIST };
public:
    void on_create(AppContext& ctx) override { consume_intent(ctx); }
    void on_resume(AppContext& ctx) override { consume_intent(ctx); }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (!ctx.wiki || !ctx.wiki->ok()) return false; // no db -> Esc bubbles
        if (level_ == READ) return read_key(k);
        // SEARCH
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) { focus_ = QUERY; input_ += (char)k.ch; return true; }
        if (k.key == Key::Backspace) { focus_ = QUERY; if (!input_.empty()) input_.pop_back(); return true; }
        if (k.key == Key::Enter) {
            if (focus_ == QUERY) { run_search(ctx); focus_ = LIST; }
            else open_selected(ctx);
            return true;
        }
        if (k.key == Key::Up || k.key == Key::Down || k.key == Key::PageUp || k.key == Key::PageDown) {
            focus_ = LIST; ls_.move(k, (int)results_.size(), rows_); return true;
        }
        return false; // Esc bubbles to launcher
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        if (!ctx.wiki || !ctx.wiki->ok()) {
            ui::header(c, "Wiki", ui::Red, "no db");
            c.text(3, 2, "No wiki on SD.", ui::White, ui::Black);
            c.text(4, 2, "Copy wiki.db to the SD card root.", ui::Gray, ui::Black);
            ui::footer(c, " esc:back ");
            return;
        }
        if (level_ == READ) render_read(c); else render_search(c);
    }

private:
    void consume_intent(AppContext& ctx) {
        if (!ctx.wiki) return;
        if (ctx.nav_arg.rfind("wiki:", 0) == 0) {
            input_ = ctx.nav_arg.substr(5); ctx.nav_arg.clear();
            run_search(ctx); focus_ = LIST; level_ = SEARCH;
        } else if (ctx.nav_arg.rfind("wikiart:", 0) == 0) {
            int64_t id = std::strtoll(ctx.nav_arg.c_str() + 8, nullptr, 10); ctx.nav_arg.clear();
            open_article(ctx, id);
        }
    }
    void run_search(AppContext& ctx) {
        results_ = ctx.wiki->search(input_, 100);
        ls_ = {};
    }
    void open_selected(AppContext& ctx) {
        if (results_.empty() || ls_.sel >= (int)results_.size()) return;
        open_article(ctx, results_[ls_.sel].id);
    }
    void open_article(AppContext& ctx, int64_t id) {
        secs_.clear(); read_title_.clear(); rscroll_ = 0;
        if (ctx.wiki->article(id, read_title_, secs_)) level_ = READ;
    }

    void render_search(TextCanvas& c) {
        char r[16]; std::snprintf(r, sizeof r, "%d hits", (int)results_.size());
        int top = ui::header(c, "Wiki", ui::BrightBlue, r);
        // search box
        ui::input_line(c, top, 0, "search: ", input_, focus_ == QUERY ? ui::BrightWhite : ui::Gray);
        c.hline(top + 1, 1, c.width() - 2, U'-', ui::Gray, ui::Black);
        rows_ = ui::body_bottom(c) - (top + 2) + 1;
        if (results_.empty())
            c.text(top + 3, 2, "type a query, enter to search", ui::Gray, ui::Black, ui::ATTR_DIM);
        ui::list(c, top + 2, rows_, ls_, (int)results_.size(),
                 [&](int i) { return results_[i].title; }, ui::White, ui::BrightBlue);
        ui::footer(c, " type+enter search  up/dn pick  enter open  esc:back ");
    }

    bool read_key(const KeyEvent& k) {
        if (k.key == Key::Esc) { level_ = SEARCH; return true; }
        if (k.key == Key::Up) { if (rscroll_ > 0) rscroll_--; return true; }
        if (k.key == Key::Down) { rscroll_++; return true; }
        if (k.key == Key::PageUp) { rscroll_ = rscroll_ > 10 ? rscroll_ - 10 : 0; return true; }
        if (k.key == Key::PageDown) { rscroll_ += 10; return true; }
        return true; // capture while reading
    }
    std::vector<std::string> wrapped(int w) const {
        std::vector<std::string> out;
        for (auto& s : secs_) {
            if (!s.title.empty()) out.push_back("== " + s.title + " ==");
            // word-wrap on spaces/hyphens, never mid-word (#12)
            for (auto& ln : ui::wrap_text(s.content, w)) out.push_back(ln);
            out.push_back("");
        }
        if (out.size() > 1 && out.back().empty()) out.pop_back();
        return out;
    }
    void render_read(TextCanvas& c) {
        int top = ui::header(c, ui::fit("Wiki  " + read_title_, c.width() - 10), ui::BrightCyan, "");
        int rows = ui::body_bottom(c) - top + 1;
        auto dl = wrapped(c.width());
        if (rscroll_ > (int)dl.size() - 1) rscroll_ = (int)dl.size() > 0 ? (int)dl.size() - 1 : 0;
        if (rscroll_ < 0) rscroll_ = 0;
        for (int r = 0; r < rows; ++r) {
            int li = rscroll_ + r;
            if (li >= (int)dl.size()) break;
            bool hdr = dl[li].size() > 2 && dl[li][0] == '=' && dl[li][1] == '=';
            c.text(top + r, 0, dl[li], hdr ? ui::BrightYellow : ui::White, ui::Black,
                   hdr ? ui::ATTR_BOLD : ui::ATTR_NONE);
        }
        char pos[24]; std::snprintf(pos, sizeof pos, " %d/%d ", rscroll_ + 1, (int)dl.size());
        ui::footer(c, std::string(" up/dn/pgup/pgdn scroll  esc:results ") + pos);
    }

    Level level_ = SEARCH;
    Focus focus_ = QUERY;
    std::string input_;
    std::vector<wiki::Hit> results_;
    ui::ListState ls_;
    int rows_ = 1;
    std::vector<wiki::Section> secs_;
    std::string read_title_;
    int rscroll_ = 0;
};

std::unique_ptr<App> make_wiki() { return std::make_unique<Wiki>(); }

} // namespace apps
