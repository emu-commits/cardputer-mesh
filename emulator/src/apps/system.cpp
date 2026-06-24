// System / About — device status at a glance: identity, radio config, uptime,
// storage, battery, message/node counts. Most fields are portable; battery and
// free-RAM are device-HAL values (the emulator shows honest placeholders).
#include "apps/apps.h"
#include <cstdio>
#include <vector>
#include "core/fs.h"
#include "core/mesh.h"
#include "core/notification_center.h"
#include "core/settings.h"
#include "core/ui_kit.h"

#define FW_VERSION "0.3.0-dev"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class System : public App {
public:
    bool on_key(AppContext&, const KeyEvent& k) override { return ls_.move(k, n_, rows_); }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        int top = ui::header(c, "System", ui::BrightCyan, FW_VERSION);

        std::vector<std::pair<std::string, std::string>> rows;
        auto add = [&](const char* k, const std::string& v) { rows.push_back({k, v}); };

        add("Device", "Cardputer ADV (ESP32-S3)");
        add("Firmware", FW_VERSION);
        if (ctx.mesh) {
            char id[16]; std::snprintf(id, sizeof id, "!%08x", ctx.mesh->our_id());
            add("Node", ctx.mesh->our_long() + " " + id);
            add("Nodes known", std::to_string(ctx.mesh->nodes().size()));
        }
        if (ctx.settings)
            add("Radio", ctx.settings->get("lora", "region") + " / " + ctx.settings->get("lora", "modem_preset"));
        if (ctx.log) add("Messages", std::to_string(ctx.log->count()));
        uint32_t up = ctx.now_ms / 1000;
        char ub[24]; std::snprintf(ub, sizeof ub, "%uh %02um %02us", up / 3600, (up / 60) % 60, up % 60);
        add("Uptime", ub);
        if (ctx.fs && ctx.fs->total_bytes()) {
            char sb[40];
            std::snprintf(sb, sizeof sb, "%llu / %llu MB free",
                          (unsigned long long)(ctx.fs->free_bytes() >> 20),
                          (unsigned long long)(ctx.fs->total_bytes() >> 20));
            add("Storage", sb);
        }
        add("Battery", ctx.notify ? ctx.notify->battery() : "USB");
        add("Free RAM", "(device HAL)"); // esp_get_free_heap_size() on hardware

        n_ = (int)rows.size();
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, n_, [&](int i) {
            return ui::fit(rows[i].first, 14) + rows[i].second;
        }, ui::White, ui::BrightCyan);
        ui::footer(c, " up/dn:scroll   esc:back ");
    }

private:
    ui::ListState ls_;
    int n_ = 0, rows_ = 1;
};

std::unique_ptr<App> make_system() { return std::make_unique<System>(); }

} // namespace apps
