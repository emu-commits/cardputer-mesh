// fs::FileSystem over the mounted SD card (FATFS VFS at /sdcard).
//
// Mirrors host::UnixFs exactly — same rooted-POSIX semantics, same path
// normalisation (".", ".." resolved, ".." above root clamped) — but reaches
// real storage with C stdio + dirent + stat instead of std::filesystem. The
// app layer sees identical behaviour to the emulator. The storage root "/" maps
// to the SD mount point; long filenames require FATFS LFN (see sdkconfig).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_vfs_fat.h"
#include "core/fs.h"

namespace device {

class SdFs : public fs::FileSystem {
public:
    explicit SdFs(std::string mount = "/sdcard") : mount_(std::move(mount)) {}

    bool list(const std::string& path, std::vector<fs::Entry>& out) override {
        out.clear();
        std::string rp = real(path);
        DIR* d = opendir(rp.c_str());
        if (!d) return false;
        struct dirent* de;
        while ((de = readdir(d)) != nullptr) {
            const char* nm = de->d_name;
            if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) continue;
            fs::Entry e;
            e.name = nm;
            struct stat st;
            std::string full = rp + "/" + e.name;
            if (stat(full.c_str(), &st) == 0) {
                e.is_dir = S_ISDIR(st.st_mode);
                e.size = e.is_dir ? 0 : (uint64_t)st.st_size;
            }
            out.push_back(std::move(e));
        }
        closedir(d);
        std::sort(out.begin(), out.end(), [](const fs::Entry& a, const fs::Entry& b) {
            if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;  // dirs first
            return a.name < b.name;
        });
        return true;
    }

    bool is_dir(const std::string& path) override {
        struct stat st;
        return stat(real(path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
    bool exists(const std::string& path) override {
        struct stat st;
        return stat(real(path).c_str(), &st) == 0;
    }

    bool read_text(const std::string& path, std::string& out, size_t max_bytes) override {
        FILE* f = std::fopen(real(path).c_str(), "rb");
        if (!f) return false;
        out.clear();
        out.resize(max_bytes);
        size_t n = std::fread(&out[0], 1, max_bytes, f);
        out.resize(n);
        std::fclose(f);
        return true;
    }

    bool write_text(const std::string& path, const std::string& data) override {
        mkparents(path);
        FILE* f = std::fopen(real(path).c_str(), "wb");
        if (!f) return false;
        size_t n = std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
        return n == data.size();
    }
    bool append_text(const std::string& path, const std::string& data) override {
        mkparents(path);
        FILE* f = std::fopen(real(path).c_str(), "ab");
        if (!f) return false;
        size_t n = std::fwrite(data.data(), 1, data.size(), f);
        std::fclose(f);
        return n == data.size();
    }
    bool remove(const std::string& path) override {
        return unlink(real(path).c_str()) == 0;
    }

    uint64_t free_bytes() override {
        uint64_t total = 0, freeb = 0;
        return esp_vfs_fat_info(mount_.c_str(), &total, &freeb) == ESP_OK ? freeb : 0;
    }
    uint64_t total_bytes() override {
        uint64_t total = 0, freeb = 0;
        return esp_vfs_fat_info(mount_.c_str(), &total, &freeb) == ESP_OK ? total : 0;
    }

    std::string join(const std::string& base, const std::string& rel) override {
        std::string start = (!rel.empty() && rel[0] == '/') ? rel : (base + "/" + rel);
        return rebuild(norm(start));
    }

private:
    // Map a rooted virtual path ("/notes/x.txt") to a real VFS path
    // ("/sdcard/notes/x.txt"), clamping ".." at the root.
    std::string real(const std::string& rooted) { return mount_ + rebuild(norm(rooted)); }

    static std::vector<std::string> norm(const std::string& path) {
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
    static std::string rebuild(const std::vector<std::string>& parts) {
        std::string s = "/";
        for (size_t i = 0; i < parts.size(); ++i) { s += parts[i]; if (i + 1 < parts.size()) s += '/'; }
        return s;
    }
    // Create any missing parent directories of a rooted file path.
    void mkparents(const std::string& path) {
        auto parts = norm(path);
        if (parts.empty()) return;
        std::string cur = mount_;
        for (size_t i = 0; i + 1 < parts.size(); ++i) {  // all but the file itself
            cur += "/" + parts[i];
            mkdir(cur.c_str(), 0775);
        }
    }

    std::string mount_;
};

} // namespace device
