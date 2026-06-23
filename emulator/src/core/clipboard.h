// Clipboard — one bounded resident text buffer shared across apps (§5).
// Portable (core); a single instance lives in AppContext. On device it's the
// same ~8 KB resident buffer outside the per-app arena.
#pragma once
#include <string>

namespace clip {

class Clipboard {
public:
    void set(const std::string& s) { buf_ = (s.size() > CAP) ? s.substr(0, CAP) : s; }
    const std::string& get() const { return buf_; }
    bool empty() const { return buf_.empty(); }
private:
    static constexpr size_t CAP = 8 * 1024;
    std::string buf_;
};

} // namespace clip
