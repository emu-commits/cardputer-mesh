#include "core/app.h"
#include <algorithm>
#include <cctype>

namespace app {

static std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
    return s;
}

void AppManager::reg(const std::string& id, const std::string& title, AppFactory f) {
    fac_[id] = std::move(f);
    order_.push_back({id, title});
}

std::vector<std::pair<std::string, std::string>> AppManager::list() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& p : order_)
        if (p.first != "launcher") out.push_back(p);
    return out;
}

void AppManager::start(const std::string& id, AppContext& ctx) {
    auto it = fac_.find(id);
    if (it == fac_.end()) return;
    cur_ = it->second();
    cur_->id = id;
    cur_id_ = id;
    cur_->on_create(ctx);
    cur_->on_resume(ctx);
}

void AppManager::request_switch(const std::string& id) {
    pending_ = id;
    has_pending_ = true;
}

void AppManager::apply_pending(AppContext& ctx) {
    if (!has_pending_) return;
    has_pending_ = false;
    std::string id = pending_;
    pending_.clear();
    if (id == "__quit__") { quit_requested = true; return; }
    if (fac_.find(id) == fac_.end()) return;
    if (cur_) { cur_->on_pause(ctx); cur_->on_destroy(ctx); }
    start(id, ctx);
}

void AppManager::handle_key(AppContext& ctx, const ui::KeyEvent& k) {
    if (k.ctrl && (k.ch == 'q' || k.ch == 'Q')) { quit_requested = true; return; }
    if (k.ctrl && (k.ch == 'p' || k.ch == 'P')) { open_palette(); return; }

    if (pal_) { palette_key(ctx, k); return; }

    if (k.key == ui::Key::Esc && cur_id_ != "launcher") {
        request_switch("launcher");
        return;
    }
    if (cur_) cur_->on_key(ctx, k);
}

void AppManager::tick(AppContext& ctx) {
    if (cur_) cur_->tick(ctx);
}

void AppManager::render(AppContext& ctx, ui::TextCanvas& canvas) {
    if (cur_) cur_->render(ctx, canvas);
    if (pal_) render_palette(canvas);
}

void AppManager::open_palette() {
    pal_ = !pal_;
    pal_sel_ = 0;
    pal_filter_.clear();
}

std::vector<std::pair<std::string, std::string>> AppManager::palette_filtered() const {
    std::vector<std::pair<std::string, std::string>> items = list();
    items.push_back({"__quit__", "Quit"});
    if (pal_filter_.empty()) return items;
    std::string f = lower(pal_filter_);
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& p : items)
        if (lower(p.second).find(f) != std::string::npos) out.push_back(p);
    return out;
}

void AppManager::palette_key(AppContext& ctx, const ui::KeyEvent& k) {
    auto items = palette_filtered();
    switch (k.key) {
        case ui::Key::Esc: pal_ = false; return;
        case ui::Key::Up: if (pal_sel_ > 0) pal_sel_--; return;
        case ui::Key::Down: if (pal_sel_ + 1 < (int)items.size()) pal_sel_++; return;
        case ui::Key::Backspace: if (!pal_filter_.empty()) pal_filter_.pop_back(); pal_sel_ = 0; return;
        case ui::Key::Enter:
            if (!items.empty() && pal_sel_ < (int)items.size()) {
                request_switch(items[pal_sel_].first);
                pal_ = false;
            }
            return;
        case ui::Key::Char:
            if (k.ch >= 0x20 && k.ch < 0x7f) { pal_filter_ += (char)k.ch; pal_sel_ = 0; }
            return;
        default: return;
    }
    (void)ctx;
}

void AppManager::render_palette(ui::TextCanvas& canvas) {
    const int boxR = 4, boxC = 8, boxW = 36, boxH = 16;
    canvas.fill_rect(boxR, boxC, boxH, boxW, U' ', ui::White, ui::Black);
    canvas.draw_box(boxR, boxC, boxH, boxW, ui::BrightCyan, ui::Black, ui::ATTR_BOLD);
    canvas.text(boxR, boxC + 2, " Command Palette ", ui::BrightCyan, ui::Black, ui::ATTR_BOLD);
    canvas.text(boxR + 1, boxC + 2, "> " + pal_filter_, ui::BrightWhite, ui::Black);
    canvas.hline(boxR + 2, boxC + 1, boxW - 2, U'-', ui::Gray, ui::Black);

    auto items = palette_filtered();
    int row = boxR + 3;
    int maxRows = boxH - 4;
    for (int i = 0; i < (int)items.size() && i < maxRows; ++i) {
        bool sel = (i == pal_sel_);
        uint8_t attr = sel ? ui::ATTR_INVERSE : ui::ATTR_NONE;
        std::string line = " " + items[i].second;
        while ((int)line.size() < boxW - 3) line += ' ';
        canvas.text(row + i, boxC + 2, line, ui::White, ui::Black, attr);
    }
}

} // namespace app
