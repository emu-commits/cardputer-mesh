// ui_kit — the shared widget vocabulary every app renders with, so the suite
// feels like one product on a small screen. Encodes the §5.1 rule from
// ARCHITECTURE.md: each primary view is ONE scrolling list; everything else is a
// modal overlay. Portable (core): no platform deps, compiled into the firmware.
//
// UI lessons unified here:
//  - Chrome: row 0 = title bar (accent name left, status right), row 1 = rule,
//    last row = inverse key-hint footer. Body is everything between.
//  - One list model (ListState + list()) — selection gutter "> ", inverse
//    highlight, auto-scroll window, edge scrollbar. No app re-implements this.
//  - Modal overlays (modal_box) for info / pickers / prompts / confirm.
//  - One text-input affordance (input_line) with a blinking-less caret.
//  - Shared keymap helpers so up/down/pgup/pgdn/home/end behave identically.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "core/input.h"
#include "core/text_canvas.h"

namespace ui {

// Per-app accent colors (title + selection tint) — a small consistent palette.
// Apps pick one so the user can tell modes apart at a glance.

// ---- chrome ---------------------------------------------------------------
// Title bar at row 0 (accent, bold) + optional right-aligned status; rule at
// row 1. Returns the first body row (2).
int header(TextCanvas& c, const std::string& title, uint8_t accent,
           const std::string& right = "");
// Full-width inverse hint bar on the last row.
void footer(TextCanvas& c, const std::string& hint);
// Two-row inverse hint bar on the last two rows (for hints too long for one row).
void footer2(TextCanvas& c, const std::string& l1, const std::string& l2);
// First body row / last body row given the standard chrome (header + footer).
inline int body_top(const TextCanvas&) { return 2; }
inline int body_bottom(const TextCanvas& c) { return c.height() - 2; }

// ---- list (the one scrolling-list model) ----------------------------------
struct ListState {
    int sel = 0;
    int top = 0; // index of first visible row
    // Apply a navigation key for n items over `rows` visible lines. Returns true
    // if the key was a navigation key (and was consumed).
    bool move(const KeyEvent& k, int n, int rows);
    void clamp(int n, int rows);
};

// Render items [ls.top ..] into rows [r0, r0+rows). item(i) -> display text
// (already sized by the caller or auto-padded here). Selected row gets a "> "
// gutter + inverse highlight in `accent`. Draws a 1-col scrollbar at the right
// edge when n > rows.
// Clamps `ls` to (n, rows) first, so the scroll window is always consistent with
// the current row count (fixes stale `top` after a selection was restored).
void list(TextCanvas& c, int r0, int rows, ListState& ls, int n,
          const std::function<std::string(int)>& item,
          uint8_t fg = White, uint8_t accent = BrightWhite);
// region-aware variant: renders inside [col, col+width) (e.g. a modal's inner rect)
// so the rows + scrollbar stay within the box instead of spanning the whole screen.
void list(TextCanvas& c, int r0, int col, int rows, int width, ListState& ls, int n,
          const std::function<std::string(int)>& item,
          uint8_t fg = White, uint8_t accent = BrightWhite);

// ---- overlays --------------------------------------------------------------
// Draw a centered modal box (rows x cols) with a title; clears its interior and
// returns the inner content rect via out-params (caller fills it). Footer hint
// is drawn on the box's bottom border row.
void modal_box(TextCanvas& c, int rows, int cols, const std::string& title,
               uint8_t accent, int& innerR, int& innerC, int& innerW, int& innerH,
               const std::string& foot = "");

// ---- text input ------------------------------------------------------------
// "label buf_" with a caret block at the end. The text scrolls to keep the
// caret (tail) visible. `max_w` bounds the field width in columns (e.g. a
// modal's inner width); -1 means "to the right edge of the canvas".
void input_line(TextCanvas& c, int r, int col, const std::string& label,
                const std::string& buf, uint8_t fg = BrightWhite, int max_w = -1);

// Truncate/pad a string to exactly w columns (ASCII).
std::string fit(const std::string& s, int w, char pad = ' ');

// Word-wrap `text` to width `w`. Breaks on spaces AND after hyphens (so a
// hyphenated word can split at the hyphen but never mid-word); over-long tokens
// are hard-broken as a last resort. Honors embedded '\n' (and '\r\n') as hard
// line breaks, preserving blank lines. Used by the journal/wiki readers.
std::vector<std::string> wrap_text(const std::string& text, int w);

} // namespace ui
