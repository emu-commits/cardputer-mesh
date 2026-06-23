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

    void render_status(ui::TextCanvas& bar, uint32_t now_ms);      // built-in screen

private:
    static bool icontains(const std::string& hay, const std::string& needle);
    void push(NotifType t, const std::string& from, const std::string& preview,
              const char* tag, uint32_t now_ms);

    mesh::MeshFacade* mesh_;
    std::deque<Notification> ring_;
    int unread_ = 0;
    std::string banner_;
    uint32_t banner_until_ = 0;
    static constexpr size_t RING_MAX = 16;
};

} // namespace nc
