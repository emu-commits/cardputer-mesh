// Contacts — an address book of full contact records (not just node aliases).
// Each record: long name, short name, LoRa node id, notes, and a local
// address-book favorite (distinct from the Meshtastic node favorite in Nodes).
// Add/edit via a multi-field form overlay; message a contact (DM) jumps to chat.
// Receives "contact:<id>\t<long>\t<short>" intents from the Nodes app (#14).
#include "apps/apps.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Contacts : public App {
    struct Rec { uint32_t id = 0; std::string longn, shortn, notes; bool fav = false; };
    enum Field { F_LONG, F_SHORT, F_ID, F_NOTES, F_FAV, F_SAVE, F_COUNT };
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) load(ctx.state->get("contacts.records", ""));
        consume_intent(ctx);
    }
    void on_resume(AppContext& ctx) override { consume_intent(ctx); }
    void on_pause(AppContext& ctx) override { if (ctx.state) ctx.state->set("contacts.records", dump()); }

    bool on_key(AppContext& ctx, const KeyEvent& k) override {
        if (form_) return form_key(ctx, k);
        if (ls_.move(k, (int)recs_.size(), rows_)) return true;
        if (k.is_char() && k.ch == 'a') { open_form(-1); return true; }
        if (recs_.empty()) return false;
        Rec& r = recs_[ls_.sel];
        if (k.key == Key::Enter || (k.is_char() && k.ch == 'e')) { open_form(ls_.sel); return true; }
        if (k.is_char()) {
            if (k.ch == 'm' && r.id) { ctx.nav_arg = "dm:" + std::to_string(r.id); ctx.apps->request_switch("chat"); return true; }
            if (k.ch == 'f') { r.fav = !r.fav; persist(ctx); return true; }
            if (k.ch == 'd') { recs_.erase(recs_.begin() + ls_.sel); ls_.clamp((int)recs_.size(), rows_); persist(ctx); return true; }
        }
        return false;
    }

    void render(AppContext&, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        sort_recs();
        char cnt[16]; std::snprintf(cnt, sizeof cnt, "%d", (int)recs_.size());
        int top = ui::header(c, "Contacts", ui::BrightYellow, cnt);
        rows_ = ui::body_bottom(c) - top + 1;
        ui::list(c, top, rows_, ls_, (int)recs_.size(), [&](int i) {
            const Rec& r = recs_[i];
            std::string nm = r.longn.empty() ? (r.shortn.empty() ? "(unnamed)" : r.shortn) : r.longn;
            char idb[12]; std::snprintf(idb, sizeof idb, "!%08x", r.id);
            return std::string(r.fav ? "* " : "  ") + ui::fit(nm, 22) + (r.id ? idb : "");
        }, ui::White, ui::BrightYellow);
        ui::footer(c, " a:add  enter/e:edit  m:message  f:fav  d:del  esc:back ");
        if (form_) render_form(c);
    }

