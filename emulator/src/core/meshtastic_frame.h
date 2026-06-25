// Meshtastic LoRa frame decode (portable: parsing + nonce, no crypto).
//
// An over-the-air Meshtastic packet is a 16-byte cleartext header followed by an
// encrypted payload (the Data protobuf). The header gives from/to/id and the
// channel hash; the payload is AES-CTR encrypted with the channel PSK and a
// nonce derived from (packet_id, sender). Channel/broadcast traffic uses the
// channel PSK (the primary channel is the well-known default key); per-node DMs
// use PKC (X25519) and can't be read without the key pair.
//
// This header does the portable, crypto-free parts: header parse, the CTR nonce,
// the default PSK, the Data protobuf parse, and portnum names. The AES-CTR
// decrypt itself is done by the caller (device: mbedtls).
#pragma once
#include <cstddef>
#include <cstdint>

namespace mtp {

constexpr size_t HEADER_LEN = 16;

struct Header {
    uint32_t dst = 0;          // destination node (!xxxxxxxx); 0xFFFFFFFF = broadcast
    uint32_t src = 0;          // sender node
    uint32_t id = 0;           // packet id
    uint8_t  channel_hash = 0; // which channel (hash of name+psk); 0 often = PKC/DM
    uint8_t  hop_limit = 0;
    uint8_t  hop_start = 0;
    bool     want_ack = false;
    bool     via_mqtt = false;
};

// Parse the 16-byte cleartext header. Returns false if too short.
bool parse_header(const uint8_t* p, size_t len, Header& h);

// Build the 16-byte AES-CTR nonce/IV: packetId (LE, 8 bytes) || sender (LE, 4) || 0.
void make_nonce(uint32_t from_node, uint32_t packet_id, uint8_t nonce[16]);

// The well-known Meshtastic default channel key (psk "AQ==" / primary channel).
extern const uint8_t DEFAULT_PSK[16];

struct Data {
    uint32_t       portnum = 0;
    const uint8_t* payload = nullptr;
    size_t         payload_len = 0;
    uint32_t       request_id = 0;   // Data.request_id (field 6) — set on ROUTING acks
};

// Parse a Routing protobuf's error_reason (field 3 varint). Returns 0 for an
// implicit/explicit ACK (NONE), >0 for a NAK reason, -1 if absent.
int routing_error(const uint8_t* p, size_t len);

// Parse a decrypted Data protobuf (field 1 = portnum varint, field 2 = payload
// bytes). Returns false on malformed input or if no portnum — a good signal that
// the decrypt used the wrong key (wrong channel / PKC).
bool parse_data(const uint8_t* p, size_t len, Data& d);

// Human-readable PortNum name (the common ones), else "PORT_n".
const char* portnum_name(uint32_t pn);

// --- inner-payload protobuf decoders (for the common broadcast portnums) ---

// --- channel keys (for multi-channel decode + TX) ---

// Expand a Meshtastic PSK into the AES key. A 1-byte "simple" PSK selects a key
// derived from the default (last byte += psk-1); 16/32-byte PSKs are used as-is.
// Returns the key length (16 or 32).
size_t expand_psk(const uint8_t* psk, size_t n, uint8_t key[32]);

// Channel hash = xor(name bytes) ^ xor(key bytes). The header's channel byte is
// this hash, so it tells which configured channel a frame belongs to. For the
// primary channel with an empty name, Meshtastic hashes the modem-preset name.
uint8_t channel_hash(const char* name, const uint8_t* key, size_t keylen);

// --- TX encode ---

// Encode a Data protobuf carrying a text message (portnum=TEXT_MESSAGE).
// Returns bytes written, or 0 if it won't fit.
size_t encode_text(uint8_t* out, size_t cap, const char* text, size_t tlen);

// Write the 16-byte cleartext header. Returns 16.
size_t write_header(uint8_t* out, const Header& h);

struct NodeInfo {                 // NODEINFO_APP (4): User protobuf
    char    id[16] = {0};         // "!093fb19a"
    char    long_name[40] = {0};
    char    short_name[8] = {0};
    uint8_t public_key[32] = {0}; // X25519 pubkey (User.public_key, field 8)
    bool    has_key = false;
};
bool decode_nodeinfo(const uint8_t* p, size_t len, NodeInfo& n);

// Encode a Data protobuf carrying our own User (NODEINFO_APP) so peers learn our
// name + public key (pubkey optional; pass nullptr to omit). Returns bytes.
size_t encode_nodeinfo(uint8_t* out, size_t cap, const char* id, const char* long_name,
                       const char* short_name, const uint8_t* pubkey32);

struct Position {                 // POSITION_APP (3)
    double  lat = 0, lon = 0;     // degrees
    int32_t alt = 0;              // metres
    bool    has_latlon = false;
};
bool decode_position(const uint8_t* p, size_t len, Position& pos);

struct DeviceMetrics {            // TELEMETRY_APP (67): Telemetry.device_metrics
    int   battery = -1;           // percent (-1 = absent)
    float voltage = 0;
    float ch_util = 0;
    float air_util = 0;
    bool  has = false;
};
bool decode_telemetry(const uint8_t* p, size_t len, DeviceMetrics& dm);

} // namespace mtp
