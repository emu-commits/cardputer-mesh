#include "core/app.h"
#include <algorithm>
#include <cctype>

namespace app {

static std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
    return s;
}

void AppManager::reg(const std::string& id, const std::string& title, AppFactory f, bool hidden) {
    fac_[id] = std::move(f);
    order_.push_back({id, title});
    if (hidden) hidden_.insert(id);
}

std::vector<std::pair<std::string, std::string>> AppManager::list() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& p : order_)
        if (p.first != "launcher" && !hidden_.count(p.first)) out.push_back(p);
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

void AppManager::restore_session(AppContext& ctx) {
    std::string id = ctx.state ? ctx.state->get("session.active", "launcher") : "launcher";
    if (fac_.find(id) == fac_.end()) id = "launcher";
    start(id, ctx);
}

void AppManager::shutdown(AppContext& ctx) {
    if (cur_) { cur_->on_pause(ctx); cur_->on_destroy(ctx); cur_.reset(); }
    if (ctx.state) { ctx.state->set("session.active", cur_id_); ctx.state->flush(); }
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
    // Checkpoint: the just-saved resume tokens (in on_pause) + the new active app.
    if (ctx.state) { ctx.state->set("session.active", id); ctx.state->flush(); }
}

void AppManager::handle_key(AppContext& ctx, const ui::KeyEvent& k) {
    if (k.ctrl && (k.ch == 'q' || k.ch == 'Q')) { quit_requested = true; return; }
    if (k.ctrl && (k.ch == 'p' || k.ch == 'P')) { open_palette(); return; }

    if (pal_) { palette_key(ctx, k); return; }

    // The active app gets first crack at every key (including Esc) so it can
    // close an overlay or pop a sub-level. Only if it doesn't consume Esc do we
    // fall back to "Esc = leave app -> launcher".
    if (cur_ && cur_->on_key(ctx, k)) return;
    if (k.key == ui::Key::Esc && cur_id_ != "launcher") {
        if (!back_to_.empty()) { std::string b = back_to_; back_to_.clear(); request_switch(b); }
        else request_switch("launcher");
    }
}

void AppManager::tick(AppContext& ctx) {
    if (cur_) cur_->tick(ctx);
}

void AppManager::render(AppContext& ctx, ui::TextCanvas& canvas) {
    if (cur_) cur_->render(ctx, canvas);
    if (pal_) render_palette(ctx, canvas);
}

void AppManager::open_palette() {
    pal_ = !pal_;
    pal_sel_ = 0;
    pal_filter_.clear();
}

std::vector<Command> AppManager::palette_items(AppContext& ctx) {
    std::vector<Command> out;
    // 1) commands contributed by the active app (most contextual)
    if (cur_) for (auto& c : cur_->commands(ctx)) out.push_back(c);
    // 2) switch to any other app
    for (auto& p : order_) {
        if (p.first == "launcher" || p.first == cur_id_ || hidden_.count(p.first)) continue;
        std::string id = p.first;
        out.push_back({"Go to " + p.second, [this, id](AppContext&) { request_switch(id); }});
    }
    if (cur_id_ != "launcher")
        out.push_back({"Go to Home", [this](AppContext&) { request_switch("launcher"); }});
    // 3) global
    out.push_back({"Quit", [this](AppContext&) { quit_requested = true; }});
    return out;
}

std::vector<Command> AppManager::palette_filtered(AppContext& ctx) {
    auto items = palette_items(ctx);
    if (pal_filter_.empty()) return items;
    std::string f = lower(pal_filter_);
    std::vector<Command> out;
    for (auto& c : items)
        if (lower(c.title).find(f) != std::string::npos) out.push_back(c);
    return out;
}

void AppManager::palette_key(AppContext& ctx, const ui::KeyEvent& k) {
    auto items = palette_filtered(ctx);
    switch (k.key) {
        case ui::Key::Esc: pal_ = false; return;
        case ui::Key::Up: if (!items.empty()) pal_sel_ = (pal_sel_ > 0) ? pal_sel_ - 1 : (int)items.size() - 1; return;
        case ui::Key::Down: if (!items.empty()) pal_sel_ = (pal_sel_ + 1 < (int)items.size()) ? pal_sel_ + 1 : 0; return;
        case ui::Key::Backspace: if (!pal_filter_.empty()) pal_filter_.pop_back(); pal_sel_ = 0; return;
        case ui::Key::Enter:
            if (!items.empty() && pal_sel_ < (int)items.size()) {
                pal_ = false;
                items[pal_sel_].run(ctx); // may switch apps or mutate the active app
            }
            return;
        case ui::Key::Char:
            if (k.ch >= 0x20 && k.ch < 0x7f) { pal_filter_ += (char)k.ch; pal_sel_ = 0; }
            return;
        default: return;
    }
}

void AppManager::render_palette(AppContext& ctx, ui::TextCanvas& canvas) {
    const int boxR = 2, boxC = 6, boxW = 41, boxH = 16;
    canvas.fill_rect(boxR, boxC, boxH, boxW, U' ', ui::White, ui::Black);
    canvas.draw_box(boxR, boxC, boxH, boxW, ui::BrightCyan, ui::Black, ui::ATTR_BOLD);
    canvas.text(boxR, boxC + 2, " Command Palette ", ui::BrightCyan, ui::Black, ui::ATTR_BOLD);
    canvas.text(boxR + 1, boxC + 2, "> " + pal_filter_, ui::BrightWhite, ui::Black);
    canvas.put(boxR + 1, boxC + 4 + (int)pal_filter_.size(), U'█', ui::BrightWhite, ui::Black);
    canvas.hline(boxR + 2, boxC + 1, boxW - 2, U'-', ui::Gray, ui::Black);

    auto items = palette_filtered(ctx);
    int maxRows = boxH - 4;
    if (pal_sel_ >= (int)items.size()) pal_sel_ = items.empty() ? 0 : (int)items.size() - 1;
    int top = 0;
    if (pal_sel_ >= maxRows) top = pal_sel_ - maxRows + 1;
    for (int r = 0; r < maxRows; ++r) {
        int i = top + r;
        if (i >= (int)items.size()) break;
        bool sel = (i == pal_sel_);
        std::string line = " " + items[i].title;
        while ((int)line.size() < boxW - 3) line += ' ';
        if ((int)line.size() > boxW - 3) line.resize(boxW - 3);
        canvas.text(boxR + 3 + r, boxC + 2, line, ui::White, ui::Black,
                    sel ? ui::ATTR_INVERSE : ui::ATTR_NONE);
    }
}

} // namespace app
