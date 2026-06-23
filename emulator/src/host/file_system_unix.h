// UnixFs — host FileSystem rooted at a sandbox directory, so the emulated file
// browser sees an SD-card-like tree without touching the rest of the machine.
// On first run it seeds the sandbox with a few sample files.
#pragma once
#include <string>
#include "core/fs.h"

namespace host {

class UnixFs : public fs::FileSystem {
public:
    explicit UnixFs(const std::string& root);

    bool list(const std::string& path, std::vector<fs::Entry>& out) override;
    bool is_dir(const std::string& path) override;
    bool exists(const std::string& path) override;
    bool read_text(const std::string& path, std::string& out, size_t max_bytes) override;
    bool write_text(const std::string& path, const std::string& data) override;
    bool append_text(const std::string& path, const std::string& data) override;
    std::string join(const std::string& base, const std::string& rel) override;

private:
    std::string real(const std::string& rooted); // rooted "/a/b" -> "<root>/a/b", clamped
    std::string root_;
};

} // namespace host
