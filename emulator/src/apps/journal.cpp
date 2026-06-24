// Journal — a jrnl.sh-inspired notebook. Topics are files under /journal/;
// each entry is timestamped and appended. Two levels: a topic list (new/delete
// topic) and a per-topic entry view (add a timestamped entry, or open the whole
// topic in the editor). Backed by the fs seam.
#include "apps/apps.h"
#include <cstdio>
#include <ctime>
#include <vector>
#include "core/fs.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Journal : public App {
    enum Overlay { NONE, NEW_TOPIC, ADD_ENTRY };
public:
    void on_create(AppContext& ctx) override { refresh(ctx); }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (overlay_ != NONE) return overlay_key(ctx, k);
        if (level_ == 0) return topics_key(ctx, k);
        return entries_key(ctx, k);
    }

    void render(AppContext&, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        if (level_ == 0) render_topics(c); else render_entries(c);
        if (overlay_ != NONE) render_prompt(c);
    }

private:
    // ---- topics (level 0) ----
    void refresh(AppContext& ctx) {
        topics_.clear();
        std::vector<fs::Entry> es;
        if (ctx.fs) ctx.fs->list("/journal", es);
        for (auto& e : es) if (!e.is_dir && ends_txt(e.name)) topics_.push_back(e.name.substr(0, e.name.size() - 4));
    }
    static bool ends_txt(const std::string& n) { return n.size() > 4 && n.substr(n.size() - 4) == ".txt"; }
    std::string path_of(const std::string& topic) { return "/journal/" + topic + ".txt"; }

    bool topics_key(AppContext& ctx, const KeyEvent& k) {
        if (ls_.move(k, (int)topics_.size(), rows_)) return true;
        if (k.is_char() && k.ch == 'n') { overlay_ = NEW_TOPIC; ibuf_.clear(); return true; }
        if (topics_.empty()) return false;
        if (k.key == Key::Enter) { open_topic(ctx, topics_[ls_.sel]); return true; }
        if (k.is_char() && (k.ch == 'd' || k.ch == 'X')) {
            if (ctx.fs) ctx.fs->remove(path_of(topics_[ls_.sel]));
            refresh(ctx); ls_.clamp((int)topics_.size(), rows_); return true;
        }
        return false; // Esc bubbles to launcher
    }
    void render_topics(TextCanvas& c) {
        char r[16]; std::snprintf(r, sizeof r, "%d topics", (int)topics_.size());
        int top = ui::header(c, "Journal", ui::BrightGreen, r);
        rows_ = ui::body_bottom(c) - top + 1;
        if (topics_.empty())
            c.text(top + 1, 2, "(no topics — press n to start one)", ui::Gray, ui::Black, ui::ATTR_DIM);
        ui::list(c, top, rows_, ls_, (int)topics_.size(), [&](int i) { return topics_[i]; },
                 ui::White, ui::BrightGreen);
        ui::footer(c, " enter:open  n:new topic  d:delete  esc:back ");
    }

    // ---- entries (level 1) ----
    void open_topic(AppContext& ctx, const std::string& topic) {
        topic_ = topic; level_ = 1; escroll_ = 0;
        raw_.clear();
        if (ctx.fs) ctx.fs->read_text(path_of(topic_), raw_, 32 * 1024);
    }
    bool entries_key(AppContext& ctx, const KeyEvent& k) {
        if (k.key == Key::Esc) { level_ = 0; refresh(ctx); return true; } // back to topic list
        if (k.is_char() && k.ch == 'a') { overlay_ = ADD_ENTRY; ibuf_.clear(); return true; }
        if (k.is_char() && k.ch == 'e') { ctx.nav_arg = "open:" + path_of(topic_); ctx.apps->request_switch("editor"); return true; }
        if (k.key == Key::Up) { if (escroll_ > 0) escroll_--; return true; }
        if (k.key == Key::Down) { escroll_++; return true; }
        if (k.key == Key::PageUp) { escroll_ = escroll_ > 8 ? escroll_ - 8 : 0; return true; }
        if (k.key == Key::PageDown) { escroll_ += 8; return true; }
        return true;
    }
    void render_entries(TextCanvas& c) {
        int top = ui::header(c, "Journal  " + topic_, ui::BrightGreen, "");
        int rows = ui::body_bottom(c) - top + 1;
        // word-wrap on spaces/hyphens (never mid-word) for the saved/displayed text (#4)
        auto dl = ui::wrap_text(raw_, c.width());
        if (dl.size() > 1 && dl.back().empty()) dl.pop_back(); // drop trailing newline's empty line
        if (scroll_to_new_) { // land on the FIRST line of the just-added entry (#5)
            scroll_to_new_ = false;
            escroll_ = 0;
            for (int i = (int)dl.size() - 1; i >= 0; --i)
                if (!dl[i].empty() && dl[i][0] == '[') { escroll_ = i; break; } // last entry's timestamp header
        }
        if (escroll_ > (int)dl.size() - 1) escroll_ = (int)dl.size() > 0 ? (int)dl.size() - 1 : 0;
        if (escroll_ < 0) escroll_ = 0;
        if (dl.empty()) c.text(top + 1, 2, "(empty — press a to add an entry)", ui::Gray, ui::Black, ui::ATTR_DIM);
        for (int r = 0; r < rows; ++r) {
            int li = escroll_ + r;
            if (li >= (int)dl.size()) break;
            bool hdr = dl[li].size() > 1 && dl[li][0] == '[';
            c.text(top + r, 0, dl[li], hdr ? ui::BrightCyan : ui::White, ui::Black);
        }
        ui::footer(c, " a:add entry  e:edit in editor  up/dn:scroll  esc:topics ");
    }

    // ---- overlays ----
    bool overlay_key(AppContext& ctx, const KeyEvent& k) {
        if (k.key == Key::Esc) { overlay_ = NONE; return true; }
        if (k.key == Key::Enter) { commit(ctx); overlay_ = NONE; return true; }
        if (k.key == Key::Backspace) { if (!ibuf_.empty()) ibuf_.pop_back(); return true; }
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) ibuf_ += (char)k.ch;
        return true;
    }
    void commit(AppContext& ctx) {
        size_t a = ibuf_.find_first_not_of(' '); if (a == std::string::npos) return;
        std::string s = ibuf_.substr(a);
        if (overlay_ == NEW_TOPIC) {
            if (ctx.fs) ctx.fs->write_text(path_of(s), ""); // create empty topic file
            refresh(ctx);
            for (int i = 0; i < (int)topics_.size(); ++i) if (topics_[i] == s) ls_.sel = i;
        } else if (overlay_ == ADD_ENTRY) {
            std::time_t t = std::time(nullptr); std::tm tm{}; localtime_r(&t, &tm);
            char ts[40]; std::snprintf(ts, sizeof ts, "[%04d-%02d-%02d %02d:%02d] ",
                                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
            if (ctx.fs) ctx.fs->append_text(path_of(topic_), std::string(ts) + s + "\n");
            raw_.clear();
            if (ctx.fs) ctx.fs->read_text(path_of(topic_), raw_, 32 * 1024);
            scroll_to_new_ = true; // jump to the top of the new entry (resolved in render)
        }
    }
    // word-wrap text to width w (breaking over-long words)
    static std::vector<std::string> wrap_words(const std::string& text, int w) {
        std::vector<std::string> lines; std::string cur;
        size_t i = 0;
        while (i < text.size()) {
            size_t sp = text.find(' ', i);
            std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            while ((int)word.size() > w) { // hard-break a very long word
                if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
                lines.push_back(word.substr(0, w)); word = word.substr(w);
            }
            if (cur.empty()) cur = word;
            else if ((int)(cur.size() + 1 + word.size()) <= w) cur += " " + word;
            else { lines.push_back(cur); cur = word; }
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        lines.push_back(cur);
        return lines;
    }
    void render_prompt(TextCanvas& c) {
        int ir, ic, iw, ih;
        if (overlay_ == NEW_TOPIC) {
            ui::modal_box(c, 5, 46, "New topic", ui::BrightGreen, ir, ic, iw, ih, "type  enter:ok  esc:cancel");
            ui::input_line(c, ir + 1, ic, "> ", ibuf_, ui::BrightWhite, iw);
            return;
        }
        // ADD_ENTRY: a word-wrapped multi-line compose area (#10)
        ui::modal_box(c, 11, 50, "Entry to " + topic_, ui::BrightGreen, ir, ic, iw, ih, "enter:save  esc:cancel");
        auto lines = wrap_words(ibuf_, iw);
        int start = (int)lines.size() > ih ? (int)lines.size() - ih : 0; // show the tail
        int r = ir;
        for (int i = start; i < (int)lines.size() && r < ir + ih; ++i, ++r) {
            c.text(r, ic, lines[i], ui::BrightWhite, ui::Black);
            if (i + 1 == (int)lines.size()) { // caret after the last char
                int cc = ic + (int)lines[i].size();
                if (cc < ic + iw) c.put(r, cc, U'█', ui::BrightWhite, ui::Black, ui::ATTR_BOLD);
            }
        }
    }

    int level_ = 0;
    std::vector<std::string> topics_;
    ui::ListState ls_;
    int rows_ = 1;
    std::string topic_, raw_;
    int escroll_ = 0;
    bool scroll_to_new_ = false;
    Overlay overlay_ = NONE;
    std::string ibuf_;
};

std::unique_ptr<App> make_journal() { return std::make_unique<Journal>(); }

} // namespace apps
