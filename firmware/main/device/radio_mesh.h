// RadioMesh — the real Meshtastic mesh backend (replaces StubMesh on device).
//
// Implements the portable mesh::MeshFacade the apps use, driven by the SX1262.
// Threading model: the radio runs on its own FreeRTOS task (all SX1262 SPI stays
// there). That task DECODES received frames into POD RxEvents and pushes them on
// a queue; RadioMesh::poll() (main task) drains the queue, updates the node DB,
// and emits mesh::Messages to subscribers — so the node DB and app callbacks are
// only ever touched on the main task (no locks). send_text() encodes+enqueues a
// TxReq; the radio task encrypts and transmits it. Retune is handled on the task.
//
// Decrypts channel/broadcast traffic with the configured channel PSKs (primary =
// default key; nyme.sh secondaries seeded). Per-node PKC DMs aren't decoded.
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
#include "core/lora_phy.h"
#include "core/meshtastic_frame.h"
#include "device/radio/sx1262.h"

namespace device {

class RadioMesh : public mesh::MeshFacade {
public:
    static constexpr const char* TAG = "mesh";
    static constexpr int MAX_CH = 8;
    static constexpr int MAX_NODES = 80;

    // Decoded-frame event handed from the radio task to poll() (POD, no heap).
    struct RxEvent {
        uint32_t from, to, id;
        int16_t  rssi; float snr;
        uint8_t  portnum, channel_idx;
        char     text[180];
        char     long_name[40], short_name[8];
        double   lat, lon; int32_t alt; uint8_t has_pos, has_node;
        int      battery; uint8_t has_telem;
    };
    struct TxReq { uint32_t dest; uint8_t channel; uint32_t id; char text[200]; };

    void configure(uint32_t our_id, const char* short_name, const char* long_name) {
        our_id_ = our_id; our_short_ = short_name; our_long_ = long_name;
    }

    bool begin(const HAL::SX1262Pins& pins, const lora::Phy& phy, const char* primary_name) {
        seed_channels(primary_name);
        rxq_ = xQueueCreate(8, sizeof(RxEvent));
        txq_ = xQueueCreate(4, sizeof(TxReq));
        static HAL::SX1262 radio(pins);
        radio_ = &radio;
        if (!radio_->init()) { ESP_LOGW(TAG, "radio init failed (no LoRa hat?)"); radio_ = nullptr; return false; }
        radio_->setEventCallback([this](HAL::RadioEvent e) { on_radio_event(e); });
        pending_phy_ = phy; pending_ = true;
        xTaskCreate(&RadioMesh::task_tramp, "radio", 5120, this, 6, &task_);
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
        // Echo the outgoing message locally so the chat view shows it immediately.
        mesh::Message m;
        m.from_id = our_id_; m.from_name = our_short_;
        m.dest = dest; m.channel = channel; m.text = text;
        m.ts_ms = last_now_; m.outgoing = true; m.id = id; m.ack = mesh::ACK_NONE;
        emit(m);
        if (txq_) {
            TxReq r{}; r.dest = dest; r.channel = channel; r.id = id;
            std::strncpy(r.text, text.c_str(), sizeof r.text - 1);
            xQueueSend(txq_, &r, 0);
            if (task_) xTaskNotifyGive(task_);
        }
        return id;
    }

    // Drain decoded frames (main task): update node DB + emit messages.
    void poll(uint32_t now_ms) override {
        last_now_ = now_ms;
        if (!rxq_) return;
        RxEvent e;
        while (xQueueReceive(rxq_, &e, 0) == pdTRUE) apply_rx(e, now_ms);
    }

private:
    struct Channel { char name[24]; uint8_t key[32]; size_t keylen; uint8_t hash; };

    void seed_channels(const char* primary_name) {
        n_ch_ = 0;
        add_channel(primary_name && *primary_name ? primary_name : "LongFast", mtp::DEFAULT_PSK, 16);
        // nyme.sh secondaries (simple 1-byte PSKs). In a full build these come
        // from channel import (URL); seeded here so secondary traffic decodes.
        uint8_t wx = 0x59, ma = 0x99;       // "WQ==" , "mQ=="
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
    int channel_by_hash(uint8_t h) const {
        for (int i = 0; i < n_ch_; ++i) if (ch_[i].hash == h) return i;
        return -1;
    }

    static uint32_t now_ms() { return (uint32_t)(esp_timer_get_time() / 1000); }
    static void task_tramp(void* a) { static_cast<RadioMesh*>(a)->run(); }

    void run() {
        for (;;) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
            if (pending_) { pending_ = false; apply_phy(pending_phy_); }
            radio_->processEvents();
            // Drain one TX request per idle cycle (gated by tx_busy_).
            if (!tx_busy_ && txq_) {
                TxReq r;
                if (xQueueReceive(txq_, &r, 0) == pdTRUE) transmit_text(r);
            }
        }
    }

