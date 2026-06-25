// Temporary seam implementations for the bring-up skeleton. These let the full
// app suite boot and render before the real device backends are wired:
//   RamStore  -> persist::Store   (later: NVS scalars + SD files)
//   NullFs    -> fs::FileSystem   (later: SD card via fatfs)
//   NullWiki  -> wiki::WikiSource (later: SQLite/FTS5 on SD)
// Mesh is NOT stubbed here — we reuse the portable mesh::StubMesh, which already
// generates live traffic, until Plai's MeshService is bridged in.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "core/fs.h"
#include "core/persist.h"
#include "core/wiki.h"

namespace stubs {

class RamStore : public persist::Store {
public:
    std::string get(const std::string& key, const std::string& def = "") override {
        auto it = m_.find(key);
        return it == m_.end() ? def : it->second;
    }
    void set(const std::string& key, const std::string& val) override { m_[key] = val; }
    void flush() override {}
private:
    std::map<std::string, std::string> m_;
};

class NullFs : public fs::FileSystem {
public:
    bool list(const std::string&, std::vector<fs::Entry>&) override { return false; }
    bool is_dir(const std::string&) override { return false; }
    bool exists(const std::string&) override { return false; }
    bool read_text(const std::string&, std::string&, size_t) override { return false; }
    bool write_text(const std::string&, const std::string&) override { return false; }
    bool append_text(const std::string&, const std::string&) override { return false; }
    bool remove(const std::string&) override { return false; }
    std::string join(const std::string& base, const std::string& rel) override {
        if (!rel.empty() && rel[0] == '/') return rel;
        if (base.empty() || base == "/") return "/" + rel;
        return base + "/" + rel;
    }
};

class NullWiki : public wiki::WikiSource {
public:
    bool ok() const override { return false; }
    std::vector<wiki::Hit> search(const std::string&, int) override { return {}; }
    std::vector<wiki::Hit> browse(const std::string&, int) override { return {}; }
    bool article(int64_t, std::string&, std::vector<wiki::Section>&) override { return false; }
};

} // namespace stubs
