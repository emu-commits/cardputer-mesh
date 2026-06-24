// Editor — a nano-style text editor backed by files on the fs seam.
// Opens/saves real files under the SD sandbox (default /notes/scratch.txt),
// supports Save As (path prompt) and Open (kicks to the Files browser, which
// opens the chosen file back here via an "open:<path>" intent). ^S save,
// ^K cut line, ^U paste via the shared clipboard. Pausing checkpoints to disk.
// On device the buffer is a gap buffer paged from SD; a line vector stands in.
#include "apps/apps.h"
#include <string>
#include <vector>
#include "core/clipboard.h"
#include "core/fs.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Editor : public App {
    enum Overlay { NONE, SAVEAS };
public:
    void on_create(AppContext& ctx) override {
        path_ = "/notes/scratch.txt";
        if (ctx.nav_arg.rfind("open:", 0) == 0) { path_ = ctx.nav_arg.substr(5); ctx.nav_arg.clear(); }
        load(ctx);
    }
    void on_pause(AppContext& ctx) override { if (dirty_) write(ctx); } // checkpoint

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (overlay_ == SAVEAS) return saveas_key(ctx, k);
        if (k.ctrl && (k.ch == 's' || k.ch == 'S')) { write(ctx); status_ = " saved "; return true; }
        if (k.ctrl && (k.ch == 'k' || k.ch == 'K')) { cut_line(ctx); return true; }
        if (k.ctrl && (k.ch == 'u' || k.ch == 'U')) { paste(ctx); return true; }
        switch (k.key) {
            case Key::Up:    if (cy_ > 0) { cy_--; clamp_cx(); } return true;
            case Key::Down:  if (cy_ + 1 < (int)lines_.size()) { cy_++; clamp_cx(); } return true;
            case Key::Left:  if (cx_ > 0) cx_--; else if (cy_ > 0) { cy_--; cx_ = (int)lines_[cy_].size(); } return true;
            case Key::Right: if (cx_ < (int)lines_[cy_].size()) cx_++; else if (cy_ + 1 < (int)lines_.size()) { cy_++; cx_ = 0; } return true;
            case Key::Home:  cx_ = 0; return true;
            case Key::End:   cx_ = (int)lines_[cy_].size(); return true;
            case Key::PageUp:   cy_ = cy_ > 12 ? cy_ - 12 : 0; clamp_cx(); return true;
            case Key::PageDown: cy_ = cy_ + 12 < (int)lines_.size() ? cy_ + 12 : (int)lines_.size() - 1; clamp_cx(); return true;
            case Key::Enter:     split_line(); return true;
            case Key::Backspace: backspace(); return true;
            case Key::Delete:    del(); return true;
            case Key::Tab:       insert_str("  "); return true;
            case Key::Char:
                if (k.ch >= 0x20 && k.ch < 0x7f) { insert_str(std::string(1, (char)k.ch)); return true; }
                return false;
            default: return false;
        }
    }

    std::vector<Command> commands(AppContext&) override {
        return {
            {"Save", [this](AppContext& c) { write(c); status_ = " saved "; }},
            {"Save as...", [this](AppContext&) { overlay_ = SAVEAS; pbuf_ = path_; }},
            {"Open file (browse)", [this](AppContext& c) { c.apps->request_switch("files"); }},
            {"Cut line", [this](AppContext& c) { cut_line(c); }},
            {"Copy line", [this](AppContext& c) { if (c.clip) c.clip->set(lines_[cy_]); status_ = " copied "; }},
            {"Paste", [this](AppContext& c) { paste(c); }},
        };
    }

    void render(AppContext&, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        char pos[40]; std::snprintf(pos, sizeof pos, "Ln %d Col %d", cy_ + 1, cx_ + 1);
        std::string title = "Editor  " + base(path_) + (dirty_ ? " *" : "");
        int top = ui::header(c, title, ui::BrightGreen, pos);
        int rows = ui::body_bottom(c) - top + 1;

        if (cy_ < top_) top_ = cy_;
        if (cy_ >= top_ + rows) top_ = cy_ - rows + 1;
        int w = c.width();
        if (cx_ < xoff_) xoff_ = cx_;
        if (cx_ >= xoff_ + w) xoff_ = cx_ - w + 1;

        for (int r = 0; r < rows; ++r) {
            int li = top_ + r;
            if (li >= (int)lines_.size()) break;
            const std::string& ln = lines_[li];
            std::string vis = (xoff_ < (int)ln.size()) ? ln.substr(xoff_, w) : "";
            c.text(top + r, 0, vis, ui::White, ui::Black);
            // off-screen indicators: text continues left ('<') and/or right ('>')
            if (xoff_ > 0) c.put(top + r, 0, U'<', ui::Black, ui::BrightYellow, ui::ATTR_INVERSE);
            if ((int)ln.size() > xoff_ + w) c.put(top + r, w - 1, U'>', ui::Black, ui::BrightYellow, ui::ATTR_INVERSE);
        }
        int crow = top + (cy_ - top_), ccol = cx_ - xoff_;
        if (crow >= top && crow <= ui::body_bottom(c) && ccol >= 0 && ccol < w) {
            char32_t under = (cx_ < (int)lines_[cy_].size()) ? (char32_t)lines_[cy_][cx_] : U' ';
            c.put(crow, ccol, under, ui::Black, ui::BrightGreen, ui::ATTR_INVERSE);
        }
        ui::footer(c, status_.empty() ? " ^S save  ^K cut  ^U paste  ctrl-p: more  esc back "
                                      : status_);
        if (overlay_ == SAVEAS) {
            int ir, ic, iw, ih;
            ui::modal_box(c, 5, 46, "Save as (path)", ui::BrightGreen, ir, ic, iw, ih, "enter:save  esc:cancel");
            ui::input_line(c, ir + 1, ic, "", pbuf_, ui::BrightWhite, iw);
        }
    }