    void apply_phy(const lora::Phy& phy) {
        phy_ = phy;
        if (!phy.valid) { radio_->setMode(HAL::RadioMode::STANDBY); ESP_LOGW(TAG, "region UNSET — idle"); return; }
        HAL::LoRaConfig cfg = {};
        cfg.frequency_hz = phy.freq_hz; cfg.bandwidth_hz = phy.bw_hz;
        cfg.spreading_factor = phy.sf; cfg.coding_rate = phy.cr;
        cfg.tx_power_dbm = phy.tx_dbm; cfg.preamble_length = phy.preamble;
        cfg.sync_word = phy.sync; cfg.crc_enabled = phy.crc;
        cfg.implicit_header = false; cfg.iq_inverted = phy.iq_invert;
        cfg.rx_boosted_gain = phy.rx_boost;
        radio_->setConfig(cfg);
        radio_->startReceive(0);
        ESP_LOGI(TAG, "tuned @ %.3f MHz SF%d BW%lu ch%d", phy.freq_hz / 1e6, phy.sf,
                 (unsigned long)phy.bw_hz, phy.channel);
    }

    void on_radio_event(HAL::RadioEvent e) {
        switch (e) {
        case HAL::RadioEvent::TX_DONE:
            tx_busy_ = false; radio_->startReceive(0);
            if (task_) xTaskNotifyGive(task_);   // check for queued TX
            break;
        case HAL::RadioEvent::RX_DONE: {
            uint8_t buf[256]; HAL::RxPacketInfo info = {};
            int len = radio_->readPacket(buf, 255, &info);
            if (len > 0) decode_to_queue(buf, len, info);
            radio_->startReceive(0);
            break;
        }
        case HAL::RadioEvent::RX_TIMEOUT:
        case HAL::RadioEvent::RX_ERROR:
            radio_->startReceive(0);
            break;
        default: break;
        }
    }

    // Radio task: parse + decrypt + decode -> RxEvent -> queue.
    void decode_to_queue(const uint8_t* buf, int len, const HAL::RxPacketInfo& info) {
        mtp::Header h;
        if (!mtp::parse_header(buf, (size_t)len, h)) return;
        int paylen = len - (int)mtp::HEADER_LEN;
        if (paylen <= 0) return;
        int ci = channel_by_hash(h.channel_hash);
        if (ci < 0) { ESP_LOGD(TAG, "RX ch=0x%02x unknown/PKC", h.channel_hash); return; }

        uint8_t pt[256], nonce[16], stream[16] = {0};
        size_t nc_off = 0;
        mtp::make_nonce(h.src, h.id, nonce);
        mbedtls_aes_context ctx; mbedtls_aes_init(&ctx);
        mbedtls_aes_setkey_enc(&ctx, ch_[ci].key, (unsigned)ch_[ci].keylen * 8);
        mbedtls_aes_crypt_ctr(&ctx, (size_t)paylen, &nc_off, nonce, stream, buf + mtp::HEADER_LEN, pt);
        mbedtls_aes_free(&ctx);

        mtp::Data d;
        if (!mtp::parse_data(pt, (size_t)paylen, d)) return;   // wrong key / not for us

        RxEvent e{};
        e.from = h.src; e.to = h.dst; e.id = h.id;
        e.rssi = info.rssi; e.snr = info.snr;
        e.portnum = (uint8_t)d.portnum; e.channel_idx = (uint8_t)ci;
        e.battery = -1;
        if (d.portnum == 1 && d.payload_len) {                 // TEXT
            size_t n = d.payload_len < sizeof e.text - 1 ? d.payload_len : sizeof e.text - 1;
            std::memcpy(e.text, d.payload, n); e.text[n] = 0;
        } else if (d.portnum == 4 && d.payload_len) {          // NODEINFO
            mtp::NodeInfo ni;
            if (mtp::decode_nodeinfo(d.payload, d.payload_len, ni)) {
                std::strncpy(e.long_name, ni.long_name, sizeof e.long_name - 1);
                std::strncpy(e.short_name, ni.short_name, sizeof e.short_name - 1);
                e.has_node = 1;
            }
        } else if (d.portnum == 3 && d.payload_len) {          // POSITION
            mtp::Position p;
            if (mtp::decode_position(d.payload, d.payload_len, p) && p.has_latlon) {
                e.lat = p.lat; e.lon = p.lon; e.alt = p.alt; e.has_pos = 1;
            }
        } else if (d.portnum == 67 && d.payload_len) {         // TELEMETRY
            mtp::DeviceMetrics dm;
            if (mtp::decode_telemetry(d.payload, d.payload_len, dm) && dm.has) { e.battery = dm.battery; e.has_telem = 1; }
        }
        ESP_LOGI(TAG, "RX from=!%08lx %s ch%d %dB RSSI=%d", (unsigned long)e.from,
                 mtp::portnum_name(d.portnum), ci, paylen, (int)info.rssi);
        xQueueSend(rxq_, &e, 0);
    }

