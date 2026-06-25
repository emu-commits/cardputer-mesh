#include "core/meshtastic_frame.h"
#include <cstring>

namespace mtp {

// Meshtastic's default channel PSK (used by the primary channel, psk "AQ==").
const uint8_t DEFAULT_PSK[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01};

static uint32_t rd_u32le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr_u32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

size_t expand_psk(const uint8_t* psk, size_t n, uint8_t key[32]) {
    if (n == 1) {                              // "simple" 1-byte key
        std::memcpy(key, DEFAULT_PSK, 16);
        key[15] = (uint8_t)(DEFAULT_PSK[15] + psk[0] - 1);
        return 16;
    }
    if (n == 32) { std::memcpy(key, psk, 32); return 32; }
    if (n == 16) { std::memcpy(key, psk, 16); return 16; }
    std::memcpy(key, DEFAULT_PSK, 16);         // unknown/empty -> default
    return 16;
}

uint8_t channel_hash(const char* name, const uint8_t* key, size_t keylen) {
    uint8_t h = 0;
    for (const char* p = name; p && *p; ++p) h ^= (uint8_t)*p;
    for (size_t i = 0; i < keylen; ++i) h ^= key[i];
    return h;
}

size_t encode_text(uint8_t* out, size_t cap, const char* text, size_t tlen) {
    if (cap < tlen + 8) return 0;
    size_t i = 0;
    out[i++] = 0x08; out[i++] = 0x01;          // field1 portnum = 1 (TEXT_MESSAGE)
    out[i++] = 0x12;                            // field2 payload, wiretype 2
    size_t l = tlen;
    do { uint8_t b = l & 0x7f; l >>= 7; if (l) b |= 0x80; out[i++] = b; } while (l);
    std::memcpy(out + i, text, tlen); i += tlen;
    return i;
}

size_t write_header(uint8_t* out, const Header& h) {
    wr_u32le(out + 0, h.dst);
    wr_u32le(out + 4, h.src);
    wr_u32le(out + 8, h.id);
    out[12] = (uint8_t)((h.hop_limit & 7) | (h.want_ack ? 8 : 0) | (h.via_mqtt ? 16 : 0) | ((h.hop_start & 7) << 5));
    out[13] = h.channel_hash;
    out[14] = 0;   // next_hop
    out[15] = 0;   // relay_node
    return HEADER_LEN;
}

bool parse_header(const uint8_t* p, size_t len, Header& h) {
    if (len < HEADER_LEN) return false;
    h.dst = rd_u32le(p + 0);
    h.src = rd_u32le(p + 4);
    h.id  = rd_u32le(p + 8);
    uint8_t flags = p[12];
    h.channel_hash = p[13];
    // p[14] = next_hop, p[15] = relay_node (newer firmware) — not needed here.
    h.hop_limit = flags & 0x07;
    h.want_ack  = (flags >> 3) & 0x01;
    h.via_mqtt  = (flags >> 4) & 0x01;
    h.hop_start = (flags >> 5) & 0x07;
    return true;
}

void make_nonce(uint32_t from_node, uint32_t packet_id, uint8_t nonce[16]) {
    std::memset(nonce, 0, 16);
    nonce[0] = (uint8_t)(packet_id);
    nonce[1] = (uint8_t)(packet_id >> 8);
    nonce[2] = (uint8_t)(packet_id >> 16);
    nonce[3] = (uint8_t)(packet_id >> 24);
    // bytes 4..7 are the high 32 bits of the 64-bit packet id == 0
    nonce[8]  = (uint8_t)(from_node);
    nonce[9]  = (uint8_t)(from_node >> 8);
    nonce[10] = (uint8_t)(from_node >> 16);
    nonce[11] = (uint8_t)(from_node >> 24);
    // bytes 12..15 = extra nonce == 0
}

static bool read_varint(const uint8_t*& p, const uint8_t* end, uint64_t& v) {
    v = 0;
    int shift = 0;
    while (p < end && shift < 64) {
        uint8_t b = *p++;
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) return true;
        shift += 7;
    }
    return false;
}

