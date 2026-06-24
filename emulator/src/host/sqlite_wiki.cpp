#include "host/sqlite_wiki.h"
#include <cctype>
#include "sqlite3.h"

namespace host {

SqliteWiki::SqliteWiki(const std::string& path) {
    // read-only; if the file is missing, db_ stays null and ok() is false.
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return;
    }
    // Device-friendly footprint (measured ~150KB open + ~50KB/query): small page
    // cache, no mmap (SD has none), temp in RAM. On the 512KB no-PSRAM part the DB
    // should be opened only while the Wiki app is the foreground arena app, and
    // sqlite3_db_release_memory() called after queries.
    sqlite3_exec(db_, "PRAGMA mmap_size=0;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA cache_size=-32;", nullptr, nullptr, nullptr); // 32 KiB
    sqlite3_exec(db_, "PRAGMA temp_store=MEMORY;", nullptr, nullptr, nullptr);
}
SqliteWiki::~SqliteWiki() { if (db_) sqlite3_close(db_); }

// Turn raw user text into a safe FTS5 MATCH: quote each token (so punctuation
// can't break the query) and prefix-match the last token for incremental search.
std::string SqliteWiki::fts_query(const std::string& raw) {
    std::string out;
    size_t i = 0;
    std::vector<std::string> toks;
    while (i < raw.size()) {
        while (i < raw.size() && std::isspace((unsigned char)raw[i])) ++i;
        std::string t;
        while (i < raw.size() && !std::isspace((unsigned char)raw[i])) {
            char ch = raw[i++];
            if (std::isalnum((unsigned char)ch)) t += ch; // drop punctuation
        }
        if (!t.empty()) toks.push_back(t);
    }
    for (size_t k = 0; k < toks.size(); ++k) {
        if (k) out += ' ';
        out += '"' + toks[k] + '"';
        if (k + 1 == toks.size()) out += " *"; // last token = prefix
    }
    return out;
}

std::vector<wiki::Hit> SqliteWiki::search(const std::string& query, int limit) {
    std::vector<wiki::Hit> hits;
    if (!db_) return hits;
    std::string m = fts_query(query);
    if (m.empty()) return hits;
    // article_search is FTS5 with content='articles', content_rowid='id'.
    sqlite3_stmt* st = nullptr;
    const char* sql = "SELECT rowid, title FROM article_search WHERE article_search MATCH ?1 "
                      "ORDER BY rank LIMIT ?2";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return hits;
    sqlite3_bind_text(st, 1, m.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        wiki::Hit h; h.id = sqlite3_column_int64(st, 0);
        const unsigned char* t = sqlite3_column_text(st, 1);
        h.title = t ? reinterpret_cast<const char*>(t) : "";
        hits.push_back(std::move(h));
    }
    sqlite3_finalize(st);
    sqlite3_db_release_memory(db_); // reclaim FTS5 working memory between queries
    return hits;
}

std::vector<wiki::Hit> SqliteWiki::browse(const std::string& prefix, int limit) {
    std::vector<wiki::Hit> hits;
    if (!db_) return hits;
    sqlite3_stmt* st = nullptr;
    const char* sql = "SELECT id, title FROM articles WHERE title LIKE ?1 ORDER BY title LIMIT ?2";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return hits;
    std::string like = prefix + "%";
    sqlite3_bind_text(st, 1, like.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 2, limit);
    while (sqlite3_step(st) == SQLITE_ROW) {
        wiki::Hit h; h.id = sqlite3_column_int64(st, 0);
        const unsigned char* t = sqlite3_column_text(st, 1);
        h.title = t ? reinterpret_cast<const char*>(t) : "";
        hits.push_back(std::move(h));
    }
    sqlite3_finalize(st);
    return hits;
}

bool SqliteWiki::article(int64_t id, std::string& title, std::vector<wiki::Section>& out) {
    out.clear();
    if (!db_) return false;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT title FROM articles WHERE id=?1", -1, &st, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(st, 1, id);
        if (sqlite3_step(st) == SQLITE_ROW) {
            const unsigned char* t = sqlite3_column_text(st, 0);
            title = t ? reinterpret_cast<const char*>(t) : "";
        }
        sqlite3_finalize(st);
    }
    const char* sql = "SELECT title, content FROM sections WHERE article_id=?1 ORDER BY id";
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int64(st, 1, id);
    while (sqlite3_step(st) == SQLITE_ROW) {
        wiki::Section s;
        const unsigned char* a = sqlite3_column_text(st, 0);
        const unsigned char* b = sqlite3_column_text(st, 1);
        s.title = a ? reinterpret_cast<const char*>(a) : "";
        s.content = b ? reinterpret_cast<const char*>(b) : "";
        out.push_back(std::move(s));
    }
    sqlite3_finalize(st);
    return !out.empty();
}

} // namespace host
