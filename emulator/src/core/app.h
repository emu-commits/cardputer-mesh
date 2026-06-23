// App framework + arena. Mirrors Plai's Mooncake: an App has a lifecycle, and
// exactly one foreground app is "alive" on the CYD at a time (the arena).
// Lifecycle maps to Mooncake: on_create=onCreate, tick+render=onRunning,
// on_pause/on_destroy=onPause/onDestroy. A global command palette overlay and
// Esc-to-launcher are handled at the manager level.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core/input.h"
#include "core/text_canvas.h"

namespace mesh { class MeshFacade; class MessageStore; }
namespace nc { class NotificationCenter; }

namespace app {

class AppManager;

struct AppContext {
    AppManager* apps = nullptr;
    mesh::MeshFacade* mesh = nullptr;
    mesh::MessageStore* store = nullptr;
    nc::NotificationCenter* notify = nullptr;
    uint32_t now_ms = 0;
};

class App {
public:
    virtual ~App() = default;
    std::string id;

    virtual void on_create(AppContext&) {}
    virtual void on_resume(AppContext&) {}
    virtual bool on_key(AppContext&, const ui::KeyEvent&) { return false; }
    virtual void tick(AppContext&) {}
    virtual void render(AppContext&, ui::TextCanvas&) {}
    virtual void on_pause(AppContext&) {}
    virtual void on_destroy(AppContext&) {}
};

using AppFactory = std::function<std::unique_ptr<App>()>;

class AppManager {
public:
    void reg(const std::string& id, const std::string& title, AppFactory f);
    // [(id,title)] excluding the launcher itself.
    std::vector<std::pair<std::string, std::string>> list() const;

    void start(const std::string& id, AppContext& ctx);
    void request_switch(const std::string& id);
    void apply_pending(AppContext& ctx);

    void handle_key(AppContext& ctx, const ui::KeyEvent& k);
    void tick(AppContext& ctx);
    void render(AppContext& ctx, ui::TextCanvas& canvas);

    const std::string& current_id() const { return cur_id_; }
    bool quit_requested = false;

private:
    void open_palette();
    void palette_key(AppContext& ctx, const ui::KeyEvent& k);
    void render_palette(ui::TextCanvas& canvas);
    std::vector<std::pair<std::string, std::string>> palette_filtered() const;

    std::map<std::string, AppFactory> fac_;
    std::vector<std::pair<std::string, std::string>> order_;

    std::unique_ptr<App> cur_;
    std::string cur_id_;
    std::string pending_;
    bool has_pending_ = false;

    bool pal_ = false;
    int pal_sel_ = 0;
    std::string pal_filter_;
};

} // namespace app
