#include "host/file_store.h"
#include <fstream>

namespace host {

namespace {
// The on-disk format is one "key<TAB>value" line per entry, so values must not
// contain a literal tab or newline. Rather than mangle them (which corrupts
// multi-line docs / tab-delimited records), we backslash-escape. The Store
// contract is therefore "holds arbitrary string values" — on device the NVS/SD
// backend preserves bytes directly.
std::string escape(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\t': o += "\\t"; break;
            case '\r': o += "\\r"; break;
            default: o += c;
        }
    }
    return o;
}
std::string unescape(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[++i];
            o += (n == 'n') ? '\n' : (n == 't') ? '\t' : (n == 'r') ? '\r' : n;
        } else o += s[i];
    }
    return o;
}
} // namespace

FileStore::FileStore(const std::string& path) : path_(path) {
    std::ifstream f(path_);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        kv_[line.substr(0, tab)] = unescape(line.substr(tab + 1));
    }
}

std::string FileStore::get(const std::string& key, const std::string& def) {
    auto it = kv_.find(key);
    return it == kv_.end() ? def : it->second;
}

void FileStore::set(const std::string& key, const std::string& val) {
    auto it = kv_.find(key);
    if (it != kv_.end() && it->second == val) return;
    kv_[key] = val;
    dirty_ = true;
}

void FileStore::flush() {
    if (!dirty_) return;
    std::ofstream f(path_, std::ios::trunc);
    for (auto& kvp : kv_) f << kvp.first << '\t' << escape(kvp.second) << '\n';
    dirty_ = false;
}

} // namespace host