bool parse_data(const uint8_t* buf, size_t len, Data& d) {
    d = Data{};
    const uint8_t* p = buf;
    const uint8_t* end = buf + len;
    bool got_port = false;
    while (p < end) {
        uint64_t tag;
        if (!read_varint(p, end, tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3);
        uint32_t wt = (uint32_t)(tag & 0x07);
        if (wt == 0) {                               // varint
            uint64_t v;
            if (!read_varint(p, end, v)) return false;
            if (field == 1) { d.portnum = (uint32_t)v; got_port = true; }
        } else if (wt == 2) {                        // length-delimited
            uint64_t l;
            if (!read_varint(p, end, l)) return false;
            if ((size_t)(end - p) < l) return false;
            if (field == 2) { d.payload = p; d.payload_len = (size_t)l; }
            p += l;
        } else if (wt == 5) {                        // 32-bit
            if (end - p < 4) return false;
            p += 4;
        } else if (wt == 1) {                        // 64-bit
            if (end - p < 8) return false;
            p += 8;
        } else {
            return false;                            // 3/4 (groups) unsupported
        }
    }
    return got_port;
}

// Read a little-endian 32-bit fixed field (protobuf wiretype 5).
static bool read_fixed32(const uint8_t*& p, const uint8_t* end, uint32_t& v) {
    if (end - p < 4) return false;
    v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    p += 4;
    return true;
}
static float as_float(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }

// Copy a length-delimited string field into a fixed buffer (NUL-terminated).
static void copy_str(const uint8_t* s, size_t n, char* out, size_t cap) {
    size_t m = n < cap - 1 ? n : cap - 1;
    std::memcpy(out, s, m);
    out[m] = 0;
}

// Generic field skip for fields we don't care about (advances p past the value).
static bool skip_field(const uint8_t*& p, const uint8_t* end, uint32_t wt) {
    if (wt == 0) { uint64_t v; return read_varint(p, end, v); }
    if (wt == 2) { uint64_t l; if (!read_varint(p, end, l)) return false; if ((size_t)(end - p) < l) return false; p += l; return true; }
    if (wt == 5) { return (end - p >= 4) ? (p += 4, true) : false; }
    if (wt == 1) { return (end - p >= 8) ? (p += 8, true) : false; }
    return false;
}

bool decode_nodeinfo(const uint8_t* buf, size_t len, NodeInfo& n) {
    const uint8_t* p = buf; const uint8_t* end = buf + len;
    bool any = false;
    while (p < end) {
        uint64_t tag; if (!read_varint(p, end, tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3), wt = (uint32_t)(tag & 7);
        if (wt == 2 && (field == 1 || field == 2 || field == 3)) {
            uint64_t l; if (!read_varint(p, end, l)) return false;
            if ((size_t)(end - p) < l) return false;
            if (field == 1) copy_str(p, (size_t)l, n.id, sizeof n.id);
            else if (field == 2) copy_str(p, (size_t)l, n.long_name, sizeof n.long_name);
            else copy_str(p, (size_t)l, n.short_name, sizeof n.short_name);
            p += l; any = true;
        } else if (!skip_field(p, end, wt)) return false;
    }
    return any;
}

bool decode_position(const uint8_t* buf, size_t len, Position& pos) {
    const uint8_t* p = buf; const uint8_t* end = buf + len;
    bool any = false;
    while (p < end) {
        uint64_t tag; if (!read_varint(p, end, tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3), wt = (uint32_t)(tag & 7);
        if (field == 1 && wt == 5) { uint32_t v; if (!read_fixed32(p, end, v)) return false; pos.lat = (int32_t)v * 1e-7; pos.has_latlon = true; any = true; }
        else if (field == 2 && wt == 5) { uint32_t v; if (!read_fixed32(p, end, v)) return false; pos.lon = (int32_t)v * 1e-7; any = true; }
        else if (field == 3 && wt == 0) { uint64_t v; if (!read_varint(p, end, v)) return false; pos.alt = (int32_t)(int64_t)v; any = true; }
        else if (!skip_field(p, end, wt)) return false;
    }
    return any;
}

bool decode_telemetry(const uint8_t* buf, size_t len, DeviceMetrics& dm) {
    // Telemetry { time=1; device_metrics=2 (submessage) }. We pull device_metrics.
    const uint8_t* p = buf; const uint8_t* end = buf + len;
    const uint8_t* sub = nullptr; size_t sublen = 0;
    while (p < end) {
        uint64_t tag; if (!read_varint(p, end, tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3), wt = (uint32_t)(tag & 7);
        if (field == 2 && wt == 2) { uint64_t l; if (!read_varint(p, end, l)) return false; if ((size_t)(end - p) < l) return false; sub = p; sublen = (size_t)l; p += l; }
        else if (!skip_field(p, end, wt)) return false;
    }
    if (!sub) return false;
    // DeviceMetrics { battery_level=1 varint; voltage=2 float; channel_utilization=3 float; air_util_tx=4 float; ... }
    const uint8_t* q = sub; const uint8_t* qend = sub + sublen;
    while (q < qend) {
        uint64_t tag; if (!read_varint(q, qend, tag)) return false;
        uint32_t field = (uint32_t)(tag >> 3), wt = (uint32_t)(tag & 7);
        if (field == 1 && wt == 0) { uint64_t v; if (!read_varint(q, qend, v)) return false; dm.battery = (int)v; }
        else if (field == 2 && wt == 5) { uint32_t v; if (!read_fixed32(q, qend, v)) return false; dm.voltage = as_float(v); }
        else if (field == 3 && wt == 5) { uint32_t v; if (!read_fixed32(q, qend, v)) return false; dm.ch_util = as_float(v); }
        else if (field == 4 && wt == 5) { uint32_t v; if (!read_fixed32(q, qend, v)) return false; dm.air_util = as_float(v); }
        else if (!skip_field(q, qend, wt)) return false;
    }
    dm.has = true;
    return true;
}

const char* portnum_name(uint32_t pn) {
    switch (pn) {
    case 0:  return "UNKNOWN";
    case 1:  return "TEXT_MESSAGE";
    case 2:  return "REMOTE_HARDWARE";
    case 3:  return "POSITION";
    case 4:  return "NODEINFO";
    case 5:  return "ROUTING";
    case 6:  return "ADMIN";
    case 7:  return "TEXT_MESSAGE_COMPRESSED";
    case 8:  return "WAYPOINT";
    case 9:  return "AUDIO";
    case 10: return "DETECTION_SENSOR";
    case 32: return "REPLY";
    case 33: return "IP_TUNNEL";
    case 34: return "PAXCOUNTER";
    case 64: return "SERIAL";
    case 65: return "STORE_FORWARD";
    case 66: return "RANGE_TEST";
    case 67: return "TELEMETRY";
    case 68: return "ZPS";
    case 70: return "TRACEROUTE";
    case 71: return "NEIGHBORINFO";
    case 72: return "ATAK_PLUGIN";
    case 73: return "MAP_REPORT";
    case 257: return "ATAK_FORWARDER";
    default: return "PORT_?";
    }
}

} // namespace mtp