private:
    bool saveas_key(AppContext& ctx, const KeyEvent& k) {
        if (k.key == Key::Esc) { overlay_ = NONE; return true; }
        if (k.key == Key::Enter) { if (!pbuf_.empty()) path_ = pbuf_; write(ctx); status_ = " saved "; overlay_ = NONE; return true; }
        if (k.key == Key::Backspace) { if (!pbuf_.empty()) pbuf_.pop_back(); return true; }
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) pbuf_ += (char)k.ch;
        return true;
    }
    static std::string base(const std::string& p) { size_t s = p.find_last_of('/'); return s == std::string::npos ? p : p.substr(s + 1); }
    void load(AppContext& ctx) {
        std::string doc;
        if (ctx.fs) ctx.fs->read_text(path_, doc, 64 * 1024);
        split(doc);
        cy_ = cx_ = top_ = xoff_ = 0; dirty_ = false; status_.clear();
    }
    void write(AppContext& ctx) { if (ctx.fs) ctx.fs->write_text(path_, join()); dirty_ = false; }

    void split(const std::string& doc) {
        lines_.clear();
        size_t i = 0;
        for (;;) {
            size_t nl = doc.find('\n', i);
            lines_.push_back(doc.substr(i, nl == std::string::npos ? std::string::npos : nl - i));
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
        if (lines_.empty()) lines_.push_back("");
    }
    std::string join() const {
        std::string s;
        for (size_t i = 0; i < lines_.size(); ++i) { s += lines_[i]; if (i + 1 < lines_.size()) s += '\n'; }
        return s;
    }
    void clamp_cx() { if (cx_ < 0) cx_ = 0; if (cx_ > (int)lines_[cy_].size()) cx_ = (int)lines_[cy_].size(); }
    void insert_str(const std::string& s) { lines_[cy_].insert(cx_, s); cx_ += (int)s.size(); dirty_ = true; status_.clear(); }
    void split_line() {
        std::string tail = lines_[cy_].substr(cx_);
        lines_[cy_].erase(cx_);
        lines_.insert(lines_.begin() + cy_ + 1, tail);
        cy_++; cx_ = 0; dirty_ = true; status_.clear();
    }
    void backspace() {
        if (cx_ > 0) { lines_[cy_].erase(cx_ - 1, 1); cx_--; }
        else if (cy_ > 0) { cx_ = (int)lines_[cy_ - 1].size(); lines_[cy_ - 1] += lines_[cy_]; lines_.erase(lines_.begin() + cy_); cy_--; }
        else return;
        dirty_ = true; status_.clear();
    }
    void del() {
        if (cx_ < (int)lines_[cy_].size()) lines_[cy_].erase(cx_, 1);
        else if (cy_ + 1 < (int)lines_.size()) { lines_[cy_] += lines_[cy_ + 1]; lines_.erase(lines_.begin() + cy_ + 1); }
        else return;
        dirty_ = true; status_.clear();
    }
    void cut_line(AppContext& ctx) {
        if (ctx.clip) ctx.clip->set(lines_[cy_]);
        if (lines_.size() > 1) { lines_.erase(lines_.begin() + cy_); if (cy_ >= (int)lines_.size()) cy_ = (int)lines_.size() - 1; }
        else lines_[0].clear();
        cx_ = 0; dirty_ = true; status_ = " cut ";
    }
    void paste(AppContext& ctx) {
        if (!ctx.clip || ctx.clip->empty()) return;
        for (char ch : ctx.clip->get()) {
            if (ch == '\n') split_line();
            else if (ch != '\r') { lines_[cy_].insert(cx_, 1, ch); cx_++; }
        }
        dirty_ = true; status_ = " pasted ";
    }

    std::vector<std::string> lines_{""};
    int cy_ = 0, cx_ = 0, top_ = 0, xoff_ = 0;
    bool dirty_ = false;
    std::string status_, path_, pbuf_;
    Overlay overlay_ = NONE;
};

std::unique_ptr<App> make_editor() { return std::make_unique<Editor>(); }

} // namespace apps