    // Radio task: encode + encrypt + transmit a queued text message.
    void transmit_text(const TxReq& r) {
        int ci = r.channel < (uint8_t)n_ch_ ? r.channel : 0;
        uint8_t data[220];
        size_t dlen = mtp::encode_text(data, sizeof data, r.text, std::strlen(r.text));
        if (!dlen) return;
        // AES-CTR encrypt the Data payload.
        uint8_t enc[220], nonce[16], stream[16] = {0};
        size_t nc_off = 0;
        mtp::make_nonce(our_id_, r.id, nonce);
        mbedtls_aes_context ctx; mbedtls_aes_init(&ctx);
        mbedtls_aes_setkey_enc(&ctx, ch_[ci].key, (unsigned)ch_[ci].keylen * 8);
        mbedtls_aes_crypt_ctr(&ctx, dlen, &nc_off, nonce, stream, data, enc);
        mbedtls_aes_free(&ctx);
        // Build frame = header || encrypted Data.
        uint8_t frame[256];
        mtp::Header h{}; h.dst = r.dest; h.src = our_id_; h.id = r.id;
        h.hop_limit = 3; h.hop_start = 3; h.channel_hash = ch_[ci].hash;
        mtp::write_header(frame, h);
        std::memcpy(frame + mtp::HEADER_LEN, enc, dlen);
        tx_busy_ = true;
        ESP_LOGI(TAG, "TX id=%08lx dst=!%08lx ch%d %uB", (unsigned long)r.id,
                 (unsigned long)r.dest, ci, (unsigned)(mtp::HEADER_LEN + dlen));
        radio_->transmit(frame, (uint8_t)(mtp::HEADER_LEN + dlen));
    }

    // Main task: fold a decoded event into the node DB + emit messages.
    void apply_rx(const RxEvent& e, uint32_t now_ms) {
        if (ign_.count(e.from)) return;                    // ignored node
        mesh::Node& n = upsert(e.from);
        n.last_heard_ms = now_ms;
        n.snr = (int)lroundf(e.snr);
        if (e.has_node) { n.long_name = e.long_name; n.short_name = e.short_name; }
        if (e.has_telem && e.battery >= 0) n.battery = e.battery;
        if (e.has_pos) { n.lat = e.lat; n.lon = e.lon; n.has_pos = true; }

        if (e.portnum == 1 && e.text[0]) {                 // TEXT -> chat + notify
            mesh::Message m;
            m.from_id = e.from; m.from_name = name_of(e.from);
            m.dest = e.to; m.channel = e.channel_idx; m.text = e.text;
            m.ts_ms = now_ms; m.outgoing = false;
            emit(m);
        }
    }

    mesh::Node& upsert(uint32_t id) {
        for (auto& n : nodes_) if (n.id == id) return n;
        if (nodes_.size() >= MAX_NODES) nodes_.erase(nodes_.begin());  // drop oldest-seen
        mesh::Node n{}; n.id = id;
        char b[12]; std::snprintf(b, sizeof b, "!%08lx", (unsigned long)id);
        n.long_name = b; n.short_name = std::string(b + 5);  // last 4 hex
        nodes_.push_back(n);
        return nodes_.back();
    }
    std::string name_of(uint32_t id) {
        for (auto& n : nodes_) if (n.id == id) return n.short_name.empty() ? n.long_name : n.short_name;
        char b[12]; std::snprintf(b, sizeof b, "!%08lx", (unsigned long)id); return b;
    }
    void emit(const mesh::Message& m) { for (auto& s : subs_) s(m); }

    HAL::SX1262* radio_ = nullptr;
    TaskHandle_t task_ = nullptr;
    QueueHandle_t rxq_ = nullptr, txq_ = nullptr;
    volatile bool tx_busy_ = false, pending_ = false;
    lora::Phy pending_phy_, phy_;

    Channel ch_[MAX_CH]; int n_ch_ = 0;
    std::vector<mesh::Node> nodes_;
    std::set<uint32_t> favs_, ign_;
    std::vector<mesh::Subscriber> subs_;
    mesh::AckCallback ack_cb_;
    uint32_t our_id_ = 0, last_now_ = 0, next_id_ = 1;
    std::string our_short_ = "DECK", our_long_ = "Cardputer Deck";
};

} // namespace device
