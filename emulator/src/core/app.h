// App framework + arena. Mirrors Plai's Mooncake: an App has a lifecycle, and
// exactly one foreground app is "alive" on the CYD at a time (the arena).
// Lifecycle maps to Mooncake: on_create=onCreate, tick+render=onRunning,
// on_pause/on_destroy=onPause/onDestroy. A global command palette overlay and
// Esc-to-launcher are handled at the manager level.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "core/input.h"
#include "core/persist.h"
#include "core/text_canvas.h"

namespace mesh { class MeshFacade; class MessageStore; }
namespace nc { class NotificationCenter; }
namespace fs { class FileSystem; }
namespace clip { class Clipboard; }
namespace cfg { class Settings; }

namespace app {

class AppManager;

struct AppContext {
    AppManager* apps = nullptr;
    mesh::MeshFacade* mesh = nullptr;
    mesh::MessageStore* store = nullptr;
    nc::NotificationCenter* notify = nullptr;
    persist::Store* state = nullptr;
    fs::FileSystem* fs = nullptr;
    clip::Clipboard* clip = nullptr;
    cfg::Settings* settings = nullptr;
    // Cross-app intent: the requester sets this before request_switch(); the
    // target app reads + clears it in on_create/on_resume. e.g. "open:/notes/x",
    // "dm:439041101", "contact:439041101".
    std::string nav_arg;
    uint32_t now_ms = 0;
};

// A palette command: a titled action. Apps contribute context commands that the
// command palette merges with global ones (§5).
struct Command {
    std::string title;
    std::function<void(AppContext&)> run;
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
    // Context commands the palette should offer while this app is foreground.
    virtual std::vector<Command> commands(AppContext&) { return {}; }
};

using AppFactory = std::function<std::unique_ptr<App>()>;

class AppManager {
public:
    // hidden apps are reachable via request_switch but excluded from the
    // launcher list and palette app-switches (e.g. the setup wizard, #18).
    void reg(const std::string& id, const std::string& title, AppFactory f, bool hidden = false);
    // [(id,title)] excluding the launcher itself and hidden apps.
    std::vector<std::pair<std::string, std::string>> list() const;

    void start(const std::string& id, AppContext& ctx);
    void restore_session(AppContext& ctx); // resume the saved app, else launcher
    void shutdown(AppContext& ctx);        // persist current app + flush on exit
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
    void render_palette(AppContext& ctx, ui::TextCanvas& canvas);
    std::vector<Command> palette_items(AppContext& ctx);    // active + app-switch + global
    std::vector<Command> palette_filtered(AppContext& ctx); // items matching the filter

    std::map<std::string, AppFactory> fac_;
    std::vector<std::pair<std::string, std::string>> order_;
    std::set<std::string> hidden_;

    std::unique_ptr<App> cur_;
    std::string cur_id_;
    std::string pending_;
    bool has_pending_ = false;

    bool pal_ = false;
    int pal_sel_ = 0;
    std::string pal_filter_;
};

} // namespace app
