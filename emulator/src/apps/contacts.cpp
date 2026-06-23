// Contacts — abook-style favorites + aliases over the mesh node DB.
// One list (favorites first), with a modal rename prompt. 'f' stars a node, 'r'
// gives it a local alias, Enter opens chat. Aliases/favorites persist; they key
// off node id so they survive nodes coming and going.
#include "apps/apps.h"
#include <algorithm>
#include <map>
#include <set>
#include "core/mesh.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Contacts : public App {
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) load(ctx.state->get("contacts.data", ""));
        if (ctx.state) ls_.sel = ctx.state->get_int("contacts.sel", 0);
    }
    void on_pause(AppContext& ctx) override {
        if (!ctx.state) return;
        ctx.state->set("contacts.data", dump());
        ctx.state->set_int("contacts.sel", ls_.sel);
    }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (renaming_) {
            if (k.key == Key::Esc) { renaming_ = false; return true; }
            if (k.key == Key::Enter) {
                if (sel_id_) { if (ibuf_.empty()) alias_.erase(sel_id_); else alias_[sel_id_] = ibuf_; }
                renaming_ = false; return true;
            }
            if (k.key == Key::Backspace) { if (!ibuf_.empty()) ibuf_.pop_back(); return true; }
            if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) ibuf_ += (char)k.ch;
            return true;
        }
        build(ctx);
        if (ls_.move(k, (int)view_.size(), rows_)) return true;
        if (view_.empty()) return true;
        uint32_t id = view_[ls_.sel].id;
        if (k.key == Key::Enter) { ctx.apps->request_switch("chat"); return true; }
        if (k.is_char()) {
            if (k.ch == 'f') { if (fav_.count(id)) fav_.erase(id); else fav_.insert(id); return true; }
            if (k.ch == 'r') { sel_id_ = id; ibuf_ = alias_.count(id) ? alias_[id] : ""; renaming_ = true; return true; }
        }
        return false;
    }

    void render(AppContext& ctx, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        build(ctx);
        char cnt[20]; std::snprintf(cnt, sizeof cnt, "%d known", (int)view_.size());
        int top = ui::header(c, "Contacts", ui::BrightYellow, cnt);
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)view_.size(), [&](int i) {
            const mesh::Node& n = view_[i];
            std::string star = fav_.count(n.id) ? "* " : "  ";
            auto it = alias_.find(n.id);
            std::string name = (it != alias_.end() && !it->second.empty()) ? it->second : n.long_name;
            return star + name + "  <" + n.short_name + ">";
        }, ui::White, ui::BrightYellow);
        ui::footer(c, " enter:chat  f:favorite  r:rename  esc:back ");
        if (renaming_) {
            int ir, ic, iw, ih;
            ui::modal_box(c, 5, 40, "Alias", ui::BrightYellow, ir, ic, iw, ih, "type  enter:ok  esc:cancel");
            ui::input_line(c, ir + 1, ic, "> ", ibuf_, ui::BrightWhite, iw);
        }
    }

private:
    void build(AppContext& ctx) {
        view_ = ctx.mesh ? ctx.mesh->nodes() : std::vector<mesh::Node>{};
        std::stable_sort(view_.begin(), view_.end(), [&](const mesh::Node& a, const mesh::Node& b) {
            bool fa = fav_.count(a.id), fb = fav_.count(b.id);
            if (fa != fb) return fa > fb;            // favorites first
            return disp(a) < disp(b);
        });
    }
    std::string disp(const mesh::Node& n) {
        auto it = alias_.find(n.id);
        return (it != alias_.end() && !it->second.empty()) ? it->second : n.long_name;
    }
    std::string dump() const {
        std::string s;
        std::set<uint32_t> ids;
        for (auto& kv : alias_) ids.insert(kv.first);
        for (auto id : fav_) ids.insert(id);
        for (auto id : ids) {
            auto it = alias_.find(id);
            s += std::to_string(id) + "\t" + (fav_.count(id) ? "1" : "0") + "\t" +
                 (it != alias_.end() ? it->second : "") + "\n";
        }
        return s;
    }
    void load(const std::string& s) {
        alias_.clear(); fav_.clear();
        size_t i = 0;
        while (i < s.size()) {
            size_t nl = s.find('\n', i);
            std::string ln = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            size_t t1 = ln.find('\t');
            size_t t2 = (t1 == std::string::npos) ? std::string::npos : ln.find('\t', t1 + 1);
            if (t1 != std::string::npos && t2 != std::string::npos) {
                uint32_t id = (uint32_t)std::strtoul(ln.substr(0, t1).c_str(), nullptr, 10);
                if (ln[t1 + 1] == '1') fav_.insert(id);
                std::string a = ln.substr(t2 + 1);
                if (!a.empty()) alias_[id] = a;
            }
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
    }

    std::vector<mesh::Node> view_;
    std::map<uint32_t, std::string> alias_;
    std::set<uint32_t> fav_;
    ui::ListState ls_;
    int rows_ = 1;
    bool renaming_ = false;
    uint32_t sel_id_ = 0;
    std::string ibuf_;
};

std::unique_ptr<App> make_contacts() { return std::make_unique<Contacts>(); }

} // namespace apps
