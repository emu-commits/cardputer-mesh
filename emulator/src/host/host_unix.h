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
    std::string slave_path() const { return slave_; }
    bool ok() const { return master_ >= 0; }
private:
    int master_ = -1;
    std::string slave_;
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
