// Host-side persist::Store backed by a flat "key<TAB>value" file. Loads on
// construct, flush() rewrites if dirty. Values are scalars / short ids (no tabs
// or newlines), matching our resume tokens. On device this role is NVS + SD.
#pragma once
#include <map>
#include <string>
#include "core/persist.h"

namespace host {

class FileStore : public persist::Store {
public:
    explicit FileStore(const std::string& path);
    std::string get(const std::string& key, const std::string& def = "") override;
    void set(const std::string& key, const std::string& val) override;
    void flush() override;

private:
    std::string path_;
    std::map<std::string, std::string> kv_;
    bool dirty_ = false;
};

} // namespace host
