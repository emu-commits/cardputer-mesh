// Persistence interface — return-to-last-position (ARCHITECTURE.md §6).
// Portable: apps use only this. On device it's backed by NVS (scalars) + SD
// files; on host by a flat key/value file (host::FileStore). Keys are namespaced
// per app, e.g. "session.active", "chat.scroll", "launcher.sel".
#pragma once
#include <cstdlib>
#include <string>

namespace persist {

class Store {
public:
    virtual ~Store() = default;
    virtual std::string get(const std::string& key, const std::string& def = "") = 0;
    virtual void set(const std::string& key, const std::string& val) = 0;
    virtual void flush() = 0; // commit to backing storage (checkpoint)

    int get_int(const std::string& key, int def = 0) {
        std::string v = get(key, "");
        if (v.empty()) return def;
        return (int)std::strtol(v.c_str(), nullptr, 10);
    }
    void set_int(const std::string& key, int v) { set(key, std::to_string(v)); }
};

} // namespace persist
