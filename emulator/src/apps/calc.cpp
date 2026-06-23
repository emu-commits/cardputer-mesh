// Calc — expression + unit converter with a scrolling history tape.
// UX lessons (Soulver/qalc/emacs-calc): the tape IS the primary list; you always
// see a live "= preview" of the in-progress line before committing; `ans` chains
// the last result. One input line, no modes, no panes (§5.1).
#include "apps/apps.h"
#include <vector>
#include "core/eval.h"
#include "core/ui_kit.h"

using namespace app;
using ui::Key; using ui::KeyEvent; using ui::TextCanvas;

namespace apps {

class Calc : public App {
public:
    void on_create(AppContext& ctx) override {
        if (ctx.state) {
            input_ = ctx.state->get("calc.input", "");
            // restore a few tape lines so the history survives a relaunch
            std::string h = ctx.state->get("calc.tape", "");
            size_t i = 0;
            while (i < h.size()) {
                size_t nl = h.find('\n', i);
                std::string line = h.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
                if (!line.empty()) tape_.push_back(line);
                if (nl == std::string::npos) break;
                i = nl + 1;
            }
        }
        ls_.sel = (int)tape_.size();
    }
    void on_pause(AppContext& ctx) override {
        if (!ctx.state) return;
        ctx.state->set("calc.input", input_);
        std::string h;
        int start = (int)tape_.size() > KEEP ? (int)tape_.size() - KEEP : 0;
        for (int i = start; i < (int)tape_.size(); ++i) h += tape_[i] + "\n";
        ctx.state->set("calc.tape", h);
    }

    bool on_key(AppContext&, const KeyEvent& k) override {
        if (ls_.move(k, (int)tape_.size(), rows_)) return true;
        if (k.key == Key::Enter) { commit(); return true; }
        if (k.key == Key::Backspace) { if (!input_.empty()) input_.pop_back(); return true; }
        if (k.is_char()) {
            if (k.ch == 'c' && input_.empty() && k.ctrl) { tape_.clear(); return true; }
            if (k.ch >= 0x20 && k.ch < 0x7f) { input_ += (char)k.ch; return true; }
        }
        return false;
    }

    void render(AppContext&, TextCanvas& c) override {
        c.clear(ui::White, ui::Black);
        int top = ui::header(c, "Calc", ui::BrightYellow,
                             "ans=" + calc::format(ans_));
        // tape occupies body minus 2 rows (preview + input)
        int bottom = c.height() - 3;
        rows_ = bottom - top + 1;
        ui::list(c, top, rows_, ls_, (int)tape_.size(),
                 [&](int i) { return tape_[i]; }, ui::Gray, ui::BrightYellow);

        // live preview of the in-progress expression
        calc::Result pv = calc::evaluate(input_, ans_);
        std::string prev = input_.empty() ? "" : (pv.ok ? "= " + pv.display
                                                         : (pv.error.empty() ? "" : "= ?"));
        c.text(c.height() - 2, 1, ui::fit(prev, c.width() - 2),
               pv.ok ? ui::BrightGreen : ui::Red, ui::Black);
        ui::input_line(c, c.height() - 1, 0, "> ", input_);
    }

private:
    void commit() {
        std::string s = input_;
        // strip leading/trailing spaces for the empty check
        if (s.find_first_not_of(" \t") == std::string::npos) return;
        calc::Result r = calc::evaluate(s, ans_);
        std::string line = s + "  =  " + (r.ok ? r.display : "err: " + r.error);
        tape_.push_back(line);
        if (r.ok) ans_ = r.value;
        input_.clear();
        ls_.sel = (int)tape_.size(); // keep view pinned to the newest line
    }

    static constexpr int KEEP = 64;
    std::vector<std::string> tape_;
    std::string input_;
    double ans_ = 0;
    ui::ListState ls_;
    int rows_ = 1;
};

std::unique_ptr<App> make_calc() { return std::make_unique<Calc>(); }

} // namespace apps
