// WikiSource — read-only access to an offline wiki (a SQLite DB with FTS5).
// Portable interface (core); the host backing (host::SqliteWiki) runs the DB's
// built-in FTS5 over articles/sections off disk, and the device backing uses the
// same SQLite amalgamation over the SD card. Designed so search + reads page
// from disk and never load the 267K-article corpus into RAM.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wiki {

struct Hit { int64_t id = 0; std::string title; };           // article id + title
struct Section { std::string title; std::string content; };

class WikiSource {
public:
    virtual ~WikiSource() = default;
    virtual bool ok() const = 0;  // DB present + opened
    // Full-text search article titles/content (FTS5). Ranked, up to `limit`.
    virtual std::vector<Hit> search(const std::string& query, int limit) = 0;
    // Title prefix browse (A-Z), up to `limit`.
    virtual std::vector<Hit> browse(const std::string& prefix, int limit) = 0;
    // Load an article's sections by id. Returns false if not found.
    virtual bool article(int64_t id, std::string& title, std::vector<Section>& out) = 0;
};

} // namespace wiki
