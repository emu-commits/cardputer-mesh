// Host (POSIX) glue: a terminal sink that drives the user's terminal as the CYD,
// a PTY sink (the faithful "CYD on a serial line" seam, for later), and raw
// keyboard input parsed into ui::KeyEvent.
#pragma once
#include <string>
#include <vector>
#include <termios.h>
#include "core/ansi.h"
#include "core/input.h"

namespace host {

// Drives the launching terminal: alt-screen + hidden cursor, restored on stop.
class StdoutTerminal : public ui::Terminal {
public:
    void write(const std::string& bytes) override;
    void on_start() override;
    void on_stop() override;
};

// Allocates a PTY; write() goes to the master. Attach a terminal to the printed
// slave path to see it (e.g. `screen /dev/pts/N`). The drop-in for the real CYD
// UART sink. Not wired by default in Phase 1.
class PtyTerminal : public ui::Terminal {
public:
    PtyTerminal();
    ~PtyTerminal() override;
    void write(const std::string& bytes) override;
    void on_start() override;
    void on_stop() override;
    std::string slave_path() const { return slave_; }
    bool ok() const { return master_ >= 0; }
    // True (and clears) if the last write(s) couldn't be fully delivered — e.g.
    // no terminal attached yet and the PTY buffer filled. Caller should force a
    // full repaint so state re-syncs once a reader drains the buffer.
    bool consume_dropped() { bool d = dropped_; dropped_ = false; return d; }
private:
    int master_ = -1;
    std::string slave_;
    bool dropped_ = false;
};

// Puts stdin in raw mode; poll() returns parsed key events (non-blocking).
class RawInput {
public:
    void start();
    void stop();
    std::vector<ui::KeyEvent> poll();
private:
    termios saved_{};
    bool active_ = false;
};

uint32_t now_ms(); // monotonic milliseconds since first call

} // namespace host
