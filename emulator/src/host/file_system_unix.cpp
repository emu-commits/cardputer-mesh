#include "host/file_system_unix.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace stdfs = std::filesystem;

namespace host {

namespace {
// Split a rooted path into clean components, resolving "." and ".." and clamping
// at the root (a ".." at the top is dropped, never escapes).
std::vector<std::string> norm(const std::string& path) {
    std::vector<std::string> parts;
    std::string cur;
    auto flush = [&] {
        if (cur.empty() || cur == ".") { cur.clear(); return; }
        if (cur == "..") { if (!parts.empty()) parts.pop_back(); }
        else parts.push_back(cur);
        cur.clear();
    };
    for (char c : path) { if (c == '/') flush(); else cur += c; }
    flush();
    return parts;
}
std::string rebuild(const std::vector<std::string>& parts) {
    std::string s = "/";
    for (size_t i = 0; i < parts.size(); ++i) { s += parts[i]; if (i + 1 < parts.size()) s += '/'; }
    return s;
}
void seed(const std::string& root) {
    std::error_code ec;
    stdfs::create_directories(root, ec);
    if (!stdfs::is_empty(root, ec) && !ec) return;
    auto wr = [&](const std::string& rel, const std::string& body) {
        stdfs::path p = stdfs::path(root) / rel;
        stdfs::create_directories(p.parent_path(), ec);
        std::ofstream(p) << body;
    };
    wr("README.txt", "Cardputer SD sandbox.\nThis tree stands in for the microSD card.\n");
    wr("notes/hello.txt", "hello from the notes folder\n");
    wr("notes/longline.txt",
       "This is a deliberately very long single line of text used to verify that "
       "the file viewer wraps or horizontally scrolls long content correctly "
       "instead of overflowing the screen boundary or corrupting the layout.\n");
    wr("logs/boot.log", "boot ok\nmesh up\n");
    wr("journal/2026-06-23.txt", "Shipped the core app suite today.\n");
}
} // namespace

UnixFs::UnixFs(const std::string& root) : root_(root) { seed(root_); }

std::string UnixFs::real(const std::string& rooted) {
    return root_ + rebuild(norm(rooted));
}

bool UnixFs::remove(const std::string& path) {
    std::error_code ec;
    return stdfs::remove(real(path), ec) && !ec;
}
uint64_t UnixFs::free_bytes() {
    std::error_code ec; auto s = stdfs::space(root_, ec); return ec ? 0 : (uint64_t)s.available;
}
uint64_t UnixFs::total_bytes() {
    std::error_code ec; auto s = stdfs::space(root_, ec); return ec ? 0 : (uint64_t)s.capacity;
}

std::string UnixFs::join(const std::string& base, const std::string& rel) {
    std::string start = (!rel.empty() && rel[0] == '/') ? rel : (base + "/" + rel);
    return rebuild(norm(start));
}

bool UnixFs::is_dir(const std::string& path) {
    std::error_code ec; return stdfs::is_directory(real(path), ec);
}
bool UnixFs::exists(const std::string& path) {
    std::error_code ec; return stdfs::exists(real(path), ec);
}

bool UnixFs::list(const std::string& path, std::vector<fs::Entry>& out) {
    out.clear();
    std::error_code ec;
    std::string rp = real(path);
    if (!stdfs::is_directory(rp, ec)) return false;
    for (auto& de : stdfs::directory_iterator(rp, ec)) {
        if (ec) break;
        fs::Entry e;
        e.name = de.path().filename().string();
        e.is_dir = de.is_directory(ec);
        e.size = e.is_dir ? 0 : (uint64_t)de.file_size(ec);
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(), [](const fs::Entry& a, const fs::Entry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir; // dirs first
        return a.name < b.name;
    });
    return true;
}

bool UnixFs::read_text(const std::string& path, std::string& out, size_t max_bytes) {
    std::ifstream f(real(path), std::ios::binary);
    if (!f) return false;
    out.clear();
    out.resize(max_bytes);
    f.read(&out[0], (std::streamsize)max_bytes);
    out.resize((size_t)f.gcount());
    return true;
}

bool UnixFs::write_text(const std::string& path, const std::string& data) {
    std::error_code ec;
    stdfs::path p = real(path);
    stdfs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return (bool)f;
}

bool UnixFs::append_text(const std::string& path, const std::string& data) {
    std::error_code ec;
    stdfs::path p = real(path);
    stdfs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::app);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return (bool)f;
}

} // namespace host
