#include "core/text_canvas.h"

namespace ui {

void TextCanvas::resize(int w, int h) {
    w_ = w; h_ = h;
    cells_.assign(static_cast<size_t>(w) * h, Cell{});
}

void TextCanvas::clear(uint8_t fg, uint8_t bg) {
    Cell blank;
    blank.cp = U' '; blank.fg = fg; blank.bg = bg; blank.attr = ATTR_NONE;
    for (auto& c : cells_) c = blank;
}

void TextCanvas::put(int r, int c, char32_t cp, uint8_t fg, uint8_t bg, uint8_t attr) {
    if (!in_bounds(r, c)) return;
    Cell& cell = at(r, c);
    cell.cp = cp; cell.fg = fg; cell.bg = bg; cell.attr = attr;
}

int TextCanvas::text(int r, int c, const std::string& s, uint8_t fg, uint8_t bg, uint8_t attr) {
    int col = c, n = 0;
    for (unsigned char ch : s) {
        if (col >= w_) break;
        if (ch == '\n' || ch == '\t') ch = ' ';
        if (ch < 0x20) continue; // skip other control bytes
        put(r, col, static_cast<char32_t>(ch), fg, bg, attr);
        ++col; ++n;
    }
    return n;
}

void TextCanvas::hline(int r, int c, int len, char32_t cp, uint8_t fg, uint8_t bg, uint8_t attr) {
    for (int i = 0; i < len; ++i) put(r, c + i, cp, fg, bg, attr);
}

void TextCanvas::fill_rect(int r, int c, int rows, int cols, char32_t cp, uint8_t fg, uint8_t bg, uint8_t attr) {
    for (int dr = 0; dr < rows; ++dr)
        for (int dc = 0; dc < cols; ++dc)
            put(r + dr, c + dc, cp, fg, bg, attr);
}

void TextCanvas::draw_box(int r, int c, int rows, int cols, uint8_t fg, uint8_t bg, uint8_t attr) {
    if (rows < 2 || cols < 2) return;
    hline(r, c, cols, U'-', fg, bg, attr);
    hline(r + rows - 1, c, cols, U'-', fg, bg, attr);
    for (int dr = 1; dr < rows - 1; ++dr) {
        put(r + dr, c, U'|', fg, bg, attr);
        put(r + dr, c + cols - 1, U'|', fg, bg, attr);
    }
    put(r, c, U'+', fg, bg, attr);
    put(r, c + cols - 1, U'+', fg, bg, attr);
    put(r + rows - 1, c, U'+', fg, bg, attr);
    put(r + rows - 1, c + cols - 1, U'+', fg, bg, attr);
}

void TextCanvas::blit(const TextCanvas& src, int dstRow, int dstCol) {
    for (int r = 0; r < src.height(); ++r)
        for (int c = 0; c < src.width(); ++c) {
            int tr = dstRow + r, tc = dstCol + c;
            if (in_bounds(tr, tc)) at(tr, tc) = src.at(r, c);
        }
}

} // namespace ui
