// RadioMesh — the real Meshtastic mesh backend (replaces StubMesh on device).
//
// Implements mesh::MeshFacade over the SX1262. Threading: the radio task does
// only SX1262 I/O — on RX it pushes the RAW frame to a queue; on the main task,
// poll() drains the queue and does ALL parsing/crypto/node-DB work (so the node
// DB and app callbacks are single-threaded, no locks). send_text() resolves the
// peer key and enqueues a TxReq (plaintext Data); the radio task encrypts +
// transmits it. Channel/broadcast traffic uses the channel PSKs; per-node DMs
// use PKC (X25519 + AES-256-CCM, device/pkc.h). Also: delivery-ack matching
// (ROUTING request_id), node-DB persistence to NVS, and periodic self-NodeInfo.
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <set>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "mbedtls/aes.h"
#include "core/mesh.h"
#include "core/persist.h"
#include "core/lora_phy.h"
#include "core/meshtastic_frame.h"
#include "device/radio/sx1262.h"
#include "device/pkc.h"

namespace device {

class RadioMesh : public mesh::MeshFacade {
public:
    static constexpr const char* TAG = "mesh";
    static constexpr int MAX_CH = 8;
    static constexpr int MAX_NODES = 80;
    static constexpr uint32_t NODEINFO_PERIOD_MS = 15u * 60 * 1000;  // re-announce every 15 min
    static constexpr uint32_t SAVE_THROTTLE_MS = 60u * 1000;        // node-DB save cadence

    struct RxRaw { uint8_t buf[256]; uint16_t len; int16_t rssi; float snr; };
    // Already-encrypted frame payload (crypto runs on the main task); the radio
    // task only frames + transmits, so no heavy mbedtls runs on its small stack.
    struct TxReq { uint32_t dest; uint32_t id; uint8_t channel_hash; uint8_t enc[256]; uint16_t enc_len; };

    void configure(uint32_t our_id, const char* short_name, const char* long_name, persist::Store* store) {
        our_id_ = our_id; our_short_ = short_name; our_long_ = long_name; store_ = store;
    }

    bool begin(const HAL::SX1262Pins& pins, const lora::Phy& phy, const char* primary_name) {
        seed_channels(primary_name);
        load_keypair();
        load_nodes();
        rxq_ = xQueueCreate(8, sizeof(RxRaw));
        txq_ = xQueueCreate(4, sizeof(TxReq));
        static HAL::SX1262 radio(pins);
        radio_ = &radio;
        if (!radio_->init()) { ESP_LOGW(TAG, "radio init failed (no LoRa hat?)"); radio_ = nullptr; return false; }
        radio_->setEventCallback([this](HAL::RadioEvent e) { on_radio_event(e); });
        pending_phy_ = phy; pending_ = true;
        xTaskCreate(&RadioMesh::task_tramp, "radio", 6144, this, 6, &task_);
        radio_->setNotifyTask(task_);
        xTaskNotifyGive(task_);
        return true;
    }

    void request_retune(const lora::Phy& phy) {
        if (!radio_) return;
        pending_phy_ = phy; pending_ = true;
        if (task_) xTaskNotifyGive(task_);
    }

    // ---- MeshFacade ----
    uint32_t our_id() const override { return our_id_; }
    std::string our_short() const override { return our_short_; }
    std::string our_long() const override { return our_long_; }
    std::vector<mesh::Node> nodes() override {
        std::vector<mesh::Node> out = nodes_;
        for (auto& n : out) n.is_favorite = favs_.count(n.id) > 0;
        return out;
    }
    void subscribe(mesh::Subscriber cb) override { subs_.push_back(std::move(cb)); }
    void on_ack(mesh::AckCallback cb) override { ack_cb_ = std::move(cb); }
    void set_favorite(uint32_t id, bool fav) override { if (fav) favs_.insert(id); else favs_.erase(id); }
    bool is_favorite(uint32_t id) const override { return favs_.count(id) > 0; }
    void set_ignored(uint32_t id, bool ign) override { if (ign) ign_.insert(id); else ign_.erase(id); }
    bool is_ignored(uint32_t id) const override { return ign_.count(id) > 0; }

