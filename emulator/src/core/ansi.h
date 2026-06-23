// ANSI backend: diffs a TextCanvas against the previous frame and emits minimal
// ANSI/VT100 to a Terminal sink. This is the exact seam the real CYD uses:
// on device the sink is the Port-A UART; on host it's a terminal / PTY.
#pragma once
#include <string>
#include "core/text_canvas.h"

namespace ui {

// Output sink. Implemented per platform (host: stdout/PTY; device: UART).
class Terminal {
public:
    virtual ~Terminal() = default;
    virtual void write(const std::string& bytes) = 0;
    virtual void flush() {}
    virtual void on_start() {} // enter alt-screen, hide cursor, etc.
    virtual void on_stop() {}  // restore
};

class AnsiRenderer {
public:
    // Render cur to term, emitting only the rows that changed since last call.
    void render(const TextCanvas& cur, Terminal& term);
    void reset() { prev_.resize(0, 0); }

private:
    TextCanvas prev_;
};

} // namespace ui
