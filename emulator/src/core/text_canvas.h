// TextCanvas — logical cell grid the whole UI renders into.
// Portable: no platform deps. On the device this is what the ANSI backend
// serialises to the CYD; on the host the same grid is rendered to a terminal.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ui {

// 16-colour palette (maps to ANSI 30-37/90-97 fg, 40-47/100-107 bg).
enum Color : uint8_t {
    Black = 0, Red, Green, Yellow, Blue, Magenta, Cyan, White,
    Gray, BrightRed, BrightGreen, BrightYellow, BrightBlue, BrightMagenta,
    BrightCyan, BrightWhite
};

enum Attr : uint8_t {
    ATTR_NONE = 0, ATTR_BOLD = 1, ATTR_DIM = 2, ATTR_UNDERLINE = 4, ATTR_INVERSE = 8
};

struct Cell {
    char32_t cp = U' ';
    uint8_t fg = White;
    uint8_t bg = Black;
    uint8_t attr = ATTR_NONE;
    bool operator==(const Cell& o) const {
        return cp == o.cp && fg == o.fg && bg == o.bg && attr == o.attr;
    }
    bool operator!=(const Cell& o) const { return !(*this == o); }
};

class TextCanvas {
public:
    TextCanvas() = default;
    TextCanvas(int w, int h) { resize(w, h); }

    void resize(int w, int h);
    int width() const { return w_; }
    int height() const { return h_; }

    void clear(uint8_t fg = White, uint8_t bg = Black);
    bool in_bounds(int r, int c) const { return r >= 0 && r < h_ && c >= 0 && c < w_; }
    Cell& at(int r, int c) { return cells_[r * w_ + c]; }
    const Cell& at(int r, int c) const { return cells_[r * w_ + c]; }

    void put(int r, int c, char32_t cp, uint8_t fg = White, uint8_t bg = Black, uint8_t attr = ATTR_NONE);
    // Writes ASCII text starting at (r,c), clipped to the row. Returns columns written.
    int text(int r, int c, const std::string& s, uint8_t fg = White, uint8_t bg = Black, uint8_t attr = ATTR_NONE);
    void hline(int r, int c, int len, char32_t cp = U'-', uint8_t fg = Gray, uint8_t bg = Black, uint8_t attr = ATTR_NONE);
    void fill_rect(int r, int c, int rows, int cols, char32_t cp, uint8_t fg, uint8_t bg, uint8_t attr = ATTR_NONE);
    void draw_box(int r, int c, int rows, int cols, uint8_t fg = Gray, uint8_t bg = Black, uint8_t attr = ATTR_NONE);
    // Copy another canvas into this one at (dstRow,dstCol).
    void blit(const TextCanvas& src, int dstRow, int dstCol);

private:
    int w_ = 0, h_ = 0;
    std::vector<Cell> cells_;
};

} // namespace ui
