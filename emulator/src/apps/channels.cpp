// Channels — manage the primary + secondary Meshtastic channels (name + PSK),
// mirroring Plai's app_channels. One list; add/rename via a modal prompt, PSK
// cycles through None/Default/Random. Channel 0 is the primary. Read-only when
// the mesh backend forbids config writes (a live connected node).
#include "apps/apps.h"
#include <vector>
#include "core/mesh.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Channels : public App {
    struct Ch { std::string name; std::string psk; };
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) load(ctx.state->get("channels.data", ""));
        if (chans_.empty()) chans_.push_back({"", "Default"}); // primary
    }
    void on_pause(AppContext& ctx) override { if (ctx.state) ctx.state->set("channels.data", dump()); }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        bool ro = ctx.mesh && !ctx.mesh->config_writable();
        if (adding_ || renaming_) {
            if (k.key == Key::Esc) { adding_ = renaming_ = false; return true; }
            if (k.key == Key::Enter) {
                std::string s = buf_;
                if (adding_) chans_.push_back({s, "Default"});
                else if (renaming_ && ls_.sel < (int)chans_.size()) chans_[ls_.sel].name = s;
                adding_ = renaming_ = false; persist(ctx); return true;
            }
            if (k.key == Key::Backspace) { if (!buf_.empty()) buf_.pop_back(); return true; }
            if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) buf_ += (char)k.ch;
            return true;
        }
        if (ls_.move(k, (int)chans_.size(), rows_)) return true;
        if (ro) return true; // read-only: navigation only
        if (k.is_char()) {
            if (k.ch == 'a' && chans_.size() < 8) { adding_ = true; buf_.clear(); return true; }
            if (chans_.empty()) return true;
            if (k.ch == 'r' || k.key == Key::Enter) { renaming_ = true; buf_ = chans_[ls_.sel].name; return true; }
            if (k.ch == 'k') { cycle_psk(chans_[ls_.sel].psk); persist(ctx); return true; }
            if (k.ch == 'd' && ls_.sel > 0) { chans_.erase(chans_.begin() + ls_.sel); ls_.clamp((int)chans_.size(), rows_); persist(ctx); return true; }
        }
        return false;
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        bool ro = ctx.mesh && !ctx.mesh->config_writable();
        int top = ui::header(c, "Channels", ui::BrightCyan, ro ? "read-only" : std::to_string(chans_.size()) + "/8");
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)chans_.size(), [&](int i) {
            std::string nm = chans_[i].name.empty() ? "(default)" : chans_[i].name;
            std::string tag = (i == 0) ? "P " : "  ";
            return tag + ui::fit(nm, 22) + "psk:" + chans_[i].psk;
        }, ui::White, ui::BrightCyan);
        ui::footer(c, ro ? " read-only (live node)  esc:back "
                         : " a:add  r:rename  k:psk  d:del  esc:back ");
        if (adding_ || renaming_) {
            int ir, ic, iw, ih;
            ui::modal_box(c, 5, 40, adding_ ? "New channel" : "Rename channel", ui::BrightCyan,
                          ir, ic, iw, ih, "type  enter:ok  esc:cancel");
            ui::input_line(c, ir + 1, ic, "> ", buf_, ui::BrightWhite, iw);
        }
    }

private:
    static void cycle_psk(std::string& p) {
        p = (p == "None") ? "Default" : (p == "Default") ? "Random" : "None";
    }
    void persist(AppContext& ctx) { if (ctx.state) ctx.state->set("channels.data", dump()); }
    std::string dump() const {
        std::string s;
        for (auto& c : chans_) s += c.name + "\t" + c.psk + "\n";
        return s;
    }
    void load(const std::string& s) {
        chans_.clear();
        size_t i = 0;
        while (i < s.size()) {
            size_t nl = s.find('\n', i);
            std::string ln = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            size_t t = ln.find('\t');
            if (t != std::string::npos) chans_.push_back({ln.substr(0, t), ln.substr(t + 1)});
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
    }

    std::vector<Ch> chans_;
    ui::ListState ls_;
    int rows_ = 1;
    bool adding_ = false, renaming_ = false;
    std::string buf_;
};

std::unique_ptr<App> make_channels() { return std::make_unique<Channels>(); }

} // namespace apps