    uint32_t send_text(uint32_t dest, uint8_t channel, const std::string& text) override {
        uint32_t id = next_id_++;
        mesh::Message m;
        m.from_id = our_id_; m.from_name = our_short_; m.dest = dest; m.channel = channel;
        m.text = text; m.ts_ms = last_now_; m.outgoing = true; m.id = id; m.ack = mesh::ACK_NONE;
        emit(m);
        uint8_t data[233];
        size_t dlen = mtp::encode_text(data, sizeof data, text.c_str(), text.size());
        bool pkc = false; const uint8_t* peer = nullptr;
        if (dest != mesh::BROADCAST) {                 // DM: PKC if we have the peer key
            const mesh::Node* n = find(dest);
            if (n && n->has_key) { pkc = true; peer = n->pubkey; }
            if (sent_ids_.size() > 64) sent_ids_.erase(sent_ids_.begin());
            sent_ids_.insert(id);                      // expect a ROUTING ack
        }
        enqueue_tx(dest, channel, id, data, dlen, pkc, peer);
        return id;
    }

    void poll(uint32_t now_ms) override {
        last_now_ = now_ms;
        if (!rxq_) return;
        RxRaw r;
        while (xQueueReceive(rxq_, &r, 0) == pdTRUE) process_frame(r, now_ms);
        // Periodic self-NodeInfo (also ~15 s after boot so peers learn us + our key).
        if (radio_ && (next_nodeinfo_ == 0 ? now_ms > 15000 : (int32_t)(now_ms - next_nodeinfo_) >= 0)) {
            broadcast_nodeinfo(); next_nodeinfo_ = now_ms + NODEINFO_PERIOD_MS;
        }
        if (dirty_ && store_ && (int32_t)(now_ms - last_save_) >= (int32_t)SAVE_THROTTLE_MS) save_nodes();
    }

private:
    struct Channel { char name[24]; uint8_t key[32]; size_t keylen; uint8_t hash; };

    // ---- channels ----
    void seed_channels(const char* primary_name) {
        n_ch_ = 0;
        add_channel(primary_name && *primary_name ? primary_name : "LongFast", mtp::DEFAULT_PSK, 16);
        uint8_t wx = 0x59, ma = 0x99;                  // nyme.sh secondaries "WQ=="/"mQ=="
        add_channel("Wx", &wx, 1);
        add_channel("mesh-around", &ma, 1);
    }
    void add_channel(const char* name, const uint8_t* psk, size_t psklen) {
        if (n_ch_ >= MAX_CH) return;
        Channel& c = ch_[n_ch_];
        std::strncpy(c.name, name, sizeof c.name - 1); c.name[sizeof c.name - 1] = 0;
        c.keylen = mtp::expand_psk(psk, psklen, c.key);
        c.hash = mtp::channel_hash(c.name, c.key, c.keylen);
        ESP_LOGI(TAG, "channel[%d] \"%s\" hash=0x%02x", n_ch_, c.name, c.hash);
        n_ch_++;
    }
    int channel_by_hash(uint8_t h) const { for (int i = 0; i < n_ch_; ++i) if (ch_[i].hash == h) return i; return -1; }

    // ---- keypair (X25519) ----
    static std::string to_hex(const uint8_t* b, size_t n) {
        static const char* H = "0123456789abcdef"; std::string s; s.reserve(n * 2);
        for (size_t i = 0; i < n; ++i) { s += H[b[i] >> 4]; s += H[b[i] & 15]; } return s;
    }
    static bool from_hex(const std::string& s, uint8_t* out, size_t n) {
        if (s.size() != n * 2) return false;
        auto v = [](char c) -> int { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; return -1; };
        for (size_t i = 0; i < n; ++i) { int hi = v(s[2*i]), lo = v(s[2*i+1]); if (hi < 0 || lo < 0) return false; out[i] = (uint8_t)(hi << 4 | lo); }
        return true;
    }
    void load_keypair() {
        if (store_ && from_hex(store_->get("pki.priv", ""), our_priv_, 32) &&
            from_hex(store_->get("pki.pub", ""), our_pub_, 32)) { have_keys_ = true; }
        else if (pkc::generate_keypair(our_priv_, our_pub_)) {
            have_keys_ = true;
            if (store_) { store_->set("pki.priv", to_hex(our_priv_, 32)); store_->set("pki.pub", to_hex(our_pub_, 32)); store_->flush(); }
            ESP_LOGI(TAG, "generated X25519 keypair");
        }
        if (have_keys_) ESP_LOGI(TAG, "pubkey %s", to_hex(our_pub_, 32).c_str());
    }

