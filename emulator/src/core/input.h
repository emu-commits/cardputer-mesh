// Keyboard event model — platform-independent. The host parses termios/stdin
// into these; on device the Cardputer keyboard HAL (KeysState) maps to the same.
#pragma once
#include <cstdint>

namespace ui {

enum class Key : uint8_t {
    None, Char, Enter, Esc, Backspace, Tab,
    Up, Down, Left, Right, Home, End, PageUp, PageDown, Delete
};

struct KeyEvent {
    Key key = Key::None;
    char32_t ch = 0;     // valid when key == Key::Char
    bool ctrl = false;
    bool alt = false;
    bool shift = false;

    bool is_char() const { return key == Key::Char; }
};

} // namespace ui
