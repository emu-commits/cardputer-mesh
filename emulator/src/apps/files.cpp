// Files — a midnight-commander-style browser over the fs::FileSystem seam.
// Primary view = the directory listing (§5.1). Enter opens a dir or views a text
// file; the text viewer and the info card are modal overlays. The viewer hard-
// wraps long lines so extended content can't overflow the screen.
#include "apps/apps.h"
#include <cstdio>
#include <vector>
#include "core/fs.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Files : public App {
    enum Overlay { NONE, VIEW, INFO };
public:
    void on_create(AppContext& ctx) override {
        cwd_ = ctx.state ? ctx.state->get("files.cwd", "/") : "/";
        if (!ctx.fs || !ctx.fs->is_dir(cwd_)) cwd_ = "/";
        refresh(ctx);
        if (ctx.state) ls_.sel = ctx.state->get_int("files.sel", 0);
        ls_.clamp((int)items_.size(), 1);
    }
    void on_pause(AppContext& ctx) override {
        if (!ctx.state) return;
        ctx.state->set("files.cwd", cwd_);
        ctx.state->set_int("files.sel", ls_.sel);
    }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (overlay_ == VIEW) return view_key(k);
        if (overlay_ == INFO) { if (k.key == Key::Esc || k.is_char()) overlay_ = NONE; return true; }
        if (ls_.move(k, (int)items_.size(), rows_)) return true;
        if (k.key == Key::Left || k.key == Key::Backspace) { go_up(ctx); return true; }
        if (k.key == Key::Enter) { activate(ctx); return true; }
        if (k.is_char()) {
            if (k.ch == 'v') { view_file(ctx); return true; }
            if (k.ch == 'i') { if (!items_.empty()) overlay_ = INFO; return true; }
        }
        return false;
    }

    void render(AppContext&, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        int top = ui::header(c, "Files", ui::BrightBlue, crumb(c.width() / 2));
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)items_.size(), [&](int i) {
            const fs::Entry& e = items_[i];
            if (e.is_dir) return e.name + "/";
            char sz[16]; std::snprintf(sz, sizeof sz, "%llu", (unsigned long long)e.size);
            std::string name = e.name;
            int pad = c.width() - 4 - (int)name.size() - (int)std::string(sz).size();
            if (pad < 1) pad = 1;
            return name + std::string(pad, ' ') + sz;
        }, ui::White, ui::BrightBlue);
        ui::footer(c, " enter:open  bksp:up  v:view  i:info  esc:back ");
        if (overlay_ == VIEW) render_view(c);
        else if (overlay_ == INFO) render_info(c);
    }

private:
    std::string crumb(int maxw) {
        std::string s = cwd_;
        if ((int)s.size() > maxw) s = "…" + s.substr(s.size() - (maxw - 1));
        return s;
    }
    void refresh(AppContext& ctx) {
        items_.clear();
        if (cwd_ != "/") items_.push_back({"..", true, 0});
        std::vector<fs::Entry> es;
        if (ctx.fs) ctx.fs->list(cwd_, es);
        for (auto& e : es) items_.push_back(e);
    }
    void go_up(AppContext& ctx) {
        if (cwd_ == "/") return;
        cwd_ = ctx.fs->join(cwd_, "..");
        refresh(ctx); ls_.sel = 0; ls_.top = 0;
    }
    void activate(AppContext& ctx) {
        if (items_.empty()) return;
        const fs::Entry& e = items_[ls_.sel];
        if (e.name == "..") { go_up(ctx); return; }
        std::string p = ctx.fs->join(cwd_, e.name);
        if (e.is_dir) { cwd_ = p; refresh(ctx); ls_.sel = 0; ls_.top = 0; }
        else view_file(ctx);
    }
    void view_file(AppContext& ctx) {
        if (items_.empty()) return;
        const fs::Entry& e = items_[ls_.sel];
        if (e.is_dir || !ctx.fs) return;
        std::string p = ctx.fs->join(cwd_, e.name);
        if (!ctx.fs->read_text(p, raw_, 32 * 1024)) raw_ = "(cannot read)";
        view_name_ = e.name; vscroll_ = 0; overlay_ = VIEW;
    }
    bool view_key(const KeyEvent& k) {
        if (k.key == Key::Esc || (k.is_char() && k.ch == 'q')) { overlay_ = NONE; return true; }
        if (k.key == Key::Up) { if (vscroll_ > 0) vscroll_--; return true; }
        if (k.key == Key::Down) { vscroll_++; return true; }
        if (k.key == Key::PageUp) { vscroll_ = vscroll_ > 10 ? vscroll_ - 10 : 0; return true; }
        if (k.key == Key::PageDown) { vscroll_ += 10; return true; }
        return true; // capture everything while viewing
    }
    // hard-wrap raw_ to width w into display rows
    std::vector<std::string> wrap(int w) const {
        std::vector<std::string> out;
        std::string line;
        auto emit_line = [&](const std::string& ln) {
            if (ln.empty()) { out.push_back(""); return; }
            for (size_t i = 0; i < ln.size(); i += w) out.push_back(ln.substr(i, w));
        };
        size_t i = 0;
        while (i <= raw_.size()) {
            size_t nl = raw_.find('\n', i);
            std::string ln = raw_.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            // strip a trailing CR for CRLF files
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            emit_line(ln);
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
        return out;
    }
    void render_view(TextCanvas& c) {
        c.clear(ui::White, ui::Black);
        int top = ui::header(c, "View  " + view_name_, ui::BrightCyan);
        int rows = ui::body_bottom(c) - top + 1;
        auto dl = wrap(c.width());
        if (vscroll_ > (int)dl.size() - 1) vscroll_ = (int)dl.size() > 0 ? (int)dl.size() - 1 : 0;
        if (vscroll_ < 0) vscroll_ = 0;
        for (int r = 0; r < rows; ++r) {
            int li = vscroll_ + r;
            if (li >= (int)dl.size()) break;
            c.text(top + r, 0, dl[li], ui::White, ui::Black);
        }
        char pos[24]; std::snprintf(pos, sizeof pos, " %d/%d lines ", vscroll_ + 1, (int)dl.size());
        ui::footer(c, std::string(" up/dn/pgup/pgdn scroll  esc:close ") + pos);
    }
    void render_info(TextCanvas& c) {
        const fs::Entry& e = items_[ls_.sel];
        int ir, ic, iw, ih;
        ui::modal_box(c, 8, 44, "Info", ui::BrightBlue, ir, ic, iw, ih, "esc:close");
        c.text(ir + 0, ic, ui::fit("name: " + e.name, iw), ui::White, ui::Black);
        c.text(ir + 1, ic, std::string("type: ") + (e.is_dir ? "directory" : "file"), ui::White, ui::Black);
        char sz[32]; std::snprintf(sz, sizeof sz, "size: %llu bytes", (unsigned long long)e.size);
        c.text(ir + 2, ic, sz, ui::White, ui::Black);
        c.text(ir + 3, ic, ui::fit("path: " + cwd_, iw), ui::Gray, ui::Black);
    }

    std::string cwd_ = "/";
    std::vector<fs::Entry> items_;
    ui::ListState ls_;
    int rows_ = 1;
    Overlay overlay_ = NONE;
    std::string raw_, view_name_;
    int vscroll_ = 0;
};

std::unique_ptr<App> make_files() { return std::make_unique<Files>(); }

} // namespace apps
