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
    (void)tag;
    ring_.push_back({t, from, preview, now_ms});
    if (ring_.size() > RING_MAX) ring_.pop_front();
    unread_++;
    last_activity_ = now_ms ? now_ms : 1; // wake the built-in screen
}

void NotificationCenter::on_mesh(const mesh::Message& m) {
    if (m.outgoing) return;
    if (mesh_->is_ignored(m.from_id)) return; // ignored node: no notification (#2)
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

void NotificationCenter::bg_tick(uint32_t) {}

void NotificationCenter::render_status(ui::TextCanvas& bar, uint32_t now_ms) {
    bar.clear(ui::White, ui::Black);
    (void)now_ms;

    // The built-in screen is an ALWAYS-ON ambient strip (clock/battery/nodes) on
    // a USB-powered clamshell — it does not blank when idle (that read as "dead"
    // on hardware). Notifications stack below as they arrive. (last_activity_ is
    // still tracked for a future real power-save / dim, but we don't blank here.)

    // Status strip (row 0) — inverse bar: clock, battery, node count, unread.
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char clock[8];
    std::snprintf(clock, sizeof clock, "%02d:%02d", tm.tm_hour, tm.tm_min);

    int nodes = (int)mesh_->nodes().size();
    std::string left = std::string(" ") + clock + "  " + battery_ + "  nodes:" + std::to_string(nodes);
    std::string right = "unread:" + std::to_string(unread_) + " ";
    std::string strip(bar.width(), ' ');
    for (int i = 0; i < (int)left.size() && i < bar.width(); ++i) strip[i] = left[i];
    for (int i = 0; i < (int)right.size(); ++i) {
        int pos = bar.width() - (int)right.size() + i;
        if (pos >= 0 && pos < bar.width()) strip[pos] = right[i];
    }
    bar.text(0, 0, strip, ui::BrightWhite, ui::Blue, ui::ATTR_INVERSE);

    // Stacked notifications, newest first.
    bar.text(2, 1, "Notifications", ui::BrightCyan, ui::Black, ui::ATTR_UNDERLINE);
    int row = 3;
    for (auto it = ring_.rbegin(); it != ring_.rend() && row < bar.height(); ++it, ++row) {
        const char* tag = (it->type == NotifType::DM) ? "[DM] " : (it->type == NotifType::Mention) ? "[@] "
                        : (it->type == NotifType::Reminder) ? "[!] " : (it->type == NotifType::Timer) ? "[T] " : "";
        std::string line = std::string(tag) + it->from + ": " + it->preview;
        if ((int)line.size() > bar.width() - 2) line.resize(bar.width() - 2);
        bar.text(row, 1, line, ui::White, ui::Black);
    }
    if (ring_.empty()) bar.text(3, 1, "(none yet)", ui::Gray, ui::Black, ui::ATTR_DIM);
}

} // namespace nc
