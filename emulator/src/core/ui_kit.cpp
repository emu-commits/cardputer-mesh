#include "core/ui_kit.h"

namespace ui {

std::string fit(const std::string& s, int w, char pad) {
    if (w <= 0) return "";
    std::string out = s;
    if ((int)out.size() > w) out.resize(w);
    else out.append(w - (int)out.size(), pad);
    return out;
}

int header(TextCanvas& c, const std::string& title, uint8_t accent,
           const std::string& right) {
    c.text(0, 1, title, accent, Black, ATTR_BOLD);
    if (!right.empty()) {
        int col = c.width() - 1 - (int)right.size();
        if (col < (int)title.size() + 3) col = (int)title.size() + 3;
        if (col < c.width()) c.text(0, col, right, Gray, Black);
    }
    c.hline(1, 1, c.width() - 2, U'-', Gray, Black);
    return 2;
}

void footer(TextCanvas& c, const std::string& hint) {
    c.text(c.height() - 1, 0, fit(hint, c.width()), Black, Cyan, ATTR_INVERSE);
}

void footer2(TextCanvas& c, const std::string& l1, const std::string& l2) {
    c.text(c.height() - 2, 0, fit(l1, c.width()), Black, Cyan, ATTR_INVERSE);
    c.text(c.height() - 1, 0, fit(l2, c.width()), Black, Cyan, ATTR_INVERSE);
}

void ListState::clamp(int n, int rows) {
    if (n <= 0) { sel = 0; top = 0; return; }
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    if (rows <= 0) { top = 0; return; }
    if (sel < top) top = sel;
    if (sel >= top + rows) top = sel - rows + 1;
    int maxTop = n - rows; if (maxTop < 0) maxTop = 0;
    if (top > maxTop) top = maxTop;
    if (top < 0) top = 0;
}

bool ListState::move(const KeyEvent& k, int n, int rows) {
    if (n <= 0) return k.key == Key::Up || k.key == Key::Down;
    bool handled = true;
    switch (k.key) {
        case Key::Up:       sel = (sel > 0) ? sel - 1 : n - 1; break;     // wrap to bottom
        case Key::Down:     sel = (sel + 1 < n) ? sel + 1 : 0; break;     // wrap to top
        case Key::PageUp:   sel -= rows; break;
        case Key::PageDown: sel += rows; break;
        case Key::Home:     sel = 0; break;
        case Key::End:      sel = n - 1; break;
        default: handled = false; break;
    }
    if (handled) clamp(n, rows);
    return handled;
}

// region-aware list: renders inside [col, col+width) so it stays within a modal box
// (fit clips each row to the region; scrollbar sits at the region's right edge).
void list(TextCanvas& c, int r0, int col, int rows, int width, ListState& ls, int n,
          const std::function<std::string(int)>& item, uint8_t fg, uint8_t accent) {
    ls.clamp(n, rows); // keep scroll window consistent with current row count
    bool bar = n > rows; // need a scrollbar?
    int textW = width - (bar ? 1 : 0);
    for (int r = 0; r < rows; ++r) {
        int i = ls.top + r;
        if (i >= n) break;
        bool sel = (i == ls.sel);
        std::string line = (sel ? "> " : "  ") + item(i);
        c.text(r0 + r, col, fit(line, textW), sel ? (uint8_t)BrightWhite : fg, Black,
               sel ? ATTR_INVERSE : ATTR_NONE);
    }
    if (bar) {
        // thumb proportional to window over total
        int thumb = rows * rows / n; if (thumb < 1) thumb = 1; if (thumb > rows) thumb = rows;
        int denom = (n - rows); if (denom < 1) denom = 1;
        int pos = (rows - thumb) * ls.top / denom;
        for (int r = 0; r < rows; ++r) {
            bool on = (r >= pos && r < pos + thumb);
            c.put(r0 + r, col + width - 1, on ? U'█' : U'│', on ? accent : (uint8_t)Gray, Black);
        }
    }
}

// full-width convenience (body lists): the original signature, now a thin wrapper.
void list(TextCanvas& c, int r0, int rows, ListState& ls, int n,
          const std::function<std::string(int)>& item, uint8_t fg, uint8_t accent) {
    list(c, r0, 0, rows, c.width(), ls, n, item, fg, accent);
}

void modal_box(TextCanvas& c, int rows, int cols, const std::string& title,
               uint8_t accent, int& innerR, int& innerC, int& innerW, int& innerH,
               const std::string& foot) {
    if (cols > c.width()) cols = c.width();
    if (rows > c.height()) rows = c.height();
    int r = (c.height() - rows) / 2;
    int col = (c.width() - cols) / 2;
    c.fill_rect(r, col, rows, cols, U' ', White, Black);
    c.draw_box(r, col, rows, cols, accent, Black, ATTR_BOLD);
    if (!title.empty())
        c.text(r, col + 2, " " + title + " ", accent, Black, ATTR_BOLD);
    if (!foot.empty())
        c.text(r + rows - 1, col + 2, " " + fit(foot, cols - 4) + " ", Gray, Black);
    innerR = r + 1; innerC = col + 2; innerW = cols - 4; innerH = rows - 2;
}

void input_line(TextCanvas& c, int r, int col, const std::string& label,
                const std::string& buf, uint8_t fg, int max_w) {
    int avail = c.width() - col;
    if (max_w > 0 && max_w < avail) avail = max_w;
    if (avail < 1) return;
    int field = avail - 1; // reserve a column for the caret block
    std::string s = label + buf;
    if ((int)s.size() > field) s = s.substr(s.size() - field); // keep the tail visible
    c.text(r, col, s, fg, Black);
    c.put(r, col + (int)s.size(), U'█', fg, Black, ATTR_BOLD);
}

std::vector<std::string> wrap_text(const std::string& text, int w) {
    std::vector<std::string> out;
    if (w < 1) w = 1;
    size_t i = 0;
    for (;;) {
        size_t nl = text.find('\n', i);
        std::string para = text.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        if (!para.empty() && para.back() == '\r') para.pop_back();

        std::string cur;
        size_t p = 0;
        while (p < para.size()) {
            // next token, up to and including a break char (space or '-')
            size_t q = p;
            while (q < para.size() && para[q] != ' ' && para[q] != '-') ++q;
            if (q < para.size()) ++q; // include the trailing space or hyphen
            std::string tok = para.substr(p, q - p);
            p = q;
            // hard-break a token longer than the line
            while ((int)tok.size() > w) {
                while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
                out.push_back(tok.substr(0, w));
                tok = tok.substr(w);
            }
            if ((int)(cur.size() + tok.size()) <= w) {
                cur += tok;
            } else {
                while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                out.push_back(cur);
                cur = tok;
            }
        }
        while (!cur.empty() && cur.back() == ' ') cur.pop_back();
        out.push_back(cur); // keep blank lines (preserves paragraph spacing)

        if (nl == std::string::npos) break;
        i = nl + 1;
    }
    return out;
}

} // namespace ui
