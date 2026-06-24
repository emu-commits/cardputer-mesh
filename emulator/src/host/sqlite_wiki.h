// SqliteWiki — host WikiSource backed by the wiki.db SQLite database (FTS5).
// Opens read-only; queries page from disk. On device the same amalgamation runs
// over the SD card behind this same interface.
#pragma once
#include <string>
#include "core/wiki.h"

struct sqlite3;

namespace host {

class SqliteWiki : public wiki::WikiSource {
public:
    explicit SqliteWiki(const std::string& path);
    ~SqliteWiki() override;

    bool ok() const override { return db_ != nullptr; }
    std::vector<wiki::Hit> search(const std::string& query, int limit) override;
    std::vector<wiki::Hit> browse(const std::string& prefix, int limit) override;
    bool article(int64_t id, std::string& title, std::vector<wiki::Section>& out) override;

private:
    static std::string fts_query(const std::string& raw); // sanitize -> FTS5 MATCH
    sqlite3* db_ = nullptr;
};

} // namespace host
