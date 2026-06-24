// FileSystem — the thin boundary the file browser (and later editor/jrnl/FZF)
// use to reach storage. Portable interface (core); on the host it's rooted at a
// sandbox directory (host::UnixFs), on device it wraps the SD card / LittleFS.
// Paths are POSIX-style and rooted: "/" is the storage root; entries never
// escape the root (".." above root is clamped).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fs {

struct Entry {
    std::string name;
    bool is_dir = false;
    uint64_t size = 0;
};

class FileSystem {
public:
    virtual ~FileSystem() = default;
    // List directory `path` (sorted: dirs first, then names). Returns false if
    // the path is not a readable directory.
    virtual bool list(const std::string& path, std::vector<Entry>& out) = 0;
    virtual bool is_dir(const std::string& path) = 0;
    virtual bool exists(const std::string& path) = 0;
    // Read up to max_bytes of a file as text. Returns false if not readable.
    virtual bool read_text(const std::string& path, std::string& out, size_t max_bytes) = 0;
    // Write/overwrite a file (creating parent dirs). Returns false on failure.
    virtual bool write_text(const std::string& path, const std::string& data) = 0;
    // Append to a file (creating it + parent dirs).
    virtual bool append_text(const std::string& path, const std::string& data) = 0;
    // Delete a file. Returns false if it didn't exist / couldn't be removed.
    virtual bool remove(const std::string& path) = 0;
    // Storage capacity of the backing volume (bytes). 0 if unknown.
    virtual uint64_t free_bytes() { return 0; }
    virtual uint64_t total_bytes() { return 0; }
    // Normalize/join: resolve `rel` against `base` (both rooted), clamping to root.
    virtual std::string join(const std::string& base, const std::string& rel) = 0;
};

} // namespace fs
