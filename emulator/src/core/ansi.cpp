#include "core/ansi.h"

namespace ui {

static std::string sgr(const Cell& c) {
    std::string s = "\x1b[0";
    if (c.attr & ATTR_BOLD) s += ";1";
    if (c.attr & ATTR_DIM) s += ";2";
    if (c.attr & ATTR_UNDERLINE) s += ";4";
    if (c.attr & ATTR_INVERSE) s += ";7";
    int fg = c.fg;
    s += ';' + std::to_string(fg < 8 ? 30 + fg : 90 + (fg - 8));
    int bg = c.bg;
    s += ';' + std::to_string(bg < 8 ? 40 + bg : 100 + (bg - 8));
    s += 'm';
    return s;
}

static void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void AnsiRenderer::render(const TextCanvas& cur, Terminal& term) {
    std::string out;
    bool full = (prev_.width() != cur.width() || prev_.height() != cur.height());
    if (full) {
        prev_.resize(cur.width(), cur.height());
        out += "\x1b[2J"; // clear
    }

    for (int r = 0; r < cur.height(); ++r) {
        bool changed = full;
        if (!changed) {
            for (int c = 0; c < cur.width(); ++c)
                if (cur.at(r, c) != prev_.at(r, c)) { changed = true; break; }
        }
        if (!changed) continue;

        out += "\x1b[" + std::to_string(r + 1) + ";1H"; // cursor to row, col 1
        std::string last_sgr;
        for (int c = 0; c < cur.width(); ++c) {
            const Cell& cell = cur.at(r, c);
            std::string s = sgr(cell);
            if (s != last_sgr) { out += s; last_sgr = s; }
            append_utf8(out, cell.cp);
        }
        out += "\x1b[0m";
    }

    // Save current frame as previous.
    prev_.resize(cur.width(), cur.height());
    for (int r = 0; r < cur.height(); ++r)
        for (int c = 0; c < cur.width(); ++c)
            prev_.at(r, c) = cur.at(r, c);

    if (!out.empty()) {
        term.write(out);
        term.flush();
    }
}

} // namespace ui
