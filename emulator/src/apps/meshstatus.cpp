// Mesh status — read-only summary of who we are and how the radio is configured
// (the §7 mesh-status app). Pulls identity + node count from the MeshFacade and
// the radio config from cfg::Settings.
#include "apps/apps.h"
#include <cstdio>
#include <vector>
#include "core/mesh.h"
#include "core/settings.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class MeshStatus : public App {
public:
    bool on_key(AppContext&, const KeyEvent& k) override { return ls_.move(k, n_, rows_); }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        bool ro = ctx.mesh && !ctx.mesh->config_writable();
        int top = ui::header(c, "Mesh status", ui::BrightMagenta, ro ? "live node" : "stub");

        std::vector<std::string> rows;
        auto add = [&](const std::string& l, const std::string& v) { rows.push_back(ui::fit(l, 14) + v); };
        if (ctx.mesh) {
            char idb[16]; std::snprintf(idb, sizeof idb, "!%08x", ctx.mesh->our_id());
            add("Node:", ctx.mesh->our_long() + " (" + ctx.mesh->our_short() + ")");
            add("Node id:", idb);
            add("Nodes known:", std::to_string(ctx.mesh->nodes().size()));
        }
        if (ctx.settings) {
            auto& s = *ctx.settings;
            add("Region:", s.get("lora", "region"));
            add("Preset:", s.get("lora", "modem_preset"));
            add("Freq slot:", s.get("lora", "freq_slot"));
            add("Hop limit:", s.get("lora", "hop_limit"));
            add("TX power:", s.get("lora", "tx_power") + " dBm");
            add("Role:", s.get("node", "role"));
        }
        n_ = (int)rows.size();
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, n_, [&](int i) { return rows[i]; }, ui::White, ui::BrightMagenta);
        ui::footer(c, " read-only  up/dn:scroll  esc:back ");
    }

private:
    ui::ListState ls_;
    int n_ = 0, rows_ = 1;
};

std::unique_ptr<App> make_mesh_status() { return std::make_unique<MeshStatus>(); }

} // namespace apps
