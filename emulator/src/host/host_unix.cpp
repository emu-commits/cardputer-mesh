#include "host/host_unix.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace host {

// ---- StdoutTerminal --------------------------------------------------------
void StdoutTerminal::write(const std::string& bytes) {
    ssize_t off = 0, n = (ssize_t)bytes.size();
    while (off < n) {
        ssize_t w = ::write(STDOUT_FILENO, bytes.data() + off, n - off);
        if (w <= 0) break;
        off += w;
    }
}
void StdoutTerminal::on_start() { write("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H"); }
void StdoutTerminal::on_stop()  { write("\x1b[0m\x1b[?25h\x1b[?1049l"); }

// ---- PtyTerminal -----------------------------------------------------------
PtyTerminal::PtyTerminal() {
    master_ = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_ >= 0 && grantpt(master_) == 0 && unlockpt(master_) == 0) {
        const char* sp = ptsname(master_);
        if (sp) slave_ = sp;
    }
}
PtyTerminal::~PtyTerminal() { if (master_ >= 0) ::close(master_); }
void PtyTerminal::write(const std::string& bytes) {
    if (master_ >= 0) { ssize_t r = ::write(master_, bytes.data(), bytes.size()); (void)r; }
}

// ---- RawInput --------------------------------------------------------------
void RawInput::start() {
    if (active_) return;
    tcgetattr(STDIN_FILENO, &saved_);
    termios raw = saved_;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);
    active_ = true;
}
void RawInput::stop() {
    if (!active_) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
    active_ = false;
}

std::vector<ui::KeyEvent> RawInput::poll() {
    std::vector<ui::KeyEvent> out;
    unsigned char buf[64];
    ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
    if (n <= 0) return out;

    for (ssize_t i = 0; i < n; ++i) {
        unsigned char b = buf[i];
        ui::KeyEvent e;
        if (b == 0x1b) {
            if (i + 1 < n && buf[i + 1] == '[') {
                i += 2;
                if (i < n) {
                    switch (buf[i]) {
                        case 'A': e.key = ui::Key::Up; break;
                        case 'B': e.key = ui::Key::Down; break;
                        case 'C': e.key = ui::Key::Right; break;
                        case 'D': e.key = ui::Key::Left; break;
                        case 'H': e.key = ui::Key::Home; break;
                        case 'F': e.key = ui::Key::End; break;
                        case '3': e.key = ui::Key::Delete; if (i + 1 < n && buf[i+1]=='~') i++; break;
                        case '5': e.key = ui::Key::PageUp; if (i + 1 < n && buf[i+1]=='~') i++; break;
                        case '6': e.key = ui::Key::PageDown; if (i + 1 < n && buf[i+1]=='~') i++; break;
                        default: e.key = ui::Key::Esc; break;
                    }
                }
            } else if (i + 1 < n) {
                // Alt + char
                e.key = ui::Key::Char; e.alt = true; e.ch = buf[++i];
            } else {
                e.key = ui::Key::Esc;
            }
        } else if (b == '\r' || b == '\n') {
            e.key = ui::Key::Enter;
        } else if (b == 0x7f || b == 0x08) {
            e.key = ui::Key::Backspace;
        } else if (b == '\t') {
            e.key = ui::Key::Tab;
        } else if (b >= 1 && b <= 26) {
            // Ctrl-letter (Ctrl-A == 1). Exclude the ones handled above.
            e.key = ui::Key::Char; e.ctrl = true; e.ch = 'a' + (b - 1);
        } else if (b >= 0x20 && b < 0x7f) {
            e.key = ui::Key::Char; e.ch = b;
        } else {
            continue;
        }
        out.push_back(e);
    }
    return out;
}

// ---- time ------------------------------------------------------------------
uint32_t now_ms() {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - start).count();
}

} // namespace host
