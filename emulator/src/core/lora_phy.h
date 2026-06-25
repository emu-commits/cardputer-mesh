// LoRa PHY derivation — turn the human-facing LoRa settings (region + modem
// preset + channel slot) into the concrete radio parameters, exactly the way
// Meshtastic does it, so the Cardputer lands on the same channel as the mesh.
//
//   numChannels = floor((freqEnd - freqStart) / (spacing + bw_MHz))
//   channel     = (slot ? slot-1 : 0) % numChannels      // slot is 1-based; 0 = ch0
//   freq        = freqStart + bw_MHz/2 + channel * bw_MHz
//
// Region sets the band (freqStart/freqEnd); the preset sets bandwidth/SF/CR; the
// slot picks the channel within the band. (Verified on nyme.sh: US + MediumSlow
// + slot 48 -> 913.875 MHz, SF10/BW250.)
#pragma once
#include <cstdint>

namespace cfg { class Settings; }

namespace lora {

struct Phy {
    bool     valid = false;   // false if region is UNSET / unknown
    uint32_t freq_hz = 0;
    uint32_t bw_hz = 250000;
    uint8_t  sf = 11;
    uint8_t  cr = 5;          // 4/5 .. 4/8
    uint16_t preamble = 16;   // Meshtastic preamble symbols
    uint8_t  sync = 0x2B;     // Meshtastic LoRa sync word
    bool     crc = true;
    bool     iq_invert = false;
    bool     rx_boost = false;
    int8_t   tx_dbm = 22;
    int      channel = 0;     // resolved 0-based channel index (for logging)
    int      num_channels = 0;
};

// Compute the PHY from the "lora" settings group. Returns {valid=false} when the
// region is UNSET/unknown (caller should not key up).
Phy phy_from_settings(const cfg::Settings& s);

// True if two PHYs would drive the radio identically (used to detect when a
// settings change actually needs a retune).
inline bool phy_same(const Phy& a, const Phy& b) {
    return a.valid == b.valid && a.freq_hz == b.freq_hz && a.bw_hz == b.bw_hz &&
           a.sf == b.sf && a.cr == b.cr && a.tx_dbm == b.tx_dbm && a.rx_boost == b.rx_boost;
}

} // namespace lora
