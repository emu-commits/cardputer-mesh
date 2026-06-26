// NotificationCenter — resident background service that renders to the built-in
// 1.14" screen (the small status/notify surface, separate from the CYD). It
// classifies mesh RX (DM / @mention) and accepts calendar/timer/reminder events,
// keeps a small ring, shows a transient banner, and a recent-events list.
// On device this is a resident Mooncake bg app (setAllowBgRunning + onRunningBG).
#pragma once
#include <deque>
#include <string>
#include "core/mesh.h"
#include "core/text_canvas.h"

namespace nc {

enum class NotifType { DM, Mention, Reminder, Timer, Generic };

struct Notification {
    NotifType type;
    std::string from;
    std::string preview;
    uint32_t ts_ms;
};

class NotificationCenter {
public:
    explicit NotificationCenter(mesh::MeshFacade* m) : mesh_(m) {}

    void on_mesh(const mesh::Message& m);                          // mesh RX hook
    void add_event(NotifType t, const std::string& from,
                   const std::string& preview, uint32_t now_ms);   // cal/timer/reminder
    void bg_tick(uint32_t now_ms);                                 // expire banner
    void mark_read() { unread_ = 0; }
    int unread() const { return unread_; }
    // Built-in screen power state: lit for OFF_MS after the last notification,
    // dark otherwise. The device cuts the backlight when this is false so the
    // light-up is the attention signal and idle is distraction-free.
    bool screen_on(uint32_t now_ms) const {
        return last_activity_ != 0 && (now_ms - last_activity_) < OFF_MS;
    }
    void set_battery(const std::string& b) { battery_ = b; } // device HAL pushes "NN%"
    const std::string& battery() const { return battery_; }
    void set_ram(const std::string& r) { ram_ = r; }         // device HAL pushes heap stats
    const std::string& ram() const { return ram_; }

    void render_status(ui::TextCanvas& bar, uint32_t now_ms);      // built-in screen

private:
    static bool icontains(const std::string& hay, const std::string& needle);
    void push(NotifType t, const std::string& from, const std::string& preview,
              const char* tag, uint32_t now_ms);

    mesh::MeshFacade* mesh_;
    std::deque<Notification> ring_;
    int unread_ = 0;
    std::string battery_ = "USB";
    std::string ram_ = "(device HAL)";
    uint32_t last_activity_ = 0;            // built-in screen sleeps after idle
    static constexpr size_t RING_MAX = 16;
    static constexpr uint32_t OFF_MS = 10000; // blank the screen after 10s quiet
};

} // namespace nc
