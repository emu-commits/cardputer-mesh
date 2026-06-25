#include "core/lora_phy.h"
#include "core/settings.h"
#include <cmath>
#include <cstring>
#include <string>

namespace lora {

// Meshtastic region band table (freqStart, freqEnd in MHz, channel spacing MHz).
struct Region { const char* name; float start; float end; float spacing; };
static const Region kRegions[] = {
    {"US",     902.0f, 928.0f,  0.0f},
    {"EU_433", 433.0f, 434.0f,  0.0f},
    {"EU_868", 869.4f, 869.65f, 0.0f},
    {"CN",     470.0f, 510.0f,  0.0f},
    {"JP",     920.8f, 927.8f,  0.0f},
    {"ANZ",    915.0f, 928.0f,  0.0f},
    {"KR",     920.0f, 923.0f,  0.0f},
    {"TW",     920.0f, 925.0f,  0.0f},
    {"RU",     868.7f, 869.2f,  0.0f},
    {"IN",     865.0f, 867.0f,  0.0f},
    {"NZ_865", 864.0f, 868.0f,  0.0f},
    {"TH",     920.0f, 925.0f,  0.0f},
    {"UA_433", 433.0f, 434.7f,  0.0f},
    {"UA_868", 868.0f, 868.6f,  0.0f},
    {"MY_919", 919.0f, 924.0f,  0.0f},
    {"SG_923", 917.0f, 925.0f,  0.0f},
    {"BR_902", 902.0f, 907.5f,  0.0f},
    {"LORA_24",2400.0f,2483.5f, 0.0f},
};

// Meshtastic modem-preset table -> (bandwidth Hz, spreading factor, coding rate 4/N).
struct Preset { const char* name; uint32_t bw_hz; uint8_t sf; uint8_t cr; };
static const Preset kPresets[] = {
    {"ShortTurbo",   500000, 7,  5},
    {"ShortFast",    250000, 7,  5},
    {"ShortSlow",    250000, 8,  5},
    {"MediumFast",   250000, 9,  5},
    {"MediumSlow",   250000, 10, 5},
    {"LongFast",     250000, 11, 5},
    {"LongModerate", 125000, 11, 8},
    {"LongSlow",     125000, 12, 8},
    {"VeryLongSlow",  62500, 12, 8},
    {"LongTurbo",    500000, 11, 5},
};

Phy phy_from_settings(const cfg::Settings& s) {
    Phy p;
    std::string region = s.get("lora", "region");
    std::string preset = s.get("lora", "modem_preset");

    const Region* rg = nullptr;
    for (auto& r : kRegions) if (region == r.name) { rg = &r; break; }
    if (!rg) return p;   // UNSET / unknown -> invalid (don't key up)

    // Preset -> bw/sf/cr (Custom pulls the manual bandwidth/SF/CR fields).
    if (preset == "Custom") {
        long bw_khz = s.get_num("lora", "bandwidth");
        p.bw_hz = (uint32_t)(bw_khz > 0 ? bw_khz * 1000 : 250000);
        p.sf = (uint8_t)s.get_num("lora", "spread_factor");
        p.cr = (uint8_t)s.get_num("lora", "coding_rate");
        if (p.sf < 7 || p.sf > 12) p.sf = 11;
        if (p.cr < 5 || p.cr > 8) p.cr = 5;
    } else {
        const Preset* pr = nullptr;
        for (auto& q : kPresets) if (preset == q.name) { pr = &q; break; }
        if (!pr) pr = &kPresets[5]; // default LongFast
        p.bw_hz = pr->bw_hz; p.sf = pr->sf; p.cr = pr->cr;
    }

    // Channel slot -> frequency (Meshtastic formula).
    float bw_mhz = p.bw_hz / 1000000.0f;
    int num = (int)std::floor((rg->end - rg->start) / (rg->spacing + bw_mhz));
    if (num < 1) num = 1;
    long slot = s.get_num("lora", "freq_slot");      // 1-based; 0 = first channel
    int ch = (int)((slot > 0 ? slot - 1 : 0) % num);
    float freq_mhz = rg->start + bw_mhz / 2.0f + ch * bw_mhz;

    p.num_channels = num;
    p.channel = ch;
    p.freq_hz = (uint32_t)llround(freq_mhz * 1000000.0);
    p.tx_dbm = (int8_t)s.get_num("lora", "tx_power");
    p.rx_boost = s.get_bool("lora", "rx_boost");
    p.valid = true;
    return p;
}

} // namespace lora
