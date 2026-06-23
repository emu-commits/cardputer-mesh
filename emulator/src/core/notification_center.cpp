#include "core/notification_center.h"
#include <algorithm>
#include <cctype>
#include <ctime>

namespace nc {

bool NotificationCenter::icontains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return false;
    auto lc = [](unsigned char c) { return (char)std::tolower(c); };
    std::string h, n;
    h.reserve(hay.size()); n.reserve(needle.size());
    for (unsigned char c : hay) h += lc(c);
    for (unsigned char c : needle) n += lc(c);
    return h.find(n) != std::string::npos;
}

void NotificationCenter::push(NotifType t, const std::string& from,
                              const std::string& preview, const char* tag, uint32_t now_ms) {
    ring_.push_back({t, from, preview, now_ms});
    if (ring_.size() > RING_MAX) ring_.pop_front();
    unread_++;
    banner_ = std::string(tag) + " " + from + ": " + preview;
    if (banner_.size() > 50) banner_.resize(50);
    banner_until_ = now_ms + 4000;
}

void NotificationCenter::on_mesh(const mesh::Message& m) {
    if (m.outgoing) return;
    if (m.dest == mesh_->our_id()) {
        push(NotifType::DM, m.from_name, m.text, "[DM]", m.ts_ms);
    } else if (m.dest == mesh::BROADCAST &&
               (icontains(m.text, mesh_->our_short()) || icontains(m.text, mesh_->our_long()))) {
        push(NotifType::Mention, m.from_name, m.text, "[@]", m.ts_ms);
    }
    // generic channel chatter: no banner (kept off the small screen)
}

void NotificationCenter::add_event(NotifType t, const std::string& from,
                                   const std::string& preview, uint32_t now_ms) {
    const char* tag = (t == NotifType::Reminder) ? "[!]" : (t == NotifType::Timer) ? "[T]" : "[*]";
    push(t, from, preview, tag, now_ms);
}

void NotificationCenter::bg_tick(uint32_t now_ms) {
    if (banner_until_ && now_ms >= banner_until_) { banner_until_ = 0; banner_.clear(); }
}

void NotificationCenter::render_status(ui::TextCanvas& bar, uint32_t now_ms) {
    bar.clear(ui::White, ui::Black);

    // Status strip (row 0) — inverse bar: clock, node count, unread badge.
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char clock[8];
    std::snprintf(clock, sizeof clock, "%02d:%02d", tm.tm_hour, tm.tm_min);

    int nodes = (int)mesh_->nodes().size();
    std::string left = std::string(" ") + clock + "  nodes:" + std::to_string(nodes);
    std::string right = "unread:" + std::to_string(unread_) + " ";
    std::string strip(bar.width(), ' ');
    for (int i = 0; i < (int)left.size() && i < bar.width(); ++i) strip[i] = left[i];
    for (int i = 0; i < (int)right.size(); ++i) {
        int pos = bar.width() - (int)right.size() + i;
        if (pos >= 0 && pos < bar.width()) strip[pos] = right[i];
    }
    bar.text(0, 0, strip, ui::BrightWhite, ui::Blue, ui::ATTR_INVERSE);

    // Banner (fresh event) or recent list.
    if (!banner_.empty() && now_ms < banner_until_) {
        bar.text(2, 1, ">> " + banner_, ui::BrightYellow, ui::Black, ui::ATTR_BOLD);
        bar.text(bar.height() - 1, 1, "(notification)", ui::Gray, ui::Black, ui::ATTR_DIM);
    } else {
        bar.text(2, 1, "Notifications", ui::BrightCyan, ui::Black, ui::ATTR_UNDERLINE);
        int row = 3;
        for (auto it = ring_.rbegin(); it != ring_.rend() && row < bar.height(); ++it, ++row) {
            std::string line = it->from + ": " + it->preview;
            if ((int)line.size() > bar.width() - 2) line.resize(bar.width() - 2);
            bar.text(row, 1, line, ui::White, ui::Black);
        }
        if (ring_.empty()) bar.text(3, 1, "(none yet)", ui::Gray, ui::Black, ui::ATTR_DIM);
    }
}

} // namespace nc
