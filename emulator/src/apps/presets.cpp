// Presets — save/recall LoRa+device config to the SD card by name, restore the
// Meshtastic-spec defaults, and apply built-in presets. Ships a "nyme.sh (NYC)"
// preset from the network's getting-started guide so travelers can switch mesh
// regions in two taps. Read-only when the backend is a live node.
// Saved presets live as /config/presets/<name>.cfg (serialized cfg::Settings).
#include "apps/apps.h"
#include <vector>
#include "core/fs.h"
#include "core/mesh.h"
#include "core/settings.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

namespace {
constexpr const char* PRESET_DIR = "/config/presets";
// Built-in nyme.sh NYC config (from the getting-started guide): US region,
// MediumSlow preset, channel slot 48, 7 hops. Primary channel stays the
// Meshtastic default (psk AQ==, empty name), so only LoRa values are set here.
constexpr const char* NYMESH_CFG =
    "lora.region=US\nlora.modem_preset=MediumSlow\nlora.freq_slot=48\nlora.hop_limit=7\n";
} // namespace

class Presets : public App {
    struct Entry { enum Kind { SAVE, DEFAULTS, BUILTIN, SAVED } kind; std::string label, payload; };
public:
    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (saving_) return save_key(ctx, k);
        build(ctx);
        if (ls_.move(k, (int)entries_.size(), rows_)) return true;
        if (entries_.empty()) return false;
        const Entry& e = entries_[ls_.sel];
        bool ro = ctx.mesh && !ctx.mesh->config_writable();
        if (k.key == Key::Enter) {
            if (e.kind == Entry::SAVE) {
                if (ro) { status_ = "read-only (live node)"; return true; }
                saving_ = true; ibuf_.clear(); return true;
            }
            if (ro) { status_ = "read-only (live node)"; return true; }
            apply(ctx, e); return true;
        }
        if (k.is_char() && k.ch == 'd' && e.kind == Entry::SAVED && !ro) {
            if (ctx.fs) ctx.fs->remove(e.payload);
            status_ = "deleted"; ls_.clamp((int)entries_.size() - 1, rows_); return true;
        }
        return false;
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        build(ctx);
        bool ro = ctx.mesh && !ctx.mesh->config_writable();
        int top = ui::header(c, "Config presets", ui::BrightGreen, ro ? "read-only" : "SD");
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)entries_.size(), [&](int i) { return entries_[i].label; },
                 ui::White, ui::BrightGreen);
        ui::footer(c, status_.empty() ? " enter:apply/save  d:delete saved  esc:back "
                                      : (" " + status_));
        if (saving_) {
            int ir, ic, iw, ih;
            ui::modal_box(c, 5, 44, "Save preset as", ui::BrightGreen, ir, ic, iw, ih, "name  enter:save  esc:cancel");
            ui::input_line(c, ir + 1, ic, "> ", ibuf_, ui::BrightWhite, iw);
        }
    }

private:
    void build(AppContext& ctx) {
        entries_.clear();
        entries_.push_back({Entry::SAVE, "Save current config to SD...", ""});
        entries_.push_back({Entry::DEFAULTS, "Restore Meshtastic defaults", ""});
        entries_.push_back({Entry::BUILTIN, "Apply nyme.sh (NYC)  [built-in]", NYMESH_CFG});
        std::vector<fs::Entry> es;
        if (ctx.fs) ctx.fs->list(PRESET_DIR, es);
        for (auto& e : es) {
            if (e.is_dir) continue;
            std::string n = e.name;
            if (n.size() > 4 && n.substr(n.size() - 4) == ".cfg")
                entries_.push_back({Entry::SAVED, "Load: " + n.substr(0, n.size() - 4),
                                    std::string(PRESET_DIR) + "/" + n});
        }
    }
    void apply(AppContext& ctx, const Entry& e) {
        if (!ctx.settings) return;
        if (e.kind == Entry::DEFAULTS) { ctx.settings->restore_defaults(); status_ = "restored defaults"; }
        else if (e.kind == Entry::BUILTIN) { ctx.settings->apply_serialized(e.payload); status_ = "applied nyme.sh"; }
        else if (e.kind == Entry::SAVED) {
            std::string blob;
            if (ctx.fs && ctx.fs->read_text(e.payload, blob, 16 * 1024)) { ctx.settings->apply_serialized(blob); status_ = "applied preset"; }
        }
        if (ctx.state) ctx.settings->save(*ctx.state);
    }
    bool save_key(AppContext& ctx, const KeyEvent& k) {
        if (k.key == Key::Esc) { saving_ = false; return true; }
        if (k.key == Key::Enter) {
            size_t a = ibuf_.find_first_not_of(' ');
            if (a != std::string::npos && ctx.fs && ctx.settings) {
                std::string name = ibuf_.substr(a);
                ctx.fs->write_text(std::string(PRESET_DIR) + "/" + name + ".cfg", ctx.settings->serialize());
                status_ = "saved " + name;
            }
            saving_ = false; return true;
        }
        if (k.key == Key::Backspace) { if (!ibuf_.empty()) ibuf_.pop_back(); return true; }
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f && k.ch != '/') ibuf_ += (char)k.ch;
        return true;
    }

    std::vector<Entry> entries_;
    ui::ListState ls_;
    int rows_ = 1;
    bool saving_ = false;
    std::string ibuf_, status_;
};

std::unique_ptr<App> make_presets() { return std::make_unique<Presets>(); }

} // namespace apps
