// Contacts — an address book of full contact records. Each record: name, short
// name, LoRa node id, phone, location, notes, and a local address-book favorite
// (distinct from the Meshtastic node favorite in Nodes). Add/edit via a
// multi-field form; message a contact (DM) jumps to chat. Receives
// "contact:<id>\t<long>\t<short>" intents from the Nodes app.
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
    struct Rec { uint32_t id = 0; std::string name, shortn, phone, location, notes; bool fav = false; };
    enum Field { F_NAME, F_SHORT, F_ID, F_PHONE, F_LOC, F_NOTES, F_FAV, F_SAVE, F_COUNT };
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
            std::string nm = r.name.empty() ? (r.shortn.empty() ? "(unnamed)" : r.shortn) : r.name;
            char idb[12]; std::snprintf(idb, sizeof idb, "!%08x", r.id);
            return std::string(r.fav ? "* " : "  ") + ui::fit(nm, 22) + (r.id ? idb : "");
        }, ui::White, ui::BrightYellow);
        ui::footer(c, " a:add  e:edit  m:msg  f:fav  d:del  esc ");
        if (form_) render_form(c);
    }

private:
    // per-field hard caps so you can't type off-screen (#6); short name follows
    // the Meshtastic 4-char limit.
    static int cap(Field f) {
        switch (f) { case F_NAME: return 32; case F_SHORT: return 4; case F_PHONE: return 16;
                     case F_LOC: return 24; case F_NOTES: return 34; default: return 0; }
    }
    std::string* text_field(Field f) {
        switch (f) {
            case F_NAME: return &edit_.name;
            case F_SHORT: return &edit_.shortn;
            case F_PHONE: return &edit_.phone;
            case F_LOC: return &edit_.location;
            case F_NOTES: return &edit_.notes;
            default: return nullptr;
        }
    }

    void consume_intent(AppContext& ctx) {
        if (ctx.nav_arg.rfind("contact:", 0) != 0) return;
        std::string s = ctx.nav_arg.substr(8); ctx.nav_arg.clear();
        std::vector<std::string> f = splitTab(s); // "<id>\t<long>\t<short>"
        open_form(-1);
        if (f.size() >= 1) edit_.id = (uint32_t)std::strtoul(f[0].c_str(), nullptr, 10);
        if (f.size() >= 2) edit_.name = f[1].substr(0, cap(F_NAME));
        if (f.size() >= 3) edit_.shortn = f[2].substr(0, cap(F_SHORT));
        char idb[12]; std::snprintf(idb, sizeof idb, "%08x", edit_.id); idbuf_str_ = (edit_.id ? idb : "");
    }

    void open_form(int idx) {
        editing_ = idx;
        edit_ = (idx >= 0 && idx < (int)recs_.size()) ? recs_[idx] : Rec{};
        char idb[12]; std::snprintf(idb, sizeof idb, "%08x", edit_.id); idbuf_str_ = (edit_.id ? idb : "");
        form_ = true; focus_ = F_NAME;
    }
    bool form_key(AppContext& ctx, const KeyEvent& k) {
        if (k.key == Key::Esc) { form_ = false; return true; }
        if (k.key == Key::Up) { focus_ = (focus_ + F_COUNT - 1) % F_COUNT; return true; }
        if (k.key == Key::Down || k.key == Key::Tab) { focus_ = (focus_ + 1) % F_COUNT; return true; }
        if (focus_ == F_SAVE) { if (k.key == Key::Enter) { commit(ctx); form_ = false; } return true; }
        if (focus_ == F_FAV) { if (k.key == Key::Enter || (k.is_char() && k.ch == ' ')) edit_.fav = !edit_.fav; return true; }
        if (focus_ == F_ID) {
            if (k.key == Key::Backspace) { if (!idbuf_str_.empty()) { idbuf_str_.pop_back(); edit_.id = (uint32_t)std::strtoul(idbuf_str_.c_str(), nullptr, 16); } return true; }
            if (k.is_char() && std::isxdigit((unsigned char)k.ch) && idbuf_str_.size() < 8) { idbuf_str_ += (char)std::tolower((int)k.ch); edit_.id = (uint32_t)std::strtoul(idbuf_str_.c_str(), nullptr, 16); }
            return true;
        }
        std::string* t = text_field((Field)focus_);
        if (!t) return true;
        if (k.key == Key::Backspace) { if (!t->empty()) t->pop_back(); return true; }
        if (k.is_char() && k.ch >= 0x20 && k.ch < 0x7f && (int)t->size() < cap((Field)focus_)) *t += (char)k.ch;
        return true;
    }

    void render_form(TextCanvas& c) {
        int ir, ic, iw, ih;
        ui::modal_box(c, F_COUNT + 2, 46, editing_ >= 0 ? "Edit contact" : "New contact", ui::BrightYellow,
                      ir, ic, iw, ih, "up/dn:field  type:edit  Save row");
        char idb[16]; std::snprintf(idb, sizeof idb, "!%s", idbuf_str_.empty() ? "00000000" : idbuf_str_.c_str());
        row(c, ir + F_NAME,  ic, iw, "Name ", edit_.name, focus_ == F_NAME);
        row(c, ir + F_SHORT, ic, iw, "Short", edit_.shortn, focus_ == F_SHORT);
        row(c, ir + F_ID,    ic, iw, "LoRa ", idb, focus_ == F_ID);
        row(c, ir + F_PHONE, ic, iw, "Phone", edit_.phone, focus_ == F_PHONE);
        row(c, ir + F_LOC,   ic, iw, "Loc  ", edit_.location, focus_ == F_LOC);
        row(c, ir + F_NOTES, ic, iw, "Notes", edit_.notes, focus_ == F_NOTES);
        row(c, ir + F_FAV,   ic, iw, "Fav  ", edit_.fav ? "On" : "Off", focus_ == F_FAV);
        c.text(ir + F_SAVE, ic, ui::fit("  [ Save ]", iw), focus_ == F_SAVE ? ui::Black : ui::BrightGreen,
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
        persist(ctx);
    }
    void sort_recs() {
        std::stable_sort(recs_.begin(), recs_.end(), [](const Rec& a, const Rec& b) {
            if (a.fav != b.fav) return a.fav > b.fav;
            return a.name < b.name;
        });
    }
    void persist(AppContext& ctx) { if (ctx.state) ctx.state->set("contacts.records", dump()); }

    static std::vector<std::string> splitTab(const std::string& s) {
        std::vector<std::string> out; size_t i = 0;
        for (;;) { size_t t = s.find('\t', i); out.push_back(s.substr(i, t == std::string::npos ? std::string::npos : t - i)); if (t == std::string::npos) break; i = t + 1; }
        return out;
    }
    // record: id\tname\tshort\tfav\tphone\tlocation\tnotes
    std::string dump() const {
        std::string s;
        for (auto& r : recs_)
            s += std::to_string(r.id) + "\t" + r.name + "\t" + r.shortn + "\t" + (r.fav ? "1" : "0") + "\t" +
                 r.phone + "\t" + r.location + "\t" + r.notes + "\n";
        return s;
    }
    void load(const std::string& s) {
        recs_.clear(); size_t i = 0;
        while (i < s.size()) {
            size_t nl = s.find('\n', i);
            std::string ln = s.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
            auto f = splitTab(ln);
            if (f.size() >= 4) {
                Rec r; r.id = (uint32_t)std::strtoul(f[0].c_str(), nullptr, 10);
                r.name = f[1]; r.shortn = f[2]; r.fav = (f[3] == "1");
                if (f.size() >= 7) { r.phone = f[4]; r.location = f[5]; r.notes = f[6]; } // new format
                else if (f.size() >= 5) r.notes = f[4];                                   // old format
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