private:
    void consume_intent(AppContext& ctx) {
        if (ctx.nav_arg.rfind("contact:", 0) != 0) return;
        std::string s = ctx.nav_arg.substr(8); ctx.nav_arg.clear();
        // "<id>\t<long>\t<short>"
        std::vector<std::string> f = splitTab(s);
        open_form(-1);
        if (f.size() >= 1) edit_.id = (uint32_t)std::strtoul(f[0].c_str(), nullptr, 10);
        if (f.size() >= 2) edit_.longn = f[1];
        if (f.size() >= 3) edit_.shortn = f[2];
    }

    void open_form(int idx) {
        editing_ = idx;
        edit_ = (idx >= 0 && idx < (int)recs_.size()) ? recs_[idx] : Rec{};
        form_ = true; focus_ = F_LONG;
    }
    std::string* text_field(Field f) {
        switch (f) {
            case F_LONG: return &edit_.longn;
            case F_SHORT: return &edit_.shortn;
            case F_NOTES: return &edit_.notes;
            default: return nullptr;
        }
    }
    bool form_key(AppContext& ctx, const KeyEvent& k) {
        if (k.key == Key::Esc) { form_ = false; return true; }
        if (k.key == Key::Up) { focus_ = (focus_ + F_COUNT - 1) % F_COUNT; return true; }
        if (k.key == Key::Down || (k.is_char() && k.ch == '\t')) { focus_ = (focus_ + 1) % F_COUNT; return true; }
        if (focus_ == F_SAVE) { if (k.key == Key::Enter) { commit(ctx); form_ = false; } return true; }
        if (focus_ == F_FAV) { if (k.is_char() && (k.ch == ' ')) edit_.fav = !edit_.fav; if (k.key == Key::Enter) edit_.fav = !edit_.fav; return true; }
        if (focus_ == F_ID) { // hex/decimal id entry
            if (k.key == Key::Backspace) { if (!idbuf_().empty()) id_pop(); return true; }
            if (k.is_char() && (std::isxdigit((int)k.ch) || k.ch == 'x' || k.ch == '!')) id_push((char)k.ch);
            return true;
        }
        std::string* t = text_field((Field)focus_);
        if (!t) return true;
        if (k.key == Key::Backspace) { if (!t->empty()) t->pop_back(); return true; }
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f) *t += (char)k.ch;
        return true;
    }
    // id editing via a scratch string mirrored to edit_.id
    std::string& idbuf_() { return idbuf_str_; }
    void id_push(char ch) { idbuf_str_ += ch; edit_.id = (uint32_t)std::strtoul(idclean().c_str(), nullptr, 16); }
    void id_pop() { idbuf_str_.pop_back(); edit_.id = (uint32_t)std::strtoul(idclean().c_str(), nullptr, 16); }
    std::string idclean() { std::string s; for (char c : idbuf_str_) if (c != '!' && c != 'x') s += c; return s; }

    void render_form(TextCanvas& c) {
        int ir, ic, iw, ih;
        ui::modal_box(c, 9, 46, editing_ >= 0 ? "Edit contact" : "New contact", ui::BrightYellow,
                      ir, ic, iw, ih, "up/dn:field  type:edit  enter on Save");
        char idb[16]; std::snprintf(idb, sizeof idb, "!%08x", edit_.id);
        row(c, ir + 0, ic, iw, "Long ", edit_.longn, focus_ == F_LONG);
        row(c, ir + 1, ic, iw, "Short", edit_.shortn, focus_ == F_SHORT);
        row(c, ir + 2, ic, iw, "LoRa ", idbuf_str_.empty() ? std::string(idb) : idbuf_str_, focus_ == F_ID);
        row(c, ir + 3, ic, iw, "Notes", edit_.notes, focus_ == F_NOTES);
        row(c, ir + 4, ic, iw, "Fav  ", edit_.fav ? "On" : "Off", focus_ == F_FAV);
        c.text(ir + 5, ic, ui::fit("  [ Save ]", iw), focus_ == F_SAVE ? ui::Black : ui::BrightGreen,
               focus_ == F_SAVE ? ui::BrightGreen : ui::Black, focus_ == F_SAVE ? ui::ATTR_INVERSE : ui::ATTR_NONE);
    }
    static void row(TextCanvas& c, int r, int col, int w, const char* label, const std::string& val, bool focus) {
        std::string line = std::string(label) + ": " + val;
        c.text(r, col, ui::fit(line, w), focus ? ui::Black : ui::White, focus ? ui::Cyan : ui::Black,
               focus ? ui::ATTR_INVERSE : ui::ATTR_NONE);
    }

    void commit(AppContext& ctx) {
        if (editing_ >= 0 && editing_ < (int)recs_.size()) recs_[editing_] = edit_;
        else recs_.push_back(edit_);
        idbuf_str_.clear();
        persist(ctx);
    }
    void sort_recs() {
        std::stable_sort(recs_.begin(), recs_.end(), [](const Rec& a, const Rec& b) {
            if (a.fav != b.fav) return a.fav > b.fav;
            return a.longn < b.longn;
        });
    }
    void persist(AppContext& ctx) { if (ctx.state) ctx.state->set("contacts.records", dump()); }

    static std::vector<std::string> splitTab(const std::string& s) {
        std::vector<std::string> out; size_t i = 0;
        for (;;) { size_t t = s.find('\t', i); out.push_back(s.substr(i, t == std::string::npos ? std::string::npos : t - i)); if (t == std::string::npos) break; i = t + 1; }
        return out;
    }
    std::string dump() const {
        std::string s;
        for (auto& r : recs_)
            s += std::to_string(r.id) + "\t" + r.longn + "\t" + r.shortn + "\t" + (r.fav ? "1" : "0") + "\t" + r.notes + "\n";
        return s;
    }
    void load(const std::string& s) {
        recs_.clear(); size_t i = 0;
        while (i < s.size()) {
            size_t nl = s.find('\n', i);
            std::string ln = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            auto f = splitTab(ln);
            if (f.size() >= 5) {
                Rec r; r.id = (uint32_t)std::strtoul(f[0].c_str(), nullptr, 10);
                r.longn = f[1]; r.shortn = f[2]; r.fav = (f[3] == "1"); r.notes = f[4];
                recs_.push_back(r);
            }
            if (nl == std::string::npos) break;
            i = nl + 1;
        }
    }

    std::vector<Rec> recs_;
    ui::ListState ls_;
    int rows_ = 1;
    bool form_ = false;
    int editing_ = -1, focus_ = 0;
    Rec edit_;
    std::string idbuf_str_;
};

std::unique_ptr<App> make_contacts() { return std::make_unique<Contacts>(); }

} // namespace apps
