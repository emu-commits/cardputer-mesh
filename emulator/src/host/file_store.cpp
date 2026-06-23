#include "host/file_store.h"
#include <fstream>
#include <sstream>

namespace host {

FileStore::FileStore(const std::string& path) : path_(path) {
    std::ifstream f(path_);
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        kv_[line.substr(0, tab)] = line.substr(tab + 1);
    }
}

std::string FileStore::get(const std::string& key, const std::string& def) {
    auto it = kv_.find(key);
    return it == kv_.end() ? def : it->second;
}

void FileStore::set(const std::string& key, const std::string& val) {
    // strip any tab/newline so the line format stays intact
    std::string v = val;
    for (auto& c : v) if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    auto it = kv_.find(key);
    if (it != kv_.end() && it->second == v) return;
    kv_[key] = v;
    dirty_ = true;
}

void FileStore::flush() {
    if (!dirty_) return;
    std::ofstream f(path_, std::ios::trunc);
    for (auto& kvp : kv_) f << kvp.first << '\t' << kvp.second << '\n';
    dirty_ = false;
}

} // namespace host