    // ---- radio task ----
    static uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000); }
    static void task_tramp(void* a) { static_cast<RadioMesh*>(a)->run(); }
    void run() {
        for (;;) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
            if (pending_) { pending_ = false; apply_phy(pending_phy_); }
            radio_->processEvents();
            if (!tx_busy_ && txq_) { TxReq r; if (xQueueReceive(txq_, &r, 0) == pdTRUE) transmit(r); }
        }
    }
    void apply_phy(const lora::Phy& phy) {
        phy_ = phy;
        if (!phy.valid) { radio_->setMode(HAL::RadioMode::STANDBY); ESP_LOGW(TAG, "region UNSET — idle"); return; }
        HAL::LoRaConfig cfg = {};
        cfg.frequency_hz = phy.freq_hz; cfg.bandwidth_hz = phy.bw_hz; cfg.spreading_factor = phy.sf;
        cfg.coding_rate = phy.cr; cfg.tx_power_dbm = phy.tx_dbm; cfg.preamble_length = phy.preamble;
        cfg.sync_word = phy.sync; cfg.crc_enabled = phy.crc; cfg.implicit_header = false;
        cfg.iq_inverted = phy.iq_invert; cfg.rx_boosted_gain = phy.rx_boost;
        radio_->setConfig(cfg); radio_->startReceive(0);
        ESP_LOGI(TAG, "tuned @ %.3f MHz SF%d ch%d", phy.freq_hz / 1e6, phy.sf, phy.channel);
    }
    void on_radio_event(HAL::RadioEvent e) {
        switch (e) {
        case HAL::RadioEvent::TX_DONE:
            tx_busy_ = false; radio_->startReceive(0); if (task_) xTaskNotifyGive(task_); break;
        case HAL::RadioEvent::RX_DONE: {
            RxRaw r; HAL::RxPacketInfo info = {};
            int len = radio_->readPacket(r.buf, 255, &info);
            if (len > 0) { r.len = (uint16_t)len; r.rssi = info.rssi; r.snr = info.snr; xQueueSend(rxq_, &r, 0); }
            radio_->startReceive(0); break;
        }
        case HAL::RadioEvent::RX_TIMEOUT:
        case HAL::RadioEvent::RX_ERROR: radio_->startReceive(0); break;
        default: break;
        }
    }
    // Main task: encrypt the plaintext Data and enqueue the framed payload.
    void enqueue_tx(uint32_t dest, uint8_t channel, uint32_t id, const uint8_t* data, size_t dlen,
                    bool pkc, const uint8_t* peer_pub) {
        if (!dlen || !txq_) return;
        int ci = channel < (uint8_t)n_ch_ ? channel : 0;
        TxReq r{}; r.dest = dest; r.id = id;
        if (pkc && have_keys_ && peer_pub) {
            r.enc_len = (uint16_t)pkc::encrypt(our_priv_, peer_pub, our_id_, id, data, dlen, r.enc);
            r.channel_hash = 0x00;
        } else {
            uint8_t nonce[16], stream[16] = {0}; size_t nc = 0;
            mtp::make_nonce(our_id_, id, nonce);
            mbedtls_aes_context c; mbedtls_aes_init(&c);
            mbedtls_aes_setkey_enc(&c, ch_[ci].key, (unsigned)ch_[ci].keylen * 8);
            mbedtls_aes_crypt_ctr(&c, dlen, &nc, nonce, stream, data, r.enc);
            mbedtls_aes_free(&c);
            r.enc_len = (uint16_t)dlen; r.channel_hash = ch_[ci].hash;
        }
        if (!r.enc_len) return;
        xQueueSend(txq_, &r, 0);
        if (task_) xTaskNotifyGive(task_);
    }

    // Radio task: frame (header + ciphertext) and transmit. No crypto here.
    void transmit(const TxReq& r) {
        uint8_t frame[256];
        mtp::Header h{}; h.dst = r.dest; h.src = our_id_; h.id = r.id; h.hop_limit = 3; h.hop_start = 3;
        h.want_ack = (r.dest != mesh::BROADCAST);
        h.channel_hash = r.channel_hash;
        mtp::write_header(frame, h);
        std::memcpy(frame + mtp::HEADER_LEN, r.enc, r.enc_len);
        tx_busy_ = true;
        ESP_LOGI(TAG, "TX id=%08lx dst=!%08lx %s %uB", (unsigned long)r.id, (unsigned long)r.dest,
                 r.channel_hash == 0 ? "PKC" : "ch", (unsigned)(mtp::HEADER_LEN + r.enc_len));
        radio_->transmit(frame, (uint8_t)(mtp::HEADER_LEN + r.enc_len));
    }

    // ---- main task: parse + decrypt + fold into node DB ----
    void process_frame(const RxRaw& r, uint32_t now_ms) {
        mtp::Header h;
        if (!mtp::parse_header(r.buf, r.len, h)) return;
        // Implicit ack: hearing one of our own sent packets relayed back (same id,
        // cleartext header) means the mesh carried it — like Plai's packet.id match.
        if (sent_ids_.count(h.id)) {
            sent_ids_.erase(h.id);
            ESP_LOGI(TAG, "ack id=%08lx (implicit)", (unsigned long)h.id);
            if (ack_cb_) ack_cb_(h.id, true);
            return;
        }
        int paylen = (int)r.len - (int)mtp::HEADER_LEN;
        if (paylen <= 0 || ign_.count(h.src)) return;
        const uint8_t* payload = r.buf + mtp::HEADER_LEN;

        uint8_t pt[256]; mtp::Data d; bool ok = false;
        if (h.channel_hash == 0 && h.dst == our_id_ && have_keys_ && paylen > (int)pkc::OVERHEAD) {
            // PKC DM to us: decrypt with the sender's public key.
            const mesh::Node* n = find(h.src);
            if (n && n->has_key) {
                size_t pl = pkc::decrypt(our_priv_, n->pubkey, h.src, h.id, payload, paylen, pt);
                if (pl) ok = mtp::parse_data(pt, pl, d);
            }
            if (!ok) { ESP_LOGD(TAG, "PKC DM from !%08lx undecodable (no key?)", (unsigned long)h.src); }
        } else {
            int ci = channel_by_hash(h.channel_hash);
            if (ci < 0) return;                         // unknown channel / PKC for someone else
            uint8_t nonce[16], stream[16] = {0}; size_t nc = 0;
            mtp::make_nonce(h.src, h.id, nonce);
            mbedtls_aes_context c; mbedtls_aes_init(&c);
            mbedtls_aes_setkey_enc(&c, ch_[ci].key, (unsigned)ch_[ci].keylen * 8);
            mbedtls_aes_crypt_ctr(&c, (size_t)paylen, &nc, nonce, stream, payload, pt);
            mbedtls_aes_free(&c);
            ok = mtp::parse_data(pt, (size_t)paylen, d);
        }
        if (!ok) return;

        mesh::Node& n = upsert(h.src);
        n.last_heard_ms = now_ms; n.snr = (int)lroundf(r.snr);
        ESP_LOGI(TAG, "RX from=!%08lx %s %dB RSSI=%d", (unsigned long)h.src,
                 mtp::portnum_name(d.portnum), paylen, (int)r.rssi);

        switch (d.portnum) {
        case 1: if (d.payload_len) {                    // TEXT -> chat + notify
            mesh::Message m; m.from_id = h.src; m.from_name = name_of(h.src);
            m.dest = h.dst; m.channel = 0; m.ts_ms = now_ms; m.outgoing = false;
            m.text.assign((const char*)d.payload, d.payload_len); emit(m);
        } break;
        case 4: if (d.payload_len) {                    // NODEINFO -> names + pubkey
            mtp::NodeInfo ni;
            if (mtp::decode_nodeinfo(d.payload, d.payload_len, ni)) {
                if (ni.long_name[0]) n.long_name = ni.long_name;
                if (ni.short_name[0]) n.short_name = ni.short_name;
                if (ni.has_key) { std::memcpy(n.pubkey, ni.public_key, 32); n.has_key = true; }
                dirty_ = true;
            }
        } break;
        case 3: if (d.payload_len) {                    // POSITION
            mtp::Position p;
            if (mtp::decode_position(d.payload, d.payload_len, p) && p.has_latlon) { n.lat = p.lat; n.lon = p.lon; n.has_pos = true; dirty_ = true; }
        } break;
        case 67: if (d.payload_len) {                   // TELEMETRY
            mtp::DeviceMetrics dm;
            if (mtp::decode_telemetry(d.payload, d.payload_len, dm) && dm.has && dm.battery >= 0) { n.battery = dm.battery; dirty_ = true; }
        } break;
        case 5: {                                       // ROUTING -> delivery ack
            int err = d.payload_len ? mtp::routing_error(d.payload, d.payload_len) : 0;
            if (d.request_id && sent_ids_.count(d.request_id)) {
                sent_ids_.erase(d.request_id);
                bool good = (err <= 0);                  // NONE/absent = ack; >0 = NAK
                ESP_LOGI(TAG, "ack id=%08lx %s", (unsigned long)d.request_id, good ? "OK" : "FAIL");
                if (ack_cb_) ack_cb_(d.request_id, good);
            }
        } break;
        default: break;
        }
    }

    void broadcast_nodeinfo() {
        uint8_t data[233];
        char id[12]; std::snprintf(id, sizeof id, "!%08lx", (unsigned long)our_id_);
        size_t dlen = mtp::encode_nodeinfo(data, sizeof data, id, our_long_.c_str(),
                                           our_short_.c_str(), have_keys_ ? our_pub_ : nullptr);
        enqueue_tx(mesh::BROADCAST, 0, next_id_++, data, dlen, false, nullptr);
        ESP_LOGI(TAG, "broadcast self NodeInfo");
    }

    // ---- node DB ----
    mesh::Node* find(uint32_t id) { for (auto& n : nodes_) if (n.id == id) return &n; return nullptr; }
    mesh::Node& upsert(uint32_t id) {
        if (mesh::Node* n = find(id)) return *n;
        if (nodes_.size() >= MAX_NODES) nodes_.erase(nodes_.begin());
        mesh::Node n{}; n.id = id;
        char b[12]; std::snprintf(b, sizeof b, "!%08lx", (unsigned long)id);
        n.long_name = b; n.short_name = std::string(b + 5);
        nodes_.push_back(n); dirty_ = true;
        return nodes_.back();
    }
    std::string name_of(uint32_t id) {
        if (mesh::Node* n = find(id)) return n->short_name.empty() ? n->long_name : n->short_name;
        char b[12]; std::snprintf(b, sizeof b, "!%08lx", (unsigned long)id); return b;
    }
    void emit(const mesh::Message& m) { for (auto& s : subs_) s(m); }

    // CSV: id<TAB>batt<TAB>lat<TAB>lon<TAB>haspos<TAB>pubkeyhex|-<TAB>long<TAB>short  (one node per line)
    void save_nodes() {
        last_save_ = last_now_; dirty_ = false;
        std::string out;
        for (auto& n : nodes_) {
            char head[96];
            std::snprintf(head, sizeof head, "%lu\t%d\t%.6f\t%.6f\t%d\t", (unsigned long)n.id, n.battery,
                          n.lat, n.lon, n.has_pos ? 1 : 0);
            out += head;
            out += n.has_key ? to_hex(n.pubkey, 32) : "-";
            out += "\t"; out += n.long_name; out += "\t"; out += n.short_name; out += "\n";
        }
        store_->set("nodes.db", out); store_->flush();
    }
    void load_nodes() {
        if (!store_) return;
        std::string s = store_->get("nodes.db", "");
        size_t i = 0;
        while (i < s.size()) {
            size_t nl = s.find('\n', i); if (nl == std::string::npos) nl = s.size();
            std::string line = s.substr(i, nl - i); i = nl + 1;
            if (line.empty()) continue;
            std::vector<std::string> f; size_t p = 0;
            while (true) { size_t t = line.find('\t', p); f.push_back(line.substr(p, t == std::string::npos ? std::string::npos : t - p)); if (t == std::string::npos) break; p = t + 1; }
            if (f.size() < 8) continue;
            mesh::Node n{}; n.id = (uint32_t)std::strtoul(f[0].c_str(), nullptr, 10);
            n.battery = std::atoi(f[1].c_str()); n.lat = std::atof(f[2].c_str()); n.lon = std::atof(f[3].c_str());
            n.has_pos = f[4] == "1";
            if (f[5] != "-" && from_hex(f[5], n.pubkey, 32)) n.has_key = true;
            n.long_name = f[6]; n.short_name = f[7]; n.last_heard_ms = 0;
            if (nodes_.size() < MAX_NODES) nodes_.push_back(n);
        }
        ESP_LOGI(TAG, "restored %d nodes from NVS", (int)nodes_.size());
    }

    HAL::SX1262* radio_ = nullptr;
    TaskHandle_t task_ = nullptr;
    QueueHandle_t rxq_ = nullptr, txq_ = nullptr;
    volatile bool tx_busy_ = false, pending_ = false;
    lora::Phy pending_phy_, phy_;
    persist::Store* store_ = nullptr;

    Channel ch_[MAX_CH]; int n_ch_ = 0;
    uint8_t our_priv_[32] = {0}, our_pub_[32] = {0}; bool have_keys_ = false;
    std::vector<mesh::Node> nodes_;
    std::set<uint32_t> favs_, ign_, sent_ids_;
    std::vector<mesh::Subscriber> subs_;
    mesh::AckCallback ack_cb_;
    uint32_t our_id_ = 0, last_now_ = 0, next_id_ = 1, next_nodeinfo_ = 0, last_save_ = 0;
    bool dirty_ = false;
    std::string our_short_ = "DECK", our_long_ = "Cardputer Deck";
};

} // namespace device
