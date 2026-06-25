// persist::Store backed by NVS (ESP-IDF non-volatile storage).
//
// The persist::Store contract is "holds arbitrary string values" — keys and
// values can contain tabs/newlines (contacts records, calcurse todos, the calc
// tape, etc.; see the FileStore escaping gotcha in the project notes). NVS key
// names are also capped at 15 chars, far shorter than our namespaced keys like
// "cfg.lora.modem_preset". Both problems vanish if we DON'T map app keys onto
// NVS keys at all: we keep the whole key/value map resident in RAM (it is only
// a few KB — settings, session tokens, contacts, todos) and serialise the entire
// map into ONE length-prefixed NVS blob on flush(). Byte-safe, no key-length or
// delimiter limits, one erase/write per checkpoint (NVS wear-levels internally).
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include "nvs.h"
#include "nvs_flash.h"
#include "core/persist.h"

namespace device {

class NvsStore : public persist::Store {
public:
    // Init NVS flash + open our namespace, then load the persisted blob.
    bool begin(const char* ns = "deck") {
        esp_err_t e = nvs_flash_init();
        if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();
            e = nvs_flash_init();
        }
        if (e != ESP_OK) return false;
        if (nvs_open(ns, NVS_READWRITE, &h_) != ESP_OK) return false;
        ok_ = true;
        load();
        return true;
    }

    std::string get(const std::string& key, const std::string& def = "") override {
        auto it = m_.find(key);
        return it == m_.end() ? def : it->second;
    }
    void set(const std::string& key, const std::string& val) override {
        auto it = m_.find(key);
        if (it != m_.end() && it->second == val) return; // no-op, skip dirty flag
        m_[key] = val;
        dirty_ = true;
    }
    // Commit the whole map to NVS as one blob (only if something changed).
    void flush() override {
        if (!ok_ || !dirty_) return;
        std::string blob;
        blob.reserve(1024);
        for (auto& kv : m_) {
            put_u32(blob, (uint32_t)kv.first.size());  blob += kv.first;
            put_u32(blob, (uint32_t)kv.second.size()); blob += kv.second;
        }
        if (nvs_set_blob(h_, BLOB_KEY, blob.data(), blob.size()) == ESP_OK &&
            nvs_commit(h_) == ESP_OK)
            dirty_ = false;
    }

private:
    static constexpr const char* BLOB_KEY = "kv";

    static void put_u32(std::string& s, uint32_t v) {
        char b[4] = {(char)(v & 0xFF), (char)((v >> 8) & 0xFF),
                     (char)((v >> 16) & 0xFF), (char)((v >> 24) & 0xFF)};
        s.append(b, 4);
    }
    static uint32_t get_u32(const char* p) {
        return (uint32_t)(uint8_t)p[0] | ((uint32_t)(uint8_t)p[1] << 8) |
               ((uint32_t)(uint8_t)p[2] << 16) | ((uint32_t)(uint8_t)p[3] << 24);
    }

    void load() {
        size_t n = 0;
        if (nvs_get_blob(h_, BLOB_KEY, nullptr, &n) != ESP_OK || n == 0) return;
        std::string blob;
        blob.resize(n);
        if (nvs_get_blob(h_, BLOB_KEY, &blob[0], &n) != ESP_OK) return;
        size_t i = 0;
        while (i + 4 <= n) {
            uint32_t kl = get_u32(&blob[i]); i += 4;
            if (i + kl + 4 > n) break;
            std::string k = blob.substr(i, kl); i += kl;
            uint32_t vl = get_u32(&blob[i]); i += 4;
            if (i + vl > n) break;
            std::string v = blob.substr(i, vl); i += vl;
            m_[std::move(k)] = std::move(v);
        }
    }

    nvs_handle_t h_ = 0;
    bool ok_ = false;
    bool dirty_ = false;
    std::map<std::string, std::string> m_;
};

} // namespace device
